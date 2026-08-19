#include "log.h"
#include "tts.h"
#include "input.h"
#include "menu_navigator.h"
#include "hooks.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <thread>

static HMODULE gThisModule = nullptr;

// Get the directory containing this DLL/ASI.
static std::string GetModuleDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(gThisModule, path, MAX_PATH);
    std::string dir(path);
    auto pos = dir.find_last_of("\\/");
    return (pos != std::string::npos) ? dir.substr(0, pos) : dir;
}

static void InitThread() {
    std::string dir = GetModuleDir();

    // 1. Logger
    if (!Log::Init(dir)) {
        // Can't log — but don't crash the game.
        return;
    }
    LOG_INFO("X2Access v0.1.0 initializing");
    LOG_INFO("Module directory: %s", dir.c_str());

    // Log process info
    DWORD pid = GetCurrentProcessId();
    HMODULE exe = GetModuleHandleA(nullptr);
    LOG_INFO("Process PID: %lu, EXE base: 0x%p", pid, exe);

    // 2. TTS (SRAL linked statically)
    if (TTS::Init()) {
        TTS::SpeakInterrupt("X2 Access loaded. XCOM 2 War of the Chosen accessibility mod active.");
    } else {
        LOG_WARN("TTS unavailable — continuing without speech");
    }

    // 3. Keyboard input
    Input::Init();

    // 4. Graph navigator (claims + announcer hooks — before game hooks go live)
    MenuNavigator::Init();

    // 5. Game hooks
    Hooks::Init();

    LOG_INFO("X2Access initialization complete");
}

static void ShutdownAll() {
    LOG_INFO("X2Access shutting down");
    Hooks::Shutdown();
    Input::Shutdown();
    TTS::Shutdown();
    Log::Shutdown();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        gThisModule = hModule;
        DisableThreadLibraryCalls(hModule);

        // Defer heavy init to a separate thread to avoid loader lock issues.
        std::thread(InitThread).detach();
    } else if (reason == DLL_PROCESS_DETACH) {
        ShutdownAll();
    }
    return TRUE;
}
