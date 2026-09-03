# Recruits and the rewards recap — recon (2026-09-03, built from source, unexercised)

Two P1 screens measured together because they sit at opposite ends of the Avenger's day and share
nothing but a lesson: **neither retains a word a player reads**, and each hides that fact
differently. Read against `SrcOrig` and the Highlander-derived SDK dump; nothing here has run live.
The build is `recruits` (recipe_avenger.cpp + `X2AccessRecruitReader`) and `rewards recap`
(recipe_avenger.cpp + `X2AccessRecapReader`); the rules are `surfaces/avenger.md`.

## 1. `UIRecruitSoldiers` — the Armory's RECRUIT door

### 1.1 What it is, and who opens it

Spawned by exactly one line in the shipped game, `UIFacility_Armory.Recruit()` — there is no
presentation-layer function for it. A 2D-movie screen (no `Get3DMovie()`), pushed over the Armory
room, which stays on the stack beneath it (`bHideOnLoseFocus = false`). The Avenger HUD shows the
monthly-income and supplies readouts over it (`UIAvengerHUD.UpdateResources` lists the class
beside `UIOfficerTrainingSchool`), which is why the recipe carries the resource stop.

### 1.2 What a sighted player sees

A list down the left — a country flag and a name per row, RECRUIT on the focused row's button —
and a panel on the right: the recruit's headshot, COST: `N Supplies`, TIME: `Instant`, and the
generated background paragraph. The list heading is RECRUITS; an empty list shows
`No Recruits Available`.

### 1.3 What is retained, and what is not

- The screen keeps **one widget** (`List`, 0x410) and **`m_arrRecruits`** (0x418) — an array of
  `XComGameState_Unit` POINTERS, rebuilt together with the list in `UpdateData` (`ClearItems` +
  `Length = 0` + one loop filling both). So `m_arrRecruits[i]` IS row i, by construction.
- ⛔ **A row retains nothing identifying.** `UIRecruitmentListItem` declares one localized string
  (`RecruitConfirmLabel`) and its `InitRecruitItem` pushes the flag and the name through
  `MC.BeginFunctionOp("populateData")` — never `SetText`, never `metadataInt`. `Text` is `""` on a
  row visibly showing a name. What it does set is `bDisabled` (0x414 bit 0) when
  `XComHQ.GetSupplies() < ResistanceHQ.GetRecruitSupplyCost()` — the SAME test for every row,
  since the cost is global.
- ⛔ **The right-hand panel is not a panel.** `OnRecruitChanged` (the list's `OnSelectionChanged`)
  paints it with four Flash calls — `setCost(label, value)`, `setTime(label, value)`,
  `setDescription(text)`, `setPicture(path)` — from locals: the screen's own localized captions
  (`m_strCost`, `m_strTime`, `m_strInstant`, `m_strSupplies` expanded with `IntValue0 =
  GetRecruitSupplyCost()`) and `Recruit.GetBackground()`. ⚠ The time line is `m_strInstant`
  UNCONDITIONALLY; `GiveRecruit` honours `RecruitOrderTime` (vanilla 0) and the screen never
  checks it. Parity is against the screen, so the companion says Instant too.
- The title (`setTitle`) and the empty line (`setEmpty`) are Flash pushes of `m_strListTitle`
  (0x428) and `m_strNoRecruits` (0x438), both retained on the instance.

### 1.4 Input

`OnUnrealCommand`: Enter / Space / A → `OnRecruitSelected(List, List.SelectedIndex)` — ⛔ the
SELECTED row, whatever widget the key came through; Escape / B / right-click → `CloseScreen`;
everything else falls to the navigator (Up/Down walk the list). `OnItemDoubleClicked` is wired,
`OnItemClicked` is NOT — a single click only selects. The per-row RECRUIT button is out of the
navigator (`SetConfirmButtonStyle` → `ConfirmButton.DisableNavigation()`), so every keyboard
activation must be the screen's own Enter. `OnRecruitSelected` buys at once (`GiveRecruit`,
`UpdateData`, `UpdateResources`) with **no confirmation**, and answers a short purse with the
`NOT ENOUGH SUPPLIES` dialog (`UIRaiseDialog`) even on a row `InitRecruitItem` already greyed.
`OnReceiveFocus` re-runs `UpdateData`, so every row is respawned on the way back from that dialog.

### 1.5 As built

Native reads the list, the rows' `bDisabled`, the selection, the count of `m_arrRecruits` and the
two captions; `X2AccessRecruitReader` walks `m_arrRecruits` for the name and the country (the
flag's meaning), and re-runs `OnRecruitChanged`'s three lines for the SELECTED row. Two stops:
the list (name, country, `unavailable` + `cannot afford` when greyed — the activation stays,
because the game's own dialog is the explanation), then the panel one node per line, read at
announce time. Enter is select-then-screen-Enter; the verb invalidates the wire cache because a
purchase respawns every row. The screen is named RECRUITS off its own caption.

### 1.6 First live pass must answer

1. `just dev-props "screen=UIRecruitSoldiers"` — `List` at 0x410, `m_arrRecruits` at 0x418 with
   `Length` equal to the row count.
2. A row must read `name, country` and the details stop `COST: N Supplies`, `TIME: Instant`, the
   bio; arrowing must re-read the bio for the new row (the wire cache is keyed on the selection).
3. Enter with supplies to spare must buy at once and leave focus on a row of the rebuilt list; with
   supplies short it must raise the dialog, and Escape from it must land back on the list.
4. Shape-count the ring for `<font` — the name arrives `GetColoredText`-wrapped in the game and
   the companion sends the raw name; a colour tag in speech means the wrong source.

## 2. `UIRewardsRecap` — the post-mission letter

### 2.1 ⛔ Which chain it belongs to (a correction)

`ui-inventory.md` had it as the last screen of the MONTHLY chain. It is not: that chain runs
`UIResistanceReport` → `_ChosenEvents` → `UIAdventOperations` → `UIStrategyPolicy`, each pushed
from the previous one's `CloseScreen`, and ends there. `UIRewardsRecap` is the tail of the
POST-MISSION chain: `UIInventory_LootRecovered.CloseScreen` → (`UIResearchUnlocked` when techs
unlocked) → `HQPRES.UIRewardsRecap()` → `ExitPostMissionSequence`. A third caller,
`X2StrategyElement_DefaultObjectives.OpenRewardsRecap`, forces it on the tutorial-objective path.
It is pushed on the 3D movie, over the whole Avenger sequence, and skipped when the mission source
sets `bSkipRewardsRecap` or before `T0_M8_ReturnToAvengerPt2`.

### 2.2 What a sighted player sees

The council screen: the operation's name (green on a win, red on a loss) and its region under a
SECURE TRANSMISSION greeble, the council's paragraph, REWARDS: with one line per reward, GLOBAL
EFFECTS: with the month's region-control / supply / Avatar effects, and an OBJECTIVE REWARDS:
heading that is always painted over an empty value. The nav help's CONTINUE.

### 2.3 What is retained — nothing

⛔ The class declares **no widget at all** — no list, no text, no button; `AUIRewardsRecap`'s
members are 23 localized templates, `CouncilRemarkChance`, a pointer to the HQ, and two tags.
`BuildScreen` resolves every string into locals and pushes them through five Flash calls
(`UpdateCouncilAfterActionInfo`, `…Rewards` per slot, `…RewardLabel`, `…GlobalAffects`,
`…Objectives`). ✅ What it keeps instead is a set of PUBLIC accessors that are exactly what
`BuildScreen` called: `GetOpNameString`, `GetOpLocationString`, `GetOpSummaryString`,
`WasMissionSuccessful`, `GetRewardStrings` (= `ResHQ.RecapRewardsStrings`),
`GetGlobalEffectsStrings` (the coloured, fused form of `RecapGlobalEffectsGood` + `…Bad`). The
companion re-runs those on the live screen and reads the two effect arrays PLAIN, one node each,
where the screen fuses them into one space-joined string.

- The outcome is stated in COLOUR alone. The companion borrows `UIMissionSummary`'s own
  `m_strMissionComplete` / `m_strMissionFailed` — the words the player heard one screen earlier —
  keyed on `WasMissionSuccessful()`.
- The rewards caption is painted `""` when no reward string is non-empty, and the effects call is
  skipped when both arrays are empty; both gates are reproduced. OBJECTIVE REWARDS: is not carried
  — a heading over a permanently empty value.
- ⛔ **Continue destroys the data.** `OnContinue` → `ExitPostMissionSequence` →
  `ResistanceHQ.ClearRewardsRecapData()` and `MissionRef` cleared. The reader guards both and
  native caches the payload on the screen pointer; nothing re-asks on the way out.

### 2.4 Input

`OnUnrealCommand` swallows everything (`return true`, no `super`): A / Enter / Escape / B /
right-click all `OnContinue`; Start opens the pause menu; Space is NOT handled; Tab continues only
outside `FINAL_RELEASE`. One-way — there is no back.

### 2.5 As built

A finder natively, everything else companion-side. One document stop — result, operation and
region, the paragraph, rewards under their caption, effects under theirs — then Continue as a
button over the screen's own Enter (its label is the nav help's and lives in the movie, so it is
the report's word). Named by the operation.

### 2.6 First live pass must answer

1. After a normal mission the screen must attach as the operation's name and land on MISSION
   COMPLETE / MISSION FAILED; `just dev-speech` must show the paragraph with the region expanded
   and no `<XGParam` left in it.
2. A mission with rewards must read REWARDS: then one node per reward, in slot order; one with
   a region-control change must read GLOBAL EFFECTS: with the good lines before the bad.
3. Enter on Continue must reach the geoscape with the recipe detached cleanly, and `just dev-log`
   must carry NO companion error from a read attempted after Continue.
4. The tutorial-objective path (`OpenRewardsRecap`, forced) — confirm it attaches there too, since
   that is the path `research-unlocked-recon.md` §5 named as the chain's hole.
