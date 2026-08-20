#include "log.h"
#include "dev_server.h"
#include "tts.h"
#include "input.h"
#include "menu_navigator.h"
#include "mouse_gate.h"
#include "object_scanner.h"
#include "tile_reader.h"
#include "tactical_hud.h"
#include "hacking_screen.h"
#include "tutorial_reader.h"
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

    // 2. TTS (Prism linked statically)
    if (TTS::Init()) {
        TTS::SpeakInterrupt("X2 Access loaded. XCOM 2 War of the Chosen accessibility mod active.");
    } else {
        LOG_WARN("TTS unavailable — continuing without speech");
    }

    // 3. Keyboard input
    Input::Init();

    // 4. Graph navigator (claims + announcer hooks — before game hooks go live)
    MenuNavigator::Init();

    // 5. Tactical tile cursor (claims — before game hooks go live)
    TileReader::Init();

    // 6. Object scanner (claims — same; shares the tile cursor's gate, so it must come after it)
    ObjectScanner::Init();

    // 7. Shot HUD narration (claims — same)
    TacticalHUD::Init();

    // 8. Hacking overlay (claims — same; arrows and Delete, live only while the pad is up)
    HackingScreen::Init();

    // 9. Tutorial message reader (claims — same)
    TutorialReader::Init();

    // 10. Game hooks
    Hooks::Init();

    // 11. Dev harness (dev builds + marker file only; no-op otherwise). Last, so it can report
    // on everything above, and so a failure here can never keep the mod itself from loading.
    DevServer::Start(dir);

    LOG_INFO("X2Access initialization complete");
}

static void ShutdownAll() {
    LOG_INFO("X2Access shutting down");
    MouseGate::ForceRestore(); // first: no game code may keep dispatching into this module
    DevServer::Stop(); // before the hooks: the pump it depends on goes away with them
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
