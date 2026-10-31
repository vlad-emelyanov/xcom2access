#include "input.h"
#include "input_arbiter.h"
#include "help_pages.h"
#include "log.h"

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

    // ? (Shift+Slash) speaks help for the CURRENT CONTEXT, one page per press (src/help_pages.cpp:
    // the context is computed from the same published gates the claims use, and the pages are
    // X2AccessVocab words). It is global because help must exist wherever a player is lost; the
    // context decides what it says, and a screen no recipe reads gets the honest "no help".
    //
    // The retired answer was NavHelpData, a string only three legacy listeners ever wrote (the
    // main menu, difficulty and narrative content screens) and nothing ever cleared, so it was
    // either right on those three screens or the main menu's help recited mid-mission. There is
    // no way to prove from here that the string belongs to the screen on top, so it is no longer
    // read at all: a generic menu page is short, a wrong one is worse.
    RegisterClaim({VK_OEM_2, ModShift, nullptr, OnPress([] { HelpPages::Speak(); }),
        "?: context help"});

    LOG_INFO("Input: hotkey claims registered");
}

void Shutdown()
{
    InputArbiter::Shutdown();
}

} // namespace Input
