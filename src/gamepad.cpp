#include "gamepad.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "log.h"
#include "menu_navigator.h"
#include "script_call.h"
#include "tts.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef X2ACCESS_DEVSERVER
#include <deque>
#include <mutex>
#include <string>
#endif

namespace Gamepad {

namespace {

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

constexpr int kSlots = 4;
constexpr WORD kOrdinalGetState = 2; // XINPUT1_3.dll's export ordinal for XInputGetState

// The press point sits well above XInput's own 7849 dead zone, and release has its own lower
// threshold so a stick held near the edge cannot chatter.
constexpr int kStickPress = 16000;
constexpr int kStickRelease = 10000;
constexpr int kTriggerPress = 128;

constexpr uint64_t kRepeatDelayMs = 400;
constexpr uint64_t kRepeatIntervalMs = 120;
constexpr uint64_t kScanEmptySlotsMs = 2000; // XInputGetState on an empty slot is slow
constexpr uint64_t kShellCheckMs = 100;

// A screen change leaves the layer detached for a few hundred ms. A second tap in that gap would
// otherwise reach the game's own A handler on whatever screen is arriving.
constexpr uint64_t kOwnershipGraceMs = 400;

enum class Source { Button, StickUp, StickDown, StickLeft, StickRight, LeftTrigger, RightTrigger };

struct Mapping {
    const char* name;
    Source source;
    WORD button; // XINPUT_GAMEPAD_* for Source::Button
    uint8_t vk;
    uint8_t mods;
    bool repeats;
};

// ⛔ B, Start, Back and the stick clicks are deliberately absent. They stay the game's: B reaches
// each screen's own OnUnrealCommand, where it shares a case with Escape, and Escape is not ours
// on a menu either. Y is details and X the secondary action, the layout the Civ VI mod settled on.
const Mapping kMap[] = {
    {"DUp", Source::Button, XINPUT_GAMEPAD_DPAD_UP, VK_UP, InputArbiter::ModNone, true},
    {"DDown", Source::Button, XINPUT_GAMEPAD_DPAD_DOWN, VK_DOWN, InputArbiter::ModNone, true},
    {"DLeft", Source::Button, XINPUT_GAMEPAD_DPAD_LEFT, VK_LEFT, InputArbiter::ModNone, true},
    {"DRight", Source::Button, XINPUT_GAMEPAD_DPAD_RIGHT, VK_RIGHT, InputArbiter::ModNone, true},
    {"LUp", Source::StickUp, 0, VK_UP, InputArbiter::ModNone, true},
    {"LDown", Source::StickDown, 0, VK_DOWN, InputArbiter::ModNone, true},
    {"LLeft", Source::StickLeft, 0, VK_LEFT, InputArbiter::ModNone, true},
    {"LRight", Source::StickRight, 0, VK_RIGHT, InputArbiter::ModNone, true},
    {"A", Source::Button, XINPUT_GAMEPAD_A, VK_RETURN, InputArbiter::ModNone, false},
    {"Y", Source::Button, XINPUT_GAMEPAD_Y, VK_SPACE, InputArbiter::ModNone, false},
    {"X", Source::Button, XINPUT_GAMEPAD_X, VK_BACK, InputArbiter::ModNone, false},
    {"LB", Source::Button, XINPUT_GAMEPAD_LEFT_SHOULDER, VK_TAB, InputArbiter::ModShift, false},
    {"RB", Source::Button, XINPUT_GAMEPAD_RIGHT_SHOULDER, VK_TAB, InputArbiter::ModNone, false},
    {"LT", Source::LeftTrigger, 0, VK_PRIOR, InputArbiter::ModNone, false},
    {"RT", Source::RightTrigger, 0, VK_NEXT, InputArbiter::ModNone, false},
};
constexpr int kMapCount = static_cast<int>(sizeof(kMap) / sizeof(kMap[0]));

constexpr WORD kMappedButtons = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN
    | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_Y
    | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER;

struct InputState {
    bool held = false;
    bool owned = false; // the press was taken here, so the release is ours too
    uint64_t nextRepeatMs = 0;
};
InputState sInputs[kMapCount];

GetStateFn sRealGetState = nullptr;
void** sIatSlot = nullptr;
bool sInstallTried = false;

// Published by the tick, read by the filter on whichever thread the engine polls from. While
// sLayerOwnsPad holds, every mapped input is hidden, not just those with a live claim: the engine
// can poll between a press and our tick, and an unclaimed X or Y on the settings screen is
// save-and-exit or reset-to-defaults. sLatched keeps a press hidden until its release after the
// layer lets go, so the game never sees half of one.
std::atomic<bool> sLayerOwnsPad{false};
std::atomic<WORD> sLatchedButtons{0};
std::atomic<bool> sLatchedStick{false};
std::atomic<bool> sLatchedLeftTrigger{false};
std::atomic<bool> sLatchedRightTrigger{false};

bool sSlotConnected[kSlots] = {};
uint64_t sNextSlotScanMs = 0;
bool sAnyPadLogged = false;

uint64_t sLastShellCheckMs = 0;
bool sInShell = false;
uint64_t sLastOwnedMs = 0;

Source sStickDirection = Source::Button; // Source::Button doubles as "centred"
bool sStickArmed = false;
bool sBackHeld = false;

uint64_t sPresses = 0;
uint64_t sTaken = 0;

#ifdef X2ACCESS_DEVSERVER
struct DevStep {
    WORD buttons = 0;
    SHORT lx = 0;
    SHORT ly = 0;
    BYTE lt = 0;
    BYTE rt = 0;
    uint32_t holdMs = 0;
    uint32_t gapMs = 0;
    int cmd = 0;          // UIUtilities_Input FXS_* command for the script-level stand-in
    bool engineOnly = false; // the mod's own layer has no mapping for it, so the game must get it
};
std::mutex sDevMutex;
std::deque<DevStep> sDevQueue;
std::atomic<WORD> sDevButtons{0};
std::atomic<SHORT> sDevLx{0};
std::atomic<SHORT> sDevLy{0};
std::atomic<BYTE> sDevLt{0};
std::atomic<BYTE> sDevRt{0};
std::atomic<bool> sHideFromEngine{false}; // the engine is told no pad is connected
std::atomic<uint64_t> sFilterCalls{0};
std::atomic<uint64_t> sFilterVirtual{0}; // calls that carried a virtual press
std::atomic<DWORD> sFilterLastThread{0};
std::atomic<DWORD> sFilterThreadA{0};
std::atomic<DWORD> sFilterThreadB{0};
std::atomic<DWORD> sDevPacket{0}; // moved on every change, so the engine notices a virtual press
bool sDevHolding = false;
int sDevHeldCmd = 0;      // the command whose release is still owed to the game
uint32_t sDevGapMs = 0;
uint64_t sDevNextMs = 0;
#endif

void ApplyVirtualPad(XINPUT_STATE* state)
{
#ifdef X2ACCESS_DEVSERVER
    state->dwPacketNumber += sDevPacket.load(std::memory_order_relaxed);
    state->Gamepad.wButtons |= sDevButtons.load(std::memory_order_relaxed);
    if (const SHORT lx = sDevLx.load(std::memory_order_relaxed))
        state->Gamepad.sThumbLX = lx;
    if (const SHORT ly = sDevLy.load(std::memory_order_relaxed))
        state->Gamepad.sThumbLY = ly;
    if (const BYTE lt = sDevLt.load(std::memory_order_relaxed))
        state->Gamepad.bLeftTrigger = lt;
    if (const BYTE rt = sDevRt.load(std::memory_order_relaxed))
        state->Gamepad.bRightTrigger = rt;
#else
    (void)state;
#endif
}

// What the engine calls instead of XInputGetState.
DWORD WINAPI FilteredGetState(DWORD user, XINPUT_STATE* state)
{
#ifdef X2ACCESS_DEVSERVER
    if (sHideFromEngine.load(std::memory_order_relaxed))
        return ERROR_DEVICE_NOT_CONNECTED;
#endif
    const DWORD result = sRealGetState(user, state);
    if (result != ERROR_SUCCESS || !state)
        return result;
    ApplyVirtualPad(state);
#ifdef X2ACCESS_DEVSERVER
    {
        const DWORD tid = GetCurrentThreadId();
        sFilterCalls.fetch_add(1, std::memory_order_relaxed);
        sFilterLastThread.store(tid, std::memory_order_relaxed);
        DWORD zero = 0;
        if (!sFilterThreadA.compare_exchange_strong(zero, tid) && sFilterThreadA.load() != tid) {
            zero = 0;
            sFilterThreadB.compare_exchange_strong(zero, tid);
        }
        if (sDevButtons.load(std::memory_order_relaxed))
            sFilterVirtual.fetch_add(1, std::memory_order_relaxed);
    }
#endif

    const bool layer = sLayerOwnsPad.load(std::memory_order_acquire);
    WORD hidden = sLatchedButtons.load(std::memory_order_acquire);
    if (layer)
        hidden |= kMappedButtons;
    state->Gamepad.wButtons &= static_cast<WORD>(~hidden);
    if (layer || sLatchedStick.load(std::memory_order_acquire)) {
        state->Gamepad.sThumbLX = 0;
        state->Gamepad.sThumbLY = 0;
    }
    if (layer || sLatchedLeftTrigger.load(std::memory_order_acquire))
        state->Gamepad.bLeftTrigger = 0;
    if (layer || sLatchedRightTrigger.load(std::memory_order_acquire))
        state->Gamepad.bRightTrigger = 0;
    return ERROR_SUCCESS;
}

void InstallFilter()
{
    sInstallTried = true;
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);

    for (; desc->Name; ++desc) {
        const char* dll = reinterpret_cast<const char*>(base + desc->Name);
        if (_strnicmp(dll, "XINPUT", 6) != 0)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
                || IMAGE_ORDINAL(names->u1.Ordinal) != kOrdinalGetState)
                continue;
            void** slot = reinterpret_cast<void**>(&slots->u1.Function);
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                LOG_ERROR("Gamepad: VirtualProtect failed on the XInputGetState import (%lu)",
                    GetLastError());
                return;
            }
            sRealGetState = reinterpret_cast<GetStateFn>(*slot);
            sIatSlot = slot;
            InterlockedExchangePointer(slot, reinterpret_cast<void*>(&FilteredGetState));
            VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
            LOG_INFO("Gamepad: filtering XInputGetState from %s (real 0x%p)", dll,
                reinterpret_cast<void*>(sRealGetState));
            return;
        }
    }
    LOG_WARN("Gamepad: no XInputGetState import found — controller support is off");
}

struct PadSnapshot {
    WORD buttons = 0;
    SHORT lx = 0;
    SHORT ly = 0;
    BYTE lt = 0;
    BYTE rt = 0;
    bool connected = false;
};

PadSnapshot ReadPads(uint64_t now)
{
    PadSnapshot snap;
    const bool scanEmpty = now >= sNextSlotScanMs;
    if (scanEmpty)
        sNextSlotScanMs = now + kScanEmptySlotsMs;

    for (DWORD i = 0; i < kSlots; i++) {
        if (!sSlotConnected[i] && !scanEmpty)
            continue;
        XINPUT_STATE state{};
        const bool connected = sRealGetState(i, &state) == ERROR_SUCCESS;
        sSlotConnected[i] = connected;
        if (!connected)
            continue;
        ApplyVirtualPad(&state);
        if (!snap.connected) {
            snap.lx = state.Gamepad.sThumbLX;
            snap.ly = state.Gamepad.sThumbLY;
        }
        snap.connected = true;
        snap.buttons |= state.Gamepad.wButtons;
        snap.lt = std::max(snap.lt, state.Gamepad.bLeftTrigger);
        snap.rt = std::max(snap.rt, state.Gamepad.bRightTrigger);
    }
    if (snap.connected && !sAnyPadLogged) {
        sAnyPadLogged = true;
        LOG_INFO("Gamepad: controller connected");
    }
    return snap;
}

bool VirtualStickActive()
{
#ifdef X2ACCESS_DEVSERVER
    return sDevLx.load(std::memory_order_relaxed) != 0 || sDevLy.load(std::memory_order_relaxed) != 0;
#else
    return false;
#endif
}

// One direction at a time, the dominant axis, and it holds until it falls below the release
// threshold — so a diagonal cannot flicker between two arrows.
void UpdateStickDirection(const PadSnapshot& snap)
{
    const int x = snap.lx;
    const int y = snap.ly;
    // ⛔ A sleeping wireless pad stays "connected" and reports a frozen stick, measured at full
    // up on both sticks with the packet number standing still. So a stick means nothing until it
    // has been seen near centre, or every press of anything would come with a phantom Up.
    if (abs(x) < kStickRelease && abs(y) < kStickRelease)
        sStickArmed = true;
    if (!sStickArmed && !VirtualStickActive()) {
        sStickDirection = Source::Button;
        return;
    }
    switch (sStickDirection) {
    case Source::StickUp:    if (y > kStickRelease) return; break;
    case Source::StickDown:  if (y < -kStickRelease) return; break;
    case Source::StickRight: if (x > kStickRelease) return; break;
    case Source::StickLeft:  if (x < -kStickRelease) return; break;
    default: break;
    }
    sStickDirection = Source::Button;
    if (abs(y) >= abs(x)) {
        if (y > kStickPress)
            sStickDirection = Source::StickUp;
        else if (y < -kStickPress)
            sStickDirection = Source::StickDown;
    } else {
        if (x > kStickPress)
            sStickDirection = Source::StickRight;
        else if (x < -kStickPress)
            sStickDirection = Source::StickLeft;
    }
}

bool IsPressed(const Mapping& m, const PadSnapshot& snap)
{
    switch (m.source) {
    case Source::Button:       return (snap.buttons & m.button) != 0;
    case Source::LeftTrigger:  return snap.lt >= kTriggerPress;
    case Source::RightTrigger: return snap.rt >= kTriggerPress;
    default:                   return sStickDirection == m.source;
    }
}

bool Send(const Mapping& m, bool down, bool repeat)
{
    InputArbiter::KeyEvent e{};
    e.vk = m.vk;
    e.mods = m.mods;
    e.down = down;
    e.repeat = repeat;
    return InputArbiter::InjectSynthetic(e);
}

void PublishLatches()
{
    WORD buttons = 0;
    bool stick = false;
    bool lt = false;
    bool rt = false;
    for (int i = 0; i < kMapCount; i++) {
        if (!sInputs[i].owned)
            continue;
        switch (kMap[i].source) {
        case Source::Button:       buttons |= kMap[i].button; break;
        case Source::LeftTrigger:  lt = true; break;
        case Source::RightTrigger: rt = true; break;
        default:                   stick = true; break;
        }
    }
    sLatchedButtons.store(buttons, std::memory_order_release);
    sLatchedStick.store(stick, std::memory_order_release);
    sLatchedLeftTrigger.store(lt, std::memory_order_release);
    sLatchedRightTrigger.store(rt, std::memory_order_release);
}

// The whole scope of this feature, in one predicate: UIFinalShell is on the stack under every
// shell screen and on no stack in the strategy or tactical layers. Pause menus and everything
// else keep the game's own pad handling.
bool LayerOwnsPad(uint64_t now, bool foreground)
{
    if (now - sLastShellCheckMs >= kShellCheckMs) {
        sLastShellCheckMs = now;
        void* shell = GameUI::FindMainMenuShell();
        sInShell = shell && GameUI::IsScreenOnStack(shell);
    }
    if (!sInShell || !foreground)
        return false;
    if (MenuNavigator::OwnsInput() && MenuNavigator::MenuAttached()) {
        sLastOwnedMs = now;
        return true;
    }
    return now - sLastOwnedMs < kOwnershipGraceMs;
}

#ifdef X2ACCESS_DEVSERVER
void ClearVirtualPad()
{
    sDevButtons.store(0, std::memory_order_relaxed);
    sDevLx.store(0, std::memory_order_relaxed);
    sDevLy.store(0, std::memory_order_relaxed);
    sDevLt.store(0, std::memory_order_relaxed);
    sDevRt.store(0, std::memory_order_relaxed);
    sDevPacket.fetch_add(1, std::memory_order_relaxed);
}

// ⛔ The engine does not poll XInputGetState in this build (measured: docs/input-map.md, "Virtual
// pad"), so a virtual press the game must see is delivered where XInput's result ends up: the live
// input object's own InputEvent, a press then a release, the same pair a pad hands it. Done when
// the mod's layer does not itself act on the press: a mapped input in the shell is the mod's, and
// delivering it to the game as well would act twice.
bool DevEngineEvent(int cmd, int actionMask)
{
    void* input = GameUI::FindLocalPlayerInput();
    if (!input)
        return false;
    struct {
        int32_t Cmd;
        int32_t ActionMask;
    } params{cmd, actionMask};
    return GameUI::CallScriptFunction(input, "InputEvent", &params);
}

constexpr int kActionPress = 1;    // UIUtilities_Input.FXS_ACTION_PRESS
constexpr int kActionRelease = 32; // UIUtilities_Input.FXS_ACTION_RELEASE

void PumpVirtualPad(uint64_t now)
{
    if (now < sDevNextMs)
        return;
    if (sDevHolding) {
        if (sDevHeldCmd) {
            DevEngineEvent(sDevHeldCmd, kActionRelease);
            sDevHeldCmd = 0;
        }
        ClearVirtualPad();
        sDevHolding = false;
        sDevNextMs = now + sDevGapMs;
        return;
    }
    DevStep step;
    {
        std::lock_guard<std::mutex> lock(sDevMutex);
        if (sDevQueue.empty())
            return;
        step = sDevQueue.front();
        sDevQueue.pop_front();
    }
    // The mod's layer acts on what it maps while it owns the pad; everything else is the game's.
    const bool modActs = sLayerOwnsPad.load(std::memory_order_acquire) && !step.engineOnly;
    if (step.cmd && !modActs) {
        const bool sent = DevEngineEvent(step.cmd, kActionPress);
        sDevHeldCmd = sent ? step.cmd : 0;
        LOG_DEBUG("Gamepad: virtual press cmd %d -> InputEvent %s", step.cmd,
            sent ? "sent" : "NOT sent (no input object)");
    }
    sDevButtons.store(step.buttons, std::memory_order_relaxed);
    sDevLx.store(step.lx, std::memory_order_relaxed);
    sDevLy.store(step.ly, std::memory_order_relaxed);
    sDevLt.store(step.lt, std::memory_order_relaxed);
    sDevRt.store(step.rt, std::memory_order_relaxed);
    sDevPacket.fetch_add(1, std::memory_order_relaxed);
    sDevHolding = true;
    sDevGapMs = step.gapMs;
    sDevNextMs = now + step.holdMs;
}

bool DevPadBusy()
{
    if (sDevHolding)
        return true;
    std::lock_guard<std::mutex> lock(sDevMutex);
    return !sDevQueue.empty();
}
#endif

} // namespace

void OnTick()
{
    if (!sInstallTried)
        InstallFilter();
    if (!sRealGetState)
        return;

    const uint64_t now = GetTickCount64();
    bool foreground = InputArbiter::IsGameForeground();
#ifdef X2ACCESS_DEVSERVER
    PumpVirtualPad(now);
    // A harness run should not need the window raised: it presses nothing the player would feel.
    foreground = foreground || DevPadBusy();
#endif

    const PadSnapshot snap = ReadPads(now);
    const bool owns = LayerOwnsPad(now, foreground);
    sLayerOwnsPad.store(owns, std::memory_order_release);
    UpdateStickDirection(snap);

    // ⛔ A screen reader cuts its own speech when it sees a key go down, under the player's own
    // settings, and a pad press is no key: without this every line would queue behind the last,
    // and scrolling a menu would lag further with each press. So a press stops speech where the
    // keypress would have, and the mod still never interrupts on its own account. B is unmapped,
    // so it never reaches the loop below.
    const bool backPressed = foreground && (snap.buttons & XINPUT_GAMEPAD_B) != 0;
    if (owns && backPressed && !sBackHeld)
        TTS::Silence();
    sBackHeld = backPressed;

    for (int i = 0; i < kMapCount; i++) {
        const Mapping& m = kMap[i];
        InputState& s = sInputs[i];
        const bool pressed = foreground && IsPressed(m, snap);

        if (pressed && !s.held) {
            s.held = true;
            s.owned = owns;
            s.nextRepeatMs = now + kRepeatDelayMs;
            sPresses++;
            if (owns) {
                TTS::Silence();
                const bool claimed = Send(m, /*down*/ true, /*repeat*/ false);
                sTaken += claimed ? 1 : 0;
                LOG_DEBUG("Gamepad: %s -> vk 0x%02X mods 0x%X (%s)", m.name, m.vk, m.mods,
                    claimed ? "claimed" : "no claim");
            }
        } else if (pressed && s.owned && m.repeats && now >= s.nextRepeatMs) {
            s.nextRepeatMs = now + kRepeatIntervalMs;
            TTS::Silence();
            Send(m, /*down*/ true, /*repeat*/ true);
        } else if (!pressed && s.held) {
            if (s.owned)
                Send(m, /*down*/ false, /*repeat*/ false);
            s.held = false;
            s.owned = false;
        }
    }
    PublishLatches();
}

void Shutdown()
{
    if (!sIatSlot)
        return;
    DWORD oldProtect = 0;
    if (VirtualProtect(sIatSlot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        InterlockedExchangePointer(sIatSlot, reinterpret_cast<void*>(sRealGetState));
        VirtualProtect(sIatSlot, sizeof(void*), oldProtect, &oldProtect);
    }
    sIatSlot = nullptr;
}

#ifdef X2ACCESS_DEVSERVER
std::string DevQueuePresses(const std::string& names, unsigned holdMs, unsigned gapMs, bool engineEvents)
{
    std::deque<DevStep> steps;
    size_t pos = 0;
    while (pos <= names.size()) {
        size_t comma = names.find(',', pos);
        if (comma == std::string::npos)
            comma = names.size();
        const std::string name = names.substr(pos, comma - pos);
        pos = comma + 1;
        if (name.empty())
            continue;

        DevStep step;
        step.holdMs = holdMs;
        step.gapMs = gapMs;
        // UIUtilities_Input.FXS_* commands; the left stick stands in as its virtual direction keys.
        struct Named { const char* name; WORD button; int cmd; bool engineOnly; };
        static const Named kNamed[] = {
            {"A", XINPUT_GAMEPAD_A, 300, false}, {"B", XINPUT_GAMEPAD_B, 301, true},
            {"X", XINPUT_GAMEPAD_X, 302, false}, {"Y", XINPUT_GAMEPAD_Y, 303, false},
            {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER, 330, false},
            {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER, 331, false},
            {"Start", XINPUT_GAMEPAD_START, 321, true}, {"Back", XINPUT_GAMEPAD_BACK, 320, true},
            {"L3", XINPUT_GAMEPAD_LEFT_THUMB, 311, true}, {"R3", XINPUT_GAMEPAD_RIGHT_THUMB, 313, true},
            {"DUp", XINPUT_GAMEPAD_DPAD_UP, 350, false}, {"DRight", XINPUT_GAMEPAD_DPAD_RIGHT, 352, false},
            {"DDown", XINPUT_GAMEPAD_DPAD_DOWN, 354, false}, {"DLeft", XINPUT_GAMEPAD_DPAD_LEFT, 356, false},
        };
        bool found = false;
        for (const Named& n : kNamed) {
            if (name == n.name) {
                step.buttons = n.button;
                step.cmd = n.cmd;
                step.engineOnly = n.engineOnly;
                found = true;
            }
        }
        if (!found) {
            found = true;
            if (name == "LUp") { step.ly = 30000; step.cmd = 370; }
            else if (name == "LDown") { step.ly = -30000; step.cmd = 371; }
            else if (name == "LLeft") { step.lx = -30000; step.cmd = 372; }
            else if (name == "LRight") { step.lx = 30000; step.cmd = 373; }
            else if (name == "LT") { step.lt = 255; step.cmd = 332; }
            else if (name == "RT") { step.rt = 255; step.cmd = 333; }
            else found = false;
        }
        if (!found)
            return name;
        if (!engineEvents)
            step.cmd = 0; // XInput only: for measuring what the engine does with the pad itself
        steps.push_back(step);
    }
    std::lock_guard<std::mutex> lock(sDevMutex);
    sDevQueue.insert(sDevQueue.end(), steps.begin(), steps.end());
    return {};
}

void DevSetHideFromEngine(bool hide)
{
    sHideFromEngine.store(hide);
}

std::string DevStatus()
{
    char line[512];
    int connected = 0;
    for (bool slot : sSlotConnected)
        connected += slot ? 1 : 0;
    snprintf(line, sizeof(line),
        "filter %s%s, pads %d, in shell %d, foreground %d, layer owns pad %d, presses %llu, "
        "taken by claims %llu, engine filter calls %llu (virtual %llu), threads %lu/%lu, last %lu\n",
        sIatSlot ? "installed" : "NOT installed", sHideFromEngine.load() ? " (engine told: no pad)" : "", connected, sInShell,
        InputArbiter::IsGameForeground(), sLayerOwnsPad.load(),
        static_cast<unsigned long long>(sPresses), static_cast<unsigned long long>(sTaken),
        static_cast<unsigned long long>(sFilterCalls.load()),
        static_cast<unsigned long long>(sFilterVirtual.load()),
        static_cast<unsigned long>(sFilterThreadA.load()),
        static_cast<unsigned long>(sFilterThreadB.load()),
        static_cast<unsigned long>(sFilterLastThread.load()));
    return line;
}
#endif

} // namespace Gamepad
