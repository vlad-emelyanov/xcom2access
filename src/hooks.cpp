#include "hooks.h"
#include "sdk_globals.h"
#include "event_dispatch.h"
#include "process_event_hook.h"
#include "ue3_reflection.h"
#include "tts.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// SDK types for event handler lambdas
#include "GameDefines.hpp"
#include "SdkConstants.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

namespace Hooks {

static constexpr int SDK_INIT_MAX_ATTEMPTS = 30;
static constexpr int SDK_INIT_RETRY_MS = 1000;

// Cached pointer to the X2AccessCompanion UIScreenListener instance.
// Set when X2Access_Speak fires; used by Input for on-demand reads.
static UObject* sActiveListener = nullptr;

// SEH-safe helper to log an object name (no C++ destructors in SEH scope)
static void SafeLogObjectName(const char* prefix, UObject* obj) {
    char buf[512] = {};
    __try {
        // Read object name through FName — just the short name, no std::string
        FName& fname = obj->Name;
        if (fname.IsValid()) {
            FNameEntry* entry = FName::Names()->at(fname.GetDisplayIndex());
            if (entry) {
                _snprintf_s(buf, sizeof(buf) - 1, "%s %s", prefix, entry->GetAnsiName());
                LOG_INFO("%s", buf);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Silently skip
    }
}

// Register initial event handlers for discovery and accessibility.
static void RegisterEventHandlers() {
    EventDispatch::Register("Tick", [](UObject*, UFunction*, void*) {
        // Retry any deferred handler registrations (mod FNames load later)
        EventDispatch::RetryPending();
    });

    EventDispatch::Register("OnInit", [](UObject* obj, UFunction*, void*) {
        // Skip Default__ objects (Class Default Objects) — they spam on load
        char buf[256] = {};
        __try {
            FName& fname = obj->Name;
            if (fname.IsValid()) {
                FNameEntry* entry = FName::Names()->at(fname.GetDisplayIndex());
                if (entry) {
                    const char* name = entry->GetAnsiName();
                    if (name && name[0] == 'D' && name[1] == 'e' && name[2] == 'f'
                        && name[3] == 'a' && name[4] == 'u' && name[5] == 'l'
                        && name[6] == 't' && name[7] == '_' && name[8] == '_')
                        return; // Skip Default__ objects
                    _snprintf_s(buf, sizeof(buf) - 1, "Hooks: OnInit on %s", name);
                    LOG_INFO("%s", buf);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    });

    // X2Access_Speak — fired by the X2AccessCompanion UnrealScript mod
    // when it has text ready for TTS (focus change, screen open, etc.)
    EventDispatch::Register("X2Access_Speak", [](UObject* obj, UFunction*, void*) {
        // Cache the listener for on-demand reads (e.g., ? key nav help)
        sActiveListener = obj;

        std::string text = UE3Reflect::ReadStringProperty(obj, "AccessibilityData");
        if (!text.empty()) {
            LOG_INFO("Hooks: X2Access_Speak -> \"%s\"", text.c_str());
            TTS::SpeakInterrupt(text);
        } else {
            LOG_WARN("Hooks: X2Access_Speak fired but AccessibilityData was empty");
        }
    });
}

std::string ReadListenerProperty(const char* propName) {
    if (!sActiveListener)
        return "";
    return UE3Reflect::ReadStringProperty(sActiveListener, propName);
}

void Init() {
    LOG_INFO("Hooks: Starting SDK initialization (up to %d attempts)...", SDK_INIT_MAX_ATTEMPTS);

    bool sdkReady = false;
    for (int attempt = 1; attempt <= SDK_INIT_MAX_ATTEMPTS; attempt++) {
        if (SDK::Init()) {
            sdkReady = true;
            LOG_INFO("Hooks: SDK initialized on attempt %d", attempt);
            break;
        }

        if (attempt < SDK_INIT_MAX_ATTEMPTS) {
            LOG_DEBUG("Hooks: SDK init attempt %d/%d failed, retrying in %dms...",
                      attempt, SDK_INIT_MAX_ATTEMPTS, SDK_INIT_RETRY_MS);
            Sleep(SDK_INIT_RETRY_MS);
        }
    }

    if (!sdkReady) {
        LOG_ERROR("Hooks: SDK initialization failed after %d attempts — game hooks disabled",
                  SDK_INIT_MAX_ATTEMPTS);
        TTS::SpeakInterrupt("Warning: game hooks could not initialize. Accessibility features unavailable.");
        return;
    }

    // Initialize event dispatch and register handlers
    EventDispatch::Init();
    RegisterEventHandlers();

    // Install the ProcessEvent vtable hook
    if (ProcessEventHook::Install()) {
        LOG_INFO("Hooks: ProcessEvent hook active — intercepting game events");
        TTS::SpeakInterrupt("Game hooks active.");
    } else {
        LOG_ERROR("Hooks: ProcessEvent hook installation failed");
        TTS::SpeakInterrupt("Warning: game hook installation failed.");
    }
}

void Shutdown() {
    ProcessEventHook::Remove();
    EventDispatch::Shutdown();
    SDK::Shutdown();
}

} // namespace Hooks
