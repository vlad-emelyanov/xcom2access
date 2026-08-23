#include "log.h"
#include "dev_server.h"
#include "tts.h"
#include "input.h"
#include "menu_navigator.h"
#include "mouse_gate.h"
#include "object_scanner.h"
#include "geo_scanner.h"
#include "free_aim.h"
#include "tile_reader.h"
#include "avenger.h"
#include "dropship.h"
#include "text_input.h"
#include "tactical_hud.h"
#include "unit_info.h"
#include "objective_reader.h"
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
    //
    // ⚠ Straight to the backend, and one of only four such lines left in the project (the other
    // three are Hooks::Init's). Two reasons, and the second is the durable one. It runs on the
    // INIT thread, and Narrator::Say declares itself game-thread only. And it reports that the
    // MOD loaded, before any producer can have queued a thing — so the queue has nothing to
    // order it against, and the scrollback ring, which answers "what happened in the game", is
    // not where it belongs. Everything that speaks ABOUT THE GAME goes through Narrator::Say.
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

    // 6. Free aim (claims — same; the tile cursor's arrows and floor keys again, under the
    // opposite gate: this layer owns them exactly while the shot HUD is raised over a
    // cursor-targeted ability, which is when the tile cursor stands down)
    FreeAim::Init();

    // 7. Object scanner (claims — same; shares BOTH cursor gates, so it must come after them)
    ObjectScanner::Init();

    // 8. Shot HUD narration (claims — same; also publishes the free-aim gate from its poll)
    TacticalHUD::Init();

    // 9. Selected-unit readout (claims — same; one key, and the gate is TacticalHUD's, so it
    // must come after it)
    UnitInfo::Init();

    // 9b. Objectives readout (claims — same; one Ctrl chord). Gates on its own panel being
    // present rather than on a tactical gate, because the same UIObjectiveList serves the
    // Avenger, so this is live in both layers and contends with neither.
    ObjectiveReader::Init();

    // 10. Hacking overlay (claims — same; arrows and Delete, live only while the pad is up)
    HackingScreen::Init();

    // 11. Tutorial message reader (claims — same)
    TutorialReader::Init();

    // 12. Avenger ant farm (claims — same; one re-announce key, no arrows: the game's own
    // navigation graph does the moving once the companion installs it)
    Avenger::Init();

    // 13. Geoscape ring (claims — same). An AID, not parity: it reads the whole globe, including
    // the fourteen regions the mission tray never holds, so it lives on its own keys and stays
    // out of the tray recipe's Tab cycle. Its gate is the same UIStrategyMap-is-current test the
    // recipe probes on, so the two stand down at the same instant.
    GeoScanner::Init();

    // 14. Skyranger load screen (claims — same; one re-announce key). Its gate is the transition
    // map's own briefing screen, which exists in no layer any of the above can be active in, so
    // it contends with none of them.
    Dropship::Init();

    // 15. Text-entry dialog (naming a save). Registers an OBSERVER, not a claim: the keys must
    // reach the Flash text field AND be spoken, because no screen reader can see that field.
    TextInput::Init();

    // 16. Game hooks
    Hooks::Init();

    // 17. Dev harness (dev builds + marker file only; no-op otherwise). Last, so it can report
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
