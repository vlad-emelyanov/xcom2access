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

/// True while a graph screen owns the surface. The legacy UnrealScript narration gate (spec
/// §10.5): while true, X2Access_Speak events from the companion mod are dropped.
bool IsAttached();

} // namespace MenuNavigator
