# Mission summary + Chosen encounter summary — recon

_Source-derived 2026-09-03 from the SDK's `Development/SrcOrig/XComGame/Classes/`
(`UIMissionSummary.uc`, `UIChosenMissionSummary.uc`, `X2TacticalGameRuleset.uc`,
`XComPresentationLayer.uc`), the shipped `Localization/INT/XComGame.int`, and the regenerated
SDK's dump comments. **Nothing here has been measured on a running game yet** — the screens exist
only in the seconds after a mission ends and no save sits on either, so §7 is the live pass and
every offset below is transcription + `static_assert`, not observation._

---

## 1. What they are

The two windows that open the moment a tactical mission ends, before the map unloads:

| Screen | When | Closes into |
|---|---|---|
| `UIChosenMissionSummary` | a Chosen was on the mission and it was not a Chosen showdown (`XComGameState_HeadquartersAlien.WasChosenOnMission()`) | `UIMissionSummary` (its own `CloseScreen` pushes it) |
| `UIMissionSummary` | every other mission end | `BATTLE.QuitAndTransition()` → the strategy transfer → `UIAfterAction` |

They are the FRONT of the post-mission chain and the only part of it with no reader at all:
**mission summary (this) → after action (✅ `after action` recipe) → loot recovered (✅ commodity
family) → research available (✅ `research unlocked`) → geoscape (✅)**. Both are P1 in
`ui-inventory.md` §12 "Mission bookends".

⛔ **Neither screen appears on three missions**, and the exclusion list is the same in both places
that consult it (`X2TacticalGameRuleset.uc:5047` for the push, `:5322` for the wait loop):

```
!bPromptForRestart && !`REPLAY.bInTutorial && !IsFinalMission() && !IsLostAndAbandonedMission()
```

So the TUTORIAL has no mission summary at all — which for once cuts the usual way round, since
Gatecrasher on a tutorial-off campaign is already the mission every event-bus feature must be
tested on (CLAUDE.md, "No gameplay event fires in the TUTORIAL"). A LOSS that offers a restart
goes to `UICombatLose` instead, which is a `UIAlert` subclass and already covered by the popup
recipe (`game_ui_popup.cpp` lists it). Challenge mode and the Ladder modes push their own screens
(`UIChallengeModeSummaryScreen`, `UITLELadder*`) and are out of scope.

⚠ `UIMissionSummary` is also the MULTIPLAYER post-match screen — `BattleData.IsMultiplayer()`
picks a different set of arguments for the same Flash call. The read below is positional, so MP
gets the right words for free; nothing about it has been or will be tested (no second player).

---

## 2. What a sighted player sees

`UIMissionSummary`, top to bottom (English loc strings quoted from `XComGame.int`
`[UIMissionSummary]`):

| On screen | Source |
|---|---|
| `MISSION COMPLETE` / `MISSION FAILED` / `MISSION ABORTED` | `m_strMissionComplete` / `m_strMissionFailed` / `m_strMissionAbandoned`, colour-wrapped |
| `OPERATION AVENGING SERPENT` | `BattleData.m_strOpName` |
| `MISSION TYPE:` + the type | `m_strMissionTypeLabel` + `MissionTemplate.PostMissionType` |
| `OBJECTIVE:` + `COMPLETED`/`FAILED`, **or** `CIVILIANS RESCUED:` + `n/m` | `GetObjectiveLabel()` / `GetObjectiveValue()` — the label itself SWITCHES on `MissionFamily == "Terror" \|\| "ChosenRetaliation"` |
| `TURNS TAKEN:` + count | `m_strTurnsTakenLabel` + the XCom `XComGameState_Player.PlayerTurnCount` |
| `ENEMIES KILLED:` + `k/n` | `m_strEnemiesKilledLabel` + `GetEnemiesKilled()` |
| `SOLDIERS WOUNDED:` + count | `m_strSoldiersWoundedLabel` + `GetSoldiersInjured()` |
| `SOLDIERS KILLED:` + count | `m_strSoldiersKilledLabel` + `GetSoldiersKilled()` |
| `RATING:` + `FLAWLESS`/`EXCELLENT`/`GOOD`/`FAIR`/`POOR` | `m_strRatingLabel` + `GetMissionRating()` |
| `Take Mission Photo` hotlink, bottom right | `m_PosterButton`, label `m_strMissionPhotobooth` |
| `CONTINUE` nav help | `NavHelp.AddContinueButton(CloseScreenTakePhoto)` |

⚠ `ITEMS RECOVERED:` (`m_strLootRecoveredLabel`) is declared, localized and **never used** — both
call sites pass `""` for that pair. Loot is reported two screens later, on
`UIInventory_LootRecovered`. A reader that speaks the label because the loc file has one would
invent a row the screen does not draw.

`UIChosenMissionSummary`, from `[UIChosenMissionSummary]`:

| On screen | Source |
|---|---|
| `CHOSEN ENCOUNTER` + (empty subtitle) | `ChosenEncounterTitle`, `ChosenEncounterSubtitle` — the subtitle ships as `""` |
| the Chosen's class, name, nickname | `ChosenState.GetChosenClassName()` / `GetChosenName()` / `GetChosenNickname()` |
| a flavour paragraph | `GetChosenNarrativeFlavor()` |
| `DEFEATED` badge over the portrait | `ChosenDefeatedLabel`, only when `UnitState.IsDead()` |
| `CHOSEN STRENGTHS:` + comma-joined list | `StrengthsLabel` + `GetStrengthsList()` |
| `CHOSEN WEAKNESSES:` + comma-joined list | `WeaknessesLabel` + `GetWeaknessesList()` |
| ONE of three outcome blocks | see §3.2 |

⚠ `ChosenActionsLabel` ("ACTIONS TAKEN:") and `CasualtiesLabel` ("SOLDIERS KILLED:") are declared
and localized on this class and **pushed nowhere** — same trap as the loot pair, and the same
answer: the wire is what the screen drew, not what the loc file holds.

---

## 3. What is readable — and the one route that works

⛔ **Both screens retain nothing.** Every visible word goes out through `MC.BeginFunctionOp` /
`QueueString` and is kept in no property: `UIMissionSummary.SetMissionInfo` is ONE call carrying
all nineteen strings, and `UIChosenMissionSummary` splits its across five. This is the house style
CLAUDE.md names, not a quirk of these two.

So the route is the **Flash command-queue capture** (`src/flash_shadow.cpp`), the same one the
research report and research-available decks use, keyed by the screen's own
`UIMCController.CacheIndex` — both screens push through their OWN `MC` (`MCName` =
`theMissionSummary` / `theScreen`), so `FlashShadow::PanelCacheIndex(screen)` is the right index
and no library panel is involved.

The alternative was considered and rejected: `UIMissionSummary`'s getters (`GetEnemiesKilled`,
`GetMissionRating`, `GetObjectiveValue`, …) are all pure reads and could be ProcessEvent'd on the
live instance, which is what "read it FROM the screen" would normally mean. Three things decide
against it. The **labels** would still have to come from `m_str*` on the CDO, which is exactly the
shape of the `UITacticalHUD_InfoPanel` bug (a `localized` var declared on a class that does not
populate it, reading `""` and voicing a double space). `GetObjectiveLabel` **switches** between two
labels on the mission family, and only the push knows which one was drawn. And re-deriving means a
second implementation of nineteen fields that drifts from Firaxis' every patch. The capture is what
the player is looking at, by construction.

### 3.1 `SetMissionInfo` — read by POSITION

`UIMissionSummary.uc:533-575`. 21 operands; 0, 19 and 20 are numbers, which `Call::StringArgs()`
renders as empty slots so nothing after them shifts (the capture's standing rule).

| # | Single player | Multiplayer |
|---|---|---|
| 0 | `MissionStatus` (0 = success) — a number | same |
| 1 | mission result headline | same |
| 2 | `BattleData.m_strOpName` | the MP mode name |
| 3 / 4 | `MISSION TYPE:` / `PostMissionType` | points-total label / value |
| 5 / 6 | objective **or** civilians-rescued label / value | turn-time label / value |
| 7 / 8 | `TURNS TAKEN:` / count | `""` / `""` |
| 9 / 10 | **always `""` / `""`** (loot) | `""` / `""` |
| 11 / 12 | `ENEMIES KILLED:` / `k/n` | same |
| 13 / 14 | `SOLDIERS WOUNDED:` / count | same |
| 15 / 16 | `SOLDIERS KILLED:` / count | same |
| 17 / 18 | `RATING:` / rating word | same |
| 19 / 20 | enemies killed, enemies total — numbers, and a DUPLICATE of 12 | same |

Every value at an odd index above 1 arrives wrapped by
`UIUtilities_Text.GetColoredText` — `<font color='#…'>…</font>`, which `CleanText` strips. The
labels arrive with their own trailing `:` already on them, which is why the label→value join must
go through `AppendPhrase` (a `:` is one of `kPhraseTerminators`, so it takes a plain space and not
a comma).

### 3.2 The Chosen screen's five calls, read by NAME

| Call | Operands | Notes |
|---|---|---|
| `SetChosenData` | 0 title, 1 subtitle, 2 class, 3 name, 4 nickname, 5 flavour, 6 strengths label, 7 strengths, 8 weaknesses label, 9 weaknesses | always pushed |
| `SetChosenImage` | **0 portrait asset path**, 1 `DEFEATED` or `""` | always pushed |
| `SetChosenReward` | 0 `REWARDS:`, 1 `+N`, 2 `ABILITY POINTS` | on defeat; also pushed with three EMPTY strings on the plain-encounter branch |
| `SetChosenKnowledge` | 0 label, **1 start %** (number), **2 end %** (number), 3 body text | on knowledge extraction only |
| `SetSoldierCapture` | 0 `SOLDIER CAPTURED:`, **1 class icon path**, **2 rank icon path**, 3 rank, 4 name, 5 class, **6 headshot path** | on kidnap only |
| `SetChosenIcon` | a bool then N `img:///…` paths | **nothing here is ever spoken** |

⛔ **Four of those operands are asset paths and one call is nothing but asset paths.** Speaking an
`img:///UILibrary_XPACK…` at a player is the failure the popup recipe's asset filter exists to
prevent; here the positions are known from source, so they are skipped by index rather than
sniffed. The two knowledge percentages are `QueueNumber`, so they are read off
`Call::args[i].n` — `StringArgs()` shows them as empty by design.

### 3.3 The two offsets that are NOT from the capture

Both on `UIMissionSummary`, both transcribed from the regenerated dump and pinned in
`sdk_layout_checks.cpp`:

| Field | Offset | Why |
|---|---|---|
| `m_PosterButton` | `0x0464` | the photobooth hotlink — read through `ReadButtonField`, which answers "absent, hidden or unlabelled" in one, and `Hide()` is exactly how the game suppresses it |
| `m_strContinue` | `0x04C4` | the Continue label. Confirmed present in `[UIMissionSummary]` of `XComGame.int` (`"CONTINUE"`), so unlike the research report's this word does NOT have to be ours |

`UIChosenMissionSummary` needs no offsets at all.

---

## 4. Input: what vanilla does

`UIMissionSummary.OnUnrealCommand` (`:646-681`) and `UIChosenMissionSummary.OnUnrealCommand`
(`:229-252`), both behind `CheckInputIsReleaseOrDirectionRepeat`:

| Key | Mission summary | Chosen summary |
|---|---|---|
| Enter / Space / A | `CloseScreenTakePhoto()` → auto-photo, then close | `CloseScreen()` |
| Escape / B | **consumed, does nothing** — deliberate, commented "so there is no UI functionality in Mission Summary" | same |
| Y (gamepad) | `CloseThenOpenPhotographerScreen()` unless MP or all soldiers dead | — |
| Start | pause menu (SP only) | pause menu |
| anything else | falls through to `super` |

`OnInit` ends with `Navigator.Clear()` on the mission summary: there is no focusable widget and no
keyboard route to the poster button at all. A sighted mouse player clicks it; a sighted keyboard
player cannot reach it either. It is ON SCREEN, so it earns a stop (sighted parity), and its
activation goes through the screen's own zero-parameter `CloseThenOpenPhotographerScreen` — the
same function the button's delegate wraps.

⚠ Activating it lands the player in `UIPhotographerScreen`, which has **no reader** (photobooth is
P3, "inherently visual"). That is the same deliberate hole `UIAfterAction`'s Memorialize button
already has, and the same answer: a sighted player can go there too, so the node exists and the
screen behind it is a separate entry.

Continue goes through `SendScreenCommand(screen, kCmdEnter)` rather than calling
`CloseScreenTakePhoto` by name — the INPUT path, per the hard rule, so the auto-photo, the camera
fade and `bWaitingForMissionSummary` all sequence exactly as the game expects.

---

## 5. Traps

1. **The loot pair and the Chosen screen's two unused labels are localized and never drawn**
   (§2). Read from the push, never from the loc file.
2. **`m_kTacticalHUD.Hide()` runs before the push** (`XComPresentationLayer.uc:884`), but the HUD
   stays on the stack — so `ReadTacticalGate` sees an unknown screen ABOVE `UITacticalHUD` and
   raises `blocked`, the ambient tactical recipe's probe refuses, and the surface is free. That is
   already the designed behaviour: `kTacticalPassives`' own comment names "mission summary" as the
   example of an unknown modal that must block. No change needed there, but it is why these
   recipes must sit ABOVE `kTacticalHudRecipe` in `kRecipes`.
3. **`CurrentTechIndex`-style clamping has no analogue here, but the capture's LRU does.** A whole
   mission's worth of Flash pushes runs between the capture's install and this screen; the panel
   cap is 2048 with `CLEANUP_OP` eviction, and this screen's index is written at `OnInit`, i.e.
   moments before the first read. If §7 finds an empty capture, `just dev-flashqueue` against the
   screen's cache index is the first thing to look at, not the offsets.
4. **The two screens are sequential, never stacked** — the Chosen summary pops itself and *then*
   pushes the mission summary. Their relative order in `kRecipes` is therefore free.
5. **`GetObjectiveLabel` switching between "OBJECTIVE:" and "CIVILIANS RESCUED:"** means the same
   node id carries two different meanings across missions. The id is positional (`stat/objective`)
   and the label rides in the spoken text, which is correct — but it is the reason the label is
   never assumed.
6. **The screen consumes Escape.** Our layer must not offer a "close" verb that Escape would
   normally serve; the only exit is Continue.

---

## 6. The shape it takes — two recipes, no companion

Native-only, in the TACTICAL family (`game_ui_tactical.cpp` + `recipes/recipe_tactical.cpp`):
these screens are pushed by `XComPresentationLayer` while the tactical map is still loaded, and
`UIMissionSummary.CloseScreen` branches on `Movie.Pres.IsA('XComHQPresentationLayer')` — they
belong to the layer the mission ran in, not to the Avenger. No companion class: every word is in
the capture, and the two remaining facts are widget reads.

**`mission summary`**

- Screen name: the result headline joined to the operation name — what a sighted player takes in
  at a glance, and the same shape `AfterActionName` uses (header + op).
- Stop `summary`: `result`, `op`, then one node per label/value pair in screen order (type,
  objective, turns, enemies, wounded, killed, rating), each label and value joined through
  `AppendPhrase` so the loc strings' own trailing `:` is honoured. A pair with both halves empty
  emits NO node — that is the loot pair, and it is also why the node ids are named
  (`stat/rating`), never numbered: an empty pair must not renumber the ones after it.
- `SetStart` lands on the first STAT node, not on `result`. The result is already the screen name
  and would otherwise be said twice in a row (the research report's rule); putting it in the graph
  anyway is the deliberate difference, because unlike a generic "RESEARCH REPORT" header the
  result is the single most important fact on the screen and has to stay re-readable.
- Stop `actions`: Continue (label from `m_strContinue`), and the photobooth hotlink when
  `ReadButtonField` says it is visible and labelled.

**`chosen summary`**

- Screen name: `SetChosenData` operand 0 (`CHOSEN ENCOUNTER`).
- Stop `chosen`: identity (class, name, nickname joined), the `DEFEATED` badge when present, the
  flavour paragraph, strengths and weaknesses as label+value pairs, then whichever outcome block
  was pushed — reward, knowledge (with both percentages), or captured soldier.
- Stop `actions`: Continue.

Neither recipe sets `SetListIndex`: there is no `UIList` and no navigable widget on either screen,
so there is nothing for focus write-back to write to. Same as the research report.

---

## 7. Open questions for the live pass

Everything below needs one Gatecrasher-style mission on a **tutorial-off** campaign, harness armed.

1. **Does the navigator attach at all?** `MenuNavigator::OnTick` refuses while the screen stack
   reports non-interactive or a movie is playing, and nothing in this recon can say what
   `IsInputBlocked` / `ScreensVisible` / `bCinematicMode` read during the end-of-mission fade.
   `just dev-log` carries the gate line ("gated — nothing is navigable", with the three flags) and
   `/health` carries the navigator's attach state. This is question ONE: if the stack is gated
   here the way the hacking screen's is, the fix is the hacking screen's (`hacking_screen.cpp:41`
   — cinematic mode on the stack does not mean "not navigable"), not anything below.
2. **Is the capture there?** `just dev-flashqueue` for the screen's cache index must show
   `SetMissionInfo` with 21 operands. Zero operands means trap §5.3; a missing index means the
   screen pushed through a panel rather than its own `MC`, which would contradict the source.
   ⚠ And listen for a TRANSIENT miss: `UIPanel.OnInit` sets `bIsInited` before
   `UIMissionSummary.OnInit` makes its push, and the push is only captured when
   `ProcessQueuedCommands` next runs `CopyCommandQueue` — so there is a window of at most one
   poll where the screen probes ready with nothing captured, and the recipe would speak
   `SummaryNoText` and then immediately be corrected. Both deck recipes have the same window and
   nobody has heard it yet; if it is audible here, the fix is a first-poll grace rather than
   anything about the offsets, and it belongs to all three recipes at once.
3. **The empty-pair rule.** Confirm nothing speaks a bare "ITEMS RECOVERED:" and that the stat
   nodes read in the drawn order with no gap.
4. **The label switch**: a Terror or Chosen-retaliation mission must say "CIVILIANS RESCUED: 4/6"
   where an ordinary one says "OBJECTIVE: COMPLETED".
5. **Continue actually continues** — Enter on the node must run the auto-photo and hand over to
   `UIAfterAction`, with no second press needed and no stall on `bWaitingForMissionSummary`. Watch
   for the navigator detaching cleanly rather than holding a corpse across the map unload.
6. **The photobooth node** appears on a normal win, is ABSENT after a total squad wipe
   (`bAllSoldiersDead`), and opening it leaves the player in a screen with no reader — confirm
   Escape gets them back out.
7. **The Chosen screen** needs a mission with a Chosen on it, and its three outcome branches are
   three separate missions: a plain encounter (blank reward — nothing should be spoken), a
   knowledge extraction, and a soldier kidnapped. Defeating a Chosen (the reward branch) is the
   rarest and the one that carries `+N ABILITY POINTS`.
8. **Nothing speaks an asset path.** `grep -n 'img:///\|UILibrary\|\.tga' X2Access.log` over the
   speech ring after the run — the Chosen screen pushes six of them.
9. The usual post-run shape-count of the speech ring, and a `grep -P '  '` for the double-space
   tell of an empty label.
