# Next steps — the running work list

`CLAUDE.md` holds the rules and the durable engine facts; this file holds what has NOT been done
yet. Four kinds of entry, in the order a session should care about them: **live-run debt** (code
that builds clean and has never been exercised against a running game), **unexercised paths** inside
surfaces that have run, **known defects**, and **queued work**. Ordering inside each section is by
value, not by dependency.

Doc references are relative to this file's own directory (`docs/`), so `geoscape-recon.md` and
`surfaces/avenger.md` resolve as written.

⚠ When one of these is finished, the RULE it earned goes back into `CLAUDE.md` (or into the
surface's own `surfaces/*.md`) — this file loses the entry rather than gaining a "done" mark.

## Live-run debt

**Camera follows the tile cursor; north-most vanilla heading** (2026-09-30,
`X2AccessCamera_TileFollow.uc`, called from `X2AccessTileReader.SetCursorTile` and
`CursorRecenter`). Not yet run. On Gatecrasher (tutorial off), mouse and keyboard:
1. Step the cursor ten or more tiles in each direction: the camera must glide with it on every
   step, and the pathing preview must still match the spoken tile. `just dev-screenshot` or a
   sighted look confirms it; our readout does not.
2. Ctrl+Q twice, then `C`: the camera must turn back the short way to the default heading and
   centre on the soldier. Screen-up should be roughly north-west (north towards the upper right).
3. After a shot (aim camera), an enemy turn and a pod reveal, the heading must be unchanged and
   the next arrow step must re-centre on the cursor.
4. Save, load the save: the first step after the load must follow again (fresh camera stack,
   re-installed). A sighted player's WASD and edge scroll must still pan.
5. Regression: soldier switch still centres the camera on the new soldier; the mouse, when moved,
   still picks tiles without the camera chasing it.

**Loot pickup announcement** (2026-09-25, `X2AccessTacticalEvents.NarrateLootAvailable`).
The companion compiles and is installed; the announcement has not been heard in game. Kill an
enemy that drops timed loot and confirm "Loot available for pickup" follows the death line.
Check a kill with no drop, explosive loot destruction, and a drop collected immediately by a
nearby soldier: none should announce a pickup. In a tutorial replay, verify the same order
without speaking loot from a future frame. Hear the Russian line in a Russian game session.

**Campaign endings and memorial detail** (2026-09-25, `X2AccessEndGameReader` and two
recipes). The original native ASI and UnrealScript companion build and are installed; the
campaign result has not been heard live. On a win and a loss, verify the localized result, difficulty, date,
Avatar progress, all five stat pages, both player and world columns when world data exists,
the player-only layout when it does not, Back, Continue, and the transition to credits.
In the Bar memorial, enter a fallen soldier's detail card from the accessible roster;
verify identity, date, service statistics, cause of death and epitaph. Edit the epitaph,
return to the card, add the soldier to the character pool, and close the card. Confirm
the button labels and dialogue remain in the game's language.
The first memorial live pass reached the detail card and found the epitaph editor's multiline Confirm inaccessible by
keyboard: Enter inserts a line and Tab does not move focus. The native text-entry reader now
claims Ctrl+Enter only for multiline dialogs and calls the game's Confirm handler. The native
change builds; compile and install both layers after the running game is closed,
then test that Ctrl+Enter saves the epitaph while bare Enter still inserts a line; also check
single-line save naming and the crash report's two fields.

**Soldier voice preview and PCS activation** (2026-09-23, native): verify Backspace previews
without closing in both `UICustomize_Voice` (mouseless) and the mouse-active trait picker, with
the localized preview caption and Backspace announced. Enter must accept and Escape must retain
the game's revert behavior. On `UIInventory_Implants`, Enter on an eligible PCS must reach the
normal install/replace dialog; confirm it and verify the soldier's equipped PCS. Check a refused
PCS and the reuse-PCS path without bypassing their game rules. Regression: research/build-item
Enter and loot-screen continue retain the generic commodity route. Build success is not live
validation; deployment requires the running game to be closed.

**Room shortcut bar** (2026-09-23, `recipe_avenger.cpp`). Rooms now reuse the HUD's
"Room shortcuts" stop after their content and resources, gated on the actual bar visibility.
The empty-room fallback no longer recommends Escape. Verify the first tutorial visit to
Commander's Quarters: the shortcut row is reachable, Right reaches Research, and Enter starts
the normal introduction. Check a fixed room and a staffed facility: Tab reaches the bar,
Left/Right reads labels, hotkeys, disabled state and attention, Space reads tooltips, and Enter
changes rooms. Opening a staff picker must remove the bar until the picker closes; vanilla
debris rooms must keep it absent. The existing grid HUD bar must still work. Build and deployment
do not establish these live results.

**Tutorial-completion crash: camera bindings on Ctrl+Q / Ctrl+E** (2026-09-23,
`mods/X2AccessCompanion/Config/XComInput.ini`; dump evidence in `input-map.md`,
"Tutorial-completion crash"). The config retains Q/E soldier switching, adds real camera
bindings, excludes Ctrl from tactical soldier switching, and removes the previous mod's
unguarded soldier entries. No native or script code changed. **Steam tutorial completion passed
2026-09-23**: both movies finished, Avenger play and the next mission followed, and the matching
logs contain no missing-binding warning or CRT assertion (`input-map.md` records the audit).
The user reported the test working; release-order and Controls persistence checks below are not
individually confirmed by the logs. Live checks still owed:

1. Read tactical `Bindings` from both the CDO and the active input object: camera commands must
   have primary Ctrl+Q/E bindings; soldier commands must ignore Ctrl. Check a profile that saved
   the old bindings through Controls as well as a fresh one.
2. In a normal mission, Q/E switch soldiers and Ctrl+Q/E rotate without switching. Release Ctrl
   first and the letter first; neither camera chord may cycle a soldier on release. Confirm
   next/previous target while aiming and unchanged Q/E behavior in the Armory.
3. Controls must show Ctrl+Q/E for camera rotation, and the tutorial camera hint must resolve
   those chords. Save Controls and restart; recheck the input arrays and key behavior.
4. Finish Anton's GOG tutorial through Skyranger Return and Commander Awakens into strategy.
   Audit `Launch.log` for the missing-binding warning / CRT assertion, and the speech ring for
   unresolved markup, double spaces and collapsed value distributions. The local Steam transition
   passed as noted above; the other supplied Steam dump was a separate base-game graphics startup
   crash.

**Wall tones** (2026-09-17, `X2AccessWallTones` + `X2AccessTileReader.CueWalls`; the design is
in `surfaces/tactical.md`, the build tags in `build.md`). Builds clean; the package re-parses with
the four looping cues (cue → `SoundNodeLooping` → wave, east/west one-channel stereo, north/south
mono). Never heard. What a first run must settle, in order:

1. **The loop node loads at all** — `just dev-props "instance=XComTacticalSoundManager" SoundCue`
   lists six cues, and Launch.log carries no warning naming `X2Access_Wall_*` or
   `SoundNodeLooping`. A cue that loads but plays nothing means the node's serialization is wrong
   (the writer's docstring has what was assumed: `ChildNodes` as a plain ArrayProperty, no native
   payload on the node).
2. **Playing silent, then rising** — the first run (2026-09-17) heard ONE landing and then nothing
   on any map. The decompile found two causes, both fixed unheard: the idle level was 0, which the
   device culls and stops (`engine-notes.md`, audio), and the current adjust level was never set
   before the first glide, so that one landing was a burst from full scale. The next run must
   hear: nothing until the first landing beside a wall, the voice rising over about half a second
   from silence, gliding out a second later in When-moving mode, and rising AGAIN on the next
   landing; open ground stays silent. If it still dies, Launch.log's `wall tone voice restarted`
   lines say the device stopped a voice regardless (`Revive` restarts it) and how often.
3. **The compass**: a wall to the east sounds in the RIGHT ear, west in the left, whatever the
   camera's rotation; north and south are centred and distinguishable. Mirrored means the baked pan
   is right and the cursor's compass is not (`NORTH_DX`/`EAST_DX` are the only flip points).
4. **The predicate's distribution**, not its lines: walk a corridor, a room's corner, a car, a
   sandbag line. A car or a crate that swells a voice is the `B--V` test admitting high cover
   across the edge — `IsAdjacentTileVisible` is documented as "no vis-blocking high cover between
   two points", so a tall crate may fail it; if so the wall test needs the tile-flag form
   (`docs/rooms-recon.md` §3). The map's edge and the level volume's edge are deliberately
   boundaries; listen for whether that reads as a wall or as noise on open maps.
5. **Volume against the game's own mix**: RT's 25 % is a guess here. `Volume` in
   `Config/XComGame.ini` is the knob.
6. **Between missions**: a second mission in the same session spawns a fresh actor (the first died
   with its map) and the bed works again; Launch.log shows no `Accessed None` naming the class.

**Soldier switching on `E` / `Q` by config, bare `Tab` as the HUD layer key, the Shift swallow
retired** (2026-09-16, `mods/X2AccessCompanion/Config/XComInput.ini`; `MenuNavigator::OnLayerEnter`;
the rule is in `CLAUDE.md` and the census in `input-map.md` §Soldier switching). Builds clean.
The config MERGE is measured (both CDO arrays read back correct at the main menu, over the
player's own saved ini — `input-map.md` has the numbers); the keys themselves have not been
pressed. What a first run must settle, in order:

1. **Tactical**: E / Q switch soldiers in world mode; Tab enters the HUD layer (once — the
   landing speaks), Escape leaves; `Shift+B` / `Shift+.` / `Shift+,` / `Shift+J` / `Shift+V` walk
   backward WITHOUT cycling the soldier; E / Q are next / previous target while the shot HUD is
   raised; Ctrl+Tab does nothing; `Shift+Tab` inside the layer is the previous stop and nothing
   else.
2. **Strategy**: E / Q cycle soldiers on the Armory menu, Customize and the Bond screen with
   the graph focused, and the soldier node re-reads the new name — the word caches are keyed on
   the unit, but the DIFFER has never been asked to announce a cycle it did not drive. Tab on the
   ant farm still enters the Avenger layer. In Squad Select `E` moves to the next slot; check what
   `Q` does there (the screen's previous-slot key is the left bumper, so probably nothing — decide
   whether that earns a claim).
3. **The Controls screen** shows E / Q under NEXT / PREVIOUS UNIT and Ctrl+Q / Ctrl+E under the
   camera rotations (the 2026-09-23 crash fix above); the tutorial's "press Tab" hint now says E.

**The fog gate on the cursor's vertical words and the `world.unexplored` ring** (2026-09-16,
`X2AccessTileReader.VerticalDrawn` and `X2AccessObjectScanner.AddFrontier` / `CAT_UNEXPLORED`;
rules in `surfaces/tactical.md`). **Both HEARD WORKING 2026-09-16** — the ring returned seven
openings reading `Unexplored space, 7 tiles across, 6 tiles, 5 north, 6 west, out of range, 1 of 7`,
distance-sorted, every bearing correctly missing its vertical leg; explored tiles in the same run
still spoke `Down 4` and `13 north, 4 down`, so the gate is not over-firing; and the player
confirmed the cursor half by ear. Three things remain unheard:

1. ⚠ **A soldier with NO moves left, on a fogged tile another soldier could reach.** The same tile
   must go quiet, because the puck and the move border are drawn for the active unit or not at all.
   Correct, and the single most bug-looking consequence of the gate — confirm it deliberately
   rather than meeting it by surprise. A shape-count over a run will show the same tile both ways.
2. ⚠ **The MAP EDGE.** Never-seen tiles on the outermost ring of the grid are frontier cells like
   any other the moment explored ground touches them, so a squad spawning near the map border may
   hear an "opening" that is just the edge of the level volume. Not seen yet — the 2026-09-16 map
   put all seven openings inland. If it happens, the fix is a `TileDataIsBorderTile*` test
   (`rooms-recon.md` §2.1 has the constant), not a radius.
3. **COST, still unmeasured.** `AddFrontier` does `NumX × NumY` `GetTileFOWValue` calls per rebuild
   (~3,100 on a 56×56 map, ~6,400 on an 80×80). Nothing sounded slow in the live run, but nothing
   was timed either.
   ⚠ Also unjudged: on that map EVERY opening read `out of range`, which is honest but may make the
   clause a wasted word. Listen on a map where the fog edge is close before deciding it earns its
   place.

**The `world.exits` ring on `V`** (2026-09-16, `X2AccessObjectScanner.AddExits` / `AddThresholdEdge`
/ `CAT_EXITS`; the `V` and `Shift+V` claims in `object_scanner.cpp`; rules in `surfaces/tactical.md`
under the object scanner, recon in `rooms-recon.md` §4/§9). **Driven live 2026-09-16 through
`/call`** on a prison map (VIP-capture, 8 soldiers) — the MODEL is exercised and correct; the `V`
KEY itself is still unpressed. What the run settled and what it did not:

- ✅ **The edge query works and gives the game's own words.** Planted beside a cell door:
  `Дверь камеры, 3 tiles, 1 south, 3 west, out of range, 2 of 7`. That label came off the EDGE via
  `GetPathObjectFromTraversalToNeighbor` → `XComInteractiveLevelActor` → the state object — no
  proximity guess, which is the thing RT never had (`rooms-recon.md` §4).
- ✅ **Not full of doors.** Seven entries inside a building: two doors, five ladders. The worry that
  `EXIT_MAX` would fill with indistinguishable `Door` lines did not happen here. ⚠ The two doors
  DID share a name (`Дверь камеры` twice), navigable at two by bearing and distance; a cell block
  with eight would not be. Unsettled.
- ✅ **Cost is a non-issue.** Medians over 5 calls each, through the harness: enemies 52 ms,
  connectors 43 ms, **exits 45 ms**, frontier 49 ms. All four sit on the HTTP + game-thread-queue
  floor, so the ~1250 `GetPathObjectFromTraversalToNeighbor` calls cost nothing detectable and the
  radius is NOT cost-constrained. (Bounded above only — the round trip hides anything under ~20 ms.)
- ✅ **Nothing is being silently dropped.** `Dropped` was 0 on every sweep, indoors included: the
  query never returned a plain `XComDestructibleActor` in ~1250 edges. So the unnameable
  window/fence/car problem is theoretical so far — and it hints the query may return ONLY
  interactive actors, which would make §4's window row unreachable for a second reason.
- ❌ **THE REAL FAILURE, and it is the overlap, not the doors.** From the squad's own position the
  ring was **8 of 8 LADDERS** — `world.exits` was `world.connectors` under another name, because
  the nearest door on that map is 43 tiles away and the threshold sweep only reaches 12. On a
  door-poor map `V` currently buys nothing over `Ctrl+PageDown` to the connectors ring.
  - Cost says the radius can be raised freely, and that is the cheap fix — but a door 43 tiles away
    is not "the way out of this room" either, and the thing that would know the difference is the
    ROOM PARTITION (item 13, §8.6). **This run is the first real evidence FOR the partition rather
    than against it.**
  - ⛔ Do not redesign off one map. What is needed is the same ring on a door-dense plot (a city
    block, an ADVENT facility) before deciding between a wider radius, a kind-first sort, and
    waiting for §8.6.

Still unheard, and needing the KEY rather than `/call`:

1. `V` and `Shift+V` actually reaching the handler in world mode, and Interact still working at
   `Ctrl+V` (the leniency bargain — never verified for this key).
2. `/` on an exit must plant the cursor ON the near side of the door, not through it — the spoken
   tile is the nearer end of the edge, and getting that backwards would send Backspace walking into
   the far room.
3. **A door that `objects.interact` does NOT list** (already open, or with no live interaction).
   This is the ring's genuinely new content and the reason it is not just two old rings joined. On
   the prison map the one door in both rings was the same cell door, so this is unproven.

**Supply Extraction crates in the markers ring** (2026-09-16, `X2AccessObjectScanner.AddObjectiveMarkers`
re-fetching each owner by ObjectID / `InteractiveMarkWords`; the `MissionIcon_` counter table in
`objective_reader.cpp`; rules in `surfaces/tactical.md` under the object scanner and the objectives
list). **Heard working 2026-09-16 11:36**: picked-up crates leave the ring, marked crates carry
`marked`, and the counter reads `crates, 12 picked up, 0 available, 0 destroyed`. Still unheard,
each needing a mission state this save never reached:

1. `B` before anyone has marked anything: every crate reads `Objective: Advent Supply Crate` with
   NO detail word. A `marked` on an untouched crate means the material name test is matching the
   crate's DEFAULT material and must be narrowed; `marked by ADVENT` has never been heard at all.
2. A crate ADVENT's transport takes: watch which counter slot moves — if the lost slot's icon
   changes to `MissionIcon_Advent`, that token needs a table entry (`taken by ADVENT`).
3. A dead objective VIP (the unit branch had the same stale read): after the VIP dies, its entry
   leaves the ring.

**Destroyed objectives in the markers ring** (2026-09-17, `X2AccessObjectScanner.AddObjectiveMarkers`
skipping `Health == 0` on the interactive and destructible branches; rule in `surfaces/tactical.md`
under the object scanner). Found on the Chosen Avenger Defense: a shot-out turret stayed in the ring,
six of six. **Heard working 2026-09-17 18:36**: with all four turrets down the ring read
*"Objective: Мобильная электростанция"* then *"Objective: object"*, `1 of 2` / `2 of 2` — destroyed
turrets gone, the generator named through the companion's own `.rus` (its first non-English loc
file, so that file IS picked up), and the generic marker (`ARC_IA_GenericObjectiveMarker`, Health
-1) still listed, the proof that -1 is not treated as 0. Still unheard:

1. The turret word. Every turret in that save was already destroyed, so *"Objective: Турель
   "Адвента""* (`AdvTurretM1`'s character name, keyed on `ARC_IA_ChosenAD_BulwarkACV` in
   `InteractiveLabel`) has not been spoken yet; the shot HUD's target line shares the function, so
   targeting a live turret is the cheaper check.
2. Destroy the Mobile Power Generator while turrets stand. The map's Kismet flips the survivors to
   blast-only targeting (`OnlyAllowTargetWithEnemiesInTheBlastRadius`), and the defense objective
   presumably completes. Check with the sighted view (`just dev-shot`) whether a disabled turret
   still wears a flag; if it does not, the ring owes the same gate on that flag — and only then,
   because a mission that makes an objective blast-only from the start would lose it.

**Localization hearing pass** (`localization.md`). English and Russian keys are complete by the
static audit (`python tools/check_localization.py`), and both companion and native compile. This
is not a live language test. In a Russian session, hear a tactical tile and its high/low cover,
target cover, locked door and evac marker; compare cover wording with the game's own
`XLocalizedData.TargetHighCover`/`TargetLowCover`. Hear combat, replay/tutorial, hacking, Avenger,
Geoscape, objectives, menus, number plurals (1/2/5/21/22) and native punctuation/color names.
After the run, shape-count the speech ring and look for English fragments and doubled spaces.
Check the log for `Vocab: game label ... not yet available` and make sure a later `resolved` line
appears for any game-owned title whose live screen field was empty. The only emergency speech
for those paths should be generic "Screen", "Section", or "button", never a copied game label.
The CDO has previously been checked in Russian for the first tile/scanner batch, but the expanded
key set and native cache have not been read live. Confirm the missing-companion warning can be
resolved through `Object.Localize` even when its script package fails to load; startup failures
before localization exists are logged, not spoken (`docs/localization.md`).

**The pod reveal and the reinforcement drop** (2026-09-16, `X2AccessTacticalEvents.OnScamperBegin` /
`OnSpawnReinforcementsComplete`, builders `BuildRevealLine` / `BuildReinforcementLine`, the replay
branches `NarrateReveal` / `NarrateReinforcements`; rules in `surfaces/tactical.md` under "Tactical
combat narration") — built from `SrcOrig`, compiled clean, **never heard**. Gatecrasher's first pod
exercises the reveal; any mission with a reinforcement flare (a retaliation, or `just dev-exec` with
the cheat manager's reinforcement call) exercises the drop. What to listen for:

1. As the camera pans to the first pod: `Enemy sighted: 2 ADVENT Trooper, ADVENT Officer.` ONCE,
   during the pan, not before it — a line that arrives before the camera moves means the window is
   wrong, and a line naming a unit twice means the same-name collapse is comparing the wrong string.
   On a fresh campaign the first Sectoid reveal adds `New enemy type: Sectoid.` and Central's
   commlink line follows from the transient reader; hearing the type name three times is the
   expected shape there, not a bug.
2. A transport drop: `Reinforcements by troop transport: ...` as the ATT flies in, and then NO
   `Enemy sighted` line for the same units when they scamper a few seconds later. Hearing both
   means the spawn memo is not being claimed (check `just dev-props "cdo=X2AccessTacticalEvents"`
   for `NarratedSpawnUnits`); hearing neither means the spawn state's units carried a previous
   version and `GetPreviousGameStateForObject` filtered them all out.
3. A Lost swarm (any Lost mission): NOTHING at the spawn, `Enemy sighted: 8 Lost.` at the reveal
   that frames them — the swarm's spawn shows the player nothing, so its words belong to the reveal.
4. The tutorial's first reveal must produce the same line through the replay branch, since no
   event fires there; a silent tutorial reveal with a working Gatecrasher one means the frame's
   context is not the reveal context (check `RevealAIEvent`).
5. After the run, shape-count the ring: the `Enemy sighted` shapes must show a SPREAD of counts
   and names, and no `Enemy sighted:.` (an empty roster must return "" and never reach speech).

**Inside / outside on the tile cursor** (2026-09-16, `X2AccessTileReader.DescribeIndoor`; rule
in `surfaces/tactical.md` under the tile cursor) — heard once, on the Abandoned supply
extraction plot, and measured against the cursor pawn's own `IndoorInfo` through the harness.
What the run settled and what it left:

1. ✅ Spoken once per crossing: `Down 8. Outside. …` on the step off the block, `Inside
   building, floor 3 of 4` on the recenter, nothing extra on the steps inside. The reader's
   floor agreed with `IndoorInfo.iCachedCurrentFloorNumber` (3) and its building with
   `CurrentBuildingVolume`.
2. **The roof band — fixed, unheard.** The soldier stood in a walled third-storey room
   (screenshot) that the reader called `floor 3 of 4`. The block is an
   `XComMegaBuildingVolume` (`pcp_16_AbandonedP_MegaBlock`), whose `Floors` are three
   256-unit storey bands (extent 128) plus a FOURTH band of extent 1096 spanning 784..2976 —
   the roof and the sky above it, `FloorNumber 4` on four `eFloor_Default` volumes with no
   designer flag telling them from a storey. A plain `XComBuildingVolume` two-storey shop on
   the same plot has no such band. The reader now drops a last band taller than two storeys
   from the count and speaks it as `On roof` (rule in `surfaces/tactical.md`), and walks
   floor volumes first because the block's modular pieces list orphan floor volumes of their
   own. What to listen for: the same room says `floor 3 of 3`; the cursor climbing onto the
   block's roof says `On roof` once; stepping off the roof edge says `Outside`; coming back
   down the stairs says `Inside building, floor 3 of 3`.
3. Still unheard: whether `EncompassesPoint` at the tile CENTER lands inside a GROUND-floor
   volume. The ground band starts 16 units up (`fCenterZ 144`, extent 128) and the tile
   centre sits about 32 up, so it should; a silent doorway would say otherwise.
4. ✅ `just dev-props "cdo=X2AccessTileReader"` shows `LastIndoorState` (2 inside, 1 outside)
   for a read that costs no keypress; `just dev-props "instance=XCom3DCursor"` then
   `ptr=` on its `IndoorInfo` is the game's answer to compare against.

**The Skyranger-arrived prompt** (2026-09-14, `skyranger arrives` recipe in
`recipes/recipe_strategy.cpp`, `GameUI::FindSkyrangerArrivesScreen`,
`X2AccessSkyrangerReader.DescribeSkyrangerArrives`; rules in `surfaces/strategy.md` under
"Skyranger-arrived prompt") — built from `SrcOrig` after a live screenshot of the silent screen,
compiled clean on both halves, **never heard**. The mission blade's sequel: any squad launch
reaches it, so the first pass is one flight. What to listen for:

1. Attach names the screen from the left blade — Russian game: `"РЕЙНДЖЕР", Начало процедуры
   посадки` — then lands on the mission stop: the operation name (`Операция "Паучий огонь"`) and
   the body line; Tab reaches the two buttons.
2. Enter on the first button must start the load (the Skyranger briefing follows); Escape or the
   second button must recall the Skyranger with the `Avenger_Skyranger_Recalled` narrative. Both
   go through the screen's own `OnUnrealCommand`, so a silent Enter means the recipe did not own
   input, not that the call is wrong.
3. On the tutorial's first mission (and any `bCannotBackOutSquadSelect` source) the buttons stop
   must hold ONE button — the return button is gated on `Button2.bIsVisible`.
4. The screen deliberately outlives the launch click; the recipe must detach when the stack is
   torn down for the mission, not speak the prompt again over the briefing.

**The blast on a listed target** (2026-09-14, `X2AccessTacticalReader.DescribeTargetBlast` /
`TargetBlastTiles` / `UnitsInTiles`; rules in `surfaces/tactical.md` under "Targeting / shot
HUD") — built from `SrcOrig`, compiled clean, **never heard**. Three situations exercise it, and
Gatecrasher's street offers the first two on its first turn:

1. A standard shot with the reticle on a CAR (Tab past the troopers; the enemy strip puts
   destructibles last). The target line must gain `Blast: <units>` naming every unit standing in
   the drawn box, or `Blast: nothing`, and `WARNING: friendly fire, <name> in blast` when a
   squadmate is in it. `Delete` must add `Blast area N by N tiles`; the box is the
   `DamageTileRadius` square, so a car should read `3 by 3` or `5 by 5`, never `1 by 1`.
2. The same reticle on an ADVENT Trooper must NOT gain a blast clause at all — a unit target
   draws no area unless it wears a Homing Mine.
3. A Reaper's Remote Start (any campaign with a Reaper, or `GiveAbility RemoteStart` via
   `just dev-exec`), whose targets are only explosives: the same clause, from the target style's
   own tiles, with the multiplied radius — expect a wider footprint than the shot on the same car.

The suspects if a run disagrees: an empty `Blast:` beside a visibly marked unit means
`GetUnitsOnTile` wants a different Z than the box carries (the box spans a Z range and units are
keyed by their floor tile — check whether the marked unit's `TileLocation.Z` is inside
`GetBlastExtents`); a clause on every unit target means `GetAOETiles` answers for an effect that
draws nothing.

**The cover ring — `J` / `Shift+J`** (2026-09-07, `X2AccessObjectScanner.AddCover` /
`ClaimCoverCell` + `CAT_COVER`, `X2AccessTileReader.CoverEdgeWords` / `CoverSignature`, the two
`'J'` claims in `object_scanner.cpp`; rules in `surfaces/tactical.md` under the object scanner) —
both halves compile clean and **NOTHING HAS EVER BEEN HEARD**. Every fact in it is inferred from
the SDK headers and from RT's `CoverModel.cs`; not one has been read off a running map. What to
listen for, in the order a single Gatecrasher run settles them:

1. **Does `GetCoverPoints` answer at all, and with what?** The whole surface rests on one native
   call this project has never made. `Radius` XY / `Height` Z in world units is read off
   `XComTacticalCheatManager.TestCoverPoints` (`5 * 96`, `5 * 64`), not measured. If `J` says "No
   cover nearby on this floor" while the cursor's own `Delete` readout names cover on the very tile
   under it, the call or its units are the suspect — and those two surfaces disagreeing is itself
   the diagnosis, exactly as the tile cursor and the shot HUD disagreeing was.
2. ⛔ **Shape-count the ring, do not listen to it.** This is the readout the distribution rule was
   written for: the 260-"High cover"/zero-"Low cover" bug lived for two months in well-formed
   lines. `grep -oP 'TTS!?: "\K[^"]*' X2Access.log | sed 's/[0-9]\+/N/g' | sort | uniq -c | sort -rn`
   must show **both** high and low cover leading entries, and a spread of `N tiles wide`
   values rather than every entry at one width. All-one-value means the signature or the flood fill
   collapsed, whatever the lines sound like.
3. **Does the clustering match the map?** A sandbag line or a long wall must read as ONE entry with
   a plausible width; a car or a crate must give up to four, one per face, at four different
   bearings. Every entry at "1 tile wide" means the flood fill never joined anything (suspect
   `CoverSignature` — PEEK bits leaking in would do exactly this); one entry spanning half the map
   means it joined too much.
4. **Does the selection survive stepping the cursor?** Press `J` twice, arrow the cursor two tiles,
   press `J` again: it must continue from where it was, not restart at 1 of N. That is the whole
   job of keying on the cluster's lowest linear tile index instead of its nearest member, and it is
   unverifiable any other way.
5. **Does `/` plant on a tile you could actually stand on**, and does `Backspace` then accept it?
   The tile the ring speaks is the nearest cluster member; if the plant lands somewhere the pathing
   pawn refuses, `IsFloorTileAndValidDestination` is being asked with the wrong unit or the ±1 Z
   band is picking a cell off a ledge.
6. **`J` must not double-fire.** It displaces a live vanilla ability-hotkey forwarder bound in TWO
   input sections; the log should show the claim swallowing it, and no ability should trigger.
   `Ctrl+J` must still reach the vanilla action. Check `Shift+J` walks BACKWARD and does not cycle
   the soldier (the companion's `XComInput.ini` unbinds `LeftShift`, so a Shift release fires
   nothing; a cycle here means the config did not merge).

⚠ Two design calls this run should raise rather than settle: whether `COVER_MAX = 16` entries is a
ring or a list on a dense urban map, and whether the label wants the sentence join it inherits from
the tile cursor ("Half cover north. Full cover east") mid-way through a comma-joined line.

**The Avatar project meter — its Tab stop and its change watch** (2026-09-14, `BuildDoomStop` in
`recipes/recipe_strategy.cpp`, `doom_meter.cpp`, `GameUI::ReadDoomMeter`,
`X2AccessGeoScanner.DescribeDoom` / `DescribeDoomTimer`; rules in `surfaces/strategy.md` under
"Avatar project meter") — the stop and the removal edge were HEARD on 2026-09-14 (Russian game:
`ПРОЕКТ "АВАТАР", 6 out of 12` on Tab; `Cheat Removal, down 1, ПРОЕКТ "АВАТАР" 5 out of 12` after
`RemoveFortressDoom 1`; all six `m_strDoom*` strings populated; `watching HUD, doom 6 of 12` on
every entry; no double space in the ring). One fact from that run: the game plays a queued doom
change only from the alien HQ's update, i.e. while the map is up, not in flight, and time is
running — a cheat fired under a popup waits until the player is back on the map. Still owed:

The fortress's generic addition was also heard the same evening, spoken while the tray was
detached in flight: `Проект "Аватар", up 1, ПРОЕКТ "АВАТАР" 6 out of 12` — the title twice, since
that message IS the title; folded since (rule in `surfaces/strategy.md`). A facility's addition
was heard too, driven by `FastForwardFacilityDoomTimer` through `just dev-exec` (the lever for
that edge — it ends the interval, and the next alien HQ update on the map adds the doom):
`Станция пришельцев в регионе Западная Азия, up 1, ПРОЕКТ "АВАТАР" 7 out of 12`, followed by
Central's commlink line from the transient reader. Still owed:

The countdown was reached too, with `GiveResource Doom 5` (five permanent pips; the alien AI
enters Lose mode on the next update, pushes `eAlert_AlienVictoryImminent`, and the timer replaces
the pips) — the Tab stop read `ПРОЕКТ "АВАТАР" ЗАВЕРШИТСЯ ЧЕРЕЗ: 21 ДН. 23 Ч. 57 МИН. 33 СЕК.`,
but the watch said NOTHING for the jump or the flip: the stack went blank during the popup push
and the watch reset (rule in `surfaces/strategy.md`). The reversal (`GiveResource Doom -5`) WAS
heard on that binary, as the predicted double: `Cheat Doom, down 5, ПРОЕКТ "АВАТАР" 7 out of 12`
and then the meter again for the timer leaving. Both fixed, and the replay on the new build was
heard: one line on the popup's close — `Cheat Doom, up 6, ПРОЕКТ "АВАТАР" ЗАВЕРШИТСЯ ЧЕРЕЗ: 24
ДН. 0 Ч. 0 МИН. 0 СЕК.` — and one on the reversal — `Cheat Doom, down 6, ПРОЕКТ "АВАТАР", 6 out
of 12`; the watch also held through a mission blade without re-baselining. Still owed:

1. Leave the geoscape and come back with doom pending from a mission — the entry-path animation
   must produce an ordinary edge, not silence and not a double line.
2. Unheard: a title-as-message addition folded to `Проект "Аватар", up 1, 6 out of 12` (needs the
   fortress timer: `FastForwardFortressDoomTimer`).
3. The ring's facility entry carries the pin's doom level — HEARD: four facilities at 1 and the
   fortress at 2 summed to the meter's 6. That hearing found the difficulty label voicing its
   `<font>` tag and four facilities reading alike; both fixed (region in the label, `bNoColor`,
   and the scanner's native half cleaning every payload), compiled, unheard: expect `Устроить
   диверсию на объекте пришельцев, Западная Азия, Сложно, ПРОЕКТ "АВАТАР" 1, 2450 miles west,
   1 of 5`.

**Unit conditions on the squad ring, the soldier sheet and the Targets stop** (2026-09-04,
`X2AccessHelper.UnlistedPenaltyWords` / `StunWords`, `X2AccessObjectScanner.StatusWords` and its
`bIsCosmetic` gate, `X2AccessUnitReader.EffectNames`; rules in `surfaces/tactical.md` under the
object scanner) — compiled clean, NEVER HEARD. Both halves were found in one 2026-09-04 log and
neither fix has reached a running game: the deploy could not replace the locked `.u`. What to
listen for: (1) the squad ring counts **6 of 6** on a six-soldier squad, not 7, and no nameless
"1 of 1 health, 0 tiles" entry follows the Specialist — the GREMLIN is the phantom and the tell is
that shape; (2) a stunned soldier reads "Оглушение: 2" on `,`, on `;` **and** on the Targets stop,
with the same words in all three — this is the first thing the shared oracle exists to guarantee,
so a disagreement between them is the regression to report; (3) a disoriented soldier reads the
game's own word, not silence, which is what the whole turn after the Assassin's attack sounded
like before; (4) ⚠ the ring entry is now a longer line than it was — if it reads as a paragraph
rather than a breath, the penalty list is the part to trim, NOT the pair. A shape-count of the
speech ring should show "Оглушение"/"Дезориентация" appearing at all, which it never has.

**Sight, flank and the target preview on the tile cursor** (2026-09-04, `X2AccessTileReader.
DescribeSight` / `DescribeTargetPreview` / `CursorTargetPreview`, the `passThrough` claim kind in
`input_arbiter.cpp`, the Left Alt claim in `tile_reader.cpp`; the parity case is in
`surfaces/tactical.md`) — ran live once (2026-09-04, a facility mission with a turret, an Archon
and a Purifier up). What that run settled: the sight clause and the flank clause both fire and both
vary (74 "Seen by 1", 20 "Seen by 1, flanked", 15 "Seen by 2", 6 "Seen by 2, flanked"; the flank
word tracks the cover side against the enemy's bearing), Alt fires through the pass-through claim
(eight presses, "passed modifier vk 0xA4 through", every answer agreeing with the step readout of
the same tile, "No targets" where the step carried no preview clause), and with "Show Target
Preview" ON the preview rides every step (155 lines). Still owed: (1) the game's OWN strip must
rise under the same Alt press — the log cannot see Flash, only the player can; (2) Alt+F4 in world
mode must still quit; (3) with "Show Target Preview" OFF, only Alt answers. ⚠ Two design calls that
run raised rather than settled: the per-step form speaks the game's heading ("ВИДИМЫЕ ЦЕЛИ:", the
loc string is upper-case) on every arrow press, which may want dropping on the per-step form and
keeping on Alt; and both clauses answer about the CURSOR tile even when it is out of range, where
the game's own preview answers about the pawn's clamped destination.

**The HUD graph's Targets stop** (2026-09-04, `X2AccessTacticalReader.BuildEnemyRecords` /
`SelectEnemyHead`, the stop in `recipe_tactical.cpp`; rules in `surfaces/tactical.md` under the
HUD graph) — reached once live (2026-09-04), on a ONE-head strip: "Targets, Тяжелая турель,
button, ХИТ 79 percent, 6 of 6 health, armour 2, 17 tiles, 7 north, 13 west, 4 up" — the name,
the shot HUD's own hit caption, health, armour, distance and bearing all read; no position, because
the kernel omits "1 of 1" on purpose, and Left/Right were silent on a single item as they are at
the ends of Commands. Everything else is still owed, on a mission with a pod up: Left/Right must
walk several heads in the strip's order with the best shot FIRST; a head the selected ability
cannot reach must say "not targetable" and a squadsight-only one "via squadsight"; Enter must
raise the shot HUD on that target (the poll then speaks ability and target) and the layer must
yield focus silently — `SelectEnemyHead` has never been called. Two things only that pass can
settle: whether the Selected part lands the stop on the targeted head without stranding the heads
before it (the kernel's own warning), and whether ten uncached `GetHitChanceForObjectRef` sweeps a
second cost a visible frame on a twelve-head strip — if they do, the cache key is `m_arrTargets`
plus the selected ability, not the pointers.

**The Commands stop's Chosen gate** (2026-09-04, `ChosenActivatedRawCall` in
`game_ui_tactical.cpp`; the pin is in `sdk_layout_checks.cpp`) — fixed from the 2026-09-04 log,
not yet rebuilt or run. The stop read "Button 7, button, 7 of 7" (the Chosen Info slot with no
label and no key) ten minutes before the Assassin revealed: the block handed to
`GetActivatedChosen` omitted its optional `HistoryIndex`, so the engine read index 0 — the OLDEST
frame — and answered about it, while the game's own `UpdateControls` asks about the latest. The
live check needs a mission with a Chosen: before the reveal the Commands stop must count SIX
buttons (five fixed plus the Skyranger call; no "Button 7"), and after it the seventh must read
with the game's own label and key. ⚠ If a seventh unlabelled button survives the fix, the
suspect is no longer the frame but the copy-back: log the returned pointer beside
`ReadMouseControls`' count and set both against what the screen shows (`/screenshot`).

**Four `AppendPhrase` forks removed** (2026-09-04, `recipe_shell.cpp` ×3 + `text_input.cpp` ×2) —
found by the mandated post-run shape-count of the 2026-09-04 ring, not reported by anyone, which is
the point of that audit. Two shapes were live: the dialog said **"Выйти в главное меню?. Все
несохраненные…"** (title + body joined with a bare `". "`, three times) and the save-name box said
**"ВВЕДИТЕ НАЗВАНИЕ:, edit box"** (prompt + role joined with a bare `", "` by a hand-rolled lambda).
Both left sides are the GAME's own strings and both end in a terminator. Builds clean, kernel tests
pass, never run. The check is two keypresses: Escape → exit to main menu must read "…меню? Все
несохраненные…" with ONE terminator, and Save Game must read "ВВЕДИТЕ НАЗВАНИЕ: edit box". Then
re-run the audit — `grep -aoP 'TTS!?: "\K[^"]*' X2Access.log | grep -P '[.!?;:](\.\s|,\s)' | grep -v
'\.\.\.'` must come back empty, where it returned four lines before.

⛔ **TWO COMPANION COMMITS POSTDATE THE DEPLOYED `.u`.** The build in the game is the ASI of
2026-08-31 17:00 beside the companion of 16:57, and the whole live-run debt below it was
retired against THAT pair — but `04a264c` (16:58, `X2AccessAfterActionReader` /
`X2AccessHelper` / `X2AccessSquadReader`: the soldier reading identically on both bookends) and
`169019d` (`X2AccessTileReader`) both landed after it. So the after-action card wording, squad
select's card wording and the tile readout in the game are the versions BEFORE those changes,
and whatever was verified about them does not carry. `just go-full` first, then re-walk those
three; the unification commit is the one that matters, because its whole claim is that two
surfaces now say the same thing and only a live pass can show that they do.

**The object scanner's one-key-one-entry guard** (2026-09-04, `X2AccessObjectScanner.
InsertByDistance`) compiles clean and has not run: it was written mid-mission with the game up, so
only `just uscript` ran and the deploy robocopy was killed. Diagnosed live on
`Neutralize_Vehicle` — the carried VIP owned two `XComGameState_ObjectiveInfo` components, the `B`
ring said "Objective: David Watson" at 1 of 3 then 2 of 3 forever, and the evac zone at 3 of 3 was
unreachable forward (`Shift`+`B` reached it, which is what proved `AddEvac` right). The check is
one press: on a mission with an evac zone, `B` must reach it without wrapping backwards, and the
ring must count 2, not 3. Rule filed in `surfaces/tactical.md`.

**The evac zone in the tile readout** (2026-09-04, `X2AccessHelper.DescribeEvacZone` +
`X2AccessTileReader.DescribeTile`) — same deploy debt, same one-press check: sweep the cursor into
the zone and every tile inside the game's own `GetEvacMinMax` box must say "Evac zone". Two things
the first pass has to settle. **Does the Z band read right?** The box is three levels tall (the
2D footprint ±1 in Z), so the tile a level BELOW the painted square is inside it and must say so —
that is the game's rule, not ours, and it is the half a player would never guess. **Does
"no clearance" ever fire, and does it fire anywhere sensible?** `ValidateEvacTile` is a qualifier
rather than a gate on purpose (a silent tile inside a painted zone reads as "the zone ends here"),
but if that phrase turns up on ordinary standable ground the qualifier is wrong, not the tile.
Shape-count the ring afterwards: "Evac zone" and "Evac zone, no clearance" should partition the
footprint, and an all-or-nothing split is the distribution tell.

**The rank out of the name column** (2026-09-04, `X2AccessPersonnelReader.SoldierCells` and
`X2AccessBondReader.AppendRows`, plus the two native joins in `recipe_avenger.cpp` /
`recipe_strategy.cpp`) — reported live: the roster read the rank twice, once in the name and once
in the column beside it, because `eNameType_FullNick` prefixes `GET_RANK_ABBRV`. Now
`eNameType_Full` plus a separate `nick` field joined native-side. The check is one arrow press per
screen: on the personnel roster and on the bond screen's roster, column 0 must be the name and the
nickname with NO rank, and column 1 must still be the rank. The regression to watch for is the
other half of the fix — a soldier with no nickname must not read a trailing separator (the wire
sends `" "`, and only native's `CleanText`-before-`JoinParts` order makes it vanish), so find a
rookie and listen to the end of the line. Untouched on purpose, and not a bug either: the MEMORIAL
tab's name (the game hands that row `eNameType_FullNick` itself and draws no rank column) and the
bond screen's HEADER (the game says the rank twice up there).

**Screens raised inside a cutscene** (2026-09-04, reported live: the Chosen reveal was three
minutes of silence and the player pressed Enter on a screen nobody had read). Cause was the
navigator refusing the whole layer on `bCinematicMode`; the refusal is now per screen on
`UIScreen.bShowDuringCinematic` (`engine-notes.md` has the measurement, `CLAUDE.md`'s
topmost-is-not-reachable rule the statement). `X2AccessPopupReader.DescribeChosenReveal` also
gained the nickname, the joined rival-faction line and the screen's own single CONFIRM button.
The live pass, in one Gatecrasher-style mission with a Chosen: (a) the reveal must announce
itself, and Tab must step class+name+nickname, the STRENGTHS heading and its traits, the
WEAKNESSES heading and its traits, the rival faction, then one button — Enter closes; (b) an
UNREVEALED trait must read as the game's "unknown" wording, NOT as the real trait; (c) the log
must show `cinematic mode on` WITHOUT a layer-wide `UI gated`, and the tactical HUD must NOT
attach while the cutscene runs (it declares no `bShowDuringCinematic`, so the per-screen refusal
has to keep doing what the old gate did); (d) the same fix opens the pause menu, `UILoadGame`,
`UIOptionsPCScreen`, `UIDialogueBox` and the hacking pad during a cutscene — press Escape mid-
cinematic once and confirm the pause menu now reads. ⚠ Chosen-reveal weaknesses are drawn
DISABLED when a Dark Event set `bIgnoreWeaknesses`; the game gives that state no words, so we say
nothing about it either — a known parity gap, not a defect to fix by inventing an English label.

**The Chosen dossier and the bond screens** (2026-09-03, built, never run —
`chosen-bond-recon.md` §8 is the checklist; the `chosen dossier`, `soldier bonds` and `bond
confirm` recipes + `X2AccessChosenReader` / `X2AccessBondReader`, and two new popup-family
branches for `UIChosenEncountered` / `UISoldierCaptured`). Needs a campaign with at least one
Chosen met and a soldier or two with cohesion. The load-bearing items: (a) on the dossier, Enter
on the second Chosen's tab must make the `dossier` stop re-read for THAT Chosen — the wire cache
is keyed on the screen pointer and discriminated on `CurrentChosenIndex`, and if the words do not
change it is that discriminator that broke; (b) on the bond screen opened from the armory's BONDS
entry, Left/Right on the soldier node must cycle soldiers with the roster respawning under it,
Right onto `CREATE BOND` + Enter must reach the confirm screen, and Enter on a row that offers no
bond must switch the page to that soldier; (c) opened from after action (`bSquadOnly`) the
soldier node must be plain text and a plain row's Enter must do nothing but log the refused verb
— vanilla gates `OnSelectSoldier` there; (d) the two continue-only popups must offer ONE button
and no `<XGParam` markup may reach the ring. Then the audit: no double space, no asset path, and
the cohesion column must not collapse to one value across the roster. ⚠ The confirm screen's
Cancel EMPTIES the Training Center's bond slots when it was opened from one — that is vanilla,
and Escape there must be tried once from the slot path to hear what the room says afterwards.

**The monthly report and its Chosen page** (2026-09-03, built from source after a live screenshot
of the Chosen page the mod was silent on, never run — `resistance-report-recon.md` §6; the
`monthly report` and `chosen activity` recipes, `X2AccessReportReader` and
`X2AccessChosenReader.DescribeChosenEvents`). Needs a month end. The load-bearing items: (a) the
supply line must carry the card's own signed figure and the activity lists must read as sentences
with no `<font` markup left in the ring; (b) Enter on the report's Continue must land on the Chosen
page with one stop per column and the unmet column saying only `UNKNOWN CHOSEN`; (c) on the FIRST
knowledge gain of a campaign the warning stop must lead, its ACCEPT must dismiss the popup and the
Continue must appear only after — the wire cache is discriminated on `bShowChosenWarningPopup`,
and if the Continue never appears that discriminator is the suspect; (d) when a Chosen levelled up
the level-up popups come first and the columns must still read once they are dismissed. The page
after this one, `UIAdventOperations`, is the popup recipe's and reads its headings only — its
dark-event rows are the next hole in the chain, with `UIRewardsRecap`.

**The mission bookends** (2026-09-03, built, never run — `mission-summary-recon.md`; the
`mission summary` and `chosen summary` recipes, both Flash-capture-only, no companion). This is
the last hole in the post-mission chain, so the pass is one mission end walked all the way
through. It needs a **tutorial-off** campaign: `X2TacticalGameRuleset` skips both screens on the
tutorial, the final mission and Lost and Abandoned, and a LOSS that offers a restart goes to
`UICombatLose` (already the popup recipe's) instead. The full checklist is recon §7; its three
load-bearing items are (a) **does the navigator attach at all** — nothing yet says what
`IsInputBlocked` / `ScreensVisible` / `bCinematicMode` read during the end-of-mission fade, and
if the stack is gated here the way the hacking screen's is, the fix is the hacking screen's and
everything below it is moot; (b) `just dev-flashqueue` must show `SetMissionInfo` with **21
operands** on the screen's cache index — an empty capture is the LRU question, not an offset
question; (c) Enter on Continue must run the auto-photo and hand over to `UIAfterAction` with no
second press and no stall on `bWaitingForMissionSummary`. Then: the objective label switching to
"CIVILIANS RESCUED:" on a Terror or Chosen-retaliation mission, the photobooth node appearing on
a normal win and being ABSENT after a total squad wipe, and nothing anywhere speaking an asset
path (`grep -n 'img:///\|UILibrary\|\.tga'` over the speech ring — the Chosen screen pushes six
of them). ⚠ The Chosen screen's three outcome branches are three separate missions — a plain
encounter (its `SetChosenReward` is pushed with three EMPTY strings and must stay silent), a
knowledge extraction, and a soldier kidnapped — and the reward branch needs a Chosen actually
killed, which is the rarest of the four.

**Staffing a BUILT facility, and the upgrade screen** (2026-09-03, built and deployed, never
run — `surfaces/avenger.md` "Inside a room" and "Facility upgrades", recon §18). Reported as
"I can't assign engineers to the Power Relay or soldiers to the Training Center": the room
recipe built no nodes for `UIFacility`'s staff slots and had no picker shape. Now the slots sit
in the crew stop and the picker is the debris room's, through one shared companion class
(`X2AccessStaffSlots`) — so the debris-room path is ALSO re-deployed and must be re-walked. The
pass, on a campaign with a built facility: (a) in the Power Relay, Down from "ENGINEERS: N"
must land on the first slot, Enter must open the picker and the screen name must gain "choose
who to assign", Enter on a name must assign and the slot must re-read with the occupant WITHOUT
leaving the room (the words re-ask while the picker is open); (b) in the GTS, Enter on the
empty slot → picker of rookies → Enter → the game's train dialog (dialog recipe) → Yes →
`UIChooseClass` (commodity recipe), and Enter on the FILLED slot must raise the stop-training
confirmation, never a picker; (c) a padlocked slot must say "locked" plus the game's unlock
tooltip and Enter must open `UIChooseUpgrade`; (d) on that screen the rows read with
"unavailable" where greyed, the card's COMPLETED line appears only on an installed upgrade, the
power line carries its sign, and Enter installs and pops back to the room; (e) a debris room
still clears by keyboard (the shared builder replaced its own). Then shape-count the ring, and
`grep -n 'MISSING DISPLAY INFO'` over it — that literal is `XComGameState_StaffSlot`'s own
fallback for a template with no display function, and would mean a slot kind the words never
covered.

**The black market** (2026-09-03, built from source, never run — `surfaces/avenger.md` "Black
market", recon §19; the `black market` and `black market sell` recipes plus
`X2AccessBlackMarketReader`). The companion compiled clean SDK-side at 18:20 that day but the
game was running, so the `.u` under `Mods/` is the OLD one. ⛔ `just go-full` with the game
stopped before anything else: the trading post's words and the entry menu's interest sentence are
companion-side, so without the fresh `.u` every row reads as "Item" and the interest node is the
bare caption. The pass, from a geoscape with the market scanned: (a) the entry menu must attach
as "The Black Market" and read the welcome line, "INTERESTS THIS MONTH:" with its sentence, then
BUY and SELL INVENTORY as buttons — a greyed one must carry its reason (SOLD OUT THIS MONTH /
EMPTY INVENTORY) — and Enter on BUY must land in the commodity recipe on "This Month's Goods";
(b) on the trading post the FIRST thing to settle is the heading-to-column pairing, `just
dev-shot` beside `just dev-speech`: the sheet assumes QTY over the owned count, COST over the
unit value, SELL over the number selling and TOTAL over the line total, read off
`UpdateSellInfo`'s push order and the loc strings and never off the Flash layout, and a swap
there miscaptions every cell; (c) `+` on a row must click and say "SELL 1, TOTAL §N", `-` back
to zero must say "SELL 0" with no total, Right from the name must read "QTY, n" and the further
cells under their headings, and the CONFIRM node must lose "unavailable" the moment something
is marked; (d) Enter must sell, respawn the rows and leave focus on a row rather than detaching,
and the resource stop must show the new supplies; (e) `Escape` must close both screens
(unclaimed). Then shape-count the ring and `grep -P '  '` it: an empty heading leaves a double
space, which is the localized-var trap.

**The P1 sweep** (2026-09-03, built from source in one pass, never run — seven screens, each with
its own checklist in its recon doc; the companion compiled clean but the `.u` under `Mods/` is
whatever the last `just go-full` left, so **`just go-full` with the game stopped comes first**,
and every screen below that carries a `X2Access*Reader` reads as bare fallbacks until then).
- **The armory hub** (`armory`, `armory-recon.md` §1.5). The one most likely to be wrong: it is
  written against the Highlander's rewrite, so the FIRST check is `just dev-gui` showing the eight
  `ArmoryMainMenu_*Button` MCNames — a list of unnamed rows means a vanilla `XComGame.upk` and the
  whole row join is void. Then the soldier spinner (Left/Right must cycle with the header
  re-reading), a greyed PCS row's reason, and the abilities/traits toggle on a soldier with a
  trait. ⚠ The loadout's header composers moved into `X2AccessHelper` in the same change, so the
  loadout's `who` and `stats` lines are re-deployed and must be re-heard.
- **Recruits** (`recruits`, `recruit-recap-recon.md` §1.6): a row must read `name, country`, the
  details stop the bio, and Enter with supplies short must raise the game's dialog and land back
  on the list afterwards.
- **The rewards recap** — ✅ heard 2026-09-04: attached as the operation, landed on ЗАДАНИЕ
  ЗАВЕРШЕНО, read the region, the paragraph and two rewards, and Continue handed over cleanly.
  Still unheard: a mission with global effects (the GLOBAL EFFECTS: context) and a failed one.
- **The covert action result** (`covert report`, `covert-actions-recon.md` §8b): needs an action
  to complete. RESULTS must carry one node per soldier with the XP and cohesion lines, and a risk
  that fired must be the LAST value on that soldier's node.
- **The loot receipt** (`loot`, `tactical-modals-recon.md` §1.5): walk onto a corpse; the HUD
  layer must detach, the receipt attach under the corpse's name, and Enter on OK must release the
  visualizer. ⚠ The screen appears unprompted at the end of a move, so a silent stall on a
  mission is the symptom of this recipe not attaching.
- **The soldier sheet** — ◐ heard 2026-09-04: opened from the HUD stop, read the seven stats
  with `none` for a zero, Escape brought the HUD back. Fixed from that run: the class label
  (written with `SetHTMLText`, so the html field is the read). Still unheard: the EFFECTS stop —
  Tab past the stats on a soldier with a passive and confirm the PASSIVES / BONUSES / PENALTIES
  contexts and one node per effect with its description.
- **Weapon upgrades** (`weapon upgrades`, `armory-recon.md` §2.6): the pin on
  `WeaponViewOffsets` is the Highlander detector, so a build that fails there is a vanilla dump.
  Enter on a slot must open the picker with the slots re-read as `current slots`; arrowing the
  picker must re-read the stat block and the description box per upgrade with no `Accessed None`
  from the companion's change-state preview; Escape must back out without leaving. The pattern
  picker must land on the current pattern and Escape there must revert. ⚠ The colour row's
  `UIColorSelector` has no reader.
- **The PCS picker's two additions** (`armory-recon.md` §3): on `UIInventory_Implants` a red row
  must say `cannot equip` (not `cannot afford`), and the card must end in a stat line
  (`AIM +5`) paired with the RIGHT stat — the game's own header shows it under the wrong label.

**The P2 pass: customization, the character pool, manage equipment** (2026-09-04, built from
source in one pass, never run — `customize-recon.md` holds the three checklists, §1.5, §2.6 and
§3.4; the `customize`, `manage equipment`, `character pool` and `pool files` recipes plus
`X2AccessCustomizeReader`). ⛔ **Neither layer is in the game.** The ASI built clean (kernel
tests green) and the companion compiled clean SDK-side, but the game was started mid-build and
holds both files: `just install` was refused on the `.asi`, and the `.u` under `Mods/` is still
the 02:24 one (the deploy robocopy stalled and was killed, per the memory note). So **`just
go-full` with the game stopped comes first**, and until then every screen below reads as the OLD
build — the customize screens silent, the pool silent. The load-bearing items, in the order they
are cheapest to reach:
- **The pool, from the main menu, needs no campaign**: attach as the header, the rows as
  checkboxes with EDIT CHARACTER advertised, Backspace on a row into the editor (`bInArmory`
  false — no soldier spinner), Escape back with the row re-read. IMPORT into a file and Escape
  OUT of it to the file list rather than closing — that is `OnCancel`'s own two-step and the
  recipe re-attaching on the same screen with new rows.
- **The colour grid is where this pass is most likely wrong**: nothing in the layer has driven
  a `UINavigator` through `SetSelectedNavigation` on a grid child before. Enter on HAIR COLOR
  must land on the chip the game highlighted, arrows must move by row and column WITH the pawn
  previewing (`just dev-shot` beside `just dev-speech`), Enter must commit and re-read the row's
  hue, Escape must revert. If the write-back does not preview, the fallback is the chip's own
  `OnReceiveFocus` by name.
- **The picker's name** is the opener's selected row, read through the stack past the mouse
  guard; a picker attaching as *Options* means that walk found no family screen beneath it.
- **The menu screen's ALLOW checkboxes** toggle through `SimulateMouseClickOnCheckbox` — the
  one activation in the family that bypasses the input path — and TIME ADDED must read
  `unavailable` with its timestamp.
- Then the distribution audit for the hues (every soldier the same hue is the collapse), `grep
  -n '0x'` over the ring for a swatch that escaped `ColourName`, and `grep -P '  '`.

**Tactical Legacy Pack, the shell half** (2026-09-17; rules in `surfaces/shell.md`, recon in
`tle-skirmish-recon.md` and `tle-challenge-recon.md`). The hub ran live the same evening (the
cards, the figures, Space, the arrows all read; Enter on a card and the `selected` part are still
unheard). The five screens under it were built from source with the game closed and have never
run. In the order a player meets them:

1. **Legacy Operations** (`legacy operations`): the attach names the screen; Up/Down walk four
   narrative rows under CENTRAL'S ARCHIVES then the procedural rows under RESISTANCE ARCHIVES,
   each row with its name, status and medal word; the `details` stop follows the selection (a
   selection change must re-speak under the NEW ladder's name); the `squad` stop lists the
   soldiers and Enter on one opens the loadout popup; CONTINUE / BEGIN, ABANDON, DELETE appear
   exactly where the game draws them. On the "[+] generate" row the details stop must be
   ABSENT, not stale. Enter on a fresh ladder opens the difficulty page; on an in-progress one
   it loads the save.
2. **Ladder difficulty** (`ladder difficulty`): the checked difficulty is the landing; the
   narrative-VO row exists on a narrative ladder only; Ironman reads `unavailable` with its
   reason; LAUNCH starts the ladder. The title must be UIShellDifficulty's own word, not the
   fallback.
3. **Soldier loadout** (`soldier loadout`): Left/Right on the soldier line step the soldier and
   re-speak it; the stats, gear (an empty pocket says `empty`) and abilities stops read; Space on
   an ability speaks its description.
4. **Skirmish** (`skirmish`): `tle-skirmish-recon.md` §6 — the state byte at 0x48C matches the
   subtitle; START GAME keeps its visible bit in every state; a picker Enter respawns the rows
   and lands with the new subtitle spoken; the stat sheet reads a heading with each value; the
   AWC rows do not all read the same text.
5. **Challenge Archives** (`challenge archive`): `Data` at 0x3D8 reads on all 107 rows and the
   attach lands on "1 of 107"; the sort stop finds five headers, Enter re-sorts and a second press
   flips, and the cursor stays on the same challenge across a sort; Enter on a row starts it.

For all five: `grep -P '  '` over the ring (dozens of localized labels are read off instances),
and the distribution audit on any enumerable field (medal words, difficulty words, gear slots).

**Tactical Legacy Pack, the mission half** (2026-09-17; rules in `surfaces/tactical.md`, recon
in `tle-mission-recon.md`). Eight recipes and one narrate-only surface, built from source with
the game closed, never run. In the order a ladder mission meets them:

1. **The tactical layer is alive at all** on a ladder or challenge mission now that the two score
   HUDs are whitelisted: the tile cursor speaks, the HUD graph attaches, and its trailing `score`
   stop reads the score (and the countdown in a challenge). Silence here means the whitelist
   missed a class name.
2. **The scoring dialog** (`mission scoring`) attaches before turn 1 with its point rows and its
   Continue starts the mission; `grep -P '  '` there checks the `m_ScoreLabels[]` population,
   which the loc file keys by enum name.
3. **The first kill** narrates the banner label with its points, then the total under the SCORE
   label, in one burst after the banner — and not in a replay.
4. **After the mission**: the upgrade pick (two cards; Left/Right move the game's highlight with
   ours; Enter chooses), the squad review (page turns re-ask; a KIA soldier lands on its header
   word; Enter on a soldier opens the loadout popup), medal progress or the medal, the end
   screen — each attaching in turn under the MISSION COMPLETE name.
5. **A challenge loss**: whether the movie shows the loss header the reader chose on
   `bMissionFailed`; and the leaderboard button hidden offline.
6. **Left out, on purpose**: `UIChallengeLeaderboards` (networked only, behind the result
   screen's LEADERBOARD) and `UIChallengeMode_SquadSelect` (the online daily challenge); the
   challenge timer's colour thresholds (an aid candidate, drawn as colour); the medal screen's
   never-pushed `m_MedalDescriptions`.

**Mod Config Menu** (2026-09-17; `mod config` recipe in `recipes/recipe_mods.cpp` over
`game_ui_mods.cpp`, plus the injected-button scan appended to `GameUI::ReadOptions`; recon in
`mod-config-menu-recon.md`). Built from the mod's own source with the game closed. **First run
21:16 the same day reached the button and stopped there**: the settings recipe stayed attached under
the mod screen because Settings never clears its focused bit (`surfaces/shell.md`, Settings —
fixed with a topmost-of-stack probe, unrun since). It is
the first screen of the mod-provided family, so the first pass also tests the family's premise:
that a class absent from the generated SDK can be read by walking its tree and by name, with no
pinned offset anywhere. The checklist is `mod-config-menu-recon.md` §5 — the fifth Options button,
the no-page landing, the page-switch rebuild, the slider's own value text, Reset's presence, and
Escape discarding. Test set: AllSoldiersGainXP, EvacAll_WotC, WOTCIridarAutoReload.

## Unexercised paths inside surfaces that HAVE run

⚠ Everything in this section belongs to a surface whose MAIN PATH has been walked live. What is
listed is the branch that has not: a state the campaign has not reached, a mod that has to be
installed, a setting that has to be off. Treat a line here as "nobody has heard this yet", not
as "this is broken".

The branches carried over when the 2026-08-31 build's live-run debt was retired (main paths all
confirmed in play):

- **Movie subtitles with Show Subtitles OFF** — the surface's main path ran on 2026-09-04 (see
  `surfaces/strategy.md`) but the option gate did not. `PumpMovie` blanks the string unless
  `UEngine.bSubtitlesEnabled` OR the movie's flag `0x80`, and we mirror both; the branch that has
  never been heard is the OFF one, which must be SILENT — our speech IS the subtitle, and a mod
  that keeps reading after the player turned subtitles off is the failure. The 0x80 half wants a
  legal/attract screen, which stays readable with the option off; that is the branch that proves
  we mirror the flag rather than just the setting.

- **The commodity card's shortfall line** (built 2026-09-16, `surfaces/avenger.md`, the
  "row's red says THAT" bullet) — the `short` field and its "not enough" line have compiled and
  never been heard. Walk it on Build Items with a row the game draws red: the card must read the
  full cost, THEN a line naming only the red item(s) trailed by *not enough*, and a row drawn
  green must produce no such line at all. The cheap distribution check is `grep -c "not enough"`
  over `just dev-speech` against the number of red rows arrowed through — equal, or the parse is
  matching the wrong colour constant.

- ⛔ **The resource strip has NEVER RUN** (built 2026-09-03, `surfaces/avenger.md`). It is
  first-pass debt rather than a branch, and it is cheap to walk because the strip is on screen
  for most of the strategy layer. Four things to confirm, in order:
  1. `just dev-log` carrying `ResourceStrip: N resource(s) from cache index …, showing`. **N
     must be 9 on the ant farm** — anything less means the repaint delimiter or the raised
     `kMaxCallsPerPanel` is not doing its job, and 8 specifically would mean the cap change did
     not take.
  2. `R` on the ant farm reads all nine; Tab in a commodity list, in a room and on the geoscape
     reaches a **Resources** stop with the subset `avenger-view-recon.md` §2.2 predicts for that
     screen. ⚠ The per-screen subsets are the thing most likely to be wrong, and the cheapest
     check is the Power Generator (power ONLY) against `UIChooseResearch` (five, no supplies).
  3. **The colour states.** `grep -c critical` / `low` over `just dev-speech` — a run in which
     NOTHING is ever `critical` is the distribution failure CLAUDE.md's audit rule describes,
     and the easy provocation is `UIFacility_ResistanceComms` with contacts at capacity.
  4. The strip must be **SILENT before `T0_M4_ReturnToAvenger`** and while the Avenger is in
     flight — no stop, and `R` unclaimed. This is the parity half and the only one that fails
     in the direction a player would not report.
- ⛔ **The Avenger HUD layer has NEVER RUN** (built 2026-09-03, `surfaces/avenger.md` §Avenger
  HUD layer). First-pass debt on the ant farm, and the checklist is short because four of its
  five stops reuse readers that have: (1) `Tab` on the grid must say "Avenger HUD" then land on
  the resources stop, and `Escape` must say "Rooms" and give the arrows back — the room name
  must still speak on the next arrow, which is the `OwnsInput()` gate in `avenger.cpp` doing its
  job; (2) `Tab` must cycle Resources → Objectives → To do → Events → Room shortcuts, and
  `just dev-log` must show the recipe attached "(ambient, unfocused)" BEFORE the first Tab —
  a refusal ("Avenger HUD unavailable.") on the first press means the probe is not passing;
  (3) Enter on a shortcut category must open that room and land in the room recipe; Enter on a
  to-do message must open its hotlink (the research picker is the easy one); (4) the event
  queue's expand button must appear only with two or more events, and the date node must read
  a real date — an empty date with the queue showing means `RefreshDateTime` was not captured
  on that cache index (check `just dev-flashqueue`). ⚠ The to-do strip is the one panel with a
  real staleness question (`UpdateCategories` runs on the grid only) — read it, open a room,
  come back, and confirm it refreshed.
- **Pause menu, mod rows.** `ActivatePauseMenuItem` sends Enter to the LIST (`SendRowCommand`)
  rather than calling `OnChildClicked` by name, so a mod-wrapped `OnItemClicked` delegate runs.
  The vanilla rows are confirmed; the MOD row that motivated it is not — with Remove Missing
  Mods Redux enabled, Enter on its row (6 of 8, tactical pause) must raise its confirm dialog.
- **Popup recipe, image-carrying alerts.** `UIAlert`'s words come from the Flash capture, and
  the asset filter reports what it removed as `dropped asset-looking operand`. Unheard: a splash
  that pushes an image path MID-LIST (`BuildGOpsAlert` / retaliation / supply raid), and an
  alert whose confirm label has to be matched out of the prose by the button filter. Grep the
  log for that line and check `just dev-flashqueue` against `just dev-speech`.
- **Popup recipe, the multi-batch fix (2026-09-03; heard 2026-09-04 on eleven alerts).**
  `ReadAlertCaptured` reads EVERY batch on the alert's panel rather than the newest —
  `popup-inventory.md` §10 has the measurement. The first pass found the leak that reading
  everything invites: `spawnChildPanel`'s two name operands led every popup (`surfaces/
  strategy.md`, fixed — `IsStructuralBatch`), and the headshot mechanism read right
  (`eAlert_NewStaffAvailableSmall`'s trailing `UpdateImage` dropped, the words kept). Still owed:
  (a) `eAlert_StaffInfo` itself — ВНИМАНИЕ!, the title, the recruit's name, the bonus label and
  BOTH facility rows, in that order; (b) ~~the four- and five-batch alerts checked for
  over-read~~ — heard 2026-09-04 afternoon, and both over-read exactly as predicted: the
  covert-action confirmation spoke its slot indices ("0", "1", "2") and Retribution its level
  badge ("2"), fixed by `IsPositionalOperand` (`surfaces/strategy.md`); still unheard AFTER the
  fix. Same session, same rule: `addTweenBetween` leaked "_alpha, time, delay" onto
  `eAlert_FacilityComplete` as a STALE batch on a recycled cache index — `IsStructuralBatch` now
  names the tween ops, and the reader logs `skipped structural batch(es) … on cache index N`
  whenever it meets one, which is the line that would show a second recycled index; (c) the
  `UpdateFacilityList` deferral reading the facility rows AFTER the name. The `read N line(s)
  from the Flash capture (A+B+C, …)` log line names the contributing batches, and the reader now
  WARNS on any contributor that is not an `Update*`/`Set*` push — grep `not an Update*/Set* push`
  after a run.
- **Commlink with subtitles OFF.** The reader obeys the game's Show Subtitles setting
  (`surfaces/strategy.md`): off must be SILENT, and turning it back on mid-conversation must
  announce the line still in progress rather than swallow it.
- **Research report from the Shadow Chamber** — the only path that puts that screen on the 2D
  movie (`research-report-recon.md` §7).
- **Research available**: a deck of ≥2 cards (the flip announcement on Continue), a
  shadow-project card, and the close into `UIRewardsRecap` on the tutorial-objective path —
  which is UNCOVERED and is that chain's next hole (`research-unlocked-recon.md` §5).
- **Geoscape region actions** (`geoscape-recon.md` §16): make contact and install a radio relay,
  the two pin buttons vanilla wires to no key. Needs a save with `ResistanceRadio` researched;
  its confirm alert is the Flash capture's `Alert_MakeContact` panel.
- **Resistance orders**: the READ-ONLY visit (`PolicyReadOnly` on the name, verbs raising
  `UICantChangeOrders`), a direct swap of two slotted cards, Backspace-cancels-hold, and the
  empty-hands refusals `PolicyCantPick` / `PolicyCantMove`.
- **After action**: a KIA card's Memorialize into the photobooth (no recipe — expect
  Escape-only) and the bond icon into `UISoldierBondScreen`, which now has the `soldier bonds`
  recipe (its own first pass is under live-run debt above) — from here it opens in `bSquadOnly`
  mode, the branch where vanilla gates the soldier switch and the cycle off.
- **Objectives readout** (`Ctrl+X`): the Avenger / dark-event branch and the counter-row branch,
  which need the ship and a Retaliation-style mission respectively.
- **Mission blade**: a GOps blade with 2+ missions (the selector buttons are mouse-only in
  vanilla, and clicking one must re-announce the switched mission via the MissionRef extra-key),
  the ambush blade (its Chosen strip must stay correctly SILENT), and the Shadow Chamber /
  sitrep sections — silent so far only because the campaign has neither, so the gates are
  untested in the POSITIVE direction.
- **Tactical HUD graph**: on a mission with evac available, Commands slot 6 appears ("Place evac
  zone"-family label) and Enter on it must raise free aim. And one design question a live run
  has to answer rather than a check: whether staying HUD-focused through the enemy turn is
  livable, or whether End Turn should hand back to Map mode on its own.
- **Promotion screens**: the classic tree is walked, the other two are not. `_PromotionHero`
  needs a faction hero (or any class after the Training Center retrofit) and its verb is buying
  an ability with AP, including the refusal when AP is short — ⛔ and it is the one screen where
  our layer is the ONLY keyboard route, since vanilla handles DPAD/LStick only and
  `InitPromotion` calls `DisableNavigation()`. `_PromotionPsiOp` needs a Psi Lab and a trained
  operative. On any of the three, a locked rank's ability names must stay UNSPOKEN — the reader
  resolves names in script precisely so `m_strAbilityLockedTitle` substitutes, and a regression
  there leaks what no sighted player can read.

Tactical narration's panic / shields / evac
events; the hacking screen's cancel line and any hack whose slot 0 is `bBadThing`; the ant farm's
countdown path (`Status` is empty unless something is building) and its `Delete` re-announce; the
SAVE screen (only LOAD was exercised — new-save row, overwrite confirmation, a save completing);
the text-entry reader's own announcements (`just dev-log` should carry `TextInput: dialog up (...
caret N, selection A..B)`); the Resistance Ring's covert-op status line, in BOTH branches — an
action running (name + days left) and none assigned.

The tile cursor **across floors, in TARGETING mode** — the half of the floor redesign that was
deliberately left unchanged. World mode treats the game's floor level as a ceiling and lands
every step on the highest surface under it (`LandingTile`, the mouse trace's rule, 2026-09-16);
`X2AccessTacticalReader`'s `ResolveAimFloor` still steps with the old one-tile-down search and
`PushAim` / `EnsureAimPlanted` push position WITHOUT floor state. The pathing-pawn clamp that
motivated the redesign does not apply there, so this is an open question rather than a known
defect: whether any targeting method, or the melee pathing pawn, cares about
`m_iRequestedFloor` across floors — and whether free aim should step off a roof to the ground
the way world mode now does. Throw a grenade onto a roof from the ground and compare the spoken
land tile against `GetTargetLocations`.

The world-mode cursor's **ceiling model, live** (`docs/surfaces/tactical.md`, the floor-level
rule) has not been heard yet. Gatecrasher with a soldier on a roof: step off the edge and expect
`Down 4` then the ground tile, step back and expect `Up 4` onto the roof (the level stays where
the soldier switch put it), `Ctrl+,` on open ground should speak `Floor N of M` and NOT move the
cursor, and a marker jump onto a roof from the ground should speak the new level and then be
movable with Backspace. Shape-count the ring for `Up N`/`Down N` afterwards: stairs should show
`N=1`, roofs `N=4`, and a distribution with nothing but one value is the audit rule from
`CLAUDE.md` firing. First heard 2026-09-16 in the Abandoned warehouse: the model itself held
(the pawn's destination was the floor beside the crate, the ceiling clamp never fired), and what
it exposed was the older *enterable is not standable* gap — a crate top spoken as *"Dash"*
(`surfaces/tactical.md`, the tile cursor). Fixed the same morning, **unheard**: on that crate
top expect *"Up 1. Cannot stand here"* and, on Backspace, *"Cannot stand there. Nearest 1 west,
1 down"* (or whichever floor tile the puck chose); on a tile past the move range, *"Out of range.
Nearest …"*. The cursor still lands on the crate top — the mouse cursor slides off one, but its
puck is what a sighted player watches, and *Nearest* is that puck.

The tile cursor's **detection readout on a hacked tower** — the one branch of the concealment
work a normal Gatecrasher sweep does not produce. "Tower detection" is confirmed; what nobody
has heard is the ring BEFORE and AFTER the hack, i.e. that hacking the tower actually silences
it. (The wider parity audit behind that readout — `concealment-recon.md` §4's four assumptions,
our recompute against the game's own markers — is queued work item 6, not a test.)

The WOTC **first-time popups** now that `TutorialReader::OnTick` polls the modal outside the
tutorial (`surfaces/tactical.md`). This one needs no campaign luck: `just dev-exec
"ShowModalTutorialDialogBox 0 0"` spawns a `UITutorialBox` on demand. Check that the line carries
title, body and the dismiss hint, that `I` re-reads it on an ordinary mission, and that dismissing
it leaves `TutorialReader: inactive (modal only — not the tutorial mission)` in the log rather than
a repeating announcement.

**The GOG build, on a GOG machine** (built 2026-09-20, the day the first GOG tester reported).
`src/game_build.cpp` holds a row per retail build and every module-relative address now comes from
it; the five GOG RVAs in it are derived and cross-checked but have **never been run against the
actual game** — nobody here has that copy. The Steam row is confirmed live (`Game build: Steam
(timestamp 0x621562FA, image 0x20C7000)`, GNames and GObjects validated, 1610 vtables patched, the
movie player validated), which proves the mechanism, not the GOG numbers. What a tester's
`X2Access.log` settles, in order: that the first lines say **`Game build: GOG (timestamp
0x5B9A17C4, image 0x20CA000)`** — if it says UNKNOWN BUILD, their exe is a third build and
`tools/derive_offsets.py` needs it; that `SDK: GNames validated` follows within a few seconds
(the two `GNames validation failed (code -1)` lines before it are normal, and the mod says so at
ten failures, not two); that `ProcessEventHook: Patched N unique vtables` reports a four-figure N;
and that a cutscene logs `MovieSubtitles: movie player ... validated (GOG build)` rather than the
DISABLED line, which is the only one of the five addresses a boot does not exercise. ⚠ Still not
settled by anything: whether a class layout we transcribe differs between the 2018 and 2022
packages, outside the three the Highlander replaces — so a surface reading wrong on GOG while the
same surface reads right on Steam is the shape to watch for, and is NOT a recipe bug until that is
ruled out.

The install check's **non-Steam branches** (`tools/dist/X2Access-Check.ps1`, 2026-09-20). Three of
them have no machine here to run on and were proven against fixtures instead — a temporary tree
plus the script's own functions lifted out with the PowerShell parser: game discovery through the
GOG registry and the Epic manifests, a Highlander hand-unzipped into `XComGame\Mods`, and the
one-folder-too-deep report for a zip that carried its own top folder. What a real non-Steam copy
would settle is whether the store's own folder shape still puts `Binaries\Win64\XCom2.exe` where
`Test-GameFolder` looks, and whether `Documents\My Games\XCOM2 War of the Chosen` is still the
config path there. The Alien Hunters halves DID run live on this machine (DLC present + plugin
subscribed, and the stray-plugin report by pointing `-GamePath` at a tree with no `DLC_2`); the
one branch nobody has seen is a real install that owns the DLC and lacks the plugin.

## Known defects

- ⛔ **An in-engine MATINEE cutscene reads NOTHING, and nothing in the mod currently owns that
  path.** Found 2026-09-04 in the same log that confirmed movie subtitles working:
  `CIN_TP_OldGods_pt1` played for **2 minutes 13 seconds in total silence** (13:37:03 → 13:39:16,
  28 log lines, not one of them speech), and `CIN_TP_ShadowChamberReveal` and `CIN_TP_BuildOutpost`
  the same. This is NOT a `movie_subtitles.cpp` defect — that surface behaved exactly right, and
  the reason is on disk: **`CIN_XP_PreIntro` has both a `.bk2` and a `.txt` cue track and read all
  35 of its cues; the `CIN_TP_*` three have a `.txt` cue track and NO `.bk2` at all.** So the Bink
  player is handed the NAME (which is why `MovieSubtitles: movie '…' opened` is logged, and why
  `IsAnyMoviePlaying` gates the navigator with `movie 1`) but never a cue table, and the reader
  correctly stays quiet. The text exists — 21 cues under `CIN_TP_OldGods_pt1_*` in
  `XComGame/Localization/<lang>/Subtitles.<lang>`, the same file the Bink path reads — drawn by
  `RenderExtraSubtitles` timed against the MATINEE rather than a `BINKHANDLE`. ⚠ The per-cutscene
  `CIN_TP_OldGods_pt1_ART.<lang>` package is a red herring: its `SpokenText` entries are all `""`.
  `surfaces/strategy.md` assumed `UINarrativeCommLink` covers these (`HideUIForCinematics` spares
  it); this run says something does not, so the first job is to find out WHICH path draws them —
  one live pass with `just dev-props` during such a cutscene, looking for who holds the cue index,
  before any code. The gap is worth the trouble: these are the game's tentpole story beats, they
  run minutes long, and silence over one is indistinguishable from a hang.
- **Raw `". "` joins survive in the LEGACY screen listeners** — `UIScreenListener_Difficulty` (six),
  `UIScreenListener_NarrativeContent` (three), `UIScreenListener_KeyBindings` (two) and
  `X2AccessGeoScanner` (one) each build `Label $ ". " $ Value` instead of calling
  `X2AccessHelper.AppendPart`. Same defect as the four fixed on 2026-09-04, and every left side is a
  game-shipped label, so a language whose labels end in a colon voices a doubled terminator on every
  row. None showed in the 2026-09-04 ring because those screens were not walked; do not read that as
  evidence they are fine. Cheap to fix, and the natural moment is when each listener is retired for a
  recipe rather than as a sweep of its own — a listener that is about to be deleted does not need a
  patch. ⚠ `X2AccessGeoScanner` is NOT in that category (no recipe is queued for it), so it is the
  one worth fixing on its own.
- Two wording defects: the no-target readout doubles a full stop and repeats the same fact in
  two languages (the localized string already ends in a period and carries the meaning); recentering
  says the unit's name three times.
- The dialog recipe attaches wherever a `UIDialogueBox` shows, including in the strategy and tactical
  layers — deliberate (a modal must be readable), but it means the layer takes over input there
  before those screens have recipes of their own. Its screen lookup caches the newest box, which a
  presentation-layer change could leave stale; covering dialogs outside the shell should switch that
  to a screen-stack walk.
- **Dev-server connections are intermittently RST mid-serve — the request still EXECUTES, only the
  response is lost.** Measured live 2026-08-31, mid-tactical: curl reports `(56) Recv failure:
  Connection was reset` after `upload completely sent off`, yet every such `/exec` command appears
  in Launch.log ("Command not recognized: X2AccessProbe1..6" for six requests of which five
  "failed"). So a `just dev-exec` that errors has usually still run the command — ⚠ blind-retrying a
  TOGGLE (TakeNoDamage, ToggleUnlimitedActions…) flips it again; verify state via `/props` instead
  of retrying. What is ruled out: not curl/just/pwsh quoting (identical bytes succeed and fail); not
  close-with-unread-data (the GET form resets too); not our `gClient` handling (single-threaded, one
  close per socket); not mere connection lifetime (a socket held open 5 s by an incomplete request
  survives and gets its response through the same SendResponse+close path). Incidence varies over
  time and eventually hit `/health` itself, so it is server-wide; `/exec`'s ~300 ms serve
  (`Sleep(250)`) just has the widest exposure window. Best theory: something in the game process
  sporadically closes a socket handle it no longer owns, and whether it collides with our live
  client handle is handle-recycling luck — `SendAll` then fails silently (`n<=0` return eats the
  error) and `closesocket` on the dead handle hides the rest. Next steps: log `WSAGetLastError` on
  `SendAll` failure so the server SEES the kill; Frida-hook `ws2_32!closesocket` logging caller
  module + handle to catch the strayer (needs a session where the game may be driven — not while
  the user is playing).

## Queued work

0. **More earcons**, now that the pipeline exists (drop a WAV in `Sounds/`, register it, play it
   by name). Candidates from the RT palette and this mod's own gaps, in value order: the enemy
   side of sight — "Seen by N" on the move preview is already spoken, and a tone under it would
   let a player hear exposure while walking the cursor without a word per step; the two-press
   move-to's "press again" prompt; the HUD-layer enter/leave; the boundary thud on `Map edge` and
   `Top floor` / `Bottom floor`; and a speech-free "input is yours again" (`layered-narrating-kestrel`).
   Each one is a parity question first (the LOS cue passed because the fog overlay is on screen).
   A settings toggle per cue is owed once there are more than two: RT's fog cue ships default-on
   with its own switch.
1. `Narrator::Say` adoption: the menu navigator — `menu_navigator.cpp` plus the recipe TUs under
   `src/recipes/` — is the last surface still calling `TTS` directly,
   and since 2026-09-16 it is a plain swap: every channel queues in order with no clear and no
   interrupt, so the keypress-driven moves map onto `Focus` and the differ landings and live-part
   changes onto `Focus` too (they are the focused control changing) — the channel is only the
   label the scrollback shows. Four boot lines stay on `TTS` on purpose and say why in place
   (`dllmain.cpp`, `Hooks::Init`): they run on the init thread, which `Narrator::Say` does not
   claim, and they report the mod's own state before any producer can have queued a line.
   ⚠ The old "harmless today" justification was wrong and is why the other surfaces went
   unmigrated for so long: it rested on `TacticalHUD::OnTick` dropping the queue, which fires only
   on the tactical layer's FALLING EDGE — nothing dropped it under the tile cursor or the object
   scanner, and the geoscape is not tactical at all.
2. Events NOT yet narrated, in rough value order: the reinforcement WARNING (`ReinforcementSpawnerCreated`
   — the "REINFORCEMENTS INCOMING" flare with its countdown, which the object scanner lists but nothing
   announces when it appears), the "surprised" clause on a reveal (`SurprisedScamperUnitIDs` plus
   `X2Action_RevealAIBegin.SurprisedText`, whose CDO must be checked to hold the word first), the
   Chosen arriving, objective completion, will/panic thresholds. Each is one more `AddCHEvent`
   line; the cost is judgement about spam, not code. `UnitMoveFinished` was deliberately dropped —
   the shipped sources trigger it only from `DLC_2`'s Alien Ruler code, so it is not a base-game event.
   The drop and the reveal themselves came off this list on 2026-09-16 (live-run debt above).
3. Dev harness: `/tile` (the tile reader's state as structured data) and `/tactical` (mode, gate
   reasons, last narration lines) are the remaining endpoints. `just dev-speech` covers most of the
   latter already.
5. Hacking phase 4: a pre-commit odds read via
   `XComGameState_Ability.GetUISummary_HackingBreakdown`, the spoken form of the mouse-hover-only
   `UITacticalHUD_HackingTooltip`, so a player can judge a hack before spending the action point.
   ⚠ Three voices can land on the hack outcome — the reveal line (everywhere), the `UIWorldMessageMgr`
   flyover poll (tutorial only today) and `X2AccessTacticalEvents`/`X2AccessReplayNarration` — and
   they do not all fire on the same mission, so check each where it actually fires.
6. The path-marker exact-parity read (`concealment-recon.md` §5): native, by-name reflection of
   `XComPathingPawn`'s five private marker arrays — no offsets or `static_assert`s needed for a
   by-name `TArray<TTile>` read. It is the audit tool for the recon's §4 assumptions (game markers
   vs our recompute on the same settled hover) and the only route to `LaserScopeMarkers` /
   `KillZoneMarkers`, two warnings the companion recompute does not attempt. Read it only where
   the path has settled — `CursorMoveTo`, or the queued `/tile` endpoint (item 4).
7. Strategy-layer surfaces still unread, in value order: `UIFacility.m_kResearchProgressBar` (the last
   of the room's Flash-only status panels — `m_kCovertOpsStatus` is done, `surfaces/avenger.md`),
   and on the Avenger HUD the two panels the ant farm's Tab layer left out on purpose:
   `UIAvengerHUD.StaffIcons` (one icon per unstaffed ENGINEER — the scientist loop is commented
   out in shipped source — clicking opens the engineer list; likely a duplicate of the to-do
   strip's Staffing category, so check that before building) and the crew status indicators
   (`UIAvengerHUD_SoldierStatusIndicatorContainer`, bond/trait icons anchored to walking pawns
   in 3D — they go off-camera for a sighted player too, so a stop for them is an AID and must be
   labelled as one). ⚠ The resource strip, the to-do widget and the event queue all came off this
   list on 2026-09-03 (`surfaces/avenger.md`) and are **unexercised live** — see the entries
   above.
8. The tactical HUD recipe: Soldier (2026-08-31, `X2AccessUnitReader.BuildSoldierRecords`),
   Commands (native — `GameUI::ReadMouseControls`) and now Targets (2026-09-04,
   `X2AccessTacticalReader.BuildEnemyRecords` / `SelectEnemyHead` — the live-run debt entry
   above) are built. Still owed: the spoken REASON on an unavailable ability (`AvailableCode`
   needs the companion vocabulary — the FName must never be spoken raw), and a decision on End
   Turn's focus hand-back.
9. The mission blade's two v1 punts, each a deliberate gap rather than test debt:
   `UIMission_GPIntelOptions` reads its intel options (name + cost + totals) but cannot PURCHASE
   one — the rows are `UIMechaListItem` checkboxes, so the toggle path and the purchased-state
   read are owed together — and the sitrep panel's mouse-only info button (it opens
   `UISitRepInformation`, which the popup recipe already reads) has no node. The second is low
   value: the reader already speaks each sitrep's name AND description inline.
10. Distribution, after the rolling `dev` release (2026-09-16, `docs/build.md` Release). Three
   things stand between it and a Steam Workshop listing for the companion, in order:
   - **A version handshake.** The native side resolves every companion function and property by
     name, so a companion that gets ahead of an installed ASI fails SILENTLY — a renamed property
     reads as empty, a renamed function is never found. Workshop auto-updates the script half on
     its own, which makes skew certain. A version constant on the companion CDO, read at init, and
     a spoken mismatch line, before anything auto-updates.
   - **Bundle the ASI loader in the archive.** MIT-licensed, and "extract one archive" is the whole
     point for a blind player; today the notes send them to a second download. Pick the host DLL
     name once and say which.
   - **Then a HIDDEN workshop item** to test the install path, public only after the archive has
     had external installs. Workshop can only ever carry the companion; the ASI stays on the
     release page, so the listing must say so in its first line.
11. Third-party mod support, scoped in `lwotc-recon.md` (2026-09-16, read off clones under
   `refmods/`, nothing measured live — LWOTC is not installed on this machine). Long War of the
   Chosen is five mods, three of which REPLACE screens we already read, and every replacement is a
   plain subclass of the class it replaces — so the first item stands on its own and is worth doing
   whatever happens to the rest:
   - **Subclass-tolerant screen matching.** `FindStackScreenRaw`, `IsInstanceRaw` and
     `ObjectClassNamedRaw` all compare with `strcmp`, so a `ModClassOverrides` subclass makes nine
     call sites across five TUs go blind while every offset they read is still correct. ⛔ Not a
     blanket replacement: `UIArmory_PromotionHero` and `UIArmory_PromotionPsiOp` both extend
     `UIArmory_Promotion` and `UIMission` has sixteen subclasses the blade recipe tells apart by
     name, so those hierarchy probes must keep most-derived-first ordering — and tolerance changes
     which recipe wins a screen, so the `kRecipes` order has to be re-reasoned rather than assumed.
   - ⛔ **Finding a screen is not reading it**, and the counter-example is already in hand:
     `robojumper_UISquadSelect` does not populate the inherited `m_kSlotList` that
     `X2AccessSquadReader` loops over. A tolerant match that lands on a reader which then reads
     nothing narrates SILENCE, which a blind player cannot tell from an empty screen.
   - The rest — roughly 12–18 new recipes, the haven layer first because it has no vanilla
     analogue — is `lwotc-recon.md` §3 and §9, and the decision recorded there is to build it
     NATIVE-ONLY: recipes match by class-name string, so a recipe for an absent screen simply never
     probes true, and no second companion mod is needed until some derivation forces one.
   - ⛔ Before the first LW offset is transcribed, settle §8: `sdk_layout_checks.cpp` pins against a
     generated SDK that contains no LW classes, so the static-assert net stops at the vanilla
     boundary.
12. Debts created by the tolerant-matching pass itself (2026-09-16, merged to main as `af4725b`).
   One vanilla run exists and it proved the change INERT there: zero `matched subclass` lines in
   nineteen minutes of menu and tactical play, meaning no finder resolved to anything but an exact
   class, so vanilla behaviour is identical to before by evidence rather than by argument.
   ⚠ **Still never run against any mod that renames a screen** — no LWOTC, robojumper or CPS
   install has been exercised, so every claim about what tolerance BUYS remains unmeasured, and
   every hierarchy claim comes from the SDK's `SrcOrig`, not from a Highlander-derived dump.
   Four screens are deliberately left on `ClassMatch::Exact` because
   finding them is not the same as reading them, and each one is owed the read before it can be
   tolerated:
   - `UISquadSelect` — `robojumper_UISquadSelect` spawns its own `robojumper_UIList_SquadEditor
     SquadList` and never fills the inherited `m_kSlotList`. Owed: read that list (a new offset, or
     find the `UIList` child by class or by its `Tag` `'rjSquadSelect_Navigable'`). ⛔ Until then
     tolerating it would make the recipe attach, TAKE THE ARROW KEYS and narrate an empty squad —
     worse than the silent non-attach, which at least leaves the keyboard to the game.
   - `UIAfterAction` — `UIAfterAction_LW` adds `m_kSlotList2/3/4` and `ReadAfterAction` reads only
     the first. Tolerating it would read six soldiers of twenty-four and drop the tail silently,
     which is the "a cap that drops the tail must be LOUD" rule exactly.
   - `UIOptionsPCScreen` — `UIOptionsPCScreen_LW` adds a tab strip while `ReadOptions` names five
     tabs from five fixed class fields. Owed: source the tab names from the screen's own tab list.
   - `UIPauseMenu` — `UIShellStrategy` extends it (the game itself separates them with
     `IsA('UIShellStrategy')`), and the tracked table probes each entry independently with no
     most-derived-first order to lean on. Owed: an ordered probe or an explicit exclusion.
     ⚠ `game_ui_shell.h` claims the pause-menu read already covers `UIShellStrategy`; that was
     only ever true of the field LAYOUT, never of the finder. Settle the two statements together.
   - The companion still switches on the EXACT class name — `X2AccessPopupReader.uc:66`,
     `X2AccessMissionReader.uc:65`/`:399`/`:420`. Now that the native popup finder tolerates
     subclasses, LWOTC's `UIAdventOperations_LW` IS found and falls through to the `default:`
     branch: the player gets "a modal is up" and the buttons rather than the bespoke text. Better
     than not attaching, but a partial read. Fixing it means ordered `if (UIAlert(Screen) != none)`
     cast chains — order matters, `UIAlert_DLC_*` extend `UIAlert` — and a companion recompile.
   - Two vanilla MULTIPLAYER screens became claimable for the first time and neither has ever been
     run: `UIArmory_Loadout_MP` (via the tracked loadout entry) and `UIMPShell_UnitEditor` (via
     `FindArmoryMenuScreen`). Both drive the same inherited widgets their bases do, so an empty
     read is unlikely — but nobody has exercised either, and a short read there costs the player
     the game's own key handling too.
   - Cost, MEASURED on the 2026-09-16 vanilla run. The whole price is paid once, at the first menu
     attach: **61 base classes resolved in 131 ms (~2.1 ms each)**, in a single navigator probe
     pass between `found UIScreenStack instance` and `attached to main menu` — call it eight
     dropped frames on the main menu, where nothing is time-critical. Nothing re-resolves
     afterwards: **63 names resolved, none of them twice**, and the remaining two resolved lazily
     when their screen first appeared (`UISpecialMissionHUD` at mission start,
     `UILoadScreenAnimation` at a load screen). The log carries no tick markers, so "one pass" is
     inferred from that bracketing rather than proven.
   - What keeps it off the steady-state tick, now that sixteen of the seventeen `ScreenFinder`s are
     Derived and each owns a full GObjects pass: BOTH scans (`ScanInstanceRaw` and
     `ScanInstancesRaw`) carry a per-call class memo, so a pass does one chain walk per distinct
     CLASS rather than one per (object × entry) — which matters most for the single-class one,
     since a `gScanEpoch` bump fires all sixteen in the same tick, at a screen change, exactly when
     the player is waiting to be told what appeared. `ResolveBaseClassCached` backs a miss off from
     1 s to 60 s so an unresolvable name cannot cost a full scan every second forever; both it and
     `NoteOverrideRaw` use flat strcmp arrays rather than `unordered_*<std::string>`, which
     allocated a key on every call including the hits. The same run recorded **0 pump faults across
     2,470,300 pump calls**, so the SEH-guarded chain walks never faulted.
     ⚠ The steady-state claims are still UNMEASURED: on vanilla no `Derived` entry ever matched a
     subclass, so the scan-heavy path this reasoning is about was never actually taken. Only the
     resolver cost above has a number against it.
13. **Rooms and exits** — the WH40KRTAccess `RoomMap` / `V`-exit-cycle port. **Scoped AND measured
   in `docs/rooms-recon.md`** (2026-09-16: source scope, then a live run through
   `X2AccessTileProbe`). Five of the seven measurements are answered and the design SURVIVED them.
   **The EXITS half shipped 2026-09-16** as `world.exits` on `V` (live-run debt above) — §8.6 now
   gates only the ROOM half, the "leads to \<room\>" clause and the room readout. What is left
   here is the partition.
   - ✅ The bits the design rests on are LIVE. `COVER_ClimbOver_*` / `ClimbOnto_*` are populated —
     6/6 against the object scanner's own cover ring, every low edge climbable, no high edge. So
     the preferred predicate (`blocked && cover && !climb`) stands.
   - ✅ ONE cover oracle: `TileData.CoverFlags`, `XComCoverPoint.Flags` and the engine's own debug
     string are the same word, so the cover ring and a room map cannot drift.
   - ✅ `TraversalStarts` is a live adjacency list (13–16 neighbours per tile) — connectivity with
     no bit transcription, though it does not name the traversal TYPE.
   - ⚠ `IsAdjacentTileBlocked` is DIRECTED, not an edge predicate: it folds in the destination
     tile's own occupancy. Correct behaviour, and the resulting asymmetry is a TELL that marks
     obstacle tiles. Barriers on EDGES are recorded symmetrically on both sides.
   - ⚠️ **§8.6 is 1 of 3 maps** (run 2026-09-17, ADVENT city centre — the OPEN URBAN sample; recon
     §8.6 run 1). ✅ **(b) sight-gated is ELIMINATED**: on the upper storeys it finds 11 walls where
     (a) and (c) find 13, and ZERO where they find 4, because it misses `BH--` edges — blocked, full
     cover, see-through (glass, railings). It lets the flood cross an edge the engine calls blocked,
     and it drops exactly the walls a blind player most needs called.
   - ⚠ **(a) vs (c) is still undecided**: over 3720 sampled edges **not one blocked edge was low
     cover or climbable**, so the climb gate and the height gate have had nothing to disagree about.
     Map 2's falsifier is one `BL`/`BO`/`BN` edge. §3's sandbag fear IS measured false here — 53
     low-cover edges, none of them blocked, because a sandbag you can vault is not blocked.
   - ⛔ **A PARTITION IS A HORIZONTAL SLICE AND A MAP IS NOT FLAT — this cost run 1 its first
     conclusion.** Sliced at the tile cursor's `Z=5` the map read one room everywhere and the
     predicates looked indistinguishable; the building with eight rooms in it was at `Z=9`, and a
     floor the slice misses reads as `#`, identical to solid rock. `ProbeRoomsColumn(X, Y, Radius)`
     was added for this and is the FIRST call of any §8.6 run.
   - ⛔ **Instrument bug found and fixed 2026-09-17**: `IsWallEdge` asked `IsAdjacentTileBlocked`
     forward only, and one-way edges between two STANDABLE tiles are real — `(27,32,9)` could be
     left and not entered, a unit standing on it. That made the whole partition SCAN-ORDER
     DEPENDENT. Both the blocked test and the per-face bits now ask from both ends (`FaceIsWall`).
     ⚠ **Needs `just uscript`** — written while the game was running, so not yet compiled.
   - ⚠ **Doorways remain the open risk, and run 1 did NOT measure one** — the upper floor's rooms
     came out separate and the plaza is one room because a plaza is one room. The a-priori argument
     stands (a doorway must be a passable edge, so no wall predicate sees it), which is why
     **`AddExits` is better understood as an INGREDIENT of rooms than as the fallback for them**,
     with the boundary set to try on map 2 being (obstacle tiles) ∪ (wall edges) ∪ (door threshold
     edges from `GetPathObjectFromTraversalToNeighbor`). Reasoning, not measurement, until map 2.
   - ⚠ Unexplained from run 1: `.--V` edges — walkable, opaque, no cover either side. 4 on the
     plaza, 28 on the upper floors. Nothing in recon §2 predicts one. Harmless to the flood; another
     reason (b) cannot be trusted, since (b) calls a walkable edge a wall.
   - ❌ **Still owed: maps 2 and 3 for §8.6** (interior-heavy, open wilderness) and **§8.7**
     (agree with `DescribeIndoor`).
   - ⛔ §8.7 should become the SCORING function rather than a checkbox: a room must never straddle
     an `XComFloorVolume` boundary and every door edge must fall on a room boundary. That is
     measurable, and it is the same rule that keeps the segmenter agreeing with `DescribeIndoor`
     instead of competing with it (§2.5). Not built yet — it wants the volume lookup the tile
     reader already does privately.
   - ⚠ §8.5 is only PARTIAL: `GetTileDebugString_StaticFlags` blows the 160-char `/props` cap after
     about two flags AND is localized (`Да`/`Нет` here). The hand-transcribed `TileStaticFlags`
     constants agree on everything visible and still have no `static_assert` net.
   - `V` is the exits key (player decision 2026-09-16) and is the first vanilla binding the mod
     DISPLACES — §6, and read `docs/input-map.md` first.
   - ~~Fallback if the partition renders badly at §8.6: ship EXITS without ROOMS.~~ **Taken
     2026-09-16, and run 1 of §8.6 then INVERTED the relationship**: exits is not the consolation
     prize for a partition that fails, it is the per-edge door query the partition needs to work at
     all (§9, recon §8.6 run 1). There is no fallback left — rooms now depends on exits.
   - The per-room "{pct}% unexplored" half of `rt-unexplored-frontier` waits for this; the frontier
     half shipped on its own as `world.unexplored` — and was **heard working live 2026-09-16**
     (live-run debt above).
   - ⚠ **§4's exit table was source-derived and two of its six kinds did not survive.** Windows and
     drop-downs cannot be listed at all: script cannot ask an arbitrary edge what traversal it
     would take, and a destructible has no localized name, so a window, a fence and a parked car
     are one unnameable thing. If the room partition ever lands, it does NOT bring them with it —
     that would need a native read of the pathing traversal data, which is its own job.
   - ⛔ `X2AccessTileProbe` is recon instrumentation living in a shipping package. DELETE IT once
     §8.6/§8.7 are answered, or promote the parts that earned their place.
14. **Regenerate the SDK dump under the Highlander BETA.** The machine switched from the stable
   Highlander to the beta on 2026-09-16 (`docs/build.md`, dependencies). The source-level check
   there is reassuring — of the 139 classes `sdk_layout_checks.cpp` pins, the 47 the Highlander
   patches have identical `var` blocks in both builds, and the beta's one real layout change
   (`XComShell.bIsMusicPlaying`) is in a class we never read by offset — but it is not the
   authoritative check and must not be mistaken for one.
   ⛔ **The layout asserts cannot catch this today.** They pin hand-transcribed offsets against the
   GENERATED SDK, and the generated SDK is still the STABLE Highlander's dump, so both sides of
   every assert move together and a native rebuild passes no matter what the runtime does. The
   asserts only become evidence once the dump is regenerated from a game running the beta — which
   is exactly the rule in CLAUDE.md ("regenerate the SDK from a game running the same mod set the
   players will run"), and the reason it exists.
   Order: launch with the beta loaded, `just sdk-generate` / `sdk-adopt` (the recipe already refuses
   a vanilla dump by testing `UIScreenStack.OnInputSubscribers`), then a native build. Any assert
   that fires is a real transcription that moved.
   ⚠ Distribution is a SEPARATE decision and is NOT answered by any of this: `ReadMe.txt`,
   `tools/release.ps1:389` and the `.XComMod` description all still tell players to install workshop
   **1134256495**, the stable. That stays correct only while the companion uses no beta-only API.
   It uses none today — the four beta-patched classes it names (`UITacticalHUD_InfoPanel`,
   `UITacticalHUD_ShotHUD`, `UITacticalHUD_ShotWings`, `X2CharacterTemplate`) are vanilla classes
   whose var blocks the beta leaves alone — but building against the beta makes drifting into one
   easy and silent. Re-check before the next release, or change what the notes ask players to
   install.
