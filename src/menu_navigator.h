#pragma once

// The navigator, v1 (spec §7): wires claimed keys to kernel operations and owns all speech for
// graph-driven screens. Hosts the screen recipes — the main menu (UIFinalShell) and the
// settings screen (UIOptionsPCScreen: tab strip, typed setting rows with Left/Right value
// adjust and a live value watch, bottom buttons) — probing for the topmost ready screen and
// attaching one recipe at a time.

namespace MenuNavigator {

/// Register key claims and install the announcer's host hooks. Call during init, BEFORE the
/// game hooks go live (claims are registered into the arbiter).
void Init();

/// Game-tick pump: poll screen activity (attach/detach), run the frame differ. Throttled
/// internally — safe to call many times per frame.
void OnTick();

/// True while a recipe is attached — INCLUDING the ambient tactical recipe, which is attached
/// for a whole mission while claiming nothing. Callers that mean "may I read this surface" or
/// "is the graph taking the keys" want OwnsInput() or MenuAttached() below; this stays the raw
/// attach bit for diagnostics and for surfaces the ambient recipe can never share a screen with.
bool IsAttached();

/// A recipe is attached AND focused — the graph's claims are live and the arrows, Tab, Enter and
/// Space belong to it. For every menu recipe this equals IsAttached(); the ambient tactical
/// recipe attaches unfocused and only owns input after Tab. This is the gate the tile
/// cursor and the legacy X2Access_Speak mute key off: a graph that is not consuming keys and not
/// speaking is not "owning the surface", however long it stays attached.
bool OwnsInput();

/// A NON-AMBIENT recipe is attached — a menu or modal took the surface outright (pause menu,
/// dialog, hacking never comes here). The tactical layer's own producers (the targeting poll,
/// free aim) gate on this rather than IsAttached(), because the ambient tactical recipe being
/// attached is their NORMAL state, not a takeover.
bool MenuAttached();

/// The attached recipe's log name ("main menu", "settings", …), or null when detached.
/// Diagnostics: IsAttached() alone cannot tell "the right recipe took the screen" from "a recipe
/// whose probe matched too eagerly did".
const char* AttachedRecipeName();

} // namespace MenuNavigator
