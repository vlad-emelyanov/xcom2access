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
};

void RegisterClaim(Claim claim);

/// The pure claim predicate the chokepoint evaluates. True when some registered claim with an
/// exact (vk, mods) match is currently active.
bool IsClaimed(uint8_t vk, uint8_t mods);

/// Game-tick pump: installs the window subclass once the game window exists (and re-installs if
/// the window is recreated), then drains queued key events into their claims' handlers. Safe to
/// call many times per frame — cheap and idempotent.
void OnTick();

/// Restore the original window procedure (only if ours is still the current one).
void Shutdown();

bool IsInstalled();

} // namespace InputArbiter
