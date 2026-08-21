#include "free_aim.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "mouse_gate.h"
#include "narrator.h"
#include "ue3_reflection.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace FreeAim {

namespace {

// Same CDO as the ordinary shot readout: free aim is a MODE of the shot HUD, not a screen of its
// own, and keeping both in one class is what lets Delete stay a single claim that branches in
// script rather than two claims racing for the same chord.
constexpr const char* kReaderClass = "X2AccessTacticalReader";

bool gActive = false;

// Armed on the first aim key of a targeting session, exactly as the tile cursor arms its own
// gate: until the player actually aims, leaving the mouse in charge is correct (a sighted
// co-pilot may be driving) and costs nothing.
bool gUsedThisTargeting = false;

// One outbound call to the companion, then speak whatever it wrote to NativeResult. Runs from
// the arbiter's tick drain — the game thread, which is what outbound ProcessEvent requires.
//
// The mouse gate is engaged HERE, before the result is read, for the same reason TileReader does
// it: the push this call just made has to survive the frame it was made in, and waiting for the
// next tick would let Mouse_CheckForFreeAim revert it first (measured at 60 pushes/second).
void Call(const char* funcName, void* params)
{
    void* cdo = GameUI::FindCompanionCDO(kReaderClass);
    if (!cdo) {
        Narrator::Say(Narrator::Channel::Aid, "Aim reader unavailable.");
        return;
    }
    if (!GameUI::CallScriptFunction(cdo, funcName, params)) {
        LOG_WARN("FreeAim: outbound call %s failed", funcName);
        return;
    }
    gUsedThisTargeting = true;
    MouseGate::SetFreeAimSwallow(true);

    std::string result
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (!result.empty())
        Narrator::Say(Narrator::Channel::Aid, result);
}

// Cardinal indices, matching X2AccessTileReader's DIR_* constants (the engine's cover-flag bit
// order). The aim point uses the tile cursor's compass verbatim — same table, same calibration,
// one definition of north for the whole tactical layer.
constexpr int32_t kDirNorth = 0;
constexpr int32_t kDirSouth = 1;
constexpr int32_t kDirEast = 2;
constexpr int32_t kDirWest = 3;

void StepCall(int32_t cardinal)
{
    struct {
        int32_t cardinal;
    } params{cardinal};
    Call("FreeAimStep", &params);
}

void FloorCall(int32_t delta)
{
    struct {
        int32_t delta;
    } params{delta};
    Call("FreeAimFloor", &params);
}

// Once per press, no auto-repeat — the same discipline as the tile cursor, and for the same
// reason: a held key would out-run the reader and blur which tile is actually aimed at.
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

    // Arrows: world-absolute compass steps on the aim point, identical to the tile cursor's, so
    // the two modes share one set of motor habits.
    //
    // What this displaces is nothing the player was using. UITacticalHUD.OnUnrealCommand calls
    // SetSelectionOnInputPress on every PRESS (:405), and with the controller branch skipped on
    // mouse+keyboard that lands in CycleAbilitySelection — i.e. a bare arrow during a grenade
    // throw ABANDONS the grenade and picks another ability. Ability cycling stays fully
    // available on the 1-9 hotkeys, which is how a keyboard player selects abilities anyway.
    RegisterClaim({VK_UP, ModNone, active, OnPress([] { StepCall(kDirNorth); }), "free aim: north"});
    RegisterClaim({VK_DOWN, ModNone, active, OnPress([] { StepCall(kDirSouth); }), "free aim: south"});
    RegisterClaim({VK_LEFT, ModNone, active, OnPress([] { StepCall(kDirWest); }), "free aim: west"});
    RegisterClaim({VK_RIGHT, ModNone, active, OnPress([] { StepCall(kDirEast); }), "free aim: east"});

    // Ctrl+comma / Ctrl+period: aim point down / up one floor. Same chord as the tile cursor's
    // floor keys and never contending with them — the tile cursor stands down exactly while the
    // shot HUD is raised. They moved off Shift with that pair, and for the same reason: Shift is
    // now the scanner's reverse-cycle modifier in BOTH modes, so leaving the floor keys there
    // would collide with Shift+comma / Shift+period stepping the scanner backwards.
    //
    // (Free aim never carried the tile cursor's Shift *hazard*: Key_Left_Shift is declared in
    // state ActiveUnit_Moving with no class-scope twin, so its PrevUnit() is unreachable from a
    // targeting state. The move here is about keymap consistency, not about that bug.)
    RegisterClaim({VK_OEM_COMMA, ModCtrl, active, OnPress([] { FloorCall(-1); }),
        "free aim: floor down"});
    RegisterClaim({VK_OEM_PERIOD, ModCtrl, active, OnPress([] { FloorCall(1); }),
        "free aim: floor up"});

    // Enter, Space and Escape are deliberately NOT claimed: the game's own confirm
    // (UITacticalHUD_AbilityContainer.OnAccept -> ConfirmAbility) and cancel
    // (CancelTargetingAction) already work from the keyboard, and swallowing them would break
    // the one part of this flow vanilla gets right. Delete stays a single TacticalHUD claim
    // that branches in script. The scanner's own keys are widened to this mode in
    // object_scanner.cpp rather than duplicated here.

    LOG_INFO("FreeAim: claims registered");
}

bool IsActive()
{
    return gActive;
}

void SetActive(bool on)
{
    if (on == gActive)
        return;
    gActive = on;
    LOG_INFO("FreeAim: %s", on ? "active (cursor-targeted ability raised)" : "inactive");

    if (!on) {
        // Leaving free aim hands the cursor back to the mouse immediately, and disarms so the
        // next ability starts from the mouse again rather than inheriting this session's gate.
        gUsedThisTargeting = false;
        MouseGate::SetFreeAimSwallow(false);
    }
}

void AimAtMarker()
{
    // No-parameter functions still get a zeroed buffer — ProcessEvent copies ParmsSize bytes.
    int64_t zeros[2] = {};
    Call("FreeAimGoToMarker", zeros);
}

} // namespace FreeAim
