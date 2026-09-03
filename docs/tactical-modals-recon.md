# The two tactical modals — recon (2026-09-03, built from source, unexercised)

`UIInventoryTactical` (the loot receipt) and `UITacticalCharInfoScreen` (the soldier sheet): the
two P1 screens pushed over `UITacticalHUD` mid-mission that had no reader. Read against `SrcOrig`
and the Highlander-derived dump; nothing here has run live. The build is `loot` and `soldier
info` in recipe_tactical.cpp, with `X2AccessLootReader` and `X2AccessCharInfoReader`; the rules
are `surfaces/tactical.md`.

Both screens fail the tactical gate's passive whitelist on purpose: each consumes every key, so
`ReadTacticalGate` marks it `blocked`, the ambient HUD recipe detaches, and the modal's own recipe
probes on the next poll — the mission bookends' arrangement (`recipe_tactical.cpp`). ⛔ Neither
belongs in `kTacticalPassives`.

## 1. `UIInventoryTactical` — a receipt, not a picker

### 1.1 Two facts that reshape it

- ⛔ **The loot is already taken.** `LootAbility_BuildGameState` runs `MakeAvailableLoot` +
  `AcquireAllLoot` before any visualization; the screen (`X2Action_Loot`, via
  `PRES.UIInventoryTactical`) is a readout of what just happened. `m_strTakeAll` / `m_strDumpAll`
  are localized but their buttons are commented out; `m_kButton_OK` is the only control. A reader
  that spoke those two labels because the loc file has them would invent two buttons.
- ⛔ **The visualizer is asleep on it.** `X2Action_Loot` sits in `Sleep(0.1f)` until the screen's
  `ClosedCallback` fires. A blind player who cannot find OK has stalled the mission. And the Loot
  ability auto-triggers on `UnitMoveFinished`, so the screen appears with no key pressed.

It is reached four ways — the Loot ability (player input or end of move), the Interact ability
(crates, chests), a successful hack, and a Kismet `SeqAct_TakeLoot`.

### 1.2 What is retained

`UIInventoryTactical extends UIScreen` — ⛔ NOT `UIInventory`; the commodity recipe's `IsKindOf`
finder never matches it. It keeps `m_kLootList` (0x418), `m_kButton_OK` (0x428, a UIButton
whose `Text` is `m_strGenericOK` inside `AlignCenter`'s `<p align='CENTER'>` markup) and
`m_strTitle` (0x458). `m_kText` is declared and never spawned. A row,
`UIInventoryTactical_LootingItem`, is a plain UIPanel keeping `GameStateObjectID` (0x3D8) and a
Flash path; its `SetText` pushes the composed name (`GetItemFriendlyName` + ` xN` when
`Quantity > 1`) and registers the description (`GetItemLootTooltip()`, else
`GetItemBriefSummary`) as a mouse-following `UITextTooltip`. The title bar (`SetTitle(m_strTitle,
Looter.GetName(eNameType_RankFull))`) and the source line (`SetHeader(LootTarget.GetLootingName())`)
are Flash pushes off the screen's retained `m_Looter` and `LootTarget`.

### 1.3 Input

`OnUnrealCommand` never calls `super` and returns true for everything: A / Enter / Space / B /
Escape / right-click all `OnButtonClicked(m_kButton_OK)`. The list is unreachable by any key. The
OK button is `DisableNavigation()`ed. `OnCancel` is dead code and would NOT close the screen if it
ran. `InputState = eInputState_Consume`, `bConsumeMouseEvents`; the HUD stays visible beneath.

### 1.4 As built

Native reads the list, the rows' ObjectIDs, the OK button's label and the title;
`X2AccessLootReader` re-derives each row's name and description from the ObjectID exactly as
`UpdateData` did, plus the title, the looter and the source. Two stops: the items under the
source's name (Space reads the description a mouse player hovers for), then OK over the screen's
own Enter. Named LOOT RECOVERY plus the looter.

### 1.5 First live pass must answer

1. Walk a soldier onto a corpse: the screen must attach as `LOOT RECOVERY, <rank name>`, land on
   the first item under the corpse's name, and Enter on OK must release the visualizer (the
   slurp animation plays) with the HUD layer re-attaching afterwards.
2. `just dev-props "screen=UIInventoryTactical"` — `m_kLootList` 0x418, `m_kButton_OK` 0x428.
3. A crate with two of one item must read `Name x2`; Space on it must read the loot tooltip.
4. `grep -c '<p' ` over the ring must be zero — the OK label arrives inside alignment markup.

## 2. `UITacticalCharInfoScreen` — the soldier sheet

### 2.1 ⛔ It is not F1, and nothing on a keyboard opens it

The inventory called it "F1 soldier details". `Key_F1` in `XComTacticalInput` is a commented-out
no-op. The screen's ONLY spawn site is `XComTacticalInput.Stick_L3` in state `ActiveUnit_Moving`
— the gamepad's left-stick click — and the HUD's `CharInfoButton` hint is spawned in the
controller branch alone; the mouse-controls strip carries `m_strSoldierInfo` and never uses it. So
on mouse+keyboard the sheet is unreachable, and the recipe has to OPEN it as well as read it:
`X2AccessCharInfoReader.OpenCharInfo` reproduces `Stick_L3`'s body (spawn over the current
screen, `GetTacticalHUD().Hide()`, `InitCharacterInfoScreen`, push) behind the HUD's own
`CanCharInfoShow`, and the HUD graph's soldier stop carries a button named with the HUD's own
`m_strNavHelpCharShow` (0x510). ⚠ `InitCharacterInfoScreen` runs BEFORE the push and `OnInit`
(which builds the sheet) after it, so a probe can see a pushed, focused screen with a null
`ListPerks`; the reader answers `valid = false` and the recipe treats that as not-yet-open.

### 2.2 ✅ It retains everything

The one tactical surface that does. `mc_nameLabel` (rank, name, nickname as `PopulateCharacterData`
composed them), `mc_classLabel` (soldiers only), and seven fixed stat slots
`mc_statLabel00..06` / `mc_statValue00..06` — HEALTH: `cur/max`, AIM:, HACK:, WILL: `cur/max`,
ARMOR:, DODGE:, PSI:, each value `---` for zero ("does not display a 0") — are all
`UIScrollingHTMLText`s that keep their `Text` (inside `AlignCenter` markup). ⛔ Mobility and
Defense are in `EUISummary_UnitStats` and NOT on this screen. The screen keeps no POINTER to any of
them (spawned into locals), so they are found by MCName under `ChildPanels`.

`ListPerks` (0x410) holds, in order, PENALTIES → BONUSES → PASSIVES, each as a bare `UIText`
heading (only when the category has something) followed by one `UIListItemPerk` per effect — and
that row keeps the whole `UISummary_UnitEffect` BY VALUE (`Perk` at 0x3E0: Name +0x00,
Description +0x10, Icon, Cooldown, Charges, AbilitySourceName +0x38). That struct is
`GetUISummary_UnitEffectsByCategory`'s own output, the oracle the `;` readout already uses, so
the read is native with no companion. The row draws name and description only; cooldown and
charges are retained and not drawn, so they are not spoken.

### 2.3 Input

`OnUnrealCommand` swallows every key: `FXS_VIRTUAL_LSTICK_UP/DOWN` scroll (gamepad only — a long
list cannot be scrolled from a keyboard), Escape / B `CloseScreen` (which `Show()`s the HUD
again), Start the pause menu. No activation of any kind. `Scrollbar` on the class is never
assigned; the working one is `ListPerks.Scrollbar`.

### 2.4 As built

Two stops: the head (name and class, then the seven stats as label/value nodes, `---` said as
`none`), and the effects with the game's own headings as contexts and one node per effect
carrying its description. Nothing activates; Escape is the game's. `UIAbilityInfoScreen` (the
Armory's ability viewer) shares the Flash package, the MCNames and the seven slots but uses
`UIListItemAbility` rows with a different layout — a separate reader when it is built.

### 2.5 First live pass must answer

1. Ctrl+Tab on a mission, Tab to the soldier stop, Down past the facts to the info button: Enter
   must open the sheet, the HUD must hide, and the recipe must attach as `<rank name>, <class>`.
2. The seven stats must read in order with HEALTH and WILL as fractions and a zero as `none`;
   PENALTIES / BONUSES / PASSIVES must appear only for a non-empty category, each effect with its
   description.
3. Escape must close the sheet, the HUD must reappear, and the HUD layer must re-attach — check
   `just dev-log` for the gate's `blocker` naming `UITacticalCharInfoScreen` while it was up.
4. `grep -c '<p' ` over the ring must be zero (every label arrives inside alignment markup).
