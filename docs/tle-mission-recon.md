# Tactical Legacy Pack — the mission-side screens: recon

How the eleven screens the pack pushes over `UITacticalHUD` were measured before they were
built (2026-09-17, from `SrcOrig`, the generated SDK dump and the INT/RUS loc files; the game was
closed, so nothing here has been read live). The shell half of the pack — the hub, the three mode
menus, the difficulty page, the loadout popup — is another recon. What was built from this:
`src/game_ui_tle_mission.{h,cpp}`, `src/recipes/recipe_tle_mission.cpp`,
`X2AccessLadderReader.uc`.

## 1. The census, and the recipe-or-not decision per screen

| Screen | Spawned by | What a player does on it | Decision |
|---|---|---|---|
| `UIChallengeModeHUD` | `XComPresentationLayer.InitializeChallengeModeUI` at mission start, pushed ABOVE the HUD, `InputState = eInputState_None` | nothing — it draws the score, a countdown, and a banner per scoring event | **no recipe, narrate-only**: nothing to walk, and the arrows are the tile cursor's. The banner and the total are narrated by an event listener at the banner's own moment; the score and the timer are one trailing stop in the HUD graph (`AddTLEHudStop`). |
| `UILadderModeHUD` | `InitializeLadderModeUI`, same | same, minus the timer (its `Tick` is a no-op) | same |
| `UIChallengeModeScoringDialog` | `UIChallengeStartTimerMessage`, before turn 1 | reads the point table, presses START CHALLENGE | recipe `mission scoring` |
| `UILadderModeScoringDialog` | `UILadderStartTimerMessage`, before turn 1 of every ladder mission | same, START MISSION | same recipe (one finder, two offset sets) |
| `UILadderUpgradeScreen` | `MaybeDoLadderProgressionChoice` after a ladder mission that is not the last | chooses one of two upgrades | recipe `ladder upgrade` |
| `UILadderSoldierInfo` | `MaybeDoLadderSoliderScreen`, right after | reviews the squad's changes, pages, opens a soldier's stats, presses NEXT MISSION | recipe `ladder squad` |
| `UITLELadderMedalProgressScreen` | `MaybeDoLadderMedalScreen`, narrative ladders, not the last mission | reads progress, Continue | recipe `medal progress` |
| `UITLELadderMedalScreen` | `MaybeDoLadderMedalScreen`, narrative ladders, after the last mission, bronze or better | reads the medal and the unlocks, Continue | recipe `ladder medal` |
| `UITLELadderEndScreen` | `MaybeDoLadderEndScreen`, after the last mission of any ladder | reads six run statistics, Continue | recipe `ladder end` |
| `UIChallengePostScreen` | `UIChallengeModeSummary`, end of a challenge | reads the result, Continue, LEADERBOARD (networked only) | recipe `challenge result` |
| `UIChallengeModeEventNotify` | `UIChallengeModeEventNotify`, mid-mission in a networked challenge | reads how other players fared, pages, Close | recipe `challenge event` |

The post-mission chain is `X2TacticalGameRuleset`'s `bWaitingForMissionSummary` loop
(`:5322-5352`): mission summary → upgrade → squad review → medal progress or medal → end, each
screen popped before the next is pushed. So none of the ladder screens is ever on the stack with
another of them, and their order in `kRecipes` relative to each other is free.

## 2. What each screen retains — the reading split

Every one of them keeps its `localized` strings and its buttons, and pushes everything else into
Flash at `OnInit`. Native reads the former; the companion re-runs the latter's arithmetic.

| Screen | Retained (native reads) | Pushed and gone (companion re-derives from) |
|---|---|---|
| scoring dialogs | header, total label, two descriptions, `m_ContinueButton` (a `UILargeButton` whose `Text` is START MISSION / START CHALLENGE) | the rows: `XComChallengeModeManager.GetChallengeMaxPossibleScoreTable` (native function, script-callable), the ladder's two penalty rows from `GetSoldierScoring` |
| upgrade picker | `SelectedColumn`, `m_strChooseUpgrade`, `m_strEquipUpgrade`, both buttons (Text = CHOOSE UPGRADE, delegates = the choices) | the offers: `LadderProgress.ProgressionChoices1/2[rung-1]` → `X2LadderUpgradeTemplate` (`ShortDescription`, `FullDescription`), the per-class lines (`PopulateUpgradePanel`'s walk over the squad, `XEXPAND` over `m_strUpgradeFormat` / `m_strReplaceFormat`) |
| squad review | `m_SoldierStartIndex`, `m_NumSoldiers`, Prev/Next (hidden = no page that way), the three info buttons (hidden = dead soldier), all headings, and — the one screen here that keeps state objects — `PrevStates` / `CurrentStates` | the per-soldier diff: `UpdateData`'s item and ability set differences over those two arrays, the header word (KIA / NEW SOLDIER / RANK UP), the narrative unit name (`GetUnitName`), the rank ICON's name |
| medal | the labels, and `m_MedalUnlockedString` **already filled** (OnInit `Repl`s it in place on the instance) | score, the medal reached (strictly greater than each threshold), `MedalThresholds[iMedal-1]`, the rewards block's three cases |
| medal progress | nothing but `NavHelp` — every label is borrowed from `UITLE_LadderModeMenu` | rung/size, the three markers (`GetLadderMedalThreshold`, the ladder menu's medal words), the meter as score over gold |
| end | `m_RunStats[6]` (captions), `m_Description` | `XComGameState_Analytics.GetValueAsString` over the six metrics OnInit names |
| challenge result | the labels, `bMissionFailed`, Continue (Text = the generic Continue), the leaderboard button (no Text; hidden offline) | gamer tag, timestamp, `GetTotalScore`, `GetMaxChallengeScore`, the MAXIMUM POINTS label borrowed from the scoring dialog |
| event notice | everything: `m_TitleHeader.Title`, `m_kDetailText.Text.Text`, three buttons with Text and `IsDisabled` | — |

## 3. Things measured that the build turns on

- ⛔ **The score HUD is a stack entry above `UITacticalHUD` for the whole mission.** The tactical
  gate whitelists passives by exact class name (`kTacticalPassives`: `UIReplay`,
  `UISpecialMissionHUD`, `UITurnOverlay`, `UIMouseGuard`); `UIChallengeModeHUD` and
  `UILadderModeHUD` are not on it, so a ladder or challenge mission reads as `blocked` from its
  first frame and the tile cursor, the HUD graph and every tactical producer stay silent. Both
  names must join the list — the subclass too, because the match is `strcmp`.
- **The scoring event.** `'ChallengeModeScoreChange'` is triggered natively per
  `XComGameStateContext_ChallengeScore` submission; its `GameState` carries one or more
  `XComGameState_ChallengeScore` (`ScoringType`, `AddedPoints`, `LadderBonus`). The HUD registers
  on `ELD_OnVisualizationBlockCompleted` and the banner is `X2Action_ChallengeScoreUpdate` on the
  same context, so Completed is the moment the banner has just been read. The early-bird bonus is
  a separate `X2Action_PlayMessageBanner` off `LadderProgress.OnChallengeModeScore` (StateSubmitted)
  with `EarlyBirdTitle` and `XEXPAND`ed `EarlyBirdBonus`. The listener speaks both in those words,
  then the total under the HUD's `SCORE` label. Not in a replay, as the action itself is not.
- **The challenge timer** is `XComGameState_TimerData` with `bIsChallengeModeTimer`; the HUD's
  `Tick` pushes `GetCurrentTime()` as minutes and seconds every frame and colours it under ten and
  five minutes. The stop reads the time; the colour thresholds are not narrated (an aid the game
  draws as colour — a candidate, not a parity fact).
- **The upgrade picker's keyboard is complete in vanilla** (Left/Right = `SelectColumn`, Enter =
  the selected column's choice), so focus write-back goes through `SelectColumn` and the game's
  highlight follows ours. The two buttons' delegates ARE `ChooseUpgrade0/1`; `Click()` runs them.
- **The squad review's Prev/Next are `InitButton`'d with `""`** and `DisableNavigation()`ed; the
  game's own Left/Right call their delegates directly. Read by pointer and visibility (`Hide()` is
  the first/last-page marker), clicked through `Click()`, which honours `bIsVisible`.
- **`m_MedalUnlockedString` is rewritten on the instance** (`m_MedalUnlockedString = Repl(...)`),
  the one localized field in the family that OnInit fills rather than reads — so the medal name is
  a native read, and a second `OnInit` on the same instance would `Repl` an already-filled string
  (harmless: no `%s` left).
- **The medal screen's `MedalThresholds[iMedal - 1]`** is `[-1]` when the score merely EQUALS
  bronze (the screen shows on `>=`, awards on `>`), which UnrealScript reads as 0 — the companion
  mirrors that as "0" rather than inventing a threshold.
- **`UIChallengePostScreen` pushes `m_Header` on both branches** and then `setFailureState` for a
  loss; whether the movie swaps in `m_HeaderLose` is the FLA's. The reader picks the loss header
  and the loss description on `bMissionFailed`, the same flag the movie is told. First live loss
  settles it.
- **Pack(4) classes.** `UILadderSoldierInfo` and `UIChallengePostScreen` have pointers at
  unaligned offsets (`0x464`, `0x4AC`, `0x4B4`); the raw readers cope and the pins record the dump.
- **Offline vs networked.** The leaderboard button is `Hide()`n for `bIsLocalChallengeModeGame`;
  the event notice never appears offline (it needs `m_ChallengeModeEventMap`). Its strings are
  English literals in the shipped source (`"Close"`, `"Event Description: "`), so an English readout
  there is the game's, not ours.

## 4. What this recon did not settle

- `UITLEUnitStats` (opened by the squad review's info buttons) and `UIChallengeLeaderboards`
  (opened by the challenge result's LEADERBOARD) are other surfaces' entries.
- Whether `m_ScoreLabels[]` on the two dialogs read populated on the instance for every index the
  score table fills — the loc files key them by enum name (`m_ScoreLabels[CMPT_KilledEnemy]`), and
  a `grep -P '  '` over the ring on the first scoring dialog is the check.
- The banner's exact wording in Flash (whether it draws a `+` before the points) — the narration
  says the signed number as the state carries it.
