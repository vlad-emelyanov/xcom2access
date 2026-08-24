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
| Main menu, settings, new-campaign flow, pause menu | `game_ui.cpp` | `recipes/recipe_shell.cpp` | — | — |
| Save / load | `game_ui.cpp` | `X2AccessSaveReader.uc`, save/load recipe | shell.md | `save-load-recon.md` |
| Text-entry dialog | `text_input.cpp` | — (observer, no recipe) | shell.md | `save-load-recon.md` §7.1 |
| Tactical tile cursor | `tile_reader.cpp`, `mouse_gate.cpp` | `X2AccessTileReader.uc` | tactical.md | — |
| Object scanner | `object_scanner.cpp` | `X2AccessObjectScanner.uc` | tactical.md | `object-scanner-recon.md` |
| Targeting / shot HUD | `tactical_hud.cpp` | `X2AccessTacticalReader.uc` | tactical.md | — |
| Free aim / AoE | `free_aim.cpp` | `X2AccessTacticalReader.uc` | tactical.md | `free-aim-recon.md` |
| Selected unit | `unit_info.cpp` | `X2AccessUnitReader.uc` | tactical.md | — |
| Objectives list | `objective_reader.cpp` | — (native reads the widget) | tactical.md | — |
| Tutorial messages + first-time popups | `tutorial_reader.cpp` | `X2AccessTutorial.uc` | tactical.md | — |
| Hacking screen | `hacking_screen.cpp` | `X2AccessHackingReader.uc` | tactical.md | `hacking-screen-recon.md` |
| Avenger ant farm | `avenger.cpp` | `X2AccessAvengerReader.uc` | avenger.md | `avenger-view-recon.md` §11 |
| Inside a room | `game_ui.cpp` | `X2AccessFacilityReader.uc`, `avenger room` | avenger.md | `avenger-view-recon.md` §13 |
| Debris room | `game_ui.cpp` | `X2AccessRoomReader.uc`, `avenger debris room` | avenger.md | `avenger-view-recon.md` §13 |
| Commodity list (13 screens) | `game_ui.cpp` | `X2AccessCommodityReader.uc`, `commodity list` | avenger.md | `avenger-view-recon.md` §14 |
| Personnel roster | `game_ui.cpp` | `X2AccessPersonnelReader.uc`, `personnel roster` | avenger.md | `avenger-view-recon.md` §15 |
| Choose facility | `game_ui.cpp` | `X2AccessBuildFacilityReader.uc`, `choose facility` | avenger.md | `avenger-view-recon.md` §16 |
| Research report | `game_ui.cpp` (Flash capture) | — (no companion), `research report` | — | `research-report-recon.md` |
| Geoscape tray | `geo_scanner.cpp` | `X2AccessGeoScanner.uc`, `geoscape tray` | strategy.md | `geoscape-recon.md` |
| Region actions (contact, radio relay) | — | `X2AccessGeoScanner.uc`, `geoscape tray` stop 2 | strategy.md | `geoscape-recon.md` §16 |
| Skyranger briefing | `dropship.cpp` | `X2AccessDropshipReader.uc` | strategy.md | `dropship-briefing-recon.md` |
| Modals / alerts | `flash_shadow.cpp` (UIAlert words) | `X2AccessPopupReader.uc`, `popup` | strategy.md | `popup-inventory.md` |
| Commlink / toasts | `transient_reader.cpp` | `X2AccessTransientReader.uc` | strategy.md | `popup-inventory.md` |
| Movie subtitles (`.bk2`) | `movie_subtitles.cpp` | — (no script surface exists) | strategy.md | — |
| Squad select | — | `X2AccessSquadReader.uc`, `squad select` | strategy.md | `squad-select-recon.md` |
| Armory loadout | — | `X2AccessLoadoutReader.uc`, `armory loadout` | strategy.md | `loadout-recon.md` |
| Covert actions | `game_ui.cpp` | `X2AccessCovertActionReader.uc`, `covert actions` | strategy.md | `covert-actions-recon.md` |
| Resistance orders | `game_ui.cpp` | `X2AccessPolicyReader.uc`, `resistance orders` | strategy.md | — |

