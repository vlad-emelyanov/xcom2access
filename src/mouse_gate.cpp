#include "mouse_gate.h"
#include "script_call.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

namespace MouseGate {

namespace {

// Why this exists (measured live 2026-08-19 for world mode, 2026-08-20 for targeting):
//
// The game re-drives its own 3D cursor from the mouse EVERY FRAME, through one script function
// per input state, called from that state's PostProcessCheckGameLogic:
//
//   ActiveUnit_Moving    -> Mouse_CheckForPathing   (XComTacticalInput.uc:3010, class scope)
//   UsingTargetingMethod -> Mouse_CheckForFreeAim   (:1104, declared INSIDE the state)
//   UsingSecondaryTargetingMethod -> Mouse_CheckForFreeAim (:900, its own copy)
//
// Both deproject the mouse and call CursorSetLocation, so a keyboard push LANDS and is reverted
// one frame later. World mode: a Frida watch at 2 ms resolution saw the revert to a
// byte-identical cached mouse position 9 ms after the push. Targeting mode: a read-only hook on
// execSetLocation, filtered to the cursor instance, counted 180 calls in 3.001 s — exactly 60.0
// per second — with a stationary mouse and ONE distinct location for the whole sample. The game
// offers no flag that gates either (m_bCustomAllowCursorMovement only drives the gamepad ribbon
// UI; m_bAllowCursorAscensionAndDescension is read by DisableFloorChanges, which returns false
// before it ever gets there).
//
// The mechanism: UObject::CallFunction dispatches to (Context->*Func)(Stack, Result) whenever the
// callee's FunctionFlags carries FUNC_Native — for script functions Func holds
// UObject::ProcessInternal and the flag is clear. Setting the flag and pointing Func at a thunk
// that only consumes the call's parameter terminator makes every script callsite of that ONE
// function a no-op. Clicks, camera scroll and every other mouse feature stay untouched; both
// writes are plain data, restored exactly on release.
//
// The two families are gated INDEPENDENTLY because they are never live at the same time: the
// pathing function is not called from a targeting state and the free-aim ones are not called
// from the active-unit state. Engaging one while the other is engaged is therefore not a
// conflict, just a wasted write — but keeping them separate means each surface releases exactly
// what it took.

constexpr int32_t kFnFlagsOffset = 0xD0;   // UFunction::FunctionFlags (dword; pinned in
                                           // sdk_layout_checks.cpp)
constexpr int32_t kFnFuncOffset = 0xF0;    // UFunction::Func — the native dispatch pointer
                                           // (inside the generated header's padding; offset
                                           // from project_ue3_offsets, exercised by every
                                           // FUNC_Native call the mod has ever made)
constexpr uint32_t kFuncNative = 0x400;    // FUNC_Native
constexpr int32_t kFrameCodeOffset = 0x24; // FFrame::Code (from execSetLocation, VA
                                           // 0x140474940: the bytecode pointer the VM
                                           // advances lives at FFrame+0x24 in this build)
constexpr uint8_t kExEndFunctionParms = 0x16;

// Dispatch target while engaged. Convention is the member call the VM makes,
// (Context->*Func)(Stack, Result): RCX = the XComTacticalInput instance, RDX = FFrame*,
// R8 = result — a free function with three pointer params matches exactly. Every function gated
// here takes no parameters, so the param stream is a single EX_EndFunctionParms byte; consume it
// and do nothing. Guarded: a ProcessEvent-built frame points Code at the function body instead,
// and must not be advanced.
void SwallowThunk(void* /*self*/, void* frame, void* /*result*/)
{
    __try {
        uint8_t** code
            = reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(frame) + kFrameCodeOffset);
        if (*code && **code == kExEndFunctionParms)
            ++(*code);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// One gated function. `state` is null for a class-scope function; naming a state routes the
// lookup through GameUI::FindStateFunction, which walks one level deeper — Mouse_CheckForFreeAim
// does not exist at class scope at all (verified live 2026-08-20).
struct Gate {
    const char* cls;
    const char* state; // null = class scope
    const char* func;
    const char* label;

    void* fn = nullptr;     // resolved once — class-owned objects persist for the session
    bool refused = false;   // resolution found something unusable; never retry
    uint32_t origFlags = 0;
    void* origFunc = nullptr;
    bool installed = false;
};

enum {
    kGatePathing = 0,
    kGateFreeAim,
    kGateFreeAimSecondary,
    kGateCount,
};

Gate gGates[kGateCount] = {
    {"XComTacticalInput", nullptr, "Mouse_CheckForPathing", "world-mode cursor follow"},
    {"XComTacticalInput", "UsingTargetingMethod", "Mouse_CheckForFreeAim", "free-aim cursor follow"},
    {"XComTacticalInput", "UsingSecondaryTargetingMethod", "Mouse_CheckForFreeAim",
        "free-aim cursor follow (secondary movement)"},
};

bool ReadOriginals(Gate& g)
{
    __try {
        uint8_t* fn = static_cast<uint8_t*>(g.fn);
        g.origFlags = *reinterpret_cast<uint32_t*>(fn + kFnFlagsOffset);
        g.origFunc = *reinterpret_cast<void**>(fn + kFnFuncOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Engage writes Func before the flag (the flag is the dispatch gate); release clears the
// flag before Func. Either order leaves no window where the VM can call a half-written pair.
bool WriteGate(Gate& g, bool engage)
{
    __try {
        uint8_t* fn = static_cast<uint8_t*>(g.fn);
        uint32_t* flags = reinterpret_cast<uint32_t*>(fn + kFnFlagsOffset);
        void** func = reinterpret_cast<void**>(fn + kFnFuncOffset);
        if (engage) {
            *func = reinterpret_cast<void*>(&SwallowThunk);
            *flags = g.origFlags | kFuncNative;
        } else {
            *flags = g.origFlags;
            *func = g.origFunc;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SetGate(Gate& g, bool on)
{
    if (on == g.installed)
        return;

    if (on) {
        if (g.refused)
            return;
        if (!g.fn) {
            g.fn = g.state ? GameUI::FindStateFunction(g.cls, g.state, g.func)
                           : GameUI::FindScriptFunction(g.cls, g.func);
            if (!g.fn)
                return; // not resolvable yet — the finder rate-limits and logs its own miss
            if (!ReadOriginals(g)) {
                g.fn = nullptr;
                return;
            }
            if (g.origFlags & kFuncNative) {
                // Another mod (or a future patch) already made it native — hooking on top
                // would fight over the same two fields. Leave the mouse alone.
                LOG_WARN("MouseGate: %s is already FUNC_Native (flags 0x%08X) — refusing to "
                         "gate it",
                    g.func, g.origFlags);
                g.refused = true;
                return;
            }
            LOG_INFO("MouseGate: %s UFunction 0x%p flags 0x%08X Func 0x%p (ProcessInternal)",
                g.label, g.fn, g.origFlags, g.origFunc);
        }
        if (WriteGate(g, true)) {
            g.installed = true;
            LOG_INFO("MouseGate: %s suppressed", g.label);
        }
    } else {
        if (g.fn && WriteGate(g, false))
            LOG_INFO("MouseGate: %s restored", g.label);
        g.installed = false;
    }
}

} // namespace

void SetSwallow(bool on)
{
    SetGate(gGates[kGatePathing], on);
}

void SetFreeAimSwallow(bool on)
{
    // Both targeting states, together: which one the controller is in depends on whether the
    // ability was chosen after a move, and the aim keys behave identically in either.
    SetGate(gGates[kGateFreeAim], on);
    SetGate(gGates[kGateFreeAimSecondary], on);
}

bool Swallowed()
{
    for (const Gate& g : gGates) {
        if (g.installed)
            return true;
    }
    return false;
}

void ForceRestore()
{
    for (Gate& g : gGates) {
        if (g.installed) {
            WriteGate(g, false);
            g.installed = false;
            LOG_INFO("MouseGate: %s restored on shutdown", g.label);
        }
    }
}

} // namespace MouseGate
