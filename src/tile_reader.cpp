#include "tile_reader.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "menu_navigator.h"
#include "tts.h"
#include "ue3_reflection.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace TileReader {

namespace {

constexpr const char* kReaderClass = "X2AccessTileReader";

// The claims' active predicate result, recomputed on the tick (the arbiter evaluates
// activeWhen inside the window procedure, so it must stay a flag read — the screen lookups
// behind it belong on the tick, same discipline as the menu navigator's IsAttached gate).
bool gActive = false;

// The gate is the screen-stack walk in GameUI::ReadTacticalGate — NOT IsScreenFocused, which
// is right for menus but wrong here: in tactical the focus bit lives on the permanent UIReplay
// overlay and UITacticalHUD never carries it (first live run read 0x238 and stayed inactive).
// `why` (>= 96 bytes) gets the reason when the result is false, for the transition log.
bool ComputeActive(char* why, size_t whyLen)
{
    why[0] = '\0';
    // A graph recipe owning the surface (pause menu, a modal dialog over tactical) wins.
    if (MenuNavigator::IsAttached()) {
        snprintf(why, whyLen, "graph recipe attached");
        return false;
    }
    GameUI::TacticalGate gate = GameUI::ReadTacticalGate();
    if (gate.blocked) {
        snprintf(why, whyLen, "blocked by %s", gate.blocker);
        return false;
    }
    if (!gate.hudOnStack) {
        snprintf(why, whyLen, "no tactical HUD on the stack");
        return false;
    }
    // The shot HUD's own targeting UI consumes the arrows (ability cycling, target cycling) —
    // the tile cursor stands down rather than swallowing them.
    if (gate.menuRaised) {
        snprintf(why, whyLen, "shot HUD raised");
        return false;
    }
    GameUI::StackState stack = GameUI::ReadStackState();
    if (stack.valid && !stack.Interactive()) {
        snprintf(why, whyLen, "UI gated (input blocked / hidden / cinematic)");
        return false;
    }
    return true;
}

// One outbound call to the companion reader, then speak whatever it wrote to NativeResult.
// Runs from the arbiter's tick drain — the game thread — which is what outbound ProcessEvent
// requires. An empty result is a deliberate "nothing to say", not an error.
void Call(const char* funcName, void* params)
{
    void* cdo = GameUI::FindCompanionCDO(kReaderClass);
    if (!cdo) {
        TTS::SpeakInterrupt("Tile reader unavailable.");
        return;
    }
    if (!GameUI::CallScriptFunction(cdo, funcName, params)) {
        LOG_WARN("TileReader: outbound call %s failed", funcName);
        return;
    }
    std::string result
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (!result.empty())
        TTS::SpeakInterrupt(result);
}

void StepCall(int32_t screenDX, int32_t screenDY)
{
    struct {
        int32_t dx;
        int32_t dy;
    } params{screenDX, screenDY};
    Call("CursorStep", &params);
    // Claimed keys bypass the game's own sound path (same as the menus), so the movement cue
    // is plumbed explicitly — first live test was dead quiet.
    GameUI::PlayNavSound();
}

void FloorCall(int32_t delta)
{
    struct {
        int32_t delta;
    } params{delta};
    Call("CursorFloor", &params);
    GameUI::PlayNavSound();
}

void SimpleCall(const char* funcName)
{
    // No-parameter functions still get a zeroed buffer — ProcessEvent copies ParmsSize bytes.
    int64_t zeros[2] = {};
    Call(funcName, zeros);
}

// Everything fires once per press — auto-repeat is DISABLED for now, including on the arrows:
// the first live test read as multi-tile jumps, and until one-press-one-tile is proven, repeat
// marching would only blur the diagnosis. Revisit held-key marching once stepping is trusted.
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

    auto active = [] { return gActive; };

    // Arrows: camera-relative tile steps (+Y = up on screen). WASD keeps camera pan for a
    // sighted co-pilot — the game binds both, we take only the arrow set.
    RegisterClaim({VK_UP, ModNone, active, OnPress([] { StepCall(0, 1); }), "tile cursor: step forward"});
    RegisterClaim({VK_DOWN, ModNone, active, OnPress([] { StepCall(0, -1); }), "tile cursor: step back"});
    RegisterClaim({VK_LEFT, ModNone, active, OnPress([] { StepCall(-1, 0); }), "tile cursor: step left"});
    RegisterClaim({VK_RIGHT, ModNone, active, OnPress([] { StepCall(1, 0); }), "tile cursor: step right"});

    // Shift+comma / Shift+period (the literal < and >): floor down / up. The UNSHIFTED keys
    // stay unclaimed — reserved for the object scanner's quick navigation.
    RegisterClaim({VK_OEM_COMMA, ModShift, active, OnPress([] { FloorCall(-1); }),
        "tile cursor: floor down"});
    RegisterClaim({VK_OEM_PERIOD, ModShift, active, OnPress([] { FloorCall(1); }),
        "tile cursor: floor up"});

    RegisterClaim({'C', ModNone, active, OnPress([] { SimpleCall("CursorRecenter"); }),
        "tile cursor: recenter on unit"});
    RegisterClaim({VK_DELETE, ModNone, active, OnPress([] { SimpleCall("CursorAnnounce"); }),
        "tile cursor: re-announce"});
    RegisterClaim({VK_BACK, ModNone, active, OnPress([] { SimpleCall("CursorMoveTo"); }),
        "tile cursor: move to (press twice)"});

    LOG_INFO("TileReader: claims registered");
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    uint64_t now = GetTickCount64();
    if (now - sLastMs < 150)
        return;
    sLastMs = now;

    char why[96];
    bool active = ComputeActive(why, sizeof(why));
    if (active != gActive) {
        gActive = active;
        if (active)
            LOG_INFO("TileReader: active (tactical HUD owns the surface)");
        else
            LOG_INFO("TileReader: inactive — %s", why);
    }
}

} // namespace TileReader
