# UI screen census — the accessibility TODO

Every class in the shipped WOTC sources that transitively extends `UIScreen`: **290** (base
game 278, DLC_2 Alien Hunters 4, DLC_3 Shen's Last Gift 8; DLC_1 and TLE add no screen
classes of their own — the TLE screens live in `XComGame`). Generated 2026-08-23 by walking
`class X extends Y` declarations across `SrcOrig`'s `XComGame`, `DLC_1..3` and `TLE`
packages and closing over `UIScreen` (declarations can span two lines — a one-line regex
missed `UIArmory_MainMenu`). Reachability of doubtful names was checked by grepping for
external `class'Name'` references: five `UIStrategyMap_*` screens and `UIResistanceGifts`
have **zero** and are dead code.

**How to use this file**: it is the TODO. Before building anything, the rules still apply —
check the Surfaces table in `CLAUDE.md` (a second reader for a covered screen is the failure
mode it prevents), read the surface's own doc in `docs/surfaces/`, and answer the
recipe-vs-narrate-only question first. "Built" here means a surface exists; whether it has
**run live** is tracked in `CLAUDE.md` → Current state, not here.

Legend: ✅ built · ◐ partial (structure or gate only, words incomplete) · ⬜ TODO ·
⚠ verify first · 🚫 out of scope · — no words to read / not player-facing on PC.

Priorities: **P1** = a blind player cannot run a campaign without it. **P2** = playable
without, painful without. **P3** = completeness. Unmarked TODO rows inherit their section's
priority.

## Summary

| Bucket | Count | Disposition |
|---|---|---|
| Built (✅) | ~93 | done; live-run status in CLAUDE.md |
| Partial (◐) | 6 | words missing, structure present |
| TODO P1 | ~38 | the campaign-blocking list below |
| TODO P2 | ~36 | customization, reports, detail screens |
| TODO P3 | ~17 | login, credits, photobooth, DLC extras |
| Challenge / TLE | 20 | later, on demand |
| Multiplayer | 30 | 🚫 deprioritized (needs a second player; revisit on request) |
| Debug / dev screens | 20 | 🚫 never |
| Dead code | 6 | 🚫 unreachable in WOTC |
| Bases / infrastructure / no-words | ~20 | nothing to speak, or spoken via other surfaces |

## The P1 short list

What actually blocks a campaign, in rough play order:

1. **`UIMission` family (17 classes)** — the geoscape mission-launch briefings: objective,
   rewards, difficulty, dark events, sitreps. Today the player can move the tray but cannot
   read what they are launching. One layout family (`UIMission` base + per-mission-type
   subclasses), so one recipe should cover all 17 the way `UIAlert`'s 118 variants did.
2. **`UIArmory_MainMenu`** — the armory hub; every soldier operation routes through it.
3. **`UIArmory_Promotion` / `_PromotionHero` / `_PromotionPsiOp`** — ability choices. The
   hero screen is also what regular classes use once the Training Center retrofits them.
4. **`UIArmory_WeaponUpgrade`** + **`UIArmory_Implants`** — weapon mods and PCS.
5. **`UIInventoryTactical`** — mid-mission loot pickup.
6. **`UITacticalCharInfoScreen`** — F1 soldier details in tactical.
7. **`UIMissionSummary`** + **`UIChosenMissionSummary`** — the front of the post-mission
   chain: stats, then the Chosen result. The walk-up that follows them (`UIAfterAction`) is
   built (`after-action-recon.md`), so these two are what still leave a blind player pressing
   Enter into silence between the mission's last shot and the dropship briefing.
8. **`UIResistanceReport`** (+ `_ChosenEvents`, `_FactionEvents`, `UIRewardsRecap`) — the
   monthly council report chain.
9. **`UICovertActions`** + **`UICovertActionReport`** — the WOTC core loop. Surveyed:
   `covert-actions-recon.md`. The soldier picker and the confirm modal are already covered by the
   personnel and popup recipes, so this is two screens, not four — and `CONFIRM ACTION` is
   MOUSE-ONLY in vanilla (§4).
10. **`UIStrategyPolicy`** — resistance orders (WOTC's card system).
11. **`UIRecruitSoldiers`**, **`UIBlackMarket`** + **`UIBlackMarket_Sell`** — recruiting and
    selling. (`_Sell` is explicitly NOT in the commodity family — own list, own card call;
    `avenger-view-recon.md` §14.1.)

Plus one standing ⚠: **`UIShellStrategy`** below — possibly a live bug in an existing surface.

## 1. Boot, shell, main menu

| Screen | What it is | Status |
|---|---|---|
| `UIShell` | developer shell (`-review` skips it) | — |
| `UIFinalShell` | retail main menu | ✅ shell recipe |
| `UIShell3D` | 3D backdrop manager | — |
| `UIShell_NavHelpScreen` | shell nav-help overlay (spawned by shell/MP presentation) | ⚠ P3 — check it ever shows on PC |
| `UIStartScreen` | press-start (console) | — |
| `UICredits` | credits scroll | ⬜ P3 |
| `UIFontTest` | font test | 🚫 debug |
| `UILoginScreen` / `UIFiraxisLiveLogin` | my2K / Firaxis Live login | ⬜ P3 |
| `UISaveExplanationScreen` | ironman/save explanation popup | ⬜ P3 |
| `UIControllerMap` / `_Shell` / `_Tactical` | controller button map | — |
| `UIVirtualKeyboard` | controller on-screen keyboard (PC uses `UIInputDialogue`) | — |

## 2. Campaign setup

All ✅ — shell recipes: `UIShellDifficulty`, `UISecondWave`, `UIShellNarrativeContent`,
`UIShellSoundtrackPicker`, `UIChooseIronMan`.

## 3. Saves, options, global dialogs

| Screen | What it is | Status |
|---|---|---|
| `UISaveGame` / `UILoadGame` | save / load | ✅ (save half not yet run live) |
| `UIOptionsPCScreen` | settings | ✅ |
| `UIKeybindingsPCScreen` | key bindings — Flash-only, no `UIList` | ✅ legacy listener (`UIScreenListener_KeyBindings`) |
| `UIPauseMenu` | pause menu (tactical) | ✅ |
| `UIShellStrategy` | **the Avenger's Escape menu** — `extends UIPauseMenu`, spawned by `XComHQPresentationLayer` | ⚠ **verify the pause recipe attaches**: probing is by exact class name, and this is not named `UIPauseMenu` |
| `UIDialogueBox` | the universal modal (confirmations, errors) | ✅ |
| `UIInputDialogue` | text entry | ✅ observer |
| `UIProgressDialogue` | blocking progress spinner | ✅ popup recipe |
| `UIReconnectController` | controller-disconnect variant of the above | ⚠ P3 — chain-covered only if the popup probe walks the class chain |
| `UIRedScreen` | dev warning overlay (`-noRedScreens` suppresses) | ✅ |

## 4. Avenger — HUD, rooms, facilities

| Screen | What it is | Status |
|---|---|---|
| `UIAvengerHUD` | persistent strategy HUD | ◐ — ant farm done; **resource strip, `UIToDoWidget`, `UIEventQueue` are panels of this screen** (CLAUDE.md next-steps 10) |
| `UIFacilityGrid` | the ant farm | ✅ narrate-only |
| `UIRoom` | debris room | ✅ |
| `UIFacility` + 19 subclasses | inside a facility (`_Academy`, `_AdvancedWarfareCenter`, `_Armory`, `_BarMemorial`, `_CIC`, `_CovertActions`, `_Infirmary`, `_Labs`, `_LivingQuarters`, `_Powercore`, `_PowerGenerator`, `_ProvingGround`, `_PsiLab`, `_ResistanceComms`, `_ShadowChamber`, `_Storage`, `_TrainingCenter`, `_UFODefense`, `_Workshop`, DLC_2 `_HuntersLodge`) | ✅ facility reader (Flash-only status panels still ⬜ — next-steps 10) |
| `UIBuildFacilities` | build-mode grid overlay | ✅ |
| `UIChooseFacility` | facility picker | ✅ |
| `UIChooseUpgrade` | facility upgrade picker (spawned from `UIFacility`/`UIAvengerHUD`) | ⬜ P2 |
| `UIFacilityUpgrade` | facility upgrade screen (spawned from `UIAvengerHUD`) | ⬜ P2 — `UIButton.SetText`-based, natively readable per CLAUDE.md |
| `UIFacilitySummary` | all-facilities overview list | ⬜ P2 |
| `UIHuntersLodge3D` | DLC_2 trophy wall viewer | ⬜ P3 |

## 5. Commodity family — ✅, one recipe, 19 classes

`UISimpleCommodityScreen`, `UIInventory` (bases) + `UIChooseResearch`, `UIResearchArchives`,
`UISchematicArchives`, `UIShadowChamberArchives`, `UIChooseProject`, `UIChoosePsiAbility`,
`UIViewObjectives`, `UIChooseClass`, `UIOfficerTrainingSchool`, `UIResistanceGoods`,
`UIResistanceOps`, `UIBlackMarket_Buy`, `UIInventory_BuildItems`, `_Storage`, `_Implants`,
`_XComDatabase`, `_LootRecovered`. Matched by class chain, so the archives subclasses ride
along. `avenger-view-recon.md` §14.

## 6. Personnel and recruiting

| Screen | What it is | Status |
|---|---|---|
| `UIPersonnel` + 10 subclasses | roster picker (`_Armory`, `_BarMemorial`, `_BoostSoldier`, `_BuildFacility`, `_ChooseResearch`, `_CovertAction`, `_LivingQuarters`, `_SpecialFeature`, `_SquadSelect`, `_TrainingCenter`) | ✅ personnel roster (family via class chain) |
| `UIRecruitSoldiers` | recruit list | ⬜ **P1** |
| `UIBarMemorial_Details` | fallen-soldier detail card | ⬜ P2 |
| `UISoldierBondScreen` | bond overview for a soldier | ⬜ P2 |
| `UISoldierBondConfirmScreen` | confirm-bond screen | ⬜ P2 |

## 7. Armory and soldier management

| Screen | What it is | Status |
|---|---|---|
| `UIArmory` | abstract base (unit switching, rotation) | base |
| `UIArmory_MainMenu` | the armory hub menu | ⬜ **P1** |
| `UIArmory_Loadout` | loadout | ✅ |
| `UIArmory_WeaponUpgrade` | weapon mods | ⬜ **P1** |
| `UIArmory_WeaponTrait` | TLE legacy weapon customization | ⬜ P3 |
| `UIArmory_Promotion` | ability tree (base classes) | ⬜ **P1** |
| `UIArmory_PromotionHero` | faction-hero / Training-Center tree | ⬜ **P1** |
| `UIArmory_PromotionPsiOp` | psi-op training tree | ⬜ **P1** |
| `UIArmory_Implants` | PCS slots | ⬜ **P1** |
| `UIAbilityInfoScreen` | ability details viewer (from armory & challenge) | ⬜ P2 |
| `UIManageEquipmentMenu` | equipment mgmt popup (from loadout & squad select) | ⬜ P2 |

## 8. Customization and character pool — ⬜ P2, one family

`UICustomize` (base) + `_Menu`, `_Info`, `_Head`, `_Body`, `_Props`, `_Weapon`, `_Trait`,
`_Voice`, `_TemplarInfo`, `_SkirmisherHead`, DLC_3 `_SparkMenu`, `_SparkInfo`, `_SparkHead`,
`_SparkBody`, `_SparkProps`, `_SparkWeapon` — 17 classes, one list-based layout family.
Plus `UICharacterPool` and `UICharacterPool_ListPools` (pool management, reached from the
main menu). Cosmetic outcomes are invisible to the player, but names, voices, attitudes and
the pool itself are not cosmetic — P2, and the voice picker matters most.

## 9. Geoscape

| Screen | What it is | Status |
|---|---|---|
| `UIStrategyMap` | the geoscape | ◐ — tray built (`geo_scanner`); map-item details beyond the tray unmeasured |
| `UIResistance` | region contact/outpost screen (from a region pin) | ⬜ P2 |
| `UIAdventOperations` | dark events | ✅ popup recipe |
| `UIResistanceReport` (+`_ChosenEvents`, `_FactionEvents`) | monthly council report | ⬜ **P1** |
| `UIRewardsRecap` | monthly rewards recap | ⬜ **P1** |
| `UIStrategyPolicy` | resistance orders | ✅ resistance orders recipe (`surfaces/strategy.md`) |
| `UICovertActions` | covert action picker | ✅ covert actions recipe (`covert-actions-recon.md`) |
| `UICovertActionReport` | covert action result | ⬜ **P1** — `covert-actions-recon.md` §8b |
| `UIChosenInfo` | Chosen dossier | ⬜ P2 |
| `UIChosenEncountered` | first-encounter overlay | ⬜ P2 (mostly cinematic) |
| `UINotificationMenu` | notification list (opened from `UIToDoWidget`) | ⬜ P2, with the to-do widget work |
| `UISkyrangerArrives` | squad-return overlay | ⬜ P3 |
| `UIStrategyMap_BlackMarket`/`_BuyIntel`/`_Outpost`/`_PoiChoice`/`_VisitCommander` | EU/EW leftovers, zero references | 🚫 dead |

## 10. Mission launch — `UIMission` family, ◐ built, 17 classes

`UIMission` (base) + `_GOps`, `_Council`, `_Retaliation`, `_SupplyRaid`, `_LandedUFO`,
`_AlienFacility`, `_GoldenPath`, `_ResOps`, `_RescueSoldier`, `_ChosenAmbush`,
`_ChosenStronghold`, `_ChosenAvengerAssault`, `_LostAndAbandoned`, `_GPIntelOptions`,
DLC_2 `_AlienNest`, DLC_3 `_LostTowers`. One layout family (`UIX2SimpleScreen` panels +
launch/cancel). ◐ built 2026-08-31 — `mission blade` recipe + `X2AccessMissionReader`
(probe is IsKindOf `UIMission`, so the DLC pair and mod blades match too); recon and the
first-pass checklist in `mission-blade-recon.md`, live-run debt in `next-steps.md`.

## 11. Popups and alerts — ✅ popup recipe + transient reader

✅ recipe: `UIAlert` (118 variants), `UIAlert_DLC_Day60`, `UIAlert_DLC_Day90`,
`UIFactionPopup`, `UIAbilityPopup`, `UISoldierBondAlert`, `UIChosenFragmentRecovered`,
`UIChosenLevelUp`, `UIChosenReveal`, `UIUFOAttack`, `UISitRepInformation`, `UICombatLose`,
`UIProgressDialogue`, `UIRedScreen`. ✅ transient reader: `UINarrativeCommLink`,
`UIEventNotices` (fed by `UIEventNoticesTactical`, which is an `Object`, not a screen),
`UIStrategyTutorialBox`.

| Remaining | What it is | Status |
|---|---|---|
| `UINarrativePopup` | skippable narrative popup (spawned by `XComPresentationLayerBase`) | ⬜ P2 — measure what triggers it |
| `UISoldierCaptured` | Chosen captured a soldier | ⬜ P2 |

## 12. Mission bookends

| Screen | What it is | Status |
|---|---|---|
| `UISquadSelect` | squad select | ✅ |
| `UIDropShipBriefingBase` + `_MissionStart` / `_MissionEnd` | Skyranger briefings | ✅ |
| `UIDropshipHUD` | interactive loading-screen HUD | ⚠ P3 — measure overlap with the briefing screens |
| `UIMissionIntro` | mission-type intro banner | ⬜ P2 |
| `UIMissionSummary` | post-mission stats | ⬜ **P1** |
| `UIChosenMissionSummary` | Chosen encounter result | ⬜ **P1** |
| `UIAfterAction` | post-mission squad walk-up (wounds, promotions) | ◐ built — `after action` recipe + `X2AccessAfterActionReader` (`after-action-recon.md`); first live pass owed |
| `UIResearchReport` | research complete | ✅ research report recipe (`research-report-recon.md`) |
| `UIResearchUnlocked` | new research available | ⬜ P2 |
| `UIEndGameStats` | campaign-end stats | ⬜ P3 |
| `UISoldierIntroCinematic` | soldier intro cinematic card | ⬜ P3 (mostly visual) |

## 13. Tactical

| Screen | What it is | Status |
|---|---|---|
| `UITacticalHUD` | the tactical HUD | ◐ — targeting, ability bar, tile cursor, scanner, free aim, unit readout built; full HUD recipe is next-steps 10 |
| `UIHackingScreen` | hacking | ✅ |
| `UISpecialMissionHUD` | mission timers | ◐ |
| `UITurnOverlay` | turn banner (initiative list is cheat-gated — never narrate) | ◐ via event narration |
| `UIUnitFlagManager` | per-unit HP flags | ◐ — facts come from the unit readout / narration |
| `UIEnemyArrowContainer` | enemy direction arrows | — (enemy strip aid owns this) |
| `UITacticalHUDMessageBanner` | mission banner — retains nothing | — Flash-only; state narration covers it |
| `UITacticalCharInfoScreen` | F1 soldier details | ⬜ **P1** |
| `UIInventoryTactical` | loot pickup | ⬜ **P1** |
| `UITutorialBox` / `UITutorialHelper` | tutorial blades / pointer overlay | ✅ / ⚠ check the helper carries no unread text |
| `UIReplay` | tutorial replay control | ✅ |
| `UIWorldMessageMgr` | world flyovers (polled) | ✅ |
| `UIMessageMgrBase` / `UIMessageMgr` / `UIAnchoredMessageMgr` | message-manager infrastructure | — (world messages are the readable one) |
| `UIMouseGuard` / `_RotatePawn`, `UIMouseCursor`, `UILoadScreenAnimation`, `UITooltipMgr` | input/visual infrastructure | — (`UIMouseGuard`/`UILoadScreenAnimation` already used as gates; tooltip CONTENT is a separate aid, e.g. hacking phase 4) |

## 14. Photobooth — P3, inherently visual

`UIPhotoboothBase`, `UIArmory_Photobooth`, `UITactical_Photobooth`, `UIPhotoboothReview`,
`UIPosterScreen`. A camera/pose tool; narrating filters and poses has little value without
sight. Revisit only on player request.

## 15. Challenge mode / TLE — later, on demand (20)

`UITLEScreen`, `UITLEHub`, `UITLE_ChallengeModeMenu`, `UITLE_LadderDifficulty`,
`UITLE_LadderModeMenu`, `UITLE_SkirmishModeMenu`, `UITLELadderEndScreen`,
`UITLELadderMedalProgressScreen`, `UITLELadderMedalScreen`, `UITLEUnitStats`,
`UIChallengeModeHUD`, `UIChallengeMode_SquadSelect`, `UIChallengeModeEventNotify`,
`UIChallengeModeScoringDialog`, `UIChallengePostScreen`, `UIChallengeLeaderboards`,
`UILadderModeHUD`, `UILadderModeScoringDialog`, `UILadderSoldierInfo`,
`UILadderUpgradeScreen`. The tactical layer's work transfers; the shells are new recipes.

## 16. Multiplayer — 🚫 deprioritized (30)

`UIMPShell_Base`, `_MainMenu`, `_ServerBrowser`, `_Lobby`, `_CustomGameCreateMenu`,
`_CustomGameMenu_Search`, `_Leaderboards`, `_SquadEditor` (+ `_AcceptInvite`,
`_CustomGameCreate`, `_CustomGameSearch`, `_Preset`, `_Quickmatch`, `_RankedGame`),
`_SquadLoadoutList` (+ 6 same variants), `_CharacterTemplateSelector`, `_UnitEditor`,
`UIArmory_Loadout_MP`, `UIMPUnitStats`, `UIMultiplayerChatManager`,
`UIMultiplayerDisconnectPopup`, `UIMultiplayerHUD`, `UIMultiplayerPlayerStats`,
`UIMultiplayerPostMatchSummary`. Needs a second player to even test; revisit on request.

## 17. Debug / dev — 🚫 never (20)

`UIDebugBehaviorTree`, `UIDebugChallengeMode`, `UIDebugGrid`, `UIDebugHistory`,
`UIDebugInfo`, `UIDebugItems`, `UIDebugMap`, `UIDebugMarketing`, `UIDebugMultiplayer`,
`UIDebugSafearea`, `UIDebugVisibility`, `UIDebugVisualizers`, `UIDynamicDebugScreen`,
`UIGraphicsOptionsDebugScreen`, `UIAuthorRegions`, `UIFontTest`, `UITacticalQuickLaunch`
(+ `_MapData`, `_SquadLoadout`, `_UnitAbilities`).

## 18. Dead code — 🚫 (6)

`UIStrategyMap_BlackMarket`, `UIStrategyMap_BuyIntel`, `UIStrategyMap_Outpost`,
`UIStrategyMap_PoiChoice`, `UIStrategyMap_VisitCommander`, `UIResistanceGifts` — zero
external references in the shipped sources.

## Black market (splits across three rows above, gathered here)

`UIBlackMarket` (entry) ⬜ P1 · `UIBlackMarket_Buy` ✅ commodity · `UIBlackMarket_Sell` ⬜ P1
(own layout, NOT commodity-family).

## Not screens, but real work items

Panels and subsystems that are TODO surfaces in their own right, already tracked in
`next-steps.md` — listed so this census reads complete:

- `UIPersonnel_DropDown` — staff drop-down, a `UIPanel`, not part of the personnel family.
- `UIToDoWidget` + `UINotificationMenu`, `UIEventQueue`, the `UIAvengerHUD` resource strip.
- Facility Flash-only status panels (via the companion).
- `UITacticalHUD_HackingTooltip` → the pre-commit hack odds read (hacking phase 4).
- Tooltips generally (`UITooltipMgr` + `UITooltip` family): mouse-hover-only facts need
  per-case parity decisions, not a blanket reader.
