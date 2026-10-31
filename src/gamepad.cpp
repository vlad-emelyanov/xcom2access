#include "gamepad.h"
#include "avenger.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "keyboard_gate.h"
#include "log.h"
#include "menu_navigator.h"
#include "script_call.h"
#include "tactical_hud.h"
#include "tile_reader.h"
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

// The layer the pad is in this tick. One computed value decides what the mod acts on and what is
// hidden from the engine; None is the vanilla pad, untouched.
enum class Layer : uint8_t { None, Menu, Hud, World, Targeting, AntFarm };
constexpr int kLayerCount = 6;
constexpr const char* kLayerNames[kLayerCount] = {"none", "menu", "hud", "world", "targeting", "antfarm"};

constexpr uint8_t LayerBit(Layer l)
{
    return l == Layer::None ? 0 : static_cast<uint8_t>(1u << (static_cast<int>(l) - 1));
}
constexpr uint8_t kLbMenu = 1, kLbHud = 2, kLbWorld = 4, kLbTarget = 8, kLbFarm = 16;
constexpr uint8_t kLbMenuHud = kLbMenu | kLbHud;

enum class Source {
    Button,
    LStickUp, LStickDown, LStickLeft, LStickRight,
    RStickUp, RStickDown, RStickLeft, RStickRight,
    LeftTrigger, RightTrigger,
};

// Any: applies with or without LT held. LtHeld / LtFree: a chord, or the plain press while no chord
// is active. "LT held" means LT was taken by a tap entry (Fire::TapRelease), so an LT the game saw
// going down never turns later presses into chords.
enum class Chord : uint8_t { Any, LtHeld, LtFree };

// Press: the key goes down on the press and up on the release (repeating if asked). TapRelease:
// nothing on the press, and the key is tapped on the release unless a chord fired meanwhile; only
// LT uses it, because LT is both a button and the chord modifier.
enum class Fire : uint8_t { Press, TapRelease };

struct Mapping {
    const char* name;
    Source source;
    WORD button;    // XINPUT_GAMEPAD_* for Source::Button
    uint8_t layers; // kLb* bits the entry applies in
    Chord chord;
    Fire fire;
    uint8_t vk;
    uint8_t mods;
    bool repeats;
};

constexpr uint8_t kNone = InputArbiter::ModNone;
constexpr uint8_t kCtrl = InputArbiter::ModCtrl;
constexpr uint8_t kShift = InputArbiter::ModShift;
constexpr Chord kAny = Chord::Any;
constexpr Chord kHeld = Chord::LtHeld;
constexpr Chord kFree = Chord::LtFree;
constexpr Fire kPress = Fire::Press;
constexpr Fire kTap = Fire::TapRelease;
constexpr Source kBtn = Source::Button;

// ⛔ Order matters in one place: the TapRelease entries (LT) come FIRST, because the tick works
// out "LT is held as a chord modifier" while it walks the table, and the chord entries after them
// read it in the same pass.
//
// ⛔ B, Start, Back and the stick clicks are the game's in every layer but one: B reaches each
// screen's own OnUnrealCommand, where it shares a case with Escape, and Escape is not ours on a
// menu either. The Hud layer is the exception, where B is the claim "HUD layer: leave" (Escape).
// Y is details and X the secondary action, the layout the Civ VI mod settled on.
//
// ⚠ Repeats: the tile cursor's claims ignore a repeat (one press, one tile), so every World entry
// has repeats off, and a held stick steps once there. Accepted for now. A repeat is also a
// TTS::Silence, which would cut the very step it follows, so a repeat is never sent to a claim
// that ignores it.
const Mapping kMap[] = {
    // LT: tap = enter the HUD layer (World, Avenger grid) or re-announce the shot (Targeting).
    {"LT", Source::LeftTrigger, 0, kLbWorld | kLbFarm, kAny, kTap, VK_TAB, kNone, false},
    {"LT", Source::LeftTrigger, 0, kLbTarget, kAny, kTap, VK_DELETE, kNone, false},
    {"LT", Source::LeftTrigger, 0, kLbMenuHud, kAny, kPress, VK_PRIOR, kNone, false},
    {"RT", Source::RightTrigger, 0, kLbMenuHud, kAny, kPress, VK_NEXT, kNone, false},

    // Menu and Hud: the shell's table, plus the right stick for the Geoscape scanner.
    {"DUp", kBtn, XINPUT_GAMEPAD_DPAD_UP, kLbMenuHud, kAny, kPress, VK_UP, kNone, true},
    {"DDown", kBtn, XINPUT_GAMEPAD_DPAD_DOWN, kLbMenuHud, kAny, kPress, VK_DOWN, kNone, true},
    {"DLeft", kBtn, XINPUT_GAMEPAD_DPAD_LEFT, kLbMenuHud, kAny, kPress, VK_LEFT, kNone, true},
    {"DRight", kBtn, XINPUT_GAMEPAD_DPAD_RIGHT, kLbMenuHud, kAny, kPress, VK_RIGHT, kNone, true},
    {"LUp", Source::LStickUp, 0, kLbMenuHud, kAny, kPress, VK_UP, kNone, true},
    {"LDown", Source::LStickDown, 0, kLbMenuHud, kAny, kPress, VK_DOWN, kNone, true},
    {"LLeft", Source::LStickLeft, 0, kLbMenuHud, kAny, kPress, VK_LEFT, kNone, true},
    {"LRight", Source::LStickRight, 0, kLbMenuHud, kAny, kPress, VK_RIGHT, kNone, true},
    {"RUp", Source::RStickUp, 0, kLbMenuHud, kAny, kPress, VK_PRIOR, kCtrl, true},
    {"RDown", Source::RStickDown, 0, kLbMenuHud, kAny, kPress, VK_NEXT, kCtrl, true},
    {"RLeft", Source::RStickLeft, 0, kLbMenuHud, kAny, kPress, VK_PRIOR, kNone, true},
    {"RRight", Source::RStickRight, 0, kLbMenuHud, kAny, kPress, VK_NEXT, kNone, true},
    {"A", kBtn, XINPUT_GAMEPAD_A, kLbMenuHud, kAny, kPress, VK_RETURN, kNone, false},
    {"Y", kBtn, XINPUT_GAMEPAD_Y, kLbMenuHud, kAny, kPress, VK_SPACE, kNone, false},
    {"X", kBtn, XINPUT_GAMEPAD_X, kLbMenuHud, kAny, kPress, VK_BACK, kNone, false},
    {"LB", kBtn, XINPUT_GAMEPAD_LEFT_SHOULDER, kLbMenuHud, kAny, kPress, VK_TAB, kShift, false},
    {"RB", kBtn, XINPUT_GAMEPAD_RIGHT_SHOULDER, kLbMenuHud, kAny, kPress, VK_TAB, kNone, false},
    {"B", kBtn, XINPUT_GAMEPAD_B, kLbHud, kAny, kPress, VK_ESCAPE, kNone, false},

    // World: the tile cursor and the object scanner.
    {"DUp", kBtn, XINPUT_GAMEPAD_DPAD_UP, kLbWorld, kFree, kPress, VK_UP, kNone, false},
    {"DDown", kBtn, XINPUT_GAMEPAD_DPAD_DOWN, kLbWorld, kFree, kPress, VK_DOWN, kNone, false},
    {"DLeft", kBtn, XINPUT_GAMEPAD_DPAD_LEFT, kLbWorld, kFree, kPress, VK_LEFT, kNone, false},
    {"DRight", kBtn, XINPUT_GAMEPAD_DPAD_RIGHT, kLbWorld, kFree, kPress, VK_RIGHT, kNone, false},
    {"LUp", Source::LStickUp, 0, kLbWorld, kAny, kPress, VK_UP, kNone, false},
    {"LDown", Source::LStickDown, 0, kLbWorld, kAny, kPress, VK_DOWN, kNone, false},
    {"LLeft", Source::LStickLeft, 0, kLbWorld, kAny, kPress, VK_LEFT, kNone, false},
    {"LRight", Source::LStickRight, 0, kLbWorld, kAny, kPress, VK_RIGHT, kNone, false},
    {"RUp", Source::RStickUp, 0, kLbWorld, kAny, kPress, VK_PRIOR, kCtrl, false},
    {"RDown", Source::RStickDown, 0, kLbWorld, kAny, kPress, VK_NEXT, kCtrl, false},
    {"RLeft", Source::RStickLeft, 0, kLbWorld, kAny, kPress, VK_PRIOR, kNone, false},
    {"RRight", Source::RStickRight, 0, kLbWorld, kAny, kPress, VK_NEXT, kNone, false},
    {"A", kBtn, XINPUT_GAMEPAD_A, kLbWorld, kFree, kPress, VK_BACK, kNone, false},
    // LT chords: floor up / down, recenter, re-announce selection, re-announce tile, objectives,
    // target preview (Left Alt's claim is a pass-through, and Y is hidden, so only the claim runs).
    {"LT+DUp", kBtn, XINPUT_GAMEPAD_DPAD_UP, kLbWorld, kHeld, kPress, VK_OEM_PERIOD, kCtrl, false},
    {"LT+DDown", kBtn, XINPUT_GAMEPAD_DPAD_DOWN, kLbWorld, kHeld, kPress, VK_OEM_COMMA, kCtrl, false},
    {"LT+DLeft", kBtn, XINPUT_GAMEPAD_DPAD_LEFT, kLbWorld, kHeld, kPress, 'C', kNone, false},
    {"LT+DRight", kBtn, XINPUT_GAMEPAD_DPAD_RIGHT, kLbWorld, kHeld, kPress, 'O', kNone, false},
    {"LT+A", kBtn, XINPUT_GAMEPAD_A, kLbWorld, kHeld, kPress, VK_DELETE, kNone, false},
    {"LT+X", kBtn, XINPUT_GAMEPAD_X, kLbWorld, kHeld, kPress, 'X', kCtrl, false},
    {"LT+Y", kBtn, XINPUT_GAMEPAD_Y, kLbWorld, kHeld, kPress, VK_LMENU, kNone, false},

    // Targeting: the game steers and confirms; LT adds the hit breakdown on Y.
    {"LT+Y", kBtn, XINPUT_GAMEPAD_Y, kLbTarget, kHeld, kPress, 'H', kNone, false},
};
constexpr int kMapCount = static_cast<int>(sizeof(kMap) / sizeof(kMap[0]));

struct InputState {
    bool held = false;  // physically down, tracked in EVERY layer so an edge is never invented
    bool owned = false; // the press was taken here, so the release is ours too
    Layer layer = Layer::None; // the layer the press was taken in: a tap or repeat dies with it
    uint64_t nextRepeatMs = 0;
};
InputState sInputs[kMapCount];

bool IsLeftStick(Source s)
{
    return s >= Source::LStickUp && s <= Source::LStickRight;
}
bool IsRightStick(Source s)
{
    return s >= Source::RStickUp && s <= Source::RStickRight;
}

// What one layer hides from the engine, built once from kMap before the filter goes live and read
// only afterwards. Buttons are split by whether LT is held, because a chord's button is hidden
// only then; a stick, LT or RT is hidden whenever any entry of the layer maps it.
struct HideMask {
    WORD buttonsNoLt = 0;
    WORD buttonsLt = 0;
    bool lStick = false;
    bool rStick = false;
    bool lt = false;
    bool rt = false;
};
HideMask sHide[kLayerCount];

void BuildHideMasks()
{
    for (int li = 1; li < kLayerCount; li++) {
        HideMask& h = sHide[li];
        const uint8_t bit = LayerBit(static_cast<Layer>(li));
        for (const Mapping& m : kMap) {
            if (!(m.layers & bit))
                continue;
            if (m.source == Source::Button) {
                if (m.chord != Chord::LtHeld)
                    h.buttonsNoLt |= m.button;
                if (m.chord != Chord::LtFree)
                    h.buttonsLt |= m.button;
            } else if (m.source == Source::LeftTrigger) {
                h.lt = true;
            } else if (m.source == Source::RightTrigger) {
                h.rt = true;
            } else if (IsLeftStick(m.source)) {
                h.lStick = true;
            } else {
                h.rStick = true;
            }
        }
    }
}

GetStateFn sRealGetState = nullptr;
void** sIatSlot = nullptr;
bool sInstallTried = false;

// Published by the tick, read by the filter on whichever thread the engine polls from: atomics
// only. sLayer selects the row of sHide, so the layer and its mask are one atomic and can never
// disagree. Every mapped input of the layer is hidden, not just those with a live claim: the
// engine can poll between a press and our tick, and an unclaimed X or Y on the settings screen is
// save-and-exit or reset-to-defaults. Two more sets keep the game from seeing HALF a press.
// sLatched*: a press we took stays hidden until its release after the layer lets go. sPassed*: a
// press the game saw on the way down stays the game's on the way up, even if the layer starts
// hiding it meanwhile.
std::atomic<uint8_t> sLayer{0};
std::atomic<WORD> sLatchedButtons{0};
std::atomic<bool> sLatchedLStick{false};
std::atomic<bool> sLatchedRStick{false};
std::atomic<bool> sLatchedLeftTrigger{false};
std::atomic<bool> sLatchedRightTrigger{false};
std::atomic<WORD> sPassedButtons{0};
std::atomic<bool> sPassedLStick{false};
std::atomic<bool> sPassedRStick{false};
std::atomic<bool> sPassedLeftTrigger{false};
std::atomic<bool> sPassedRightTrigger{false};

bool sSlotConnected[kSlots] = {};
uint64_t sNextSlotScanMs = 0;
bool sAnyPadLogged = false;

uint64_t sLastShellCheckMs = 0;
bool sInShell = false;
uint64_t sLastOwnedMs = 0;
Layer sCurrentLayer = Layer::None; // game thread only

// One direction at a time per stick; Source::Button doubles as "centred".
struct Stick {
    Source up, down, left, right;
    Source dir = Source::Button;
    bool armed = false;
};
Stick sLeft{Source::LStickUp, Source::LStickDown, Source::LStickLeft, Source::LStickRight};
Stick sRight{Source::RStickUp, Source::RStickDown, Source::RStickLeft, Source::RStickRight};

WORD sPrevButtons = 0;
bool sPrevLt = false;
bool sPrevRt = false;
Source sPrevLeftDir = Source::Button; // a direction CHANGE counts as a press for Silence
Source sPrevRightDir = Source::Button;
bool sLtTapHeld = false;  // LT is down and a tap entry took it: it is a chord modifier
bool sChordFired = false; // a chord ran while LT was held, so its release is not a tap

uint64_t sPresses = 0;
uint64_t sTaken = 0;

#ifdef X2ACCESS_DEVSERVER
// One virtual press: up to three inputs held together ("LT+DUp"), each with the game command that
// stands in for it when the engine does not poll XInput.
struct DevComponent {
    Source source = Source::Button;
    WORD button = 0;
    int cmd = 0;
};
struct DevStep {
    WORD buttons = 0;
    SHORT lx = 0;
    SHORT ly = 0;
    SHORT rx = 0;
    SHORT ry = 0;
    BYTE lt = 0;
    BYTE rt = 0;
    uint32_t holdMs = 0;
    uint32_t gapMs = 0;
    DevComponent comps[3];
    int compCount = 0;
};
constexpr int kDevMaxComps = 3;
std::mutex sDevMutex;
std::deque<DevStep> sDevQueue;
std::atomic<WORD> sDevButtons{0};
std::atomic<SHORT> sDevLx{0};
std::atomic<SHORT> sDevLy{0};
std::atomic<SHORT> sDevRx{0};
std::atomic<SHORT> sDevRy{0};
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
int sDevHeldCmds[kDevMaxComps] = {};  // commands whose release is still owed to the game
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
    if (const SHORT rx = sDevRx.load(std::memory_order_relaxed))
        state->Gamepad.sThumbRX = rx;
    if (const SHORT ry = sDevRy.load(std::memory_order_relaxed))
        state->Gamepad.sThumbRY = ry;
    if (const BYTE lt = sDevLt.load(std::memory_order_relaxed))
        state->Gamepad.bLeftTrigger = lt;
    if (const BYTE rt = sDevRt.load(std::memory_order_relaxed))
        state->Gamepad.bRightTrigger = rt;
#else
    (void)state;
#endif
}

// What the engine calls instead of XInputGetState. Runs on an engine thread: atomics and the
// read-only sHide table only, never engine memory.
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

    const uint8_t layer = std::min<uint8_t>(sLayer.load(std::memory_order_acquire), kLayerCount - 1);
    const HideMask& h = sHide[layer];
    // A chord's buttons are hidden while LT is held as a modifier, judged on the raw trigger so no
    // frame between the press and our tick leaks one; an LT the game saw going down is no modifier.
    const bool chord = state->Gamepad.bLeftTrigger >= kTriggerPress
        && !sPassedLeftTrigger.load(std::memory_order_acquire);
    const WORD passed = sPassedButtons.load(std::memory_order_acquire);
    WORD hidden = static_cast<WORD>((chord ? h.buttonsLt : h.buttonsNoLt) & ~passed);
    hidden |= sLatchedButtons.load(std::memory_order_acquire);
    state->Gamepad.wButtons &= static_cast<WORD>(~hidden);

    const bool zeroL = (h.lStick && !sPassedLStick.load(std::memory_order_acquire))
        || sLatchedLStick.load(std::memory_order_acquire);
    if (zeroL) {
        state->Gamepad.sThumbLX = 0;
        state->Gamepad.sThumbLY = 0;
    }
    const bool zeroR = (h.rStick && !sPassedRStick.load(std::memory_order_acquire))
        || sLatchedRStick.load(std::memory_order_acquire);
    if (zeroR) {
        state->Gamepad.sThumbRX = 0;
        state->Gamepad.sThumbRY = 0;
    }
    if ((h.lt && !sPassedLeftTrigger.load(std::memory_order_acquire))
        || sLatchedLeftTrigger.load(std::memory_order_acquire))
        state->Gamepad.bLeftTrigger = 0;
    if ((h.rt && !sPassedRightTrigger.load(std::memory_order_acquire))
        || sLatchedRightTrigger.load(std::memory_order_acquire))
        state->Gamepad.bRightTrigger = 0;
    return ERROR_SUCCESS;
}

void InstallFilter()
{
    sInstallTried = true;
    BuildHideMasks(); // before the pointer swap: the filter only ever reads it
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
    SHORT rx = 0;
    SHORT ry = 0;
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
            snap.rx = state.Gamepad.sThumbRX;
            snap.ry = state.Gamepad.sThumbRY;
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

bool VirtualStickActive(bool right)
{
#ifdef X2ACCESS_DEVSERVER
    if (right)
        return sDevRx.load(std::memory_order_relaxed) != 0 || sDevRy.load(std::memory_order_relaxed) != 0;
    return sDevLx.load(std::memory_order_relaxed) != 0 || sDevLy.load(std::memory_order_relaxed) != 0;
#else
    (void)right;
    return false;
#endif
}

// One direction at a time, the dominant axis, and it holds until it falls below the release
// threshold — so a diagonal cannot flicker between two arrows. Both sticks use this.
void UpdateStickDirection(Stick& stick, int x, int y, bool right)
{
    // ⛔ A sleeping wireless pad stays "connected" and reports a frozen stick, measured at full
    // up on both sticks with the packet number standing still. So a stick means nothing until it
    // has been seen near centre, or every press of anything would come with a phantom Up. Each
    // stick arms on its own.
    if (abs(x) < kStickRelease && abs(y) < kStickRelease)
        stick.armed = true;
    if (!stick.armed && !VirtualStickActive(right)) {
        stick.dir = Source::Button;
        return;
    }
    if (stick.dir == stick.up) {
        if (y > kStickRelease) return;
    } else if (stick.dir == stick.down) {
        if (y < -kStickRelease) return;
    } else if (stick.dir == stick.right) {
        if (x > kStickRelease) return;
    } else if (stick.dir == stick.left) {
        if (x < -kStickRelease) return;
    }
    stick.dir = Source::Button;
    if (abs(y) >= abs(x)) {
        if (y > kStickPress)
            stick.dir = stick.up;
        else if (y < -kStickPress)
            stick.dir = stick.down;
    } else {
        if (x > kStickPress)
            stick.dir = stick.right;
        else if (x < -kStickPress)
            stick.dir = stick.left;
    }
}

bool IsPressed(const Mapping& m, const PadSnapshot& snap)
{
    switch (m.source) {
    case Source::Button:       return (snap.buttons & m.button) != 0;
    case Source::LeftTrigger:  return snap.lt >= kTriggerPress;
    case Source::RightTrigger: return snap.rt >= kTriggerPress;
    default:                   return sLeft.dir == m.source || sRight.dir == m.source;
    }
}

// Does this entry apply in the layer right now? LT as a chord modifier is the one extra state.
bool Applies(const Mapping& m, Layer layer)
{
    if (!(m.layers & LayerBit(layer)))
        return false;
    switch (m.chord) {
    case Chord::LtHeld: return sLtTapHeld;
    case Chord::LtFree: return !sLtTapHeld;
    default:            return true;
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

// Latches (what we took) and passes (what the game saw) for the filter. Called with the tick's
// results BEFORE the layer is published, so a layer that starts hiding more never meets stale sets.
void PublishSets(const PadSnapshot& snap, bool ltDown, bool rtDown)
{
    WORD owned = 0;
    bool lStick = false, rStick = false, lt = false, rt = false;
    for (int i = 0; i < kMapCount; i++) {
        if (!sInputs[i].owned)
            continue;
        const Source src = kMap[i].source;
        if (src == Source::Button)
            owned |= kMap[i].button;
        else if (src == Source::LeftTrigger)
            lt = true;
        else if (src == Source::RightTrigger)
            rt = true;
        else if (IsLeftStick(src))
            lStick = true;
        else
            rStick = true;
    }
    sLatchedButtons.store(owned, std::memory_order_release);
    sLatchedLStick.store(lStick, std::memory_order_release);
    sLatchedRStick.store(rStick, std::memory_order_release);
    sLatchedLeftTrigger.store(lt, std::memory_order_release);
    sLatchedRightTrigger.store(rt, std::memory_order_release);

    // A press nobody took, going down, is the game's until it comes up.
    WORD passed = sPassedButtons.load(std::memory_order_relaxed);
    passed |= static_cast<WORD>(snap.buttons & ~sPrevButtons & ~owned);
    passed &= snap.buttons;
    sPassedButtons.store(passed, std::memory_order_release);

    if (ltDown && !sPrevLt && !lt)
        sPassedLeftTrigger.store(true, std::memory_order_release);
    if (!ltDown)
        sPassedLeftTrigger.store(false, std::memory_order_release);
    if (rtDown && !sPrevRt && !rt)
        sPassedRightTrigger.store(true, std::memory_order_release);
    if (!rtDown)
        sPassedRightTrigger.store(false, std::memory_order_release);

    sPassedLStick.store(sLeft.dir != Source::Button && !lStick, std::memory_order_release);
    sPassedRStick.store(sRight.dir != Source::Button && !rStick, std::memory_order_release);
}

// The layer for this tick. Past the shell the pad is the mod's only in controller mode, because in
// mouse mode the game answers a pad with its own mouse-and-keyboard handling and the mod must not
// disturb it. UIFinalShell on the stack is the shell: pause menus and everything else keep the
// game's own pad handling.
Layer ComputeLayer(uint64_t now, bool foreground)
{
    if (!foreground)
        return Layer::None;
    const bool controller = KeyboardGate::ControllerMode();
    // The shell scan only decides anything in mouse mode; controller mode skips it.
    if (!controller && now - sLastShellCheckMs >= kShellCheckMs) {
        sLastShellCheckMs = now;
        void* shell = GameUI::FindMainMenuShell();
        sInShell = shell && GameUI::IsScreenOnStack(shell);
    }
    if (!sInShell && !controller)
        return Layer::None;

    // A non-ambient recipe holding input: a menu, anywhere.
    if (MenuNavigator::OwnsInput() && MenuNavigator::MenuAttached()) {
        sLastOwnedMs = now;
        return Layer::Menu;
    }
    if (controller) {
        if (MenuNavigator::OwnsInput())
            return Layer::Hud; // an ambient recipe holding input
        if (TacticalHUD::IsTargeting())
            return Layer::Targeting;
        if (TileReader::IsActive())
            return Layer::World;
        if (Avenger::IsGridActive())
            return Layer::AntFarm; // nothing focused, or the branch above would have taken it
    }
    return now - sLastOwnedMs < kOwnershipGraceMs ? Layer::Menu : Layer::None;
}

#ifdef X2ACCESS_DEVSERVER
void ClearVirtualPad()
{
    sDevButtons.store(0, std::memory_order_relaxed);
    sDevLx.store(0, std::memory_order_relaxed);
    sDevLy.store(0, std::memory_order_relaxed);
    sDevRx.store(0, std::memory_order_relaxed);
    sDevRy.store(0, std::memory_order_relaxed);
    sDevLt.store(0, std::memory_order_relaxed);
    sDevRt.store(0, std::memory_order_relaxed);
    sDevPacket.fetch_add(1, std::memory_order_relaxed);
}

// ⛔ The engine does not poll XInputGetState in this build (measured: docs/input-map.md, "Virtual
// pad"), so a virtual press the game must see is delivered where XInput's result ends up: the live
// input object's own InputEvent, a press then a release, the same pair a pad hands it. Done when
// the mod's layer does not itself act on the press: a mapped input is the mod's, and delivering it
// to the game as well would act twice.
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

// Does the layer act on this input (so the game must NOT also get it)? The same table the tick
// walks; `chord` says LT is held with it as a modifier.
bool LayerActsOn(Layer layer, const DevComponent& c, bool chord)
{
    for (const Mapping& m : kMap) {
        if (m.source != c.source || (c.source == Source::Button && m.button != c.button))
            continue;
        if (!(m.layers & LayerBit(layer)))
            continue;
        if (m.chord == Chord::LtHeld && !chord)
            continue;
        if (m.chord == Chord::LtFree && chord)
            continue;
        return true;
    }
    return false;
}

// LT is a chord modifier in a layer that taps it, i.e. one that has a TapRelease entry for LT.
bool LayerTapsLt(Layer layer)
{
    for (const Mapping& m : kMap) {
        if (m.fire == Fire::TapRelease && (m.layers & LayerBit(layer)))
            return true;
    }
    return false;
}

void PumpVirtualPad(uint64_t now)
{
    if (now < sDevNextMs)
        return;
    if (sDevHolding) {
        for (int& cmd : sDevHeldCmds) {
            if (cmd)
                DevEngineEvent(cmd, kActionRelease);
            cmd = 0;
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
    // Each input is delivered to the game exactly when the current layer does not act on it.
    const Layer layer = sCurrentLayer;
    bool ltInStep = false;
    for (int i = 0; i < step.compCount; i++)
        ltInStep = ltInStep || step.comps[i].source == Source::LeftTrigger;
    const bool chord = ltInStep && LayerTapsLt(layer);
    for (int i = 0; i < step.compCount; i++) {
        const DevComponent& c = step.comps[i];
        if (!c.cmd || LayerActsOn(layer, c, chord))
            continue;
        const bool sent = DevEngineEvent(c.cmd, kActionPress);
        sDevHeldCmds[i] = sent ? c.cmd : 0;
        LOG_DEBUG("Gamepad: virtual press cmd %d -> InputEvent %s (layer %s)", c.cmd,
            sent ? "sent" : "NOT sent (no input object)", kLayerNames[static_cast<int>(layer)]);
    }
    sDevButtons.store(step.buttons, std::memory_order_relaxed);
    sDevLx.store(step.lx, std::memory_order_relaxed);
    sDevLy.store(step.ly, std::memory_order_relaxed);
    sDevRx.store(step.rx, std::memory_order_relaxed);
    sDevRy.store(step.ry, std::memory_order_relaxed);
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
    const Layer layer = ComputeLayer(now, foreground);
    if (layer != sCurrentLayer) {
        LOG_INFO("Gamepad: layer %s -> %s", kLayerNames[static_cast<int>(sCurrentLayer)],
            kLayerNames[static_cast<int>(layer)]);
        sCurrentLayer = layer;
    }
    UpdateStickDirection(sLeft, snap.lx, snap.ly, /*right*/ false);
    UpdateStickDirection(sRight, snap.rx, snap.ry, /*right*/ true);

    const bool ltDown = foreground && snap.lt >= kTriggerPress;
    const bool rtDown = foreground && snap.rt >= kTriggerPress;
    const bool leftDir = sLeft.dir != Source::Button;
    const bool rightDir = sRight.dir != Source::Button;

    // ⛔ A screen reader cuts its own speech when it sees a key go down, under the player's own
    // settings, and a pad press is no key: without this every line would queue behind the last,
    // and scrolling a menu would lag further with each press. So any press stops speech where the
    // keypress would have — including one that passes to the game (RB, RT, X, Y, B), because the
    // reader cannot see those either — and the mod still never interrupts on its own account.
    // Not in the None layer: there the pad is vanilla and so is the reader's view of it.
    const bool anyPress = foreground
        && ((snap.buttons & ~sPrevButtons) != 0 || (ltDown && !sPrevLt) || (rtDown && !sPrevRt)
            || (leftDir && sLeft.dir != sPrevLeftDir) || (rightDir && sRight.dir != sPrevRightDir));
    if (layer != Layer::None && anyPress)
        TTS::Silence();

    sLtTapHeld = false;
    for (int i = 0; i < kMapCount; i++) {
        const Mapping& m = kMap[i];
        InputState& s = sInputs[i];
        const bool pressed = foreground && IsPressed(m, snap);

        if (pressed && !s.held) {
            s.held = true;
            const bool applies = Applies(m, layer);
            s.owned = applies;
            s.layer = layer;
            s.nextRepeatMs = now + kRepeatDelayMs;
            sPresses++;
            if (applies) {
                if (m.fire == Fire::TapRelease) {
                    sChordFired = false; // a fresh LT: nothing has fired under it yet
                    sTaken++;
                } else {
                    if (m.chord == Chord::LtHeld)
                        sChordFired = true;
                    const bool claimed = Send(m, /*down*/ true, /*repeat*/ false);
                    sTaken += claimed ? 1 : 0;
                    LOG_DEBUG("Gamepad: %s -> vk 0x%02X mods 0x%X (%s)", m.name, m.vk, m.mods,
                        claimed ? "claimed" : "no claim");
                }
            }
        } else if (pressed && s.owned && m.fire == Fire::Press && m.repeats
            && s.layer == layer && now >= s.nextRepeatMs) {
            s.nextRepeatMs = now + kRepeatIntervalMs;
            TTS::Silence();
            Send(m, /*down*/ true, /*repeat*/ true);
        } else if (!pressed && s.held) {
            if (s.owned) {
                if (m.fire == Fire::Press) {
                    Send(m, /*down*/ false, /*repeat*/ false);
                } else if (!sChordFired && s.layer == layer) {
                    // The tap: LT went down and came up with no chord between.
                    const bool claimed = Send(m, /*down*/ true, /*repeat*/ false);
                    Send(m, /*down*/ false, /*repeat*/ false);
                    LOG_DEBUG("Gamepad: %s tap -> vk 0x%02X (%s)", m.name, m.vk,
                        claimed ? "claimed" : "no claim");
                }
            }
            s.held = false;
            s.owned = false;
        }
        if (m.fire == Fire::TapRelease && s.held && s.owned)
            sLtTapHeld = true;
    }

    PublishSets(snap, ltDown, rtDown);
    sLayer.store(static_cast<uint8_t>(layer), std::memory_order_release);
    sPrevButtons = foreground ? snap.buttons : 0;
    sPrevLt = ltDown;
    sPrevRt = rtDown;
    sPrevLeftDir = sLeft.dir;
    sPrevRightDir = sRight.dir;
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
    // UIUtilities_Input.FXS_* commands stand in for each input when the engine does not poll XInput.
    struct Named {
        const char* name;
        Source source;
        WORD button;
        int cmd;
    };
    static const Named kNamed[] = {
        {"A", Source::Button, XINPUT_GAMEPAD_A, 300}, {"B", Source::Button, XINPUT_GAMEPAD_B, 301},
        {"X", Source::Button, XINPUT_GAMEPAD_X, 302}, {"Y", Source::Button, XINPUT_GAMEPAD_Y, 303},
        {"LB", Source::Button, XINPUT_GAMEPAD_LEFT_SHOULDER, 330},
        {"RB", Source::Button, XINPUT_GAMEPAD_RIGHT_SHOULDER, 331},
        {"Start", Source::Button, XINPUT_GAMEPAD_START, 321},
        {"Back", Source::Button, XINPUT_GAMEPAD_BACK, 320},
        {"L3", Source::Button, XINPUT_GAMEPAD_LEFT_THUMB, 311},
        {"R3", Source::Button, XINPUT_GAMEPAD_RIGHT_THUMB, 313},
        {"DUp", Source::Button, XINPUT_GAMEPAD_DPAD_UP, 350},
        {"DRight", Source::Button, XINPUT_GAMEPAD_DPAD_RIGHT, 352},
        {"DDown", Source::Button, XINPUT_GAMEPAD_DPAD_DOWN, 354},
        {"DLeft", Source::Button, XINPUT_GAMEPAD_DPAD_LEFT, 356},
        {"LUp", Source::LStickUp, 0, 370}, {"LDown", Source::LStickDown, 0, 371},
        {"LLeft", Source::LStickLeft, 0, 372}, {"LRight", Source::LStickRight, 0, 373},
        {"RUp", Source::RStickUp, 0, 374}, {"RDown", Source::RStickDown, 0, 375},
        {"RLeft", Source::RStickLeft, 0, 376}, {"RRight", Source::RStickRight, 0, 378},
        {"LT", Source::LeftTrigger, 0, 332}, {"RT", Source::RightTrigger, 0, 333},
    };

    std::deque<DevStep> steps;
    size_t pos = 0;
    while (pos <= names.size()) {
        size_t comma = names.find(',', pos);
        if (comma == std::string::npos)
            comma = names.size();
        const std::string token = names.substr(pos, comma - pos);
        pos = comma + 1;
        if (token.empty())
            continue;

        DevStep step;
        step.holdMs = holdMs;
        step.gapMs = gapMs;
        // "LT+DUp": inputs held together. A space counts too, since a '+' in a query decodes to one.
        size_t cpos = 0;
        while (cpos <= token.size()) {
            size_t plus = token.find_first_of("+ ", cpos);
            if (plus == std::string::npos)
                plus = token.size();
            const std::string name = token.substr(cpos, plus - cpos);
            cpos = plus + 1;
            if (name.empty())
                continue;
            const Named* found = nullptr;
            for (const Named& n : kNamed) {
                if (name == n.name)
                    found = &n;
            }
            if (!found || step.compCount >= kDevMaxComps)
                return name;
            switch (found->source) {
            case Source::Button:       step.buttons |= found->button; break;
            case Source::LStickUp:     step.ly = 30000; break;
            case Source::LStickDown:   step.ly = -30000; break;
            case Source::LStickLeft:   step.lx = -30000; break;
            case Source::LStickRight:  step.lx = 30000; break;
            case Source::RStickUp:     step.ry = 30000; break;
            case Source::RStickDown:   step.ry = -30000; break;
            case Source::RStickLeft:   step.rx = -30000; break;
            case Source::RStickRight:  step.rx = 30000; break;
            case Source::LeftTrigger:  step.lt = 255; break;
            case Source::RightTrigger: step.rt = 255; break;
            }
            DevComponent& c = step.comps[step.compCount++];
            c.source = found->source;
            c.button = found->button;
            c.cmd = engineEvents ? found->cmd : 0; // engine=0: XInput only, to measure the engine alone
        }
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
    char line[640];
    int connected = 0;
    for (bool slot : sSlotConnected)
        connected += slot ? 1 : 0;
    snprintf(line, sizeof(line),
        "filter %s%s, pads %d, in shell %d, foreground %d, layer %s, lt chord %d, presses %llu, "
        "taken by claims %llu, engine filter calls %llu (virtual %llu), threads %lu/%lu, last %lu\n",
        sIatSlot ? "installed" : "NOT installed", sHideFromEngine.load() ? " (engine told: no pad)" : "", connected, sInShell,
        InputArbiter::IsGameForeground(), kLayerNames[sLayer.load() % kLayerCount], sLtTapHeld,
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
