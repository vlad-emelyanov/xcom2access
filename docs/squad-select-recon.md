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

**Status 2026-08-21: BUILT AND RUN LIVE** (`Операция "Божественный обвал"`, four rookies).
`game_ui.cpp`'s `ReadSquadSelect` (structure and selection, every offset pinned in
`sdk_layout_checks.cpp`), `X2AccessSquadReader.uc` (the words), and the `squad select` recipe in
`menu_navigator.cpp`. The first run attached on both entries, read the mission header and every
card, and launched the mission — and found five defects, all fixed the same day. They are worth
recording because four of the five are the SAME defect wearing different clothes: **a field looked
up in the wrong place answers "" and an empty answer is indistinguishable from "the game is not
showing this"**, which is the exact ambiguity the drop-empties wire format was built on.

1. ⛔ **The four screen-wide labels live on the MISSION record and were read off the CARD.**
   `edit`, `dismiss`, `promotelabel` and `selectunit` are class defaults — identical for every
   card, so the companion sends them once. `SquadControlLabel` asked the card for them, got "",
   and the build skips an unlabelled node. **So the Edit, Dismiss and Promote nodes did not exist
   at all**, and an empty slot announced nothing but "button": a whole live run offered a weapon
   and its utility slots and nothing else, and read as a screen with three controls per soldier.
   The empty label was silent in both directions — nothing logged, nothing spoken.
2. ⛔ **`util` was dropped when empty, so the utility slots shifted.** `AddField` drops empty
   values by design; `util` is the one field that REPEATS, and native indexes it by list position
   (`SquadControl::utilityIndex`). A locked slot in front of an occupied one therefore made the
   occupied one answer for the pocket in front of it and pushed the last slot off the end into the
   `Utility slot` fallback. It happened not to bite on this squad — a rookie's second slot is
   locked and comes LAST — which is exactly why it needed finding by reading rather than by
   listening. `AddPositionalField` now emits the blank.
3. A nickname that `CleanText` reduced to nothing still left its separator behind, because the
   companion pre-joined rank/name/nick/class into one `who` field: every rookie read
   *"Новобранец, Sokolova,,"*. The four are separate fields now and native joins them AFTER
   cleaning each.
4. The header labels were English constants against localized values —
   *"objective, Уничтожить передатчик"*. They come from `UISquadSelectMissionInfo`'s own
   `m_strObjectives` / `m_strDifficulty` / `m_strRewards` now, the same rule
   `X2AccessHelper.GetInputVocabulary` exists to enforce on the tutorial side.
5. Every rookie's card header ended in *"ВЗАИМОСВЯЗЬ СОЛДАТ"* — `UISoldierBondScreen.BondTitle`,
   spoken for the state in which `UpdateData` leaves the bond icon at `SetBondLevel(0)` with
   `AnimateCohesion(false)`, i.e. unlit and passed over by a sighted eye. That state gets no words
   now; the icon's own node still carries the title as its label, which is the parity a mouse
   player has.

**⚠️ Not a defect, and the run's most useful negative result.** The session reported a frag grenade
still being named after a medikit had been equipped for it. The reader's chain is live at every
keypress — `KeyGraph::Move` re-renders, `BuildSquadSelect` re-calls `DescribeSquad`, and the
companion resolves the item through `XCOMHISTORY` exactly as `UpdateData` does — so a stale answer
has nowhere to come from. What the log shows in that 15-second window instead is
`UISquadSelect` losing focus with **five Escapes and two Enters passed straight to the game**:
`UIArmory_Loadout` has no recipe and no listener, so the swap was attempted with **zero feedback of
any kind**. The likeliest reading is that it never landed. That makes the loadout screen the
highest-value next recipe in this flow — ahead of `UIPersonnel_SquadSelect` — because it is the one
screen this one hands the player to. ✅ **BUILT the same day: see `docs/loadout-recon.md`.**


The tempting read of §4 is "vanilla navigation works, so this only needs narration". That is wrong,
and the reason is §4's second half: **the controls a sighted player uses most — launch, and the
three strip buttons — are not on vanilla's focus path at all.** A narrate-what-vanilla-focuses
reader must therefore spend one claimed hotkey per unreachable control, on a screen that also wants
a mission-header key; in a graph they are simply four more nodes in a third region, costing no keys
and arriving with position announcements and stop memory for free. So: **a `menu_navigator.cpp`
recipe**, like the shell screens, with the companion supplying the words the way tactical does.

**Arrows follow the screen's own geometry** (changed 2026-08-21 after the live run): the cards sit
SIDE BY SIDE and each stacks its controls vertically, so **Up/Down walks one soldier's card and
Left/Right steps to the soldier beside them at the same height**. The first build let
`GraphBuilder`'s automatic single-item chain wire it, which runs Down out of the bottom of one card
and into the top of the next — one long list, and the column the screen actually draws is lost. The
squad stop now wires all four directions EXPLICITLY with `Connect` (raw edges are applied after the
menu pass, so they win), and a direction with nowhere to go points **at itself**: `KeyGraph::Move`
returns not-moved on `dest == node`, which is how a card's top and bottom stop without wrapping.
Cards are ragged — a Reaper has one utility slot where a Grenadier has three, and only some
soldiers can be promoted — so Left/Right clamps to the neighbour's last control rather than falling
off its end.

**Structure** (`GraphBuilder`: parent chains for context, `StopKey` for Tab, `RegionKey` for
Ctrl+arrows):

| Stop / region | Nodes |
|---|---|
| `mission` | operation name, squad size, objective, difficulty, rewards, sit-rep and its effect text — non-activatable, each with the game's own label |
| `squad` | one non-focusable parent per card carrying *rank, name, nickname, class, HP, will, mental state, psi gift, faction, bond state* (the announcer reads it on entering the card and stays quiet while moving inside it), children in vanilla's own vertical order: promote, edit, dismiss, primary weapon, heavy weapon, the utility slots, the bond icon, and — only for a soldier the game marks tired — the flyover warning as a read-only tail |
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

## 7a. Parity audit — everything the screen draws, and where we get it

Read off `UISquadSelect_ListItem.UpdateData` and `UISquadSelectMissionInfo` line by line, so the
list is the screen's own inventory rather than a sketch of one. ✅ covered, ⚪ deliberately not.

| On screen | How it is drawn | Ours |
|---|---|---|
| rank, name, nickname, class | `AS_SetFilled` lines 1-3 + `ClassStr` | ✅ card header, four fields joined after cleaning |
| class icon, rank icon | images | ⚪ the words beside them say the same thing |
| health bar | `AS_SetUnitHealth` | ✅ `N of M health` |
| will bar | `AS_SetUnitWill` | ✅ percent |
| mental-state warning | `AS_SetUnitStatus`, only below ready will | ✅ label in the header |
| tired flyover | `CreateFlyoverTooltip`, only at `eMentalState_Tired` | ✅ tail node inside the card |
| psi-gift markup | `PsiMarkup.SetVisible(HasPsiGift())` — icon only | ✅ the Psi Operative class name, the game's own word for what the icon means |
| faction crest | `AS_SetFactionIcon` — icon only | ✅ `GetFactionTitle()` |
| promote chevron | `AS_SetFilled`'s `PromoteLabel`, only when `ShowPromoteIcon()` | ✅ node |
| edit / dismiss | `AS_SetFilled`, greyed via `disableEdit`/`disableDismiss` | ✅ nodes, announced when greyed |
| primary weapon + label | `AS_SetFilled` | ✅ node |
| heavy weapon + desc | `AS_SetFilled`, hidden by `AS_HasHeavyWeapon(false)` | ✅ node, emitted only when the widget is |
| heavy weapon ability icon | `UIIcon` in `AbilityIcons` | ⚪ the weapon's name carries it |
| utility slots | the icon strip, padlock per locked slot | ✅ one node each, item name or the lock's own reason |
| bond icon | level, bondmate-on-squad, cohesion animation, tooltip | ✅ node labelled from its tooltip, state in the header |
| operation name, objective, difficulty, rewards | `UISquadSelectMissionInfo.UpdateData` | ✅ mission stop, labels included |
| sit-rep name | `UpdateSitRepPanel` | ✅ |
| sit-rep effect | the sit-rep panel's TOOLTIP | ✅ rebuilt the way `UpdateSitRep` builds it |
| filled slots at a glance | six cards side by side | ✅ `SOLDIERS, 4 of 6` — an aid, not a widget: serially the empty slots are several presses apart |
| launch button | `UILargeButton`, `bIsNavigable` false | ✅ node + its refusal tooltip |
| strip / build / boost verbs | nav-help, mouse-dispatched only | ✅ nodes |

⚪ **Armour is deliberately absent** and that is parity, not a gap: the card never draws it. It
lives one screen deeper, in `UIArmory_Loadout` — which nothing narrates yet.

---

## 8. Open questions for a second pass

1. Still unexercised, because this save has none of them: promote chevron, bond icon, faction
   icon, heavy weapon, mental-state warning and its flyover, `СОЗДАТЬ ПРЕДМЕТЫ`,
   `ОЗДОРОВИТЬ СОЛДАТА`. Each needs one live look at a mid-campaign squad select — and the
   utility-slot fix above matters most for a Grenadier, whose grenade pocket is the case where
   the old positional bug actually bit.
2. Empty slots — what the card reads as, whether `HandleEmptyListItemFocus` changes the focus
   model, and whether `Enter` on one really lands in `UIPersonnel_SquadSelect`.
3. Multi-squad missions (`SquadCount > 1`, the `СЛЕДУЮЩИЙ ОТРЯД` button) — untested, and the launch
   button changes meaning there.
4. Whether `Enter` on a *disabled* sub-button is silent or plays the game's own refusal, which
   decides whether we speak the reason ourselves.
5. Harness gap noticed while measuring: `/props` prints the elements of object arrays but not of
   scalar ones, so `SlotListOrder` reads as `6 x IntProperty` with no values — the one number in
   this screen's model that had to come from `SrcOrig` rather than from the running process.
6. ✅ **FIXED 2026-08-21.** `/call` refused a function whose only "parameter" was its return
   value, which is how it turned down `UIButton.Click()` — the exact ungate the endpoint exists
   for. The guard now counts only the parameters that are not `CPF_ReturnParm`, the block is
   sized to hold the return, and the answer is printed through the same formatter `/props` uses.
   Pressing `НАЧАТЬ ЗАДАНИЕ` for the 2026-08-21 session therefore went through Frida instead —
   one `Interceptor.attach` on `ProcessEvent`, calling `Click()` on the game thread from inside the
   first hit and standing down (`ret 1`, screen popped, `UIDropShipBriefing_MissionStart` up). That
   is the workaround, not the fix: a rebuild would have cost the live screen.
