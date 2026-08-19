#include "mouse_gate.h"
#include "game_ui.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

namespace MouseGate {

namespace {

// Why this exists (measured live 2026-08-19, mid-mission):
//
// XComTacticalInput.Mouse_CheckForPathing pushes the mouse pick point through
// CursorSetLocation EVERY FRAME while the mouse is the active input device — it is the last
// call in the input state's PostProcessCheckGameLogic (XComTacticalInput.uc:3010), gated only
// on IsMouseActive(). A Frida watch at 2 ms resolution showed a keyboard CursorSetLocation
// push LANDING on the cursor pawn and being reverted to the byte-identical cached mouse
// position 9 ms later; a ProcessEvent hook showed the revert entering as script->script
// bytecode, not native. The game offers no flag that gates it (m_bCustomAllowCursorMovement
// only drives the gamepad ribbon UI, verified in SrcOrig).
//
// The mechanism: UObject::CallFunction dispatches a call to (Context->*Func)(Stack, Result)
// whenever the callee's FunctionFlags carries FUNC_Native — for script functions Func holds
// UObject::ProcessInternal and the flag is clear. Setting the flag and pointing Func at a
// thunk that only consumes the call's parameter terminator makes every script callsite of
// this ONE function a no-op, while clicks, free-aim targeting (Mouse_CheckForFreeAim), camera
// scroll and every other mouse feature stay untouched. Both writes are plain data, restored
// exactly on release.

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

void* gFn = nullptr;       // the UFunction, resolved once — class-owned objects persist
bool gRefused = false;     // resolution found something unusable; never retry
uint32_t gOrigFlags = 0;
void* gOrigFunc = nullptr;
bool gInstalled = false;

// Dispatch target while engaged. Convention is the member call the VM makes,
// (Context->*Func)(Stack, Result): RCX = the XComTacticalInput instance, RDX = FFrame*,
// R8 = result — a free function with three pointer params matches exactly. The function
// takes no parameters, so the param stream is a single EX_EndFunctionParms byte; consume it
// and do nothing. Guarded: a ProcessEvent-built frame points Code at the function body
// instead, and must not be advanced.
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

bool ReadOriginals()
{
    __try {
        uint8_t* fn = static_cast<uint8_t*>(gFn);
        gOrigFlags = *reinterpret_cast<uint32_t*>(fn + kFnFlagsOffset);
        gOrigFunc = *reinterpret_cast<void**>(fn + kFnFuncOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Engage writes Func before the flag (the flag is the dispatch gate); release clears the
// flag before Func. Either order leaves no window where the VM can call a half-written pair.
bool WriteGate(bool engage)
{
    __try {
        uint8_t* fn = static_cast<uint8_t*>(gFn);
        uint32_t* flags = reinterpret_cast<uint32_t*>(fn + kFnFlagsOffset);
        void** func = reinterpret_cast<void**>(fn + kFnFuncOffset);
        if (engage) {
            *func = reinterpret_cast<void*>(&SwallowThunk);
            *flags = gOrigFlags | kFuncNative;
        } else {
            *flags = gOrigFlags;
            *func = gOrigFunc;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void SetSwallow(bool on)
{
    if (on == gInstalled)
        return;

    if (on) {
        if (gRefused)
            return;
        if (!gFn) {
            gFn = GameUI::FindScriptFunction("XComTacticalInput", "Mouse_CheckForPathing");
            if (!gFn)
                return; // not resolvable yet — the finder rate-limits and logs its own miss
            if (!ReadOriginals()) {
                gFn = nullptr;
                return;
            }
            if (gOrigFlags & kFuncNative) {
                // Another mod (or a future patch) already made it native — hooking on top
                // would fight over the same two fields. Leave the mouse alone.
                LOG_WARN("MouseGate: Mouse_CheckForPathing is already FUNC_Native "
                         "(flags 0x%08X) — refusing to gate it",
                    gOrigFlags);
                gRefused = true;
                return;
            }
            LOG_INFO("MouseGate: UFunction 0x%p flags 0x%08X Func 0x%p (ProcessInternal)",
                gFn, gOrigFlags, gOrigFunc);
        }
        if (WriteGate(true)) {
            gInstalled = true;
            LOG_INFO("MouseGate: mouse cursor-follow suppressed — tile cursor owns world mode");
        }
    } else {
        if (gFn && WriteGate(false))
            LOG_INFO("MouseGate: mouse cursor-follow restored");
        gInstalled = false;
    }
}

bool Swallowed()
{
    return gInstalled;
}

void ForceRestore()
{
    if (gInstalled) {
        WriteGate(false);
        gInstalled = false;
        LOG_INFO("MouseGate: restored on shutdown");
    }
}

} // namespace MouseGate
