#include "flash_shadow.h"
#include "script_call.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace FlashShadow {

namespace {

// ---- engine layout (every one pinned in sdk_layout_checks.cpp) ----

constexpr int32_t kFnFuncOffset = 0xF0;      // UFunction::Func — the native dispatch pointer.
                                             // Same field mouse_gate.cpp rewrites; confirmed in
                                             // the ProcessEvent decompile as the call target.
constexpr int32_t kMovieQueueOffset = 0x270; // UIMovie::CommandQueue (TArray<FASValue>)
constexpr int32_t kPanelMCOffset = 0x2D0;    // UIPanel::MC (UIMCController*)
constexpr int32_t kMCCacheIndexOffset = 0x68; // UIMCController::CacheIndex

// FASValue, 0x1C under pack(4): Type u8 @0x00, B bitfield @0x04, N float @0x08, S FString @0x0C.
constexpr int32_t kASValueStride = 0x1C;
constexpr int32_t kASTypeOffset = 0x00;
constexpr int32_t kASBoolOffset = 0x04;
constexpr int32_t kASNumOffset = 0x08;
constexpr int32_t kASStrOffset = 0x0C;

// UIMCController.uc:13 — the opcode grammar.
constexpr uint32_t kIndexMask = 0x000FFFFF;
constexpr uint32_t kOpMask = 0x00F00000;
constexpr uint32_t kFieldOp = 0x00A00000;
constexpr uint32_t kFunctionOp = 0x00B00000;
constexpr uint32_t kChildFieldOp = 0x00C00000;
constexpr uint32_t kChildFunctionOp = 0x00D00000;
constexpr uint32_t kCleanupOp = 0x00E00000;

// ---- bounds ----
//
// ⛔ A cap that drops data must be LOUD (CLAUDE.md). Each of these logs on the transition into
// clipping rather than per occurrence, which would drown the log at 60 Hz.

// One flush is one frame of one movie. A busy strategy screen pushes a few hundred operations;
// this is the runaway guard, not a working limit.
constexpr int32_t kMaxOpsPerFlush = 4096;
// A single batch's operand list. `UIMakeContactAlert` is the widest stock panel at 12.
constexpr size_t kMaxArgsPerOp = 64;
// Operations retained per panel. A panel is normally painted by ONE Update* call; the extra room
// covers a screen that splits its text across several, and the oldest is dropped first.
//
// ⛔ SIZED FROM THE WIDEST REPAINTER, not from a round number. `UIX2ResourceHeader` is painted by
// ONE clearResources plus up to NINE addResource calls (UIAvengerHUD.UpdateDefaultResources —
// income, supplies, intel, elerium, alloys, sci, eng, contacts, power), so the strip alone needs
// ten; a screen whose own transaction handler refreshes the strip in the same frame the screen
// listener does pushes that batch twice. At the old value of 8 the delimiter and the first two
// resources fell off the front and INCOME silently ceased to exist — the tail-truncation failure
// mode CLAUDE.md names, wearing a different hat. Doubling the strip's worst case leaves headroom
// without changing what any other panel costs: this is a per-panel BOUND, and a panel that pushes
// one Update* call still holds one entry.
//
// ✅ It also lifts a latent truncation on the one OTHER multi-push consumer: `UIResearchUnlocked`
// pushes one `addResearch` per tech, so a deck of more than eight was losing its earliest cards
// (visible as that reader's `COUNT MISMATCH vs NumUnlockedTechs` line). Nothing else changes for
// it — `PopulateData` runs exactly once on a freshly spawned screen, so a larger history cannot
// carry cards over from a previous deck.
constexpr size_t kMaxCallsPerPanel = 24;
// Panels retained. Cache indices are recycled by the game, so the real bound is eviction on
// CLEANUP_OP; this is the backstop for a movie that never cleans up.
constexpr size_t kMaxPanels = 2048;

// ---- state ----

struct PanelEntry {
    uint64_t stamp = 0; // last write, for LRU eviction
    std::vector<Call> calls;
};

std::mutex gMutex;
std::unordered_map<int32_t, PanelEntry> gPanels;
uint64_t gStamp = 0;

Stats gStats;

void* gFunction = nullptr; // the UFunction object for UIMovie.CopyCommandQueue
void* gOrigFunc = nullptr; // its original native thunk
bool gInstalled = false;
bool gRefused = false;

// ---- SEH-guarded raw reads ----
//
// Local rather than borrowed from game_ui.cpp: this TU runs INSIDE the VM's dispatch, and the
// primitives it uses on that path should be visible in the same file as the thunk that calls
// them. They are the same shapes game_ui.cpp uses (ReadPtrRaw / ReadU32Raw / CopyFStringRaw).

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

uint8_t ReadU8(const void* base, int32_t offset)
{
    __try {
        return *reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

float ReadF32(const void* base, int32_t offset)
{
    __try {
        return *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
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

// A TArray header: data pointer at +0x00, count at +0x08. Returns false when unreadable.
bool ReadArrayHeader(const void* base, int32_t offset, const uint8_t** data, int32_t* count)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(base) + offset;
        *data = *reinterpret_cast<const uint8_t* const*>(arr);
        *count = *reinterpret_cast<const int32_t*>(arr + 8);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ⛔ Sized from the DATA, never from a buffer we picked (CLAUDE.md's hard rule, earned by the
// XCOM-archives truncation). The count is read first and the string built to fit; an implausible
// count is refused rather than clamped, because a clamp here would silently drop the tail of a
// line and read to a blind player as a screen with nothing more to say.
constexpr int32_t kMaxPlausibleStringChars = 1 << 16;

// ⛔ The SEH halves are their OWN functions with no C++ objects in scope, because MSVC refuses
// __try in a frame that needs unwinding (C2712). That is why the read is split in three rather
// than written as one guarded block.
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

bool ReadFString(const void* base, int32_t offset, std::string* out)
{
    const wchar_t* data = nullptr;
    int32_t count = 0;
    if (!ReadFStringHeader(base, offset, &data, &count))
        return false;
    if (!data || count <= 1)
        return false; // count includes the terminator, so 1 is the empty string
    const int32_t len = count - 1;
    if (len > kMaxPlausibleStringChars) {
        LOG_WARN("FlashShadow: refusing implausible string length %d", len);
        return false;
    }

    // Copy under the guard before touching it — the engine may free the array at any moment, and
    // WideCharToMultiByte would fault outside a handler.
    std::wstring wide(static_cast<size_t>(len), L'\0');
    if (!CopyWideRaw(data, wide.data(), len))
        return false;

    const int bytes = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), len, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return false;
    out->resize(static_cast<size_t>(bytes));
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), len, out->data(), bytes, nullptr, nullptr);
    return true;
}

// ---- the decoder ----

bool ReadValue(const uint8_t* entry, Value* out)
{
    const uint8_t type = ReadU8(entry, kASTypeOffset);
    if (type > static_cast<uint8_t>(ValueType::Boolean))
        return false;
    out->type = static_cast<ValueType>(type);
    switch (out->type) {
    case ValueType::Number:
        out->n = ReadF32(entry, kASNumOffset);
        break;
    case ValueType::Boolean:
        // `B` is a one-bit bitfield in the low bit of the dword at +0x04.
        out->b = (ReadU32(entry, kASBoolOffset) & 1u) != 0;
        break;
    case ValueType::String:
        ReadFString(entry, kASStrOffset, &out->s);
        break;
    default:
        break;
    }
    return true;
}

// Does this batch carry anything worth keeping?
//
// ⛔ The filter is what makes an always-on capture affordable. The overwhelming majority of the
// per-frame stream is numeric and boolean churn — `_x`, `_y`, `_alpha`, `_visible` — pushed by
// every animating widget on screen. None of it is text and none of it is ours. Keeping only
// batches with a non-empty string operand drops that volume before it reaches the map, while
// costing nothing on the path that matters: a panel painting itself with words always has one.
bool CarriesText(const Call& call)
{
    for (const Value& v : call.args) {
        if (v.type == ValueType::String && !v.s.empty())
            return true;
    }
    return false;
}

// The one exception to CarriesText, and it is about BOUNDARIES rather than words.
//
// A panel that paints itself with a single Update* call needs no delimiter — the newest recorded
// batch IS the current state, which is what GetCall and GetTextCall assume. `UIX2ResourceHeader`
// breaks that assumption: it paints with N same-named `addResource` batches, so "the newest" is
// one resource out of nine, and the whole list is only recoverable if the consumer can tell where
// THIS repaint started. `ClearResources` is `MC.FunctionVoid("clearResources")` — a FUNCTION_OP
// carrying its name and no operands at all — so it is exactly the marker needed and exactly the
// shape CarriesText drops. Without it, walking back through the recorded calls would happily read
// the previous screen's resources: the strip is rebuilt on every screen change, and a screen
// showing one resource leaves the eight before it looking current.
//
// Kept deliberately narrow — a name match, not "any wordless op". FunctionVoid is how the game
// drives animations too, and admitting those would fill every panel's history with noise.
bool IsRepaintDelimiter(const Call& call)
{
    return !call.isField && call.child.empty() && call.name == "clearResources";
}

void Remember(int32_t index, Call&& call)
{
    PanelEntry& entry = gPanels[index];
    entry.stamp = ++gStamp;
    if (entry.calls.size() >= kMaxCallsPerPanel)
        entry.calls.erase(entry.calls.begin());
    entry.calls.push_back(std::move(call));
}

// The LRU backstop. Eviction is normally CLEANUP_OP's job — the game telling us a panel is gone,
// which is also what makes a RECYCLED cache index safe (UIMCController.Remove queues CLEANUP_OP
// and only then frees the slot). This runs when a movie never cleans up.
void EvictIfNeeded()
{
    if (gPanels.size() <= kMaxPanels)
        return;
    uint64_t oldestStamp = UINT64_MAX;
    int32_t oldestIndex = 0;
    for (const auto& [index, entry] : gPanels) {
        if (entry.stamp < oldestStamp) {
            oldestStamp = entry.stamp;
            oldestIndex = index;
        }
    }
    gPanels.erase(oldestIndex);
    gStats.evictions++;
}

// Walk one flushed queue. Returns the number of operations decoded.
//
// Resynchronisation matters more than strictness here: the stream is a flat value list with a
// null delimiter, so a malformed batch is recoverable by scanning to the next delimiter, whereas
// bailing on the whole flush would lose every panel that painted this frame.
void DecodeQueue(const uint8_t* data, int32_t count)
{
    int32_t i = 0;
    int32_t ops = 0;
    bool clipped = false;

    while (i < count) {
        if (ops >= kMaxOpsPerFlush) {
            clipped = true;
            break;
        }

        Value opcode;
        if (!ReadValue(data + static_cast<size_t>(i) * kASValueStride, &opcode)) {
            i++;
            continue;
        }
        i++;
        if (opcode.type != ValueType::Number)
            continue; // not a batch header — resync on the next entry

        // The opcode is carried as a float, and exactly: `CacheIndex | OP` maxes out at
        // 0x00FFFFFF = 2^24 - 1, the largest integer a float32 represents without loss. That is
        // why INDEX_MASK and OP_MASK are sized the way they are.
        const uint32_t word = static_cast<uint32_t>(opcode.n);
        const uint32_t op = word & kOpMask;
        const int32_t index = static_cast<int32_t>(word & kIndexMask);

        // ⛔ CLEANUP FIRST, because it is the one opcode with NO OPERANDS AND NO DELIMITER:
        // UIMCController.Remove() calls BeginOp(CLEANUP_OP) and then ProcessCommands() directly,
        // never EndOp(), so nothing terminates it. Falling into the operand loop below would
        // consume the NEXT batch's opcode and every operand up to ITS delimiter — swallowing a
        // whole panel's text on any frame where a widget happened to be removed first. (The AS2
        // side has to special-case it for exactly the same reason.)
        //
        // Handling it is also what keeps a RECYCLED cache index honest: the game frees the slot
        // only after queueing this, so evicting here is the corpse rule CLAUDE.md states for
        // screens, in its Flash form.
        if (op == kCleanupOp) {
            ops++;
            if (gPanels.erase(index) > 0)
                gStats.evictions++;
            continue;
        }

        // Collect operands up to the AS_Null delimiter, whatever the opcode — an unrecognised
        // one still has to be stepped over to find the next batch.
        std::vector<Value> operands;
        while (i < count) {
            Value v;
            if (!ReadValue(data + static_cast<size_t>(i) * kASValueStride, &v)) {
                i++;
                break;
            }
            i++;
            if (v.type == ValueType::Null)
                break;
            if (operands.size() < kMaxArgsPerOp)
                operands.push_back(std::move(v));
        }
        ops++;

        Call call;
        size_t at = 0;
        const bool isChild = (op == kChildFieldOp || op == kChildFunctionOp);
        if (isChild) {
            if (at < operands.size() && operands[at].type == ValueType::String)
                call.child = operands[at].s;
            at++;
        }
        if (op == kFieldOp || op == kChildFieldOp || op == kFunctionOp || op == kChildFunctionOp) {
            if (at < operands.size() && operands[at].type == ValueType::String)
                call.name = operands[at].s;
            at++;
        } else {
            continue; // an opcode we do not model; already stepped over
        }
        call.isField = (op == kFieldOp || op == kChildFieldOp);
        for (size_t k = at; k < operands.size(); k++)
            call.args.push_back(std::move(operands[k]));

        if (!CarriesText(call) && !IsRepaintDelimiter(call))
            continue;

        gStats.opsKept++;
        Remember(index, std::move(call));
    }

    gStats.opsSeen += static_cast<uint64_t>(ops);
    EvictIfNeeded();

    if (clipped) {
        static bool sWarned = false;
        if (!sWarned) {
            sWarned = true;
            LOG_WARN("FlashShadow: command queue exceeded %d operations in one flush (%d entries)"
                     " — decoding was cut short; later panels in this frame were not recorded",
                kMaxOpsPerFlush, count);
        }
    }
}

// ---- the thunk ----

using ThunkFn = void (*)(void* self, void* frame, void* result);

// Dispatch convention is the member call the VM makes, `(Context->*Func)(Stack, Result)`:
// RCX = the UIMovie, RDX = FFrame*, R8 = the result pointer. Identical to MouseGate::SwallowThunk,
// and on x64 there is only the one calling convention.
//
// ⛔ The frame is NEVER touched. We want `self`, not the parameter, and the parameter is still
// unevaluated bytecode at this point. Everything below reads the movie's own CommandQueue field
// and then hands the call on unchanged.
// No __try here, and none needed: every read below reaches engine memory through one of the
// guarded primitives above, so a queue freed mid-walk yields zeroes rather than a fault. This is
// the same discipline game_ui.cpp uses, and it is what lets the function hold a lock_guard at all
// (MSVC forbids __try in a frame that unwinds).
void CaptureFrom(void* movie)
{
    const uint8_t* data = nullptr;
    int32_t count = 0;
    if (!ReadArrayHeader(movie, kMovieQueueOffset, &data, &count) || !data || count <= 0)
        return;
    std::lock_guard<std::mutex> lock(gMutex);
    gStats.flushes++;
    DecodeQueue(data, count);
}

void CaptureThunk(void* self, void* frame, void* result)
{
    if (self)
        CaptureFrom(self);

    // Always hand the call on. This is a capture, not a gate: the engine's own snapshot must
    // happen or the frame's UI never reaches Flash.
    if (gOrigFunc)
        reinterpret_cast<ThunkFn>(gOrigFunc)(self, frame, result);
}

bool WriteFunc(void* value)
{
    __try {
        *reinterpret_cast<void**>(static_cast<uint8_t*>(gFunction) + kFnFuncOffset) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

std::vector<std::string> Call::StringArgs() const
{
    std::vector<std::string> out;
    out.reserve(args.size());
    for (const Value& v : args)
        out.push_back(v.type == ValueType::String ? v.s : std::string());
    return out;
}

void OnTick()
{
    if (gInstalled || gRefused)
        return;

    // FindScriptFunction rate-limits its own retries and logs its own miss, so this is free to
    // run every frame until the UI package is loaded.
    gFunction = GameUI::FindScriptFunction("UIMovie", "CopyCommandQueue");
    if (!gFunction)
        return;

    gOrigFunc = ReadPtr(gFunction, kFnFuncOffset);
    if (!gOrigFunc) {
        LOG_WARN("FlashShadow: UIMovie.CopyCommandQueue has no native thunk — capture disabled");
        gRefused = true;
        return;
    }
    if (gOrigFunc == reinterpret_cast<void*>(&CaptureThunk)) {
        gInstalled = true; // already ours (a second Install after a failed Remove)
        return;
    }

    if (!WriteFunc(reinterpret_cast<void*>(&CaptureThunk))) {
        LOG_WARN("FlashShadow: could not write UFunction::Func — capture disabled");
        gRefused = true;
        return;
    }

    gInstalled = true;
    gStats.installed = true;
    LOG_INFO("FlashShadow: capturing UIMovie.CopyCommandQueue (UFunction 0x%p, thunk 0x%p)",
        gFunction, gOrigFunc);
}

void ForceRestore()
{
    if (!gInstalled || !gFunction)
        return;
    // A plain pointer write — no join, no library call, so this is legal under the loader lock
    // (CLAUDE.md's DLL_PROCESS_DETACH rule). It must happen FIRST for the same reason
    // MouseGate::ForceRestore does: after it, no game code can dispatch into this module.
    WriteFunc(gOrigFunc);
    gInstalled = false;
    gStats.installed = false;
    LOG_INFO("FlashShadow: capture removed");
}

bool IsActive()
{
    return gInstalled;
}

bool GetCall(int32_t cacheIndex, const char* name, Call& out)
{
    if (!name || cacheIndex < 0)
        return false;
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gPanels.find(cacheIndex);
    if (it == gPanels.end())
        return false;
    const std::vector<Call>& calls = it->second.calls;
    for (auto rit = calls.rbegin(); rit != calls.rend(); ++rit) {
        if (rit->name == name) {
            out = *rit;
            return true;
        }
    }
    return false;
}

bool GetTextCall(int32_t cacheIndex, Call& out)
{
    if (cacheIndex < 0)
        return false;
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gPanels.find(cacheIndex);
    if (it == gPanels.end())
        return false;
    const std::vector<Call>& calls = it->second.calls;
    if (calls.empty())
        return false;
    // Newest first: only batches carrying text are stored at all, so the newest IS the one that
    // last painted words onto this panel.
    out = calls.back();
    return true;
}

std::vector<Call> GetCalls(int32_t cacheIndex)
{
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gPanels.find(cacheIndex);
    if (it == gPanels.end())
        return {};
    return it->second.calls;
}

int32_t PanelCacheIndex(void* panel)
{
    if (!panel)
        return -1;
    void* mc = ReadPtr(panel, kPanelMCOffset);
    if (!mc)
        return -1;
    return ReadI32(mc, kMCCacheIndexOffset);
}

Stats GetStats()
{
    std::lock_guard<std::mutex> lock(gMutex);
    Stats s = gStats;
    s.installed = gInstalled;
    s.panels = gPanels.size();
    return s;
}

std::vector<int32_t> TrackedIndices()
{
    std::lock_guard<std::mutex> lock(gMutex);
    std::vector<int32_t> out;
    out.reserve(gPanels.size());
    for (const auto& [index, entry] : gPanels)
        out.push_back(index);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace FlashShadow
