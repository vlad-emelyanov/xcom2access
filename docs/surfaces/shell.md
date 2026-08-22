# Shell surfaces

Implementation rules for the menus outside a campaign — saving, loading, and typing into a Flash text field. Each ⛔ was earned by a live bug; each ⚠ is a
known limit or an open decision. **Read the section for a surface before editing
it** — `CLAUDE.md`'s Surfaces table points here for exactly that reason.

Cross-cutting rules (buffer sizing, the screen-accept trap, the empty-field trap,
speech joining, parity) live in `CLAUDE.md`; only what is specific to these
screens is here.

## Save / load

`UILoadGame` and `UISaveGame` are one screen twice over — the same three fields at the same
offsets (`m_iCurrentSelection` 0x410, `m_arrListItems` 0x424) and the same row widget — and the
SHELL's first case of the tactical HUD's problem: `UISaveLoadGameListItem.UpdateData` resolves
date, name and mission into LOCALS, pushes them into Flash and retains only the `OnlineSaveGame`
struct it built them from. So native reads structure and the companion re-derives the words from
that struct, mirroring `UpdateData` line for line — with no `XCOMHISTORY` and no `GetSaveGames`
re-query, because the screen already asked. The 12-hour conversion is a NATIVE function
(`FormatTimeStampFor12HourClock`), which is why the formatting is not done in C++: reproducing it
would say *14:22* to a player whose screen says *2:22 PM*. Nothing here is un-ungated, so this is
a reader, not a rescue — deliberately the thinnest recipe in the file.

- **ONE NODE PER ROW**: the row shows two buttons, but a second column would replace the one
  number a blind player cannot get any other way — *save 4 of 12* — with *1 of 2*, because
  `GraphBuilder` stamps positions WITHIN a row holding more than one item.
- ⛔ The layer must NOT claim `Delete` here: every other Delete claim in the project (tile cursor,
  shot HUD, hacking pad, ant farm) stands down while this screen is up, so the key is free and it
  is the screen's own delete verb — named once on attach in the row's own localized word, since
  the game's nav help offers nothing but Back on mouse+keyboard.
- **Activation goes through the row's own `AcceptButton.Click()`**, which dispatches
  new-save-rename versus overwrite for free, **after** setting the selection — load-bearing rather
  than tidy: `UISaveGame.OnAccept` and `OnDelete` ignore the control they are handed and act on
  `m_iCurrentSelection`, so a click sent while the two disagreed would overwrite or delete the
  WRONG SAVE.
- Words are cached on the ROW POINTER VECTOR (the navigator re-renders ten times a second and the
  save screen's new-save row re-runs `FillInHeaderForSave`, which walks the history): a save list
  changes only through `BuildMenu()`, which respawns every row panel, so a changed pointer list is
  an exact, self-invalidating cache key.
- **Reading ORDER is not the row's order**: the row draws the wall-clock timestamp at the head of
  its first line, and speech puts it LAST, after description, game type and mission. A speech
  decision rather than a parity one — a sighted player takes the row in at a glance, while speech
  is serial, and a list of autosaves differs in its description, not the minute it was written.
  The companion emits `time` and `desc` as separate fields for exactly this.

## Text-entry dialog

`UIInputDialogue` — naming a new save, and the error reporter. The player types into a **Flash
TextField**, which no screen reader can see, so nothing echoes their own keystrokes back.

- ⛔ It is NOT a graph recipe and it claims NOTHING: a recipe attaching turns on the navigator's
  arrows, Enter, Space, Tab, Home and End, every one of which the player is using to type or
  answer — and a read-back key would answer the wrong question anyway, since what the player needs
  is the echo of the key just pressed. It **observes and reads**:
  `InputArbiter::RegisterObserver` records that a key happened, and the field itself answers what
  it did.
- **The Flash side is READABLE** (measured; `docs/save-load-recon.md` §7.1 has the table): AS2's
  global `Selection` class resolves through `ActionScriptInt` **with or without the `_global.`
  prefix**, so `Selection.getCaretIndex()` / `getBeginIndex()` / `getEndIndex()` give an EXACT
  caret and selection, `Selection.getFocus()` names the focused field as a full AS path, and
  `GetVariableString("<path>.text")` reads the contents. `GameUI::ReadFocusedTextField` is all
  four in one call. ⚠ A path that does not resolve answers **0** while the real accessors answer
  **-1** when nothing is focused — that is how "it worked" was told from "it silently did
  nothing".
- It costs two leaked `FString`s per read — the `ConsoleCommand` bargain — so it runs behind a
  keystroke and on a 500 ms heartbeat (which catches a mouse click placing the caret), never
  free-running. Spoken: the prompt plus starting text on arrival, and *selected* with it when the
  whole prefill is selected — which it IS, meaning the next keystroke replaces the name and no
  caret model could have known. Then per keystroke: the inserted run, what Backspace removed, the
  newly selected text, or the character the caret landed on. A key the field REFUSED changes
  nothing and stays silent. `getFocus` earns its place beyond the caret — it says WHICH field,
  which the crash-report dialog (title box and body box with Tab between) needs and "the text"
  cannot answer.
