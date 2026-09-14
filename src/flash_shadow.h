#pragma once

// The Flash command-queue capture — what a panel PUSHED into Scaleform, recorded at the one
// instant the engine has it all in one place.
//
// WHY THIS EXISTS. CLAUDE.md's house-style rule says a list row in this game retains no text:
// `UIPersonnel_ListItem`, `UIInventory_ListItem`, every panel of `UITacticalHUD` and all 118
// variants of `UIAlert` take their words as PARAMETERS and push them straight through
// `MC.BeginFunctionOp` / `QueueString`, bypassing `SetText`. The retained `Text` is empty on a
// widget that is visibly showing a name. Until now the only answers were re-deriving the words in
// the companion (thousands of lines that drift from the game's own source) or reading them back
// out of the movie with `GetVariableString` — which `UIMCController.uc` itself calls "an
// immediate and blocking operation … very expensive, use sparingly", and which cannot reach an
// `XComScrollingTextField` at all, because its text lives behind an AS2 addProperty getter that
// GFx's path API cannot run.
//
// This module takes the third route: read the words IN FLIGHT, on their way out.
//
// THE TAP. Every `MC.QueueString` lands in that controller's own `CommandQueue`; `EndOp` flushes
// it into the movie-wide `UIMovie.CommandQueue`. Once per frame `UIMovie.ProcessQueuedCommands`
// (a UI-update subscriber, so: game thread) does exactly this:
//
//     if(CommandQueue.Length > 0) {
//         CopyCommandQueue(CommandBuffer);   // native — snapshots the whole queue
//         CommandQueue.length = 0;           // …and now it is gone
//         ControlOps(CommandBuffer);         // into Flash
//     }
//
// So `UIMovie.CommandQueue` holds every string every panel pushed this frame, fully populated,
// for exactly as long as it takes `CopyCommandQueue` to run. ⛔ That is why POLLING it never
// worked and the attempt is recorded in game_ui.cpp: by the time anything else looks, the array
// is empty. The instant is only reachable from inside the call.
//
// ⛔ NO MinHook, and this is the point worth keeping. `CopyCommandQueue` is a NATIVE function, so
// its `UFunction::Func` (+0xF0) ALREADY holds the exec thunk the VM dispatches to — both
// `UObject::CallFunction` (for `FUNC_Native` callees) and `ProcessEvent` itself read that one
// field (`pcVar3 = *(code **)(param_2 + 0xf0)`, decompiled at moduleBase+0x143B50). Chaining it
// is a POINTER WRITE, not an inline patch: save the original, write ours, call theirs. That is
// the same mechanism `mouse_gate.cpp` already uses and the project already trusts, so this adds
// no dependency, no trampoline and no code patching.
//
// ⛔ AND NO FFrame PARSING. At native-thunk entry the parameters are NOT yet evaluated — `Code`
// points at bytecode the thunk itself steps. Reading the `out` parameter would mean replaying the
// VM's own param evaluation, which is precisely the trap docs/object-scanner-recon.md §4.6 warns
// against. We never touch the frame: `this` is the `UIMovie`, and the payload is read out of its
// `CommandQueue` field by plain reflection, then handed to the original thunk untouched.
//
// WHAT IT IS NOT. This captures the PUSH stream, not Flash's internal state. Which row is
// selected, whether a button is disabled, and anything an `.as` computed after receiving its
// parameters are all still invisible here — those stay with the widget tree and the companion.
// What this owns is the WORDS.

#include <cstdint>
#include <string>
#include <vector>

namespace FlashShadow {

// `ASType` from GFxMoviePlayer.uc:205. Values are the engine's, not ours.
enum class ValueType : uint8_t {
    Undefined = 0,
    Null = 1,
    Number = 2,
    String = 3,
    Boolean = 4,
};

struct Value {
    ValueType type = ValueType::Undefined;
    std::string s; // UTF-8; meaningful only when type == String
    float n = 0.0f;
    bool b = false;
};

// One decoded operation, i.e. one `BeginOp … EndOp` batch.
//
// The grammar is UIMCController.uc's own (the opcode constants at :13 and the `ControlOps`
// comment at UIMovie.uc:1147): a leading AS_Number carrying `CacheIndex | OP`, then the operands,
// then an AS_Null delimiter. `child` is populated only by the CHILD_* opcodes, which prefix the
// operand list with a path.
struct Call {
    std::string child;       // CHILD_* ops only; empty otherwise
    std::string name;        // function name, or field name when `isField`
    bool isField = false;    // FIELD_OP / CHILD_FIELD_OP rather than a function call
    std::vector<Value> args; // operands in queue order
    /// Recording order across the WHOLE capture, strictly increasing, never reused. A consumer
    /// that reacts to a push (the doom meter's message) remembers the last `seq` it acted on and
    /// so can tell a new push from the previous one still sitting in the panel's history — which
    /// a position or a count cannot, because the per-panel ring evicts from the front.
    uint64_t seq = 0;

    // ⛔ Non-string operands are KEPT, as typed entries rather than dropped. A repeating field
    // emitted without its empties shifts every value after it — CLAUDE.md's wire-format rule —
    // and the same applies here: `UpdateGuerrillaOpsSplash` pushes title, subtitle, body, IMAGE,
    // confirm, ignore, and a consumer that reads by position must see the image slot occupied.
    /// The string operands only, in order, as a convenience for consumers that read by position
    /// within the text (every arg the panel received, non-strings rendered as empty entries).
    std::vector<std::string> StringArgs() const;
};

/// Install the capture the first time `UIMovie.CopyCommandQueue` resolves, then do nothing.
/// Called from the frame pump. Game thread only.
void OnTick();

/// Restore the original dispatch pointer. Safe under the loader lock — it is a plain data write
/// and joins nothing, the same contract MouseGate::ForceRestore has.
void ForceRestore();

/// Is the capture currently installed?
bool IsActive();

/// The most recent recorded operation named `name` on `cacheIndex`. False when nothing matches.
bool GetCall(int32_t cacheIndex, const char* name, Call& out);

// ⛔ THERE IS NO "the batch that painted this panel" ACCESSOR, and the absence is deliberate.
// One used to exist — `GetTextCall`, returning the newest batch carrying a string, on the theory
// that a panel paints itself with a single `Update*` call. `UIAlert` disproved it twice over: at
// least seven of its builders paint through several batches, and the ones showing a soldier
// headshot append a LATER `UpdateImage` whose only operand is a texture path, so "the newest
// batch carrying a string" was a picture and the screen read as empty. A consumer that cannot
// name the call it wants must take `GetCalls` and read them ALL — see ReadAlertCaptured.

/// Every operation recorded against `cacheIndex`, oldest first.
std::vector<Call> GetCalls(int32_t cacheIndex);

/// A panel's `UIMCController.CacheIndex` — the identity the command stream is keyed by. Reads
/// `UIPanel.MC` (+0x2D0) then `UIMCController.CacheIndex` (+0x68), both SEH-guarded and pinned in
/// sdk_layout_checks.cpp. Returns -1 when unreadable, which is also the value the game itself
/// writes into a removed controller.
int32_t PanelCacheIndex(void* panel);

// ---- dev harness ----

struct Stats {
    bool installed = false;
    uint64_t flushes = 0;   // CopyCommandQueue calls seen
    uint64_t opsSeen = 0;   // operations decoded
    uint64_t opsKept = 0;   // …of those, ones carrying a string (the rest are discarded)
    uint64_t evictions = 0; // panels dropped, by CLEANUP_OP or by the LRU cap
    size_t panels = 0;      // entries currently held
};

Stats GetStats();

/// Every cache index currently holding recorded operations, ascending. For the dev harness.
std::vector<int32_t> TrackedIndices();

} // namespace FlashShadow
