#include "input.h"
#include "input_arbiter.h"
#include "hooks.h"
#include "narrator.h"
#include "log.h"
#include "vocab.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace Input {

namespace {

// Edge-triggered hotkey: fire on the press, ignore auto-repeats and the release.
std::function<void(const InputArbiter::KeyEvent&)> OnPress(std::function<void()> fn)
{
    return [fn = std::move(fn)](const InputArbiter::KeyEvent& e) {
        if (e.down && !e.repeat)
            fn();
    };
}

} // namespace

// The project's only GLOBAL claims — the ones that hold on every screen, in every layer, for the
// whole session. Everything else is claimed by the module that owns the surface and stands down
// with it.
//
// There used to be four more: F1 status, F2 help, F3 engine info and F4 event-discovery logging,
// plus Ctrl+Shift+S to mute speech. All five were built as TESTING scaffolding before the dev
// harness existed, and the harness answers each of them better and without costing a chord —
// /health for the build stamp, /speech for exactly what the reader was handed, /log for the rest.
// They were retired 2026-08-21.
//
// ⛔ Retiring them was not only tidying: F2/F3/F4 are LIVE GAME KEYS in the Avenger.
// UIFacilityStaffContainer.OnUnrealCommand maps FXS_KEY_F2..F4 to "activate staff slot 0..2", with
// no `ISCONTROLLERACTIVE gate, so on mouse+keyboard they are the game's own direct route into a
// facility's staffing — and a global claim swallowed all three for as long as they shipped. F1-F8
// are also the shot HUD's target hotkeys (UITacticalHUD.uc:432). A global claim has to earn the
// whole keyboard, not just the screens we happened to test on.
void Init()
{
    using namespace InputArbiter;

    // ⛔ KNOWN GAP, and it is the answer this key gives that is wrong, not the key itself.
    // NavHelpData comes off the v1 sentinel path, and exactly three classes ever write it —
    // UIScreenListener_MainMenu, _Difficulty and _NarrativeContent, each from its own OnInit.
    // No recipe writes it, no tactical or strategy surface writes it, and NOTHING clears it: the
    // listeners are CDOs, so the string outlives the screen that filled it. Two consequences.
    // On those three screens the answer is right (the listener's OnInit runs whether or not a
    // recipe owns the surface, so this is deliberately NOT gated on MenuNavigator::IsAttached).
    // Everywhere else it is either the honest fallback below or — once the shell has been
    // visited — the MAIN MENU's nav help, recited in the middle of a mission. A help key that
    // describes a different screen is worse than one that says nothing.
    //
    // The fix is not more producers: the claim registry already knows what works here, because
    // every Claim carries a description written as help text ("tile cursor: north", "scanner:
    // re-announce selection"). Enumerating the ACTIVE ones answers the question the player is
    // actually asking, on every surface, and stays correct as claims are added. That needs a
    // DescribeActiveClaims in input_arbiter.cpp; until it exists the legacy read stands, since
    // the three screens it does cover would otherwise lose the help they have.
    RegisterClaim({VK_OEM_2, ModShift, nullptr, OnPress([] {
        std::string navHelp = Hooks::ReadListenerProperty("NavHelpData");
        // Aid, through the Narrator: an on-demand read, queued in order and labelled as such in
        // the scrollback ring.
        Narrator::Say(Narrator::Channel::Aid,
            !navHelp.empty() ? navHelp : std::string(Vocab::NavHelpUnavailable));
    }), "?: navigation help"});

    LOG_INFO("Input: hotkey claims registered");
}

void Shutdown()
{
    InputArbiter::Shutdown();
}

} // namespace Input
