#include "avenger.h"
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
#include <string>

namespace Avenger {

namespace {

constexpr const char* kReaderClass = "X2AccessAvengerReader";

// Poll cadence. The same 150 ms the shot HUD and the tile cursor use — fast enough that a room
// stepped onto is spoken before the next arrow, cheap enough to leave running for a whole
// strategy session. It only runs while the ant farm is the current screen, so the geoscape, the
// shell and every other screen pay one stack walk here and no outbound call at all.
constexpr uint64_t kPollIntervalMs = 150;

// The claim's active predicate result, recomputed on the tick. The arbiter evaluates activeWhen
// inside the window procedure, so it must stay a flag read — the screen-stack walk behind it
// belongs on the tick, the same discipline as every other surface in this project.
bool gActive = false;

// One outbound call to the companion reader, then speak whatever it wrote to NativeResult.
// An empty result is a deliberate "nothing to say": the poll uses it on every tick where the
// selection has not moved.
//
// Focus channel, like the shot HUD's selection readout: this is the answer to a key the player
// just pressed, and it outranks anything still queued behind it.
void Call(const char* funcName, Narrator::Channel channel)
{
    // The shared helper logs both a missing companion and a failed call. Stay silent on the
    // poll path either way; the key handler below is the one that owes the player an answer.
    std::string result;
    if (!GameUI::CallCompanion(kReaderClass, funcName, nullptr, &result))
        return;

    // Strip markup. The strategy layer's own text is COLOURED — GetQueueMessage hands the
    // Armory's crew line back as "Солдаты: <font color='#53b45e'>Доступно: 11, </font>…" and
    // UpdateLockedRoom runs m_strLockedLabel through GetColoredText — so every one of those
    // tags reached speech verbatim on the first live run. The colours carry no information the
    // words do not (the counts are already labelled), which is why this is a strip rather than
    // the translation the tutorial reader's <img> markup needs.
    result = GameUI::CleanText(result);
    if (!result.empty())
        Narrator::Say(channel, result);
}

// A pressed key must never be silent, so the on-demand handler says so when the companion is
// missing entirely — the one case Call() swallows.
void OnAnnounce(const InputArbiter::KeyEvent& e)
{
    if (!e.down || e.repeat) // a held key would only stutter the same line over itself
        return;
    if (!GameUI::FindCompanionCDO(kReaderClass)) {
        Narrator::Say(Narrator::Channel::Aid, Vocab::SurfaceUnavailable("Avenger reader"));
        return;
    }
    Call("AnnounceRoom", Narrator::Channel::Aid);
}

} // namespace

void Init()
{
    using namespace InputArbiter;

    // Delete: re-announce the selected room. The same verb the tile cursor and the shot HUD put
    // on this key, and it never contends with either — those gate on tactical, this on a
    // strategy screen. What it displaces is DefaultInput.ini's [Engine.PlayerInput] binding of
    // Delete to Toggledebugcamera, which is a debug command a player has no reason to reach and
    // which stays available at Ctrl+Delete anyway (UE3 matches modifiers leniently).
    RegisterClaim({VK_DELETE, ModNone, [] { return gActive; }, OnAnnounce,
        "Avenger grid: re-announce room"});

    LOG_INFO("Avenger: claims registered");
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    // A graph recipe owns the whole surface when it is attached (the dialog recipe attaches over
    // strategy screens too), so it wins here the same way it does over the tile cursor.
    GameUI::AvengerGate gate = MenuNavigator::IsAttached() ? GameUI::AvengerGate{}
                                                           : GameUI::ReadAvengerGate();
    GameUI::StackState stack = GameUI::ReadStackState();
    bool active = gate.Active() && (!stack.valid || stack.Interactive());

    if (active != gActive) {
        gActive = active;
        if (active)
            LOG_INFO("Avenger: ant farm active");
        else
            LOG_INFO("Avenger: ant farm inactive (grid on stack %d, current %d, build %d, "
                     "blocker '%s')",
                gate.gridOnStack ? 1 : 0, gate.current ? 1 : 0, gate.buildMode ? 1 : 0,
                gate.blocker);
    }

    // The poll runs only while the grid is the current screen — the geoscape and every other
    // strategy screen cost nothing. The falling edge gets ONE more call: the companion resets
    // its change detection when its own lookup returns none, and without that call it would
    // never see the screen go, so coming back to the same selected room would be silent.
    static bool sWasActive = false;
    if (active || sWasActive)
        Call("PollGrid", Narrator::Channel::Focus);
    sWasActive = active;
}

} // namespace Avenger
