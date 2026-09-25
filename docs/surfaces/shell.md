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
- ⛔ `X2AccessSaveReader.uc` uses `Chr(10)` rather than `"\n"` for the separator the engine wrote
  into `Header.Description`, for the same reason `X2AccessNarrator`'s separators are printable.

## Difficulty and Second Wave

`UIShellDifficulty` — the new-campaign flow's difficulty page and its Second Wave option rows,
read by the shell recipe, with `UIScreenListener_Difficulty` fixing the rows up.

- ⛔ **A widget's declared `Type` does not tell you what the widget IS; the visible child does.**
  Every Second Wave row reads `UIMechaListItem.Type` = `Description` while showing a live, visible
  `UICheckbox` — the same object whose `bChecked` the campaign start reads in
  `GetSelectedSecondWaveOptionNames`. `ReadOptionRowRaw` only looked at the checkbox inside its
  `Type == Checkbox` branch, so those rows reported "unchecked" forever. ⚠ That `Description` is
  OUR OWN companion's write, not the engine's: the GAME types those rows `Checkbox`
  (`UIShellDifficulty.uc:472` sets it, `UIMechaListItem.UpdateDataCheckbox:549` sets it again), and
  `UIScreenListener_Difficulty.FixSecondWaveItems` retypes them so Enter stops being eaten by the
  game's own `bReadOnly` checkbox (`UIShellDifficulty.uc:482`, `UIMechaListItem.uc:186-190`). The
  rows that listener does not loop over — the screen's own bottom rows — still read `Type` =
  Checkbox, and that asymmetry was the tell all along. The durable half is the game's and survives
  the correction: `SetWidgetType` hides the widgets it switches away from but leaves their pointers
  behind, so presence alone is not enough, and a screen can hold both kinds at once.
- ⛔ **Prove which HALF is broken before fixing either.** The same bug cost a full cycle spent on
  the write path — reading the game's source for `OnSecondWaveOptionClicked`, ruling out the param
  layout, regenerating the whole SDK, adding a verify-and-fall-back — because the readback said
  "unchecked" and the readback was the broken thing. The toggle had been landing correctly all
  along; one screenshot settled it in seconds.

## Pause menu

`UIPauseMenu`, read by the shell recipe. Activation sends Enter to the LIST rather than calling
`OnChildClicked` by name: mods interpose by wrapping a list's `OnItemClicked` /
`OnItemDoubleClicked` delegates (Remove Missing Mods dispatches on the row's `Tag` there), so the
named call activated every vanilla row and silently skipped every mod-added one. The game's path
means the INPUT path, not the handler at its end.

## Character pool

`UICharacterPool` and its import / export picker `UICharacterPool_ListPools` — the `character
pool` and `pool files` recipes, native only. Built 2026-09-04 from source (`../customize-recon.md`
§3); **not yet run live**. The editor they open is the customization family's recipe
(`strategy.md`, "Soldier customization").

- ⛔ **Both screens are keyboard dead ends in vanilla, in different ways.** On the pool, Enter
  toggles a row's checkbox and EDIT — the reason the screen exists — is a gamepad button
  (`FXS_BUTTON_X` → `EditSoldier`) with no keyboard equivalent. On the picker, `OnAccept` picks
  only under `ISCONTROLLERACTIVE`, and Enter sent to the list is consumed by the row (a value row
  clicks nothing; a button row presses its DELETE, whose handler looks the BUTTON up as a list
  item and finds nothing). So EDIT is re-provided (Backspace on a row, and a button in the
  actions stop), and the picker's Enter calls `OnClickLocal(List, index)` — the exact call the
  controller branch makes with the selection.
- **Backspace deletes a pool only where the game draws a DELETE button.** The game's own Y
  button has no such gate and, in export mode, would delete `EnumeratedFilenames[-1]` from the
  CREATE NEW POOL row. The verb is `FXS_BUTTON_Y` after selecting, so the game's own confirm
  dialog follows.
- The list keeps its selection under the action buttons (write-back onto a button does not
  clear it): EDIT CHARACTER as a button acts on the row last visited.
- The pool is `Hide()`n for up to two seconds after the push while the camera flies in
  (`ForceShow`), and `OnCancel` animates out for three seconds before it pops. The recipe attaches
  and detaches on focus, not on visibility, so it may speak into both windows; the words are
  right either way.

## Tactical Legacy Pack hub

`UITLEHub` — the `legacy hub` recipe, native words plus `X2AccessTLEReader.DescribeHub` for the
numbers. Built 2026-09-17 from source after a player found the screen silent, and heard the same
evening: the attach, the three cards with their figures, and Space on a description all read as
designed. The three screens its cards open have recipes of their own below; their in-mission
and post-mission screens are `tactical.md`'s.

- **One stop, three cards, each a button.** Title, the game's own NEW tag where the profile
  records no play of that mode, the card's statistics as the value, LAUNCH as the value's tail,
  and the description sentence on Space. Enter clicks the card's button, which runs the game's
  own delegate (`EnterLadderMode` / `EnterSkirmishMode` / `EnterChallengeMode`).
- ⛔ **The three buttons carry no `Text`.** `InitButton('Button0', , delegate)` hands them no
  label — the LAUNCH word reaches Flash through `SetButtonInfo` — so `ReadButtonField`, which
  treats an empty label as "no button here", cannot read them. `ReadTLEHub` takes the pointer
  and the panel focus bit directly. A second screen built the same way (the TLE mode menus are
  candidates) needs the same read, not a "fix" to `ReadButtonField`.
- **Spawn order is not screen order.** The class spawns Challenge, Ladder, Skirmish and the
  game's Navigator walks them in that order; the screen draws Ladder, Skirmish, Challenge. The
  recipe reads the button fields in screen order and writes focus back onto the pointer, so the
  game's own order never shows.
- **Every number is the profile's, not the screen's.** `OnInit` reads
  `XComOnlineProfileSettingsDataBlob.HubStats` once, pushes the figures into Flash and keeps
  nothing; the companion re-runs the same arithmetic over the same object, hard-coded
  denominators included (28 narrative missions, 109 challenges), and the NEW tag's condition
  with it. The ladder card's meter label is `UITLE_LadderModeMenu.m_NarrativeLaddersHeader` —
  the one word on the screen that lives on another class — so it travels with its number.
- The completion meters are rings with no figure on them: the count is spoken with its label,
  then the percentage the ring was filled to.

## Legacy Operations menu, its difficulty page, the soldier loadout popup

`UITLE_LadderModeMenu`, `UITLE_LadderDifficulty`, `UITLEUnitStats` — the `legacy operations`,
`ladder difficulty` and `soldier loadout` recipes in `recipe_tle.cpp`, read by `game_ui_tle.cpp`,
with `X2AccessTLEReader` for the words the widgets do not retain. Built 2026-09-17 from source
plus one `dev-props` read of the live ladder menu; **not yet run live**.

- **The ladder menu is two lists in one column and a panel that follows the selection.** The
  recipe reads both lists as one stop under their two headers, the panel as a `details` stop
  under the selected ladder's name, the squad as a stop of its own, and CONTINUE / BEGIN,
  ABANDON and DELETE as an `actions` row. Enter on a row is the screen's own `OnLadderClicked`
  (load the save, or push the difficulty page); ABANDON and DELETE are its own popup functions,
  each raising its confirm dialog.
- ⛔ **The panel's words live in the HISTORY, and only while it matches the selection.**
  `UpdateData` reads a ladder history file into the history and pushes from the state objects it
  finds there — so the history IS the screen's cache of the selected ladder, and the companion
  reads those objects, never the file. But the "[+] generate" row blanks the panel WITHOUT
  reloading, so the companion checks that the `LadderProgress` in the history carries the index
  the screen's own `ClickedList` + `clickedIndex` resolve to, and emits no panel otherwise. A
  panel that reads the previous ladder's facts under the generate row is that check missing.
- **Focus write-back is the screen's own select-and-refresh, not a list index.** Two lists,
  so `SetListIndex` cannot name the row; the row's `SetSelectedNavigation` walks the game's
  navigator chain, the other list is deselected, and `OnSelectedChange` — what the screen's own
  arrow handler calls after moving — rebuilds the panel. The wire cache takes the selection as
  its `extra` key because the panel is the selection's shadow.
- **Vanilla keyboard here is a dead end for three verbs**: ABANDON and DELETE are Flash
  mouse buttons or gamepad X / R3, and the six soldier-info buttons are mouse-only (a gamepad's
  Y opens slot 0). All are re-provided through the screen's own functions.
- **The difficulty page** is the campaign difficulty page's shape (radio group, options, LAUNCH)
  in the Legacy Pack's skin; its title and description are `UIShellDifficulty`'s own class
  defaults, which the companion reads so native need not resolve a second class's CDO.
- **The loadout popup consumes every key**, and Left/Right are its Prev/Next soldier — so the
  soldier node is a spinner whose adjust calls the screen's own step functions, and the soldier
  index is part of every node identity so a step re-speaks the node under focus. Gear slots are
  positional on the wire (an empty pocket keeps its place in front of the grenade).

## Skirmish Mode menu

`UITLE_SkirmishModeMenu` — the `skirmish` recipe (`recipe_tle_skirmish.cpp`,
`game_ui_tle_skirmish.cpp`, `X2AccessSkirmishReader.uc`). Built 2026-09-17 from source; **not
yet run live**. The measurement is `../tle-skirmish-recon.md`.

- **One screen, twenty-one states, no sub-screen.** Squad editing, soldier pickers, map,
  mission, biome, enemy and difficulty choice are all states of the same `UIList`; only three
  confirm dialogs are ever pushed over it, and Enter on the base menu opens the map by console
  command. So one recipe: the rows under the state's subtitle as context (a state change respawns
  every row and is heard as a new context), the right-hand panel as read-only facts from the
  companion, and START GAME by pointer and focus bit (a label-less `UILargeButton`, the hub's
  trick again).
- ⛔ **The companion reads the screen's own cached members** (`m_CurrentSquad`,
  `BattleDataState`, `ChallengeMissionSite`, `StartState`, `History`) and never the global
  history, and it runs only the QUERY half of `GetPossibleEnemiesInPod`: the game's
  `UpdateEnemyPortraits` also mutates the mission site, which a reader must never do.
- Focus write-back is `List.SetSelectedIndex`, which fires the screen's `OnSetSelectedIndex`,
  so the panel follows our focus exactly as it follows the game's arrows.

## Challenge Archives

`UITLE_ChallengeModeMenu` — the `challenge archive` recipe (`recipe_tle_challenge.cpp`,
`game_ui_tle_challenge.cpp`, `X2AccessChallengeReader.uc`). Built 2026-09-17 from source; **not
yet run live**. The measurement is `../tle-challenge-recon.md`.

- **One screen, straight to the map.** `OnChallengeClicked` reads the challenge's history file
  and runs the map command; nothing is pushed between Enter and the mission but the LOADING
  progress dialog (the popup recipe's). `UIChallengeLeaderboards` and
  `UIChallengeMode_SquadSelect` are NOT reachable from the offline archive (the online daily
  challenge's path only), which is why they have no recipe here.
- **The rows retain their words** (`UITLEChallenge_ListItem.Data` is the struct `PopulateData`
  pushes), so all five cells are read natively; the archive is a `GraphSheet` under the game's
  own sort headers, 107 rows, sized from the list (that count exceeds the fixed row cap).
- ⛔ **A sort refreshes the rows in place with no respawn**, so a row's identity is its
  `ChallengeListDataIndex` (the challenge's file index), never the widget handle, and the wire
  cache takes the sort state as its `extra` key. Identity by handle would keep the cursor on a
  row SLOT across a sort instead of on the same challenge.

## Text-entry dialog

`UIInputDialogue` — naming a new save, and the error reporter. The player types into a **Flash
TextField**, which no screen reader can see, so nothing echoes their own keystrokes back.

- ⛔ It is NOT a graph recipe and leaves editing keys to the field: a recipe attaching turns on the navigator's
  arrows, Enter, Space, Tab, Home and End, every one of which the player is using to type or
  answer — and a read-back key would answer the wrong question anyway, since what the player needs
  is the echo of the key just pressed. It **observes and reads**:
  `InputArbiter::RegisterObserver` records that a key happened, and the field itself answers what
  it did.
- **Multiline confirmation is Ctrl+Enter.** The game deliberately uses Enter for a new line and
  ignores Tab in `UIInputDialogue`. The scoped claim calls the visible Confirm button's
  `OnMouseAccept`, which reads the Flash field and runs its normal accept callback. Single-line
  dialogs keep their own Enter path; every other editing key passes through.
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

## Settings (`UIOptionsPCScreen`)

- ⛔ **Settings never clears its focused bit, so its probe tests the STACK POSITION, not the
  bit.** `UIOptionsPCScreen.OnLoseFocus` overrides `UIScreen`'s without calling `super` — it
  `Hide()`s and clears the nav help and leaves `bIsFocused` set (SrcOrig
  `UIOptionsPCScreen.uc`, `OnLoseFocus`). Live 2026-09-17: Mod Config Menu pushed over Settings
  read `0x279 inited focused visible` on top and Settings read `0x259 inited focused` two entries
  down, `ProbeFocused` kept passing, and because the navigator re-probes only the ATTACHED recipe
  the screen on top was never probed at all — silence on MOD SETTINGS. `ProbeOptions` now
  requires `GameUI::IsTopmostScreen` (topmost live stack entry, mouse guards skipped) as well;
  anything pushed over Settings — Mod Config Menu, key bindings, credits — inherits the fix.
  **The general form:** a recipe for a screen the game pushes other screens OVER must yield by
  its own hand, and the focused bit is not the hand to trust; the stack is the game's own answer.
- The injected **MOD SETTINGS** button (and any other mod's) is found by walking the screen's
  `ChildPanels` for a visible labelled `UIButton` whose `ParentPanel` IS the screen. ⛔ A
  screen's `ChildPanels` is a flat REGISTRY of every panel on it (`UIPanel.uc`: every panel calls
  `Screen.AddChild(self)`), not its child list — without the parent test the Key Bindings row's
  embedded button became a fifth bottom button (`mod-config-menu-recon.md` §2).
