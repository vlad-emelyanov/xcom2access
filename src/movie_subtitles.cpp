// movie_subtitles.cpp — the subtitles the game draws over a full-screen `.bk2` movie.
//
// ⛔ READ WHAT THE GAME IS SHOWING, DO NOT REBUILD IT. The cue track really is a plain text file
// (`XComGame/Movies/<name>.txt`, `startMs endMs CueKey`) and the words really are a plain loc
// file (`XComGame/Localization/<lang>/Subtitles.<lang>`), and parsing those ourselves would have
// been half the code — and wrong. A private parse is a SECOND subtitle system that agrees with
// the game only by luck: it would keep talking after the player turned subtitles off, it would
// pick a language off our own guess rather than the one the engine resolved, it would drift the
// moment a mod replaced a track, and it could not see the loop counter that decides which pass
// through a looping movie we are on. So every one of those four facts is READ OUT OF THE PLAYER:
// the cue table the engine parsed, the frame the engine is on, the option the engine tests, and
// the text the engine's own Localize returns. What is reproduced here is exactly one thing —
// the interval scan that picks the cue for a time, ten lines mirroring the engine's own.
//
// ---------------------------------------------------------------------------------------------
// Why this reads a raw C++ object, which nothing else in this project does
//
// `FFullScreenMovieBink` is not a UObject. It has no class, no properties, no reflection and no
// script surface, so the mod's two usual doors — the widget tree and the companion — are both
// shut. The ONE address this surface needs is the engine global that holds it — pinned per build
// in `GameBuild`, since the two retail executables put it in different places; everything else is
// an offset within an object that global points at.
//
// That is a real risk (another build puts it elsewhere, and a stale pointer is a crash rather
// than a wrong word), so the global is VALIDATED before it is ever dereferenced further: the object's vtable
// pointer must be the one FFullScreenMovieBink's constructor writes. A mismatch disables the
// whole surface permanently with one log line. Same discipline as sdk_globals.cpp's GNames /
// GObjects heuristics, and for the same reason — a wrong global must fail loudly at the door
// rather than quietly somewhere downstream.
//
// Addresses are Ghidra VAs minus ImageBase 0x140000000, as everywhere else in this project.
// Provenance, so the next person can re-derive them rather than trust them:
//   * `FUN_14024c630` is the lazy creator — `if (!g) g = new FFullScreenMovieBink(...)` — which
//     names both the global (0x141DB8900) and the constructor.
//   * `FUN_1402073b0` is that constructor: it writes the vtable (0x1413E9B28) and allocates the
//     0x30-byte cue-track object into +0x114.
//   * `FUN_14023d7a0` (PlayMovie) composes `Movies\<name>.txt` and hands it to the track, stores
//     the movie name at +0x40 and the BINKHANDLE at +0x84.
//   * `FUN_14023ed10` (PumpMovie) is the per-frame driver, and the time arithmetic below is
//     transcribed from it line for line.
//   * `FUN_14034db80` is the track's cue lookup, which the scan below mirrors.
//   * `FUN_1402336c0` is the per-pump subtitle fetch: cue lookup, then `Localize("Subtitles",
//     key, "Subtitles")` (`FUN_140163d80`) — ON THE MOVIE THREAD, every pump while a cue is up.
//
// ---------------------------------------------------------------------------------------------
// ⛔ NEVER CALL `Localize` WHILE A MOVIE IS PLAYING
//
// PumpMovie runs on the movie player's own worker thread, and it localizes the current cue on
// every pump. `Localize` walks GConfig, which has no lock, so a game-thread `Localize` racing it
// corrupts the config cache. That is not theory: on 2026-09-30 this file's first `Localize` of the
// session, one cue into `CIN_XP_Stronghold_Warlock`, came back empty AND the movie thread died in
// `scalable_realloc` under `FUN_140163d80` in the same second (minidump
// `XCom-…-2026.09.30-02.09.03`), freezing the game on the Chosen's first line.
//
// So the cue TEXT is resolved AHEAD of time: while no movie is up, on the game thread, every key of
// the `[Subtitles]` section goes through the engine's own `Localize` — the same lookup PumpMovie
// makes, in the language the engine resolved — a batch per tick, into `gCueText`. During a movie
// this file only reads that table. The key LIST comes from the shipped INT file (`Subtitles.int`),
// which says which keys exist and nothing else; the words are still the engine's.

#include "movie_subtitles.h"

#include "game_build.h"
#include "game_ui_core.h"
#include "log.h"
#include "narrator.h"
#include "script_call.h"
#include "sdk_globals.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MovieSubtitles {

namespace {

// ---- the one hard-coded pair: GFullScreenMovie and FFullScreenMovieBink's vtable ----
//
// Both live in `GameBuild`'s per-build table rather than here, because the GOG image puts them
// somewhere else (docs/engine-notes.md). The vtable check in FindPlayer is what pins them either
// way — it is the only thing standing between this surface and a wild read, which is exactly why
// it has to compare against the RIGHT build's vtable.

// ---- FFullScreenMovieBink (0x140 bytes) ----

constexpr int32_t kPlayerName = 0x40;         // FString, the movie being played
constexpr int32_t kPlayerFlags = 0x60;        // playback flag dword
constexpr int32_t kPlayerFrameNum = 0x68;     // int32, refreshed from BinkGetRealtime each pump
constexpr int32_t kPlayerSecsPerFrame = 0x70; // double, FrameRateDiv / FrameRate
constexpr int32_t kPlayerBinkHandle = 0x84;   // HBINK — null exactly when no movie is up
constexpr int32_t kPlayerCueTrack = 0x114;    // the parsed <name>.txt
constexpr int32_t kPlayerLoopCount = 0x13C;   // int32, how many times the movie has wrapped

// The one playback flag this surface cares about: set means "show the subtitle whatever the
// option says", which is how the engine keeps a legal screen readable. Mirrored so that a movie
// the game subtitles unconditionally is one we speak unconditionally.
constexpr uint32_t kPlayerForceSubtitles = 0x80;

// HBINK's own header — Width, Height, Frames, FrameNum.
constexpr int32_t kBinkTotalFrames = 0x08;

// ---- the cue track (0x30 bytes) ----

constexpr int32_t kTrackGroups = 0x00;     // array of groups
constexpr int32_t kTrackGroupCount = 0x08; // int32
constexpr int32_t kTrackGroupIndex = 0x10; // int32, -1 when nothing is loaded

constexpr int32_t kGroupStride = 0x28;
constexpr int32_t kGroupMode = 0x10;       // 0 = timed cues; anything else is the random picker
constexpr int32_t kGroupEntries = 0x18;
constexpr int32_t kGroupEntryCount = 0x20; // int32

constexpr int32_t kCueStride = 0x1C;
constexpr int32_t kCueName = 0x00;    // FString — the key into the Subtitles loc package
constexpr int32_t kCueStartMs = 0x10; // uint32
constexpr int32_t kCueEndMs = 0x14;   // uint32

// ---- the loc package the engine resolves a cue through ----
//
// Section and package are BOTH "Subtitles" — that is the engine's own call, not a guess. Note it
// is `XComGame/Localization/<lang>/Subtitles.<lang>` that carries the cue text; the file of the
// same name under `Engine/Localization` holds only the paused caption, and reading that one is
// how this was first written off as "the game ships no movie subtitles".
constexpr const char* kSubtitleSection = "Subtitles";
constexpr const char* kSubtitlePackage = "Subtitles";

// 100 ms, matching the narrator drain. Every stock cue is at least a second long, so this cannot
// miss one; going faster would only cost outbound calls on a screen where nothing else runs.
constexpr uint64_t kPollIntervalMs = 100;

// Keys resolved per idle tick. The stock section is ~800 keys, so the table is whole about a
// second and a half after the first idle tick — long before the first story movie — while no
// single frame pays for more than a few dozen config lookups.
constexpr size_t kResolveBatch = 64;

// Runaway guards on counts read out of engine memory. Neither is a working limit — the longest
// stock track is under 200 cues in one group — so tripping either means the layout is wrong, and
// it is logged as such rather than clamped.
constexpr int32_t kMaxPlausibleGroups = 1024;
constexpr int32_t kMaxPlausibleCues = 65536;

// ---- SEH-guarded primitives ----
//
// Local rather than borrowed from game_ui.cpp for the same reason flash_shadow.cpp keeps its
// own: this TU walks a raw C++ object that no reflection describes, and the reads that make
// that safe belong beside the offsets they are applied to.
//
// ⛔ Each guarded read is its OWN function with no C++ object in scope — MSVC refuses __try in
// a frame that needs unwinding (C2712).

uint32_t ReadU32(const void* base, int32_t offset)
{
    __try {
        return *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const uint8_t*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int32_t ReadI32(const void* base, int32_t offset)
{
    __try {
        return *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

double ReadF64(const void* base, int32_t offset)
{
    __try {
        return *reinterpret_cast<const double*>(reinterpret_cast<const uint8_t*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0;
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

bool CopyWideRaw(const wchar_t* src, wchar_t* dst, int32_t len)
{
    __try {
        memcpy(dst, src, static_cast<size_t>(len) * sizeof(wchar_t));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A cue key is an identifier the game wrote ("CIN_XP_PreIntro_01"), so unlike a localized line
// its length really is bounded — but it is still sized from the count rather than from a buffer,
// because the count is what tells a valid read from a wild pointer.
constexpr int32_t kMaxPlausibleCueChars = 512;

std::string ReadFString(const void* base, int32_t offset)
{
    const wchar_t* data = nullptr;
    int32_t count = 0;
    if (!ReadFStringHeader(base, offset, &data, &count))
        return {};
    if (!data || count <= 1)
        return {}; // the count includes the terminator, so 1 is the empty string
    const int32_t len = count - 1;
    if (len > kMaxPlausibleCueChars)
        return {};

    std::wstring wide(static_cast<size_t>(len), L'\0');
    if (!CopyWideRaw(data, wide.data(), len))
        return {};

    const int bytes
        = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), len, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {};
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), len, out.data(), bytes, nullptr, nullptr);
    return out;
}

// ---- state ----

bool gChecked = false;  // has the global been validated yet
bool gUsable = false;   // ...and did it pass
void* gHandle = nullptr; // the BINKHANDLE of the movie we are following
std::string gLastCue;    // the cue key last acted on, so a held cue speaks once
std::string gMovieName;  // the movie we are following, for the close line
int gCuesSpoken = 0;     // ...and how many of its cues reached speech

// The cue text table — see the header comment for why it is filled ahead of time. A key maps to
// its cleaned line, or to "" when the engine has no words for it (that answer is cached too, so a
// missing line is looked up once, not once per movie).
std::unordered_map<std::string, std::string> gCueText;
std::vector<std::string> gPendingKeys; // keys not yet resolved, consumed from the back
bool gKeysRead = false;
bool gCacheAnnounced = false;

/// The movie player, or null — either because none exists yet or because the global did not
/// validate, which is checked exactly once and then remembered.
void* FindPlayer()
{
    const uintptr_t base = SDK::GetModuleBase();
    if (!base)
        return nullptr;

    const GameBuild::Build& build = GameBuild::Current();
    void* player = ReadPtr(reinterpret_cast<const void*>(base + build.moviePlayerGlobal), 0);
    if (!player)
        return nullptr; // created lazily on the first movie — not an error, just not yet

    if (!gChecked) {
        gChecked = true;
        const void* vtable = ReadPtr(player, 0);
        const void* expected = reinterpret_cast<const void*>(base + build.moviePlayerVtable);
        gUsable = vtable == expected;
        if (gUsable) {
            LOG_INFO("MovieSubtitles: movie player at 0x%p validated (%s build)", player,
                build.name);
        } else {
            // Loud and terminal. The alternative — carrying on against an object that is not
            // what we think it is — reads engine memory at arbitrary offsets, and the offsets
            // here are not `static_assert`able against anything, so this check IS the pin.
            LOG_ERROR("MovieSubtitles: DISABLED — global 0x%llX holds 0x%p whose vtable is 0x%p, "
                      "expected 0x%p for the %s build; this executable is %s "
                      "(re-derive with tools/derive_offsets.py)",
                static_cast<unsigned long long>(base + build.moviePlayerGlobal), player, vtable,
                expected, build.name, GameBuild::Fingerprint());
        }
    }
    return gUsable ? player : nullptr;
}

/// Where the movie is now, in milliseconds, exactly as PumpMovie computes it before asking for
/// the subtitle. The loop term is what keeps a looping movie's second pass from replaying the
/// first pass's cues.
uint32_t PlaybackMs(const void* player, const void* handle)
{
    const double msPerFrame = ReadF64(player, kPlayerSecsPerFrame) * 1000.0;
    const int32_t frame = ReadI32(player, kPlayerFrameNum);
    const int32_t loops = ReadI32(player, kPlayerLoopCount);
    const uint32_t totalFrames = ReadU32(handle, kBinkTotalFrames);

    const int32_t withinPass = static_cast<int32_t>(frame * msPerFrame + 0.5);
    const int32_t completedPasses
        = static_cast<int32_t>(static_cast<double>(totalFrames * loops) * msPerFrame);
    return static_cast<uint32_t>(withinPass + completedPasses);
}

/// The cue key showing at `timeMs`, or empty in the gap between two cues.
///
/// Mirrors the engine's own scan: the track's CURRENT group (the engine's index, not ours), its
/// entries in order, first interval that contains the time, and an early break once the entries
/// have run past it. Only the timed branch is reproduced — the other one picks a random line for
/// looping ambience and no movie track uses it.
std::string CueAt(const void* track, uint32_t timeMs)
{
    const int32_t groupCount = ReadI32(track, kTrackGroupCount);
    const int32_t groupIndex = ReadI32(track, kTrackGroupIndex);
    if (groupIndex < 0 || groupIndex >= groupCount)
        return {}; // nothing loaded
    if (groupCount > kMaxPlausibleGroups) {
        LOG_WARN("MovieSubtitles: refusing implausible group count %d", groupCount);
        return {};
    }

    const auto* groups = static_cast<const uint8_t*>(ReadPtr(track, kTrackGroups));
    if (!groups)
        return {};
    const uint8_t* group = groups + static_cast<ptrdiff_t>(groupIndex) * kGroupStride;

    if (ReadI32(group, kGroupMode) != 0)
        return {}; // the random picker, not a timed track

    const int32_t cueCount = ReadI32(group, kGroupEntryCount);
    if (cueCount <= 0)
        return {};
    if (cueCount > kMaxPlausibleCues) {
        LOG_WARN("MovieSubtitles: refusing implausible cue count %d", cueCount);
        return {};
    }

    const auto* cues = static_cast<const uint8_t*>(ReadPtr(group, kGroupEntries));
    if (!cues)
        return {};

    for (int32_t i = 0; i < cueCount; i++) {
        const uint8_t* cue = cues + static_cast<ptrdiff_t>(i) * kCueStride;
        const uint32_t start = ReadU32(cue, kCueStartMs);
        if (timeMs < start)
            break; // cues are in order, so everything after this one is still ahead
        if (timeMs <= ReadU32(cue, kCueEndMs))
            return ReadFString(cue, kCueName);
    }
    return {};
}

/// What the engine loaded for this movie, for the open line. `PlayMovie` (`FUN_14023d7a0`) reads
/// `Movies\<name>.txt` only while the global at 0x141E82224 is set — `XComEngine.PlayMovie` sets
/// it when subtitles are on, a loading movie can clear it, and a Kismet `SeqAct_ControlGameMovie`
/// never touches it — and otherwise hands the track an EMPTY path. So "no cues" here is the engine's
/// own choice, and the log has to say which of the two happened or a silent cinematic is
/// indistinguishable from a broken reader.
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

/// The game's own root (`…\XCom2-WarOfTheChosen`), from the running executable's path, which is
/// `<root>\Binaries\Win64\XCom2.exe` on both retail builds.
std::filesystem::path GameRoot()
{
    std::wstring exe(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (len == 0 || len >= exe.size())
        return {};
    exe.resize(len);
    return std::filesystem::path(exe).parent_path().parent_path().parent_path();
}

/// Every key of the `[Subtitles]` section of the shipped INT file. Only the NAMES are taken from
/// disk — which keys exist is the same in every language — and the text is left to the engine.
std::vector<std::string> ReadSubtitleKeys()
{
    const std::filesystem::path file
        = GameRoot() / L"XComGame" / L"Localization" / L"INT" / L"Subtitles.int";
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        LOG_ERROR("MovieSubtitles: cannot open %s — movie subtitles will not be spoken",
            file.string().c_str());
        return {};
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // Every shipped loc file is UTF-16LE with a BOM; anything else means the file is not what
    // this parser assumes, and guessing would produce keys the engine never asks for.
    if (bytes.size() < 2 || static_cast<uint8_t>(bytes[0]) != 0xFF
        || static_cast<uint8_t>(bytes[1]) != 0xFE) {
        LOG_ERROR("MovieSubtitles: %s is not UTF-16LE — movie subtitles will not be spoken",
            file.string().c_str());
        return {};
    }
    std::wstring text((bytes.size() - 2) / 2, L'\0');
    memcpy(text.data(), bytes.data() + 2, text.size() * sizeof(wchar_t));

    auto trim = [](std::wstring_view s) {
        const size_t b = s.find_first_not_of(L" \t\r");
        if (b == std::wstring_view::npos)
            return std::wstring_view{};
        const size_t e = s.find_last_not_of(L" \t\r");
        return s.substr(b, e - b + 1);
    };

    std::vector<std::string> keys;
    std::unordered_set<std::string> seen;
    bool inSection = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos)
            end = text.size();
        const std::wstring_view line = trim(std::wstring_view(text).substr(pos, end - pos));
        pos = end + 1;

        if (line.empty() || line.front() == L';')
            continue;
        if (line.front() == L'[') {
            inSection = line == L"[Subtitles]";
            continue;
        }
        const size_t eq = line.find(L'=');
        if (!inSection || eq == std::wstring_view::npos)
            continue;
        const std::wstring_view wkey = trim(line.substr(0, eq));
        std::string key;
        key.reserve(wkey.size());
        bool ascii = !wkey.empty();
        for (const wchar_t c : wkey) {
            if (c >= 0x80) {
                ascii = false;
                break;
            }
            key.push_back(static_cast<char>(c));
        }
        // A cue key is an identifier the game wrote; a non-ASCII one is not a key the cue tracks
        // can name, so it is skipped rather than mangled.
        if (ascii && seen.insert(key).second)
            keys.push_back(std::move(key));
    }
    return keys;
}

/// Resolve the next batch of cue keys through the engine's `Localize`. ⛔ The caller guarantees
/// no movie is playing — see the header comment; this is the ONLY place this file localizes.
void ResolvePendingCues()
{
    if (!gKeysRead) {
        gKeysRead = true;
        gPendingKeys = ReadSubtitleKeys();
        LOG_INFO("MovieSubtitles: resolving %zu subtitle cue(s) while no movie is playing",
            gPendingKeys.size());
    }
    if (gPendingKeys.empty())
        return;

    for (size_t n = 0; n < kResolveBatch && !gPendingKeys.empty(); n++) {
        std::string key = std::move(gPendingKeys.back());
        gPendingKeys.pop_back();
        std::string line = GameUI::CleanText(
            GameUI::LocalizeString(kSubtitleSection, key.c_str(), kSubtitlePackage));
        gCueText.emplace(std::move(key), std::move(line));
    }

    if (gPendingKeys.empty() && !gCacheAnnounced) {
        gCacheAnnounced = true;
        size_t withText = 0;
        for (const auto& [key, line] : gCueText)
            withText += line.empty() ? 0 : 1;
        LOG_INFO("MovieSubtitles: cue table ready — %zu key(s), %zu with localized text",
            gCueText.size(), withText);
    }
}

void Reset()
{
    if (gHandle) {
        LOG_INFO("MovieSubtitles: movie '%s' closed — %d cue(s) spoken", gMovieName.c_str(),
            gCuesSpoken);
    }
    gHandle = nullptr;
    gLastCue.clear();
    gMovieName.clear();
    gCuesSpoken = 0;
}

} // namespace

void OnTick()
{
    static uint64_t sLastMs = 0;
    const uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    void* player = FindPlayer();
    if (gChecked && !gUsable)
        return; // the vtable pin failed; FindPlayer said so once and the surface is off

    // The BINKHANDLE is the engine's own "is a movie open". Nothing else here is meaningful
    // without it, and a CHANGED one is a different movie — which must forget the last cue, or a
    // second cinematic opening on the same key would be swallowed as already-said. No player at
    // all means none was ever created, so no movie thread exists either.
    void* handle = player ? ReadPtr(player, kPlayerBinkHandle) : nullptr;
    if (!handle) {
        Reset();
        // ⛔ The one window in which `Localize` is safe: no movie open, and the engine's own
        // answer agrees. IsAnyMoviePlaying is asked as well because it is the game's test, and a
        // disagreement costs only a tick of delay. Asked only while the table is filling, so the
        // idle cost afterwards is back to one pointer read.
        if ((!gKeysRead || !gPendingKeys.empty()) && !GameUI::MoviePlaying())
            ResolvePendingCues();
        return;
    }
    if (handle != gHandle) {
        Reset();
        gHandle = handle;
        gMovieName = ReadFString(player, kPlayerName);
        LOG_INFO("MovieSubtitles: movie '%s' opened — cue track %s", gMovieName.c_str(),
            TrackSummary(ReadPtr(player, kPlayerCueTrack)).c_str());
    }

    // ⛔ The option gate, and it is the GAME's own test rather than a policy of ours: PumpMovie
    // blanks the subtitle string it just fetched unless subtitles are enabled or the movie
    // carries the force flag. Speech is our rendering of that string, so it obeys the same
    // switch — the same rule the narrative commlink follows (docs/surfaces/strategy.md).
    if ((ReadU32(player, kPlayerFlags) & kPlayerForceSubtitles) == 0
        && !GameUI::SubtitlesEnabled()) {
        // The cue marker is dropped but the HANDLE is kept: the movie is still running, so this
        // is not a Reset. Dropping the marker is what lets a player who turns subtitles back on
        // mid-cinematic hear the line currently on screen instead of waiting for the next one —
        // the same behaviour the commlink's gate has, for the same reason.
        gLastCue.clear();
        return;
    }

    void* track = ReadPtr(player, kPlayerCueTrack);
    if (!track)
        return;

    const std::string cue = CueAt(track, PlaybackMs(player, handle));
    if (cue == gLastCue)
        return; // the same line still on screen, which is true for most polls
    gLastCue = cue;
    if (cue.empty())
        return; // a gap between two lines — the screen went quiet, and so do we

    // ⛔ A table read, never a `Localize` — the movie thread is localizing this very cue as we
    // speak, and a second caller is what crashed the game (header comment).
    const auto found = gCueText.find(cue);
    if (found == gCueText.end()) {
        // The table was not whole when this movie began, or the cue track names a key the INT
        // file lacks (a mod's track). Either way the text cannot be fetched safely now, and a
        // raw key is not speech — so it is logged, loudly, rather than guessed.
        LOG_WARN("MovieSubtitles: cue '%s' is not in the resolved table (%zu key(s) still "
                 "pending) — not spoken",
            cue.c_str(), gPendingKeys.size());
        return;
    }
    const std::string& line = found->second;
    if (line.empty()) {
        // The engine falls back to DRAWING the raw cue key here, and we deliberately do not
        // speak it: "CIN_XP_PreIntro_01" tells a blind player exactly as much as it tells a
        // sighted one, which is nothing, and this project's rule against reading an internal
        // name aloud outranks literal parity with a caption that is itself a bug. Logged
        // instead, because a missing line must never be indistinguishable from a silent one.
        LOG_WARN("MovieSubtitles: no localized text for cue '%s'", cue.c_str());
        return;
    }

    // Combat, not Alert: subtitles are a SEQUENCE and something that HAPPENED, not a change in
    // what the player can do. Flyover, because this
    // is text the game itself put on screen — which also lets the narrator drop a State line
    // that duplicates it.
    Narrator::Say(Narrator::Channel::Combat, line, Narrator::Source::Flyover);
    gCuesSpoken++;
}

} // namespace MovieSubtitles
