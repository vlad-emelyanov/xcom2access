#include "input.h"
#include "input_arbiter.h"
#include "hooks.h"
#include "tts.h"
#include "event_dispatch.h"
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

void Init()
{
    using namespace InputArbiter;

    RegisterClaim({VK_F1, ModNone, nullptr, OnPress([] {
        std::string status = "X2 Access is running. ";
        status += IsInstalled() ? "Input arbitration active."
                                : "Input arbitration not yet installed.";
        status += " Press F2 for help.";
        TTS::SpeakInterrupt(status);
    }), "F1: status"});

    RegisterClaim({VK_F2, ModNone, nullptr, OnPress([] {
        TTS::SpeakInterrupt(
            "X2 Access hotkeys. "
            "F1: Status. "
            "F2: This help message. "
            "F3: TTS engine info. "
            "F4: Toggle event discovery logging. "
            "Question mark: Read navigation help. "
            "Control plus Shift plus S: Toggle speech on or off.");
    }), "F2: help"});

    RegisterClaim({VK_F3, ModNone, nullptr, OnPress([] {
        TTS::SpeakInterrupt(TTS::GetEngineInfo());
    }), "F3: engine info"});

    RegisterClaim({VK_F4, ModNone, nullptr, OnPress([] {
        bool nowOn = !EventDispatch::IsDiscoveryLogging();
        EventDispatch::SetDiscoveryLogging(nowOn);
        TTS::SpeakInterrupt(nowOn ? "Event discovery logging enabled."
                                  : "Event discovery logging disabled.");
    }), "F4: event discovery"});

    RegisterClaim({'S', static_cast<uint8_t>(ModCtrl | ModShift), nullptr, OnPress([] {
        bool nowEnabled = !TTS::IsEnabled();
        TTS::SetEnabled(nowEnabled);
        LOG_INFO("Hotkey: Ctrl+Shift+S — speech %s", nowEnabled ? "enabled" : "disabled");
        if (nowEnabled)
            TTS::SpeakInterrupt("Speech enabled.");
    }), "Ctrl+Shift+S: speech toggle"});

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
