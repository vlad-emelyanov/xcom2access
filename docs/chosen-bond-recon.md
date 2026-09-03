# Chosen dossier + soldier bonds — recon

Measured from the SDK sources (`UIChosenInfo.uc`, `UIChosenEncountered.uc`, `UISoldierCaptured.uc`,
`UISoldierBondScreen.uc`, `UISoldierBondListItem.uc`, `UISoldierBondConfirmScreen.uc`,
`UIBondIcon.uc`, `UIFacility_BondSlot.uc`, the presentation layers) and the generated SDK's
layout dump, 2026-09-03. Built the same day; **nothing here has run live yet** — the first-pass
checklist is §8 and the debt is in `next-steps.md`.

Five screens, two families, and they split cleanly by what the widget tree retains:

| Screen | Family | Retains | Reached from |
|---|---|---|---|
| `UIChosenInfo` | Chosen | `AllActiveChosen`, `SelectedChosen`, `CurrentChosenIndex` (the state objects it populated Flash FROM) | geoscape HUD's Chosen button (`UIStrategyMap_HUD.SetUpChosenButton`), `UIStrategyMap.OnChosenInfoClicked`, the Chosen panel on a `UIAlert` (`ChosenPanelProcessMouseEvents`); in tactical the mouse-controls "Chosen info" button goes through the same `UIChosenInformation()` |
| `UIChosenEncountered` | Chosen | `ChosenRef` | `UIChosenRevealComplete` → `UIChosenEncountered(ChosenState)`, right after the reveal cinematic |
| `UISoldierCaptured` | Chosen | `ChosenRef`, `TargetRef`, `bCapturedOnMission` | `BuildUIScreen(…, 'SoldierCaptured', …)` — after a mission with a kidnap, and from a covert-action risk (`X2StrategyElement_DefaultCovertActionRisks:109`) |
| `UISoldierBondScreen` | Bond | `UnitRef`, `BondmateRef`, `bSquadOnly`, `List`, `m_arrSoldiers`, the sort state; each row keeps `UnitRef` + `ScreenUnitRef` | `HQPRES.UISoldierBonds(UnitRef, bSquadOnly)`: the armory main menu's BONDS entry, squad select's bond icon (`bSquadOnly`), after action's bond icon (`bSquadOnly`) |
| `UISoldierBondConfirmScreen` | Bond | `Soldier1State`, `Soldier2State`, `UnitSlotState` (object pointers — the screen's own cache) | `UISoldierBondScreen.OnConfirmBond`, `UISoldierBondAlert`'s confirm, the Training Center's bond slot after its Yes/No dialog |

Every one of them spawns from the presentation layer with the class named above — none is a
`UIFacilityUpgrade`-style corpse. ⛔ **Not one retains a word.** All five push their text through
`MC.BeginFunctionOp` with locals on both sides (house style), so every line below is re-derived by
the companion from the state objects the screen itself cached, exactly the way the screen derived
it. `XCOMHISTORY` is touched only where the screen's own code touches it (a `StateObjectReference`
the screen keeps has to be resolved somewhere, and the screen resolves it in the same call).

## 1. What a sighted player sees, screen by screen

### 1.1 `UIChosenInfo` — the Chosen database

`RefreshHeaders` + `RefreshDisplay`, in push order:

| On screen | Source |
|---|---|
| `CHOSEN DATABASE` / `Active Known Chosen:` | `ChosenInfo_ListTitle` / `ChosenInfo_ListSubtitle` (`SetListHeader`) |
| four tab buttons: name + nickname, or `UNKNOWN CHOSEN` for an empty slot | `AS_SetButton(i, GetChosenName(), GetChosenNickname())` for `AllActiveChosen`, then `class'UIResistanceReport_ChosenEvents'.default.UnknownChosenLabel` for `i < 4` |
| portrait, class (CAPS), name, nickname | `SetInfoPanel` operands 0-3 |
| `ENCOUNTERS:` n, `SOLDIERS INTERROGATED:` n, `SOLDIERS CAPTURED:` n | operands 4-9 — `GetNumEncounters()`, `GetNumExtracts()`, `GetNumSoldiersCaptured()` |
| `KNOWLEDGE:` meter + `CURRENT ACTION: <action>` | operands 10-12 — `GetKnowledgePercent(true)`, `ChosenInfo_MonthActionPrefix @ GetCurrentMonthActionName()` (prefix only when the action is non-empty) |
| `HUNT PROGRESS:` meter, no text | operands 13-15 — `FragmentLevel` = 3 when `IsStrongholdMissionAvailable()`, else `int(RivalFaction.GetInfluence())` (an `EFactionInfluence`, 0-2); the text operand is `""` |
| `DEFEATED` badge | `ShowChosenDefeat(ChosenInfo_DefeatedLabel)` only when `bDefeated`, else `HideChosenDefeat` |
| `STRENGTHS:` + up to 6 rows (icon, name, long description) | `GetChosenStrengths()`; a trait whose `AbilityRevealEvent` has not fired reads `ChosenInfo_UnknownChosenTraitName` / `…Description` coloured disabled — **the unknown wording is information, not a gap** (same rule the reveal popup follows) |
| `WEAKNESSES:` + up to 4 rows | `GetChosenWeaknesses()`, same reveal filter |
| `ADVERSARY:` faction title + faction name | `AS_SetWeaknessFaction(FactionState.GetFactionTitle(), GetFactionName())` |

`XComGame.int` populates every one of the sixteen `[UIChosenInfo]` strings on the class that
declares them (`ChosenInfo_JobLabel` and `ChosenInfo_HuntXComLabel` are declared, populated and
never drawn). The `UNKNOWN CHOSEN` label is borrowed from `[UIResistanceReport_ChosenEvents]`,
which also populates.

**Keyboard in vanilla**: Enter, Space and Escape all `OnContinue()` (close). Switching Chosen is
`FXS_DPAD_UP/DOWN` and `FXS_VIRTUAL_LSTICK_UP/DOWN` only — no `FXS_ARROW_*` — so on mouse+keyboard
the only way to look at the second Chosen is to CLICK its tab (`OnMouseEvent` → `SelectChosen`).
A keyboard dead end of the promotion-hero kind; re-providing the switch is what the recipe is for.

### 1.2 `UIChosenEncountered` — "CHOSEN REVEALED"

One `UpdateData` push: `ChosenEncounteredTitle`, poster path, icon, `string(Level)`, class (CAPS),
name, nickname (CAPS), and `ChosenEncounteredBody` expanded with `StrValue0 = GetChosenName()`.
Enter/Space → `OnContinue()`; **no Escape branch**. The level is drawn as a bare number in a badge
with no label anywhere on the screen, so it is NOT spoken — a bare "2" at the end of a name reads
as a count.

### 1.3 `UISoldierCaptured` — "SOLDIER CAPTURED"

`UpdateData`: `SoldierCapturedTitle`, poster, icon, `string(Level)`, `""` (the type slot is
empty), `GetChosenName()` in the NAME slot, **`GetChosenClassName()` in the NICKNAME slot** (the
screen's own choice — it reads "CAPTURED BY <name> <CLASS>"), the soldier's class icon, rank icon,
class name, full name, rank name, `SoldierCapturedBy`, and `SoldierCapturedBody` expanded with
`StrValue0 = ChosenName`, `StrValue1 = UnitState.GetFullName()`. Enter/Space → `OnContinue()`
(which fires `SoldierCapturedClosed` when `bCapturedOnMission` — that is why activation must go
through the screen's input path and not a `CloseScreen` poke); **no Escape branch**.

### 1.4 `UISoldierBondScreen` — "SOLDIER BONDS"

| On screen | Source |
|---|---|
| `SOLDIER BONDS` | `BondTitle` (`SetScreenHeader`) |
| the screen's soldier: rank name (CAPS), full name + nickname (CAPS), class icon, rank icon | `SetPlayerInfo` — `GET_RANK_STR`, `GetName(eNameType_FullNick)` |
| `BONDMATE` panel: level number in the icon, name, nickname, rank abbreviation, class, flag, compatibility word, cohesion bar | `SetBondMateInfo` when `Unit.HasSoldierBond(BondmateRef, BondData)`; `NoBond` ("No bondmate") with level -1 otherwise, which hides the panel |
| sort header: `RANK` `NAME` `CLASS` `COMPATIBILITY` `COHESION` | five `UIFlipSortButton`s in `m_kSoldierSortHeader` — these DO retain `Label`, `bArrowVisible`, `bArrowFlipped` (the personnel roster's finding) |
| one row per other living soldier (crew, or squad when `bSquadOnly`): name, nickname, rank abbreviation, class, flag, compatibility word, cohesion bar, and a `CREATE BOND` button on rows where `CanHaveBondAtLevel(Unit, ScreenUnit, BondLevel + 1)` | `UISoldierBondListItem.UpdateData` — `ActivateBondButton.Show()` + `CanShowBondButton` on those rows, `Hide()` elsewhere; `SetDisabled(true)` on a row whose soldier is bonded to someone else (either direction) |

Cohesion is `float(BondData.Cohesion) / CohesionThresholds[Clamp(BondLevel + 1, …)]`, a 0-1
fraction Flash draws as a bar — spoken as a percentage. Compatibility is already a word
(`GetSoldierCompatibilityLabel`). Default sort is cohesion, descending (`m_bFlipSort = true`).

**Keyboard in vanilla** (`OnUnrealCommand`): Escape closes; **Tab / mouse-5 = `NextSoldier()`,
LeftShift / mouse-4 = `PrevSoldier()`** (outside the squad screens); **Enter = `OnSelectSoldier`
on the highlighted row** (switch the whole screen to that soldier; outside squad screens); the
sort column changes on `FXS_DPAD_LEFT/RIGHT` and flips on `FXS_BUTTON_L3` — controller only;
and **`OnConfirmBond` is `FXS_BUTTON_A` or the mouse on the row's `CREATE BOND` button, which
called `DisableNavigation()`**. So on mouse+keyboard a bond cannot be CREATED from this screen
without a mouse, and the sort cannot be changed. Those two are the verbs the recipe re-provides;
the Tab/Shift soldier cycle is displaced by the layer's own Tab and re-provided as Left/Right on
the soldier node. `IsInSquadScreen()` (squad select or after action on the stack) is the game's
own gate for both the cycle and the switch, and the companion asks it rather than re-deriving it.

### 1.5 `UISoldierBondConfirmScreen` — "DEVELOP BOND - LEVEL n"

`SetBondData`: `m_strTitle @ (BondLevel + 1)`, then for each soldier rank icon, class icon, class
name, full name (CAPS), then `m_strBenefits`; `SetBenefitRow(i, Name, Description)` for every
ability template carrying an `X2Condition_Bondmate` (target or shooter conditions) whose
`MinBondLevel == BondLevel + 1` and `RequiresAdjacency == EAR_AnyAdjacency`; `SetConfirmButton
(m_strConfirm)`. `BondLevel` comes from `Soldier1State.HasSoldierBond(unitRef, BondData)` with an
uninitialised local ref — i.e. soldier 1's CURRENT bond, 0 for a first bond — and the companion
makes the same call so its number matches the title's.

Enter/Space → `ConfirmClicked` (creates the bond at level 1 outright, or fills the Training
Center slot for level 2+, then asks the poster question through a `UIDialogueBox` the dialog
recipe already reads); Escape → `Cancel()`, which **empties the staff slots** when the screen was
reached from one. Both go through the screen's own input path.

## 2. Division of labour

Native reads STRUCTURE and SELECTION; the companion re-derives WORDS from the screen's own cached
state. Offsets (generated SDK dump, pinned in `sdk_layout_checks.cpp`):

| Class | Field | Offset | Used for |
|---|---|---|---|
| `UIChosenInfo` | `CurrentChosenIndex` | `0x520` | which tab is selected — the wire-cache discriminator |
| | `AllActiveChosen` | `0x524` | how many tabs are real (count only) |
| `UISoldierBondScreen` | flag dword (`m_bFlipSort` 0x1, `bSquadOnly` 0x4) | `0x410` | squad mode; sort direction |
| | `m_eSortType` | `0x414` | sort column (byte) |
| | `UnitRef` | `0x4E4` | the screen's soldier — wire-cache discriminator (rows respawn on switch) |
| | `List` | `0x4EC` | rows + `SelectedIndex` |
| `UISoldierBondListItem` | `UnitRef` | `0x4C4` | join key to the companion's row record |
| | `ActivateBondButton` | `0x4CC` | its `bIsVisible` = the game's own "a bond can be made here" |

`UISoldierBondConfirmScreen`, `UIChosenEncountered` and `UISoldierCaptured` need **no offsets at
all**: activation is the screen's own input path and every word is companion-side.

Companion classes: `X2AccessChosenReader` (`DescribeChosenInfo`, `SelectChosen(int)`),
`X2AccessBondReader` (`DescribeBonds`, `DescribeBondConfirm`, `ConfirmBond(int)`,
`SwitchSoldier(int)`, `CycleSoldier(int)`, `ClickBondSort(int)`), and two new branches in
`X2AccessPopupReader` for the two continue-only screens, which join the popup family's class table
(`game_ui_popup.cpp`) — they satisfy its contract exactly: topmost on the stack, Enter answered at
screen level. ⚠ Neither answers Escape, so their button record carries ONLY the continue verb;
the recipe's accept/cancel fallback pair would advertise a Dismiss that does nothing.

## 3. The recipes

**`chosen dossier`** (`UIChosenInfo`). Screen name: `ChosenInfo_ListTitle` + subtitle.
- Stop `chosen`: one tab node per slot, four in all — met Chosen as `name, nickname` (selected one
  carries the landing mark), the empty slots as `UNKNOWN CHOSEN`, unavailable, no activation
  (clicking one in vanilla wraps to Chosen 0, which is not a verb worth re-providing). Enter =
  `SelectChosen(i)` — the game's own handler, sounds and Flash focus included.
- Stop `dossier`: class + name + nickname (+ `DEFEATED`), the three counters as label + value, the
  knowledge meter as label + percent + current action, the hunt meter as label + "level n of 3",
  the adversary line. Positions suppressed — this is a document.
- Stops `strengths` and `weaknesses`: the section title as context, one node per trait (name as
  label, description as value; Space re-speaks the description).
- Stop `actions`: Continue → Enter to the screen.

**`soldier bonds`** (`UISoldierBondScreen`). Screen name: `BondTitle`.
- Stop `soldier`: the screen's soldier as a SPINNER when the game offers the cycle (Left/Right =
  `PrevSoldier()`/`NextSoldier()`), plain text otherwise; then the bondmate panel as one line
  (`BONDMATE`, bond level n, name, rank, class, compatibility, cohesion n percent) or `NoBond`.
- Stop `sort`: the five headers, spoken with the personnel roster's words (`sort by RANK,
  ascending`); Enter = the header's own mouse handler, which flips when already selected.
- Stop `list`: a `GraphSheet` — column 0 the soldier (name + nickname), then RANK, CLASS,
  COMPATIBILITY, COHESION under the game's own headings, then a fifth heading-less column holding
  the `CREATE BOND` button only on rows that show it. Enter on the ROW is the bond when the game
  offers one (the prominent button is what a sighted player clicks on that row), else vanilla's
  own Enter (switch the page to that soldier) where vanilla allows it; Shift+Enter is the switch
  regardless; Enter on the `CREATE BOND` cell is `OnConfirmBond`. A row showing the button also
  announces it as an attention part so the affordance is heard without arrowing right. Disabled
  rows are announced, not skipped. The screen is NAMED with the soldier (`SOLDIER BONDS, Sergeant
  Jane Doe`) and attach lands on the roster, the personnel roster's arrangement.

**`bond confirm`** (`UISoldierBondConfirmScreen`). Screen name: the title with its level.
- Stop `bond`: the two soldiers (rank, class, name), then `BENEFITS:` as context over one node per
  benefit (name label, description value).
- Stop `buttons`: `CONFIRM BOND` → Enter; Cancel (`m_strGenericCancel`) → Escape.

Probe order (`kRecipes`): the confirm screen ABOVE the bond screen it is pushed over; the bond
screen above squad select, after action and the armory it is pushed over; the dossier above the
geoscape and below the popups (an alert's Chosen panel pushes the dossier over the alert, and the
popup recipe's topmost-only probe already stands down).

## 4. Sighted-parity rulings

- The `UNKNOWN CHOSEN` slots are ON SCREEN and get nodes; they are marked unavailable rather than
  omitted, because a player who hears "1 of 4" and finds three dead tabs learns the same thing a
  sighted player reads off the greyed buttons — how many Chosen remain unmet.
- An unrevealed trait speaks the game's unknown wording, never the real trait name the template
  would hand us (the reveal popup's rule, `X2AccessPopupReader.PushTrait`).
- The Chosen's `Level` is drawn as a bare number on two of these screens and is not spoken: no
  label anywhere on either screen gives the number a unit.
- The hunt meter has no text operand at all; "level n of 3" is ours, keyed on the same
  `FragmentLevel` the meter is filled from.
- Nothing is spoken about the poster or the portrait — asset paths, never words.

## 5. Why re-derive rather than capture

`UIChosenMissionSummary` went Flash-capture-only because it retains nothing and needs no
selection. `UIChosenInfo` HAS a selection (`CurrentChosenIndex`) and re-pushes the whole panel on
every switch; the bond screen respawns its rows on every sort, switch and refresh. Re-derivation
from the screen's own cached state objects is the pattern every list screen in this mod already
uses, it needs no LRU luck, and it keeps the reveal filter, the `IsInSquadScreen` gate and
`CanHaveBondAtLevel` in the game's code where they belong.

## 6. Wire formats

`X2AccessChosenReader.DescribeChosenInfo`:
```
kind=header    title subtitle
kind=tab       index name nick selected=1 | index label unknown=1
kind=identity  class name nick defeated
kind=stat      label value                        (×3: encounters, interrogated, captured)
kind=knowledge label percent action
kind=hunt      label level max
kind=section   which=strengths|weaknesses title
kind=trait     which index name desc unknown=1
kind=faction   label type name
kind=continue  label
```

`X2AccessBondReader.DescribeBonds`:
```
kind=header   title
kind=soldier  rank name cycle=1 switch=1
kind=bondmate title level name nick rank class compat cohesion   (title only when there is none)
kind=sort     index label selected=1 desc=1
kind=cols     col col col col col        (RANK, CLASS, COMPATIBILITY, COHESION, "" — positional)
kind=row      index name cell cell cell cell cell bond=1 disabled=1   (cells positional, empties kept)
```

`X2AccessBondReader.DescribeBondConfirm`:
```
kind=header   title
kind=soldier  index rank class name
kind=benefits label
kind=benefit  index name desc
kind=button   index label cmd
```

## 7. Known limits going in

- The bond screen's vanilla Tab / LeftShift soldier cycle is displaced by the layer's Tab; the
  cycle lives on the soldier node's Left/Right instead. `Ctrl+Tab` is not re-advertised.
- Sorting from the sheet: the sort stop is a row of buttons, as on the personnel roster — no
  hotkey.
- `UIChosenInfo` opened from tactical (the mouse-controls Chosen button, `UITacticalHUD_MouseControls
  :274`) is pushed over the tactical HUD; the recipe probes on focus like every menu recipe and
  should attach there too, but the tactical stack has never been walked with it up.

## 8. First live pass — checklist

1. **Dossier**: with two Chosen met, Tab to `chosen`, Down/Right to the second, Enter — the
   dossier stop must re-read for the second Chosen (the `CurrentChosenIndex` discriminator is what
   makes the cache refetch; if the words do not change, that is the bug). The two empty slots must
   say `UNKNOWN CHOSEN, unavailable`. An unrevealed strength must read the unknown wording.
   `grep -n 'img:///\|UILibrary' ` over the speech ring must find nothing.
2. **Encountered / captured**: after a Chosen reveal, the popup recipe must attach to
   `UIChosenEncountered` with the body sentence expanded (no `<XGParam` markup in the ring) and
   offer ONE button; Enter must close it. Same for `UISoldierCaptured` after a kidnap.
3. **Bonds from the armory**: BONDS on a soldier with no bondmate — `No bondmate`; Left/Right on
   the soldier node must cycle soldiers and the LIST must re-read for the new soldier. Enter on a
   row must switch the screen to that soldier. Right onto `CREATE BOND` on an eligible row, Enter
   → the confirm screen; Escape back. Sort by RANK, then again → descending.
4. **Bonds from after action** (`bSquadOnly`): the soldier node must be plain text (no cycle),
   Enter on a row must do nothing (vanilla gates `OnSelectSoldier` there) — check the log for a
   refused verb rather than a silent no-op.
5. **Confirm**: `CONFIRM BOND` → the poster dialog (dialog recipe) → No → back on the bond screen
   with the row now disabled or gone. From the Training Center's bond slot, Escape must empty the
   slots again (vanilla `Cancel()`).
6. Shape-count the ring afterwards (CLAUDE.md's distribution audit): every row's cohesion must
   not collapse to one value, and no line may carry a double space.
