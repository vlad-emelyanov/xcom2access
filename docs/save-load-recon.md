# Save / load recon — UILoadGame, UISaveGame, UISaveLoadGameListItem

Sources: `SrcOrig/XComGame/Classes/UILoadGame.uc` (927 lines), `UISaveGame.uc`
(844), `UISaveLoadGameListItem.uc` (441), `UIInputDialogue.uc`,
`Engine/Classes/OnlineSubsystem.uc` (the save structs), and
`XComOnlineEventMgr.FillInHeaderForSave`. Offsets from the regenerated SDK's
dump comments. **VERIFIED LIVE 2026-08-21**: the load screen read its whole list
correctly on the first run, Backspace deletions rebuilt and re-announced it each
time, and the Flash probe in §7.1 came back with an exact caret and selection.

## 1. What the player is looking at

Two screens, structurally the same and built from the same row widget:

| | `UILoadGame` | `UISaveGame` |
|---|---|---|
| reached from | main menu LOAD GAME, pause menu | pause menu SAVE GAME (never the shell) |
| rows | one per save, newest first | **row 0 = new save**, then one per save |
| Enter | `OnAccept` → load (via confirmations) | row 0 → `OnRename`; else `OnAccept` → overwrite confirm |
| Delete / Backspace | `OnDelete` → confirm → delete | same, refused on row 0 |
| Escape | `OnCancel` → pop | same |
| in flight | `m_bLoadInProgress` | `m_SaveStage != SaveStage_None` |

Both sort by timestamp (`SortSavedGameListByTimestamp`) and both filter the
list through `UILoadGame.FilterSaveGameList` (other-language saves, ladders).

## 2. The keyboard already works — which decides the whole design

`UILoadGame.OnUnrealCommand` and `UISaveGame.OnUnrealCommand` handle
`FXS_ARROW_UP` / `FXS_ARROW_DOWN` (and W/S) with **no controller gate**, and
`RawInputHandler` — installed as `XComInputBase.RawInputListener` in
`InitScreen` — handles `Delete` and `BackSpace`. So unlike the Avenger's ant
farm, nothing here is un-ungated: a sighted keyboard player can already walk
the list and delete a save. What is missing is only that **the rows say
nothing**, so this layer is a reader, not a rescue.

Two consequences:

- **The graph is the LIST and nothing else.** One node per row. A second
  column for the DELETE button would replace the one number a blind player
  cannot get any other way — *save 4 of 12* — with *1 of 2*: `GraphBuilder`
  stamps positions **within** a row when a row holds more than one item, and
  across single-item rows otherwise. Delete stays on the game's own key, and
  the affordance is named once on attach, in the row's OWN localized button
  words.
- **⛔ Do not claim `Delete` here.** `TileReader`, `TacticalHUD`,
  `HackingScreen` and `Avenger` all claim it, and every one of those gates
  stands down while this screen is on the stack (it blocks the tactical gate,
  and it is not `UIFacilityGrid`), so the key is free and must stay free — it
  is the screen's own delete verb.

## 3. The rows hold no words (the tactical-HUD pattern, in the shell)

`UISaveLoadGameListItem.UpdateData` resolves date, name and mission into
**locals**, pushes them into Flash through one `Invoke("updateData", …)` and
retains none of them. What it DOES retain is the `OnlineSaveGame SaveGame`
struct it was built from (+0x3F0) — literally the data Flash was told. So the
hard rule applies in its strongest form: the words come from the row's own
struct, re-formatted exactly as `UpdateData` formatted them, with no
`XCOMHISTORY` and no `ONLINEEVENTMGR.GetSaveGames` re-query anywhere.

`Header.Description` is assembled in `XComOnlineEventMgr.FillInHeaderForSave`
as `Time \n PlayerSaveDesc \n GetSavedGameDescription()` (the last part may
itself contain newlines), so after `SplitString(Description, "\n")`:

```
[0] date   [1] time   [2] player save desc   [3] game type   [4],[5] detail
```

and the row prints `FormatTime(Header.Time) @ [2]`, then `[3]`, then
`[4] - [5]`. `FormatTime` is `Repl(Time, "\n", " - ")` plus, for `INT` only,
`FormatTimeStampFor12HourClock` — a **native** function, and that is the
reason the formatting lives in the companion rather than in C++: reproducing
it natively would say *14:22* to a player whose screen says *2:22 PM*.

Fewer than four description parts is the game's own error-report shape
(`MC.FunctionBool("SetErrorReport", true)`), and the companion mirrors that
branch instead of inventing one.

**The save screen's blank row is not blank.** `UpdateData` calls
`FillInHeaderForSave` for it, so it shows the CURRENT date and the name the
save is about to get — and the companion calls the same function for the same
row. That call iterates the history, which is why the words are cached (§6).

## 4. What native reads, and what it must not

Per row (`UISaveLoadGameListItem`): `AcceptButton` +0x3D8, `DeleteButton`
+0x3E0, `Index` +0x4B0, `ID` +0x4B4. Per screen: `m_iCurrentSelection` +0x410
and `m_arrListItems` +0x424 — **identical on both screens**, because they
declare the same three fields first; the titles and the in-flight flags differ
(`m_sLoadTitle` +0x448 and `m_bLoadInProgress`, bit 0x02 of the dword at
+0x434, against `m_sSaveTitle` +0x508 and `m_SaveStage` +0x504).

The buttons' `Text` is the localized verb the row is showing
(`ЗАГРУЗИТЬ` / `СОХРАНИТЬ` / `НОВОЕ СОХРАНЕНИЕ` / `УДАЛИТЬ`) — read it, never
translate it: `GetAcceptLabel` picks between three localized strings and
`UpdateData` / `ShowHighlight` write the result into the widget.

`DeleteButton` is `Hide()`n on the save screen's new-save row, and
`UIButton.Click()` refuses an invisible button, so button visibility is the
existence test for the delete verb. This is the documented exception to
"never gate on `bIsVisible`": a child BUTTON's visible bit is toggled
deliberately and does mean hidden.

## 5. Activation goes through the row's own button

`AcceptButton` was spawned with `ID == -1 ? RenameClickedDelegate :
AcceptClickedDelegate`, so **clicking it dispatches new-save-rename versus
overwrite for free**, and `UILoadGame.OnAccept` opens by calling
`SetSelection(control.Owner.Owner.Index)` — clicking row N also selects row N,
so the two halves cannot disagree about which save is being loaded. That is
strictly better than calling `OnAccept()` ourselves with a null control.

Focus write-back is the screen's own `SetSelection(int)`: it moves the Flash
highlight, and it sets `m_iCurrentSelection`, which is what the game's own
`Delete` key then acts on.

⚠ `SetSelection` on `UILoadGame` early-returns when the index is unchanged,
and clamps an out-of-range index to `GetNumSaves()` — one PAST the end of
`m_arrListItems`. Only reachable by passing an out-of-range index, which the
recipe never does.

## 6. Costs

The navigator re-renders every ~100 ms, so a naive companion call would walk
every save ten times a second (and re-run `FillInHeaderForSave` with it). The
words are therefore cached on the ROW POINTER VECTOR: the only thing that
changes a save list is `BuildMenu()`, which respawns every row panel, so a
changed pointer list is an exact and self-invalidating cache key.

## 7. Naming a new save: `UIInputDialogue`

Enter on the save screen's row 0 raises `UIInputDialogue` — the one surface in
this feature where the player must TYPE, into a **Flash TextField** that no
screen reader can see. Its text lives in Flash; only the prompt and the PREFILL
are retained (`m_kData.strTitle` +0x410, `m_kData.strInputBoxText` +0x420) —
and the prompt is the only one of those the reader still uses, because the field
itself turned out to be readable (§7.1).

⛔ It must NOT be a graph recipe, and it must claim NOTHING. A recipe attaching
turns on the navigator's claims — arrows, **Enter**, **Space**, Tab, Home, End
— and every one of those is a key the player is using to type or to answer; and
a spare chord for a read-back key does not exist either (`F1`–`F4`, `?` and
Ctrl+Shift+S are claimed globally by `src/input.cpp`, and everything else is a
text-editing key).

So `src/text_input.cpp` **observes and reads**. `InputArbiter::RegisterObserver`
is a passive fan-out added for this: it sees every key-down and every `WM_CHAR`
the chokepoint handles, claimed or not, and changes nothing about what the game
receives. It runs inside the window procedure and so records one bit — *a key
happened* — which is all it needs to, because the field itself turned out to be
readable.

### 7.1 The Flash side is readable — MEASURED LIVE 2026-08-21

`GFxMoviePlayer` exposes the whole Scaleform path API to script, and every one
of these is an ordinary UFunction we can ProcessEvent:

```
native function string GetVariableString(string path);
native function float  GetVariableNumber(string path);
public native noexport final function int    ActionScriptInt(string path);
public native noexport final function string ActionScriptString(string path);
```

They all take a PATH STRING, which is why `/call` now accepts arguments
(`&arg=`, once per declared argument). Passing a string is safe for the reason
ProcessEvent's parameter semantics give: parameters are shallow-`memcpy`'d and
the destroy pass skips everything below `ParmsSize`, so a buffer we own and keep
alive across the call is all that is needed.

With the save-name dialog open, on `UIMovie_2D` (the movie behind
`UIInputDialogue.Movie`):

| call | answer |
|---|---|
| `ActionScriptString "Selection.getFocus"` | `_level0.theInterfaceMgr.UIInputDialogue_0.theInputDialogueScreen.textInputBox` |
| `ActionScriptInt "Selection.getCaretIndex"` | `12` |
| `ActionScriptInt "Selection.getBeginIndex"` / `getEndIndex` | `0` / `12` |
| `GetVariableString "<focusPath>.text"` | `Сохранение 5` |
| `GetVariableString "<focusPath>.length"` / `.maxChars` | `12` / `40` |
| `GetVariableString "<focusPath>.caretIndex"` | `12` |

So AS2's global `Selection` class resolves — **with or without the `_global.`
prefix** — and the caret and the selection are EXACT. The way the two were told
apart during the probe is worth keeping: a path that does not resolve answers
**0**, while the real accessors answer **-1** when nothing has focus.

Read what that first line says about the dialog, too: it opens with
`begin 0, end 12, caret 12` — **the whole prefill is SELECTED**, so the next
keystroke replaces the name. No caret model could have known that, and the
announcement now says *selected* on arrival because of it.

`GameUI::ReadFocusedTextField` is the whole thing in one call: focus path, text,
caret, selection. Four outbound calls, two of which leak the `FString` they
answer with — the same bargain `ConsoleCommand` makes — which is why the module
reads behind a keystroke (and on a 500 ms heartbeat, so a mouse click placing
the caret is still noticed) and never free-runs.

`Selection.getFocus()` earns its place beyond the caret: it names WHICH field
has focus, which the crash-report dialog (`eDialogType_Report`, a title box and
a body box with Tab between them) needs and "the text" alone could not answer.

What gets spoken: the prompt plus the starting text (and *selected* when it is
all selected) on arrival; then, per keystroke, the inserted run, or what a
Backspace removed, or the newly selected text, or the character the caret landed
on — whichever of those actually changed. A key the field REFUSED (box full,
Backspace at position zero, arrow at the edge) changes nothing and stays silent,
which is the same rule the rest of the layer follows.

None of this is specific to naming a save. It is the same question behind the
key-bindings screen, which CLAUDE.md lists as unreadable by either layer because
it is Flash-driven with no `UIList`.

And it generalizes one step further, which is why the harness now carries
`/flash?field=` (`just dev-field`): the paths above are ABSOLUTE, but every
`UIPanel` knows its own `MCPath`, and `UIMCController.GetString` composes
`Movie.GetVariableString(Panel.MCPath $ "." $ Field)`. So any widget printed by
`/gui` can be asked what it actually put on screen — which is the first time
this project has been able to check a readout against the display without a
sighted person looking at it. ⛔ A measuring instrument, not a reading strategy:
those getters are blocking VM round-trips (`UIMCController.uc:170`, *"very
expensive, use sparingly"*), Flash holds the rendered text and none of the
semantics, and no field name in it can be pinned by a `static_assert`.

## 8. Not covered here

- `UIProgressDialogue` (the progress modal on consoles and async loads); on PC
  loading is synchronous and the dialog is skipped.
- The confirmations (lost progress, overwrite, delete, bad version, missing
  DLC, different language) — all `UIDialogueBox`, already covered by the
  dialog recipe, which probes first and so wins over this screen.
