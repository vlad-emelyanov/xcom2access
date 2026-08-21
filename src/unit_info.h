#pragma once

// The selected unit — the tactical layer's "who am I controlling, and in what shape".
//
// Two surfaces over one subject, and they divide the way every other pair in this project does:
// an AUTOMATIC line when the thing changes, and an ON-DEMAND key that always answers.
//
//   * Switching soldiers speaks the new one. Detected by POLLING the tactical controller rather
//     than by claiming Tab — selecting a soldier submits no game state and fires no event, so
//     there is nothing to listen to, and watching the selection leaves the vanilla Tab binding
//     exactly where the player's config put it. It also catches every other way the selection
//     moves, which a Tab claim never could: clicking a soldier, the start of your turn, and the
//     game re-selecting for you when the unit you were on goes down.
//   * Semicolon reads the whole block on demand — class, health, shield, armour, action points,
//     concealment and the named buffs and debuffs.
//
// Division of labor is the tile reader's: C++ owns the gate, the key and the speech; every word
// comes from the companion CDO X2AccessUnitReader, because the tactical HUD retains no text (its
// panels and the unit flags are write-only pipes into Flash). See that class for the per-value
// parity notes and for why action points are a friendly-only readout.

namespace UnitInfo {

/// Register key claims. Call during init, BEFORE the game hooks go live.
void Init();

/// Game-tick pump: the selection-change poll, plus the once-per-mission reset behind it.
/// Throttled internally — safe to call many times per frame. Game thread only (it makes
/// outbound ProcessEvent calls). Must run AFTER TacticalHUD::OnTick, whose gates it reads.
void OnTick();

} // namespace UnitInfo
