#include "input.h"
#include "input_arbiter.h"
#include "hooks.h"
#include "tts.h"
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

    RegisterClaim({VK_OEM_2, ModShift, nullptr, OnPress([] {
        std::string navHelp = Hooks::ReadListenerProperty("NavHelpData");
        TTS::SpeakInterrupt(!navHelp.empty() ? navHelp : "No navigation help available.");
    }), "?: navigation help"});

    LOG_INFO("Input: hotkey claims registered");
}

void Shutdown()
{
    InputArbiter::Shutdown();
}

} // namespace Input
