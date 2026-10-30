# Tactical surfaces

Implementation rules for the tactical layer — the map cursor, the scan ring, targeting, and what the game narrates during a mission. Each ⛔ was earned by a live bug; each ⚠ is a
known limit or an open decision. **Read the section for a surface before editing
it** — `CLAUDE.md`'s Surfaces table points here for exactly that reason.

Cross-cutting rules (buffer sizing, the screen-accept trap, the empty-field trap,
speech joining, parity) live in `CLAUDE.md`; only what is specific to these
screens is here.

## Tactical tile cursor

The RT TileExplorer port, deliberately OUTSIDE the Graph kernel (the map is not a
widget tree). C++ owns keys and speech — arrows (world-absolute: Up is always north, so the
camera's yaw plays no part in stepping; the rotation now lives on Ctrl+Q/E), `Ctrl+,`/`Ctrl+.`
floors, `C` recenter, `Delete` re-announce, `Backspace` two-press move-to. Each
press makes one outbound call to the companion CDO (`GameUI::FindCompanionCDO` +
`CallScriptFunction`) and speaks the `NativeResult` reflected back. Script drives
the game's OWN `XCom3DCursor` (`CursorSetLocation`/`CursorSnapToFloor`/`MoveToUnit`
— the pathing preview comes free; the cursor is NOT range-clamped,
only the pathing pawn's destination is) and commits through
`XComTacticalInput.ClickToPath`, guarded: commit only when the pawn's clamped
`LastDestinationTile` EQUALS the cursor tile, so a move can never land anywhere
other than the tile just spoken. Readout is fog-gated (`GetTileFOWValue`:
NeverSeen → "unexplored"; HaveSeen → static layout; Seen → full) and the
reachability clause is gated on the tile being STANDABLE, not merely enterable — the
game's own pair from `XGAIBehavior:3439`, the same one the object scanner's cover ring
asks. `CanUnitsEnterTile` is "can a unit pass through" and is TRUE for the top of a
full-cover crate a soldier climbs over; `IsFloorTileAndValidDestination` is "can a unit
stop here" and is false for it. `GetPathCostToTile` prices every tile on the path graph,
climb-overs included, so gating on enterable alone read a crate top as *"Dash"*, and
Backspace then said *"Out of range"* for a tile the pawn had merely declined to stop on
(measured live 2026-09-16: cursor (17,32,10), pawn destination (16,33,9), the floor
beside it; the building's ceiling clamp allowed Z 12 and played no part). A sighted
player never meets this because the mouse pick keeps, within a floor band, a
valid-destination hit over a non-valid one, so the ray slides off the crate onto the
floor behind it; our column model cannot follow a ray, so the tile reads *"Cannot stand
here"* instead, and the move key's refusal names its reason — *"Cannot stand there"*,
*"Out of range"* (negative path cost), or *"Cannot stop there"* (the ceiling clamp) —
followed by *"Nearest …"*, the puck's actual tile as an offset, which is the one thing
the sighted player reads off the screen there. Claims stand down while
the shot HUD is raised, while a recipe is attached, or when `UITacticalHUD` loses
focus.

- `SetCursorTile` passes `bSnapToValidPosition` EXPLICITLY false — snap ON slid a
  floor-up push two tiles sideways (`GetClosestValidCursorPosition`) where the
  commit guard needs byte-exact pawn/reader agreement. State-first stepping
  always: never read the cursor pawn back (it sits at body-center heights and is
  repositioned natively every frame). `XCom3DCursorMouse.CursorSnapToFloor` is
  overridden to a no-op, so floor keys must set exact Z themselves.
- ⛔ **Pushing the 3D cursor does NOT move the camera on mouse and keyboard.** The mouse-mode
  default camera (`X2Camera_FollowMouseCursor`) never follows the cursor, because the mouse
  places the cursor by a ray through that camera and a follow would chase itself; only the
  controller camera (`X2Camera_FollowCursor.UpdateCamera`) follows. So `SetCursorTile` tells
  `X2AccessCamera_TileFollow` (a subclass of the mouse camera, added above the vanilla default
  on the first landing of each camera stack, found by `CameraTag`) to centre on every landing.
  It follows OUR moves only, never the mouse. Heading: the game offers only `HumanTurnYaw`
  (48.5) plus 90 degree steps, none of which puts north up; by player decision (2026-09-30)
  the one nearest north is applied at the camera's first activation and on `C` (the shipped
  default itself, screen-up about 41.5 degrees west of north). A true north-up yaw of 90 is
  possible (the free-look path proves the camera takes any angle) and was deferred, not refused.
- ⛔ **The game's FLOOR LEVEL is a CEILING the cursor lands under, never a floor it is
  pinned to — and it is shared, load-bearing state: never bypass it, never duplicate it.**
  The rule is the mouse trace's own, `XComTacticalHUD.GetMouseInteractionInterface`: of every
  surface the pick ray crosses it keeps *"the first actor that is at or below the requested
  floor, unless there are no hits below our requested floor, in which case we take the lowest
  floor above"*. With the wheel left on a roof, hovering open ground picks the ground, a shed
  its roof, a three-storey block its second floor — and hovering never moves the level; only
  the wheel, the D-pad and a soldier switch (`CursorMode_NoCollision.Reset`) do. Camera ZOOM
  is a separate binding and plays no part. `X2AccessTileReader.LandingTile` transcribes that
  rule per column (highest floor tile whose band is at or below the level, else the lowest
  above, else hang), every step and floor key lands through it, and the step speaks the height
  it took (`HeightWord`, "Down 4" off a roof) wherever a sighted player watches the cursor drop —
  which inside the fog is not everywhere (`VerticalDrawn`, below).
  The previous design pinned the level to the landed tile and stepped with a one-tile-down
  search, so a step off a roof hung in the air, a step back under it hit the wall, and a
  one-tile rise was never found — a sniper on a roof could not point at the ground without a
  floor key (player report 2026-09-16).
  - Four consumers key on `m_iRequestedFloor`: the camera band, the building cutaway,
    `WorldCascadeZ`, and `XComPathingPawn:991`, which caps the destination search at that
    level's ceiling whenever the cursor is inside a `XComBuildingVolume`. The clamp is an UPPER
    bound only — a cursor below the level is what every mouse player produces and the pawn
    accepts it — so the invariant is *never land ABOVE the level while a surface exists at or
    below*, which `LandingTile` holds by construction. Breaking it is what a position-only push
    onto a roof did (measured live 2026-08-31: cursor Z=13, pawn destination Z=9, level 1 — the
    evac-zone roof), masked as "genuinely out of range" whenever the tile WAS out of range,
    which is why it survived every ground-level test. The trace's own fallback, the lowest
    surface above, breaks it too and is kept for parity: the move key refuses that tile
    exactly as the pawn refuses a mouse player's.
  - `SetCursorTile` does NOT write the level. It still calls `CursorSnapToFloor`, which is what
    derives camera band, cutdown height and effective floor from the level and on the mouse
    cursor moves nothing (`XCom3DCursorMouse:11`), and pushes the exact position LAST so a
    base-class snap could never displace the tile the reader spoke. An explicit JUMP — plant,
    recenter, the scanner marker — adopts the target tile's band as the level (`AdoptFloor`,
    the soldier-switch idiom) and speaks it when it changed, so a marker on a roof is reachable
    without a floor key first.
  - The floor KEYS drive the game's own `AscendFloor`/`DescendFloor` (one press = one 4-tile
    band, announced as "Floor N of M" in the game's numbering) rather than an invented
    per-column Z hop — the class-scope versions are empty stubs, the real bodies live in
    `state CursorMode_NoCollision`, and script dispatch reaches them. Ascending on open ground
    moves the camera band and not the cursor, as the wheel does. And the reverse direction is
    POLLED (`CursorFloorPoll`, 150 ms from `TileReader::OnTick`, only while the layer owns
    world mode): the wheel or the game itself re-flooring on a soldier switch is spoken once
    and the planted cursor re-landed under the new level, so the two can never stay diverged.
  - ⚠ Targeting mode's aim cursor (`X2AccessTacticalReader.ResolveAimFloor`) still steps with
    the old one-tile search; whether the level means anything to a targeting method is the
    open question in `docs/next-steps.md`.
- ⛔ **A NEVER-SEEN TILE IS OPAQUE ON SCREEN, and the cursor's VERTICAL words are the one part of
  its readout that has to be gated for it (`X2AccessTileReader.VerticalDrawn`).** The fog is not a
  tint over visible terrain: `XComFOWEffect` paints never-seen voxels `FogColor` (R=5, G=5, B=25)
  at **A=255**, and `HaveSeen` at `HaveSeenTintColor` (128,128,128). Measured on the live instance
  for `Plot_CSH_Stronghold_Short_04` (2026-09-16): `VoxelSizeUVW` = 1/56, 1/56, 1/9 against a
  56×56×9 `XComWorldData` — exactly one voxel per tile — `LevelVolumeDimensions` the reciprocal of
  the map's world extent, `bHiding` and `bShowFOW` both true. Confirmed in pixels: 4.1% of a
  screenshot fell within ±14 of that exact colour as one contiguous hard-edged blob, and the blob
  halved and dispersed (4.4% → 2.2%) when only the floor cut changed, which a solid object cannot
  do. So the three-state readout above is a faithful mirror of the shader, not a parity loss, and
  "un-gate the static layer" is a REFUSED idea, not a queued one.
  - The hole in the fog, and it is deliberate: Firaxis added `bTranslucentIgnoreFOW` to
    `PrimitiveComponent:248` (marked `//FIRAXIS ADDITION`) and set it on `XComPathingPawn`'s
    destination puck, circle, melee puck, out-of-range puck and path ribbon (`:1721`..`:1918`) and
    on `XComMovementGridComponent:114`, the move-range border. **A sighted player who puts the
    puck on a never-seen tile sees its elevation** — the puck sits on the hidden surface and the
    ribbon climbs to it. `LandingTile` reading a true height through fog is therefore parity
    THERE, and only there.
  - Where the hole ends: the pawn clamps its destination to the reachable area, and the marker it
    draws at the CURSOR instead is **controller-only** (`XComPathingPawn:1228` gates it on
    `` `ISCONTROLLERACTIVE ``). On mouse and keyboard a cursor past the unit's reach is marked by
    nothing at all. So `VerticalDrawn` is a conjunction — *not never-seen*, **or** *standable and
    priced by the reachable cache* — the same pair `CursorMoveTo` already asks before it will
    issue a move. It gates `HeightWord` at all three call sites and the up/down leg of
    `OffsetWords` (via `DescribeOffset`, where the gate lives so a third caller cannot skip it).
    Before this the scan of that map was full of `"Up 1. Unexplored."` — the elevation of tiles
    the game shows nobody.
  - ⛔ Gate the WORDS, never the LANDING. `LandingTile` mirrors the game's own cursor snap and
    four consumers depend on the cursor agreeing with it (the bullet above); a cursor that refused
    to follow hidden ground would break the move key, not protect anything.
  - ⚠ The answer depends on WHO is selected — the same fogged tile speaks its height for a
    soldier who can reach it and goes quiet for one out of moves, because the puck and the border
    are drawn for the active unit or not at all. Correct, and it will look like a bug in a speech
    ring: a shape-count over a run will show the same tile both ways.
  - NOT gated, and each for a reason the game states: the BEARING (`3 north, 2 east`) — the player
    drives the cursor, so where they put it is theirs to know (WH40KRTAccess, `rt-visual-parity`);
    the floor LEVEL spoken by `FloorWord`/`AdoptFloor` — that is the player's own camera band, the
    wheel's readout; and the object scanner's MARKER offsets, because objective flags are drawn
    through fog by the game's own exception (`UIUnitFlag:1568`, `UISimpleFlag:387` — a flag is
    screen-projected, so its height is on screen with it).
- **Line of sight is an EARCON, played on the crossing (`X2AccessTileReader.CueSight`).** The
  first earcon in the mod: the cursor stepping from a tile the squad sees (`eFOWTileStatus_Seen`)
  onto one it does not plays `X2Access_LOS_Lost` (WH40KRTAccess's `fog_enter`, a short higher
  tone), and the reverse plays `X2Access_LOS_Gained` (`fog_exit`, longer and lower). Parity is the
  fog overlay itself, drawn on every tile at every moment. Edge-triggered through `LastSightState`
  exactly as `DescribeIndoor` is (reset with every plant, the planted tile adopted silently), and
  fired from `SetCursorTile` rather than beside each `DescribeIndoor` call, because every landing —
  step, floor key, floor poll, plant, recenter, scanner marker — passes through that one function.
  The tone goes through the game's own sound manager (`docs/build.md`, sounds), not the screen
  reader, and precedes the step's words because the call returns before native speaks
  `NativeResult`. Heard working 2026-09-17, three fog crossings on a live mission, no audio
  error logged; the package's cue table, cue and wave all read back as written (`just dev-props
  "instance=XComTacticalSoundManager" SoundCue` is the check).
- **Wall tones are a looping BED on a spawned actor (`X2AccessWallTones`), re-aimed on every
  landing.** WH40KRTAccess's WallTones (WrathAccess's WallToneSystem) on this engine's grid: four
  looping voices, one per compass cardinal, whose volumes rise on the quadratic proximity curve
  as a wall nears in that direction from the cursor tile — east baked hard right, west hard left,
  north and south centred and told apart by timbre (the pan lives in the samples because the
  listener is the camera, `docs/build.md`, sounds). The aid sonifies the SHAPE of the space, the
  thing a sighted player takes in at a glance and no per-object readout can carry. A wall is the
  rooms recon's `B--V`: `IsAdjacentTileBlocked` forward AND not `IsAdjacentTileVisible` across the
  same face, walked tile by tile at the cursor's height out to `RangeTiles`; the map's edge and
  the level volume's edge count as boundaries, both drawn. Cover is vaultable or seen over and is
  therefore not a wall — the cover ring already names it. Fog gate as RT's: a never-seen tile is
  silent, the ray itself is not fog-checked. Modes are Off / When moving (default: the bed rises
  on a landing and glides out `HoldSeconds` later, on the actor's own timer) / Continuous, config
  only for now — no hotkey (player decision 2026-09-17), the controls are in `Config/XComGame.ini`.
  Why an actor: the voices are `AudioComponent`s, object references a CDO may not hold; the actor
  is spawned into the tactical world on the first landing, dies with the map, and is found again
  by `DynamicActors` iteration, never by a stored reference. Volumes move only through
  `AdjustVolume(GlideSeconds, target)`, and the voices are never stopped between landings — a
  stopped loop restarts from its head, a silent one resumes where it was. ⛔ **"Silent" is
  `SILENT` (0.002), never zero.** The device's update drops a wave instance whose final volume
  is at or under 0.0001 out of the active set and stops its source, and stops any source unheard
  for 60 ticks, so a voice glided to zero is a stopped voice and the next `AdjustVolume` aims at
  nothing — the first run heard one landing, then nothing on any map (`docs/engine-notes.md`,
  audio). Two more facts from the same decompile: `AdjustVolume` has no playing guard and only
  writes the interpolated level back when a glide lands, so the idle level is set as
  `CurrAdjustVolumeTargetVolume` BEFORE `Play()` (an `AdjustVolume(0, x)` in the same frame as
  the first glide leaves the current level at its default 1.0 and that landing bursts from full
  scale); and `Play()` on a stopped component resets its clock without touching the adjust
  state, which is why `Revive` can restart a voice the device stopped anyway (the 16×-duration
  cutoff, a flush) and glide on from where it last landed.
- **Inside / outside is the game's floor-volume rule, asked synchronously
  (`X2AccessTileReader.DescribeIndoor`).** `XComPawnIndoorOutdoorInfo` defines inside as
  "in an `XComFloorVolume`" — a building volume alone is outside, a nested
  `m_bIsInternalBuilding` wins over the outer one, the floor is the HIGHEST `FloorNumber`
  touched, and a volume with `IsInside` off (a tunnel plot's whole-level volume) is not a
  building. ⛔ Do not read the cursor pawn's own `IndoorInfo` for this: it is fed by Touch
  events that land the frame AFTER `CursorSetLocation`, so it answers about the previous
  tile. The reader walks `XComFloorVolume` with `EncompassesPoint` at the tile just adopted
  and takes the building from the volume's `CachedBuildingVolume`, the tracker's own
  resolution. ⛔ Not building-first: a modular block's `XComModularBuildingVolume` pieces
  still list floor volumes of their own, with no cached building and zero band heights, and
  a walk that started from buildings could credit one of them instead of the merged
  `XComMegaBuildingVolume` the game credits. Spoken on the crossing only ("Inside building,
  floor 2 of 3" / "Building floor 3 of 3" / "On roof" / "Outside"), forced on the verbose
  re-read; the count is the building's own `Floors.Length`, a different number from the
  plot-wide band count `FloorWord` speaks, hence the different phrasing.
  - **The roof is a band recognized by its HEIGHT.** A mega block's last `Floors` band is
    the space over its roof — measured live as 784..2976 over three 256-unit storeys — with
    nothing on its volumes to tell it from a storey, while a plain two-storey shop has no
    such band. The game's `IsOnRoof` ("top band of a multi-band building") would call the
    shop's upper storey a roof, so the reader instead drops a last band taller than two
    storeys (`fExtent > XCom3DCursor.CursorFloorHeight`) from the count and speaks it as
    "On roof".
- ⛔ **Per-edge cover: `COVER_<dir>` is PRESENCE, `COVER_<dir>Low` is a HEIGHT
  MODIFIER on it — never an alternative.** A low-cover edge carries BOTH bits, so
  `if (dir) … else if (dirLow)` reports every covered edge as high. The engine's
  own reading is `X2AbilityToHitCalc_StandardAim.GetCoverTypeForTile` (`:934`):
  *"if the cover flags in the specified direction don't have the low cover flag,
  then it is high cover"*. Test presence first, height second. Bit order is N/S/E/W
  (`0x01/0x02/0x04/0x08`, low bits four up).
- **Mouse gate** (`src/mouse_gate.cpp`): with the mouse active,
  `XComTacticalInput.Mouse_CheckForPathing` re-pushes the cached pick point through
  `CursorSetLocation` EVERY FRAME (last line of the input state's
  `PostProcessCheckGameLogic`), reverting a keyboard push one frame later. There is
  **one such function per input state** and the gate holds three:
  `Mouse_CheckForPathing` for world mode, `Mouse_CheckForFreeAim` for each of
  `UsingTargetingMethod` and `UsingSecondaryTargetingMethod` (measured at exactly
  **60.0 pushes/second**). They never contend, so each surface releases exactly what
  it took. ⚠ The free-aim pair is **state-scoped**, invisible to
  `FindScriptFunction`'s class-chain walk; `GameUI::FindStateFunction` walks the
  `UState`'s own `Children`. `TileReader::Call` engages the gate on first use per mission. While gated, the
  UFunction is flipped to `FUNC_Native`
  with `Func` (+0xF0) → a thunk that consumes `EX_EndFunctionParms` and returns —
  script→script dispatch goes through exactly those two fields — and both are
  restored when targeting raises, a recipe attaches, tactical ends, or the ASI
  unloads. `sdk_layout_checks.cpp` pins `FunctionFlags` (0xD0); the generated header
  mis-declares it uint64, so only that field can be `offsetof`'d. Clicks, free-aim
  and camera scroll are untouched; a click while gated commits to the pawn's preview
  of OUR tile. Preferred over `UIMovie.DeactivateMouse()`, which also stops the
  re-push but changes which HUD the game builds and stops
  `UITacticalHUD_Ability.UpdateData` refreshing `m_strAntennaText`.
- ⛔ **The evac zone is a TILE fact as well as a scanner entry, and the two must be
  asked of the same box.** `X2AccessHelper.DescribeEvacZone` is that one oracle
  (added 2026-09-04 on report: the scanner's ring could say where the zone was and
  no key could say whether you were standing in it). Both halves are the game's:
  `XComGameState_EvacZone.GetEvacMinMax` is the extent, and it is the one
  `IsUnitInEvacZone` tests — which is what makes the tile line and the scanner's
  "N of M inside" count incapable of disagreeing. ⚠ **The box is not the painted
  square**: `GetEvacMinMax2D` gives the footprint, and `GetEvacMinMax` then widens Z
  by one level in EACH direction, so the tile a level below the paint is inside the
  zone and must say so. `ValidateEvacTile` (floor + vertical clearance, with
  `bMissionPlaced` as its `IgnoreVerticalClearance` — the same argument
  `IsUnitInEvacZone` passes) is a QUALIFIER, not a gate: the paint covers invalid
  tiles too, so a silent tile inside a zone the player knows is there would read as
  *the zone ends here*, which is the truncation failure mode — indistinguishable
  from the truth and therefore never reported.
- **Sight and flank ride every step; the target preview rides Alt (or the game's option).**
  The game draws three things the instant the path destination changes
  (`XComPathingPawn.UpdatePathTileData`, `:1028-1038`): the EYE on every visible enemy's flag
  that has line of sight to the destination (`UIUnitFlagManager.RealizePreviewEndOfMoveLOS`;
  `ShowLOSPreview` ships true, no key held), the destination's cover shield in `FlankedColor`
  when a visible enemy flanks it (`XComActionIconManager`, native), and — only while Left Alt
  is held or with the Gameplay option "Show Target Preview" (`m_bTargetPreviewAlwaysOn`) on —
  the `UITacticalHUD_EnemyPreview` strip: which enemies could be shot from there, which only
  via squadsight, which would be flanked. The readout mirrors all three on the same terms:
  "Seen by N, flanked" is a clause of every step and of the move preview (`DescribeSight`),
  and the strip (`DescribeTargetPreview`) speaks on Alt (`CursorTargetPreview`) or, with the
  option on, at the end of every step. Oracles: the enemy set is `GetAllEnemiesForLocation`
  kept to `bVisibleBasic`, to `IsEnemyUnit` (the location query returns everyone off the
  soldier's team, civilians included) and to `CanXComSquadSeeTarget` (the gate the detection
  readout and the scanner's ring already use); the flank test is
  `XGUnitNativeBase.DoesFlankCover` over the tile's cover point, handed the FLANKER's position
  (what the AI runs, `XGAIBehavior.uc:5060`; the shield's colouring is native and unreadable);
  the strip is `UpdatePreviewTargets` reproduced clause for clause, using the panel's OWN
  `StandardAbilityTarget`, filter and localized `Label`, and `TargetCover == CT_None` is
  "flanked" there because that is the game's marker (a unit in the open included, as in the
  to-hit calc). ⚠ Both answer about the CURSOR tile, where the game answers about the pathing
  pawn's clamped destination: the two agree wherever the tile is reachable, and on an
  out-of-range tile ours describes the tile the player just heard rather than one they did
  not. Before the cursor is planted, Alt answers about the pawn's destination — literally the
  strip — and plants nothing. ⛔ **Alt is a PASS-THROUGH claim, never a swallow**
  (`input-map.md`): `LeftAlt` is bound in the tactical section, `Left_Alt_Key_Press` is how the
  strip rises, and `F4`'s `bIgnoreAlt` is what keeps Alt+F4 off the game action — hiding the
  modifier from the engine would break all three. Never run live (2026-09-04); the checks are
  in `next-steps.md`.
- ⚠ `XComPathingPawn.Tick` hides everything while `UIReplay` is on the stack — and
  in some sessions it is, permanently.

## Object scanner

The RT `Scanner.cs` port, a PULL surface next to the tile cursor's push. THIRTEEN
categories: `markers` (tutorial required-action marker + objective-carrying objects
+ the evac zone — one group, because a waypoint, an objective glint and a
destination are the same thing to a player working out where to go),
`units.enemies`, `units.squad`, `ground.loot`, `ground.reinforcements`,
`units.civilians`, `hazards`, `objects.interact`, `objects.towers`,
`world.connectors`, `ground.cover`, `world.unexplored`, `world.exits`. Each keeps its OWN
remembered position; the
`CAT_*` ids are the one thing the two halves share as a NUMBER rather than a name.
The ring is what makes a further category cost one `case` label instead of a key.

Keys, all gated on `TileReader::IsActive()` (shared, not recomputed — both surfaces
must stand down at the same instants): `B` next marker, `.` next enemy, `,` next
squad member, `J` next cover position, `V` next exit, **`Shift`+those five for the
PREVIOUS one**,
`PageDown`/`PageUp` within the category, `Ctrl+PageDown`/`Ctrl+PageUp` next/previous
NON-EMPTY category (announcing its name), `O` re-announce, `/` plant the tile cursor
on it. The Shift chords are safe because the companion's `XComInput.ini` unbinds
`LeftShift` (its release used to cycle the soldier); nothing here claims Shift or Tab.

⛔ **Five `CAT_*` ids are mirrored in native, not three** — 0, 1, 2, **10**
(`kCatCover`) and **12** (`kCatExits`). `CAT_COVER` sits at the END of the enum rather than beside
its keyed
siblings precisely so the free-to-renumber range stays contiguous at 3..9; a new
category appended after it takes 11 and shifts nothing — which is what
`CAT_UNEXPLORED` did (2026-09-16), companion-only, native untouched. `CAT_EXITS` appended at 12
the same way and paid the two-file price only because it wanted a KEY, not because of where it
sits.

- **`world.unexplored` names the FOG FRONTIER** — never-seen tiles touching explored
  ground, flood-filled 8-way into one entry per opening ("Unexplored space, 43 tiles
  across, 2 tiles, 1 west, 1 action"). The WH40KRTAccess `FrontierModel` port
  (`src/RTAccess/Exploration/FrontierModel.cs`), and the only category that names an
  ABSENCE. It exists because of a measured failure: on `Plot_CSH_Stronghold_Short_04`
  the fog boundary ran along a wall and EVERY other ring went silent at once — no
  ramps, no cover on this floor, seven doors all behind the squad — while a sighted
  player was looking at the largest, hardest-edged shape on the screen. Parity is the
  strongest in the scanner, not the weakest: the opaque fog proof is in the tile-cursor
  section above.
  - ⛔ **NOT ONE QUESTION IS ASKED OF THE GROUND.** No `IsFloorTile`, no
    `CanUnitsEnterTile`, no cover, no height — a blob is defined by FOG STATE ALONE.
    RT's own `FrontierModel` requires its cells to be WALKABLE, which is safe there (its
    navmesh loads whole and the fog is an overlay) and would be a leak here: asking
    whether hidden ground is standable is reading terrain through the fog, exactly what
    `VerticalDrawn` exists to stop. The consequence is accepted — a blob may sit against
    a sealed wall and lead nowhere, which is what a sighted player cannot tell either.
    The reach clause is the one exception and it is drawn: `XComMovementGridComponent`
    carries `bTranslucentIgnoreFOW`.
  - Scope is ONE Z LEVEL, the origin's, and **map-wide in X and Y** — the only category
    that is. A fog edge is a large-scale fact (the one on that map ran forty tiles) and a
    12-tile window like the connectors' would have cut it into fragments and named none
    usefully. The empty text names the level scope, as the connectors' and cover's name
    theirs.
  - Key is `KEY_FRONTIER` minus the LOWEST linear tile index in the blob, spoken tile is
    the member NEAREST the origin — the two different answers `AddCover` spells out.
    Extent is the bounding box (`AddHazards`' measure for the same kind of shape), not the
    cell count, which means nothing a player can picture once a blob wraps around them.

- **`world.exits` is the ROOMS port shipping WITHOUT its rooms** (2026-09-16, `V` / `Shift+V`;
  recon `rooms-recon.md` §4 and §9). Doors, ramps and ladders, nearest first — RT's `V` and RT's
  verb (`RoomModel.CycleExit`), minus the partition it stands on there. That is deliberate and not
  a stub: §3 forbids shipping a segmentation predicate until it has been rendered over three maps
  (§8.6, unanswered), and an exit needs no partition — a door is a door whether or not we know
  which two spaces it joins. The missing partition costs exactly one clause, "leads to \<room\>",
  and its most useful case is already answered from the other end by `world.unexplored`.
  - ⚠ **It RE-LISTS `world.connectors` and `objects.interact` on purpose, and that is allowed
    here because the QUESTION differs** — "how do I change floor" and "how do I get out of here"
    are not the same question, and a way out that lives only in a ring you must STEP to is one you
    find by accident or never. Same reasoning as `AddEvac` riding `markers`. The ⛔ one-key-one-
    entry rule is about a SINGLE ring holding a key twice (`InsertByDistance`), never about two
    rings naming one thing.
  - ⛔ **The two rings share CONTRIBUTORS, not just wording.** Ramps and ladders come from
    `AddConnectors` verbatim rather than a second sweep, and a door keeps the state `ObjectID`
    `objects.interact` keys it by — so the same door is the same ENTRY in both rings. This is the
    shared-oracle rule turned on our own two answers.
  - ⛔ **Only `XComInteractiveLevelActor` thresholds are listed; a plain `XComDestructibleActor`
    is DROPPED, and the reason is WORDS.** A destructible carries no localized name of any kind,
    and script cannot ask what TRAVERSAL an arbitrary edge would take (`MaybeModifyTraversalForWindow`
    is cpptext, `XComWorldData.uc:510`; `TraversalStarts` names neighbours without naming their
    kind). So a window, a fence and a parked car are indistinguishable and the only honest label
    left would be one bare English word on all three. The count dropped goes to the log, not to
    speech.
    The consequence is in the empty text — *"No doors, ramps or ladders nearby"* — because this
    ring lists the exits it can NAME, and **a smashable wall is a way out that is knowingly not in
    it**.
  - The edge query is `GetPathObjectFromTraversalToNeighbor`, which is the thing RT never had: it
    answers per EDGE, so a doorway with a door in it simply IS that door — no proximity guess, no
    double-listing, and **a closed door is still an edge** (RT's exit cycle missed doors entirely
    because a closed one cut its grid at build time).
  - Sweep is `+X`/`+Y` edges only, at the origin's Z, radius 12. Every edge is shared by two
    tiles, so asking once per edge halves the sweep AND makes double-listing impossible by
    construction rather than by a dedup pass.

- **NO REGISTRY.** RT needs a ticked `WorldModel` because Unity entities are mutable
  references; XCOM's `ObjectID` is a stable int over a versioned history, so the list
  is rebuilt per keypress and only the selection persists (an int key plus its tile),
  keyed by ID so a completed objective cannot silently shift what is selected. The
  ring cursor lives on the CDO, NOT in native: stepping it means knowing which
  categories are EMPTY, and only the model can answer that. `/` acts on the LAST
  ANNOUNCED entry — on what you just heard. Keys are stable per TILE (a linear index,
  not packed decimal, which would collide on a wide grid).
- ⛔ **Hazards group by TYPE, not by state object** — fire SPREADS, and every spread
  step writes another `XComGameState_WorldEffectTileData`, so per-object entries would
  grow a new "Fire" every turn for one fire. Each stored tile is re-checked against
  `TileContainsFire/Smoke/Poison/Acid` because a burned-out blob keeps its state
  object: the state is an INDEX, the world is the truth, and sharing that oracle with
  `X2AccessHelper.DescribeHazards` is why the ring and the tile cursor cannot
  disagree. Walk `StoredTileData` **by index**: its element type
  `WorldEffectTileDataContainer` is declared inside that class and referenced NOWHERE
  else in the SDK, so cross-class scoping of the name is untested ground.
- A Codex's Psionic Bomb is a delayed ability, not a world-effect hazard tile. The live
  stage-one shooter effect retains its target location; stage two's multi-target style
  supplies the blast tiles. The tile reader and hazards ring use that same query, and
  only currently seen tiles are spoken. The name comes from `PsiBombStage1`'s localized
  `LocFriendlyName`.
- `IsCivilian()` takes precedence over the friendly/enemy split on a visible tile.
  A neutral civilian has the rescue circle when `RescueRingRadius > 0`, or on a
  `Terror` mission, where `X2UnitRadiusManager` draws the same circle dynamically
  without writing that radius. The tile reader and civilian ring distinguish that
  person from an ordinary civilian.
- ⛔ **`ground.evac` speaks the origin CLAMPED into the zone bounds**, not
  `CenterLocation` — a rectangle's centre gives the wrong distance, the wrong bearing
  and a plant you may not be able to stand on.
- ⛔ **`objects.towers` gates on ANY XCom unit concealed**, not the player state's
  `bSquadIsConcealed`: a lone Phantom ranger ahead of a revealed squad is when a
  detection radius matters most. Its empty text names the GATE ("Towers shown only
  while concealed") rather than claiming the map has none.
- **The Lost ride the ENEMY ring**, as ONE entry (label "Lost swarm", count and nearest
  tile) however many there are. They are enemies, so the enemy key is where a player
  looks for them, and a category empty on most missions costs a ring step on all of
  them; the summary form is what makes sharing safe, since a swarm is 10-20 bodies
  with one shared name and a per-unit ring would bury the ADVENT pod standing behind
  them.
- **Enemies carry a sight clause**: the ring stays on squad visibility (what the unit
  FLAGS show), but `GetAllVisibleEnemyTargetsForUnit` +
  `GetAllSquadsightEnemiesForUnit` — the two lists `UITacticalHUD_Enemies` builds —
  decide between "via squadsight", "spotted by the squad" and silence, so the first
  unshootable target does not read as a bug in us. They reuse
  `X2AccessTacticalReader.DescribeTargetCover`/`DescribeTargetHealth` verbatim, so
  scanner and shot readout can never disagree about flanking. Ceiling on labels:
  `GetName(eNameType_Full)` gives a non-soldier its CHARACTER TEMPLATE, so every ADVENT
  trooper reads alike — parity, not a gap, because the game gives a sighted player no
  per-enemy identifier either.
- **`world.connectors` is SWEPT rather than enumerated.** The engine offers two handles
  on Z movement and only one is a list: `CollectLadderLandingTiles` is global (the same
  list `XGAIPlayer_Civilian` routes panicking civilians along, and both landings of one
  ladder are separate entries — "1 up" and "2 down" at one bearing IS the shape of a
  ladder), while ramps have only the per-tile `IsRampTile` predicate, so they come out
  of a bounded box (±12 tiles, ±1 Z). The bound is the right SCOPE, not just a budget —
  a ramp 40 tiles off is not the one you would take — which is why this category's empty
  text names the SCOPE where every other names the map. Clustering happens LAST, over
  already-distance-sorted candidates, so the survivor is the nearest end of the
  staircase. It exists because every other readout ends in "2 up" on a multi-storey map,
  and a blind player cannot act on that without knowing where the stairs are.
- **`ground.cover` answers where to STAND, not what is there** — the RT `CoverModel.cs`
  port, and the second swept category. A cover position is a walkable tile whose edges
  give cover, named by the side(s) it covers ("Full cover north"), so `/` plants the
  cursor on the tile you would occupy and Backspace walks there: *"the crate is 4 tiles
  north-east"* never told a blind player which side of it to stand on. Unlike RT there
  is no sweep to write — `XComWorldData.GetCoverPoints(InPoint, Radius, Height, out,
  bSortedByDist)` is native and hands back each point's TILE COORDINATES and cover flags
  already sorted (`Radius` is XY world units, `Height` is Z;
  `XComTacticalCheatManager.TestCoverPoints` passes `5 * 96` and `5 * 64` for five tiles
  of each). ±12 tiles, ±1 Z.
- ⛔ **Cluster on an IDENTICAL signature, and cluster only what TOUCHES.** A ten-tile
  sandbag line is one entry ("Full cover north, 10 tiles wide") and the four faces of a
  car are four, because four faces are four different places to stand — that is the
  choice the player is making. Identical, not "compatible": the cell where a run turns
  half into full, or a corner covered on two sides, is a genuinely different position.
  This is a flood fill over a `(2R+1)²` grid, **not** the connectors' greedy
  distance-cluster, and the difference is load-bearing — two separate crates three tiles
  apart with the same signature would merge under a distance rule, and a merged-away
  crate reads as *there is no cover there*, the truncation failure mode again.
- ⛔ **The cluster's KEY is its lowest linear tile index; its spoken TILE is the member
  nearest the origin.** They must be different reads. The key has to be
  origin-independent or the remembered selection dies every time the scan window slides
  with the cursor; the tile has to be the nearest member or `/` plants somewhere you
  would not have walked. Lowest-index is unique per cluster because clusters are
  disjoint.
- **`CoverSignature` is normalised, and the normalisation is the point.** A low bit with
  no presence bit under it is meaningless (the presence-first rule) and would split a run
  that reads identically; PEEK bits (`0x0100`-`0x8000`) are per-tile firing geometry that
  differs between two tiles of one crate face and would shatter every run into single
  cells. ⚠ `COVER_Diagonal` rides the signature but NOT the words — it means the four
  edges are rotated 45° (local N = world NE), so a diagonal tile must never merge with a
  square one, but **no script in all of `XComGame` reads that flag** (the game's own
  `GetCoverTypeForTile` takes N/S/E/W at face value), so speaking eight compass points
  here would put this ring at odds with the tile cursor and the shot readout, which speak
  four. Unverified gap, deliberately not fixed on one surface.
- **The fog latch is the WHOLE parity gate here — and the ring MIRRORS a shipped overlay.**
  RT gates its cover cycle on the game's own cover overlay being TOGGLED UP; XCOM's is
  always on, so there is no mode to mirror, but the overlay itself is real:
  `XComActionIconManager` pools in-world cover icons around the path destination
  (`UpdateCoverIcons`), drawing `UI_3D.Cover.CoverFull` or `CoverHalf` per covered edge.
  ✅ **The two cover states are drawn by the game.** The mesh names are `CoverFull` and
  `CoverHalf`, but the player-facing shot breakdown names them through
  `XLocalizedData.TargetHighCover` / `TargetLowCover` ("Высокое укрытие" /
  "Низкое укрытие" in `XComGame.rus`). Tactical target readouts use those fields;
  our directional cover phrases use the same Russian nouns. ⚠ Its scope is
  `CoverIconTileRange=2` (`XComGameCore.ini:1221`), i.e. two tiles around the cursor,
  against our twelve — which is a RANGE difference, not a parity breach, and the rule for
  telling those apart is under **Sighted parity** in `CLAUDE.md`: a sighted player sweeps
  the mouse and reads the whole map's cover in a second, and sees the sandbags themselves
  with no overlay at all. So `TileSeen(bLatch=true)`, the same latch the cursor applies to
  the same fact, is the entire rule, and a ring over cover leaks nothing the arrows could
  not reach one tile at a time. Standability is the game's
  own pair from `XGAIBehavior:3439` (`IsFloorTileAndValidDestination` +
  `CanUnitsEnterTile`), with the one carve-out every cover reader makes: **the active
  soldier's own tile stays listable**, because you can already be in the best cover on the
  map. Out-of-range spots stay in with a `ReachWords` note ("out of range") rather than
  being filtered — planning a position you cannot reach yet is the point of the feature.
  The sweep is one Z band, so the empty text names the FLOOR as well as the scope ("No
  cover nearby on this floor").
- ⛔ **NO CYCLE KEY MOVES THE CURSOR — that is `/`'s job alone**, and cover is where the
  temptation to break it is strongest, since a cover spot is somewhere you STAND. It must
  not: `ScanOrigin()` returns the planted cursor tile when there is one, so a key that
  planted implicitly would re-measure the next press from the tile it just landed on. The
  ring would re-rank around its own last answer, the nearest entry would always be the
  cell you are already on, and repeated presses would wander outward with no way back
  instead of walking a stable list. RT refuses the same thing for the same reason
  (`CycleCover`'s own comment: moving the cursor "would silently flip `ScanFrom` for every
  later scanner action"). Any future key that DOES plant needs a frozen anchor first, not
  an exception to this rule.
- ⛔ **RT's FIRING gear is REFUSED here, not queued.** In RT, `J` changes gear when an enemy
  is the review selection: the question becomes *where can I stand and still shoot THAT*,
  ranked by hit chance (`FiringPositions.cs`). XCOM must not grow it, and the reason is
  parity, not effort. **Nothing in this game shows a player which tiles can shoot a given
  enemy.** Per-tile shot information exists only for the tile the cursor is ON — the shot
  HUD's chance, and Left Alt's target preview — and both are things we already read. The
  one thing in the game that DOES sweep candidate tiles and score them for a shot is
  `XGAIBehavior` (`:1708`, `:10231`, over this same `GetCoverPoints`), i.e. the ENEMY's
  planner. Shipping it as an aid would hand a blind player the AI's view of the board and
  call it accessibility. The distinction against the cover ring above is exact and is the
  test to apply next time: cover on a tile is a fact the game DRAWS (`CoverFull` mesh, two
  tiles out) and we deliver at twelve; a ranked firing solution is a fact the game draws
  NOWHERE, at any range, for anyone.
- **Both countdown categories gate on the game's OWN rule for drawing the mark**: loot
  uses `LootExpirationTurnsRemaining > 0 && HasAvailableLoot()`, verbatim from
  `XComGameState_LootDrop.BuildVisualizationForLootTicked`'s `SetVisible`, so an entry
  exists exactly when a waypoint is drawn; reinforcements use `Countdown > 0` (what
  `UITacticalHUD_Countdown.RefreshCounter` hides on) AND a `SpawnVisualizationType` that
  actually builds the flare — `BuildVisualizationForSpawnerCreation` is SKIPPED for `''`,
  `'TheLostSwarm'`, `'ChosenSpecialNoReveal'` and `'ChosenSpecialTopDownReveal'`. Speaking
  the reinforcement LOCATION (which the HUD counter never shows) is covered because that
  visualization leaves a persistent effect and a revealed area (`bStopEffect = false`,
  `bDestroyViewer = false`). Words come from the game's own
  `m_strReinforcementsTitle`/`m_strReinforcementsBody`, and both countdowns speak the
  NUMBER, because a number is what the sighted counter shows. Loot is labelled from
  `LastLootOwnerName`, never `GetLootingName()` — the latter re-enters the source object
  and, for an interactive-object source, walks into the same unguarded `GetVisualizer()`
  deref `InteractiveLabel` sidesteps.
- **Reachability is per-entry** (`bIsDestination`): spoken for markers and objectives,
  which you walk to; suppressed for enemies and the tutorial's attack marker, where "out
  of range" would read as "you cannot do this" about a shootable target.
- **The tutorial contributor** reads `XComTutorialMgr.NextPlayerAbilityContext`, NOT the
  six `StaticMeshComponent`s (routinely hidden at the origin while an action is still
  required), gated on `bIsTutorialAllowingControls`, and honours the parity guard:
  `AbilityTemplateName` / `SourceObject` / `PrimaryTarget` / `TargetLocations` /
  `MovementPaths[0]` only, NEVER `ResultContext`, which carries the recorded hit/miss of
  a scripted shot the player has not fired. Objective markers are deliberately NOT
  fog-gated — the game's own rule (`UIUnitFlag`/`UISimpleFlag` special-case an
  `ObjectiveInfo` component to keep the flag visible). Bearings come from
  `X2AccessTileReader.OffsetWords`, static and shared, so there is exactly ONE calibration
  of `NORTH_DX`/`EAST_DX`.
- ⛔ **`FindComponentObject` answers in the COMPONENT'S frame, never the present — re-fetch the
  owner by ObjectID before reading anything off it.** Supply Extraction (2026-09-16): the ring kept
  all twelve crates after the counter said twelve picked up, and two rewrites of a "has the crate
  left the screen" test changed nothing. Measured through the harness (`obj dump` on every crate
  version, ordered by the frame's `HistoryIndex`, with `True`/`False` printing as the localized
  `Да`/`Нет`): each crate has a spawn version (`InteractionCount` 0), three marking versions
  (count 1), and one pickup version with **`bRemovedFromPlay` TRUE** — the Skyranger pickup, the
  ADVENT transport and the mission-end sweep all set it, whatever the cooked Kismet's links
  suggested. The ring never saw it because every `XComGameState_ObjectiveInfo` still lives in the
  frame that spawned its crate (indices 102039..102298), and the crate the component walk returned
  was that frame's version, pristine forever. The old `bRemovedFromPlay` test had been correct and
  dead since the day it was written; the visualizer test that followed it was diagnosing the wrong
  object. The tell was *"Objective: object"*: `SyncVisualizer` clears the visualizer on the same
  flag, so a removed crate has no actor to name. A dead objective VIP was the same bug on the unit
  branch. ⛔ A theory about what a Kismet path does to a state is not a measurement of the state;
  the harness could have answered this in one pass on the first night.
  **Marked-ness is read off the crate's COLOUR, not the count**: marking runs
  `SeqAct_SetMaterialGameState` with `SupplyExtractionCrate_XCOM` / `_ADVENT` (plus `_Premium_`
  variants), and since `XComGameState_MaterialSwaps.MaterialSwaps` is `private`, the mesh's own
  `GetMaterial(i)` names are asked — "marked" / "marked by ADVENT" — which is the material on
  screen anyway. ADVENT marks crates too and XCOM can convert one, so the two words are parity, not
  a hint. ⚠ The fallback words for a marking that changed no material ("marked" / "used") now see
  the crate's real `InteractionCount`; before the re-fetch they always saw 0.
- ⛔ **An objective can leave play by DESTRUCTION, and destruction sets no flag — the only state
  fact is `Health == 0`.** Chosen Avenger Defense (2026-09-17): the four turrets and the Mobile
  Power Generator are `XComInteractiveLevelActor`s that `SeqAct_MakeObjective` flags (Health 18
  and 48 at turn 1, read through Frida off every live `XComGameState_ObjectiveInfo` and the LATEST
  version of its owner), and the cooked Kismet holds no `SeqAct_RemoveInteractiveObjectFromLevel`
  and no `SeqAct_SetObjectiveShaderEnabled`, so a shot-out turret keeps its component, keeps
  `bRemovedFromPlay` false and keeps a wreck on the tile — the ring read six of six after Central
  had called one turret down. `XComGameState_Destructible.ApplyDamage` fires `ObjectDestroyed`
  on exactly the `OldHealth > 0 && Health == 0` edge, so that is the test, on the interactive AND
  the plain-destructible branch. ⚠ `-1` means indestructible OR not yet initialised (the generic
  objective marker on this map reads -1 for the whole mission), so the test is equality with 0,
  never `<= 0`. Two things this map also teaches. **The turrets and the generator are NAMED
  *"Chosen Sarcophagus"* in every language**: `FriendlyDisplayName` is a `localized` var resolved
  at load from the MAP PACKAGE's loc file, and Firaxis' `sm_ChosenBulwark_01/02` (two turret
  actors each, archetype `ARC_IA_ChosenAD_BulwarkACV`), `md_ChosenArtillery_01` and `SiegeCannon`
  (the generator, `ARC_IA_ChosenAD_ArtilleryGenerator`, actor `_5` in both variants) all carry
  the stronghold's string — which is why five ring entries read alike, and why the shot HUD shows
  a sighted player the same word. Nothing in the state gives a localized word instead; the
  ARCHETYPE (`XComInteractiveLevelActor.ObjectArchetype.Name`) is what tells the two apart, and
  `InteractiveLabel` asks it BEFORE the display name. ⛔ **A mod loc file under a MAP PACKAGE's
  name does not override that package's existing key.** Tried 2026-09-17 — ten files,
  `sm_ChosenBulwark_01.int`/`.rus` and the rest, UTF-16 in staging, deployed, game restarted —
  and the ring still said sarcophagus; and of forty workshop mods shipping an `XComGame.int`,
  not one redefines a key vanilla already has, so there was never evidence that overriding
  works, only that ADDING keys does. So the turret takes the game's own word, the `AdvTurretM1`
  character template's `strCharacterName` (*ADVENT Turret* / *Турель "Адвента"*, every language),
  and the generator — which the game names nowhere outside the objective sentence — takes the
  companion's own `X2AccessVocab.ChosenADGeneratorName`, shipped INT and RUS
  (`Localization/X2AccessCompanion.int`/`.rus`; the file contract is `docs/localization.md`),
  English elsewhere — the
  degradation `X2AccessDropshipReader` warns about, accepted because the alternative is a wrong
  word in every language. And the generator's death flips the surviving turrets to
  `OnlyAllowTargetWithEnemiesInTheBlastRadius` (the map's five
  `SeqAct_ToggleInteractiveActorBlastTargeting` ops) — whether a turret DISABLED that way still
  wears an objective flag is unmeasured (`next-steps.md`), so the ring does not gate on it.
- ⛔ **Every ring that walks `IterateByClassType(XComGameState_Unit)` must refuse
  `bIsCosmetic`.** The squad ring did not, and a Specialist's GREMLIN — a live `eTeam_XCom`
  unit standing on its owner's own tile — entered it as a NAMELESS entry: *"1 of 1 health,
  0 tiles, 2 of 7"*, a seventh squad member on a six-soldier squad, right behind its owner
  at `1 of 7`. The tell is the shape, not the words: an entry with an empty `Label`, zero
  distance from the previous one, and a maximum health of 1. The parity source is the flag
  manager's own list of who gets a flag (`UIUnitFlagManager.CheckForMissingFlags`: HP > 0,
  **not cosmetic**, not civilian, not `bRemovedFromPlay`) — the three GREMLIN templates set
  `bIsCosmetic` *and* `bDisplayUIUnitFlag=false`, so nothing about the drone is on screen to
  read. The tile reader and the shot readout had carried this guard from the start
  (`X2AccessTileReader.uc:536`, `X2AccessTacticalReader.uc:580`); the enemy ring needs it too,
  because a GREMLIN takes its OWNER's team and a mind-controlled or hacked Specialist puts the
  same phantom on the other key.
- ⛔ **PANIC AND STUN ARE NOT IN `GetUISummary_UnitEffectsByCategory`, and nothing may source
  them from it.** Neither `X2StatusEffects.CreateStunnedStatusEffect` nor any of the four
  `X2Effect_Panicked` constructors (`Panicked`, `Berserk`, `Obsessed`, `Shattered`) calls
  `SetDisplayInfo`, so both carry `bDisplayInUI` false and an **empty `FriendlyName`** and can
  never reach either category. The game does not read them from there either: the flag draws its
  stun icon straight off `StunnedActionPoints` (`GetUISummary_UnitStatusIcons`). Their words do
  exist and are localized — `X2StatusEffects.StunnedPerActionFriendlyName`
  (`"Stunned: <Ability:STUNNEDACTIONPOINTS>"`, expanded against the unit exactly as
  `GetStunnedFlyoverText` does, with `RoboticStunnedPerActionFriendlyName` — "Shutdown" — for a
  SPARK or a hacked MEC) and `UIEventNoticesTactical.PanickedTitle`.
  **`X2AccessHelper.UnlistedPenaltyWords` is the one implementation**, and it is one because the
  gap was fixed in the squad ring first and immediately put two of our surfaces at odds about the
  same soldier — the `,` ring saying "Оглушение: 2" while the `;` sheet said nothing. It is now
  led into the PENALTY category inside `X2AccessUnitReader.EffectNames`, so the soldier sheet and
  the Targets stop inherit it and a stunned ADVENT trooper reads as stunned before you spend the
  shot. ⚠ Bleeding out and unconscious ARE in the penalty list (both get `SetDisplayInfo`), so a
  readout that also names them by hand for ordering — the squad ring does — must skip the
  duplicate rather than say it twice.

## Targeting / shot HUD

Watches only `m_isMenuRaised` — the flag `GameUI::ReadTacticalGate` already reports —
and calls `PollTargeting()` on the companion CDO every 150 ms while the HUD is usable;
the companion reads `m_iCurrentIndex` and the targeting method's target index and answers
only when the selection moved. **That is why this layer adds no offsets and no
`static_assert`s**: change detection sits next to the data, so a regenerated SDK cannot
silently break it. Resolution mirrors `UITacticalHUD_InfoPanel.Update` and
`UITacticalHUD_ShotWings.RefreshData` — the targeting method owns the target INDEX, into
the SELECTED ACTION's own target list, and `LookupShotBreakdown` gets an EXPLICIT
`UseAbility` because the empty-ability branch `RedScreen`s. Claims, live only while
raised and neither steering: `Delete` re-announces (never contends with the tile cursor's,
which stands down exactly then) and `H` reads the per-modifier breakdown — the spoken form
of the shot wings a mouse player opens by clicking.

The target line carries its **position in the cycle** (`Sectoid, 1 of 3, hit 73 percent…`)
beside the ability line's own index. Both are the same fact in the same words: the length of
a ring the player is walking with a key, not a survey of the map. The target ring is
`AvailableTargets` and the index is `X2TargetingMethod.GetTargetIndex()` — literally what
`GetTargetedObjectID()` reads and what `NextTarget()` advances — so it cannot drift from the
reticle. Spoken at `1 of 1` too: "there is nothing else to Tab to" is the same answer a
sighted player gets free from the reticles.

**A listed target can carry a blast, and then the target line carries it too.** Two target-list
abilities draw an area without being free-aimed, and the game draws both the same way — the
tiles on the ground and a mark on every unit standing in them: a standard shot whose reticle is
on an EXPLOSIVE OBJECT (`X2TargetingMethod_OverTheShoulder.DirectSetTarget:200` asks the actor
for its own `GetRadialDamageTiles`, the box its RadialDamage destruction event hurts), and the
Reaper's Remote Start (`X2TargetingMethod_RemoteStart.DirectSetTarget:104` asks the target
style's `GetValidTilesForActor`, the multiplied radius). A unit wearing a Homing Mine is the
third case in the same OverTheShoulder branch. `DescribeTargetBlast` re-asks the SAME functions
the method drew from and lists the units on those tiles (`GetUnitsOnTile` per tile, once each,
the primary target left out because the line already named it) through the free-aim clause's
own `DescribeVictims` — so a squadmate reads identically inside a grenade's ring and beside a
car about to be shot, friendly-fire warning included. Silent for any target that draws no area;
`Blast: nothing` once one is drawn and empty. `Delete` adds the area's footprint (`5 by 5
tiles`). ⚠ Two things it deliberately does not do: it does not read the method's `MarkedTargets`
cache, because what the native `MarkTargetedActors` keeps there has never been measured; and it
lists no OBJECTS in the area, because the native marks with `bUnitsOnly=false` and which objects
it picks is unmeasured too — a list of "Object, Object" over every wall fragment in the box
would say less than nothing. The damage number needs nothing new: `GetDamagePreview` already
answers with the explosion's damage for a shot that would destroy the object
(`XComGameState_Ability.uc:1244`) and with the multiplied damage for Remote Start
(`RemoteStartDamagePreview`).

## Free aim / AoE preview

The third tactical input mode. An ability whose target is a POINT (`bFreeAim` set,
`AvailableTargets` EMPTY) reads its aim point off the game's own `XCom3DCursor` — grenades
and everything deriving from their method, cones, lines, the blaster launcher, the evac
zone. **Vanilla has no keyboard aiming path at all**:
`UITacticalHUD_AbilityContainer.AbilityRequiresTargetingActivation` returns false unless a
controller is active (`:155`), so the D-pad branch is dead code, and
`SetSelectionOnInputPress` (`:405`, on every PRESS) sends a bare arrow to
`CycleAbilitySelection` — which ABANDONS the throw. Claiming the arrows displaces nothing
(ability cycling stays on `1`-`9`). Aim keys are the tile cursor's: arrows step,
`Ctrl+,`/`Ctrl+.` change floor, `Delete` re-announces (ONE claim branching in script, never
two racing for a chord), and the scanner's `/` retargets the AIM point.
`Enter`/`Space`/`Escape` stay unclaimed. Detection and words live in the companion, which
reports the mode through `NativeState`, published by `TacticalHUD::OnTick`'s existing poll
— so no offsets and no extra outbound call.

⛔ Three traps:
- **Do NOT reuse `X2AccessTileReader.SetCursorTile`** — it zeroes
  `m_fMaxChainedDistance`, which under a targeting method IS the throw-range limit
  (`X2TargetingMethod_Grenade.Init:43` sets it from `GetAbilityCursorRangeMeters`; live
  960 units = 15 m = 10 tiles). Its comment calling that safe "because our keys stand down
  while the shot HUD is raised" is exactly what free aim falsifies.
- **Do NOT trust the pushed tile as the answer** — for a thrown object the blast centre is
  `GrenadePath.GetEndPosition()`, where it comes to REST, so a bounced grenade lands
  elsewhere. That resolves a frame late, which is why `PushAim` verifies the clamp
  synchronously (reading `GetCursorFeetLocation()`, the exact inverse of the offset
  `CursorSetLocation` adds, compared as a horizontal distance with half a tile of slack —
  the clamp lands on a SPHERE and owes the tile grid nothing) while the landing divergence
  is reported by the poll.
- **Blast contents come from `Ability.GatherAdditionalAbilityTargetsForLocation` asked
  about OUR tile**, not the method's `MarkedTargets` cache, which only refreshes inside its
  own `Update()`. It is the same list `ConfirmAbility` submits, fog-gated through
  `X2AccessTacticalEvents.IsNarratable` (the native gather does not care what the squad can
  see — a real leak guard). Friendly fire is read from that list, not the method's
  `bFriendlyFireAgainstUnits`, which is one frame stale; the game's own modal runs over a
  WIDER list (`CheckForFriendlyUnit` uses the visual `GetTargetedActors`) and stays the
  authority — ours is the early warning that keeps a blind player from reaching it.

⚠ The arc resolves a frame AFTER the push, so a single divergent reading is not evidence —
a pre-jump arc still points back at the shooter. A divergence must survive two consecutive
polls before it is spoken.

## Selected unit

An automatic line when the selection CHANGES (name + health/shield/armour + action points +
how many enemies THIS soldier sees) and `;` for the whole block on demand (adds CLASS,
concealment, named buffs and debuffs).

- The enemy count is the **enemy strip's** own number (`UITacticalHUD_Enemies`, *"currently
  visible aliens to the active soldier"*), from the two lists
  `XComGameState_Unit.GetUISummary_TargetableUnits` builds it from — what the unit sees, plus
  a sniper's squadsight, which `GetAllSquadsightEnemiesForUnit` already keeps disjoint. It is
  PER-SOLDIER on purpose: the squad's answer is already on the map as unit flags, while the
  strip answers the question the player pressed Tab to ask. ⛔ Filtered to living non-cosmetic
  UNITS — the strip's raw array also carries hackable objects, and counting it whole would
  call a door an enemy.
- ⛔ **Zero is SILENT on the switch line**, and explicit ("No enemies visible") only on `;`.
  Most switches on most missions happen with nothing in sight, so a spoken zero is the clause
  heard most often and wanted least, on the one line built to be short. This is not the
  empty-answer trap: that rule is about a readout whose silence cannot be told from its
  absence, and the switch line always names the soldier, so the player hears it fire either
  way. A pressed key is the case that does owe an answer, which is what `bSpeakZero` marks.

- ⛔ **Tab is NOT claimed and must not be.** The switch is found by POLLING
  `XComTacticalController` instead, which is the better instrument: selecting a soldier
  submits no game state and triggers no event (it is a VISUALIZER change), so there is
  nothing for `X2AccessTacticalEvents` to hear and nothing in the replay's frames for
  `X2AccessReplayNarration` to walk — and a poll catches every OTHER way the selection
  moves, which a Tab claim never could: a mouse click, the start of your turn, and the game
  re-selecting when the unit you were on goes down.
- Resolves through `X2AccessTileReader.ActiveUnitState()` rather than a resolver of its own,
  so the readout and the tile cursor can never name different units. Every value is parity:
  name and class from `UITacticalHUD_SoldierInfo.SetStats`, health/shield/armour from
  `UIUnitFlag.RealizeStats` via `X2AccessTacticalReader.DescribeTargetHealth` (the SAME
  oracle the shot HUD and object scanner use), action points from `RealizeMoves`' pips via
  `NumAllActionPoints()`, overwatch from `ReserveActionPoints.Find('Overwatch')`, and effect
  names from `GetUISummary_UnitEffectsByCategory` — which gets panic, disorientation,
  burning and stun spoken with no hand-written English word for any of them.
- ⛔ **ACTION POINTS ARE A FRIENDLY-ONLY READOUT.** `UIUnitFlag.RealizeMoves` draws the pips
  inside `if( m_bIsFriendly.GetValue() )` — *"Only show moves for friendly units"* is
  Firaxis' own comment — so how many actions an ALIEN has left is a fact the game shows
  nobody. That is why this reader is scoped to the SELECTED unit rather than to whatever the
  scanner or cursor points at. Do not generalize the companion's functions to an arbitrary
  unit without splitting that clause off first.
- The team test comes BEFORE the change check and deliberately does NOT record the id:
  `ControllingUnitVisualizer` can follow a unit the player does not command, and recording
  those would make every transient follow look like a change back to your own soldier. The
  forget-what-was-selected edge is the **MISSION**, which is what `GameUI::TacticalGate` grew
  `missionActive` for (a `UITacticalHUD` anywhere on the stack, where `hudOnStack` means "and
  nothing over it"). `IsUsable()` drops on every pause menu and cinematic, so resetting there
  would re-announce the same soldier each time one closed; never resetting is wrong too,
  because object IDs live in the campaign history and a soldier keeps hers across missions.

## Objectives list

`Ctrl+X` reads the mission's objective panel — the heading, then every row in the order the
panel draws it, with completed/failed state and any timer or counter. Native end to end
(`objective_reader.cpp` + `GameUI::ReadObjectives`); no companion call.

- ⛔ **This panel is the exception to "the tactical HUD holds no text".** Every other
  `UITacticalHUD` panel is a write-only pipe into Flash, but `UIObjectiveListItem.RefreshDisplay`
  ends EVERY one of its `Set*Row` branches with `Data = NewData`, so the `ObjectiveDisplayInfo`
  the row was built from survives in the widget. Verified live 2026-08-23 on
  `LostAndAbandonedB`: three rows, real `DisplayLabel`s, one with `ShowCompleted=true`.
- That retention is also why the readout works in the **tutorial**. The objective list is driven
  by `UIObjectiveList.OnVisualizationBlockComplete`, which a replay fires exactly as a live
  mission does — so unlike anything sourced from a submitted context or the event bus, this is
  not silently dead on the mission a new blind player starts with.
- **One class, both layers.** `UITacticalHUD.m_kObjectivesControl` (0x444) in a mission and
  `UIAvengerHUD.Objectives` (0x44C) on the ship are the same `UIObjectiveList`, so the Avenger's
  objectives and its dark-events block come out of the same reader. The gate is simply "is a list
  reachable", not a tactical gate.
- **The headings are localized and genuinely populated** — `m_strObjectivesListTitle`
  ("ЗАДАЧИ"), `m_strDarkEventsListTitle`, `m_strInProgress` all held real values on the running
  class. That is worth stating because it is the opposite of the `UITacticalHUD_InfoPanel` trap,
  where a `localized` var is declared on two classes and populated on only one; here the check
  was run and passed.
- ⛔ **The title rows carry no words of their own.** `UIObjectiveList.RefreshObjectivesDisplay`
  synthesizes a row with `MissionType = "TITLE"` (or `"DARKEVENTTITLE"`), `GroupID = -1` and an
  EMPTY `DisplayLabel`; `SetTitle` then pushes a class default into Flash. So a reader that trusts
  `DisplayLabel` alone emits a blank row where the heading is. Skip by `MissionType`, take the
  heading from the class.
- `Items` order IS the drawn order (`RefreshDisplay` fills `Items[i]` from `Data[i]` and
  `OnItemChanged` lays them out top to bottom in that order), so it is also the right reading
  order. Do not re-sort — `SortObjectives` has already run.
- **Completed and failed are OUR words, and have to be.** The game distinguishes them with a
  checkbox tick and a colour and no text at all, so there is nothing localized to borrow. Same
  for a counter row, which draws three icons and three numbers: the noun is parsed out of the
  asset token (`...objectives_civilian_rescued` → "civilian" / "rescued") rather than tabled, so
  a counter we have never seen still gets a real word.
  - ⛔ **The expansion's counters do not follow that shape, and the counter row's label is a
    PLACEHOLDER.** Supply Extraction's row draws
    `UILibrary_XPACK_StrategyImages.MissionIcon_SupplyPickup` / `_SupplyDrop` / `_SupplyDestroyed`
    — the split read *"MissionIcon, 0 SupplyPickup, 12 SupplyDrop, 0 SupplyDestroyed"* — and its
    `DisplayLabel` is the literal `"CounterLabel"`, `XComGameState_ObjectivesList.CounterDisplayLabel`,
    a key the list finds the row by because *"the counter does not have text"*. The `MissionIcon_`
    family is a small table keyed by the token's LEAF (the vocabulary rule: a missing entry degrades
    to the leaf's own tail, never shifts a slot), the states being the counter's three slots in
    `SeqAct_DisplayMissionCounter`'s own words — Current, Available, Lost — and the placeholder label
    is not spoken.
- `Data.Timer` and the four counter amounts are **-1 when unset** (the struct's own
  `structdefaultproperties`), and `0` is a real value — a timer of 0 means this is the last turn.
  Test `>= 0`, never truthiness.
- An invisible row is skipped (`ToggleVisibility` hides the members of a collapsed group, and the
  documented child-widget exception makes `bIsVisible` meaningful on a ROW) — but the count is
  LOGGED, because a list that silently shrank reads to a blind player as "the mission has fewer
  objectives" and would never be reported as a bug.
- ⛔ **The key stands down while a text-entry dialog is up.** `Ctrl+X` is CUT in a Flash text
  field and the claim would swallow it before the field saw it. The objectives list stays
  reachable under that dialog (`UIAvengerHUD` remains on the stack), so presence alone is not a
  sufficient gate — `FindTextInputScreen()` is the other half.
- **Why `Ctrl+X` and not bare `X`.** Bare `X` is live: `[XComGame.XComTacticalInput]` binds it to
  the generic `X_Key_Press | onrelease X_Key_Release` forwarder into `XComInputBase.InputEvent`
  (the ability-hotkey path). And because UE3 matches modifiers leniently, an UNCLAIMED `Ctrl+X`
  would fire that vanilla action alongside ours — claiming the chord is what stops the
  double-fire, and the player keeps the vanilla action on bare `X`.

### Stage counters (`UISpecialMissionHUD_TurnCounter`)

`Ctrl+X` also reads the top-right counter panel, after the objectives. It answers what the list
cannot: the list says what the goals ARE, the counter says how far through the current one you are
(«Приведите солдат на запасной маршрут / Осталось солдат / 3»).

- `m_sLabel`, `m_sSubLabel` and `m_sCounter` are all RETAINED — every `Set*` assigns its field
  whether or not the panel was inited, so the strings are authoritative. `m_iState`, `m_bExpired`,
  `m_bInfinity` and `m_bLocked` come along too.
- ⛔ **`bIsVisible` is REQUIRED on the counter panel.** Every `AS_Set*` ends in `Show()` — Firaxis'
  comment is *"don't show panel unless data is pumped into it"* — so an unfed counter is hidden,
  and the second of the two slots is routinely inited-and-hidden. Reading a hidden one invents a
  counter the player cannot see.
- The sub-label and the number are joined as ONE phrase, not two: "Осталось солдат" and "3" only
  mean anything together, and a comma between them would offer the number as a separate fact.
- ⚠ CLAUDE.md used to say these counters appear "only while a `UiTimer.ShouldShow`, so an untimed
  mission reads `Timers = 0`". That is too narrow — measured live 2026-08-23 on an **untimed**
  mission with the counter live and populated. A stage counter is not the same thing as a turn
  timer.

⚠ **This surface does not answer WHERE to go — and the thing that does was hiding in another
ring.** The counter gives the count and never the destination. `X2AccessObjectScanner.AddEvac`
already gives the destination properly: it clamps the scan origin into the zone's bounds so the
distance and bearing describe the walk you would actually make, and adds "N of M inside".

⛔ **A "move the squad to a zone" stage IS an evac zone** — confirmed live 2026-08-23 on
`LostAndAbandonedB`, whose «Приведите солдат на запасной маршрут» is exactly that. So the readout
was correct the entire time and simply never reached the player: `AddEvac` lived only in `CAT_EVAC`,
which has no key of its own and is reachable only by STEPPING the category ring. Meanwhile the `B`
markers ring — the one a player actually presses to ask "where do I have to go" — offered only the
objective anchor, which can say nothing but `"Objective: object"`.

**The lesson is about placement, not about reading.** A correct readout in a ring nobody steps to is
indistinguishable from a missing feature, and it cost this mission's whole navigation story. The
evac zone now also rides `CAT_MARKERS` (the Lost's precedent: one entry, shared ring, no extra
step); `CAT_EVAC` stays for asking about the zone alone. **When adding a contributor, ask which key
a player will actually press to ask that question** — not which category it taxonomically belongs
to.

⛔ **ONE KEY, ONE ENTRY — a duplicate key does not merely repeat itself, it WALLS OFF everything
behind it.** The same evac zone went missing a second time on 2026-09-04, on `Neutralize_Vehicle`,
with the placement lesson above already applied: it was on the `B` ring, entry 3 of 3, read
correctly, and still unreachable. `AnnounceCategory` remembers the selection by KEY and resolves it
with `IndexOfKey`, which answers with the FIRST match. The carried VIP owns **two**
`XComGameState_ObjectiveInfo` components (state IDs 50208 and 51149, both `OwningObjectId` 50206),
so `AddObjectiveMarkers` emitted him twice under one `ObjectID`; landing on the second copy stored a
key that resolved back to the first, and every further `B` press recomputed that same index. The
ring read "Objective: David Watson" at 1 of 3, then 2 of 3 forever. `Shift`+`B` reached the zone,
because stepping BACKWARD from index 0 wraps past the duplicate.

`InsertByDistance` now refuses an entry whose key is already in the ring — in the shared insert
rather than in the contributor, because every contributor already keys one entry per state object or
per synthetic slot and the invariant they all assume simply had no place that enforced it. Two
things generalize:

- **A "same thing twice" defect in a key-addressed ring is a NAVIGATION defect, not a cosmetic
  one.** The duplicate is the audible half; the truncation behind it is the expensive half, and it
  presents as *the ring ends here* — the same failure mode as the clipped `ReadStringProperty`
  buffer, indistinguishable from the truth and therefore never reported as a bug.
- **Deduplicating was PARITY, not tidiness.** The two components are real and differently sourced —
  #50208 from `CreateObjective_Pawn` at spawn, #51149 from a Kismet `SeqAct_MakeObjective` re-flag
  when the stage became "evacuate him" — but nothing the game DRAWS counts them: every
  player-visible query is a per-object boolean, and `UIUnitFlagManager.AddFlag` even carries its own
  «don't add duplicate units» scan. A sighted player sees ONE marker on that VIP. `AddObjectiveMarkers`
  was the only thing in the game counting attachments instead of objects; the full evidence and the
  general form of the trap are in `../engine-notes.md`.

## Tactical HUD graph (the ambient recipe)

The mod's first AMBIENT recipe (`recipes/recipe_tactical.cpp`): attached silently for the whole
mission, UNFOCUSED by default, so the tile cursor keeps the arrows while the graph keeps its
position and its render machinery warm. `Tab` focuses it (HUD mode — the navigator's claims
come alive: arrows, Tab/Shift+Tab between stops, Enter, Home/End), `Escape`
returns to world mode ("Map"), and the shot HUD raising takes focus back WITHOUT a spoken exit —
the targeting poll's own narration is the next thing heard, and an exit line would talk over it.
Stops, in on-screen order: **Soldier** (companion-backed — one label node per fact off
`X2AccessUnitReader.BuildSoldierRecords`, fetched once per rebuild), **Abilities** (pure widget
read), **Commands** (the `UITacticalHUD_MouseControls` strip, native — the one other tactical
panel that RETAINS its words), **Targets** (the enemy strip, companion-backed off the strip's
own arrays — `X2AccessTacticalReader.BuildEnemyRecords`), and **Objectives** (via
`ObjectiveReader::DescribeLine` — one composer with `Ctrl+X`, so the two surfaces cannot
disagree about a row). The abilities, commands and targets stops are HORIZONTAL, mirroring the
strips the game draws — Left/Right walks them, and past 15 abilities the bar wraps to a second
shared-key row (Up/Down, column-preserving) exactly where the game's own
`MAX_NUM_ABILITIES_PER_ROW_BAR` wraps the icons. Soldier and objectives stay vertical, as their
panels do.

- ⛔ **The probe must refuse while a `UIDialogueBox` is showing, by its own hand.** While a
  recipe is attached the navigator re-probes only THAT recipe — a higher entry in `kRecipes`
  gets its chance only after a detach — and the dialog is the one overlay the tactical gate
  cannot see, because `UIRaiseDialog` pushes no screen. Every other mission overlay (pause menu,
  hacking screen, tutorial box) is a real stack entry and detaches us through
  `hudOnStack`/`blocked`. Without the refusal the player answers the modal blind, with our
  claims eating the keys.
- ⛔ **The bar's live icon count is the container's own `ActiveAbilities` — never the widget
  array's length and never "which icons hold text".** `UITacticalHUD_Ability.ClearData()` clears
  availability, cooldown and charge but NOT `m_strAntennaText` or the hotkey label, so a soldier
  switch leaves the previous soldier's ability names sitting on the trailing icons, readable and
  wrong. `ActiveAbilities` is written by the same `PopulateFlash` pass that feeds the icons.
  ⚠ The mapping to `m_arrAbilities` is NOT 1:1 when commander abilities exist: `PopulateFlash`
  walks `m_arrAbilities` in order and gives icon `ActiveAbilities` to every ability whose template
  is not `bCommanderAbility`, so icon k is the k-th NON-commander entry. `SortAbilities` returns -1
  for a commander A against a non-commander B, which reads as commanders FIRST (an earlier note
  here said "tail"; neither order is measured). `ActivateBarAbility` passes the bar index
  straight to `AbilityClicked`, which indexes `m_arrAbilities` — right only while the offset is
  zero. Unverified live and untouched here (it is the mouse path).
- **Ability names in controller mode come from the companion (2026-10-30, NOT VERIFIED LIVE).**
  `UITacticalHUD_Ability.UpdateData:89` writes `m_strAntennaText` only under
  `Movie.IsMouseActive()`, so with a controller as the input device every icon's name is empty
  and the stop read `Ability 1`, `Ability 2`. `BuildTacticalHud` asks
  `X2AccessTacticalReader.BuildAbilityBarNames` (one record per icon, name positional, empty
  values kept; commander abilities skipped as above) ONLY when an icon's retained name is empty, so
  mouse mode never makes the call. The words are `CapsCheckForGermanScharfesS(GetMyFriendlyName())`,
  the pair the game would have pushed. Cached in a `WireCache` keyed on `ReadAbilityBarSignature`
  (a fold of the ObjectIDs in `m_arrAbilities`, so a soldier switch or a changed bar re-asks;
  0 = uncomputable = never cached). A record count that differs from the bar's icon count is
  logged and the names dropped for the numbered fallback — a shifted name is worse.
- **The stop list is `UITacticalHUD_MouseControls`'s list, never a sketch of what a HUD "should"
  have.** Both halves of the parity rule bit here: the WOTC initiative order looks like an obvious
  stop and is CHEAT-GATED in retail (`UITurnOverlay.ShowOrHideInitiative` shows it only when
  `CHEATMGR.bShowInitiative`), so narrating it would leak enemy turn order; and the standard HUD
  carries no turn counter — the only counters are `UISpecialMissionHUD`'s pair, shown only while a
  `UiTimer.ShouldShow`, so an untimed mission reads `Timers = 0`. On mouse/keyboard the
  mouse-controls strip IS the command surface: `CharInfoButton` / `EndTurnButton` /
  `SkyrangerButton` are spawned only in the CONTROLLER branch of `UITacticalHUD.OnInit` and read
  null live (in mouse mode; the controller-mode stop is the next-but-one bullet).
- **Activation is `AbilityClicked(index)`** — the mouse path, with the input gate
  (`PreProcessCheckGameLogic` refuses outside the player's moment), the tutorial's
  disabled-trigger check and the select sound. It returns false SILENTLY, so the recipe speaks
  the refusal ("unavailable") itself; on success targeting raises, the navigator yields focus on
  its next tick, and the targeting poll announces ability and default target.
- The unavailability REASON is not spoken yet: `AvailableCode` is an FName
  (`AA_CannotAfford_ActionPoints`) with no localized form the widgets retain, and speaking it
  would hand a Russian player an internal identifier. It waits for the companion vocabulary; the
  node is marked "unavailable" meanwhile.
- **Bare Tab is the entry key, and E / Q switch soldiers underneath the graph.** Both follow from
  the companion's `XComInput.ini` (`docs/input-map.md`): the game has nothing left on Tab, and
  a soldier switch made with E while the graph is focused is the game's own — the selection
  poll narrates it and the soldier stop re-reads on the next rebuild. `Shift+Tab` is safe
  because `LeftShift` carries no binding any more; there is no Shift swallow.
- **Soldier stop**: one node per fact, all through the `;` readout's own composers where the two
  overlap (`DescribeTargetHealth`, `DescribeActions`, the effect lists) so the stop and the key
  cannot disagree, plus the facts `;` never carried: the weapon readout (primary weapon's
  localized name + `Ammo` of `GetClipSize`, the ammo clause dropped for infinite-ammo weapons,
  mirroring `UITacticalHUD_Weapon.SetWeaponAndAmmo`), the aim number (localized
  `m_aCharStatLabels[eStat_Offense]`, confirmed populated live), the promotion arrow
  (`UISquadSelect_ListItem.m_strPromote`), the focus meter (through `CHHelpers.GetFocusTuple`,
  the SAME HL tuple the panel reads, so a focus-UI override carries over), the bond icon and the
  hack-reward icons. The unit's ObjectID rides every node identity so a soldier SWITCH reads as
  new nodes to the differ, while damage or a spent point on the same soldier is a value change
  under a held identity.
- ⛔ **The commands stop's VISIBLE set is reconstructed, never read.** `SetNumActiveControls`
  goes straight to Flash and is retained nowhere, and a `ButtonItems` slot outside the visible
  set keeps its last-written label FOREVER — an unused command slot, a Chosen button after the
  Chosen left. Membership therefore comes from the same three inputs `UpdateControls` used:
  buttons 0-4 always, slot 5 iff `CommandAbilities.Length > 0`, slot 6 iff
  `GetActivatedChosen() != none` (the game's own gate, asked fresh per rebuild — and asked
  with its optional `HistoryIndex` of -1 in the block, because a pointer-only block asked
  about history frame 0 and read "Button 7" ten minutes before the Assassin revealed; the
  rulebook's outbound-call rule) — and the stop is built only while the shot HUD is DOWN,
  because raised, slot 0's retained label is legitimately "cancel shot" and the layer is
  yielding focus anyway.
- **Commands with a controller as the input device (2026-10-30, FROM SOURCE, NOT VERIFIED LIVE).**
  `UITacticalHUD.OnInit` spawns `m_kMouseControls` only with `Movie.IsMouseActive()`; otherwise it
  spawns `EndTurnButton`, `SkyrangerButton` and `CharInfoButton`, plain `UIButton`s on the HUD, so
  the stop list was Objectives, Score, Soldier, Abilities with no way to end the turn. The recipe's
  commands block is now `if (strip valid with buttons) {...} else if (!strip valid) {pad branch}`,
  and `ReadPadCommands` returns nothing whenever the strip POINTER exists, so mouse mode cannot
  reach it. One node, End Turn, labelled with the button's retained text (else the HUD's
  `m_strNavHelpEndTurn`, else `CommandFallback`), no key hint. Activation mirrors `OnAccept`
  `m_optEndTurn` and `Back_Button`: refused in the tutorial (`TUTORIAL != none`, here
  `ReadTutorialState().valid`; speaks `Disabled`), otherwise
  `XComTacticalController.PerformEndTurn(ePlayerEndTurnType_PlayerInput)` called on `UIPanel.PC`
  (0x2C8), the block hand-rolled with both members. No other gate, same as the mouse path: not the
  player's turn or a move running is decided inside, where `XGPlayer.EndTurn` calls
  `Visualizer_ReleaseControl` unconditionally and submits the skip turn only when the local player
  is the cached unit-action player; the return value is false either way and is not read.
  **Left out, on purpose:** Evac. The pad path is `XComTacticalInput.Stick_R3`, a STATE function that
  gates on `SkyrangerButton.bIsVisible`, the tutorial and `ActivateAbilityByName(mask, FName)`,
  and the Highlander swaps the name for `CHHelpers.GetPlaceEvacZoneAbilityName()` (Issue 855). That
  needs an FName this layer has no way to build and more than one call; the button is therefore
  still unreachable on a pad (`next-steps.md`). CharInfo is the Soldier stop's own button.
  Highlander diff for the classes touched here (`Workshop/1134256495`): `UITacticalHUD`,
  `UITacticalHUD_Ability`, `UITacticalHUD_AbilityContainer`, `XComTacticalInput` carry changes, none
  that touch the controller-mode branches above; `XComTacticalController.PerformEndTurn` is unchanged.
- **The `TButtonItems` element stride is the live engine's, not the compiled SDK's.** The
  ScriptStruct's runtime `PropertiesSize` is 0x21 and whether `UStructProperty` rounds that to 4
  when striding is the engine's business, so `ReadMouseControls` asks the live `UArrayProperty`
  for `Inner->ElementSize` (`UE3Reflect::ArrayElementSize`), sanity-bounds it, and logs it once
  — the `static_assert` pins only the intra-element offsets (Label 0x00 / key 0x10 / state
  0x20) and documents the compiled sizeof it deliberately does not use. Measured live
  2026-08-31: the engine answers **0x24**, agreeing with the pack(4) compiled sizeof — so the
  engine does round, but the reflection read stays, because it is the read that would have been
  RIGHT either way.
- **Command activation is the panel's own click**: write `m_iCurrentSelection`, call
  `OnAccept()` — the set-selection-first rule, byte-for-byte the `FXS_L_MOUSE_UP` path. A
  disabled button (any non-Normal `UIState`, OnAccept's own test) gets no `OnActivate` and is
  spoken "unavailable"; the key string each button retains is the player's ACTUAL binding, so
  the stop doubles as where the vanilla hotkeys get taught — except the localized "no key
  bound" line, which is a sentence of its own and is spoken without the "key" prefix.
- **Targets stop** (2026-09-04, never run live): one head per entry of
  `UITacticalHUD_Enemies.m_arrTargets` in the strip's OWN order — `SortEnemies` puts the best
  shot leftmost and destructibles last, and that order is the fact a sighted player reads off the
  strip. Every mark a head carries is spoken from the exact place the strip draws it: the glyph
  is the character template's icon, so the NAME is the template name (`GetName(eNameType_Full)`;
  objects through `DescribeNonUnitTarget`); the background colour is `GetMyHUDIconColor`, so
  "civilian" and "ally" are spoken and the default red is not; the hit number is the strip's own
  `GetHitChanceForObjectRef` (the selected ability, else the default attack; hidden when the
  breakdown says so), captioned with the shot HUD's label like the reticle line; the flanked
  marker and the flag's shield and bars go through `DescribeTargetCover` / `DescribeTargetHealth`;
  the squadsight badge is `m_arrSSEnemies`; the greyed icon is "not targetable" — visible, but
  outside `m_arrCurrentlyAffectable` for the selected ability (or, with none selected, for every
  icon-bearing one); the buff/debuff badges are spoken as the effect NAMES their icons and the
  Enemy Info screen show (`X2AccessUnitReader.EffectNames`, now static); the targeted head
  carries the kernel's `Selected` part, which also lands the stop on it. Distance and bearing
  ride last as the one AID the plan grants, in the scanner's own words (`TileDistance`,
  `OffsetWords`), so the ring and the stop agree about where a thing is. ⛔ **Enter is the
  strip's click keyed on the OBJECT, never the icon index**: the strip re-sorts on every refresh,
  so an index held for a tick may name a different head. `SelectEnemyHead(ObjectID)` re-finds
  the head, replicates `OnMouseEvent`'s one guard (no strip selection in the tutorial while
  soldier chatter is off), runs `SelectEnemyByIndex` — `DirectTargetObject`, the flag
  extension, the select sound — and answers the game's silent false by asking whether
  `m_isMenuRaised` actually rose. ⚠ Uncached like the Soldier stop: the hit chances and the
  flanked marks change under a held pointer set, so nothing about the strip is a safe cache key.
- While unfocused the navigator runs NO rebuilds and no differ — the graph is dormant, and every
  pointer in its stale render is refreshed by the `Rerender()` that focus entry starts with. A
  mid-mission cinematic detaches and silently re-attaches the recipe; stop positions reset there
  (GraphState is per-attach), which is the accepted cost until a live run says otherwise.

## Tutorial message reader

The tutorial is a REPLAY (`XComTutorialMgr extends XComReplayMgr`): it plays a recording
back, halts at every frame that needed input, and REJECTS anything that does not match —
with a sound and **no text at all**. Four surfaces carry its words and three are readable.
`UITutorialBox` keeps `Title`/`Desc`/`ButtonHelp` in real properties (deliberately NOT a
recipe: it holds nothing navigable — `OnInit` calls `DisableNavigation()` on container and
button — and a recipe would claim Enter/Space/Escape, which are how the box is dismissed).
The anchored blade and every other flyover come from polling `UIWorldMessageMgr`. The
bottom-right HUD blade goes straight into Flash and is recovered by the companion.

⛔ **`UITutorialBox` is not a tutorial-mission screen, and gating it on `XComTutorialMgr` cost
every one of the WOTC first-time popups.** That manager exists only in Tutorial_01, so
`OnTick`'s "are we in the tutorial" early-return also skipped `PollModal` — and the boxes that
actually matter are submitted on ORDINARY missions by
`XComTacticalController.CheckForTutorialMoments` (`:298` — target preview, dazed, soldier bonds,
Chosen tracking shot, Reaper, Templar, Templar momentum, Skirmisher),
`XComGameState_Unit`'s ambush box (`:2094`), `UITacticalHUD_AbilityContainer`'s first-melee box
(`:1138`) and `SeqAct_ShowTutorialBox`. Each sets its own `bHasSeen…` flag as it fires, so they
are **one-shot per campaign**: silence there is not a bug the player can hit again. The modal
poll now runs above the gate; only surfaces 2 and 3 stay behind it.

**Nothing read wrong — that is the part worth keeping.** `/tutorial`'s `Describe()` printed the
box's title and body perfectly throughout, the offsets were `static_assert`ed, the screen was in
the tracked list. A gate standing in for the wrong question fails exactly this way, with every
part it guards in perfect health, so `/tutorial` showing a surface is not evidence the poll that
speaks it ever runs. Repeatable test without waiting for a campaign first: the retail binary has
a CheatManager, so `just dev-exec "ShowModalTutorialDialogBox 0 0"` spawns one on demand.

- **Markup is TRANSLATED, not stripped**: `%KEY:...%` already resolves to a bracketed,
  binding-aware key name, but mouse/arrow glyphs arrive as `<img src='rmb_icon'>` and
  `CleanText` would leave a hole exactly where the instruction is.
- Damage flyovers are skipped and plain world messages are read only inside the tutorial —
  `X2AccessTacticalEvents` narrates those from the game state, and both speaking would double
  every hit. A plain message is spoken only while `m_bVisible` (it is anchored in the WORLD,
  and `UpdateMessageLocation` clears that bit off screen) and stays PENDING rather than
  dropped so panning onto it still announces it; **blades are exempt and must be**, because
  they sit at the DESTINATION rather than the camera and read `vis=0` routinely.
- ⛔ **World messages are queued PER UNIT** (`X2Action_ConcealmentLost` queues one at each
  `Unit.GetLocation()`), so identical text from several slots is normal — a whole squad going
  concealed publishes the word once per soldier. Do NOT de-duplicate on text: two soldiers
  falling unconscious would collapse into one line. The fix is NAMING the anchor:
  `DescribeMessageAnchor` resolves `m_kFollowObject.ObjectID` through the history (gated on
  `CurrentHistoryFrame`, or the replay answers with the object as it WILL be) and reuses
  `X2AccessTacticalEvents.IsNarratable` / `UnitName`, so a flyover line and a combat line
  agree about who they name and obey one fog rule. Blades are excluded: an anchored blade is
  an instruction about a PLACE, so prefixing whoever stands there would be actively wrong.
- A MISS reaches `UIWorldMessageMgr` as an ordinary world message, not a damage one
  (`QueueWorldMessage` stamps `eHUDMessage_Damage` only when `m_iDamagePrimary > 0`), so
  flyovers matching the game's own result strings are dropped — the attributed line outranks
  the bare word.
- It announces that a submission was REFUSED (`InvalidSubmissionCnt` rose) but never where
  the tutorial wanted the player to go — that is the 3D marker's data and belongs to the
  object scanner. It leaves `X2AccessTileReader.CursorMoveTo` alone, which says "Moving" for
  a move the tutorial then vetoes: `ClickToPath` returns `true` before the submission is
  attempted, so "Moving" can only ever mean *submitted* — anywhere, not just here.

## Hacking screen

The purest case of "the widget tree holds no words" — `/gui` prints **`1 panels`**, the
screen and no children, because every string goes into Flash through `MC.FunctionOp` /
`ActionScriptVoid`. The companion supplies ALL of it and this module hand-transcribes **no
offsets at all** — correctness, not tidiness: the Highlander interleaves
`m_UsingSkulljackScreen` and `m_hackCanceled` into the bitfield at 0x450, so
`bWaitingForInput`'s **MASK** is 0x20 under the Highlander and 0x08 in vanilla, and
`static_assert`s pin offsets, not masks.

The screen's `OnUnrealCommand` handles only `FXS_DPAD_*`, so on mouse+keyboard the reward
choice is reachable **only by clicking a card on the 3D pad**. `Left`/`Right` move a cursor
across the (up to) three cards and hand the screen its own
`OnUnrealCommand(FXS_DPAD_LEFT/RIGHT, FXS_ACTION_PRESS)` — the int
`X2HackRewardTemplateManager.AcquireHackRewards` reads back off that very screen object, so
there is one source of truth; `Delete` re-announces. `Enter`/`Space`/`Escape` are NOT claimed
and every claim drops once the hack commits (the reveal ends with "press any key", and a
swallowed key would not dismiss it), reported through `NativeState`. Cancelling is detected
on the C++ falling edge — `OnCancel` closes the screen before anything can be read, and a pad
that goes away uncommitted was cancelled.

- ⛔ **The lit cards are NOT the rewards.** `AcquireHackRewards` only ever considers
  `i == 0 || i == SelectedHackRewardOption`, so an unselected middle card cannot be awarded
  whatever the meter did, while the meter lights panels by threshold and the thresholds
  DESCEND with the odds — passing card 3's lights cards 1 and 2 as well — and on a MISS
  `InitiateHack` further inflates the bar for drama
  (`Max(DisplayedScore, Rand(100 - SelectedHackChance - 1))`, whose own comment says it "does
  not affect the outcome of the hack at all"). Naming the lit prefix credits the player with
  rewards that were never in play. What `m_rewardsTaken` DOES answer is the only question
  that matters — a panel lights at `100 - GetHackChance(index)` and the inflation is bounded
  at `100 - SelectedHackChance - 2`, strictly under the selected card's own threshold, so
  `m_rewardsTaken > SelectedHackRewardOption` is an exact did-the-gamble-land test. The award
  is then rebuilt the way the game builds it: slot 0 always, UNLESS it is `bBadThing`, in
  which case it is the PENALTY and lands only when the gamble missed — and then it is named,
  so "missed" says what it cost. That also beats the banner Flash prints, which reads
  `HackRewards[RewardIndex].bBadThing` and so reports success even on a miss whenever slot 0
  is ordinary.
- ⛔ It must NOT gate on `StackState::Interactive()`: the stack reports **cinematic=1** while
  this screen is up, legitimately — `HideUIForCinematics` skips every screen with
  `bShowDuringCinematic`, which this screen sets. Detection reuses `ReadTacticalGate()`, whose
  `blocker` reads `UIHackingScreen` at exactly the instant the tile cursor stands down.
- Only the first `Min(len, 3)` rewards are spoken: the movie has three panels and
  `RefreshPreviewState` hard-loops `Index < 3`, so on the tutorial door (whose array is FIVE — object rewards plus
  `BattleData.TacticalHackRewards`) the tail is unreachable for everyone. Chances come from the screen's own `GetHackChance`
  (never re-derived — sitreps, Gremlins and Skullmining all move the inputs) and are
  truncated, matching Flash: 6.42 and 2.75 print as 6% and 2%.

## Loot receipt

`UIInventoryTactical`, the `loot` recipe with `X2AccessLootReader`. Built 2026-09-03 from source
(`../tactical-modals-recon.md` §1); **not yet run live**.

- ⛔ **It is a receipt the game is waiting on.** The loot is already in the soldier's inventory
  when the screen opens (`LootAbility_BuildGameState`), TAKE ALL / DUMP ALL are commented out of
  the shipped source, and `X2Action_Loot` sleeps until the screen's callback fires — so OK is the
  one verb and it must always be reachable. The Loot ability auto-fires at the end of a move, so
  the screen appears unprompted.
- ⛔ **Not a `UIInventory`.** The commodity recipe's finder never matches it and its rows are plain
  UIPanels with one int (`GameStateObjectID`) — no flag dword, no text. The companion re-derives
  each row's name (with the ` xN` suffix the row draws) and its hover description from that id.
- **OK goes through the SCREEN's Enter**: `OnUnrealCommand` maps every key it handles to
  `OnButtonClicked(m_kButton_OK)`, the button is `DisableNavigation()`ed, and `OnCancel` is dead
  code that would not close the screen. Escape is left to the game — it is OK too.
- The gate marks the screen `blocked` (not in `kTacticalPassives`, deliberately: it consumes every
  key), which is what lets this recipe probe after the HUD layer detaches.

## Soldier sheet

`UITacticalCharInfoScreen`, the `soldier info` recipe, and the HUD graph's soldier-info button
with `X2AccessCharInfoReader.OpenCharInfo`. Built 2026-09-03 from source
(`../tactical-modals-recon.md` §2); **not yet run live**.

- ⛔ **Nothing on a keyboard opens it in vanilla.** `Stick_L3` (the gamepad's stick click) is the
  screen's only spawn site; F1 is a commented-out no-op; the mouse-controls strip has no
  soldier-info button. The HUD graph's soldier stop therefore carries a button — named with the
  HUD's own `m_strNavHelpCharShow`, the label its controller-only hint uses — and the companion
  reproduces `Stick_L3`'s body behind `CanCharInfoShow`. It is offered only while the shot HUD is
  down, the same gate. This re-provides a route the game ships for gamepads; it hands out no fact
  a sighted gamepad player cannot see.
- ✅ **A pure widget read.** Every label keeps its `Text` and every effect row keeps its
  `UISummary_UnitEffect` by value — `GetUISummary_UnitEffectsByCategory`'s own output, the `;`
  readout's oracle — so there is no companion and no Flash capture. The labels are found by
  MCName because the screen keeps no pointer to them.
- ⛔ **A probe can win before the sheet exists.** `InitCharacterInfoScreen` runs before the push
  and `OnInit` (which spawns `ListPerks`) after it, so the reader answers `valid = false` on a
  null list and the recipe treats that as not-yet-open.
- **The stats are the FLA's seven** (HEALTH, AIM, HACK, WILL, ARMOR, DODGE, PSI) and nothing else
  — Mobility and Defense are in the struct and not on the screen. `---` is the screen's marker for
  zero and is said as `none`. The effect headings are the game's own, present only for a
  non-empty category, and become contexts. ✅ Attached and read the stats live 2026-09-04.
- ⛔ **`SetHTMLText` fills only the html field.** The class label is the one label written that
  way, and the first live sheet read the heading as the name alone; the labels are read Text
  first, HtmlText as the fallback (both cleaned). The name and the stats go through
  `SetText`/`SetTitle` and fill both fields.
- Nothing activates; scrolling is gamepad-only in vanilla and the graph is the way to read a long
  list. Escape is the game's close, which `Show()`s the HUD again.

## Tactical Legacy Pack, the mission side

The eleven screens the pack pushes over `UITacticalHUD` — `recipe_tle_mission.cpp`,
`game_ui_tle_mission.cpp`, `X2AccessLadderReader.uc`. Built 2026-09-17 from source with the game
closed; **none has run**. The measurement and the per-screen decision table are
`../tle-mission-recon.md`.

- ⛔ **The score HUDs (`UIChallengeModeHUD`, `UILadderModeHUD`) are stack entries above the
  tactical HUD for the whole mission, and they are in `kTacticalPassives`.** They take no
  input and hold nothing a player acts on; unlisted, the gate read every ladder and challenge
  mission as blocked from its first frame — tile cursor, HUD graph and every tactical producer
  silent. Any future overlay the pack (or a mod) parks above the HUD for a whole mission is the
  same case, and the match is by exact class name.
- **The score HUDs are narrate-only, by the destruction test**: nothing on them is walkable, and
  the arrows are the tile cursor's. The banner per scoring event and the running total are
  narrated by `X2AccessLadderReader`'s listener on `ChallengeModeScoreChange` at
  `ELD_OnVisualizationBlockCompleted` — the HUD's own registration window, so the words follow
  the banner — in the HUD's own labels; the score and, in a challenge, the countdown are one
  TRAILING stop of the HUD graph (`AddTLEHudStop`), added only on those missions.
- **Every other screen is a recipe**, each a modal the ruleset's post-mission loop pushes and
  pops in turn (mission summary → upgrade → squad review → medal progress or medal → end), so
  no two are ever stacked and their order among themselves is free. The squad review pushes the
  soldier loadout popup over itself, which is why that recipe leads the band.
- **Words the widgets keep are read natively; the rest the companion re-derives** from the
  objects each screen's `OnInit` read: the challenge manager's score table for the scoring
  dialogs, the ladder's `ProgressionChoices` for the upgrade pick, the squad review's own
  `PrevStates` / `CurrentStates` diff, the analytics for the end screen. The one localized field
  in the family that `OnInit` FILLS rather than reads is the medal screen's
  `m_MedalUnlockedString` (rewritten in place with the medal name), so it is a native read.
- The upgrade picker's keyboard is complete in vanilla (Left/Right select a column, Enter
  chooses), so write-back goes through `SelectColumn` and the game's highlight follows ours; the
  squad review's Prev/Next carry no label and `DisableNavigation()`, so they are read by pointer
  and visibility (hidden means no page that way) and clicked through `Click()`.

## Tactical combat narration and the replay path

Moved out of `CLAUDE.md` (which keeps the ownership rules and points here): the design of
`X2AccessTacticalEvents.uc`, `X2AccessReplayNarration.uc`, the tutorial blade recovery in
`X2AccessTutorial.uc`, and the markup vocabulary they share. Every ⛔ below is load-bearing —
the listener set does not work if any one of them is undone.

- **Tactical combat narration** (`X2AccessTacticalEvents.uc`): one `CHEventListenerTemplate`,
  tactical only, nine events — the path forced by the flyover/notice/banner tap not being
  buildable. **Deferral is a per-event decision, not a default**, and for one action it is SPLIT
  ACROSS BOTH visualization windows: `ELD_OnVisualizationBlockStarted` for the ACTION
  (`AbilityActivated`) and `ELD_OnVisualizationBlockCompleted` for its OUTCOME
  (`UnitTakeEffectDamage`, `UnitDied`), because a listener otherwise fires at state submission,
  seconds ahead of the animation; `ELD_OnStateSubmitted` for structural facts (`PlayerTurnBegun`,
  concealment, panic, shields, evac), which are tied to no animation and would be dropped entirely
  by a state that never builds a visualization block.
  - **The player-turn alert uses the game's `UISpecialMissionHUD_TurnCounter.m_strPlayerTurnLabel`**
    on both live and replay paths. In Russian its loaded value is "Ваш ход"; the regular
    `UITurnOverlay.m_sXComTurn` says "ДЕЙСТВИЯ СОПРОТИВЛЕНИЯ", which sounds like Resistance NPC
    activity rather than the player's turn. Keep both paths on the same game-owned label.
  - ⛔ **The split is what puts cause before consequence, and a shared window cannot.** Priority
    orders listeners for the SAME event only; between events the order is the GAME's trigger order,
    and that is backwards for us — `XComGameState_Unit` triggers `UnitTakeEffectDamage` (`:5874`)
    and `UnitDied` (`:6022`) while effects are applied, and `XComGameStateContext_Ability` only
    reaches `AbilityActivated` afterwards under its own *"Trigger post-activation events"* comment
    (`:191`). All three on one window speak the outcome before the attack — you learn you have been
    shot before you learn by whom — and they land in the SAME 100 ms drain, so the narrator cannot
    reorder what it cannot tell apart. Blocks run sequentially, so the split also stops two
    abilities resolving in one burst from interleaving; it is what the game's own HUD does
    (`UIUnitFlagManager:55`, `UITacticalHUD_Enemies:68`). The replay narrator needs none of this:
    walking frames in order gets the human order for free.
  - ⛔ **One grenade landing as several `DamageResults` entries is TWO bugs, and summing is only
    the first.** `DamageResults` accumulates for the whole mission, so damage is SUMMED over the
    entries whose `DamageResult.Context` matches this state — that fixes what the line SAYS.
    `XComGameState_Unit.TakeDamage` also fires `UnitTakeEffectDamage` once per entry, and deferred
    to `ELD_OnVisualizationBlockCompleted` those N callbacks are IDENTICAL in every argument (same
    unit state, array already complete), so each re-sums the same total into the same sentence and
    all N land in one 100 ms drain — that is how many times it is said, and summing does nothing
    about it. Nothing stateless can pick one of N indistinguishable calls, so
    `OnUnitTakeEffectDamage` claims `(GameState.HistoryIndex, Unit.ObjectID)` through
    `ClaimDamageNarration` before it speaks. **The key is identity, never the text** — that is what
    keeps it compatible with the narrator's no-text-dedup rule, since two ADVENT Troopers differ by
    ObjectID where their words do not. The replay narrator needs no such guard: it visits each unit
    of a frame once by construction.
  - Two more details that needed the game's own source: only abilities that actually roll get a
    result word (`X2AbilityToHitCalc_DeadEye` always hits, so the word would be noise), and it is the game's own
    localized `m_aAbilityHitResultStrings`; and the interrupt pass is skipped, because
    `AbilityActivated` re-triggers per interrupt step. Fog parity via `CanXComSquadSeeTarget` with
    ONE documented exception — a unit stops being visible the moment it dies, so a kill the squad
    CAUSED is always announced.
  - **Loot appears after a death only when the death state contains a timed drop** whose
    `LootSourceID` is that unit and whose marker is visible (`LootExpirationTurnsRemaining > 0`
    and `HasAvailableLoot()`). `NarrateDeath` says the kill first, then `Loot available for pickup`
    on the combat channel. In live play it checks the current drop version again, since the game's
    `LootDropCreated` handler can collect it immediately; the replay path stays on its visualized
    frame because later history is already populated. No separate loot event listener is needed.
  - **The pod reveal and the reinforcement drop** (`ScamperBegin`, `SpawnReinforcementsComplete`,
    both `ELD_OnVisualizationBlockStarted`; builders `BuildRevealLine` / `BuildReinforcementLine`,
    shared with the replay narrator). Neither cutscene draws a word — the reveal is a camera pan
    and a matinee, the drop is the transport matinee with the units in its slots — so the source is
    the state that DRIVES the picture: `XComGameStateContext_RevealAI.RevealedUnitObjectIDs` (what
    `X2Action_RevealAIBegin` pans to and force-shows) and the units created in the spawn state
    (what `X2Action_ATT.AddUnitsToMatinee` slots). Block START, because the words belong during
    the pan; the game's own enemy strip takes the same event on Completed, which is why the heads
    appear only after. Same units collapse by spoken name with a count only above one — the game
    has no plural strings. `FirstEncounterCharacterTemplate` on the context adds the "new enemy
    type" clause; Central's line for it is the commlink subtitle the transient reader already
    speaks.
    - ⛔ **A drop is BOTH events**: `OnSpawnReinforcementsComplete` ends in `AlertAndScamperUnits`,
      which reaches `OnScamperBegin` and a reveal context for the units that just landed. The guard
      is the spawn memo (`NarratedSpawnUnits`, cleared on every turn handover by both paths): the
      drop line CLAIMS each unit it names by ObjectID, the reveal skips claimed units. Identity, not
      text — a second ADVENT pod revealed the same turn still speaks. Cleared per TURN rather than
      never, because a stale memo would silence a Chosen's real reveal turns after its arrival, and
      a memo cleared too early costs only an audible duplicate.
    - ⛔ **The drop speaks only the spawn kinds the player WATCHES** (`SpawnLabel`, mirroring
      `BuildVisualizationForUnitSpawning`): `ATT`, `PsiGate`, `Dropdown` and the two Chosen kinds
      get a camera plus `ShowSpawnedUnit`; `TheLostSwarm` gets neither (the swarm is first seen at
      its REVEAL, which is where it is spoken, and the memo therefore holds nothing for it); the
      blank kind gets a camera but no `ShowSpawnedUnit`, so its units stay under the fog rule and
      wait for their reveal. `bForceScamper` defaults to false, so a pod that lands out of sight
      never fires `ScamperBegin` at all — which is why the composition is read at the DROP and not
      deferred to the reveal.
    - Both are `Combat`, not `Alert`: the reveal often lands seconds after the concealment-broken
      alert for the same move, the drop after "Enemy turn.", and an alert would cut those short.
- **Replay narration** (`X2AccessReplayNarration.uc`): the tutorial's stand-in for the dead event
  bus. Polled from `Narrator::OnTick` (100 ms, next to the drain), it walks history frames and emits
  ability / damage / death / concealment / turn / reveal / reinforcement lines through
  `X2AccessTacticalEvents`'s shared builders — `BuildAbilityLine`, `BuildDamageLine`,
  `NarrateDeath`, `BuildRevealLine`, `BuildReinforcementLine` — so a shot reads identically
  whether an event or a frame produced it. Its gate is `bInReplay`, i.e. exactly when the listeners
  cannot fire, so the two paths can never both narrate one action.
  - ⛔ **Never read past what the player was SHOWN**: the whole tutorial script is in the history at
    mission start, so the cursor is `XComGameStateVisualizationMgr.LastStateHistoryVisualized` (the
    game's own "which frame is on screen", what `UIUnitFlag` uses), clamped again to the replay's
    `CurrentHistoryFrame`. That also syncs narration to the animation.
  - ⛔ The catch-up bound is a RUNAWAY guard, not a pacing one: normal playback advances that cursor
    in steps of **25-50 frames**, because most history frames carry no visualization of their own
    and `LastStateHistoryVisualized` jumps to the end of a block when the block completes. A
    24-frame bound read as "seek" on ordinary play and dropped 597 frames across one tutorial run —
    among them an overwatch shot the player watched and never heard. It is 512 now, and the only
    real seek is `StartReplay`'s own `JumpReplayToFrame`, which the first-poll branch already
    adopts. A skip is always logged, never silently swallowed. It resolves the GRI by hand rather
    than through `` `REPLAY `` / `` `XCOMVISUALIZATIONMGR ``, because those dereference a null GRI
    in the shell ten times a second.

### The narrator bridge (`X2AccessNarrator.uc` + `src/narrator.cpp`)

Bridge v2, the queued and channelled path the v1 `X2Access_Speak` sentinel cannot be. Producers
call `Alert()` / `Combat()`, which append to an `ENTRY_SEP`-joined string on the CDO, and C++
drains the whole burst in ONE `DrainNarration()` per 100 ms — the outbound call is the cost, not
the payload. Four channels natively — `Focus`, `Aid`, `Alert`, `Combat` — and all four route
identically: queued in arrival order, the channel being the label the scrollback ring and the log
show. ⛔ **No channel clears the queue and no channel interrupts** (stated in `CLAUDE.md`): a line
that cleared it would eat the damage line explaining the kill it follows, and `TTS::Speak` carries
no interrupt flag — Prism is always handed `interrupt = false`, so whether a new line cuts off the
one being read is the screen reader's decision under the player's own settings (2026-09-16, when
the last `SpeakInterrupt` site and the Focus/Aid queue clear both went). Flush is on the frame pump,
so the queue orders *within* a frame and adds no latency; past it the screen reader's own queue
takes over, which is the honest limit — once handed off, a line is the reader's, hence the 24-line
cap, logged on overflow at both ends. A 128-line ring with channel, SOURCE and timestamp backs the
future speech-scrollback key.

- ⛔ **`default.X = ...` does not compile unless X is a `config` property** — the UnrealScript
  compiler says *"Can't change default values of non-config properties"*, and a static producer has
  no other route to per-class storage, so the queue vars are `config(Game)` with no ini section
  shipped (nothing is ever written back; `SaveConfig` is never called). The payload is a delimited
  STRING rather than a dynamic array because mutating one through `default.` is a pattern the
  shipped SDK sources use nowhere.

### Tutorial blade recovery

- **Tutorial blade recovery** (`X2AccessTutorial.uc`): the one tutorial surface native cannot read.
  Walks the history BACKWARDS from `XComTutorialMgr.CurrentHistoryFrame` (never past it) collecting
  removed `MessageID`s, and resolves the newest surviving UNANCHORED blade through the mission's own
  `X2MissionNarrativeTemplate.GetObjectiveText`, exactly as
  `X2Action_ShowTutorialPopup.GetTutorialBoxText` does. A removal context carries only `MessageID` +
  `Remove` — `BoxType` reads as its default — so removals must be matched by id, never filtered by
  type.

### Speech vocabulary

- **Speech vocabulary** (`X2AccessHelper.GetInputVocabulary` / `GetResultFlyovers`): the game's OWN
  localized words for markup native has to speak. Our built-in table is English, which is wrong the
  moment the player is not. Keyed by markup token (an `<img>` `src`, or `KEY:NAME`) so a missing
  entry degrades to the English word instead of shifting a positional table. ⚠️ `%KEY:LMB%` is the
  one that MUST be there: `X2MissionNarrativeTemplate.KeyBindingMarkups` resolves only
  RMB/ENTER/TAB/Q/E/P and `%KEY:LMB%` is substituted solely by `UIUtilities_Input.InsertPCIcons`,
  which the blade path never calls — so it reaches speech raw.
