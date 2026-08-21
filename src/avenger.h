#pragma once

// The Avenger's ant farm (UIFacilityGrid) — phase 1 of the strategy layer.
//
// This is the first surface in the project where the game already HAS the keyboard navigation
// and merely never turns it on: UIFacilityGrid.InitScreen builds its whole eight-way room
// adjacency graph inside `if( `ISCONTROLLERACTIVE)`, and that macro is not a hardware check but
// the 2D movie's mouse-active flag. So this layer is narrate-only — the companion calls the
// gated function once per grid instance and then watches Navigator.SelectedIndex, while C++
// owns nothing but the gate, one re-announce key and the speech.
//
// That is why there is no Graph recipe here and no arrow claim: the game's own UINavigator does
// the spatial navigation correctly the moment the graph exists (confirmed live 2026-08-20 —
// six arrow steps matching the hardcoded graph exactly), and it brings the camera swoop, the
// focus delivery and the highlight box with it. Claiming the arrows would displace all three.
//
// Recon and the four source facts behind the ungate: docs/avenger-view-recon.md §6.1.

namespace Avenger {

/// Register key claims. Call during init, BEFORE the game hooks go live.
void Init();

/// Game-tick pump: refresh the gate and run the change poll. Throttled internally — safe to
/// call many times per frame. Game thread only (it makes outbound ProcessEvent calls).
void OnTick();

} // namespace Avenger
