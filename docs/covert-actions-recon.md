# Covert actions — `UICovertActions` recon

Survey date 2026-08-24, read off `Development/SrcOrig` (retail WOTC), the Highlander's `Src/`, the
generated SDK's live-dump offsets, and `XComGame.int`. Nothing here has been measured in a running
game yet — §9 is the list of things the first live pass has to answer.

`docs/ui-inventory.md:172` lists this as **P1 #9**, the WOTC core strategy loop. Two of the four
screens in the family are already covered by existing recipes, so this is two new surfaces, not
four:

| Screen | Role | Status |
|---|---|---|
| `UICovertActions` | the picker | ⬜ this document |
| `UICovertActionReport` | the completion report | ⬜ §8 |
| `UIPersonnel_CovertAction` | the soldier picker | ✅ personnel recipe (family via class chain) |
| `eAlert_ConfirmCovertAction` | the confirm modal | ✅ popup recipe |

## 1. What the screen is

`class UICovertActions extends UIScreen` (`:10`), package
`/ package/gfxXPACK_CovertOps/XPACK_CovertOps`, `LibID = "CovertOpsScreen"`,
`InputState = eInputState_Consume`.

Reached only from the Resistance Ring: `UIFacility_CovertActions.OnViewActions()` →
`` `HQPRES().UICovertActions() ``. Pick a faction's operation, staff it, optionally spend
resources to buy risks down, confirm; it then runs for N days and blocks every other covert action
until it completes.

`FindActions()` (`:113`) collects every `XComGameState_CovertAction` the faction still owns and
sorts it four times — by faction name, farthest faction, faction met, started — so a running
action is always row 0. `ActionRef` starts at `arrActions[0]`.

## 2. What a sighted player is looking at

Seven regions, all of them the selected action's shadow.

| Region | Contents | Built by |
|---|---|---|
| List (left, 438×638) | faction group headings + `"(NEW) " $ ActionObjective` rows, greyed with a why-not tooltip | `BuildScreen` `:317` |
| Detail card | image, display name, pre-mission narrative, `REWARD:` + preview + details, `LOCATION:` + region | `AS_SetInfoData` `:630` |
| Lock overlay | `OPERATION UNAVAILABLE` + the same reason | `AS_SetLockData` `:661` |
| Duration | `DURATION:`/`TIME REMAINING:` + `12 Days`, green when bondmates shortened it | `AS_UpdateCost` `:596` |
| Faction panel | leader portrait, faction title, faction name in the faction colour, `INFLUENCE` + level | `AS_SetFactionData` `:583` |
| Risks | ≤4 × (risk name, `Low`/`Moderate`/`High`) + a free-text ambush warning | `AS_SetRiskRow` `:614` |
| Slots | ≤4 cards: staff slots then cost slots | `AS_SetSlotData` `:640` |
| Confirm | `CONFIRM ACTION`, disabled until `CanBeginAction()` | `RefreshConfirmPanel` `:421` |

The two disabled reasons are the whole vocabulary: *"Another Covert Action is in progress."* and
*"Requires higher Influence with this Faction."*

**A staff slot** reads `REQUIRED:`/`OPTIONAL:` (+ `FAMOUS`) + the slot's `GetNameDisplayString()`,
a `Soldier Reward: …` preview, portrait/rank/class icons once filled, a `+ ADD SOLDIER` /
`+ ADD ENGINEER` / `+ ADD SCIENTIST` / `CLEAR` button, an `EDIT` second button when the action can
be ambushed, and *"No appropriate unit available"* when the roster cannot fill it
(`UICovertActionStaffSlot.UpdateData`).

**A cost slot** reads the cost (`50 Intel`), the reward it buys — coloured by affordability — and
`ALLOCATE`/`CLEAR`, or *"Insufficient resources"* (`UICovertActionCostSlot.UpdateData`).

⚠ **The risk PERCENTAGE is never shown.** `GetRiskLevel` (`XComGameState_CovertAction.uc:1070`)
buckets `ChanceToOccur + ChanceToOccurModifier` through
`X2StrategyGameRulesetDataStructures.RiskThresholds` into `CovertActionRiskLabels` = `Low` /
`Moderate` / `High`. Sighted parity means we speak the bucket. Reading `ChanceToOccur` would be the
initiative-order mistake again.

## 3. The three-way split: what is readable how

**Tier 1 — retained, native reads it directly.** The action rows are plain `UIListItemString`:
`InitListItem` → `SetText` (`UIListItemString.uc:154`), so `Text` and `HtmlText` really are
retained, along with `bDisabled` and the tooltip `SetDisabled` parked on `ButtonBG`. And
`Item.metadataInt = arrActions[idx].ObjectID` (`:368`) is a **real join key**, written
unconditionally for every action row — no positional coupling with the companion, and none of the
locked-card hazard commit `0364ec7` records.

This is the `UIChooseFacility` shape, not the `UIPersonnel_ListItem` shape. `game_ui.cpp` already
pins the offsets it needs (`kListItemStringFlagsOffset = 0x414`).

**Tier 2 — nothing at all.** Every other string on the screen is an `MC.BeginFunctionOp` pipe with
locals on both sides. Worse than the usual case: `UICovertActionSlot extends Object` (`:8`) — the
slots are **not `UIPanel`s**, so `/gui` will not even list them. The whole widget tree under this
screen is: the screen, `List` + its item panels, `SlotContainer` (`infoPanelMC`) with four empty
`slotMC_N` panels, and `LaunchButton`. The faction group headings are `UICovertOpsFactionListItem`
and push through `MC.BeginFunctionOp("populateData")`, so they are wordless too.

**Tier 3 — re-derivable off state the screen itself cached.** `arrActions` is an
`array<XComGameState_CovertAction>` **on the screen** and every displayed string has a named
accessor on those objects: `GetObjective`, `GetDisplayName`, `GetNarrative`,
`GetRewardDescriptionString`, `GetRewardDetailsString`, `GetLocationDisplayString`,
`GetDurationString`, `GetRisksStrings`, `GetFaction().GetInfluenceString()`. That is tier 2 of the
read-the-screen rule verbatim — *the state objects the screen itself cached to populate Flash* — so
a companion reading off `Screen.arrActions` rebuilds the entire card with no `XCOMHISTORY` lookup.

⛔ **The Flash command-queue capture is the wrong tool here**, unlike `UIAlert`. The words are
trivially re-derivable, and the capture knows only what was pushed — never which slot is disabled,
which risk was negated, or whether a cost is affordable.

## 4. ⛔ Two controls are unreachable from a keyboard

`OnUnrealCommand` (`:1040`–`:1095`):

| Key | Effect |
|---|---|
| `B` / `ESCAPE` / `RMB` | leave slot mode if in it, else `CloseScreen()` |
| `A` / **`ENTER`** / **`SPACEBAR`** | `SelectStaffSlots()`, or click the focused slot's `theButton` |
| `Y` | `EditSoldierLoadout()` — **gamepad only** |
| `X` | `OnLaunchClicked(none)` — **gamepad only** |

- **`CONFIRM ACTION` cannot be pressed on mouse+keyboard.** `LaunchButton.DisableNavigation()`
  (`:433`) takes it out of the navigator, and the only key path to `OnLaunchClicked` is
  `FXS_BUTTON_X` (`:1087`) — which `DefaultInput.ini` produces **only** from `XboxTypeS_X`
  (`X_Button_Press`, `XComPlayerController.uc:608`). There is no keyboard binding in any input
  section. A keyboard-only player literally cannot launch a covert action.
- **`EDIT` (loadout) is gamepad-only** for the same reason.
- Slot mode itself **does** work on keyboard: `:1074` is not `` `ISCONTROLLERACTIVE ``-gated. Only
  the nav help that advertises it is (`RefreshNavigation` `:463`).

This is the shape `/call` exists for: the game already has the function and calls it from a branch
mouse+keyboard never takes. `UIButton.Click()` (`UIButton.uc:365`) self-gates on
`IsDisabled && bIsVisible`, and `UpdateData` genuinely maintains `LaunchButton.Show()/Hide()`
(`:412`), so `LaunchButton.Click()` is the game's own path.

## 5. ⛔ Browsing the list is DESTRUCTIVE

`SelectedItemChanged` (`:883`) calls `ClearSlots()` **before** switching `ActionRef`, and
`ClearSlots` (`:819`) submits a real game state that runs `EmptyAllStaffSlots` +
`ResetAllCostSlots` on the **outgoing** action. So moving off an action you have staffed throws the
staffing away.

That is vanilla — a sighted player clicking around gets it too — but our focus write-back would
fire it on **every arrow press**. The hard rule (*prefer the game's own path*) says mirror it, so:
write back, and **announce the loss** on the `Alert` channel when the outgoing action had filled
slots. Decoupling instead is the trap: the confirm handler acts on the screen's own `ActionRef`, so
a focus that disagreed with it would be the `UISaveGame.OnAccept` hazard with a campaign-scale cost.

## 6. Two vanilla bugs

**6.1 `RefreshRisksPanel` bounds the risk rows by the wrong array.** `:524`:

```unrealscript
GetActionRisks(Labels, Values);
for (idx = 0; idx < 4; idx++)
{
    if (idx < arrActions.Length)                       // <- Labels.Length was meant
        AS_SetRiskRow(idx, Caps(Labels[idx]), Caps(Values[idx]));
    else
        AS_SetRiskRow(idx, "", "");
}
```

`arrActions` is the number of **actions on offer**, not the number of risks. It usually works by
accident because ≥4 actions are normally listed; when fewer are, the extra risks are blanked and
hidden from sighted players too. The Highlander does **not** fix it — its only change to this file
is issue #1035 (`SelectedIndex` → `SlotContainer.Navigator.SelectedIndex`, twice).

**Decision for the first pass: speak all of `GetRisksStrings`.** This one goes the opposite way to
the WOTC initiative order, and the reason is the direction of the error: the initiative overlay is
*deliberately* cheat-gated, this is a typo that removes information the design intends to show.
Flag it in the live pass if the count ever disagrees with what the user sees.

**6.2 `UIFacility_CovertOpsStatus.Label` is declared and never assigned** (`:9`, offset 0x0408).
It reads `""` at runtime — same family as the `UITacticalHUD_InfoPanel` caption trap, and the
reason the status readout in §7 is re-derived rather than read off the panel.

## 7. ⚠ Layout note: this class is a counter-example to "the Highlander appends"

`XComGameState_CovertAction` is one of the few places where the Highlander does **not** append.
`AmbushMissionSource` (issue #485) and `RewardsNotGivenOnCompletion` (issue #810) are declared at
`Src/.../XComGameState_CovertAction.uc:48` and `:59` — *before* the struct declarations — so every
array after them moves. In the live dump:

```
0x00EC  bAvailable/bStarted/bCompleted/bAmbushed/bNeedsActionCompletePopup/
        bNeedsAmbushPopup/bNewAction/bBondmateDurationBonusApplied  (bits 0x01..0x80)
        RewardsNotGivenOnCompletion                                 (bit 0x100, HL-only)
0x0100  EndDateTime          0x0110  HoursToComplete
0x0114  Faction              0x0118  LocationEntity     0x011C  RewardRefs
0x012C  RequiredFactionInfluence
0x0164  AmbushMissionSource  (HL-only)
0x016C  StaffSlots           0x017C  CostSlots
0x018C  Risks                0x019C  NegatedRisks
```

A vanilla-derived dump puts `StaffSlots` and everything after it eight bytes low. This is exactly
the case `just sdk-adopt`'s vanilla refusal exists for.

Other offsets, from the same Highlander-derived dump:

```
UICovertActions            ActionRef 0x0580   arrActions 0x0584   NewActionFactions 0x0594
                           flags 0x05A4 (bActionInProgress 0x01, bInstantInterp 0x02,
                                         bShowCosts 0x04, bHasRewards 0x08, bHasRisks 0x10,
                                         bIsSelectingSlots 0x20, bTutorialPopupDisplayed 0x40)
                           List 0x05A8   LaunchButton 0x05B0   SlotContainer 0x05B8
UICovertActionSlotContainer ActionSlots 0x03D8   ActionSlotPanels 0x03E8
UICovertActionSlot (UObject) ActionRef 0x0060  SlotContainer 0x0064
                           flags 0x006C (bSizeRealized 0x01, IsDisabled 0x02)
                           MCIndex 0x0070  eState 0x0074
                           PromoteLabel 0x0078  Value1 0x0088 … Value4 0x00B8
UICovertActionStaffSlot    StaffSlotRef 0x0108  RewardRef 0x010C
                           flags 0x0110 (bOptional 0x01, bFame 0x02)
                           StaffIndex 0x0114   UnitRef 0x0164
UICovertActionCostSlot     Cost 0x0108  RewardRef 0x0128  CostIndex 0x012C
UIListItemString           metadataInt 0x03D8  Text 0x03F4  flags 0x0414 (already pinned)
UIFacility                 m_kCovertOpsStatus 0x0628
```

## 8. Proposed shape — six stops, one graph, no new kernel primitives

Everything maps onto primitives `src/Graph/` already has. Focus **starts on the list**, because
that is where the game puts it.

```
[list]     PushContext(factionTitle) per faction, action rows inside
[card]     name / narrative / location / duration / reward / reward details / lock reason
[faction]  title, faction name, influence
[risks]    ≤4 × "name, level"  + the ambush warning
[slots]    ≤4 × slot: label, value, Enter = add/clear, Backspace = edit loadout
[confirm]  CONFIRM ACTION (+ Reason part when disabled)
```

- **Faction headings → `PushContext`.** They are non-focusable pure structure, announced once on
  entry — which is exactly what `listHeader.DisableNavigation()` (`:364`) already makes them.
- **Risks are a LIST, not a `GraphSheet`.** Two facts, no drawn column headings, and
  *"Soldier captured, moderate"* is a complete sentence. The sheet rule is *reach for a sheet
  whenever a screen draws column headings*; this screen draws none.
- **Flatten the 2×2 slot grid into four rows.** The game wires slots as a grid
  (`SelectStaffSlots` `:920`ff) but geometry is not information, and menu mode gives the flat list
  for free.
- **Skip the game's `bIsSelectingSlots` mode entirely.** Our recipe owns the arrows, so slots can
  be ordinary nodes in one graph and activation calls
  `SlotContainer.ActionSlots[i].HandleClick("theButton")` from the companion. Inheriting a second
  input mode where Escape means something different buys nothing.
- **Slot verbs → `OnActivate` + `OnSecondary`.** Two verbs, so a `FlyoutKeyOnly` column would be
  ceremony; keep the flyout in reserve if a mod ever adds a third.
- **Disabled reasons → `AnnouncementKinds::Reason`**, which the kernel already orders immediately
  after the label.

Native reads the list (tier 1) and the structure; one companion — `X2AccessCovertActionReader` —
owns every word in tiers 2 and 3, joined on `metadataInt`. Two verbs the companion must re-provide
because the keyboard cannot reach them: `LaunchButton.Click()` and `HandleClick("theButton2")`.

## 8b. `UICovertActionReport` — built 2026-09-03 (`covert report`), unexercised

Same slot container, different consumer. `UICovertActionSlot.Update` takes the **report** branch
there (`UICovertActionSlot.uc:57`), which passes `PromoteLabel` / `Value1..Value4` — and those
**are** member vars, so the per-soldier results really are retained on the slot objects (reachable
through `SlotContainer.ActionSlots`, which is reflection over a `UObject` array, not the widget
tree). The rest — header, action name, post-mission `GetSummary()`, reward, faction — is the same
Flash pipe and the same re-derivation. `ContinueButton` is `DisableNavigation()`ed only in the
controller branch, so on mouse+keyboard it stays navigable.

What the build settled, from a full read of the class:

- **When it exists.** One spawn site, `XComHQPresentationLayer.UIActionCompleted`, reached only
  from the geoscape tick: the action's clock runs out → `StartActionCompleteSequence` → camera pan
  → `ShowActionCompletePopups` → `TriggerActionCompletePopup`. Never from the Ring, a map pin or
  the event queue (those open the PICKER). ⛔ The rewards are GRANTED before the screen opens
  (`CompleteCovertAction` → `GiveRewards`): it is a receipt.
- ⛔ **Escape is Continue.** `OnUnrealCommand` answers A, Enter, Space AND Escape with one call,
  `OnContinueClicked` → `CloseScreen` + `UIActionCompleteRewards`, which raises the reward popups
  and then `RemoveEntity`s the action — `GetAction()` answers none from then on. No abort, no
  second verb; the companion returns empty after that point rather than dereferencing.
- ⛔ **No slot verbs.** `HandleClick` gates on `!bStarted` (staff) / `!bCompleted` (cost), both
  false forever on a completed action. `IsDisabled` is never set here either (`DisableAllSlots`
  is the picker's). `ClickCovertActionSlot` / `EditCovertActionLoadout` are not offered.
- **Two widgets only**: `ContinueButton` (a `UILargeButton` whose `Text` really is retained —
  `m_strGenericContinue`, icon-injected on a controller) and `SlotContainer`, whose
  `ActionSlotPanels` is EMPTY on this screen (`InitSlotContainer` spawns the `slotMC_N` panels
  under `UICovertActions` alone). `/gui` shows two nodes.
- **`GetRewardString()` means the GRANTED values** (`GetRewardValuesString`) here, the preview
  (`GetRewardDescriptionString`) on the picker. The companion asks the screen, never copies the
  picker's line.
- **The per-soldier card** is `Label` (a local — re-derived from `StaffSlotRef` as
  `GetNameDisplayString`, or `GetStrategyCostString` for a cost slot) plus the retained five:
  `PromoteLabel` (PROMOTE when `ShowPromoteIcon`), `Value1` Gained XP, `Value2` cohesion (or the
  reward when nobody else went), `Value3` the reward, `Value4` in RED — `GetStaffRisksAppliedString`
  or SOLDIER KILLED; a captured soldier gets `Value4` alone. ⛔ Read OFF THE SLOT, never
  recomputed: the Highlander's #810 (`RewardsNotGivenOnCompletion`) changes the XP/cohesion
  branch, and reading the retained values is right under both builds for free. Only slots that
  had somebody in them are built (`ShouldStaffSlotBeDisplayed` hides unfilled ones once
  `bCompleted`).
- `CovertOpsReport_Ambushed` looks unreachable in the shipped flow (`bAmbushed` is cleared in the
  same `Update` pass that raises the completion flag); the description accessor is called as
  written, so a mod that sets the flag is still read.
- **Offsets** (`AUICovertActionReport`, packed): `ActionRef` 0x440, `ContinueButton` 0x444,
  `SlotContainer` 0x44C. Pinned. Nothing else on the class is read natively.

As built: three stops — the card (name, narrative, REWARD: line, faction), RESULTS: one node per
slot card with PROMOTE as an attention part and the four value lines as parts, then CONTINUE
over the screen's own Enter. Named `COVERT ACTION REPORT:` + the action.

First live pass: (1) a completed two-soldier action must attach on the geoscape after the camera
pan and read the card, then Tab to RESULTS with one node per soldier carrying Gained XP and the
cohesion line; (2) an action with a risk that fired must put the risk's name LAST on that
soldier's node; (3) Enter on CONTINUE must raise the reward popups (popup recipe) and `just
dev-log` must carry no companion error after it; (4) `/gui` must show exactly two nodes.

## 9. What the first live pass must answer

1. `just dev-gui` on the screen — confirm the tree is as small as §3 predicts, and that the four
   `slotMC_N` panels exist with no text.
2. `just dev-props "screen=UICovertActions"` — confirm `arrActions`, `ActionRef` and
   `bIsSelectingSlots` against the offsets in §7, on a Highlander-loaded game.
3. Does `Enter` on the list really enter slot mode on a keyboard, and does `Escape` leave it
   without closing the screen? §4 says yes from the source; it has never been pressed.
4. Does `LaunchButton.Click()` reach `ConfirmAction()` → the `eAlert_ConfirmCovertAction` popup,
   and does the popup recipe read that alert? (It is a `BuildUIAlert` variant, so it should.)
5. §5: arrow down the list with one action staffed, and check the slots really do empty — then
   check the announce.
6. §6.1: count the risks the screen draws against `GetRisksStrings` on an action with 3 risks and
   fewer than 3 actions listed.
7. `UIPersonnel_CovertAction` — the personnel recipe should already speak the ineligible soldiers
   as disabled (native reads `UIButton::IsDisabled`, `game_ui.cpp:4759`; the recipe consumes it at
   `recipe_avenger.cpp:1027`). Confirm it on this door, since it is the first one where the
   *majority* of the roster is disabled.
8. Shape-count the speech ring afterwards, per the distribution rule — in particular the risk
   levels, which must not collapse to one value.
