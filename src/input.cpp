#include "input.h"
#include "hooks.h"
#include "tts.h"
#include "event_dispatch.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <thread>

namespace Input {

static std::atomic<bool> sRunning{false};
static std::thread sThread;

// Track key-down state for debounce — only fire on transition from up to down.
static bool sKeyDown[256] = {};

static bool IsPressed(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static void PollLoop() {
    LOG_INFO("Input polling thread started");

    while (sRunning.load(std::memory_order_relaxed)) {
        // F1 — Status
        if (IsPressed(VK_F1)) {
            if (!sKeyDown[VK_F1]) {
                sKeyDown[VK_F1] = true;
                LOG_DEBUG("Hotkey: F1");
                TTS::SpeakInterrupt("X2 Access is running. Press F2 for help.");
            }
        } else {
            sKeyDown[VK_F1] = false;
        }

        // F2 — Help
        if (IsPressed(VK_F2)) {
            if (!sKeyDown[VK_F2]) {
                sKeyDown[VK_F2] = true;
                LOG_DEBUG("Hotkey: F2");
                TTS::SpeakInterrupt(
                    "X2 Access hotkeys. "
                    "F1: Status. "
                    "F2: This help message. "
                    "F3: TTS engine info. "
                    "F4: Toggle event logging. "
                    "Question mark: Read navigation help. "
                    "Control plus Shift plus S: Toggle speech on or off."
                );
            }
        } else {
            sKeyDown[VK_F2] = false;
        }

        // F3 — Engine info
        if (IsPressed(VK_F3)) {
            if (!sKeyDown[VK_F3]) {
                sKeyDown[VK_F3] = true;
                LOG_DEBUG("Hotkey: F3");
                std::string info = TTS::GetEngineInfo();
                TTS::SpeakInterrupt(info);
            }
        } else {
            sKeyDown[VK_F3] = false;
        }

        // F4 — Toggle verbose event logging
        if (IsPressed(VK_F4)) {
            if (!sKeyDown[VK_F4]) {
                sKeyDown[VK_F4] = true;
                LOG_DEBUG("Hotkey: F4");
                bool nowVerbose = !EventDispatch::IsVerboseLogging();
                EventDispatch::SetVerboseLogging(nowVerbose);
                TTS::SpeakInterrupt(nowVerbose ? "Event logging enabled." : "Event logging disabled.");
            }
        } else {
            sKeyDown[VK_F4] = false;
        }

        // Ctrl+Shift+S — Toggle speech
        if (IsPressed(VK_CONTROL) && IsPressed(VK_SHIFT) && IsPressed('S')) {
            if (!sKeyDown['S']) {
                sKeyDown['S'] = true;
                bool nowEnabled = !TTS::IsEnabled();
                TTS::SetEnabled(nowEnabled);
                LOG_INFO("Hotkey: Ctrl+Shift+S — speech %s", nowEnabled ? "enabled" : "disabled");
                if (nowEnabled) {
                    TTS::SpeakInterrupt("Speech enabled.");
                }
                // If disabling, speech is already stopped by SetEnabled
            }
        } else {
            sKeyDown['S'] = false;
        }

        // Shift+/ (?) — Read navigation help from companion mod
        if (IsPressed(VK_SHIFT) && IsPressed(VK_OEM_2)) {
            if (!sKeyDown[VK_OEM_2]) {
                sKeyDown[VK_OEM_2] = true;
                LOG_DEBUG("Hotkey: ? (nav help)");
                std::string navHelp = Hooks::ReadListenerProperty("NavHelpData");
                if (!navHelp.empty()) {
                    TTS::SpeakInterrupt(navHelp);
                } else {
                    TTS::SpeakInterrupt("No navigation help available.");
                }
            }
        } else {
            sKeyDown[VK_OEM_2] = false;
        }

        Sleep(50);
    }

    LOG_INFO("Input polling thread stopped");
}

void Init() {
    if (sRunning.load()) return;
    sRunning.store(true);
    sThread = std::thread(PollLoop);
}

void Shutdown() {
    if (!sRunning.load()) return;
    sRunning.store(false);
    if (sThread.joinable()) {
        sThread.join();
    }
}

} // namespace Input
