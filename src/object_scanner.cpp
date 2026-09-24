#include "object_scanner.h"
#include "free_aim.h"
#include "script_call.h"
#include "input_arbiter.h"
#include "narrator.h"
#include "tile_reader.h"
#include "ue3_reflection.h"
#include "vocab.h"
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
//
// Aid, through the Narrator rather than TTS directly. Every line here is an on-demand read the
// player pressed a key for; it queues in order with the combat narration of the same frame
// (speaking straight to the backend jumped it ahead of the burst it belonged after), and it
// lands in the scrollback ring, which a review key will need.
void Call(const char* funcName, void* params)
{
    // The missing-companion case is checked FIRST so a pressed key is never silent — the shared
    // helper would log it, but the player deserves the spoken line.
    if (!GameUI::FindCompanionCDO(kScannerClass)) {
        Narrator::Say(Narrator::Channel::Aid, Vocab::SurfaceUnavailable(Vocab::SurfaceObjectScanner));
        return;
    }
    std::string result;
    if (!GameUI::CallCompanion(kScannerClass, funcName, params, &result))
        return;
    if (!result.empty())
        Narrator::Say(Narrator::Channel::Aid, result);
}

// Category ids. MUST match the CAT_* constants in X2AccessObjectScanner.uc — the one place the
// two halves share a number rather than a name.
constexpr int32_t kCatMarkers = 0;
constexpr int32_t kCatEnemies = 1;
constexpr int32_t kCatSquad = 2;
// Everything from loot (3) to level connectors (9) has no dedicated key — those are reached
// through the category ring below, so nothing here names them. Listed for the record, since this
// comment block is the only place the two halves' numbering is written down side by side.
constexpr int32_t kCatCover = 10;
// 11 is world.unexplored, ring-only. 12 is the exits cycle below.
constexpr int32_t kCatExits = 12;

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

// A no-parameter companion function still gets a zeroed buffer — ProcessEvent copies ParmsSize
// bytes out of whatever it is handed.
void SimpleCall(const char* funcName)
{
    int64_t zeros[2] = {};
    Call(funcName, zeros);
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
    // unlike B and O it displaces nothing at all.
    RegisterClaim({VK_OEM_PERIOD, ModNone, active, OnPress([] { ScanCall(kCatEnemies, 1); }),
        "scanner: next enemy"});

    // Comma is period's twin and equally unbound: the two sit under the same two fingers, so
    // "who is on my side" and "who is not" are one key apart.
    RegisterClaim({VK_OEM_COMMA, ModNone, active, OnPress([] { ScanCall(kCatSquad, 1); }),
        "scanner: next squad member"});

    // SHIFT REVERSES, on every dedicated cycle key — the idiom WrathAccess (Pathfinder: WotR)
    // uses across all seven of its review keys, and the one every other scanner in this lineage
    // has. Before this the only way back was PageUp, which walks the RING's current category and
    // so could not step "one enemy back" while the ring sat on markers. The floor keys vacated
    // Shift for exactly this (they are on Ctrl now).
    RegisterClaim({'B', ModShift, active, OnPress([] { ScanCall(kCatMarkers, -1); }),
        "scanner: previous marker"});
    RegisterClaim({VK_OEM_PERIOD, ModShift, active, OnPress([] { ScanCall(kCatEnemies, -1); }),
        "scanner: previous enemy"});
    RegisterClaim({VK_OEM_COMMA, ModShift, active, OnPress([] { ScanCall(kCatSquad, -1); }),
        "scanner: previous squad member"});

    // J / Shift+J — the COVER POSITIONS nearest the scan origin: the tiles you could stand on to
    // be behind something, named by the side(s) they cover, in or out of this turn's movement
    // range so a position can be planned before it can be reached. The WH40KRTAccess key of the
    // same name and the same verb (its `scan.cover_next` / `scan.cover_prev`).
    //
    // A fourth dedicated key against the recon doc's budget of three, and it earns the slot the
    // way the other three did — it answers a question asked every single turn, and the ring is
    // the wrong place for it because reaching cover through Ctrl+PageDown means stepping past
    // every other category while a pod is looking at you. Like every cycle key here it only
    // SELECTS: `/` then plants the tile cursor on the tile, and Backspace walks there.
    //
    // ⚠ Bare J carries a vanilla `J_Key_Press` forwarder in BOTH [Engine.PlayerInput] (:183) and
    // [XComGame.XComTacticalInput] (:306) — one of the generic ability-hotkey forwarders that
    // look empty and are not. It costs the player nothing for exactly the reason B and O above
    // cost nothing: UE3 matches modifiers leniently, so swallowing the BARE key leaves the
    // vanilla action reachable at Ctrl+J, and the claim stands down entirely outside world mode.
    // No config snippet, no rebind.
    RegisterClaim({'J', ModNone, active, OnPress([] { ScanCall(kCatCover, 1); }),
        "scanner: next cover position"});
    RegisterClaim({'J', ModShift, active, OnPress([] { ScanCall(kCatCover, -1); }),
        "scanner: previous cover position"});

    // V / Shift+V — the EXITS: the doors, ramps and ladders that get you out of where you are,
    // nearest first. RT's own V and RT's own verb (its RoomModel.CycleExit), which is why the key
    // is the one the player asked for by name.
    //
    // A fifth dedicated key, and it passes the same test the other four did — "which way out" is
    // asked every time a room turns out to be the wrong room, and reaching it through the ring
    // would mean stepping past eleven categories while doing it.
    //
    // ⚠ Bare V carries a vanilla `V_Key_Press` forwarder in [XComGame.XComTacticalInput] — it is
    // the Interact ability's default hotkey (`AddInteractAbility` sets DefaultKeyBinding =
    // FXS_KEY_V, and XComTacticalController:2628 routes Key_V to ActivateAbilityByHotKey). That is
    // EXACTLY the shape of J's binding above and it costs the player exactly as much: UE3 matches
    // modifiers leniently, so swallowing the bare key leaves Interact reachable at Ctrl+V with no
    // config and no rebind, and the claim stands down entirely outside world mode. Interact is
    // also on the ability bar, which the tactical recipe already reads.
    // ⛔ docs/rooms-recon.md §6 called this "the first vanilla binding the mod DISPLACES". That was
    // wrong when written — B, O and J are all generic ability-hotkey forwarders taken on the same
    // terms, J since the cover ring — so V is precedent, not a precedent-setter.
    RegisterClaim({'V', ModNone, active, OnPress([] { ScanCall(kCatExits, 1); }),
        "scanner: next exit"});
    RegisterClaim({'V', ModShift, active, OnPress([] { ScanCall(kCatExits, -1); }),
        "scanner: previous exit"});

    // Everything else is reached through the RING rather than through a key of its own. The
    // recon doc budgets THREE dedicated keys — markers, enemies, squad — and cover is the one
    // addition to that list, on the same test the three passed: a question asked every turn.
    // The ring is what keeps loot, reinforcements and each category after them from costing a
    // key the tactical layer does not have. It also left ; and ' free — ; has since gone to the
    // selected-unit readout (unit_info.cpp) and ' is still earmarked for the vantage read.
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

    // ---- why the Shift chords above are safe ----
    //
    // In vanilla `LeftShift` is bound in [XComGame.XComTacticalInput] and `Key_Left_Shift`'s
    // RELEASE calls PrevUnit() UNCONDITIONALLY in state ActiveUnit_Moving
    // (XComTacticalInput.uc:2147), so the tail of EVERY Shift chord cycled the selected soldier.
    // The arbiter used to swallow the modifier itself and re-provide Shift+Tab by hand. Neither
    // exists any more: the companion's Config/XComInput.ini moves the previous-unit command off
    // LeftShift onto Q (and next-unit off Tab onto E), and a command with no binding has no
    // handler to reach — the release fires nothing. Shift is an ordinary modifier here now, and
    // the rule is in docs/input-map.md. If a Shift chord ever cycles the soldier again, the
    // first thing to check is whether that config still merges (`just dev-props`), not this file.

    LOG_INFO("ObjectScanner: claims registered");
}

} // namespace ObjectScanner
