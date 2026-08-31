#include "tactical_hud.h"
#include "free_aim.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "menu_navigator.h"
#include "narrator.h"
#include "ue3_reflection.h"
#include "vocab.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace TacticalHUD {

namespace {

constexpr const char* kReaderClass = "X2AccessTacticalReader";

// Poll cadence. The same 150 ms the tile reader uses: fast enough that a target cycled with Tab
// is spoken before the player presses again, cheap enough to leave running for a whole mission.
constexpr uint64_t kPollIntervalMs = 150;

// The claims' active predicate result, recomputed on the tick. The arbiter evaluates activeWhen
// inside the window procedure, so it must stay a flag read — the screen-stack walk behind it
// belongs on the tick, the same discipline as the tile reader and the menu navigator.
bool gTargeting = false;

// The two gates this module publishes for the rest of the tactical layer (see tactical_hud.h).
// Both are written by OnTick and read from the window procedure, so they stay plain flag reads.
bool gUsable = false;
bool gMissionActive = false;

// Is the tactical HUD usable at all (mission running, no modal over it, no recipe attached)?
// Wider than gTargeting: the poll runs here so it can see the shot HUD go up and come down,
// while the keys are claimed only while it is actually up.
//
// missionActive comes back separately and is deliberately NOT part of the answer: it stays true
// under everything that makes this function say no, which is exactly what a per-mission edge
// needs and exactly what a may-we-act-now gate must not have.
bool ComputeUsable(bool* menuRaised, bool* missionActive)
{
    *menuRaised = false;
    GameUI::TacticalGate gate = GameUI::ReadTacticalGate();
    *missionActive = gate.missionActive;
    // A MENU recipe attached (a dialog over tactical — the pause menu already clears
    // hudOnStack) stands this layer down. The ambient tactical recipe does not, focused or
    // not: it being attached is this layer's normal state for a whole mission, and the poll
    // must keep running under it so activating an ability from the HUD graph narrates its
    // default target the moment targeting raises.
    if (MenuNavigator::MenuAttached())
        return false;
    if (!gate.hudOnStack || gate.blocked)
        return false;
    GameUI::StackState stack = GameUI::ReadStackState();
    if (stack.valid && !stack.Interactive())
        return false;
    *menuRaised = gate.menuRaised;
    return true;
}

// One outbound call to the companion reader, then speak whatever it wrote to NativeResult.
// Runs from the frame pump or the arbiter's tick drain — both the game thread, which is what
// outbound ProcessEvent requires. An empty result is a deliberate "nothing to say": the poll
// uses it on every tick where the selection has not moved.
//
// Everything here goes out on an INTERRUPTING channel: the selection under the player's own keys
// outranks whatever combat narration is still queued behind it. That is the whole reason these
// calls route through the Narrator rather than TTS directly.
void Call(const char* funcName, Narrator::Channel channel)
{
    void* cdo = GameUI::FindCompanionCDO(kReaderClass);
    if (!cdo) {
        // FindCompanionCDO already logs the miss (companion mod not loaded). Stay silent on the
        // poll path; the key handlers below are the ones that owe the player an answer.
        return;
    }
    // No-parameter functions still get a zeroed buffer — ProcessEvent copies ParmsSize bytes.
    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, funcName, zeros)) {
        LOG_WARN("TacticalHUD: outbound call %s failed", funcName);
        return;
    }
    std::string result
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (!result.empty())
        Narrator::Say(channel, result);
}

// Which MODE the shot HUD is in, as the companion reports it through a second reflected-out
// property — the same pattern the hacking screen uses for NativeState. Deliberately read rather
// than re-derived in C++: the answer depends on the selected action's bFreeAim AND on the
// targeting method's CLASS (only the four cursor-driven families count), and putting that
// decision next to the data is what keeps a regenerated SDK or a Highlander bump from silently
// breaking it. PollTargeting rewrites NativeState on every call, so a stale value cannot linger.
bool ReadFreeAimFlag()
{
    void* cdo = GameUI::FindCompanionCDO(kReaderClass);
    if (!cdo)
        return false;
    return UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeState")
        == "freeaim";
}

// A pressed key must never be silent, so the on-demand handlers say so when the companion is
// missing entirely — the one case Call() swallows.
void CallOnDemand(const char* funcName)
{
    if (!GameUI::FindCompanionCDO(kReaderClass)) {
        Narrator::Say(Narrator::Channel::Aid, Vocab::SurfaceUnavailable("Shot reader"));
        return;
    }
    Call(funcName, Narrator::Channel::Aid);
}

// Auto-repeat is off, matching the tile reader: both keys are re-reads, and a held key would
// only stutter the same line over itself.
std::function<void(const InputArbiter::KeyEvent&)> OnPress(const char* funcName)
{
    return [funcName](const InputArbiter::KeyEvent& e) {
        if (e.down && !e.repeat)
            CallOnDemand(funcName);
    };
}

} // namespace

void Init()
{
    using namespace InputArbiter;

    auto active = [] { return gTargeting; };

    // Delete: re-announce the whole shot. Mirrors the tile cursor's re-announce key, and the
    // two never contend — the tile reader stands down exactly when the shot HUD is raised.
    RegisterClaim({VK_DELETE, ModNone, active, OnPress("AnnounceShot"),
        "shot HUD: re-announce shot"});

    // H: the per-modifier to-hit breakdown — the spoken form of the shot wings a mouse player
    // opens by clicking. Free in tactical: DefaultInput.ini's [XComGame.XComTacticalInput]
    // block binds no command to H at all, so claiming it costs the player nothing.
    RegisterClaim({'H', ModNone, active, OnPress("AnnounceBreakdown"),
        "shot HUD: hit chance breakdown"});

    LOG_INFO("TacticalHUD: claims registered");
}

bool IsUsable()
{
    return gUsable;
}

bool IsMissionActive()
{
    return gMissionActive;
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    bool menuRaised = false;
    bool missionActive = false;
    bool usable = ComputeUsable(&menuRaised, &missionActive);
    gUsable = usable;
    gMissionActive = missionActive;

    bool targeting = usable && menuRaised;
    if (targeting != gTargeting) {
        gTargeting = targeting;
        LOG_INFO("TacticalHUD: targeting %s", targeting ? "raised" : "lowered");
    }

    // The poll runs across the whole usable window, not just while raised: the companion tracks
    // what it last announced and resets that on the way down, so the next raise reads in full.
    // Its not-raised path is a screen lookup and a bool read, which is why leaving it running
    // for a mission is affordable.
    if (usable) {
        Call("PollTargeting", Narrator::Channel::Focus);
        // Published from the same poll rather than from a tick of its own: free aim is a mode of
        // this screen, so the whole layer costs one outbound call per interval.
        FreeAim::SetActive(ReadFreeAimFlag());
    } else {
        FreeAim::SetActive(false);
    }

    // Leaving tactical — mission over, a modal taking the layer, a recipe attaching — retires
    // whatever combat narration is still queued on both sides of the bridge. Lines about a
    // finished fight spoken over the mission summary would be worse than losing them.
    static bool sWasUsable = false;
    if (sWasUsable && !usable)
        Narrator::DropPending();
    sWasUsable = usable;
}

} // namespace TacticalHUD
