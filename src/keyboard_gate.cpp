#include "keyboard_gate.h"

#include "game_ui.h"
#include "log.h"
#include "script_call.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>

// Keeps the keyboard alive in controller mode. docs/input-map.md, "The controller", has the
// measurement: with the 2D movie's MouseActive false, XComInputBase.InputEvent drops every
// keyboard and mouse event at :483 (IsEventWithinInputTypeRange), and bAllowAllInput lifts it.

namespace KeyboardGate {

namespace {

// XComInputBase flag dword at 0x20C, bAllowAllInput = mask 0x80; UIMovie flag dword at 0x214,
// MouseActive = mask 0x04. Both pinned in sdk_layout_checks.cpp.
constexpr int32_t kInputFlagsOffset = 0x20C;
constexpr uint32_t kAllowAllInputMask = 0x80;
constexpr int32_t kMovieFlagsOffset = 0x214;
constexpr uint32_t kMouseActiveMask = 0x04;

constexpr uint64_t kPollMs = 300;

std::atomic<bool> sControllerMode{false};
uint64_t sNextPollMs = 0;
bool sWeSetIt = false;
void* sLoggedFor = nullptr; // compared, never dereferenced: which input object we last announced

bool ReadDword(const void* base, int32_t offset, uint32_t* out)
{
    __try {
        *out = *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(base) + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteDword(void* base, int32_t offset, uint32_t value)
{
    __try {
        *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(base) + offset) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void OnTick()
{
    const uint64_t now = GetTickCount64();
    if (now < sNextPollMs)
        return;
    sNextPollMs = now + kPollMs;

    // Re-resolved on every poll: there is one input object per map, and a pointer kept across a
    // travel names a stranger.
    void* input = GameUI::FindLocalPlayerInput();
    if (!input)
        return;

    struct {
        void* ReturnValue;
    } movie{};
    if (!GameUI::CallScriptFunction(input, "Get2DMovie", &movie) || !movie.ReturnValue)
        return; // no movie yet: the mode is unknown, so write nothing

    uint32_t movieFlags = 0;
    uint32_t inputFlags = 0;
    if (!ReadDword(movie.ReturnValue, kMovieFlagsOffset, &movieFlags)
        || !ReadDword(input, kInputFlagsOffset, &inputFlags))
        return;

    const bool mouseActive = (movieFlags & kMouseActiveMask) != 0;
    sControllerMode.store(!mouseActive, std::memory_order_relaxed);
    const bool allowed = (inputFlags & kAllowAllInputMask) != 0;

    if (!mouseActive) {
        if (allowed)
            return;
        if (!WriteDword(input, kInputFlagsOffset, inputFlags | kAllowAllInputMask))
            return;
        sWeSetIt = true;
        if (sLoggedFor != input) {
            sLoggedFor = input;
            LOG_INFO("KeyboardGate: controller mode - bAllowAllInput set on input object 0x%p", input);
        }
        return;
    }

    // Mouse mode: the gate already lets the keyboard through, so this is never written. Only a
    // bit we set ourselves is taken back, and never while the settings screen is up: it raises the
    // bit on entry and lowers it on exit (UIOptionsPCScreen.uc:927, :2919) and would lose it.
    if (!sWeSetIt)
        return;
    void* options = GameUI::FindOptionsScreen();
    if (options && GameUI::IsScreenOnStack(options))
        return;
    sWeSetIt = false;
    if (allowed && WriteDword(input, kInputFlagsOffset, inputFlags & ~kAllowAllInputMask))
        LOG_INFO("KeyboardGate: mouse mode - bAllowAllInput cleared again");
}

bool ControllerMode()
{
    return sControllerMode.load(std::memory_order_relaxed);
}

bool SendCommandTap(int cmd)
{
    constexpr int kActionPress = 1;    // UIUtilities_Input.FXS_ACTION_PRESS
    constexpr int kActionRelease = 32; // UIUtilities_Input.FXS_ACTION_RELEASE
    void* input = GameUI::FindLocalPlayerInput();
    if (!input)
        return false;
    // XComInputBase.InputEvent(int cmd, optional int ActionMask): the optional slot is still a
    // slot (pinned in sdk_layout_checks.cpp).
    struct {
        int32_t Cmd;
        int32_t ActionMask;
    } press{cmd, kActionPress}, release{cmd, kActionRelease};
    return GameUI::CallScriptFunction(input, "InputEvent", &press)
        && GameUI::CallScriptFunction(input, "InputEvent", &release);
}

} // namespace KeyboardGate
