#include "object_scanner.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "tile_reader.h"
#include "tts.h"
#include "ue3_reflection.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace ObjectScanner {

namespace {

constexpr const char* kScannerClass = "X2AccessObjectScanner";

// One outbound call to the companion scanner, then speak whatever it wrote to NativeResult.
// Runs from the arbiter's tick drain — the game thread — which is what outbound ProcessEvent
// requires. An empty result is a deliberate "nothing to say", not an error.
void Call(const char* funcName, void* params)
{
    void* cdo = GameUI::FindCompanionCDO(kScannerClass);
    if (!cdo) {
        TTS::SpeakInterrupt("Object scanner unavailable.");
        return;
    }
    if (!GameUI::CallScriptFunction(cdo, funcName, params)) {
        LOG_WARN("ObjectScanner: outbound call %s failed", funcName);
        return;
    }
    std::string result
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (!result.empty())
        TTS::SpeakInterrupt(result);
}

// Category ids. MUST match the CAT_* constants in X2AccessObjectScanner.uc — the one place the
// two halves share a number rather than a name.
constexpr int32_t kCatMarkers = 0;
constexpr int32_t kCatEnemies = 1;

// Which category the last cycle key addressed, so re-announce knows what to repeat. It lives here
// rather than on the CDO because it describes the KEYBOARD's state, not the model's: the companion
// remembers a position per category, and this remembers which category you were last in.
int32_t gLastCategory = kCatMarkers;

// Delta 0 re-announces the current selection without moving it — the reason a one-entry ring is
// answerable at all without cycling through it.
void ScanCall(int32_t category, int32_t delta)
{
    struct {
        int32_t category;
        int32_t delta;
    } params{category, delta};
    gLastCategory = category;
    Call("ScanCategory", &params);
}

// Once per press, no auto-repeat: same discipline as the tile cursor, and a held cycle key would
// race through a short ring faster than the reader can speak any of it.
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

    // Shared with the tile cursor rather than recomputed — see TileReader::IsActive.
    auto active = [] { return TileReader::IsActive(); };

    // B and O both carry vanilla actions on the bare key (push-to-talk, the Chosen Info screen).
    // Taking them costs the player nothing: UE3 matches modifiers leniently, so the arbiter
    // swallowing the BARE key leaves Ctrl+B / Ctrl+O reaching the vanilla binding untouched, and
    // the claim stands down entirely outside world mode. No config snippet, no rebind — see
    // CLAUDE.md, "The accessibility verbs outrank the vanilla bindings".
    RegisterClaim({'B', ModNone, active, OnPress([] { ScanCall(kCatMarkers, 1); }),
        "scanner: next marker"});

    // Period is RT's own "cycle enemies" key and is unbound in every tactical input section, so
    // unlike B and O it displaces nothing at all. Shift+period stays the tile cursor's floor key —
    // the arbiter matches the exact chord, so the two never contend.
    RegisterClaim({VK_OEM_PERIOD, ModNone, active, OnPress([] { ScanCall(kCatEnemies, 1); }),
        "scanner: next enemy"});

    // Re-announce whatever the last cycle key said, in its own category.
    RegisterClaim({'O', ModNone, active, OnPress([] { ScanCall(gLastCategory, 0); }),
        "scanner: re-announce selection"});

    // Slash plants the tile cursor on the selection — the scanner's whole action verb. XCOM
    // interaction is adjacency-gated, so "go there" is the useful move and the ability bar offers
    // Interact by itself once the soldier arrives. Shift+slash stays the global navigation-help
    // key; the bare key is unbound in every tactical input section.
    RegisterClaim({VK_OEM_2, ModNone, active, OnPress([] { TileReader::GoToMarker(); }),
        "scanner: cursor to marker"});

    LOG_INFO("ObjectScanner: claims registered");
}

} // namespace ObjectScanner
