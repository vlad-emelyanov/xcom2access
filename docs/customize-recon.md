# Soldier customization, manage equipment, the character pool — recon (2026-09-04, built from source, unexercised)

Three P2 surfaces measured together because two of them are one layout and the third is reached
through the first: `UIManageEquipmentMenu` (the strip-equipment menu the loadout and squad select
open), the `UICustomize` family (the category menu, its category screens and the option picker —
17 classes counting the Templar / Skirmisher / SPARK variants), and `UICharacterPool` with its
import / export picker `UICharacterPool_ListPools`. Read against `SrcOrig`, the Highlander's four
overrides (`UICustomize_Head`, `_Body`, `_SkirmisherHead`, `UICharacterPool_ListPools`) and the
Highlander-derived SDK dump; nothing here has run live. The armory hub (`armory-recon.md`) and the
pause menu were the neighbours copied from.

§4 records the fourth screen this pass was asked about — `UIResistance` — and why it got no
recipe: it is dead code.

## 1. `UIManageEquipmentMenu` — the strip-equipment menu

### 1.1 What it is

"Free onscreen clutter by moving the shortcuts to *make items available* to a separate list
(MODELED AFTER THE PAUSE MENU)" — the class's own header. Spawned by `UIArmory_Loadout.
OnManageEquipmentPressed:296` and `UISquadSelect.OnManageEquipmentPressed` (Highlander `:934`),
three `AddItem`s each — `m_strStripItems`, `m_strStripGear`, `m_strStripWeapons`, all
`UISquadSelect`'s strings — then `ScreenStack.Push`. A `UIList` of `UIListItemString` rows and a
title pushed by `SetTitle`. On mouse+keyboard the opener is a **nav-help button** (`ICON_LT_L2`
— a gamepad's LT); there is no key. Both openers already have the layer's own verbs for it.

### 1.2 What is retained

- ✅ Rows: `InitListItem(Label)` → `SetText`, so `Text` (0x3F4) is real; `bDisabled` (0x414 bit 0)
  is never set here. The title: `m_strTitleLabel` (0x440), the class's own localized string —
  *Manage Equipment*.
- `List` at 0x410. `InputState = eInputState_Consume`.

### 1.3 Input

`OnUnrealCommand` handles `FXS_BUTTON_B` only (`CloseScreen`) and forwards the rest to
`UIScreen` → `Navigator` → the list. **The rows consume nothing**: `UIListItemString.
OnUnrealCommand` answers Enter only when it has a `ConfirmButton`, which these do not, so
`UIList.OnUnrealCommand` reaches its own `OnItemDoubleClicked` / `OnItemClicked` — both
`OnSelectionPressed`, which runs the row's callback (a confirmation dialog) and `CloseScreen`s.
So the pause menu's activation applies unchanged: select, then Enter to the LIST. Escape is left
to the game.

### 1.4 As built

`manage equipment` recipe (`recipe_strategy.cpp`), native only (`ReadManageEquipment`). One stop,
one button per row, named by the title. Listed above the loadout and squad select in `kRecipes`.

### 1.5 First live pass must answer

1. From the loadout's MANAGE EQUIPMENT verb: attach as *Manage Equipment*, three rows, Enter on
   STRIP ITEMS raises the game's confirmation (dialog recipe) and the menu is gone behind it.
2. The same from squad select, and that squad select re-attaches afterwards with its cards
   refreshed (`m_bNoRefreshOnLoseFocus` is set by the opener — the strip must still show).

## 2. The `UICustomize` family

### 2.1 What it is

`XComPresentationLayerBase.UICustomize_Menu(Unit, ActorPawn)` initializes the customization
manager and pushes `Unit.GetMyTemplate().UICustomizationMenuClass` — `UICustomize_Menu` for a
human, the SPARK's own for a SPARK — on the 3D movie; the four category verbs push
`UICustomizationInfoClass` / `HeadClass` / `BodyClass` / `WeaponClass` the same way, and every
category row that has a list of options pushes `UICustomize_Trait` (`UICustomize_Voice` only when
the mouse is inactive). Reached from the armory hub's CUSTOMIZE row (`UIArmory_MainMenu.
OnCustomize`, `bInArmory`), from the character pool's EDIT (`UICharacterPool.EditSoldier`) and
CREATE, and from the multiplayer unit editor (out of scope). `UICustomize_Props` is shipped and
has an opener (`UICustomize_Props`) but the menu no longer lists it (`OnCustomizeProps` has no
caller); it is covered by the family probe regardless.

Every screen is the base class's layout: `Header` (a `UISoldierHeader`, stats hidden), `List` (a
`UIList` of `UIMechaListItem`s), and on demand `ColorSelector`.

### 2.2 What is retained

- ✅ **Every row keeps its words.** `GetListItem(i)` REUSES rows (it spawns only past
  `ItemCount`) and fills them through the mecha setters: `UpdateDataDescription` (the name rows,
  the menu's verbs, the picker's plain options), `UpdateDataValue` (label + the current choice,
  `FormatCategoryDisplay`), `UpdateDataColorChip` (label + a `UIBGBox` swatch whose `BGColor`
  0x3D8 is the game's `"0x00RRGGBB"` — hidden for the weapon-colour default `0x00000000`),
  `UpdateDataCheckbox` (the pool's three ALLOW rows), `UpdateDataButton` (the voice picker's
  PREVIEW). `MakeRow` reads all five; a `ColorChip` row now folds to its own `OptionWidget`
  carrying the hex, and `Vocab::ColourName` names it.
- ✅ A greyed row's reason: `SetDisabled(disabled, Reason)` → `BG.SetTooltipText`, which
  `ReadRowTooltip` reads. The reasons are the base class's seven localized strings (super
  soldier, no nationality, needs veteran status, remove helmet, no variations, incompatible, no
  class variants) plus the Highlander's `m_strChangeFace` (Issue #219).
- ⛔ **The header is a Flash push** (`Header.PopulateData`), and it is repainted from the
  manager's `UpdatedUnitState` after a name edit (`OnNameInputBoxClosed`). The companion composes
  it with `X2AccessHelper.DescribeArmorySoldier` off that pending copy, so the armory screens and
  this one name a soldier identically.
- ⛔ **The picker keeps no title.** `UpdateTrait(_Title, …)` stores `Data`, `StartingIndex` and
  the delegates and DROPS `_Title` — the shipped screen shows the options under the header and
  nothing else. The screen beneath keeps its selection across the push (every `UpdateData`
  restores `currentSel`), so the picker is named by that row's label: HAIR, VOICE, NATIONALITY —
  what a sighted player just read. Native walks the stack to the next screen of the family (a
  mouse guard sits between them: `bConsumeMouseEvents` pushes one UNDER every screen).
- ⛔ **The biography box** (`AS_SetCharacterBio`) is a Flash push on the info screen; the
  companion re-derives it (`GetBackground()` off the pending copy).
- The screen's own title: each direct subclass declares `m_strTitle` (CUSTOMIZE SOLDIER, EDIT
  SOLDIER INFO, EDIT HEAD, EDIT BODY, EDIT WEAPON, EDIT PROPS — all in the INT loc file) and
  never draws it; the companion reads it by cast chain for the screen name.
- Offsets (`UICustomize`): `UnitRef` 0x418, `Unit` 0x41C, flag dword 0x424 (`bInArmory` bit 0),
  `List` 0x440, `Header` 0x448, `ColorSelector` 0x47C; `UICustomize_Trait::Data` 0x514,
  `StartingIndex` 0x544. ⚠ The DLC_3 SPARK classes are NOT in the dump (the generator ran without
  that DLC's package loaded); they are matched by class chain and use no offset of their own.

### 2.3 The colour selector

`GetColorSelector(Colors, Preview, Set, Selection)` hides the list and spawns a `UIColorSelector`
(0x47C) — a `ChipContainer` of `UIColorChip`s laid out `iMaxChipsPerRow` wide, each retaining
`Index` 0x3F8, `Row` 0x3FC, `Col` 0x400 and its colour on `BG` 0x3D8 (`BGColor`), with its index
DRAWN as its label; `ColorChips` 0x400 and `Colors` 0x410 on the selector, `InitialSelection`
0x420 the colour worn now. While it exists `UICustomize.OnUnrealCommand` hands it every key
first; it forwards to the chip its container's navigator holds. Arrowing = `Navigator.SetSelected`
→ `OnSelectedIndexChanged` → `OnPreviewColor` → the screen's `Preview*` delegate (the pawn shows
it); Enter on a chip = `OnAcceptDelegate` → the screen's `Set*` delegate → commit, `UpdateData`,
`CloseColorSelector`. Escape = `OnCancel` → `CloseScreen` → `CloseColorSelector(true)` →
`OnSetDelegate(InitialSelection)`: a REVERT, and the screen stays. Focus write-back is
`SetSelectedNavigation` on the chip, which is that same navigator path.

### 2.4 Input

`UICustomize.OnUnrealCommand`: the colour selector first; then `List.OnUnrealCommand`, which for
Enter hands the SELECTED row its own `OnUnrealCommand` — `UIMechaListItem.HandleCustomControls`
(a checkbox row's box, a button row's button, a spinner's spinner) then `HandleClickableControls`
(any row with a `Desc` or `ColorChip`: `Click()` → the row's delegate — the name rows open the
text-entry dialog, the value rows push the picker, the swatch rows open the selector) — and a row
that answers CONSUMES the key, so the list's own `OnItemClicked` never fires from the keyboard.
Then Tab / Mouse5 → `NextSoldier`, LeftShift / Mouse4 → `PrevSoldier` (both only `bInArmory`,
behind `HasSoldiersToCycleThrough(UnitRef, CanCycleTo)`), Escape / right-click → `OnCancel`.

⛔ **`UICustomize_Menu` makes its checkboxes `bReadOnly`** (`OnListInited`, "so that we don't get
double click feedback"), so the row's Enter reaches `UICheckbox.OnUnrealCommand`, which returns
true WITHOUT toggling — consumed, dead. The game's route is `FXS_BUTTON_A` →
`SimulateMouseClickOnCheckbox` (behind `bIsFocused && bIsVisible` — the flag the game leaves
false on screens it shows), and the mouse's is the LIST's `OnItemClicked` = `OnListOptionClicked`.
The layer calls `SimulateMouseClickOnCheckbox` on the selection directly.

`UICustomize_Trait.OnUnrealCommand` answers Enter at SCREEN level — `OnClickLocal(List,
SelectedIndex)` → the clicked delegate, then `CloseScreen` — before forwarding to the base, whose
list path then finds a description row with no delegate and consumes. `OnCancel` re-fires the
delegate with `StartingIndex` first: Escape REVERTS. `List.OnSelectionChanged` is the same
delegate for most categories, so moving the selection PREVIEWS the option on the pawn — our
write-back (`SetSelectedIndex`) fires it. The voice picker's rows carry a PREVIEW button
(`UpdateDataButton`); its `OnUnrealCommand` Enter is reached only through the ROW (the screen's
Enter picks and closes first), which the layer sends as the row's secondary action.

### 2.5 As built

`customize` recipe (`recipe_strategy.cpp`) with `X2AccessCustomizeReader`, for the whole family
(the probe is the topmost stack entry being `IsKindOf UICustomize`). Three stops: the soldier (a
spinner while the companion says `cycle=1` — `CycleCustomizeSoldier` through `NextSoldier` /
`PrevSoldier`), the rows under the title (the picker's under the opener's label; a row's value
is its current choice, a swatch its hue, a checkbox its state; greyed rows carry the tooltip
reason; the picker marks `StartingIndex` selected and lands there; Enter is the screen's, a
checkbox row's is `SimulateMouseClickOnCheckbox`, a PREVIEW row's Backspace is the row's own
Enter), and on the info screen the biography. With the selector open the graph is the grid
instead: a `StartRow("chips")` per grid row (column-preserving Up/Down), each chip *hue, number*
with `selected` on `InitialSelection`, write-back and Enter through the chip's navigator. The
resource strip trails where the game shows it. Wire cache keyed on the rows, the screen and the
`Unit` POINTER (the rows are reused across a cycle, and a pool unit has no ObjectID),
discriminated on `UnitRef.ObjectID`.

### 2.6 First live pass must answer

1. From the hub's CUSTOMIZE: attach as *CUSTOMIZE SOLDIER, <name, rank, class>*, land on EDIT
   INFO, Up to the soldier node and Left/Right must cycle soldiers with the header re-reading —
   and cycling onto a SPARK must re-attach on the SPARK's menu, not go silent.
2. EDIT HEAD: every row must read `label, current choice`; HAIR COLOR must read a hue (audit the
   DISTRIBUTION across the roster — every soldier "dark grey" is the collapsed-value failure);
   Enter on it must open the grid, arrows must move by row and column with the pawn previewing,
   Enter must commit and land back on the row with the new hue, Escape must revert.
3. HAIR: the picker must attach as *HAIR* with the current style marked `selected` and landed
   on; arrowing must preview; Enter picks and closes; Escape reverts. `just dev-props
   "screen=UICustomize_Trait" StartingIndex` against the row that reads selected.
4. EDIT INFO: FIRST NAME → the text-entry dialog (observer), and on return the soldier node must
   carry the new name. VOICE → the picker with PREVIEW on every row; Backspace must play the
   voice without closing. The biography stop must read the label and the paragraph.
5. In the character pool's editor (`bInArmory` false): no spinner on the soldier node, the
   three ALLOW checkboxes must toggle on Enter, and TIME ADDED must read `unavailable` with its
   timestamp.
6. Shape-count the ring, then `grep -P '  '` for the localized-var trap and grep for `0x` — a
   raw hex reaching speech is a `ColorChip` row that missed the hue path.

## 3. `UICharacterPool` and `UICharacterPool_ListPools`

### 3.1 What it is

The main menu's CHARACTER POOL (`XComPresentationLayerBase.UICharacterPool`): the pool's soldiers
as CHECKBOX rows (`UpdateDataCheckbox(name, "", selected, SelectSoldier, EditSoldier)` — the box
selects for export / delete, the ROW click edits), six `UIButton`s spawned only when
`Movie.IsMouseActive()` (Create, Import, Export, Delete, Select all, Deselect all — greyed with
`m_strNothingSelected` / `m_strNoCharacters` / `m_strEverythingSelected` as tooltips), an
`OptionsList` of one row holding the pool-USAGE dropdown, and a `UIX2PanelHeader` (`Title` 0x3D8,
`Label` 0x3E8 retained) — CHARACTER POOL / ACTIVE CHARACTERS. The picker is pushed by
`UICharacterPool_ExportPools` (with the selection) and `UICharacterPool_ImportPools`; its header
says the mode (`m_strTitle` + `m_strExportSubtitle`; `m_strTitleImportPoolLocation` +
`m_strImportSubtitle`; `m_strTitleImportPoolCharacter` + the file's friendly name) and its rows
are `UpdateDataValue(name, "")` — or, on the file list outside a file, `UpdateDataButton(name,
DELETE, OnDeletePool)`. Offsets: pool `List` 0x420, `TitleHeader` 0x428, buttons 0x430–0x458,
`m_strEditCharacter` 0x4D4, `m_strNoCharacters` 0x554, `OptionsList` 0x5F8; picker `List`
0x420, `TitleHeader` 0x430, flag dword 0x468 (`bIsExporting` bit 0, `bHasSelectedImportLocation`
bit 1).

### 3.2 Input — two keyboard dead ends

- **The pool.** Enter on a row → the row's checkbox toggles (`HandleCustomControls`, not
  read-only here) and the key is consumed; EDIT is `FXS_BUTTON_X` (`EditSoldier`, on
  `List.GetSelectedItem()`), CREATE / DELETE `FXS_BUTTON_Y`, IMPORT `L3`, EXPORT `RT` — gamepad,
  all of it. A keyboard-only player can select soldiers and do nothing with them. The mouse
  build's buttons cover create / import / export / delete / select all; EDIT is a ROW click.
- **The picker.** `OnUnrealCommand` maps Enter / Space / A to `OnAccept`, which picks only
  `if (ISCONTROLLERACTIVE)`. Sending Enter to the LIST instead does not help: a value row
  `Click()`s nothing and consumes, a button row presses its DELETE — whose `OnDeletePool` looks
  the BUTTON up as a list item and gets `INDEX_NONE` — and consumes. `OnItemClicked`
  (`OnClickLocal`) is reached by mouse only. DELETE by keyboard is `FXS_BUTTON_Y` →
  `DeleteCurrentlySelectedPool`, on the selection.

### 3.3 As built

`character pool` and `pool files` recipes (`recipe_shell.cpp`), native only. The pool: the
characters (checkbox nodes — Enter toggles through `SimulateMouseClickOnCheckbox` on the
selection, Backspace edits through `EditSoldier`, both advertised on the row with the game's own
EDIT CHARACTER), the actions (EDIT CHARACTER as a button too, then the six buttons with their
tooltip reasons, `ClickButton`), the usage dropdown (`AdjustDropdown`). The picker: one stop of
buttons — Enter calls `OnClickLocal(List, index)`, the controller branch's own call; Backspace on
a row that draws DELETE sends `FXS_BUTTON_Y` after selecting, and only on those rows (in export
mode the game's Y would also hit the CREATE NEW POOL row and delete `EnumeratedFilenames[-1]`).
Both named by their header, title then subtitle.

### 3.4 First live pass must answer

1. Main menu → CHARACTER POOL: attach as *CHARACTER POOL, ACTIVE CHARACTERS* after the 3D
   transition, one checkbox row per soldier reading `name, unchecked, EDIT CHARACTER`; Enter
   must check and re-speak; Tab to the actions must find EDIT CHARACTER then CREATE CHARACTER,
   IMPORT CHARACTER, EXPORT SELECTION (`unavailable, NO CHARACTERS SELECTED` with nothing
   checked), DELETE SELECTION, SELECT ALL, DESELECT ALL.
2. Backspace on a row must open that soldier's customize menu (the family recipe, `bInArmory`
   false) and Escape must come back here with the row re-read.
3. IMPORT CHARACTER: the picker attaches as *SELECT POOL FILE, IMPORTABLE POOLS*, rows are pool
   names with DELETE; Enter must go INTO the pool (*SELECT CHARACTER TO IMPORT, <file>*, first
   row IMPORT ENTIRE POOL, no DELETE anywhere); Escape must come back OUT to the file list, not
   close. Backspace on a file must raise the game's delete confirmation.
4. EXPORT with two checked: *SELECT POOL, EXPORT CHARACTERS*, CREATE NEW POOL first (no DELETE)
   then the files; Enter on CREATE NEW POOL → the text-entry dialog; Enter on a file → the
   confirm dialog.
5. The usage dropdown must read its label and current value, Left/Right must change it and
   re-speak, and Space must read `m_strUsage_Tooltip`.

## 4. `UIResistance` — dead code, no recipe

Asked for alongside the three above; measured and refused. `XComHQPresentationLayer.
UIResistance(RegionState)` exists and spawns a full `UIX2SimpleScreen` (income, contact cost,
outpost, the Resistance HQ panel, a FLY button), but its ONLY call site —
`XComGameState_WorldRegion.DisplaySelectionPrompt` — is commented out in vanilla (`:1313`) and in
the Highlander (`:1366`); the region item's own casts to `UIResistance` (`UIStrategyMapItem_
Region:187`, `:502`) only ask whether one is up, which it never is. Every verb it would have
offered is already the geoscape tray's region stop (`geoscape-recon.md` §16). Marked 🚫 dead in
`ui-inventory.md`.
