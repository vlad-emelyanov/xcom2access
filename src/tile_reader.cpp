#include "tile_reader.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "menu_navigator.h"
#include "mouse_gate.h"
#include "narrator.h"
#include "ue3_reflection.h"
#include "vocab.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace TileReader {

namespace {

constexpr const char* kReaderClass = "X2AccessTileReader";

// The claims' active predicate result, recomputed on the tick (the arbiter evaluates
// activeWhen inside the window procedure, so it must stay a flag read — the screen lookups
// behind it belong on the tick, same discipline as the menu navigator's IsAttached gate).
bool gActive = false;

// Has the player used the tile cursor in THIS tactical session? Set by any successful
// outbound reader call, cleared when the tactical HUD leaves the stack. It is what arms the
// mouse gate: until the first press, the mouse keeps its native cursor-follow (lazy plant
// means lazy takeover); from then on, keyboard owns the world cursor for the mission and a
// mouse CLICK commits to the pathing pawn's preview of OUR tile — the same two-press
// semantics the Backspace commit uses.
bool gUsedThisTactical = false;

// The gate is the screen-stack walk in GameUI::ReadTacticalGate — NOT IsScreenFocused, which
// is right for menus but wrong here: in tactical the focus bit lives on the permanent UIReplay
// overlay and UITacticalHUD never carries it (first live run read 0x238 and stayed inactive).
// `why` (>= 96 bytes) gets the reason when the result is false, for the transition log.
bool ComputeActive(char* why, size_t whyLen, bool* hudOnStack)
{
    why[0] = '\0';
    *hudOnStack = false;
    // A graph recipe owning the surface (pause menu, a modal dialog over tactical) wins.
    if (MenuNavigator::IsAttached()) {
        snprintf(why, whyLen, "graph recipe attached");
        return false;
    }
    GameUI::TacticalGate gate = GameUI::ReadTacticalGate();
    *hudOnStack = gate.hudOnStack;
    if (gate.blocked) {
        snprintf(why, whyLen, "blocked by %s", gate.blocker);
        return false;
    }
    if (!gate.hudOnStack) {
        snprintf(why, whyLen, "no tactical HUD on the stack");
        return false;
    }
    // The shot HUD's own targeting UI consumes the arrows (ability cycling, target cycling) —
    // the tile cursor stands down rather than swallowing them.
    if (gate.menuRaised) {
        snprintf(why, whyLen, "shot HUD raised");
        return false;
    }
    GameUI::StackState stack = GameUI::ReadStackState();
    if (stack.valid && !stack.Interactive()) {
        snprintf(why, whyLen, "UI gated (input blocked / hidden / cinematic)");
        return false;
    }
    return true;
}

// One outbound call to the companion reader, then speak whatever it wrote to NativeResult.
// Runs from the arbiter's tick drain — the game thread — which is what outbound ProcessEvent
// requires. An empty result is a deliberate "nothing to say", not an error.
//
// Through the Narrator rather than TTS directly, and on an INTERRUPTING channel: the player
// pressed a key, so their answer outranks — and CLEARS — whatever combat narration is still
// queued behind it. Speaking straight to the backend cut the current utterance but left the
// queue standing, so the stale burst resumed on top of the answer on the very next flush.
// It is also what puts these lines in the scrollback ring, which a review key will need.
void Call(const char* funcName, void* params, Narrator::Channel channel)
{
    void* cdo = GameUI::FindCompanionCDO(kReaderClass);
    if (!cdo) {
        Narrator::Say(Narrator::Channel::Aid, Vocab::SurfaceUnavailable("Tile reader"));
        return;
    }
    if (!GameUI::CallScriptFunction(cdo, funcName, params)) {
        LOG_WARN("TileReader: outbound call %s failed", funcName);
        return;
    }
    // Arm and engage the mouse gate right here (game thread, claim already implies world
    // mode) so even this press's push survives the frame instead of waiting out the tick.
    gUsedThisTactical = true;
    MouseGate::SetSwallow(true);
    std::string result
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (!result.empty())
        Narrator::Say(channel, result);
}

// Compass cardinals, matching X2AccessTileReader's DIR_* constants (the engine's cover-flag
// bit order). World-absolute by player decision: Up arrow is north regardless of camera.
// No synthetic movement tick here — the player wants the game's own sounds only (the pathing
// pawn's dash/warning cues fire natively as it follows the pushed cursor).
constexpr int32_t kDirNorth = 0;
constexpr int32_t kDirSouth = 1;
constexpr int32_t kDirEast = 2;
constexpr int32_t kDirWest = 3;

// Moving the cursor is Focus — a selection changed under the player's own key, the same reading
// UnitInfo's Tab poll takes. The on-demand reads below are Aid. Both interrupt and clear the
// queue identically; the channel is what the scrollback ring will show them as.
void StepCall(int32_t cardinal)
{
    struct {
        int32_t cardinal;
    } params{cardinal};
    Call("CursorStep", &params, Narrator::Channel::Focus);
}

void FloorCall(int32_t delta)
{
    struct {
        int32_t delta;
    } params{delta};
    Call("CursorFloor", &params, Narrator::Channel::Focus);
}

void SimpleCall(const char* funcName, Narrator::Channel channel)
{
    // No-parameter functions still get a zeroed buffer — ProcessEvent copies ParmsSize bytes.
    int64_t zeros[2] = {};
    Call(funcName, zeros, channel);
}

// Everything fires once per press — auto-repeat is DISABLED for now, including on the arrows:
// the first live test read as multi-tile jumps, and until one-press-one-tile is proven, repeat
// marching would only blur the diagnosis. Revisit held-key marching once stepping is trusted.
std::function<void(const InputArbiter::KeyEvent&)> OnPress(std::function<void()> fn)
{
    return [fn = std::move(fn)](const InputArbiter::KeyEvent& e) {
        if (e.down && !e.repeat)
            fn();
    };
}

} // namespace

void Init()
{
    using namespace InputArbiter;

    auto active = [] { return gActive; };

    // Arrows: world-absolute compass steps. WASD keeps camera pan for a sighted co-pilot —
    // the game binds both, we take only the arrow set.
    RegisterClaim({VK_UP, ModNone, active, OnPress([] { StepCall(kDirNorth); }), "tile cursor: north"});
    RegisterClaim({VK_DOWN, ModNone, active, OnPress([] { StepCall(kDirSouth); }), "tile cursor: south"});
    RegisterClaim({VK_LEFT, ModNone, active, OnPress([] { StepCall(kDirWest); }), "tile cursor: west"});
    RegisterClaim({VK_RIGHT, ModNone, active, OnPress([] { StepCall(kDirEast); }), "tile cursor: east"});

    // Ctrl+comma / Ctrl+period: floor down / up. The UNSHIFTED keys belong to the object
    // scanner's quick navigation and SHIFT+them is now its reverse cycle, so the floor keys
    // moved here — which is also where they always belonged. Ctrl is the safe modifier
    // (Key_Left_Control only toggles the pathing pawn's waypoint mode), and comma/period carry
    // no bare binding in any tactical input section, so leniency cannot double-fire the chord.
    //
    // This also fixes a shipped bug rather than merely relocating a key: Key_Left_Shift's
    // RELEASE calls PrevUnit() unconditionally in state ActiveUnit_Moving
    // (XComTacticalInput.uc:2147), so every one of these presses used to cycle the selected
    // soldier when the player let go. It never reproduced in the tutorial because the REPLAY
    // state's copy of the same function (:587) is a bare `return false`. The scanner now takes
    // Shift properly (LeftShift claim in object_scanner.cpp); this pair no longer needs it.
    RegisterClaim({VK_OEM_COMMA, ModCtrl, active, OnPress([] { FloorCall(-1); }),
        "tile cursor: floor down"});
    RegisterClaim({VK_OEM_PERIOD, ModCtrl, active, OnPress([] { FloorCall(1); }),
        "tile cursor: floor up"});

    RegisterClaim({'C', ModNone, active,
        OnPress([] { SimpleCall("CursorRecenter", Narrator::Channel::Focus); }),
        "tile cursor: recenter on unit"});
    RegisterClaim({VK_DELETE, ModNone, active,
        OnPress([] { SimpleCall("CursorAnnounce", Narrator::Channel::Aid); }),
        "tile cursor: re-announce"});
    RegisterClaim({VK_BACK, ModNone, active,
        OnPress([] { SimpleCall("CursorMoveTo", Narrator::Channel::Aid); }),
        "tile cursor: move to (press twice)"});

    LOG_INFO("TileReader: claims registered");
}

bool IsActive()
{
    return gActive;
}

void GoToMarker()
{
    SimpleCall("CursorGoToMarker", Narrator::Channel::Focus);
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    uint64_t now = GetTickCount64();
    if (now - sLastMs < 150)
        return;
    sLastMs = now;

    char why[96];
    bool hudOnStack = false;
    bool active = ComputeActive(why, sizeof(why), &hudOnStack);
    if (active != gActive) {
        gActive = active;
        if (active)
            LOG_INFO("TileReader: active (tactical HUD owns the surface)");
        else
            LOG_INFO("TileReader: inactive — %s", why);
    }

    // Mouse gate: while the tile cursor has been used this mission AND owns world mode,
    // suppress the game's per-frame mouse re-push of the cursor (see mouse_gate.cpp). Any
    // stand-down — targeting raised, a recipe attached, pause menu, mission over — restores
    // the mouse instantly; leaving tactical also disarms until the next press.
    if (!hudOnStack)
        gUsedThisTactical = false;
    MouseGate::SetSwallow(gActive && gUsedThisTactical);
}

} // namespace TileReader
