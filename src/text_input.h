#pragma once

// The text-entry dialog (UIInputDialogue) — naming a new save, and the error reporter.
//
// ⛔ NOT a graph recipe, and that is the whole design. A recipe attaching turns on the
// navigator's claims — arrows, Enter, Space, Tab, Home, End — and on this screen every one of
// those is a key the player is using to TYPE or to answer. For the same reason this module
// claims NOTHING: every key here belongs to the field.
//
// What it does instead is WATCH. The field is a Flash TextField, so no screen reader can see it
// and nothing echoes the player's own keystrokes back to them. This module registers a passive
// observer on the input arbiter — the messages still reach the game untouched — and speaks the
// prompt when the dialog appears, each character as it is typed, whatever a Backspace removed,
// and the character the caret lands on when the arrows move it.
//
// ⚠ The caret is a MODEL, not a reading: Flash does not expose the caret index and
// UIInputDialogue offers only GetInputText. Every edit re-anchors it exactly (the text diff says
// where the change happened), and the keys it cannot follow — Shift-selection, Ctrl word jumps,
// a mouse click in the field — move it not at all rather than move it wrongly.
//
// See docs/save-load-recon.md §7.

namespace TextInput {

/// Register the passive keyboard observer. Call during init, BEFORE the game hooks go live.
/// This registers NO claims — every key on this screen belongs to the text field; the observer
/// only watches the traffic on its way past, so the field still receives everything.
void Init();

/// Game-tick pump: the appearance poll. Throttled internally — safe to call many times per
/// frame. Game thread only.
void OnTick();

} // namespace TextInput
