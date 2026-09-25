#pragma once

// The text-entry dialog (UIInputDialogue) — naming a new save, and the error reporter.
//
// ⛔ NOT a graph recipe, and that is the whole design. A recipe attaching turns on the
// navigator's claims — arrows, Enter, Space, Tab, Home, End — and on this screen every one of
// those is a key the player is using to TYPE or to answer. The one exception is Ctrl+Enter in
// a multiline dialog: the game gives it no meaning and offers no keyboard Confirm action.
//
// What it does instead is WATCH. The field is a Flash TextField, so no screen reader can see it
// and nothing echoes the player's own keystrokes back to them. This module registers a passive
// observer on the input arbiter — the messages still reach the game untouched — and speaks the
// prompt when the dialog appears, each character as it is typed, whatever a Backspace removed,
// and the character the caret lands on when the arrows move it.
//
// The Flash Selection object supplies the actual caret and selection on every read.
//
// See docs/save-load-recon.md §7.

namespace TextInput {

/// Register the passive keyboard observer and the multiline-only Ctrl+Enter confirm claim.
/// Call during init, BEFORE the game hooks go live. Ordinary editing keys reach the field.
void Init();

/// Game-tick pump: the appearance poll. Throttled internally — safe to call many times per
/// frame. Game thread only.
void OnTick();

} // namespace TextInput
