# Squad select — recon

_Live-verified 2026-08-21 against the running game: a fresh Veteran campaign standing at the
pre-Gatecrasher squad select (`Операция "Звездный лик"`, four soldiers, edit/dismiss gated off by
the tutorial objectives). Script claims cite the SDK's `Development/SrcOrig/XComGame/Classes/`;
where the Highlander's rebuild differs it is called out, and every offset below was read off the
**running** class through `/props`, i.e. from a Highlander build._

---

## 1. What it is

`UISquadSelect` — the screen between the Avenger and every mission. It is the last gate before
tactical, and a blind player who cannot get through it cannot play the game at all.

Live stack when it is up (top first):

```
0 UISquadSelect    0x279 inited focused visible
1 UIFacilityGrid   0x019 inited
2 UIAvengerHUD     0x239 inited visible
3 UIEventNotices   0x219 inited
stack: interactive (inputBlocked=0 screensHidden=0 cinematic=0)
```

`UIAvengerHUD` stays underneath because the squad-select nav-help buttons live on **its**
`UINavigationHelp`, not on this screen (§4). `/gui` reports **66 panels**.

Three facts shape everything:

1. **The widget tree holds structure and selection, never words.** Every string a player reads on a
   soldier card goes into Flash through one call — `AS_SetFilled(rank, name, nickname, classIcon,
   rankIcon, edit, dismiss, primaryWeaponName, primaryWeaponLabel, heavyWeaponName,
   heavyWeaponLabel, promote, psi, classStr)` — and is retained nowhere. Same for the mission
   header: `UISquadSelectMissionInfo.UpdateData` is four `QueueString`s and an `EndOp`, and the
   only field it keeps is `SitRepText`. This is the tactical-HUD pattern, not the shell-screen
   pattern: **the companion supplies the words, the widget tree supplies which card and which
   button.**
2. **Vanilla keyboard navigation already works here** — measured live, §4. That makes this screen
   the opposite of the hacking screen: it needs narration first, and native claims for exactly two
   controls that have no keyboard path at all.
3. **Launching the mission is one of those two.** The `НАЧАТЬ ЗАДАНИЕ` button is
   `bIsNavigable = false` and no `OnUnrealCommand` case reaches it on mouse+keyboard. Today a
   sighted player clicks it and a blind player has nothing.

---

## 2. What a sighted player sees

From the live screenshot, top to bottom:

| On screen | Source |
|---|---|
| `Операция "Звездный лик"` | `GeneratedMissionData.BattleOpName` |
| `ЗАДАЧИ:` / `Захватить преобразователь мощности "Адвента"` | `m_strObjectives` + `MissionState.GetMissionObjectiveText()` |
| `СЛОЖНОСТЬ:` / `Легко` (green) | `m_strDifficulty` + `MissionState.GetMissionDifficultyLabel()` — the MISSION's rating, not the campaign's |
| `НАГРАДЫ:` / *(absent here)* | `m_strRewards` + `MissionState.GetRewardAmountString()` — Gatecrasher has none |
| *(absent here)* sit-rep strip | `UpdateSitRep`, the one string this screen retains (`SitRepText`), with a per-line tooltip |
| four soldier cards | one `UISquadSelect_ListItem` each, §3 |
| per card: `НОВОБРАНЕЦ` / `BOSE` | `X2ExperienceConfig.GetRankName(rank, class)` + `GetName(eNameType_Last)`, falling back to first name |
| per card: `KELLY` with `РЯДОВОЙ` above and `РЕЙНДЖЕР` below | nickname is `eNameType_Nick`; the class line prints **only for rank > 0**, deliberately, so a rookie does not read its rank twice |
| per card: `Винтовка` / `ОСНОВНОЕ ОРУЖИЕ` | `PrimaryWeaponTemplate.GetItemFriendlyName()` + `UIArmory_Loadout.m_strInventoryLabels[eInvSlot_PrimaryWeapon]` |
| per card: grenade icon, then a padlock | one `UISquadSelect_UtilityItem` per small slot; the padlock is `SetLocked(m_strNeedsMediumArmor / m_strNoUtilitySlots)` |
| per card: two bars under the card | `AS_SetUnitHealth(cur, max)` and `AS_SetUnitWill(percent, colour)` |
| the focused card's `ИЗМЕНИТЬ` / `УБРАТЬ` pop-up | `m_strEdit` / `m_strDismiss`, greyed when `bDisableEdit` / `bDisableDismiss` |
| bottom centre: `СНЯТЬ ПРЕДМЕТЫ` `ИЗЪЯТЬ ВСЕ ОРУЖИЕ` `СНЯТЬ ВСЮ БРОНЮ` | `UpdateNavHelp` → `UIAvengerHUD.NavHelp.AddCenterHelp` ×3, **mouse-only on PC** |
| bottom right: `НАЧАТЬ ЗАДАНИЕ` | `LaunchButton`, a `UILargeButton`, **mouse-only on PC** |

Not present in this save but part of the screen: the promote chevron (`ShowPromoteIcon`), the bond
icon and its tooltip, the faction icon, the mental-state warning line (`BelowReadyWillState` →
`GetMentalStateLabel`), a heavy-weapon slot with its ability icon, `СОЗДАТЬ ПРЕДМЕТЫ` (after
`T0_M5_WelcomeToEngineering`) and `ОЗДОРОВИТЬ СОЛДАТА` (the Elerium-core recovery boost).

---

## 3. What is readable off the tree

Live offsets, read off the running classes with `/props`:

**`UISquadSelect`** — `m_iSelectedSlot` +0x0410, `m_kSlotList` +0x0420, `LaunchButton` +0x0428,
`XCOMHQ` +0x0440, `UnitPawns` +0x0448, `SoldierSlotCount` +0x0460 (=4), `SquadCount` +0x0464 (=1),
the bool bitfield at +0x0478 (`bLaunched` 0x1, `bReceivedWalkupEvent` 0x2, `bDirty` 0x10,
`bNoCancel` 0x20, `bDisableEdit` 0x40, `bDisableDismiss` 0x80, `bDisableLoadout` 0x100),
`SlotListOrder` +0x0650, `MaxDisplayedSlots` +0x0660 (=6), plus every localized label from
`m_strLaunch` +0x0480 through the strip-gear confirmation texts.

**`UISquadSelect_ListItem`** — `SlotIndex` +0x03D8, the bitfield at +0x03DC (`bDisabled` 0x1,
`bDisabledLoadout` 0x2, `bDisabledEdit` 0x4, `bDisabledDismiss` 0x8), `UtilitySlots` +0x0408,
`BondIcon` +0x0410, `m_eActiveButton` +0x04E0 (`ESquadSelectButton`: 0 Promote, 1 Edit, 2 Dismiss,
3 WeaponPrime, 4 WeaponHeavy, 5 Utility, 6 Bond).

**`UISquadSelect_UtilityItem`** — `SlotIndex` +0x03F0, `SlotType` +0x03F4 (`EInventorySlot`).

So the tree answers *which card, which button, which utility slot, and whether that control is
disabled* — the whole selection model — and answers nothing about the soldier. Names, ranks,
weapons, HP, will and bonds come from `XComGameState_Unit`, reached the way the card itself reaches
it: `ListItem.GetUnitRef()` → `UISquadSelect.XComHQ.Squad[SlotIndex]`. That is the screen's own
source object, so this obeys the read-the-screen rule rather than re-deriving anything.

⚠ **Enumerate the utility slots from the live `UtilitySlots` list, never from
`Unit.HasGrenadePocket()`-style arithmetic.** The Highlander replaces `UpdateData` wholesale
(issue #118) and builds that list through `CHUIItemSlotEnumerator` + `CHItemSlot.SlotIsSmall`, so
the vanilla count is wrong the moment a slot-adding mod is installed. Each item panel already
carries the `SlotType`/`SlotIndex` the enumerator gave it; ask the unit for what is in that slot and
the reader is mod-proof for free. (Issue #1134 also swaps the faction icon for a stacked class
icon — cosmetic for us.)

---

## 4. Input: what vanilla already does — measured, not assumed

`UISquadSelect.OnUnrealCommand` hands the arrows to `m_kSlotList` **before** its own switch
(`:1684`), and `UIList_SquadEditor.OnUnrealCommand` handles `FXS_ARROW_*`, `FXS_KEY_TAB` and
`FXS_KEY_ENTER` with **no `ISCONTROLLERACTIVE` gate**. Confirmed live with `/input`:

| Key | Measured effect |
|---|---|
| `Down` | `m_eActiveButton` 3 (`eSSB_WeaponPrime`) → **5** (`eSSB_Utility`) — 4 skipped, the soldier has no heavy weapon |
| `Right` | focus moved card 1 → card 2 (`m_iSelectedSlot` 2, list `SelectedIndex` 2, `bIsFocused` on that item), carrying the active button across |
| `Left` / `Up` | the mirror; `Left` steps within the utility row first and falls through to the previous card only when the row is exhausted |
| `Tab` | `ShiftFocus(true)` — next card |
| `Enter` | `HandleControllerSelect` → the focused card's `HandleButtonSelect()`, i.e. exactly what clicking that sub-button does |
| `Escape` | `CloseScreen()`, but only `if(!bNoCancel \|\| XComHQ.AllSquads.Length > 0)` — on Gatecrasher `bNoCancel` is true, so Escape is a deliberate no-op |

**What has no keyboard path at all on mouse+keyboard:**

- `НАЧАТЬ ЗАДАНИЕ`. `LaunchButton` is `bIsNavigable = false`; the only launch case in
  `OnUnrealCommand` is `FXS_BUTTON_X`, i.e. controller-only. Its `__OnClickedDelegate__` reads
  `UISquadSelect.OnLaunchMission`, and `UIButton.Click()` (zero-param, `UIButton.uc:365`) invokes
  it through the game's own path — so this is the `/call`-shaped ungate this project keeps meeting.
- The three strip buttons, plus `СОЗДАТЬ ПРЕДМЕТЫ` and `ОЗДОРОВИТЬ СОЛДАТА`. `UINavigationHelp`
  dispatches them from `OnMouseEvent` alone, off a Flash callback index into
  `m_arrButtonClickDelegates`; on PC the controller shortcuts are explicitly not added (the
  `if(!ISCONTROLLERACTIVE)` branch of `UpdateNavHelp` registers them as click-only nav help).
  Their handlers — `OnStripItems()`, `OnStripGear()`, `OnStripWeapons()`, `OnBuildItems()`,
  `OnBoostSoldier()` — are all **zero-parameter functions on the screen**, so each is one
  `CallScriptFunction` away.

---

## 5. Traps

- ⛔ **Visual order is not squad order.** `SlotListOrder = [4, 1, 0, 2, 3, 5]` — "refer to the
  points / camera setup in CIN_PreMission". With a four-soldier squad the cards run left to right
  as squad indices **1, 0, 2, 3**, verified live (list positions 0..3 carry `SlotIndex` 1, 0, 2, 3 —
  the squaddie stands centre stage, not first). A reader must count the **visual** position for
  "2 of 4" and use `SlotIndex` only to look the unit up. Getting this backwards names the wrong
  soldier on every card.
- ⛔ **`m_iSelectedSlot` lags.** It is written from `ListItem.OnReceiveFocus`, which `RefreshFocus`
  defers behind a 0.25 s `DelayedRefreshFocus` timer. Caught live disagreeing with the list
  (`m_iSelectedSlot` 0 while `SelectedIndex` 1) and agreeing again a moment later. The authority is
  the one the game's own `GetActiveListItem()` uses: **the first list item with `bIsFocused`**.
- ⛔ **The screen is deaf until the walk-up matinee.** `OnUnrealCommand` returns early while
  `!bReceivedWalkupEvent` (`:1670`), and that flag is set by the `PreM_LineupUI` remote event once
  the soldiers finish walking up. Announce and claim from that edge, not from `OnInit` — before it
  the cards are hidden and every key is dropped on the floor.
- ⚠ **`ShiftButtonFocus` is an unbounded `do…until(!IsActiveButtonDisabled())`.** Firaxis' own
  comment at `OnInit` ("To Prevent an infinite loop in the tutorial") says what happens when every
  button on a card is disabled. Our layer must never drive that loop into a state the vanilla
  keyboard cannot already reach — which is an argument for narrating vanilla's focus rather than
  writing `m_eActiveButton` ourselves.
- ⚠ **Two `bDisable*` families.** The screen's `bDisableEdit`/`bDisableDismiss`/`bDisableLoadout`
  are copied onto each card as `bDisabledEdit`/`bDisabledDismiss`/`bDisabledLoadout` **and again**
  as `bEditDisabled`/`bDismissDisabled` (`UpdateData` sets both). Read the card's, not the
  screen's — the tutorial disables edit and dismiss per card while leaving the loadout editable,
  which is exactly the live state here.
- ⚠ `bDisabled` on a card (the greyed "increase squad size" slot) is a different thing again:
  `UpdateData` returns immediately for it, so such a card holds stale data and must read as its own
  locked label, never as a soldier.

---

## 6. Adjacent screens this one opens

| From | Screen | Note |
|---|---|---|
| empty card → `ВЫБРАТЬ БОЙЦА` | `UIPersonnel_SquadSelect` | a real `UIList` of `UIPersonnel_ListItem`, each retaining `UnitRef` — structurally the friendliest screen in the flow, and a natural first recipe |
| `ИЗМЕНИТЬ`, primary weapon, a utility slot | `UIArmory_Loadout` | large; its own recon |
| promote chevron | `UIArmory_Promotion` | its own recon |
| bond icon | `UISoldierBondScreen` | |
| the three strip buttons | `UIDialogueBox` | already covered by the existing dialog recipe |
| `ОЗДОРОВИТЬ СОЛДАТА` | a `UIPersonnel` picker plus a confirm dialog | |

---

## 7. The shape it took — a graph recipe, not a bespoke reader

**Status 2026-08-21: BUILT, not yet run live.** `game_ui.cpp`'s `ReadSquadSelect` (structure and
selection, every offset pinned in `sdk_layout_checks.cpp`), `X2AccessSquadReader.uc` (the words),
and the `squad select` recipe in `menu_navigator.cpp`. The C++ half compiles and the kernel tests
pass; the UnrealScript half is **uncompiled** — `build_uscript` hangs forever against a running
game, and the session that built this was mid-mission (see the Build & deploy rule in CLAUDE.md).
First live run needs `just kill && just go-full`, then a mission launch from the Avenger.


The tempting read of §4 is "vanilla navigation works, so this only needs narration". That is wrong,
and the reason is §4's second half: **the controls a sighted player uses most — launch, and the
three strip buttons — are not on vanilla's focus path at all.** A narrate-what-vanilla-focuses
reader must therefore spend one claimed hotkey per unreachable control, on a screen that also wants
a mission-header key; in a graph they are simply four more nodes in a third region, costing no keys
and arriving with position announcements and stop memory for free. So: **a `menu_navigator.cpp`
recipe**, like the shell screens, with the companion supplying the words the way tactical does.

**Structure** (`GraphBuilder`: parent chains for context, `StopKey` for Tab, `RegionKey` for
Ctrl+arrows):

| Stop / region | Nodes |
|---|---|
| `mission` | operation name, objective, difficulty, rewards, sit-rep — non-activatable |
| `squad` | one non-focusable parent per card carrying *rank, name, nickname, class, HP, will, status* (the announcer reads it on entering the card and stays quiet while moving inside it), children in vanilla's own vertical order: promote, edit, dismiss, primary weapon, heavy weapon, the utility row (horizontal siblings), bond |
| `actions` | launch, strip items, strip weapons, strip gear, build items, boost soldier |

**The recipe owns the arrows and writes focus back.** Attaching claims the arrows, so vanilla's
`ShiftFocus*` never runs — which also sidesteps its unbounded skip loop (§5). To keep the Flash
highlight and the pop-up card menu in step for anyone watching, mirror each focus move into the
game the way its own controller path does: `ListItem.OnReceiveFocus()` plus
`SetSquadSelectButtonFocus(int ButtonEnumAsInt, bool bReverseFocusIfUtility)` — the one call here
that takes parameters, so it is a native `CallScriptFunction` and not a `/call`.

**Activate through the widget's own handler, never through `HandleButtonSelect()`.**
`HandleButtonSelect` dispatches on `m_eActiveButton`, i.e. on the game's idea of focus rather than
ours, so a one-frame disagreement fires the wrong verb — dismiss instead of edit. Every leaf has a
direct, zero-parameter entry point: `OnClickedPromote()`, `OnClickedEditUnitButton()`,
`OnClickedDismissButton()`, `OnClickedPrimaryWeapon()`, `OnClickedHeavyWeapon()`,
`OnClickedSelectUnitButton()`, `UtilityItem.Button.Click()`, `LaunchButton.Click()`,
`OnStripItems()` / `OnStripGear()` / `OnStripWeapons()` / `OnBuildItems()` / `OnBoostSoldier()`.

**Declare disabled controls, do not skip them.** Vanilla's keyboard skips a greyed `ИЗМЕНИТЬ`;
a sighted player still *sees* it. Parity says the node exists and announces `Enabled` + `Reason` —
and for launch that reason is the game's own `GetTooltipText()` (`НЕДОСТАТОЧНО БОЙЦОВ`, or the
mission's `CanLaunchMission` fail string).

**Gates.** Do not attach while `!bReceivedWalkupEvent` (§5) — the cards are hidden and the data is
still settling. Rebuild the render when `bDirty` flips or a card's `UpdateData` runs; `GraphRender`
is per-operation anyway, so the callbacks re-read live state on every keypress by construction.

Then **`UIPersonnel_SquadSelect`** as a second recipe, so a squad can be built and not merely
inspected — its `UIList` of `UIPersonnel_ListItem` (each retaining `UnitRef`) is the easiest recipe
in this flow.

This is worth building before the Avenger recon's own phases: the screen is on the critical path to
every mission, and unlike the Avenger it does not need a mid-campaign save to study.

---

## 8. Open questions for a second pass

1. Everything gated off in this save: promote chevron, bond icon, faction icon, heavy weapon,
   mental-state warning, `СОЗДАТЬ ПРЕДМЕТЫ`, `ОЗДОРОВИТЬ СОЛДАТА`, the rewards line and the sit-rep
   strip. Each needs one live look at a mid-campaign squad select.
2. Empty slots — what the card reads as, whether `HandleEmptyListItemFocus` changes the focus
   model, and whether `Enter` on one really lands in `UIPersonnel_SquadSelect`.
3. Multi-squad missions (`SquadCount > 1`, the `СЛЕДУЮЩИЙ ОТРЯД` button) — untested, and the launch
   button changes meaning there.
4. Whether `Enter` on a *disabled* sub-button is silent or plays the game's own refusal, which
   decides whether we speak the reason ourselves.
5. Harness gap noticed while measuring: `/props` prints the elements of object arrays but not of
   scalar ones, so `SlotListOrder` reads as `6 x IntProperty` with no values — the one number in
   this screen's model that had to come from `SrcOrig` rather than from the running process.
6. **`/call` refuses a function whose only "parameter" is its return value**, which is how it
   turned down `UIButton.Click()` — the exact ungate this endpoint exists for. The guard should
   test the parameters that are not `CPF_ReturnParm`: ProcessEvent writes the return straight into
   our block (`Parms + ReturnValueOffset`), so a zero-argument call is safe whether or not it
   answers. `ReadFunctionParmsRaw` already records `isReturn`; only the `parmCount > 0` test in
   `GuiDump::Call` needs to change, plus a parameter block sized to hold the return.
   Pressing `НАЧАТЬ ЗАДАНИЕ` for the 2026-08-21 session therefore went through Frida instead —
   one `Interceptor.attach` on `ProcessEvent`, calling `Click()` on the game thread from inside the
   first hit and standing down (`ret 1`, screen popped, `UIDropShipBriefing_MissionStart` up). That
   is the workaround, not the fix: a rebuild would have cost the live screen.
