# Tactical Legacy Pack — Challenge Archives recon

How the offline challenge branch was measured before it was built (2026-09-17, from `SrcOrig`,
the generated SDK dump and the loc files; the game was closed, so nothing here has run). The
rules the build settled on go in `surfaces/shell.md` once the first live pass has earned them.

## 1. Reachability — one screen is the whole branch

`UITLEHub.EnterChallengeMode` → `XComShellPresentationLayer.UITLEChallengeMode()` pushes
`UITLE_ChallengeModeMenu`. From there:

- `OnChallengeClicked` (Enter, or the list's `OnItemClicked`) reads the challenge's history file
  (`History.ReadHistoryFromFile("Challenges/", …)`), calls
  `ChallengeModeManager.BootstrapDebugChallenge()`, sets `bIsChallengeModeGame` and
  `bIsLocalChallengeModeGame`, stamps `ChallengeData.OfflineID`, and runs
  `BattleDataState.m_strMapCommand`. **Nothing is pushed between Enter and the map.**
- `UIChallengeMode_SquadSelect` is spawned only by `XComChallengeModeManager` (line 218) — the
  ONLINE daily-challenge path. Not reachable from the archive.
- `UIChallengeLeaderboards` is pushed only from `UIChallengeMode_SquadSelect` (line 1826) and
  from `UIChallengePostScreen` (lines 294, 308) — the post-mission summary. Not reachable
  before a mission from the archive.
- The one thing the archive pushes over itself is `UIProgressDialogue` (LOADING CHALLENGES,
  `ShowProgressDialogue`) while the `LoadingItems` state reads 107 history files one per tick.
  That is the popup recipe's screen, listed before this one.

So the branch is `UITLE_ChallengeModeMenu` alone. What comes after the map loads (the challenge
HUD, the scoring dialog, the post screen, and the leaderboards behind it) is the in-mission
work, not this recon's.

## 2. What the screen draws

| On screen | Source | Retained where |
|---|---|---|
| Title `АРХИВ ИСПЫТАНИЙ` | `m_ScreenTitle` (0x44C), `SetScreenTitle` | the screen |
| Five column headers `ЗАДАНИЕ ЗАДАЧА КАРТА ОТРЯД ВРАГИ`, one with a sort arrow | five `UIFlipSortButton`s spawned by `BuildHeaders`, labels from `m_strButtonLabels[5]` (0x500, 0x10 stride); state `m_eSortType` (0x554, byte) + `m_bFlipSort` (0x550 bit 0) | labels on the screen; the buttons are unnamed children — `GetChildrenOfType` only |
| 107 rows × 5 cells | `UITLEChallenge_ListItem.PopulateData` pushes `Data` (mission, objective, map, squad, enemies) | **the row retains `Data`** (`FChallengeListItemData` at 0x3D8: index +0, MissionName +4, MissionObjective +0x14, MapName +0x24, MapImagePath +0x34, Squad +0x44, Enemies +0x54) |
| Details panel: map image, mission name, SQUAD / OPPONENTS / OBJECTIVE with values | `OnSelectedChange` → `SetChallengeDetails`, from the selected row's `Data` and `m_Header_*` | the same `Data`; the headers are `m_Header_Squad` 0x4BC, `m_Header_Enemies` 0x4CC, `m_Header_Objective` 0x49C |
| Details panel: VICTORIES n, HIGH SCORE n, CHALLENGES COMPLETED n | `OnSelectedChange`, from `XPROFILESETTINGS.Data.HubStats` under `UITLEHub`'s labels | nowhere — profile, re-derived by the companion |
| Back (nav help) | `AddBackButton(OnCancel)` | Escape, the game's |

`m_OpNames[107]` is a localized array on the screen: `CreateListItemData` names row *i*
`m_OpNames[i]`, not the battle data's op name. It reaches the row as `Data.MissionName`, so it
is read off the row, never off the array.

## 3. Sorting

`IUISortableScreen`. Each header's `OnMouseEvent(FXS_L_MOUSE_UP)` deselects the others, flips
the direction if it was already the sort column (else selects it, resetting the flip), then
`RefreshData()` → `SortData` → `UpdateList`. ⛔ `UpdateList` **`Refresh()`es the existing rows
in place** — no `ClearItems`, no respawn — so after a sort the same widget pointers carry
different challenges. Two consequences the build honours:

- the wire cache's pointer key would not notice a sort; the sort state is the cache's `extra`;
- a row's graph identity is `Data.ChallengeListDataIndex` (the file index, per challenge), not
  the widget handle, or every sort would rebadge the challenge under the cursor.

Keyboard sorting does not exist in vanilla (click-only), so the sort stop re-provides it through
the button's own mouse-up, exactly as the personnel roster does (`ClickPersonnelSort`).

## 4. Selection and activation

- Arrows: the screen's `OnUnrealCommand` comments out `FXS_ARROW_UP/DOWN` and forwards
  `W`/`S`/d-pad to the Navigator; the list's own navigator takes the arrows. `List.SetSelectedIndex`
  fires `OnSelectionChanged = OnSelectedChange`, which repaints the details panel — so the P11
  write-back IS the details refresh.
- Enter / Space / A: `OnChallengeClicked(List, List.SelectedIndex)`. The build selects the row,
  then makes that exact call (`_Params`: `ContainerList` +0, `ListItemIndex` +8).
- First landing: `LoadFinished` → `List.Navigator.SelectFirstAvailable()`; on a return from a
  mission the screen is a fresh push, and `UpdateList` keeps `List.SelectedIndex` where it was.

## 5. The shape chosen

Three stops, screen order: `sort` (five header buttons, row), `list` (a `GraphSheet`: primary =
mission name carrying the four other cells as value parts; columns objective / map / squad /
enemies under the headers' own labels; position "n of 107"), `progress` (the three profile
figures, text). Focus starts on the list. The details panel's per-row half is not a stop: it
is the focused row's cells again.

## 6. Owed by the first live pass

- The rows read at all (Data at 0x3D8 on a `UIPanel` subclass — the dump's offset, unverified
  live), all 107 of them, and the attach lands on row 1 with "1 of 107".
- The sort stop reaches five headers through `GetChildrenOfType` and Enter on one re-sorts
  (arrow moves, then flips on a second press) with the sheet re-reading in the new order and
  the cursor staying on the same CHALLENGE, not the same row slot.
- `grep -P '  '` over the ring: five header labels, the title and three profile labels are
  localized reads, and any one empty is the double-space tell.
- Whether the row-vanish rule ever fires here (it should not: the row set is fixed).
