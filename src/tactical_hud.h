#pragma once

// Shot-HUD narration — the tactical layer's targeting half (plan phase 1).
//
// While UITacticalHUD.m_isMenuRaised, the GAME owns the arrows, Tab, Shift and F1-F8: raising
// a shot is the one tactical mode where every steering key is already spoken for, and the layer
// claims nothing that steers. What it does instead is watch the selection the game maintains
// and say what changed — which ability, which target, at what odds — plus two on-demand keys
// that never move anything.
//
// Division of labor is the tile reader's, for the reason CLAUDE.md records: the tactical HUD
// retains no text (its panels are write-only pipes into Flash), so the widget tree supplies
// STRUCTURE and SELECTION while the companion CDO X2AccessTacticalReader supplies the WORDS,
// re-resolved from XCOMHISTORY. C++ owns the gate, the keys and the speech.
//
// Deliberately NOT a Graph recipe: nothing here is navigable. The navigable HUD (ability bar,
// enemy strip, objectives as tab stops) is a later phase and will attach unfocused so the tile
// cursor keeps the arrows.

namespace TacticalHUD {

/// Register key claims. Call during init, BEFORE the game hooks go live.
void Init();

/// Game-tick pump: refresh the targeting gate and run the change poll. Throttled internally —
/// safe to call many times per frame. Game thread only (it makes outbound ProcessEvent calls).
void OnTick();

/// Is the tactical HUD usable at all — mission running, nothing modal over it, no recipe
/// attached — whether or not the shot HUD is currently raised? Wider than the targeting gate the
/// claims above use, and shared rather than recomputed for the reason TileReader::IsActive() is:
/// two surfaces that answer this question separately will eventually answer it differently.
/// Reflects the last OnTick, so call it after this module's tick within a frame.
bool IsUsable();

/// Is a mission running at all — i.e. is UITacticalHUD on the screen stack? Unlike IsUsable()
/// this stays true under a pause menu, a dialogue box or an attached recipe, which is what makes
/// it the right edge for per-mission state: it rises once when the mission starts.
bool IsMissionActive();

} // namespace TacticalHUD
