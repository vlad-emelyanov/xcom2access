// movie_subtitles.cpp — the subtitles the game draws over a full-screen `.bk2` movie.
//
// ⛔ CATCH THE LINE THE ENGINE DRAWS; DO NOT LOOK IT UP. The engine keeps a movie's subtitle
// nowhere: every pump, its movie thread picks the cue for the current frame, localizes it, blanks
// it if subtitles are off, hands it to the frame draw, and frees it. So there is exactly one place
// the finished line exists — the draw call's argument — and this file takes it there, with an
// inline hook on that draw.
//
// Every alternative was built first and each one failed for a reason worth keeping:
//   * Rebuilding the line ourselves (read the cue track, localize the key) is a second subtitle
//     system that agrees with the game only by luck — its own option gate, its own language, its
//     own idea of which pass of a looping movie is running.
//   * Localizing the cue key on the game thread CRASHED the game: `Localize` walks GConfig with no
//     lock, the movie thread is inside the same walk every pump, and one concurrent call froze
//     `CIN_XP_Stronghold_Warlock` on the Chosen's first line (minidump
//     `XCom-…-2026.09.30-02.09.03`, `scalable_realloc` under `FUN_140163d80`).
//   * Localizing every key AHEAD of the movie avoided the race but made a cache, and the cache
//     silently lost the last 128 cues of the file for a whole session, because its first batches
//     ran before `Object.Localize` was resolvable and stored the empty answers as "no words".
// The hook has none of those failure modes: it calls nothing, it caches nothing, and it hears
// exactly the string the player is about to see — option gate, language, force flag and all.
//
// ---------------------------------------------------------------------------------------------
// The hook (MinHook; the ONE inline hook in the mod — CLAUDE.md, reverse-engineering workflow)
//
// `FUN_1402432d0` (Steam; GOG `FUN_1402434f0`) is the per-frame Bink draw. Its single caller is
// PumpMovie (`FUN_14023ed10`), and the arguments are, measured on both builds' call sites:
//   RCX  the renderer (`player+0x104`)          RDX  the BINKHANDLE (`player+0x84`)
//   R8   the subtitle line, "" when none        R9   the "movie is paused" caption, or null
//   [rsp+0x20..0x30]  three more, passed through untouched
// PumpMovie has already applied the option gate to R8 (engine `+0x46C` bit 0x10, unless the
// movie's flag 0x80 forces subtitles), so an empty line here means the screen shows none either.
// It is a DIRECT call — no vtable slot, no `UFunction::Func` — which is why a pointer swap cannot
// reach it and this is the case CLAUDE.md reserved MinHook for.
//
// The target address is per build (`GameBuild`), and it is VALIDATED before MinHook touches it,
// because patching the wrong bytes is a crash, not a wrong word: the prologue must be the exact 25
// bytes both builds open with, and the instruction at +0x62 must load the address of the string
// "RenderFMV" — the GPU-profiler label this function and no other passes. Either check failing
// disables the surface with one log line.
//
// ⛔ The detour runs ON THE MOVIE THREAD, so it obeys the observer rule: RECORD, nothing else. No
// engine call, no speech, no log, no UObject. It copies the line into a slot under a lock only
// when it differs from the last one, and `OnTick` — on the game thread — speaks what changed.
//
// ---------------------------------------------------------------------------------------------
// The movie player object, kept for DIAGNOSTICS only
//
// `FFullScreenMovieBink` is a raw C++ object behind the `GFullScreenMovie` global — no UObject, no
// reflection. Nothing spoken depends on it any more; it is read so the log can say which movie
// opened and whether the engine loaded a cue track for it (`PlayMovie`, `FUN_14023d7a0`, loads
// `Movies\<name>.txt` only while the global at 0x141E82224 is set, and a Kismet
// `SeqAct_ControlGameMovie` never sets it — so "no cues" is the engine's own choice and has to be
// told apart from a broken reader). The global is validated by its VTABLE before any further read.
//
// Addresses are Ghidra VAs minus ImageBase 0x140000000, as everywhere else in this project.

#include "movie_subtitles.h"

#include "game_build.h"
#include "log.h"
#include "narrator.h"
#include "script_call.h"
#include "sdk_globals.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <MinHook.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

namespace MovieSubtitles {

namespace {

// ---- FFullScreenMovieBink, the fields the diagnostics read ----

constexpr int32_t kPlayerName = 0x40;       // FString, the movie being played
constexpr int32_t kPlayerBinkHandle = 0x84; // HBINK — null exactly when no movie is up
constexpr int32_t kPlayerCueTrack = 0x114;  // the parsed <name>.txt

// The cue track (0x30 bytes) and its groups — enough to say how many cues the engine loaded.
constexpr int32_t kTrackGroups = 0x00;
constexpr int32_t kTrackGroupCount = 0x08;
constexpr int32_t kTrackGroupIndex = 0x10; // -1 when nothing is loaded
constexpr int32_t kGroupStride = 0x28;
constexpr int32_t kGroupMode = 0x10; // 0 = timed cues; anything else is the random picker
constexpr int32_t kGroupEntryCount = 0x20;
constexpr int32_t kMaxPlausibleGroups = 1024;

// ---- the draw's pin ----
//
// The first 25 bytes of the draw, identical on Steam and GOG: `mov rax,rsp; mov [rax+18h],r8;`
// seven pushes; `sub rsp,0B0h`. Not unique on its own — plenty of functions open this way — which
// is why the RenderFMV load below is checked too.
constexpr uint8_t kDrawPrologue[] = {
    0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00,
};
// `lea r8, [rip+disp32]` loading L"RenderFMV", at this offset on both builds.
constexpr int32_t kRenderFmvLea = 0x62;
constexpr uint8_t kLeaR8Rip[] = {0x4C, 0x8D, 0x05};
constexpr wchar_t kRenderFmv[] = L"RenderFMV";

// A subtitle line is whatever the translator wrote, so its length is the GAME's to decide — this
// is a runaway guard on a pointer read out of a register, not a working limit (the longest stock
// line is under 400 characters), and tripping it is reported rather than truncated.
constexpr size_t kMaxPlausibleLineChars = 8192;

// 100 ms, matching the narrator drain. Every stock cue is at least a second long.
constexpr uint64_t kPollIntervalMs = 100;

// ---- SEH-guarded primitives ----
//
// ⛔ Each guarded read is its OWN function with no C++ object in scope — MSVC refuses __try in a
// frame that needs unwinding (C2712).

int32_t ReadI32(const void* base, int32_t offset)
{
    __try {
        return *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void* ReadPtr(const void* base, int32_t offset)
{
    __try {
        return *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadFStringHeader(const void* base, int32_t offset, const wchar_t** data, int32_t* count)
{
    __try {
        auto* fstr = reinterpret_cast<const uint8_t*>(base) + offset;
        *data = *reinterpret_cast<const wchar_t* const*>(fstr);
        *count = *reinterpret_cast<const int32_t*>(fstr + 8);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CopyWideRaw(const wchar_t* src, wchar_t* dst, size_t len)
{
    __try {
        memcpy(dst, src, len * sizeof(wchar_t));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool BytesEqualRaw(const void* at, const void* expected, size_t n)
{
    __try {
        return memcmp(at, expected, n) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

/// Length of a NUL-terminated engine string, or SIZE_MAX when it faults or runs past the guard.
size_t WideLengthRaw(const wchar_t* s)
{
    __try {
        for (size_t n = 0; n <= kMaxPlausibleLineChars; n++) {
            if (s[n] == L'\0')
                return n;
        }
        return SIZE_MAX;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return SIZE_MAX;
    }
}

bool WideEqualRaw(const wchar_t* engine, const wchar_t* ours, size_t len)
{
    __try {
        return wmemcmp(engine, ours, len) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string WideToUtf8(const wchar_t* s, size_t len)
{
    if (len == 0)
        return {};
    const int n = static_cast<int>(len);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, s, n, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {};
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, n, out.data(), bytes, nullptr, nullptr);
    return out;
}

// The movie name is an identifier the game wrote, bounded — but sized from its count anyway,
// because the count is what tells a valid read from a wild pointer.
constexpr int32_t kMaxPlausibleNameChars = 512;

std::string ReadFString(const void* base, int32_t offset)
{
    const wchar_t* data = nullptr;
    int32_t count = 0;
    if (!ReadFStringHeader(base, offset, &data, &count) || !data || count <= 1
        || count - 1 > kMaxPlausibleNameChars)
        return {};
    std::wstring wide(static_cast<size_t>(count - 1), L'\0');
    if (!CopyWideRaw(data, wide.data(), wide.size()))
        return {};
    return WideToUtf8(wide.data(), wide.size());
}

// ---- the slot the movie thread writes and the game thread reads ----
//
// One writer (the detour) and one reader (OnTick), under an SRW lock that the writer takes only on
// a CHANGE — a held line is compared without the lock's help being needed by anyone else, and the
// reader takes it ten times a second for a few instructions.

SRWLOCK gSlotLock = SRWLOCK_INIT;
std::wstring gSlotLine;         // the last line the engine drew, raw
const void* gSlotBink = nullptr; // ...and the movie it was drawn over
uint64_t gSlotSeq = 0;          // bumped on every change, "" included
bool gSlotOversize = false;     // the guard tripped; OnTick reports it (the detour may not log)

using DrawFn = void(__fastcall*)(void* renderer, void* bink, const wchar_t* subtitle,
    const wchar_t* paused, uintptr_t a5, uintptr_t a6, uintptr_t a7);
DrawFn gOriginalDraw = nullptr;

/// The movie-thread half. ⛔ Record only — see the header comment.
void RecordLine(const void* bink, const wchar_t* subtitle)
{
    const size_t len = subtitle ? WideLengthRaw(subtitle) : 0;

    AcquireSRWLockExclusive(&gSlotLock);
    if (len == SIZE_MAX) {
        gSlotOversize = true;
    } else if (bink != gSlotBink || len != gSlotLine.size()
               || !WideEqualRaw(subtitle, gSlotLine.data(), len)) {
        try {
            gSlotLine.assign(len, L'\0');
            if (len && !CopyWideRaw(subtitle, gSlotLine.data(), len))
                gSlotLine.clear();
            gSlotBink = bink;
            gSlotSeq++;
        } catch (...) {
            // bad_alloc on the movie thread: drop this frame's line; the next frame retries.
        }
    }
    ReleaseSRWLockExclusive(&gSlotLock);
}

// The two extra stack arguments are declared and forwarded as full slots: the caller writes all
// three (`[rsp+20h]`, `[rsp+28h]`, `[rsp+30h]`), and passing them on unchanged is all a detour owes.
void __fastcall DrawDetour(void* renderer, void* bink, const wchar_t* subtitle,
    const wchar_t* paused, uintptr_t a5, uintptr_t a6, uintptr_t a7)
{
    RecordLine(bink, subtitle);
    gOriginalDraw(renderer, bink, subtitle, paused, a5, a6, a7);
}

// ---- install ----

bool gInstallTried = false;
bool gHooked = false;

/// Validate the build's draw address, then hook it. Once per session, on the game thread.
void InstallHook()
{
    gInstallTried = true;
    const uintptr_t base = SDK::GetModuleBase();
    if (!base) {
        gInstallTried = false; // too early; try again next tick
        return;
    }
    const GameBuild::Build& build = GameBuild::Current();
    auto* target = reinterpret_cast<uint8_t*>(base + build.movieSubtitleDraw);

    // The pin. A wrong address here is a patched instruction stream in the middle of someone
    // else's function, so both checks must pass before MinHook is even initialised.
    bool pinned = BytesEqualRaw(target, kDrawPrologue, sizeof(kDrawPrologue))
        && BytesEqualRaw(target + kRenderFmvLea, kLeaR8Rip, sizeof(kLeaR8Rip));
    if (pinned) {
        const int32_t disp = ReadI32(target, kRenderFmvLea + 3);
        const auto* label = reinterpret_cast<const wchar_t*>(target + kRenderFmvLea + 7 + disp);
        pinned = BytesEqualRaw(label, kRenderFmv, sizeof(kRenderFmv));
    }
    if (!pinned) {
        LOG_ERROR("MovieSubtitles: DISABLED — 0x%p is not the subtitle draw on this executable "
                  "(%s; prologue or RenderFMV label did not match). Movie subtitles will not be "
                  "spoken; re-derive the address with tools/derive_offsets.py",
            target, GameBuild::Fingerprint());
        return;
    }

    // MinHook is initialised here rather than at startup because this is its only user; a second
    // hook would move this to one place. ⛔ Never uninitialised: MH_Uninitialize suspends threads,
    // and the only place it could run is DllMain's detach, under the loader lock (CLAUDE.md).
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR("MovieSubtitles: DISABLED — MH_Initialize failed: %s", MH_StatusToString(st));
        return;
    }
    st = MH_CreateHook(target, reinterpret_cast<LPVOID>(&DrawDetour),
        reinterpret_cast<LPVOID*>(&gOriginalDraw));
    if (st != MH_OK) {
        LOG_ERROR("MovieSubtitles: DISABLED — MH_CreateHook failed: %s", MH_StatusToString(st));
        return;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        LOG_ERROR("MovieSubtitles: DISABLED — MH_EnableHook failed: %s", MH_StatusToString(st));
        return;
    }
    gHooked = true;
    LOG_INFO("MovieSubtitles: subtitle draw hooked at 0x%p (%s build)", target, build.name);
}

// ---- diagnostics: which movie is up, and what the engine loaded for it ----

bool gPlayerChecked = false;
bool gPlayerUsable = false;

void* FindPlayer()
{
    const uintptr_t base = SDK::GetModuleBase();
    if (!base)
        return nullptr;
    const GameBuild::Build& build = GameBuild::Current();
    void* player = ReadPtr(reinterpret_cast<const void*>(base + build.moviePlayerGlobal), 0);
    if (!player)
        return nullptr; // created lazily on the first movie

    if (!gPlayerChecked) {
        gPlayerChecked = true;
        const void* vtable = ReadPtr(player, 0);
        const void* expected = reinterpret_cast<const void*>(base + build.moviePlayerVtable);
        gPlayerUsable = vtable == expected;
        if (!gPlayerUsable) {
            // Only the open/close log lines are lost — speech rides the hook, not this object.
            LOG_WARN("MovieSubtitles: movie player vtable is 0x%p, expected 0x%p for %s — movie "
                     "open/close lines disabled",
                vtable, expected, GameBuild::Fingerprint());
        }
    }
    return gPlayerUsable ? player : nullptr;
}

std::string TrackSummary(const void* track)
{
    if (!track)
        return "no cue track object";
    const int32_t groupCount = ReadI32(track, kTrackGroupCount);
    const int32_t groupIndex = ReadI32(track, kTrackGroupIndex);
    if (groupIndex < 0 || groupIndex >= groupCount || groupCount > kMaxPlausibleGroups)
        return "EMPTY (the engine loaded no cues for this movie)";
    const auto* groups = static_cast<const uint8_t*>(ReadPtr(track, kTrackGroups));
    if (!groups)
        return "EMPTY (no group array)";
    const uint8_t* group = groups + static_cast<ptrdiff_t>(groupIndex) * kGroupStride;
    return std::to_string(ReadI32(group, kGroupEntryCount)) + " cue(s) in group "
        + std::to_string(groupIndex) + " of " + std::to_string(groupCount)
        + (ReadI32(group, kGroupMode) != 0 ? " (random picker, not timed)" : "");
}

void* gHandle = nullptr;
std::string gMovieName;
int gLinesSpoken = 0;

void TrackMovie()
{
    void* player = FindPlayer();
    void* handle = player ? ReadPtr(player, kPlayerBinkHandle) : nullptr;
    if (handle == gHandle)
        return;
    if (gHandle) {
        LOG_INFO("MovieSubtitles: movie '%s' closed — %d line(s) spoken", gMovieName.c_str(),
            gLinesSpoken);
    }
    gHandle = handle;
    gLinesSpoken = 0;
    gMovieName.clear();
    if (handle) {
        gMovieName = ReadFString(player, kPlayerName);
        LOG_INFO("MovieSubtitles: movie '%s' opened — cue track %s", gMovieName.c_str(),
            TrackSummary(ReadPtr(player, kPlayerCueTrack)).c_str());
    }
}

// ---- the game-thread half ----

uint64_t gSeenSeq = 0;

/// The engine draws the line on a canvas, which renders `<` and `>` as themselves — the one
/// bracketed stock line, `<Growl>` / `<Рычит>`, is a stage direction a sighted player reads
/// literally. So the brackets are dropped as CHARACTERS, never stripped as a tag, and the word
/// between them is spoken; CleanText then only collapses whitespace.
std::string SpeakableLine(const std::wstring& raw)
{
    std::string line = WideToUtf8(raw.data(), raw.size());
    for (char& c : line) {
        if (c == '<' || c == '>')
            c = ' ';
    }
    return GameUI::CleanText(line);
}

/// True when the engine drew a cue it could not localize: `Localize` then returns either the raw
/// key ("CIN_XP_PreIntro_01") or its `?LANG?Package.Section.Key?` marker. Neither is a sentence —
/// no space, and an underscore or dot where words would be.
bool IsUnlocalizedKey(const std::string& line)
{
    if (line.find(' ') != std::string::npos)
        return false;
    if (line.size() > 2 && line.front() == '?' && line.back() == '?')
        return true;
    bool underscore = false;
    for (const char c : line) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x80 || !(std::isalnum(u) || c == '_' || c == '-'))
            return false;
        underscore |= c == '_';
    }
    return underscore;
}

} // namespace

void OnTick()
{
    static uint64_t sLastMs = 0;
    const uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    if (!gInstallTried)
        InstallHook();
    if (!gHooked)
        return;

    TrackMovie();

    std::wstring raw;
    bool oversize = false;
    bool changed = false;
    AcquireSRWLockShared(&gSlotLock);
    if (gSlotSeq != gSeenSeq) {
        gSeenSeq = gSlotSeq;
        changed = true;
        try {
            raw = gSlotLine;
        } catch (...) {
            changed = false;
        }
    }
    oversize = gSlotOversize;
    ReleaseSRWLockShared(&gSlotLock);

    if (oversize) {
        static bool sReported = false;
        if (!sReported) {
            sReported = true;
            // Loud, per the fixed-buffer rule: a line we refused is a line the player never hears.
            LOG_ERROR("MovieSubtitles: a subtitle line ran past %zu characters or faulted and was "
                      "not spoken — raise kMaxPlausibleLineChars if it was real",
                kMaxPlausibleLineChars);
        }
    }
    if (!changed)
        return;

    // Empty is the gap between two lines, or subtitles switched off — the screen is quiet too.
    const std::string line = SpeakableLine(raw);
    if (line.empty())
        return;
    if (IsUnlocalizedKey(line)) {
        // Drawn for a sighted player too, and deliberately NOT spoken: an internal name tells a
        // blind player exactly as much as it tells a sighted one, which is nothing, and the rule
        // against reading one aloud outranks literal parity with a caption that is itself a bug.
        // Logged, so a missing line is never indistinguishable from a silent one.
        LOG_WARN("MovieSubtitles: the engine drew an unlocalized cue '%s' — not spoken",
            line.c_str());
        return;
    }

    // Combat, not Alert: subtitles are a SEQUENCE and something that HAPPENED, not a change in
    // what the player can do. Flyover, because this is text the game itself put on screen — which
    // also lets the narrator drop a State line that duplicates it.
    Narrator::Say(Narrator::Channel::Combat, line, Narrator::Source::Flyover);
    gLinesSpoken++;
}

} // namespace MovieSubtitles
