#pragma once

// Input arbitration (spec P4, §7.4, §8 "native / own-the-event-source").
//
// Subclasses the game window's procedure and swallows CLAIMED chords before the engine's input
// pipeline sees them — the one chokepoint through which every keyboard path into the game runs.
// UE3 pumps window messages on the game thread, so the window procedure, the ProcessEvent tick,
// and the event queue here all execute on one thread; the mutex below is a belt-and-braces guard,
// not a design requirement.
//
// Claiming vs consuming are two computations (spec §7.4): a claim's activeWhen is a PURE function
// of current state, evaluated inside the window procedure at message time; the handler runs later,
// from the game tick, and may well decide the chord did nothing. The two may disagree at hard
// edges — that is correct.

#include <cstdint>
#include <functional>

namespace InputArbiter {

enum Mods : uint8_t {
    ModNone  = 0,
    ModCtrl  = 1,
    ModShift = 2,
    ModAlt   = 4,
};

struct KeyEvent {
    uint8_t vk = 0;      // Windows virtual-key code
    uint8_t mods = 0;    // Mods bitmask at message time
    bool down = false;   // key-down (else the release of a swallowed down)
    bool repeat = false; // auto-repeat key-down (bit 30 of lParam)
};

/// The layer owns (vk, mods) while activeWhen() is true. Claims are append-only and matched in
/// registration order; exact modifier-mask match (spec P4: claims are per-(key, modifier-mask)).
/// Register claims BEFORE the game hooks go live (during init), or from the game thread — the
/// registry is not locked against the window procedure, which reads it on the game thread.
///
/// A MODIFIER KEY may be claimed too — vk = VK_LSHIFT and so on — which swallows the modifier
/// itself, press and release, so the engine never sees it. That is a heavy hammer (the game
/// stops counting the key as held for its own bindings) and exists for exactly one reason: an
/// engine binding whose RELEASE does something we cannot allow while our chords use that
/// modifier. Its one user was the object scanner's LeftShift claim, retired when the
/// companion's XComInput.ini unbound LeftShift in both layers (docs/input-map.md); the
/// capability stays for the next such binding. Matching strips the key's OWN modifier bit
/// before comparing, so the natural registration is {VK_LSHIFT, ModNone} — GetKeyState already
/// reports Shift down while delivering Shift's own key-down, and demanding ModShift there would
/// read as a chord with itself. Observers are NOT notified for modifier keys, claimed or not.
struct Claim {
    uint8_t vk = 0;
    uint8_t mods = ModNone;

    /// Pure predicate over current state — no side effects, cheap, callable from the window
    /// procedure on any message. Null = always active.
    std::function<bool()> activeWhen;

    /// Dispatched from the game tick for the claimed key's downs, repeats, and releases. A
    /// swallowed key-down's release is always delivered (and swallowed), even if the claim went
    /// inactive in between — no unpaired key-up ever reaches the game or the handler set.
    std::function<void(const KeyEvent&)> onKey;

    const char* description = "";

    /// The game receives the key AS WELL: the handler runs from the game tick like any other,
    /// but nothing is swallowed. For a VANILLA key whose meaning we keep and merely add words
    /// to — Left Alt raises the game's target-preview strip, and the tile cursor speaks it.
    /// Presses only, no repeats and no release: the arbiter never latches a key it did not take
    /// (the game saw the down, so the game owns the up). ⛔ Reach for this, never a swallow,
    /// on a modifier the game's own `bIgnoreAlt`-style suppressors depend on — hiding Alt from
    /// the engine would make Alt+F4 fire the bare F4 binding and skip ALT_F4_QUIT.
    bool passThrough = false;
};

void RegisterClaim(Claim claim);

/// Passive observation of the keyboard traffic the chokepoint sees — claimed or not — which
/// NEVER changes what the game receives. This exists for the one job a claim cannot do: echoing
/// what the player types into a Flash text field, where the keys must reach the field AND be
/// spoken. Claiming them would stop the typing; not seeing them would leave the player deaf to
/// their own input.
///
/// ⛔ Observers run INSIDE the window procedure. An observer may do nothing but RECORD — no
/// engine memory, no speech, no outbound calls — exactly the rule a claim's activeWhen follows.
/// Act on what was recorded from the game tick.
struct Observer {
    /// Every non-modifier key-DOWN, repeats included (a held Backspace really does delete
    /// repeatedly). Fires whether or not a claim swallowed the key.
    std::function<void(const KeyEvent&)> onKey;

    /// Every character the game is about to receive, in UTF-16 code units. Not called for
    /// characters belonging to a swallowed chord — those never reach the game either, so
    /// echoing them would describe an edit that is not happening.
    std::function<void(wchar_t)> onChar;

    const char* description = "";
};

/// Register BEFORE the game hooks go live, or from the game thread — same contract as claims.
void RegisterObserver(Observer observer);

/// The pure claim predicate the chokepoint evaluates. True when some registered claim with an
/// exact (vk, mods) match is currently active.
bool IsClaimed(uint8_t vk, uint8_t mods);

#ifdef X2ACCESS_DEVSERVER
/// Dev harness only: push an event into the queue as though the window procedure had claimed
/// it, so a test can drive OUR layer without the game window being focused (or even foreground).
/// Returns false when no active claim matches, which is the honest answer — the key would have
/// gone to the game.
///
/// GAME THREAD ONLY, for the same reason the real chokepoint is: `activeWhen` is evaluated here,
/// and the contract says it is evaluated at message time on the game thread. Note what this
/// does NOT do — it never reaches the engine's own input, so it tests the layer, not the game.
bool InjectSynthetic(const KeyEvent& e);
#endif

/// Game-tick pump: installs the window subclass once the game window exists (and re-installs if
/// the window is recreated), then drains queued key events into their claims' handlers. Safe to
/// call many times per frame — cheap and idempotent.
void OnTick();

/// Restore the original window procedure (only if ours is still the current one).
void Shutdown();

bool IsInstalled();

} // namespace InputArbiter
