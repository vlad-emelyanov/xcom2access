#pragma once

// Free aim — the third tactical input mode, alongside world mode (tile cursor) and ordinary
// targeting (target cycling).
//
// An ability is free-aiming when its target is a POINT rather than an entry in a target list:
// AvailableAction.bFreeAim is set, AvailableTargets is empty, and the aim point comes from the
// game's own XCom3DCursor. Grenades, cones, lines and the blaster launcher all work this way.
// Vanilla offers NO keyboard path to move that point — UITacticalHUD_AbilityContainer's aiming
// branch is gated on `ISCONTROLLERACTIVE`, so on mouse+keyboard an arrow press cycles the
// ability instead, abandoning the throw.
//
// This module owns only the KEYS and the mouse gate. Detection and every spoken word live in the
// companion's X2AccessTacticalReader — the same class that owns the ordinary shot readout,
// because free aim is a mode of the shot HUD rather than a separate screen. That is also why the
// active flag arrives from outside: TacticalHUD's existing 150 ms poll reads it off the
// companion's NativeState and calls SetActive here, so the whole layer costs one outbound call.

namespace FreeAim {

/// Register key claims. Call during init, BEFORE the game hooks go live.
void Init();

/// The free-aim gate, as last published by TacticalHUD's poll. The object scanner shares it
/// rather than recomputing, exactly as it shares TileReader::IsActive() for world mode — the two
/// are mutually exclusive by construction, since the tile cursor stands down whenever the shot
/// HUD is raised.
bool IsActive();

/// Publish the gate. Called from TacticalHUD::OnTick with what the companion reported; also
/// drives the free-aim mouse gate's release. Game thread only.
void SetActive(bool on);

/// Move the aim point onto the object scanner's current selection and speak the result — the
/// scanner's action verb while free-aiming, where world mode plants the tile cursor instead.
/// Lives here rather than in the scanner so the mouse gate is armed by the same code path as
/// every other aim move.
void AimAtMarker();

} // namespace FreeAim
