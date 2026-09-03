# The armory hub and its two item screens — recon (2026-09-03, built from source, unexercised)

`UIArmory_MainMenu` (the hub every soldier operation routes through), `UIArmory_WeaponUpgrade`
and the PCS path (`UIInventory_Implants`, and the shipped-but-dead `UIArmory_Implants`). Read
against `SrcOrig` and the Highlander-derived SDK dump; nothing here has run live. The loadout
(`loadout-recon.md`) and the promotion screens were the neighbours copied from.

⛔ **The SDK dump is Highlander-derived and for `UIArmory_MainMenu` the Highlander is a near-total
rewrite of the class (Issue #47).** `AUIArmory_MainMenu` in the dump carries `CustomizeButton …
DismissButton` and `CheckForDisabledListItem` / `OnCustomize` … — none of which exist in
`SrcOrig`. The mod requires the Highlander, so the reader is written against ITS class, and the
one place vanilla would differ (the eight fields absent, `PromoteItem` live instead of
`PromotionButton`) is exactly the vanilla-dump refusal `just sdk-adopt` exists for.

## 1. `UIArmory_MainMenu` — the hub

### 1.1 What it is

Pushed on the 3D movie by `XComHQPresentationLayer.UIArmory_MainMenu(UnitRef, …)` from the Armory
room's soldier list, squad select's card (with the remote-event chain that makes `OnCancel` fire
`PreM_GoToLineup` instead of popping), and the promotion hotlink. The soldier header
(`UISoldierHeader`, extended data shown), a `UIList` of eight `UIListItemString` rows with a
one-line description panel under it, and the ability/trait summary down the right.

### 1.2 What is retained

- ✅ **The rows keep their words.** `InitListItem(label)` → `SetText`, so `Text` (0x3F4) and
  `HtmlText` are real; the promote row's `Text` is `m_strPromote` with an `InjectImage` `<img>`
  prefix when the promote icon shows (`CleanText` strips it), or `m_strAbilities`. `bDisabled`
  (0x414 bit 0) and the `AttentionIcon` (0x440 — spawned on first `NeedsAttention(true)`, then
  `SetVisible` toggled) are the two state bits.
- ⛔ **A row's identity is its MCName.** `PopulateData` names them `ArmoryMainMenu_CustomizeButton`
  … `_DismissButton`, fires `OnArmoryMainMenuUpdate(List, self)` for mods to insert rows, and
  re-runs on EVERY `OnReceiveFocus` — respawning all eight. Index and pointer are both unstable;
  the name is the contract on both halves.
- ⛔ **The disabled REASON is not on the row.** `SetDisabled(disabled, Tooltip)` keeps the bit and
  parks the words in the tooltip manager on `ButtonBG` (`CachedTooltipId` only). The companion
  re-runs `UpdateData`'s cascades — `GetPCSAvailability` (five tooltips), `GetWeaponUpgradeAvailability`
  (three), the rookie promote lock — against the same data; the tutorial-locked rows are greyed
  with an empty tooltip in the game and say nothing here either.
- ⛔ **The description panel is a Flash child.** `OnSelectionChanged` switches on the row OBJECT
  and writes `m_str*Desc` (or the character template's `strCustomizeDesc`) through
  `MC.ChildSetString("descriptionText", …)`. Mirrored by object identity in the companion.
- ⛔ **The header's numbers and the ability panel are MC ops on the screen.** The header is the
  loadout's composers (moved into `X2AccessHelper` so the two screens name a soldier identically)
  plus `ShowExtendedData`'s three lines — bond status, combat intelligence, ability points — with
  its gates (class can bond, past the tutorial, uses the will system, Training Center or hero for
  the AP number). The summary is `UIUtilities_Strategy.PopulateAbilitySummary` (PCS items, earned
  class abilities, character-template abilities ever valid for the unit) or `_Traits`, chosen as
  `RefreshAbilitySummary` chooses (traits when listing them AND the soldier has any); a rookie
  gets no panel at all (`hideAbilityList`).

### 1.3 Input

`UIArmory.OnUnrealCommand`: Tab / Mouse5 / RB → `NextSoldier`, LeftShift / Mouse4 / LB →
`PrevSoldier` (both behind `m_bAllowAbilityToCycle`, the `T0_M2_WelcomeToArmory` objective,
`IsAllowedToCycleSoldiers` and `HasSoldiersToCycleThrough(UnitReference, CanCycleTo)`); Escape / B
/ right-click → `OnCancel` (a NO-OP during the tutorial); Enter / Space / A → `OnAccept`. The
Highlander's `OnAccept` refuses a disabled selection with a sound and otherwise re-sends Enter to
the LIST, so a mod-wrapped row delegate runs; vanilla's switches on `selectedIndex`. Both read the
SELECTION, so the layer selects first and hands the SCREEN an Enter. `FXS_BUTTON_X` toggles the
abilities/traits panel — a nav-help button on mouse+keyboard, no key; re-provided as a node.

### 1.4 As built

`armory` recipe (recipe_strategy.cpp) with `X2AccessArmoryReader`. Native reads the list, each
row's MCName / Text / bDisabled / attention, the selection and the unit id; the companion supplies
the header (`who`, status, stats, bond, intel, ap, `cycle=1`), per-row `desc` and `reason` keyed
on MCName, and the summary panel with its toggle label. Three stops: the soldier (a spinner while
the game offers the cycle — `CycleArmorySoldier` through `NextSoldier`/`PrevSoldier`), the menu
(label, "!" as attention, the description as a value, `unavailable` + reason), the abilities or
traits with the toggle button (`ToggleArmoryTraits` → `ToggleAbilitiesAndTraits`). Named by the
soldier, as the loadout is. Offsets: `List` 0x6F0, `DismissButton` 0x740 (the Highlander tail),
`UnitReference` 0x450 (UIArmory).

### 1.5 First live pass must answer

1. On entering from the Armory room: attach as `<name>, <rank>, <class>`, land on CUSTOMIZE,
   Up to the soldier node and Left/Right must cycle soldiers with the whole screen re-reading.
2. Each row must carry its one-line description; PCS and WEAPON UPGRADE greyed must say why
   (the tooltip a mouse player hovers for); a row with the "!" badge must say `attention`.
3. Enter on LOADOUT must land in the loadout recipe and Escape back must re-attach here with
   the rows respawned (the wire cache re-asks — the header must still be right).
4. On a soldier with a trait, the summary stop must end in the toggle and Enter on it must
   swap the panel to TRAITS and back, re-reading.
5. `just dev-gui` — confirm the eight MCNames, and that a mod-inserted row (if any) reads by
   its Text with no description.

## 2. `UIArmory_WeaponUpgrade` — weapon mods, and the pattern picker

### 2.1 What it is

Pushed by `XComHQPresentationLayer.UIArmory_WeaponUpgrade(UnitOrWeaponRef)` from the hub's
WEAPON UPGRADE row alone; `InitArmory` accepts a unit OR a weapon ref (the primary weapon when a
unit). The loadout's shape over a weapon: the header; a LEFT list of the weapon's slots (filled
first, then EMPTY SLOT rows); a RIGHT list of the inventory's upgrades that slides open when a
slot is chosen; the three CUSTOMIZE rows under the left list (name, colour, pattern); the stat
block and the upgrade description box on the right.

### 2.2 The Highlander changed the layout

⛔ `WeaponViewOffsets` (Issue #832) heads the class, so every offset is 0x10 past vanilla's —
`ActiveList` 0x510, `SlotsList` 0x520, `UpgradesList` 0x530, `WeaponRef` 0x538, `CustomizeList`
0x574 — and the pin on `WeaponViewOffsets` itself is the Highlander detector. Issue #93 routes the
slot count through `XComGameState_Item.GetNumUpgradeSlots`, which the companion calls.

### 2.3 What is retained

- ✅ **Both upgrade lists' rows keep their words.** `UIArmory_WeaponUpgradeItem` is the loadout
  row's layout through `Count` (title 0x3D8, subTitle 0x3E8, Count 0x438) and stores before it
  pushes: title = the upgrade's name or `m_strEmptySlot`, subTitle = `GetUpgradeEffectForUI` (the
  "+N" effect line) or `m_strUpgradeAvailable`. Three bits at 0x43C — `bIsLocked` (the chosen
  slot while the picker is open), `bIsInfinite`, `bIsDisabled` — then `UpgradeTemplate` 0x440 and
  `SlotIndex` 0x450 (meaningful on slot rows; -1 on upgrade rows, whose `InitUpgradeItem` skips
  it). ⛔ `disabledText` is declared and never assigned, and this screen does not even park the
  reason in a tooltip; the two reasons are `m_strRequiresContinentBonus` (an equipped slot past
  the current count) and `m_strInvalidUpgrade` (an upgrade `CanApplyUpgradeToWeapon` refuses).
- ⛔ **`ActiveList` is the mode bit**, the loadout's arrangement: `ChangeActiveList`
  DisableNavigation()s the other container and moves the navigator, so the slots list is a
  read-only mirror while the picker is open. `PreviewUpgrade` (both lists' `OnSelectionChanged`)
  reads the SLOT from the slots list's selection whichever list the cursor is in — so the slot
  selection is never moved under an open picker.
- ⛔ **Everything else is Flash.** The two panel titles (`m_strUpgradeWeapon` /
  `m_strWeaponFullyUpgraded`, `m_strSelectUpgrade` / `m_strNoUpgradeAvailable`), the slots counter
  (`m_strSlotsAvailable` + "N/M"), the weapon banner, the equipped-on line
  (`UpdateOwnerSoldier`), the customize title, the description box (`SetUpgradeText` — the ONLY
  place an upgrade's prose appears) and the whole stat block: `UIArmory_WeaponUpgradeStats` is a
  class with no members, fed by `PopulateData` with `Weapon.GetUISummary_WeaponStats(Template)`
  — from a PENDING change state with the selected upgrade applied. The companion reproduces
  `PreviewUpgrade` line for line (change state, delete, apply, read, cleanup — never submitted)
  so the stats speak what the sighted player is previewing.
- The customize rows are ordinary `UIMechaListItem`s (`ReadListRows`): the name row a
  description, the colour row a swatch with no words, the pattern row a value (the pattern's
  display name in `cachedValue`).

### 2.4 Input

`OnAccept` → `OnItemClicked(ActiveList, ActiveList.SelectedIndex)`: on a slot row it opens the
picker; on an upgrade row it raises the confirm dialog (`EquipUpgrade` / `ReplaceUpgrade`) or,
under the reuse-upgrades continent bonus, commits at once. With NO selection in the active list
`OnAccept` forwards Enter to `CustomizeList` — the customize rows are otherwise D-pad-only
(`FXS_DPAD_LEFT/RIGHT` with no keyboard constant). `OnCancel` is three-level: close the colour
selector → back out of the picker → leave. Tab / LeftShift cycle soldiers through the narrowed
`CanCycleTo`. The name row opens `UIInputDialogue` (the text-entry observer's); the pattern row
opens `UIArmory_WeaponTrait`.

**The pattern picker** is a SUBCLASS that reuses `SlotsList` as a plain `UIListItemString` list
(Text retained) with the options in `Data` (0x6D4) and the landing index in `StartingIndex`
(0x704); Enter picks and closes, ⛔ Escape REVERTS to the starting index and closes.

### 2.5 As built

`weapon upgrades` recipe (recipe_strategy.cpp) with `X2AccessWeaponUpgradeReader`; one recipe
for both classes, the finder taking the topmost of the family. Native reads the three lists, both
rows' retained fields, the active list, the weapon and unit ids; the companion supplies the header
(who, weapon, equipped-on, the counter, the three titles, `cycle=1`), per-row `count` and
`reason`, the `preview` (the box) and the `stat` block for the selection, and the picker's
heading. Stops: the weapon (soldier spinner, weapon, whose, counter); the slots (live, or the
mirror with the frozen slot marked selected while the picker is open); the picker while open;
the stats with the description box on top, read at announce time; the customize rows. Slot and
upgrade rows write back through their own list and activate through the screen's Enter; a
customize row activates through its own `OnUnrealCommand(Enter)`. Escape is left to the game.
Named by the weapon, then the soldier.

### 2.6 First live pass must answer

1. `just dev-props "screen=UIArmory_WeaponUpgrade"` — `ActiveList` 0x510 equal to `SlotsList`
   0x520 on entry; the pins refuse a vanilla dump, so a build that fails there is the answer.
2. Attach as `<weapon>, <soldier>`, land on the first slot; the slots stop must read
   `EMPTY SLOT, <m_strUpgradeAvailable>` or `<upgrade>, <effect line>`; the stats stop must read
   the weapon's block with no `<font` in the ring.
3. Enter on a slot must open the picker: the slots stop re-reads as `current slots` with the
   chosen slot `selected`, a new UPGRADES stop lands, and arrowing it must re-read the stats and
   the description box for each upgrade (the change-state preview — `just dev-log` must carry no
   `Accessed None` from the companion).
4. Enter on an upgrade must raise the confirm (dialog recipe); Yes must install it, close the
   picker and leave focus on the slots. Escape in the picker must back out without leaving.
5. The customize stop: Enter on the name row must open the rename dialog (echoed), Enter on the
   pattern row must open the picker as `WEAPON PATTERN` landing on the current pattern, and
   Escape there must revert. ⚠ The colour row opens `UIColorSelector`, which has no reader — a
   known hole, D-pad-driven in vanilla.
6. A greyed upgrade must say `unavailable` + the game's `m_strInvalidUpgrade` sentence.

## 3. The PCS path — `UIInventory_Implants`, and the dead `UIArmory_Implants`

⛔ **`UIArmory_Implants` is dead code.** `XComHQPresentationLayer.UIArmory_Implants()` exists and
nothing in the shipped sources or the Highlander calls it; the hub's PCS row goes straight to
`UIInventory_Implants` (`OnPCS` → `HQPRES.UIInventory_Implants()`), which is a `UIInventory` and
therefore ALREADY the commodity recipe's (`avenger-view-recon.md` §14). Same census entry as
`UIFacilityUpgrade`. Two implant-specific facts the commodity family lacked were added to it
(2026-09-03): the stat change the selected implant would make — `UIInventory_Implants.
SelectedItemChanged` writes it into the ARMORY HUB's header one screen down, ⛔ with the
arguments to `SetSoldierStats` in the wrong order (Will under HEALTH, Aim under MOBILITY…), a
shipped bug — so `X2AccessCommodityReader.ImplantDelta` re-asks the screen's own
`GetStatBoostString` per stat and pairs each with the stat it actually changes; and the red
row's meaning, which on this screen is `CanEquipImplant` refusing (a weaker implant of a stat
already boosted, or a psi implant on a non-psi soldier) rather than the price — `Vocab::
ImplantCannotEquip`, keyed natively on the screen's class. `UIInventory_Implants.OnItemSelected`
reads its index but the dialog text reads the selection, so the commodity recipe's
select-then-Enter is right here too; ⛔ it hard-depends on `UIArmory_MainMenu` being on the
stack (every unit lookup reaches up to it) and touches PCS slot 0 only.
