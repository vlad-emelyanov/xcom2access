#include "hacking_screen.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "narrator.h"
#include "ue3_reflection.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

namespace HackingScreen {

namespace {

constexpr const char* kReaderClass = "X2AccessHackingReader";
constexpr const char* kScreenClass = "UIHackingScreen";

// Poll cadence, the same 150 ms the tile reader and the shot HUD use. The poll's job is only to
// notice the screen and announce it once, so this is about latency between the pad appearing and
// the first word, not about tracking anything continuous.
constexpr uint64_t kPollIntervalMs = 150;

// Is the hacking screen the thing that owns the tactical surface right now? Recomputed on the
// tick; the claim predicates below read it as a flag, because the arbiter evaluates them inside
// the window procedure and the screen-stack walk belongs on the tick.
bool gScreenUp = false;

// Has the hack been committed? Reported by the companion (NativeState), not read out of the
// screen's bitfield — see the header for why that mask is not safe to hard-code. Once it is set
// the claims go inactive so that every key falls through to the game: the reveal ends with
// "press any key to continue", and a key we swallowed would not dismiss it.
bool gHackStarted = false;

// The claim predicate. Deliberately NOT gated on GameUI::ReadStackState().Interactive(): the
// stack reports cinematic mode while this screen is up (HideUIForCinematics sets bCinematicMode
// and then skips every screen with bShowDuringCinematic, which this screen sets), so the usual
// "any gate flag means nothing is navigable" test would refuse the whole feature. IsInputBlocked
// is the flag that would really mean that, and it is clear here.
bool Active()
{
    return gScreenUp && !gHackStarted;
}

// The screen-stack answer, taken from the tile cursor's own gate rather than a second walk — so
// the two surfaces can never disagree about who owns the tactical input. ReadTacticalGate stops
// at the first non-passive screen above UITacticalHUD and reports its class name; while the pad
// is up that name is UIHackingScreen, which is exactly the instant the tile cursor stands down.
bool ScreenUp()
{
    GameUI::TacticalGate gate = GameUI::ReadTacticalGate();
    return gate.blocked && strcmp(gate.blocker, kScreenClass) == 0;
}

// One outbound call to the companion reader, then speak whatever it wrote to NativeResult.
// Game thread only, which is what outbound ProcessEvent requires. An empty result is a
// deliberate "nothing to say" — the poll answers that way on every tick after the first.
bool Call(const char* funcName, Narrator::Channel channel)
{
    void* cdo = GameUI::FindCompanionCDO(kReaderClass);
    if (!cdo)
        return false; // FindCompanionCDO already logs the miss (companion mod not loaded)

    // No-parameter functions still get a zeroed buffer — ProcessEvent copies ParmsSize bytes.
    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, funcName, zeros)) {
        LOG_WARN("HackingScreen: outbound call %s failed", funcName);
        return false;
    }

    std::string result
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (!result.empty())
        Narrator::Say(channel, result);
    return true;
}

// A pressed key must never be silent, so the on-demand handlers say so when the companion is
// missing entirely — the one case Call() swallows.
void CallOnDemand(const char* funcName, Narrator::Channel channel)
{
    if (!GameUI::FindCompanionCDO(kReaderClass)) {
        Narrator::Say(Narrator::Channel::Aid, "Hacking reader unavailable.");
        return;
    }
    Call(funcName, channel);
}

// Auto-repeat is off. Left/Right step across three cards and each step speaks a full card
// description; a held arrow would only stack descriptions the player cannot follow.
std::function<void(const InputArbiter::KeyEvent&)> OnPress(
    const char* funcName, Narrator::Channel channel)
{
    return [funcName, channel](const InputArbiter::KeyEvent& e) {
        if (e.down && !e.repeat)
            CallOnDemand(funcName, channel);
    };
}

} // namespace

void Init()
{
    using namespace InputArbiter;

    // Left / Right: the card cursor, and on cards 1 and 2 the game's own selection. These are
    // free here — the tile cursor's arrows stand down on exactly this screen (its gate reports
    // blocker=UIHackingScreen) and the menu navigator is detached.
    RegisterClaim({VK_LEFT, ModNone, Active, OnPress("SelectPrev", Narrator::Channel::Focus),
        "hacking: previous reward card"});
    RegisterClaim({VK_RIGHT, ModNone, Active, OnPress("SelectNext", Narrator::Channel::Focus),
        "hacking: next reward card"});

    // Delete: re-announce, mirroring the tile cursor's and the shot HUD's re-announce key. The
    // three never contend — each is gated on owning the surface, and only one ever does.
    RegisterClaim({VK_DELETE, ModNone, Active, OnPress("AnnounceHack", Narrator::Channel::Aid),
        "hacking: re-announce"});

    LOG_INFO("HackingScreen: claims registered");
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    bool up = ScreenUp();
    if (up != gScreenUp) {
        gScreenUp = up;
        LOG_INFO("HackingScreen: %s", up ? "open" : "closed");
        if (!up) {
            // A pad that goes away without ever having been committed was cancelled — the only
            // other way out of this screen. The player pressed Escape and the game answers with a
            // sound and nothing else, so this is the whole confirmation they get. gHackStarted
            // still holds the LAST polled state here, which is why it is read before the reset.
            if (!gHackStarted)
                Narrator::Say(Narrator::Channel::Aid, "Hack cancelled");
            // Let the companion forget what it announced, so a hack retried on the SAME object
            // after a cancel reads out again rather than opening in silence.
            Call("EndHack", Narrator::Channel::Aid);
            gHackStarted = false;
        }
    }

    if (!up)
        return;

    // Speaks twice per hack and is silent in between: the opening burst when the pad appears,
    // and the outcome when the meter stops. Alert, not Focus: nothing the player pressed produced
    // either line, but a modal that has taken over the screen outranks queued combat lines and
    // should cut in — while still keeping the burst that explains it intact.
    if (!Call("PollHack", Narrator::Channel::Alert))
        return;

    void* cdo = GameUI::FindCompanionCDO(kReaderClass);
    if (!cdo)
        return;
    std::string state
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeState");
    bool started = state == "started";
    if (started != gHackStarted) {
        gHackStarted = started;
        LOG_INFO("HackingScreen: hack %s — claims %s", started ? "committed" : "pending",
            started ? "released" : "held");
    }
}

} // namespace HackingScreen
