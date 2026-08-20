#include "object_scanner.h"
#include "free_aim.h"
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
constexpr int32_t kCatSquad = 2;
// Loot (3) and reinforcements (4) have no dedicated key — they are reached through the category
// ring below, so nothing here names them. Listed for the record, since this comment block is the
// only place the two halves' numbering is written down side by side.

// Delta 0 re-announces the current selection without moving it — the reason a one-entry ring is
// answerable at all without cycling through it.
void ScanCall(int32_t category, int32_t delta)
{
    struct {
        int32_t category;
        int32_t delta;
    } params{category, delta};
    Call("ScanCategory", &params);
}

// The ring keys and the re-announce key, which all address whichever category the companion's
// cursor is on. That cursor deliberately lives over there and not here — stepping it means knowing
// which categories are EMPTY, and only the model can answer that.
void StepCall(const char* funcName, int32_t delta)
{
    struct {
        int32_t delta;
    } params{delta};
    Call(funcName, &params);
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

    // Shared with the tile cursor and the free-aim layer rather than recomputed — see
    // TileReader::IsActive and FreeAim::IsActive. The scan itself is about the WORLD, not about
    // whichever cursor is being driven, so it reads identically in both modes; the two gates are
    // mutually exclusive by construction, since the tile cursor stands down exactly when the shot
    // HUD is raised. Only the action verb below differs between them.
    auto active = [] { return TileReader::IsActive() || FreeAim::IsActive(); };

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

    // Comma is period's twin and equally unbound: the two sit under the same two fingers, so
    // "who is on my side" and "who is not" are one key apart. Shift+comma stays the tile cursor's
    // floor-down key — the arbiter matches the exact chord, so the two never contend.
    RegisterClaim({VK_OEM_COMMA, ModNone, active, OnPress([] { ScanCall(kCatSquad, 1); }),
        "scanner: next squad member"});

    // Everything else is reached through the RING rather than through a key of its own. Three
    // dedicated keys is what the recon doc budgets — markers, enemies, squad, the ones pressed
    // every turn — and the ring is what keeps loot, reinforcements and each category after them
    // from costing a key the tactical layer does not have. It also leaves ; and ' free for the
    // vantage read and the inspect verb they are earmarked for.
    //
    // PageUp/PageDown are genuinely unbound in tactical (docs/object-scanner-recon.md §4.4) — no
    // Bindings= entry in any input section — so both the bare pair and the Ctrl pair are free, and
    // the Ctrl pair cannot double-fire off leniency because there is no bare binding to match.
    RegisterClaim({VK_NEXT, ModNone, active, OnPress([] { StepCall("ScanCurrent", 1); }),
        "scanner: next in category"});
    RegisterClaim({VK_PRIOR, ModNone, active, OnPress([] { StepCall("ScanCurrent", -1); }),
        "scanner: previous in category"});
    RegisterClaim({VK_NEXT, ModCtrl, active, OnPress([] { StepCall("StepCategory", 1); }),
        "scanner: next category"});
    RegisterClaim({VK_PRIOR, ModCtrl, active, OnPress([] { StepCall("StepCategory", -1); }),
        "scanner: previous category"});

    // Re-announce whatever was last said, wherever the ring cursor happens to be.
    RegisterClaim({'O', ModNone, active, OnPress([] { StepCall("ScanCurrent", 0); }),
        "scanner: re-announce selection"});

    // Slash plants the tile cursor on the selection — the scanner's whole action verb. XCOM
    // interaction is adjacency-gated, so "go there" is the useful move and the ability bar offers
    // Interact by itself once the soldier arrives. Shift+slash stays the global navigation-help
    // key; the bare key is unbound in every tactical input section.
    RegisterClaim({VK_OEM_2, ModNone, active,
        OnPress([] {
            // While free-aiming, "go there" means AIM there: the selection is what the player
            // wants to throw at, and planting a walk cursor instead would answer a question they
            // did not ask. Everything else about the scanner is unchanged between the modes.
            if (FreeAim::IsActive())
                FreeAim::AimAtMarker();
            else
                TileReader::GoToMarker();
        }),
        "scanner: cursor to marker / aim at marker"});

    LOG_INFO("ObjectScanner: claims registered");
}

} // namespace ObjectScanner
