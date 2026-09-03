#pragma once

// The resource strip's HOTKEY — `R`, anywhere the game is drawing the strip.
//
// The strip itself is read by GameUI::ReadResourceStrip (game_ui_avenger.cpp) and appears as a
// trailing Tab stop in every recipe whose screen shows it (Recipes::AddResourceStop). This module
// is the OTHER half of that split, and the split is the point:
//
//   * the STOP is parity — arrow across nine resources one at a time, hear the one you came for,
//     exactly as a sighted player's eye picks one figure out of the band;
//   * the KEY is the aid — one press, the whole strip, from wherever the player happens to be,
//     including screens that have no recipe at all.
//
// ⛔ That second clause is why the key is not redundant with the stop. The ANT FARM has no recipe
// (it is the documented narrate-only exception — the room name is set inside `if( bIsFocused )`,
// so our own focus model would leave every room nameless), and the ant farm is the screen showing
// the WIDEST strip in the game: all nine resources plus the unstaffed-engineer icons. Without the
// key, the one screen with the most to say about resources would be the one screen that could not
// say it.
//
// ⚠ `R` is free in the strategy layer and NOT free in tactical, where it is Reload — the bare
// binding at DefaultInput.ini:354 sits inside `[XComGame.XComTacticalInput]`. Nothing needs doing
// about that: the claim is gated on the strip being ON SCREEN, and the strip exists only in the
// strategy layer. `[XComGame.XComHeadquartersInput]` binds `R` only with Shift held
// (`ErrorReport`, which `[Engine.PlayerInput]` also binds the same way), and UE3's lenient
// modifier matching REQUIRES a modifier a binding declares — so a bare `R` there matches nothing
// and this claim displaces nothing. See docs/input-map.md for the census this follows.
//
// ⛔ The same gate is what keeps this away from the save-name field. Typing a save name happens on
// `UISaveGame`, which falls to `UpdateResources`' `default:` and hides the strip — so `R` is not
// claimed there and reaches the text field, where TextInput's observer echoes it. That gate is the
// GAME's own answer rather than an exclusion list of ours to maintain.

namespace ResourceStrip {

/// Register the read-the-strip claim. Call during init, BEFORE the game hooks go live.
void Init();

/// Game-tick pump: recompute the claim's gate, and repair a capture that has nothing for a strip
/// the player can see. Throttled internally. Game thread only (it can make one outbound
/// ProcessEvent call).
void OnTick();

} // namespace ResourceStrip
