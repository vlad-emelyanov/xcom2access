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
| Partial (◐) | ~26 | words missing, structure present — or built from source and never run (the customization family, the pool, 2026-09-04) |
| TODO P1 | 0 | the campaign-blocking list below is empty as of 2026-09-03 — seven ◐ entries owe a live pass |
| TODO P2 | ~15 | reports, detail screens, `UIResistance` gone (dead code) and the customization family gone (built) |
| TODO P3 | ~17 | login, credits, photobooth, DLC extras |
| Challenge / TLE | 20 | later, on demand |
| Multiplayer | 30 | 🚫 deprioritized (needs a second player; revisit on request) |
| Debug / dev screens | 20 | 🚫 never |
| Dead code | 7 | 🚫 unreachable in WOTC |
| Bases / infrastructure / no-words | ~20 | nothing to speak, or spoken via other surfaces |

## The P1 short list

What actually blocks a campaign, in rough play order:

⚠ This list holds only what is NOT built. An entry that ships leaves it rather than gaining a
"done" mark, and what the built surface still OWES moves to `next-steps.md` — so a name appearing
here and a `◐`/`✅` in the tables below is a contradiction, not a nuance. (It happened: the
promotion screens sat here for weeks after they were written.)

*Empty as of 2026-09-03.* The last seven came off in one sweep, all built from source and
unexercised live — their first passes are `next-steps.md`'s: the armory hub and weapon upgrades
(`armory` and `weapon upgrades`, `armory-recon.md`; the PCS path turned out to be the commodity
list's already, with `UIArmory_Implants` dead code), the loot receipt and the soldier sheet
(`loot` and `soldier info`, `tactical-modals-recon.md`), the rewards recap and the recruit list
(`rewards recap` and `recruits`, `recruit-recap-recon.md`), and the covert action result
(`covert report`, `covert-actions-recon.md` §8b). What a campaign still cannot be run without is
now a question for a live pass, not for this list.

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
| `MCM_OptionsScreen` (Mod Config Menu, Workshop 667104300 — not a shipped class) | mod settings, pushed over `UIOptionsPCScreen` by the MOD SETTINGS button it injects there | ◐ built from source, unrun (`mod-config-menu-recon.md`; the first mod-provided screen, `game_ui_mods.cpp`) |
| `UIKeybindingsPCScreen` | key bindings — Flash-only, no `UIList` | ✅ legacy listener (`UIScreenListener_KeyBindings`) |
| `UIPauseMenu` | pause menu (tactical) | ✅ |
| `UIShellStrategy` | `extends UIPauseMenu` — the **developer** shell's strategy menu, NOT the Avenger's Escape menu | 🚫 dev-only, and the ⚠ it used to carry is answered: `XComPresentationLayerBase.UIPauseMenu()` spawns `class'UIPauseMenu'` LITERALLY for every layer including the HQ's, so the Avenger's Escape menu really is a plain `UIPauseMenu` and the exact-name probe is right. The only caller of `UIStrategyShell()` that runs is `UIShell.uc:357`, which `-review` skips; the HQ layer's copy sits under its own "BEGIN DEPRECATED UI" banner |
| `UIDialogueBox` | the universal modal (confirmations, errors) | ✅ |
| `UIInputDialogue` | text entry | ✅ observer |
| `UIProgressDialogue` | blocking progress spinner | ✅ popup recipe |
| `UIReconnectController` | controller-disconnect variant of the above | ⚠ P3 — chain-covered only if the popup probe walks the class chain |
| `UIRedScreen` | dev warning overlay (`-noRedScreens` suppresses) | ✅ |

## 4. Avenger — HUD, rooms, facilities

| Screen | What it is | Status |
|---|---|---|
| `UIAvengerHUD` | persistent strategy HUD | ✅ `Avenger HUD` ambient recipe (2026-09-03, unexercised live): resource strip, objectives + dark events, `UIToDoWidget`, `UIEventQueue` + date, shortcut buttons + hologlobe — Tab on the ant farm (`surfaces/avenger.md`). The strip is also a trailing stop in six other recipes and on `R`. Still ⬜: `StaffIcons` (unstaffed engineers) and the crew status indicators (an aid, not parity — `next-steps.md`) |
| `UIFacilityGrid` | the ant farm | ✅ narrate-only for the rooms; the HUD around it is the ambient recipe above |
| `UIRoom` | debris room | ✅ |
| `UIFacility` + 19 subclasses | inside a facility (`_Academy`, `_AdvancedWarfareCenter`, `_Armory`, `_BarMemorial`, `_CIC`, `_CovertActions`, `_Infirmary`, `_Labs`, `_LivingQuarters`, `_Powercore`, `_PowerGenerator`, `_ProvingGround`, `_PsiLab`, `_ResistanceComms`, `_ShadowChamber`, `_Storage`, `_TrainingCenter`, `_UFODefense`, `_Workshop`, DLC_2 `_HuntersLodge`) | ✅ facility reader (Flash-only status panels still ⬜ — next-steps 10) |
| `UIBuildFacilities` | build-mode grid overlay | ✅ |
| `UIChooseFacility` | facility picker | ✅ |
| `UIChooseUpgrade` | THE facility upgrade screen — what `XComHQPresentationLayer.UIFacilityUpgrade()` actually spawns, from the room's UPGRADE button, a locked staff slot, the to-do widget and the ant farm | ✅ `facility upgrades` recipe + `X2AccessUpgradeReader` (2026-09-03, unexercised live) |
| `UIFacilityUpgrade` | DEAD CODE — shipped, never instantiated; `UIFacilityUpgrade()` on the presentation layer spawns `UIChooseUpgrade` instead | — out of scope (its `m_strCompleted` loc string is borrowed by the reader above) |
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
| `UIRecruitSoldiers` | recruit list | ◐ built — `recruits` recipe + `X2AccessRecruitReader` (2026-09-03, unexercised live; `recruit-recap-recon.md` §1). A row retains nothing; the join is position against `m_arrRecruits` |
| `UIBarMemorial_Details` | fallen-soldier detail card | ⬜ P2 |
| `UISoldierBondScreen` | bond overview for a soldier | ◐ built — `soldier bonds` recipe + `X2AccessBondReader` (2026-09-03, unexercised live; `chosen-bond-recon.md`). The roster is a `GraphSheet`; CREATE BOND, sorting and the soldier cycle are re-provided |
| `UISoldierBondConfirmScreen` | confirm-bond screen | ◐ built — `bond confirm` recipe, same reader (2026-09-03, unexercised live) |

## 7. Armory and soldier management

| Screen | What it is | Status |
|---|---|---|
| `UIArmory` | abstract base (unit switching, rotation) | base |
| `UIArmory_MainMenu` | the armory hub menu | ◐ built — `armory` recipe + `X2AccessArmoryReader` (2026-09-03, unexercised live; `armory-recon.md` §1). ⛔ Written against the Highlander's rewrite (Issue #47): rows are keyed on MCName |
| `UIArmory_Loadout` | loadout | ✅ |
| `UIArmory_WeaponUpgrade` | weapon mods | ◐ built — `weapon upgrades` recipe + `X2AccessWeaponUpgradeReader` (2026-09-03, unexercised live; `armory-recon.md` §2). ⛔ Highlander layout (`WeaponViewOffsets` heads the class); the stat block is a change-state preview reproduced by the companion |
| `UIArmory_WeaponTrait` | the weapon PATTERN picker (a subclass of the upgrade screen; not TLE-only) | ◐ same recipe — a plain list, Enter picks, Escape reverts |
| `UIArmory_Promotion` | ability tree (base classes) | ✅ `promotion` recipe + `X2AccessPromotionReader` — one recipe, all three classes |
| `UIArmory_PromotionHero` | faction-hero / Training-Center tree | ◐ same recipe, branch unexercised (`next-steps.md`). ⛔ Vanilla here is a KEYBOARD DEAD END (`game_ui_strategy.h`): its `OnUnrealCommand` takes DPAD/LStick only and `InitPromotion` calls `DisableNavigation()`, so the layer is the ONLY way in without a mouse |
| `UIArmory_PromotionPsiOp` | psi-op training tree | ◐ same recipe, branch unexercised — needs a Psi Lab and a trained operative |
| `UIArmory_Implants` | DEAD CODE — `XComHQPresentationLayer.UIArmory_Implants()` has no caller in the shipped sources or the Highlander; the hub's PCS row opens `UIInventory_Implants` (the commodity list, §5 — with the implant delta and cannot-equip word added 2026-09-03, `armory-recon.md` §3) | — out of scope |
| `UIAbilityInfoScreen` | ability details viewer (from armory & challenge) | ⬜ P2 |
| `UIManageEquipmentMenu` | the strip-equipment menu (from loadout & squad select) | ◐ built — `manage equipment` recipe, native only (2026-09-04, unexercised live; `customize-recon.md` §1). The pause menu's shape; its rows consume nothing, so Enter-to-the-list reaches the delegate |

## 8. Customization and character pool — ◐ built, one family

`UICustomize` (base) + `_Menu`, `_Info`, `_Head`, `_Body`, `_Props`, `_Weapon`, `_Trait`,
`_Voice`, `_TemplarInfo`, `_SkirmisherHead`, DLC_3 `_SparkMenu`, `_SparkInfo`, `_SparkHead`,
`_SparkBody`, `_SparkProps`, `_SparkWeapon` — 17 classes, one list-based layout family.
◐ built — one `customize` recipe + `X2AccessCustomizeReader` (2026-09-04, unexercised live;
`customize-recon.md` §2), probed as the topmost stack entry `IsKindOf UICustomize`, so the SPARK
classes (absent from the SDK dump) ride along. The rows retain their words; the picker keeps no
title and is named by the row that opened it; the colour selector reads as a grid of hues (an
aid). ⛔ Two vanilla keyboard traps: a mecha row consumes Enter before the list's delegate, and the
menu screen's checkboxes are `bReadOnly`, so they toggle through `SimulateMouseClickOnCheckbox`.
Plus `UICharacterPool` and `UICharacterPool_ListPools` (pool management, reached from the
main menu) — ◐ built, `character pool` + `pool files` recipes, native only (2026-09-04,
unexercised live; `customize-recon.md` §3). ⛔ Both are keyboard dead ends in vanilla (EDIT is a
gamepad button; the picker picks only for a controller), re-provided.

## 9. Geoscape

| Screen | What it is | Status |
|---|---|---|
| `UIStrategyMap` | the geoscape | ◐ — tray built (`geo_scanner`); map-item details beyond the tray unmeasured |
| `UIResistance` | region contact/outpost screen — `XComHQPresentationLayer.UIResistance()` exists, but its only caller (`XComGameState_WorldRegion.DisplaySelectionPrompt`) is commented out in vanilla and the Highlander alike; the region pin's own two verbs are the geoscape tray's stop 2 (`customize-recon.md` §4) | 🚫 dead |
| `UIAdventOperations` | dark events | ✅ popup recipe |
| `UIResistanceReport` | monthly council report — the report card | ◐ built — `monthly report` recipe + `X2AccessReportReader` (2026-09-03, unexercised live; `resistance-report-recon.md`) |
| `UIResistanceReport_ChosenEvents` | the report's Chosen page (knowledge, activities, planned actions) | ◐ built — `chosen activity` recipe + `X2AccessChosenReader` (2026-09-03, unexercised live; `resistance-report-recon.md`) |
| `UIResistanceReport_FactionEvents` | empty `BuildScreen`, and its presentation-layer opener has no caller in the shipped sources | 🚫 dead |
| `UIRewardsRecap` | the POST-MISSION rewards letter (not the monthly chain's — `recruit-recap-recon.md` §2.1) | ◐ built — `rewards recap` recipe + `X2AccessRecapReader` (2026-09-03, unexercised live). No widget tree at all; every word off the screen's own accessors |
| `UIStrategyPolicy` | resistance orders | ✅ resistance orders recipe (`surfaces/strategy.md`) |
| `UICovertActions` | covert action picker | ✅ covert actions recipe (`covert-actions-recon.md`) |
| `UICovertActionReport` | covert action result | ◐ built — `covert report` recipe + `X2AccessCovertActionReader.DescribeCovertActionReport` (2026-09-03, unexercised live; `covert-actions-recon.md` §8b). ⛔ Escape is Continue, and Continue deletes the action |
| `UIChosenInfo` | Chosen dossier | ◐ built — `chosen dossier` recipe + `X2AccessChosenReader` (2026-09-03, unexercised live; `chosen-bond-recon.md`). ⛔ Vanilla switches Chosen on the D-pad only; the tab nodes re-provide it |
| `UIChosenEncountered` | first-encounter overlay | ◐ built — popup recipe branch (2026-09-03, unexercised live). Enter-only in vanilla, so one button |
| `UINotificationMenu` | notification list (opened from `UIToDoWidget`) | ⬜ P2, with the to-do widget work |
| `UISkyrangerArrives` | the "Skyranger arrived, awaiting orders" prompt at the mission pin (pushed by `XComGameState_MissionSite.DestinationReached`) | ◐ built — `skyranger arrives` recipe + `X2AccessSkyrangerReader` (2026-09-14, unexercised live; `surfaces/strategy.md`). Every word a localized class default or `GetOpName()`; Enter launches, Escape recalls, and the recall button is emitted only while the game shows it |
| `UIStrategyMap_BlackMarket`/`_BuyIntel`/`_Outpost`/`_PoiChoice`/`_VisitCommander` | EU/EW leftovers, zero references | 🚫 dead |

## 10. Mission launch — `UIMission` family, ✅ built and run, 17 classes

`UIMission` (base) + `_GOps`, `_Council`, `_Retaliation`, `_SupplyRaid`, `_LandedUFO`,
`_AlienFacility`, `_GoldenPath`, `_ResOps`, `_RescueSoldier`, `_ChosenAmbush`,
`_ChosenStronghold`, `_ChosenAvengerAssault`, `_LostAndAbandoned`, `_GPIntelOptions`,
DLC_2 `_AlienNest`, DLC_3 `_LostTowers`. One layout family (`UIX2SimpleScreen` panels +
launch/cancel). ✅ built and run 2026-08-31 — `mission blade` recipe + `X2AccessMissionReader`
(probe is IsKindOf `UIMission`, so the DLC pair and mod blades match too); recon in
`mission-blade-recon.md`. The blades the campaign has not produced yet, and the two deliberate
v1 punts (`_GPIntelOptions` cannot purchase; the sitrep info button has no node), are in
`next-steps.md`.

## 11. Popups and alerts — ✅ popup recipe + transient reader

✅ recipe: `UIAlert` (118 variants), `UIAlert_DLC_Day60`, `UIAlert_DLC_Day90`,
`UIFactionPopup`, `UIAbilityPopup`, `UISoldierBondAlert`, `UIChosenFragmentRecovered`,
`UIChosenLevelUp`, `UIUFOAttack`, `UISitRepInformation`, `UICombatLose`,
`UIProgressDialogue`, `UIRedScreen`; ◐ `UIChosenEncountered`, `UISoldierCaptured` (2026-09-03,
unexercised live — `chosen-bond-recon.md` §1.2-1.3); ◐ `UIChosenReveal` — the branch shipped
2026-09-03 but the screen is pushed inside the reveal cinematic and the navigator's layer-wide
cinematic gate never let it attach (fixed 2026-09-04, unexercised live —
`popup-inventory.md` T6). ✅ transient reader: `UINarrativeCommLink`,
`UIEventNotices` (fed by `UIEventNoticesTactical`, which is an `Object`, not a screen),
`UIStrategyTutorialBox`.

| Remaining | What it is | Status |
|---|---|---|
| `UINarrativePopup` | skippable narrative popup (spawned by `XComPresentationLayerBase`) | ⬜ P2 — measure what triggers it |

## 12. Mission bookends

| Screen | What it is | Status |
|---|---|---|
| `UISquadSelect` | squad select | ✅ |
| `UIDropShipBriefingBase` + `_MissionStart` / `_MissionEnd` | Skyranger briefings | ✅ |
| `UIDropshipHUD` | interactive loading-screen HUD | ⚠ P3 — measure overlap with the briefing screens |
| `UIMissionIntro` | mission-type intro banner | ⬜ P2 |
| `UIMissionSummary` | post-mission stats | ◐ built — `mission summary` recipe, Flash capture, no companion (`mission-summary-recon.md`); first live pass owed |
| `UIChosenMissionSummary` | Chosen encounter result | ◐ built — `chosen summary` recipe, Flash capture, no companion (`mission-summary-recon.md`); first live pass owed |
| `UIAfterAction` | post-mission squad walk-up (wounds, promotions) | ✅ `after action` recipe + `X2AccessAfterActionReader` (`after-action-recon.md`). ⚠ The soldier-wording unification (`04a264c`) postdates the deployed companion — re-walk after the next `just go-full` |
| `UIResearchReport` | research complete | ✅ research report recipe (`research-report-recon.md`) |
| `UIResearchUnlocked` | new research available | ✅ `research unlocked` recipe, Flash capture, no companion (`research-unlocked-recon.md`); multi-card decks and shadow projects still unheard (`next-steps.md`) |
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
| `UITacticalCharInfoScreen` | the soldier sheet — ⛔ gamepad L3 only in vanilla (F1 is a commented-out no-op) | ◐ built — `soldier info` recipe, pure widget read, opened from the HUD graph's soldier stop through `X2AccessCharInfoReader.OpenCharInfo` (2026-09-03, unexercised live; `tactical-modals-recon.md` §2) |
| `UIInventoryTactical` | loot receipt — the loot is already taken when it opens, and the visualizer waits on OK | ◐ built — `loot` recipe + `X2AccessLootReader` (2026-09-03, unexercised live; `tactical-modals-recon.md` §1). Not a `UIInventory` |
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

The shell half is ✅ (2026-09-17): `UITLEHub` (`legacy hub`), `UITLE_LadderModeMenu` (`legacy
operations`), `UITLE_LadderDifficulty` (`ladder difficulty`), `UITLEUnitStats` (`soldier
loadout`), `UITLE_SkirmishModeMenu` (`skirmish`, all 21 states), `UITLE_ChallengeModeMenu`
(`challenge archive`). `UITLEScreen` is the abstract base. `UIChallengeMode_SquadSelect` and
`UIChallengeLeaderboards` are the ONLINE daily challenge's (unreachable from the offline
archive; the leaderboards are reachable from the post screen only) — out of scope with the
online mode.

The in-mission and post-mission half: `UILadderModeHUD`, `UILadderModeScoringDialog`,
`UILadderUpgradeScreen`, `UILadderSoldierInfo`, `UITLELadderMedalScreen`,
`UITLELadderEndScreen`, `UITLELadderMedalProgressScreen`, `UIChallengeModeHUD`,
`UIChallengeModeScoringDialog`, `UIChallengePostScreen`, `UIChallengeModeEventNotify` — see
`surfaces/README.md` for which have recipes.

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

`UIBlackMarket` (entry) ✅ `black market` recipe · `UIBlackMarket_Buy` ✅ commodity ·
`UIBlackMarket_Sell` ✅ `black market sell` recipe, a `GraphSheet` (own layout, NOT
commodity-family). Both new recipes 2026-09-03, built from source, **unexercised live** and the
companion not yet deployed to the game (`next-steps.md`); `surfaces/avenger.md` "Black market".

## Not screens, but real work items

Panels and subsystems that are TODO surfaces in their own right, already tracked in
`next-steps.md` — listed so this census reads complete:

- `UIPersonnel_DropDown` — staff drop-down, a `UIPanel`, not part of the personnel family. Read
  as the picker shape of both room recipes (`X2AccessStaffSlots`), on `UIRoom` and `UIFacility`.
- `UIToDoWidget` + `UINotificationMenu`, `UIEventQueue`, the `UIAvengerHUD` resource strip.
- Facility Flash-only status panels (via the companion).
- `UITacticalHUD_HackingTooltip` → the pre-commit hack odds read (hacking phase 4).
- Tooltips generally (`UITooltipMgr` + `UITooltip` family): mouse-hover-only facts need
  per-case parity decisions, not a blanket reader.
