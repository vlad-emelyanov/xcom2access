# Surface registry

One row per screen or subsystem the mod reads: which native TU, which companion class or recipe,
which rules doc, which recon. **Check this table before building anything that sounds like one of
these** — a second reader for a covered screen is the failure mode it exists to prevent.

The rules columns point at the files beside this one: `shell.md`, `tactical.md`, `strategy.md`,
`avenger.md`, each earned by a live bug, with a section heading matching the Surface column. Recon
(`../*-recon.md`) is how a surface was measured *before* it was built; the surface doc is what the
build settled on. The full census of every `UIScreen` class in the game — covered, TODO with
priority, or out of scope — is `../ui-inventory.md`, and what is still owed on these surfaces is
`../next-steps.md`.

| Surface | Native | Companion / recipe | Rules | Recon |
|---|---|---|---|---|
| Main menu, settings, new-campaign flow, pause menu | `game_ui_shell.cpp` | `recipes/recipe_shell.cpp` | — | — |
| Save / load | `game_ui_shell.cpp` | `X2AccessSaveReader.uc`, save/load recipe | shell.md | `save-load-recon.md` |
| Text-entry dialog | `text_input.cpp` | — (observer, no recipe) | shell.md | `save-load-recon.md` §7.1 |
| Tactical tile cursor | `tile_reader.cpp`, `mouse_gate.cpp` | `X2AccessTileReader.uc` | tactical.md | `concealment-recon.md` (detection readout) |
| Object scanner | `object_scanner.cpp` | `X2AccessObjectScanner.uc` | tactical.md | `object-scanner-recon.md` |
| Targeting / shot HUD | `tactical_hud.cpp` | `X2AccessTacticalReader.uc` | tactical.md | — |
| Free aim / AoE | `free_aim.cpp` | `X2AccessTacticalReader.uc` | tactical.md | `free-aim-recon.md` |
| Selected unit | `unit_info.cpp` | `X2AccessUnitReader.uc` | tactical.md | — |
| Objectives list | `objective_reader.cpp` | — (native reads the widget) | tactical.md | — |
| Tactical HUD graph (Ctrl+Tab layer) | `game_ui_tactical.cpp` (ability bar, mouse-controls strip) | `X2AccessUnitReader.BuildSoldierRecords` (soldier stop), `recipes/recipe_tactical.cpp` | tactical.md | — |
| Tutorial messages + first-time popups | `tutorial_reader.cpp` | `X2AccessTutorial.uc` | tactical.md | — |
| Hacking screen | `hacking_screen.cpp` | `X2AccessHackingReader.uc` | tactical.md | `hacking-screen-recon.md` |
| Resource strip (every strategy screen) | `game_ui_avenger.cpp` (Flash capture), `resource_strip.cpp` (the `R` key) | — (no companion), `Recipes::AddResourceStop` in six recipes | avenger.md | `avenger-view-recon.md` §2.2 |
| Avenger ant farm | `avenger.cpp` | `X2AccessAvengerReader.uc` | avenger.md | `avenger-view-recon.md` §11 |
| Avenger HUD layer (Tab on the ant farm: resources, objectives, shortcut buttons, to-do strip, event queue + date) | `game_ui_avenger.cpp` (widget reads; the date via Flash capture) | — (no companion), `Avenger HUD` ambient recipe | avenger.md | `avenger-view-recon.md` §2.3-2.6 |
| Inside a room (incl. staff slots + picker) | `game_ui_avenger.cpp` | `X2AccessFacilityReader.uc` + `X2AccessStaffSlots.uc`, `avenger room` | avenger.md | `avenger-view-recon.md` §13, §18 |
| Debris room | `game_ui_avenger.cpp` | `X2AccessRoomReader.uc` + `X2AccessStaffSlots.uc`, `avenger debris room` | avenger.md | `avenger-view-recon.md` §13 |
| Facility upgrades (`UIChooseUpgrade`) | `game_ui_avenger.cpp` | `X2AccessUpgradeReader.uc`, `facility upgrades` | avenger.md | `avenger-view-recon.md` §18 |
| Commodity list (13 screens) | `game_ui_avenger.cpp` | `X2AccessCommodityReader.uc`, `commodity list` | avenger.md | `avenger-view-recon.md` §14 |
| Personnel roster | `game_ui_avenger.cpp` | `X2AccessPersonnelReader.uc`, `personnel roster` | avenger.md | `avenger-view-recon.md` §15 |
| Black market — entry menu + trading post (the Buy side is the commodity list's) | `game_ui_avenger.cpp` (buttons, numbers, headings; the welcome line via Flash capture) | `X2AccessBlackMarketReader.uc`, `black market` + `black market sell` | avenger.md | `avenger-view-recon.md` §19 |
| Choose facility | `game_ui_avenger.cpp` | `X2AccessBuildFacilityReader.uc`, `choose facility` | avenger.md | `avenger-view-recon.md` §16 |
| Research report | `game_ui_avenger.cpp` (Flash capture) | — (no companion), `research report` | — | `research-report-recon.md` |
| Research available (post-mission deck) | `game_ui_avenger.cpp` (Flash capture) | — (no companion), `research unlocked` | — | `research-unlocked-recon.md` |
| Geoscape tray | `geo_scanner.cpp` | `X2AccessGeoScanner.uc`, `geoscape tray` | strategy.md | `geoscape-recon.md` |
| Region actions (contact, radio relay) | — | `X2AccessGeoScanner.uc`, `geoscape tray` stop 2 | strategy.md | `geoscape-recon.md` §16 |
| Avatar project meter (the stop, and the change watch) | `game_ui_strategy.cpp` (`ReadDoomMeter`), `doom_meter.cpp` (the watch; words via Flash capture) | `X2AccessGeoScanner.DescribeDoom` / `DescribeDoomTimer`, `geoscape tray` stop 3 | strategy.md | `geoscape-recon.md` §4.5 |
| Skyranger briefing | `dropship.cpp` | `X2AccessDropshipReader.uc` | strategy.md | `dropship-briefing-recon.md` |
| Modals / alerts | `flash_shadow.cpp` (UIAlert words) | `X2AccessPopupReader.uc`, `popup` | strategy.md | `popup-inventory.md` |
| Mission blade (`UIMission` + 16 subclasses) | `game_ui_popup.cpp` | `X2AccessMissionReader.uc`, `mission blade` | — | `mission-blade-recon.md` |
| Skyranger-arrived prompt (`UISkyrangerArrives`) | `game_ui_strategy.cpp` (finder only) | `X2AccessSkyrangerReader.uc`, `skyranger arrives` | strategy.md | — (built from `SrcOrig` alone; recon owed with the first live pass) |
| Commlink / toasts | `transient_reader.cpp` | `X2AccessTransientReader.uc` | strategy.md | `popup-inventory.md` |
| Movie subtitles (`.bk2`) | `movie_subtitles.cpp` | — (no script surface exists) | strategy.md | — |
| Squad select | — | `X2AccessSquadReader.uc`, `squad select` | strategy.md | `squad-select-recon.md` |
| Chosen encounter summary | `game_ui_tactical.cpp` | — (no companion), `chosen summary` | — | `mission-summary-recon.md` |
| Mission summary (post-mission stats) | `game_ui_tactical.cpp` | — (no companion), `mission summary` | — | `mission-summary-recon.md` |
| After action (post-mission walk-up) | `game_ui_strategy.cpp` | `X2AccessAfterActionReader.uc`, `after action` | — | `after-action-recon.md` |
| Armory loadout | — | `X2AccessLoadoutReader.uc`, `armory loadout` | strategy.md | `loadout-recon.md` |
| Promotion / level-up (3 screens) | `game_ui_strategy.cpp` | `X2AccessPromotionReader.uc`, `promotion` | — | — (no recon; built straight from source) |
| Covert actions | `game_ui_strategy.cpp` | `X2AccessCovertActionReader.uc`, `covert actions` | strategy.md | `covert-actions-recon.md` |
| Resistance orders | `game_ui_strategy.cpp` | `X2AccessPolicyReader.uc`, `resistance orders` | strategy.md | — |
| Chosen dossier (`UIChosenInfo`) | `game_ui_strategy.cpp` | `X2AccessChosenReader.uc`, `chosen dossier` | — | `chosen-bond-recon.md` |
| Chosen revealed / soldier captured (`UIChosenEncountered`, `UISoldierCaptured`) | `game_ui_popup.cpp` (class table) | `X2AccessPopupReader.uc`, `popup` | strategy.md | `chosen-bond-recon.md` §1.2-1.3 |
| Chosen reveal, mid-cutscene (`UIChosenReveal`) | `game_ui_popup.cpp` (class table) + the navigator's per-screen cinematic gate | `X2AccessPopupReader.uc`, `popup` | strategy.md | `popup-inventory.md` T6 |
| Soldier bonds (`UISoldierBondScreen`) + bond confirmation (`UISoldierBondConfirmScreen`) | `game_ui_strategy.cpp` | `X2AccessBondReader.uc`, `soldier bonds` + `bond confirm` | — | `chosen-bond-recon.md` |
| Monthly council report (`UIResistanceReport`) | `game_ui_strategy.cpp` (finder only) | `X2AccessReportReader.uc`, `monthly report` | — | `resistance-report-recon.md` |
| The report's Chosen page (`UIResistanceReport_ChosenEvents`) | `game_ui_strategy.cpp` | `X2AccessChosenReader.DescribeChosenEvents`, `chosen activity` | — | `resistance-report-recon.md` |
| Covert action result (`UICovertActionReport`) | `game_ui_strategy.cpp` | `X2AccessCovertActionReader.DescribeCovertActionReport`, `covert report` | strategy.md | `covert-actions-recon.md` §8b |
| Armory hub (`UIArmory_MainMenu`) | `game_ui_strategy.cpp` | `X2AccessArmoryReader.uc`, `armory` | strategy.md | `armory-recon.md` §1 |
| Weapon upgrades + the pattern picker (`UIArmory_WeaponUpgrade`, `UIArmory_WeaponTrait`) | `game_ui_strategy.cpp` | `X2AccessWeaponUpgradeReader.uc`, `weapon upgrades` | strategy.md | `armory-recon.md` §2 |
| PCS picker (`UIInventory_Implants`) — the commodity list, plus the implant delta and the cannot-equip word | `game_ui_avenger.cpp` | `X2AccessCommodityReader.ImplantDelta`, `commodity list` | avenger.md | `armory-recon.md` §3 |
| Recruits (`UIRecruitSoldiers`) | `game_ui_avenger.cpp` | `X2AccessRecruitReader.uc`, `recruits` | avenger.md | `recruit-recap-recon.md` §1 |
| Post-mission rewards recap (`UIRewardsRecap`) | `game_ui_avenger.cpp` (finder only) | `X2AccessRecapReader.uc`, `rewards recap` | avenger.md | `recruit-recap-recon.md` §2 |
| Loot receipt (`UIInventoryTactical`) | `game_ui_tactical.cpp` | `X2AccessLootReader.uc`, `loot` | tactical.md | `tactical-modals-recon.md` §1 |
| Soldier sheet (`UITacticalCharInfoScreen`) + the HUD stop's open verb | `game_ui_tactical.cpp` (widget reads) | `X2AccessCharInfoReader.OpenCharInfo` (the verb only), `soldier info` | tactical.md | `tactical-modals-recon.md` §2 |
| Manage equipment (`UIManageEquipmentMenu`) | `game_ui_strategy.cpp` | — (no companion), `manage equipment` | strategy.md | `customize-recon.md` §1 |
| Soldier customization (`UICustomize` + 16 subclasses, the option picker, the colour selector) | `game_ui_strategy.cpp` | `X2AccessCustomizeReader.uc`, `customize` | strategy.md | `customize-recon.md` §2 |
| Character pool (`UICharacterPool`) + its import / export picker (`UICharacterPool_ListPools`) | `game_ui_shell.cpp` | — (no companion), `character pool` + `pool files` | shell.md | `customize-recon.md` §3 |

