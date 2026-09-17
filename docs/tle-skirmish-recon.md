# Skirmish Mode menu — recon (`UITLE_SkirmishModeMenu`)

How the Tactical Legacy Pack's Skirmish Mode menu was measured before the `skirmish` recipe was
built (2026-09-17, from `SrcOrig`, the generated SDK dump and the loc files; the game was closed,
so nothing here has been checked against a live instance — §6 lists what the first live pass must
settle). Rules earned by a live bug go to `surfaces/shell.md`; this file is the measurement.

## 1. What the screen is

One `UITLEScreen` (`Package = gfxTLE_SkirmishMenu/TLE_SkirmishMenu`, `bHideOnLoseFocus`), pushed by
`XComShellPresentationLayer.UITLESkirmishMode()` from the hub's Skirmish card. It pushes NO screen
of its own: every branch of its state machine rebuilds the same list, and the only things it raises
over itself are three `UIDialogueBox` confirmations (leave, remove soldier, mission needs a human)
— the dialog recipe's. Enter from its base menu runs `CreateNewLadder(2)`, which opens the map by
console command; the mission itself is the tactical layer's.

`OnInit` builds the whole tactical start state (`InitHistory`: a fresh history, battle data, a
mission site, four random soldiers with random gear) and keeps every object it needs as a member:
`StartState`, `BattleDataState`, `ChallengeMissionSite`, `CampaignSettings`, `History`,
`m_CurrentSquad`. **Those members are the screen's cache**, and the read-the-screen rule's second
tier; the companion reads them and never the global history.

## 2. The state machine

`m_CurrentState` (`EUISkirmishScreen`, a byte at 0x48C) selects which rows `UpdateData` builds and
which panel the right side shows. Twenty-one states:

| state | list rows (`UpdateData*`) | right panel |
|---|---|---|
| Base | Edit Squad, Edit Map Details, Edit Opponent Squad (not on RecoverExpedition), Edit Difficulty: *value* | mission info |
| Squad | Add New Unit (while < 6), Edit Unit: *name* per soldier | squad blurb on Add / nothing selected, else the soldier |
| SoldierData | Remove Unit, Choose New Class, Edit Abilities (non-rookie), Edit Equipment, Choose Rank (non-rookie) | the soldier |
| SoldierClass | one row per class template (no Rookie, no hidden) | the soldier (stale, not redrawn) |
| SoldierEquipment | Primary: *item*, Secondary, Armor, PCS, Utility 1, Utility 2, Grenade, Heavy — as the soldier has slots | the soldier |
| PrimaryWeapon … HeavyWeapon, PCS | one row per candidate item (PCS leads with "No PCS") | the soldier (stale) |
| SoldierAbilities | one CHECKBOX per ability in the tree, then the AWC unlocks; rank-0 and unmet-prerequisite rows disabled | the SELECTED row's ability name + description |
| SoldierRank | one row per rank name 1..max-1 | the soldier (stale) |
| MapData | Edit Mission Type: *name*, Edit Map Type: *name*, Edit Biome: *name* (when the plot has biomes) | mission info |
| Map / Mission / Biome | one row per plot type / mission template / biome | mission info (biome selection swaps the image only) |
| Enemies | one row per `X2ChallengeEnemyForces` template | the SELECTED row's pod: name, description, enemy portraits |
| Difficulty | the four difficulty names | mission info (unchanged) |

Every state change goes through `HideListItems` (destroys every row) and respawns them, so a row
pointer never survives a state change — which is exactly the self-invalidating key the wire cache
wants. Within a state, the list's `OnSetSelectedIndex` is the screen's own selection hook and
redraws the panel on three pages (squad → `RefreshSquadDetailsPanel`, enemies →
`UpdateEnemyPortraits`, abilities → `UpdateAbilityInfo`).

## 3. What retains what

- **Rows**: `UIMechaListItem`, `UpdateDataValue(label, value, click)` or `UpdateDataCheckbox`.
  Label and value are retained on the row (the settings screen reads the same widget) — native
  reads every row with `ReadListRowsAll`. Rows carry `metadataString`/`metadataInt` (a template
  name; on the abilities page the tree rank and branch) that the click handlers resolve.
- **Title / subtitle**: `m_ScreenTitle` (0x710) and `m_arrSubtitles[m_CurrentState]` (0x540, stride
  0x10, 21 entries; the base state's is the empty string) — localized on the instance.
- **The START GAME button**: `ContinueButton` (0x428), a `UILargeButton` spawned with NO label —
  `m_StartGame` (0x690) reaches Flash through `SetMissionInfo` — so `ReadButtonField` cannot see
  it; the reader takes the pointer and the focus bit. It is navigable (the game's Down reaches it
  from the last row through `LeftColumn.Navigator`) and clicks through
  `OnButtonStartBattleClicked`.
- **The right panel**: pushed into Flash and retained nowhere. Each push reads the screen's own
  members, so the companion re-runs it:
  - mission info (`UpdateGeneralMissionInfoPanel`): `BattleDataState.m_strOpName`; `m_EnemyLabel`
    + the selected `X2ChallengeEnemyForces` template's `DisplayName` and `Description`;
    `m_ObjectiveLabel` + the active mission template's `DisplayName`; six soldier slots from
    `m_CurrentSquad` (name, class, rank icon — named as the rank).
  - squad blurb (`UpdateDetailsGeneric`): `m_SquadLabel`, `m_SquadGeneralDescription`,
    `m_SoldiersFactionLabel`, `m_SoldiersFactionDescription`, six class captions, the faction
    titles off the screen's own `History`.
  - one soldier (`UpdateDataSoldierData`): rank name, name, class; `SetSoldierStats` — HEALTH,
    MOBILITY, AIM, WILL (current/max), ARMOR, DODGE, HACK, PSI (psi ops only), each as base + item
    bonus coloured and concatenated; `SetSoldierGear` — armor, primary, secondary (the Reaper's
    claymore from its template), the utility items, grenade pocket, then heavy weapon and PCS.
  - ability (`UpdateAbilityInfo`): the selected row's template by tree rank/branch, or — for a row
    whose branch resolves to nothing, which is every AWC row — the FIRST unlock template the class
    allows (the game's own fallback, reproduced as-is; §6).
  - opponent pod (`UpdateEnemyPortraits`): the template's name and description, then
    `GetPossibleEnemiesInPod` grouped by character group. ⛔ That function also calls
    `SelectEnemyForces` and MUTATES the mission site on every selection change; the companion asks
    only the query half.

## 4. Input

`OnUnrealCommand`: Enter → `Navigator.OnUnrealCommand(ENTER)` (the selected row's own click);
Escape → `OnCancel`, which steps one state back (and raises the leave dialog on the base menu);
Up/Down/W/S → the navigator. No mouse-only control exists on the keyboard path except the START
GAME button, which the game's Down reaches anyway. Nothing to re-provide.

## 5. The recipe (`skirmish`, `src/recipes/recipe_tle_skirmish.cpp`)

Three stops:

1. **options** — the rows, under the state's subtitle as context. Checkbox rows are checkboxes
   (Enter toggles through the row: `OnAbilityCheckboxChanged`); every other row a button carrying
   its value. Space: the row's hover tooltip, or on the abilities page the row's own ability text.
   Focus write-back: `List.SetSelectedIndex`, which fires the screen's `OnSetSelectedIndex` — so the
   panel follows our focus exactly as it follows the game's arrows.
2. **details** — the panel, as read-only facts: mission (op name; opponent, description on Space;
   objective; the soldier slots), the squad blurb, the soldier (rank/name/class as the primary of a
   one-row **sheet** whose columns are the stat headings, then the gear lines), the selected
   ability, the pod.
3. **start** — the START GAME button, write-back onto it (deselecting the list, as the game's Down
   does).

Wire cache key: the row pointers + the screen, with `extra = state << 16 | selected + 1` because the
panel follows the selection on three pages and the rows do not change when it does.

Companion: `X2AccessSkirmishReader.DescribeSkirmish` → records `mission`, `member`, `squadinfo`,
`unit`, `stat` (label/value pairs), `gear` (label/item pairs), `abil` (per row, with `index`),
`ability` (the selected one), `pod` (repeating `enemy`).

## 6. What the first live pass must settle

- `m_CurrentState` reads at 0x48C as a byte, and the subtitle index matches the page (the enum
  order in the class vs. the loc file's named entries).
- The button's `bIsVisible` is set on a Flash-named `UILargeButton` — three states in, the START
  GAME stop must still exist.
- After Enter on a picker row (a new armor, a new class) the rows respawn and the recipe lands on
  the new list's selected row with the new subtitle spoken as context; if it lands silently, the
  differ needs the subtitle in the row identity.
- The stat sheet: stepping Right from the soldier line says a heading ("HEALTH") and its value;
  Tab from the list lands on the soldier line, not a cell.
- The AWC rows' description: the game's fallback shows the first allowed unlock for every AWC row;
  if that reads as the same text on every AWC row, that is the game's own panel doing the same, not
  a wire bug — decide then whether Space should resolve the row's real template instead.
- `grep -P '  '` over the ring for an empty label (every label on this screen is a localized
  instance string; `m_strUtilLabel` is the one that appears twice, once per utility item).
- Distribution: on the squad page, `Edit Unit` values should be four different names; on the
  equipment page, the PCS row exists only for a soldier of sufficient rank with a PCS stat.
