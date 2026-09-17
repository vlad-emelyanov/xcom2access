// X2AccessTileReader
// The tactical tile cursor's game-state half. The C++ layer owns the keys (input arbiter
// claims) and speech; it calls these functions on this class's default object via ProcessEvent
// (game thread, from the tick drain) and reads NativeResult back through reflection right after
// the call returns. Nothing here runs on its own — no listeners, no timers, no polling.
//
// STATE-FIRST (the first live test's lesson): the authoritative cursor tile is CurX/CurY/CurZ
// on this CDO. The game's XCom3DCursor is PUSHED to that tile (CursorSetLocation) so the
// pathing-pawn preview and the camera follow — but it is never read back for stepping: the
// cursor pawn sits at body-center heights and keeps moving between presses, so position→tile
// round-trips climbed one Z level per press and drifted tiles sideways.
//
// Directions are WORLD-ABSOLUTE compass cardinals (player decision 2026-08-19, replacing the
// earlier camera-relative scheme): Up arrow = north always. The compass is derived from the
// game's own GetCoverDirection mapping, so spoken cover edges, offsets, and steps all share
// the exact north the engine's cover flags mean.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessTileReader extends Object;

// Cardinal indices, matching the engine's cover-flag bit order (COVER_North = bit 0).
const DIR_NORTH = 0;
const DIR_SOUTH = 1;
const DIR_EAST = 2;
const DIR_WEST = 3;

// OUR compass: the world-axis deltas that "north" and "east" mean. The engine defines cover
// N/S/E/W natively but no script API documents the axis mapping — GetCoverDirection's
// out-value proved NOT to be a bit index (the first compass build keyed on it and shipped two
// dead arrows and no offsets, live-diagnosed 2026-08-19 via the dev server's speech ring). So
// the mapping is a fixed table, calibrated empirically against the cover words, which come
// straight from the documented flag-bit order: stand by a wall, read "High cover <word>",
// step toward <word> — the wall tile must be there. If mirrored, flip the constants below
// (the ONLY flip points; steps, offsets, and cover words all compose from them).
// Calibrated live 2026-08-19 against two crate faces (the cover flags are the oracle: a tile
// adjacent to an impassable crate names its crate-facing edge): engine north = +Y, engine
// east = -X. The first guess was rotated 180 degrees on both axes.
const NORTH_DX = 0;
const NORTH_DY = 1;
const EAST_DX = -1;
const EAST_DY = 0;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The authoritative cursor tile. Valid only while bPlanted for the current mission.
var int CurX, CurY, CurZ;

// Lazy plant: the first press in a mission plants the cursor on the active unit and reads that
// tile instead of acting, so a cold key never fires an action blind (RT TileExplorer parity).
var bool bPlanted;
var int PlantedMissionID;

// Two-press move-to: first press previews the path, second press on the SAME destination
// commits. Stateless beyond the tile — no timeout; stepping the cursor clears it.
var bool bMovePending;
var TTile PendingMoveTile;

// The floor LEVEL the player last heard (or our own code last set), CursorFloorPoll's change
// detector. -1 until the first observation. The game's m_iRequestedFloor is the truth; this
// only remembers what was spoken, so external changes — the mouse wheel, the game re-flooring
// on a soldier switch — announce exactly once. Every SetCursorTile adopts it silently. The
// level is a CEILING the cursor lands under, never a floor it is pinned to (LandingTile).
var int LastSpokenFloor;

// What the player last heard about buildings — DescribeIndoor's change detector, so "Inside
// building" and "Outside" are spoken on the crossing and never on every step. 0 = nothing heard
// yet (reset with every plant), 1 = outside, 2 = inside, 3 = on a roof. The building is
// remembered by its LOCATION, a value: the CDO rule forbids holding the volume itself.
var int LastIndoorState;
var int LastIndoorFloor;
var int LastIndoorFloorCount;
var vector LastIndoorBuildingPos;

// Whether the tile the player last landed on was in the squad's sight — the change detector
// behind the line-of-sight earcons (CueSight). 0 = nothing heard yet (reset with every plant),
// 1 = in sight, 2 = out of sight. Edge-triggered like DescribeIndoor: the cue marks the CROSSING,
// never the state, so a walk across open ground stays silent.
var int LastSightState;

// The cue keys, as XComSoundManager.PlaySoundEvent resolves them: the SoundCue object names in
// mods/X2AccessCompanion/Content/X2AccessSounds.upk (built by tools/make_sound_upk.py from the
// WAVs under Sounds/), registered under +SoundCuePaths in Config/XComGameData.ini.
const CUE_SIGHT_LOST = "X2Access_LOS_Lost";
const CUE_SIGHT_GAINED = "X2Access_LOS_Gained";

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// Step one tile toward a compass cardinal (DIR_* above).
function CursorStep(int Cardinal)
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Target;
	local int DX, DY;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (EnsurePlanted(TC, Cursor, WorldData))
		return;

	bMovePending = false;

	if (!CardinalDelta(Cardinal, DX, DY))
	{
		// Unreachable for the four cardinals, but a press must never be silent.
		NativeResult = "Bad direction" @ string(Cardinal);
		return;
	}
	Target.X = Clamp(CurX + DX, 0, WorldData.NumX - 1);
	Target.Y = Clamp(CurY + DY, 0, WorldData.NumY - 1);
	Target.Z = CurZ;
	if (Target.X == CurX && Target.Y == CurY)
	{
		NativeResult = class'X2AccessVocab'.default.MapEdge;
		return;
	}

	// Land where the mouse would: the highest surface of the target column at or below the
	// current floor level (LandingTile). Off a roof edge that is the ground, four tiles down;
	// up a slope it is the next tile up. The height change is spoken where a sighted player
	// watches the cursor take it — which inside the fog is not everywhere (VerticalDrawn).
	Target = LandingTile(Cursor, WorldData, Target.X, Target.Y, Cursor.m_iRequestedFloor);

	if (VerticalDrawn(WorldData, Target))
		NativeResult = HeightWord(CurZ, Target.Z);
	SetCursorTile(Cursor, WorldData, Target);
	AppendPart(NativeResult, DescribeIndoor(WorldData, Target, false));
	AppendPart(NativeResult, DescribeTile(Target, false));
}

// Step the floor LEVEL up (+1) or down (-1) by driving the game's OWN floor verbs — the same
// AscendFloor/DescendFloor the mouse wheel and the controller D-pad call. The floor is shared,
// load-bearing state with four consumers (the camera band, the building cutaway, WorldCascadeZ,
// and the pathing pawn's MaxZ destination clamp, XComPathingPawn:991), so an invented per-column
// Z hop that bypassed it left the pawn refusing tiles the reachable cache called Dash (live
// 2026-08-31, the evac-zone roof). One press = one 4-tile band, the game's own granularity.
// The level is a CEILING, not a target (LandingTile): after the game accepts the change the
// cursor re-lands on the highest surface of its column at or below the new level, which on open
// ground means it does not move at all and only the camera band does — exactly what a sighted
// player sees the wheel do. The only refusal in X2 is the game's own top/bottom clamp
// (DisableFloorChanges is hardwired false).
function CursorFloor(int Delta)
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Target;
	local int OldFloor;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (EnsurePlanted(TC, Cursor, WorldData))
		return;

	bMovePending = false;

	OldFloor = Cursor.m_iRequestedFloor;
	if (Delta > 0)
		Cursor.AscendFloor();
	else
		Cursor.DescendFloor();

	if (Cursor.m_iRequestedFloor == OldFloor)
	{
		if (Delta > 0)
			NativeResult = class'X2AccessVocab'.default.TopFloor;
		else
			NativeResult = class'X2AccessVocab'.default.BottomFloor;
		return;
	}

	Target = LandingTile(Cursor, WorldData, CurX, CurY, Cursor.m_iRequestedFloor);
	NativeResult = FloorWord(Cursor) $ ".";
	if (VerticalDrawn(WorldData, Target))
		AppendPart(NativeResult, HeightWord(CurZ, Target.Z));
	SetCursorTile(Cursor, WorldData, Target);
	AppendPart(NativeResult, DescribeIndoor(WorldData, Target, false));
	AppendPart(NativeResult, DescribeTile(Target, false));
}

// Passive poll — native calls this on its tick while the tile cursor owns world mode. When the
// game's floor changed OUTSIDE our own pushes (the mouse wheel, the game re-flooring on a
// soldier switch via MoveToUnit's GetIdealPawnFloor), speak it and re-land the planted cursor
// under the new ceiling, so the cursor can never sit above the level the pathing clamp keys on.
// Our own writes adopt LastSpokenFloor inside SetCursorTile and never echo here. Before the
// cursor is planted there is nothing to sync, and announcing the game's own camera housekeeping
// would be noise: adopt silently.
function CursorFloorPoll()
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Target;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (Cursor.m_iRequestedFloor == LastSpokenFloor)
		return;
	if (LastSpokenFloor < 0 || !bPlanted || PlantedMissionID != CurrentMissionID())
	{
		LastSpokenFloor = Cursor.m_iRequestedFloor;
		return;
	}

	bMovePending = false;
	Target = LandingTile(Cursor, WorldData, CurX, CurY, Cursor.m_iRequestedFloor);
	NativeResult = FloorWord(Cursor) $ ".";
	if (VerticalDrawn(WorldData, Target))
		AppendPart(NativeResult, HeightWord(CurZ, Target.Z));
	SetCursorTile(Cursor, WorldData, Target);
	AppendPart(NativeResult, DescribeIndoor(WorldData, Target, false));
	AppendPart(NativeResult, DescribeTile(Target, false));
}

// Recenter the cursor on the active unit and read its tile.
function CursorRecenter()
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local XComGameState_Unit UnitState;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (EnsurePlanted(TC, Cursor, WorldData))
		return;

	bMovePending = false;

	UnitState = ActiveUnitState();
	if (UnitState == none)
	{
		NativeResult = class'X2AccessVocab'.default.NoUnitSelected;
		return;
	}
	NativeResult = AdoptFloor(Cursor, WorldData, UnitState.TileLocation);
	SetCursorTile(Cursor, WorldData, UnitState.TileLocation);

	// No name prefix: DescribeTile's own DescribeUnitsOnTile leads with "Ally: <name>" for the
	// unit standing here, which on a recenter is always this one. Prefixing it said the name
	// three times in one line ("Shreya Bose. Ally: Shreya Bose. ... At Shreya Bose").
	AppendPart(NativeResult, DescribeIndoor(WorldData, UnitState.TileLocation, false));
	AppendPart(NativeResult, DescribeTile(UnitState.TileLocation, false));
}

// Re-read the cursor tile, verbose (adds coordinates, explicit "no cover").
function CursorAnnounce()
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Tile;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (EnsurePlanted(TC, Cursor, WorldData))
		return;

	Tile = CurTile();
	// Forced: the re-read states where the cursor stands, "Outside" included.
	NativeResult = DescribeIndoor(WorldData, Tile, true);
	AppendPart(NativeResult, DescribeTile(Tile, true));
}

// Two-press guarded move order. First press: path preview (tiles, action cost, destination
// cover). Second press on the same destination: the game's own click-to-move
// (XComTacticalInput.ClickToPath — includes the end-of-move melee behavior a real click has).
// Refuses, with the reason spoken, when the destination is out of range: a blind player must
// never issue a move that lands anywhere other than the tile they just heard.
function CursorMoveTo()
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local XComPathingPawn PathingPawn;
	local XComTacticalInput TacInput;
	local XGUnit ActiveUnit;
	local XComGameState_Unit UnitState;
	local TTile CursorTile, Dest;
	local float Cost;
	local string Desc;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (EnsurePlanted(TC, Cursor, WorldData))
		return;

	UnitState = ActiveUnitState();
	ActiveUnit = ActiveUnitVisualizer(UnitState);
	PathingPawn = TC.m_kPathingPawn;
	if (ActiveUnit == none || UnitState == none)
	{
		NativeResult = class'X2AccessVocab'.default.NoUnitSelected;
		return;
	}
	if (PathingPawn == none || PathingPawn.bHidden)
	{
		NativeResult = class'X2AccessVocab'.default.NoMoveAvailable;
		bMovePending = false;
		return;
	}

	CursorTile = CurTile();
	if (SameTile(CursorTile, UnitState.TileLocation))
	{
		NativeResult = class'X2AccessVocab'.default.AlreadyHere;
		bMovePending = false;
		return;
	}

	// The pathing pawn's destination is clamped to the reachable area; when it disagrees with
	// the cursor the game shows its out-of-range marker — we speak it instead, with the REASON,
	// because "Out of range" covers three different refusals a sighted player tells apart by
	// eye: a tile nobody can stop on (a crate top — the puck slides to the floor beside it), a
	// tile beyond the move range (the red marker), and a tile the pawn's floor ceiling clamped
	// (XComPathingPawn:991). Then where the puck actually went, which is the one fact the sighted
	// player reads off the screen here: the tile the game WOULD walk to, said as an offset from
	// the tile the player is standing on. Measured live 2026-09-16 (DescribeTile has the trace).
	Dest = PathingPawn.LastDestinationTile;
	if (!SameTile(Dest, CursorTile))
	{
		if (!WorldData.IsFloorTileAndValidDestination(CursorTile, UnitState))
			NativeResult = class'X2AccessVocab'.default.CannotStandThere;
		else if (ActiveUnit.m_kReachableTilesCache == none
			|| ActiveUnit.m_kReachableTilesCache.GetPathCostToTile(CursorTile) < 0)
			NativeResult = class'X2AccessVocab'.default.ReachOutOfRange;
		else
			NativeResult = class'X2AccessVocab'.default.CannotStopThere;
		Desc = OffsetWords(CursorTile, Dest);
		if (Desc != "")
			AppendPart(NativeResult, class'X2AccessVocab'.static.FillS(
				class'X2AccessVocab'.default.Nearest, Desc));
		bMovePending = false;
		return;
	}

	if (!bMovePending || !SameTile(PendingMoveTile, Dest))
	{
		// First press: preview. The path is the one the pathing pawn already built for this
		// cursor position — exactly what the game would run.
		Desc = class'X2AccessVocab'.static.Count(Max(PathingPawn.PathTiles.Length - 1, 0),
			class'X2AccessVocab'.default.Tiles_One, class'X2AccessVocab'.default.Tiles_Few,
			class'X2AccessVocab'.default.Tiles_Many);
		Cost = ActiveUnit.m_kReachableTilesCache.GetPathCostToTile(Dest);
		if (Cost >= 0 && Cost > ActiveUnit.GetMobility())
			AppendComma(Desc, class'X2AccessVocab'.default.ReachDash);
		// The path-marker warnings, BEFORE the commit prompt — a blind player must hear "breaks
		// concealment" while the second press is still theirs to withhold.
		class'X2AccessHelper'.static.AppendComma(Desc, DescribePathRisks(PathingPawn, UnitState));
		AppendPart(Desc, DescribeCoverEdges(WorldData.GetPositionFromTileCoordinates(Dest), true));
		// Who would see the soldier there and whether the cover holds — the eye markers and the
		// red shield the game draws on this very destination while the puck sits on it.
		AppendPart(Desc, DescribeSight(Dest, WorldData.GetPositionFromTileCoordinates(Dest)));
		AppendPart(Desc, class'X2AccessVocab'.default.PressAgainToMove);
		NativeResult = Desc;
		bMovePending = true;
		PendingMoveTile = Dest;
		return;
	}

	// Second press: commit through the game's own click path. Its own guards (visualizer
	// busy, melee-from-move, path validity) all still apply.
	bMovePending = false;
	TacInput = XComTacticalInput(TC.PlayerInput);
	if (TacInput != none && TacInput.ClickToPath())
		NativeResult = class'X2AccessVocab'.default.Moving;
	else
		NativeResult = class'X2AccessVocab'.default.CannotMoveThere;
}

// Left Alt — the game's target preview, spoken (DescribeTargetPreview has the parity case). The
// native side passes the key THROUGH rather than swallowing it, so the game's own strip rises
// under the same press and nothing vanilla is displaced.
//
// Answers about the planted cursor tile, the tile the last readout described. Before the cursor
// is planted it answers about the pathing pawn's own destination — literally what the strip on
// screen is showing — and plants nothing: this is a vanilla key, and planting would take the
// mouse over (lazy plant is lazy takeover, docs/surfaces/tactical.md). A hidden pawn (not the
// player's turn to move) falls back to the unit's own tile, the one place the question still
// has an answer.
function CursorTargetPreview()
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local XComPathingPawn PathingPawn;
	local XComGameState_Unit UnitState;
	local TTile Tile;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;

	if (bPlanted && PlantedMissionID == CurrentMissionID())
		Tile = CurTile();
	else
	{
		UnitState = ActiveUnitState();
		if (UnitState == none)
		{
			NativeResult = class'X2AccessVocab'.default.NoUnitSelected;
			return;
		}
		PathingPawn = TC.m_kPathingPawn;
		if (PathingPawn != none && !PathingPawn.bHidden)
			Tile = PathingPawn.LastDestinationTile;
		else
			Tile = UnitState.TileLocation;
	}
	NativeResult = DescribeTargetPreview(Tile, true);
}

// ---------------------------------------------------------------------------
// Tile description
// ---------------------------------------------------------------------------

// Compose the spoken line for a tile. Fog parity (RT TileExplorer): a never-seen tile reveals
// nothing but "unexplored" and its offset; an explored-but-not-visible tile reveals static
// layout (cover, objects, floor) but no live units or hazards; a visible tile reads in full.
private function string DescribeTile(TTile Tile, bool bVerbose)
{
	local XComWorldData WorldData;
	local vector TilePos;
	local string Result, UnitPart;
	local FOWTileStatus FOW;
	local byte GameplayFlags;
	local bool bImpassable, bNoFooting, bNoStop;

	WorldData = `XWORLD;
	TilePos = WorldData.GetPositionFromTileCoordinates(Tile);
	WorldData.GetTileFOWValue(Tile, FOW, GameplayFlags);

	if (FOW == eFOWTileStatus_NeverSeen)
	{
		Result = class'X2AccessVocab'.default.Unexplored;
		AppendPart(Result, DescribeOffset(Tile));
		return Result;
	}

	Result = "";
	UnitPart = "";
	if (FOW == eFOWTileStatus_Seen)
		UnitPart = class'X2AccessHelper'.static.DescribeUnitsOnTile(Tile);

	// Occupied is not impassable: a tile with a live unit on it fails CanUnitsEnterTile, and
	// speaking "Impassable" over an ally read as a bug in the first live test.
	// A drop is no longer a kind of impassable tile: the cursor follows the ground off a roof
	// edge (LandingTile) and the step speaks the height it took (HeightWord), so the tile this
	// describes is always a surface, and Impassable means a wall or an object on it.
	//
	// Passable is not standable. The game keeps two tests and the AI asks both
	// (XGAIBehavior:3439): CanUnitsEnterTile is "can a unit pass through", true for the top of a
	// crate a soldier climbs OVER; IsFloorTileAndValidDestination is "can a unit STOP here",
	// false for that same top. The mouse never rests on such a surface — the pick keeps, within
	// a floor band, a valid-destination hit over a non-valid one, so the ray slides off a crate
	// onto the floor behind it (XComTacticalHUD.GetMouseInteractionInterface). Our column model
	// cannot follow a ray, so the cursor lands on the crate top and has to SAY it cannot stand
	// there — measured live 2026-09-16: the reader called a full-cover crate's top "Dash"
	// (GetPathCostToTile is finite for anything on the path graph, climb-overs included) and the
	// move key then said "Out of range" for a tile the pawn had merely declined to stop on. The
	// object scanner's cover ring asks the same pair, so the two surfaces agree.
	bImpassable = (UnitPart == "" && !WorldData.CanUnitsEnterTile(Tile));
	bNoFooting = (UnitPart == "" && !bImpassable
		&& !WorldData.IsFloorTileAndValidDestination(Tile, ActiveUnitState()));
	bNoStop = bImpassable || bNoFooting;
	if (bImpassable)
		AppendPart(Result, class'X2AccessVocab'.default.Impassable);
	else if (bNoFooting)
		AppendPart(Result, class'X2AccessVocab'.default.CannotStandHere);
	AppendPart(Result, UnitPart);

	AppendPart(Result, DescribeInteractives(Tile));
	// The evac zone, ahead of cover: on the turn it matters, "am I standing in it" outranks
	// every other fact this line carries. Not fog-gated — the never-seen branch above already
	// returned, and inside explored ground the painted footprint is something a sighted player
	// simply sees. The oracle is X2AccessHelper's, shared with the object scanner's ring so the
	// two surfaces cannot disagree about where the zone is.
	AppendPart(Result, class'X2AccessHelper'.static.DescribeEvacZone(Tile));
	AppendPart(Result, DescribeCoverEdges(TilePos, bVerbose));
	// Straight after the cover it qualifies: who would see a soldier here, and whether that
	// cover holds against them. Tiles nobody can stop on skip it for the reason they skip
	// reachability.
	if (!bNoStop)
		AppendPart(Result, DescribeSight(Tile, TilePos));

	// The red detection overlay, per tile (docs/concealment-recon.md §3). No FOW gate of our own:
	// the game paints red over explored fog too, and a never-seen tile already returned above.
	// The helper itself goes silent the moment the active unit is not concealed — the same edge
	// on which the game hides the overlay. Impassable tiles skip it for the same reason they skip
	// reachability: nobody can stand there, so the warning has no move to warn about.
	if (!bNoStop)
		AppendPart(Result, class'X2AccessHelper'.static.DescribeDetection(ActiveUnitState(), Tile));

	if (FOW == eFOWTileStatus_Seen)
		AppendPart(Result, class'X2AccessHelper'.static.DescribeHazards(WorldData, Tile));

	if (WorldData.IsWaterTile(Tile))
		AppendPart(Result, class'X2AccessVocab'.default.Water);
	if (WorldData.IsRampTile(Tile))
		AppendPart(Result, class'X2AccessVocab'.default.Ramp);

	// "Impassable. Out of range." says one thing twice — a tile nobody can stand on has no
	// reachability to report, and this was the single noisiest line of the 2026-08-21 run
	// (~99 of them, against 23 actual moves). Sweeping the cursor along a wall should cost one
	// word per tile, not three.
	if (!bNoStop)
		AppendPart(Result, DescribeReachability(Tile));
	AppendPart(Result, DescribeOffset(Tile));

	if (bVerbose)
		AppendPart(Result, class'X2AccessVocab'.static.Fill3(
			class'X2AccessVocab'.default.TileCoords, Tile.X, Tile.Y, Tile.Z));

	// The game's own Gameplay option "Show Target Preview" keeps the strip up on every hover
	// instead of only while Alt is held; on, the strip rides every step here too. LAST, because
	// it is the long clause and everything before it is what a step is for.
	if (!bNoStop && TargetPreviewAlwaysOn())
		AppendPart(Result, DescribeTargetPreview(Tile, false));

	if (Result == "")
		Result = class'X2AccessVocab'.default.Clear;
	return Result;
}

// The profile flag behind the Gameplay option "Show Target Preview" (UIOptionsPCScreen ->
// m_bTargetPreviewAlwaysOn), the same one UITacticalHUD_EnemyPreview.CheckForHide reads.
private function bool TargetPreviewAlwaysOn()
{
	local XComOnlineProfileSettings Profile;

	Profile = `XPROFILESETTINGS;
	return Profile != none && Profile.Data.m_bTargetPreviewAlwaysOn;
}

// Who would see a soldier standing on Tile, and whether the tile's cover holds against them —
// the spoken form of two things the game draws the instant the path destination changes
// (XComPathingPawn.UpdatePathTileData): the EYE every visible enemy's flag gets when that enemy
// has line of sight to the destination (UIUnitFlagManager.RealizePreviewEndOfMoveLOS; the
// ShowLOSPreview config ships true, no key held), and the FlankedColor the destination's cover
// shield takes when a visible enemy would flank it (XComActionIconManager). Both are on screen
// for every hover, so both ride every step.
//
// The enemy set is the game's: GetAllEnemiesForLocation is what FillPathTileDataAndVisibleObjects
// runs per path tile, kept — as the game keeps it — to bVisibleBasic (clear LOS AND inside the
// enemy's sight radius: the eye means "they would see you", not "there is a line") and to
// enemies the squad can see. The game's filter for the latter is ObjectsVisibleToPlayer;
// CanXComSquadSeeTarget is the gate the detection readout and the scanner's enemy ring already
// use, so three surfaces agree about who counts. IsEnemyUnit is the hostility test rather than
// a team compare, because the location query returns everyone NOT on the soldier's team —
// civilians included — while the Lost, on their own team, must count.
//
// The flank test is XGUnitNativeBase.DoesFlankCover over the tile's own cover point — the
// function the AI runs when it weighs a spot (XGAIBehavior.uc:5060), handed the FLANKER's
// position. The shield's colouring is native and unreadable, so this is the nearest documented
// oracle rather than a copy of it; a first live pass should shape-count "flanked" against
// covered tiles, since a clause that fires on every covered tile or on none is this argument
// order being wrong.
//
// Silent at zero: the game draws no eye, and "Seen by 0" on every step of a quiet map is the
// noise the DescribeVisibleEnemies rule already forbids. "flanked" can only follow a count.
private function string DescribeSight(TTile Tile, vector TilePos)
{
	local XComWorldData WorldData;
	local XComGameStateHistory History;
	local XComGameState_Unit UnitState, Enemy;
	local X2CharacterTemplate Template;
	local array<GameRulesCache_VisibilityInfo> Viewers;
	local XComCoverPoint CP;
	local bool bHasCover, bFlanked;
	local int i, Count;
	local string Result;

	UnitState = ActiveUnitState();
	if (UnitState == none)
		return "";

	WorldData = `XWORLD;
	History = `XCOMHISTORY;
	bHasCover = WorldData.GetCoverPointAtFloor(TilePos, CP);

	class'X2TacticalVisibilityHelpers'.static.GetAllEnemiesForLocation(TilePos,
		UnitState.ControllingPlayer.ObjectID, Viewers);
	for (i = 0; i < Viewers.Length; i++)
	{
		if (!Viewers[i].bVisibleBasic)
			continue;
		Enemy = XComGameState_Unit(History.GetGameStateForObjectID(Viewers[i].SourceID));
		if (Enemy == none || !Enemy.IsAlive() || !UnitState.IsEnemyUnit(Enemy))
			continue;
		Template = Enemy.GetMyTemplate();
		if (Template != none && Template.bIsCosmetic)
			continue;
		if (!class'X2TacticalVisibilityHelpers'.static.CanXComSquadSeeTarget(Enemy.ObjectID))
			continue;
		Count++;
		if (bHasCover && !bFlanked
			&& class'XGUnitNativeBase'.static.DoesFlankCover(
				WorldData.GetPositionFromTileCoordinates(Enemy.TileLocation), CP))
			bFlanked = true;
	}

	if (Count == 0)
		return "";
	Result = class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.SeenBy, Count);
	if (bFlanked)
		AppendComma(Result, class'X2AccessVocab'.default.Flanked);
	return Result;
}

// The game's target preview — the strip UITacticalHUD_EnemyPreview fills for the path
// destination (UpdatePreviewTargets, reproduced clause for clause): which enemies could be shot
// from Tile, which only through squadsight, and which would be FLANKED from there. A sighted
// player sees it while Left Alt is held, or on every hover with the Gameplay option "Show Target
// Preview" on; it is spoken on exactly those two terms — Alt (CursorTargetPreview) and, with the
// option on, at the end of every step's readout.
//
// Read off the screen wherever the screen has it: the strip's own current target list is the
// starting set (the game starts from it too), the preview panel's StandardAbilityTarget and its
// LivingHostileGameplayVisibleFilter are the very objects the game validates with, and the
// heading is the panel's localized Label. Per target, the facts come from the same
// GetVisibilityInfoFromRemoteLocation query the panel runs. TargetCover is what it tests for the
// flanked marker — CT_None on a unit that can take cover, a unit in the open included, which is
// also what the to-hit calc calls flanked — and the other two values are spoken through the shot
// readout's own CoverWords, so the clause matches what the shot HUD will say the moment that
// target is selected from there.
//
// Greyed icons — enemies on the strip now that could NOT be shot from Tile — are "not
// targetable", because a sighted player sees them dim rather than gone. bSpeakEmpty: a pressed
// key owes an answer; the per-step line stays silent at zero, as the rest of it does.
private function string DescribeTargetPreview(TTile Tile, bool bSpeakEmpty)
{
	local XComPresentationLayer Pres;
	local UITacticalHUD HUD;
	local UITacticalHUD_EnemyPreview Preview;
	local X2GameRulesetVisibilityManager VisibilityMgr;
	local XComGameStateHistory History;
	local XComGameState_Unit UnitState, EnemyUnit;
	local XComGameState_BaseObject Target;
	local X2CharacterTemplate Template;
	local GameRulesCache_VisibilityInfo Info, EmptyInfo;
	local array<StateObjectReference> Targets, VisibleToPlayer;
	local array<int> AffIDs, AffSquadsight, AffCover;
	local string Result, Entry, Heading;
	local int i, j;

	UnitState = ActiveUnitState();
	if (UnitState == none)
		return bSpeakEmpty ? class'X2AccessVocab'.default.NoUnitSelected : "";

	Pres = `PRES;
	if (Pres == none)
		return "";
	HUD = Pres.GetTacticalHUD();
	if (HUD == none || HUD.m_kEnemyTargets == none)
		return "";
	Preview = UITacticalHUD_EnemyPreview(HUD.m_kEnemyPreview);
	if (Preview == none || Preview.StandardAbilityTarget == none)
		return "";

	History = `XCOMHISTORY;
	VisibilityMgr = `TACTICALRULES.VisibilityMgr;
	if (VisibilityMgr == none)
		return "";

	// Start with what the real strip shows from where the soldier stands now.
	Targets = HUD.m_kEnemyTargets.m_arrTargets;

	class'X2TacticalVisibilityHelpers'.static.GetAllVisibleObjectsForPlayer(
		UnitState.ControllingPlayer.ObjectID, VisibleToPlayer, , , true);
	for (i = 0; i < VisibleToPlayer.Length; i++)
	{
		Info = EmptyInfo;
		Info.SourceID = UnitState.ObjectID;
		Info.TargetID = VisibleToPlayer[i].ObjectID;
		if (!VisibilityMgr.GetVisibilityInfoFromRemoteLocation(UnitState.ObjectID, Tile,
			VisibleToPlayer[i].ObjectID, Info))
			continue;
		if (!Info.bClearLOS)
			continue;
		if (!VisibilityMgr.EvaluateConditions(Info, -1,
			class'UITacticalHUD_EnemyPreview'.default.LivingHostileGameplayVisibleFilter))
			continue;

		Target = History.GetGameStateForObjectID(VisibleToPlayer[i].ObjectID);
		if (Target == none)
			continue;
		EnemyUnit = XComGameState_Unit(Target);
		if (EnemyUnit != none)
		{
			Template = EnemyUnit.GetMyTemplate();
			if (Template != none && Template.bIsCosmetic)
				continue;
		}
		if (!Preview.StandardAbilityTarget.ValidatePrimaryTargetOptionFromRemoteLocation(
			UnitState, Tile, Target))
			continue;

		AffIDs.AddItem(Target.ObjectID);
		AffSquadsight.AddItem(Info.bVisibleBasic ? 0 : 1);
		AffCover.AddItem((EnemyUnit != none && EnemyUnit.CanTakeCover()) ? int(Info.TargetCover) : -1);
		if (Targets.Find('ObjectID', Target.ObjectID) == INDEX_NONE)
			Targets.AddItem(Target.GetReference());
	}

	Result = "";
	for (i = 0; i < Targets.Length; i++)
	{
		Target = History.GetGameStateForObjectID(Targets[i].ObjectID);
		if (Target == none)
			continue;
		EnemyUnit = XComGameState_Unit(Target);
		if (EnemyUnit != none)
			Entry = EnemyUnit.GetName(eNameType_Full);
		else
			Entry = class'X2AccessTacticalReader'.static.DescribeNonUnitTarget(Targets[i]);

		j = AffIDs.Find(Targets[i].ObjectID);
		if (j == INDEX_NONE)
			AppendComma(Entry, class'X2AccessVocab'.default.NotTargetable);
		else
		{
			if (AffSquadsight[j] != 0)
				AppendComma(Entry, class'X2AccessVocab'.default.ViaSquadsight);
			if (AffCover[j] == CT_None)
				AppendComma(Entry, class'X2AccessVocab'.default.Flanked);
			else if (AffCover[j] >= 0)
				AppendComma(Entry, class'X2AccessTacticalReader'.static.CoverWords(ECoverType(AffCover[j])));
		}
		AppendPart(Result, Entry);
	}

	if (Result == "")
	{
		if (!bSpeakEmpty)
			return "";
		Result = class'X2AccessVocab'.default.NoTargets;
	}

	// The panel's own heading ("TARGET PREVIEW:"), localized by the game. Verified populated: the
	// class that declares it is the section XComGame.<loc> ships it under.
	Heading = class'UITacticalHUD_EnemyPreview'.default.Label;
	if (Heading == "")
		Heading = class'X2AccessVocab'.default.TargetsHeading;
	AppendPart(Heading, Result);
	return Heading;
}

// The cursor tile's own cover, from the cover point the game keeps for it. The WORDS are
// CoverEdgeWords' — shared with the object scanner's cover ring, which names the same edges of
// the same tiles from the same flags, so the two surfaces cannot drift apart.
private function string DescribeCoverEdges(vector TilePos, bool bVerbose)
{
	local XComWorldData WorldData;
	local XComCoverPoint CP;
	local string Result;

	WorldData = `XWORLD;
	if (!WorldData.GetCoverPointAtFloor(TilePos, CP))
	{
		if (bVerbose)
			return class'X2AccessVocab'.default.NoCover;
		return "";
	}
	Result = CoverEdgeWords(CP.Flags);
	if (Result == "" && bVerbose)
		Result = class'X2AccessVocab'.default.NoCover;
	return Result;
}

// Per-edge cover in compass words, named directly by the engine's own cover-flag order
// (COVER_North bit 0 → "north", the matching height bit four bits up).
//
// ⛔ THE TWO BIT GROUPS ARE NOT ALTERNATIVES. COVER_<dir> means "this edge HAS cover";
// COVER_<dir>Low is a HEIGHT MODIFIER on it, set only when that same cover is low. A low-cover
// edge therefore has BOTH bits, and testing the direction bit first with an `else if` reports
// every covered edge as high — which is exactly what shipped: 260 "High cover" and not one
// "Low cover" across a whole Gatecrasher run (live 2026-08-21), on a map that is mostly cars
// and sandbags. The game's own reading is X2AbilityToHitCalc_StandardAim.GetCoverTypeForTile
// (:934), comment included: "if the cover flags in the specified direction don't have the low
// cover flag, then it is high cover". Presence first, height second — never one or the other.
//
// Static and public because the cover ring (X2AccessObjectScanner.AddCover) names the cover of
// tiles the cursor is NOT standing on, off the flags GetCoverPoints hands back rather than off a
// cover point looked up here — same fact, same words, one implementation. Same reasoning as
// InteractiveLabel over in the scanner: a shared read with no instance state costs nothing to
// share, and two vocabularies for one wall is how a player stops trusting the mod.
static function string CoverEdgeWords(int Flags)
{
	local string Result;
	local int Dir;

	Result = "";
	for (Dir = 0; Dir < 4; Dir++)
	{
		if ((Flags & (class'XComWorldData'.const.COVER_North << Dir)) == 0)
			continue; // no cover at all on this edge

		// FULL/HALF, not HIGH/LOW. The engine's flag is named "low" but the game's own UI, the
		// shot readout's DescribeTargetCover and the player all say full/half cover — and this
		// surface and that one describe the same wall. Two vocabularies for one fact is how you
		// get a player who cannot tell whether the mod is contradicting itself.
		class'X2AccessHelper'.static.AppendPart(Result,
			CoverEdgeWord((Flags & (class'XComWorldData'.const.COVER_NLow << Dir)) != 0, Dir));
	}
	return Result;
}

// One edge's words. A key per edge and height rather than a template over a direction word, so a
// translator can decline the direction ("с севера") instead of being handed the nominative.
private static function string CoverEdgeWord(bool bHalf, int Dir)
{
	switch (Dir)
	{
	case DIR_NORTH:
		return bHalf ? class'X2AccessVocab'.default.HalfCoverNorth
			: class'X2AccessVocab'.default.FullCoverNorth;
	case DIR_SOUTH:
		return bHalf ? class'X2AccessVocab'.default.HalfCoverSouth
			: class'X2AccessVocab'.default.FullCoverSouth;
	case DIR_EAST:
		return bHalf ? class'X2AccessVocab'.default.HalfCoverEast
			: class'X2AccessVocab'.default.FullCoverEast;
	case DIR_WEST:
		return bHalf ? class'X2AccessVocab'.default.HalfCoverWest
			: class'X2AccessVocab'.default.FullCoverWest;
	}
	return "";
}

// The canonical cover SIGNATURE of a tile: every covered edge's presence bit, that edge's low bit
// only when the edge actually has cover, and COVER_Diagonal — and nothing else. Two tiles share a
// signature exactly when they are the same KIND of place to stand, which is what the cover ring
// clusters on: a ten-tile sandbag line collapses to one entry, while a corner covered on two
// sides, or the point where a run turns half into full, stays its own.
//
// It is normalised rather than a raw `Flags & 0xFF` mask for two reasons. A low bit set on an
// edge with no presence bit is meaningless (the presence-first rule again) and would split a run
// that reads identically. And PEEK bits (0x0100-0x8000) are per-tile firing geometry, not cover:
// two tiles either side of the same crate face differ in them constantly, so masking them in
// would shatter every run into single cells.
//
// ⚠ COVER_Diagonal rides the signature but NOT the words. The flag means the tile's four cover
// edges are rotated 45° clockwise (local N = world NE), so a diagonal tile and a square one that
// both read `COVER_North` are facing different ways and must never merge into one spot. The WORDS
// are left alone deliberately: no script in all of XComGame reads COVER_Diagonal — the game's own
// GetCoverTypeForTile takes the N/S/E/W bits at face value — so speaking eight compass points
// here would put this surface at odds with the tile cursor and the shot readout, which speak
// four. Named in surfaces/tactical.md as an unverified gap rather than fixed on one surface.
static function int CoverSignature(int Flags)
{
	local int Dir, Sig;

	Sig = 0;
	for (Dir = 0; Dir < 4; Dir++)
	{
		if ((Flags & (class'XComWorldData'.const.COVER_North << Dir)) == 0)
			continue;
		Sig = Sig | (class'XComWorldData'.const.COVER_North << Dir);
		if ((Flags & (class'XComWorldData'.const.COVER_NLow << Dir)) != 0)
			Sig = Sig | (class'XComWorldData'.const.COVER_NLow << Dir);
	}
	if (Sig != 0)
		Sig = Sig | (Flags & class'XComWorldData'.const.COVER_Diagonal);
	return Sig;
}

// Doors, hackable objectives, loot — the static map features a sighted player recognizes on
// sight. Interactive objects are few per mission, so the history walk is cheap at key rate.
//
// The NAME is the object scanner's InteractiveLabel and the two clauses are the scanner's words,
// so the tile under the cursor and the same object in the objects ring cannot disagree (the
// two-surfaces-must-agree rule). Until the vocabulary moved into one place this read "Hackable
// object" for a door the ring called "Взломать дверь".
private function string DescribeInteractives(TTile Tile)
{
	local XComGameState_InteractiveObject IObj;
	local string Result, ObjName;

	foreach `XCOMHISTORY.IterateByClassType(class'XComGameState_InteractiveObject', IObj)
	{
		if (!SameTile(IObj.TileLocation, Tile))
			continue;
		ObjName = class'X2AccessObjectScanner'.static.InteractiveLabel(IObj);
		if (IObj.LockStrength > 0)
			AppendComma(ObjName, class'X2AccessVocab'.static.Fill1(
				class'X2AccessVocab'.default.LockedStrength, IObj.LockStrength));
		if (IObj.HasLoot())
			AppendComma(ObjName, class'X2AccessVocab'.default.HasLoot);
		AppendPart(Result, ObjName);
	}
	return Result;
}

// "1 action" / "Dash" / "Out of range" for the active unit — the spoken version of the
// blue/yellow move highlight, answered from the same cache the pathing pawn uses. Silent on a
// tile the unit could pass through but never stop on (a crate top): the cache prices every
// tile on the path graph, and "Dash" over one of those is what sent a player to the move key
// for a tile the pawn would only ever slide off (DescribeTile has the measurement).
private function string DescribeReachability(TTile Tile)
{
	local XComWorldData WorldData;
	local XGUnit ActiveUnit;
	local XComGameState_Unit UnitState;
	local float Cost;

	UnitState = ActiveUnitState();
	if (UnitState == none)
		return "";
	if (SameTile(Tile, UnitState.TileLocation))
		return class'X2AccessVocab'.default.ReachYourTile;
	WorldData = `XWORLD;
	if (WorldData == none || !WorldData.IsFloorTileAndValidDestination(Tile, UnitState))
		return "";
	ActiveUnit = ActiveUnitVisualizer(UnitState);
	if (ActiveUnit == none || ActiveUnit.m_kReachableTilesCache == none)
		return "";
	Cost = ActiveUnit.m_kReachableTilesCache.GetPathCostToTile(Tile);
	if (Cost < 0)
		return class'X2AccessVocab'.default.ReachOutOfRange;
	if (Cost > ActiveUnit.GetMobility())
		return class'X2AccessVocab'.default.ReachDash;
	return class'X2AccessVocab'.default.ReachOneAction;
}

// The spoken form of the path preview's warning icons (docs/concealment-recon.md §3): "noise"
// where the game would pin its noise marker, "breaks concealment" where it would pin the eye.
// Both from the game's own primitives rather than the pawn's marker arrays, which are private
// to script — and one hover STALE at step time anyway, since the pawn rebuilds its path on its
// own Tick after our cursor push. By the time this runs, CursorMoveTo has already proven the
// path settled (LastDestinationTile agreed with the cursor).
//
// Both are spoken only while the moving unit is concealed. Revealed, a window on the path is
// something a sighted player simply sees — and our tile readout already names it — so pushing
// "noise" on every routine dash through glass is chatter, not parity. Destruction implies the
// break directly (XComGameState_Unit.uc:8940 — no enemy required), so the per-tile oracle is
// only consulted when no smashed window already answered.
private function string DescribePathRisks(XComPathingPawn PathingPawn, XComGameState_Unit UnitState)
{
	local XComWorldData WorldData;
	local XComDestructibleActor Destructible;
	local array<TTile> Tiles;
	local string Result;
	local bool bLarge, bNoise, bBreaks;
	local int i, Kind;

	if (UnitState == none || !UnitState.IsConcealed())
		return "";

	WorldData = `XWORLD;
	Tiles = PathingPawn.PathTiles;
	bLarge = UnitState.GetMyTemplate().UnitSize > 1;

	// Pairwise traversal destructibles — the same per-edge query the game runs when it marks the
	// real move's Destructibles at click time (XComTacticalController.uc:1230), '_Pristine'
	// meaning "still intact, this traversal smashes it".
	for (i = 1; i < Tiles.Length; i++)
	{
		Destructible = XComDestructibleActor(
			WorldData.GetPathObjectFromTraversalToNeighbor(Tiles[i - 1], Tiles[i], bLarge));
		if (Destructible != none && Destructible.IsInState('_Pristine'))
		{
			bNoise = true;
			break;
		}
	}

	bBreaks = bNoise;
	for (i = 1; i < Tiles.Length && !bBreaks; i++)
	{
		bBreaks = class'X2AccessHelper'.static.TileBreaksConcealment(
			UnitState, Tiles[i], Kind);
	}

	if (bBreaks)
		Result = class'X2AccessVocab'.default.BreaksConcealment;
	if (bNoise)
		class'X2AccessHelper'.static.AppendComma(Result, class'X2AccessVocab'.default.Noise);
	return Result;
}

// Offset from the active unit in compass words ("3 north, 2 east, 1 up"). Always spoken — it
// proves the step registered even when the tile itself has nothing to say.
private function string DescribeOffset(TTile Tile)
{
	local XComGameState_Unit UnitState;

	UnitState = ActiveUnitState();
	if (UnitState == none)
		return "";

	// DescribeReachability already answered this tile with "Your tile", and it is appended just
	// ahead of us on every path that reaches here. Naming the unit a second time ("At Shreya
	// Bose") said the same fact in different words — and on a recenter, the third time in one line.
	if (SameTile(Tile, UnitState.TileLocation))
		return "";
	// The UP/DOWN leg is a terrain fact and takes the same gate the step's own height word takes:
	// the unit's tile is known, so "2 up" states the cursor's absolute Z. The bearing itself is
	// never gated — the player drives the cursor, so where they put it is theirs to know
	// (WH40KRTAccess, rt-visual-parity). The gate lives HERE rather than at the two call sites
	// so a third one cannot be added without it.
	return OffsetWords(UnitState.TileLocation, Tile, !VerticalDrawn(`XWORLD, Tile));
}

// The compass words for one tile relative to another ("3 north, 2 east, 1 up"); empty when the
// two are the same tile. Public and static because the object scanner speaks the same bearings
// about the same world — sharing this keeps ONE calibration of NORTH_DX/EAST_DX (see the header),
// so the two surfaces can never drift into disagreeing about which way north is.
//
// bOmitVertical drops the up/down leg only — the caller's answer to "does the game draw this
// tile's elevation" (VerticalDrawn). It is a parameter rather than a second function for the same
// reason the whole thing is shared: the vertical clause is composed in exactly one place.
static function string OffsetWords(TTile FromTile, TTile ToTile, optional bool bOmitVertical)
{
	local int DX, DY, DZ, NDX, NDY, EDX, EDY, NorthAmt, EastAmt;
	local string Result;

	DX = ToTile.X - FromTile.X;
	DY = ToTile.Y - FromTile.Y;
	DZ = bOmitVertical ? 0 : ToTile.Z - FromTile.Z;
	if (DX == 0 && DY == 0 && DZ == 0)
		return "";

	if (!CardinalDelta(DIR_NORTH, NDX, NDY) || !CardinalDelta(DIR_EAST, EDX, EDY))
		return "";
	NorthAmt = DX * NDX + DY * NDY;
	EastAmt = DX * EDX + DY * EDY;

	Result = "";
	if (NorthAmt > 0)
		Result = class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.OffsetNorth, NorthAmt);
	else if (NorthAmt < 0)
		Result = class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.OffsetSouth, -NorthAmt);
	if (EastAmt > 0)
		AppendComma(Result, class'X2AccessVocab'.static.Fill1(
			class'X2AccessVocab'.default.OffsetEast, EastAmt));
	else if (EastAmt < 0)
		AppendComma(Result, class'X2AccessVocab'.static.Fill1(
			class'X2AccessVocab'.default.OffsetWest, -EastAmt));
	if (DZ > 0)
		AppendComma(Result, class'X2AccessVocab'.static.Fill1(
			class'X2AccessVocab'.default.OffsetUp, DZ));
	else if (DZ < 0)
		AppendComma(Result, class'X2AccessVocab'.static.Fill1(
			class'X2AccessVocab'.default.OffsetDown, -DZ));
	return Result;
}

// ---------------------------------------------------------------------------
// Shared plumbing
// ---------------------------------------------------------------------------

private function bool GetContext(out XComTacticalController TC, out XCom3DCursor Cursor,
	out XComWorldData WorldData)
{
	TC = TacCtrl();
	if (TC == none)
		return false;
	Cursor = TC.GetCursor();
	WorldData = `XWORLD;
	return Cursor != none && WorldData != none;
}

static function XComTacticalController TacCtrl()
{
	return XComTacticalController(class'WorldInfo'.static.GetWorldInfo().GetALocalPlayerController());
}

// The selected unit's state. GetActiveUnit() merely returns the controller's
// ControllingUnitVisualizer VAR, which lags or sits empty in stretches of a mission (the first
// live test lost every offset and reachability line to it) — the ControllingUnit
// StateObjectReference on the same controller is the durable source, so fall back to it.
static function XComGameState_Unit ActiveUnitState()
{
	local XComTacticalController TC;
	local XGUnit ActiveUnit;
	local int ObjectID;

	TC = TacCtrl();
	if (TC == none)
		return none;
	ActiveUnit = TC.GetActiveUnit();
	if (ActiveUnit != none)
		ObjectID = ActiveUnit.ObjectID;
	else
		ObjectID = TC.ControllingUnit.ObjectID;
	if (ObjectID <= 0)
		return none;
	return XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(ObjectID));
}

// The selected unit's visualizer actor (owns the reachable-tiles cache), tolerant of the
// controller's visualizer var being stale: resolve through the unit state when needed.
static function XGUnit ActiveUnitVisualizer(XComGameState_Unit UnitState)
{
	local XComTacticalController TC;
	local XGUnit ActiveUnit;

	TC = TacCtrl();
	if (TC != none)
	{
		ActiveUnit = TC.GetActiveUnit();
		if (ActiveUnit != none)
			return ActiveUnit;
	}
	if (UnitState != none)
		return XGUnit(UnitState.GetVisualizer());
	return none;
}

// Plant the cursor on the active unit's OWN TILE (UnitState.TileLocation — never the pawn's
// body-center position, which reads one Z level high) on the first press of a mission, and
// read that tile instead of acting. Returns true when this call did the planting.
private function bool EnsurePlanted(XComTacticalController TC, XCom3DCursor Cursor,
	XComWorldData WorldData)
{
	local XComGameState_Unit UnitState;
	local int MissionID;

	MissionID = CurrentMissionID();
	if (bPlanted && MissionID == PlantedMissionID)
		return false;

	UnitState = ActiveUnitState();
	if (UnitState == none)
	{
		NativeResult = class'X2AccessVocab'.default.NoUnitSelected;
		return true; // consumed: spoke the refusal, nothing planted
	}

	bPlanted = true;
	PlantedMissionID = MissionID;
	bMovePending = false;
	LastIndoorState = 0;
	LastSightState = 0;

	NativeResult = class'X2AccessVocab'.default.CursorOn;
	AppendPart(NativeResult, AdoptFloor(Cursor, WorldData, UnitState.TileLocation));
	SetCursorTile(Cursor, WorldData, UnitState.TileLocation);
	AppendPart(NativeResult, DescribeIndoor(WorldData, UnitState.TileLocation, false));
	AppendPart(NativeResult, DescribeTile(UnitState.TileLocation, false));
	return true;
}

// Plant the cursor on the object scanner's current marker selection and read the tile out.
//
// The scanner owns the selection and this class owns the cursor, so the hand-off is a cross-class
// default READ — the one direction UnrealScript allows (writing another class's defaults does not
// compile for non-config properties, which is why this lives here and not in the scanner).
//
// Deliberately routed through the same SetCursorTile as every arrow step, so the pathing-pawn
// preview, the camera follow and the C++ mouse gate behave identically whether the player walked
// the cursor here or jumped it here — and so Backspace's two-press move-to works immediately after.
function CursorGoToMarker()
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Target;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;

	if (!class'X2AccessObjectScanner'.default.bSelValid)
	{
		NativeResult = class'X2AccessVocab'.default.NoMarkerSelected;
		return;
	}

	Target.X = Clamp(class'X2AccessObjectScanner'.default.SelX, 0, WorldData.NumX - 1);
	Target.Y = Clamp(class'X2AccessObjectScanner'.default.SelY, 0, WorldData.NumY - 1);
	Target.Z = Clamp(class'X2AccessObjectScanner'.default.SelZ, 0, WorldData.NumZ - 1);

	bPlanted = true;
	PlantedMissionID = CurrentMissionID();
	bMovePending = false;
	LastIndoorState = 0;
	LastSightState = 0;
	NativeResult = class'X2AccessVocab'.default.CursorJumped;
	AppendPart(NativeResult, AdoptFloor(Cursor, WorldData, Target));
	SetCursorTile(Cursor, WorldData, Target);
	AppendPart(NativeResult, DescribeIndoor(WorldData, Target, false));
	AppendPart(NativeResult, DescribeTile(Target, false));
}

private static function int CurrentMissionID()
{
	local XComGameState_BattleData BattleData;

	BattleData = XComGameState_BattleData(
		`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData', true));
	return (BattleData != none) ? BattleData.m_iMissionID : -1;
}

// Adopt a tile as the authoritative cursor state and push the game's 3D cursor onto it —
// exact tile center, snap search OFF. Every historical "the push never lands" measurement
// was the game's own per-frame mouse re-push (XComTacticalInput.Mouse_CheckForPathing)
// reverting it one frame later; the C++ mouse gate now suppresses that while the tile
// cursor owns world mode, and with it gone an exact push is strictly better than a snapped
// one: GetClosestValidCursorPosition slid a floor-up push two tiles sideways (measured live
// 2026-08-20), and the pathing pawn must preview exactly the tile the reader spoke.
private function SetCursorTile(XCom3DCursor Cursor, XComWorldData WorldData, TTile Tile)
{
	local vector Pos;

	CurX = Tile.X;
	CurY = Tile.Y;
	CurZ = Tile.Z;
	// Free the chain the way the game itself does (MoveToUnit), by zeroing the chain
	// distances — NOT by clearing ChainedPawn, which makes CursorMode_NoCollision.Tick abort
	// its own state every frame. ProcessChainedDistance clamps only while a distance is > 0
	// (decompiled at VA 0x140b616e0); a stale positive m_fMaxChainedDistance is what
	// collapsed earlier pushes back onto the unit. Our keys stand down while the shot HUD is
	// raised, so this can never loosen an active targeting method's own clamp.
	Cursor.m_fMaxChainedDistance = 0;
	Cursor.m_fMinChainedDistance = 0;
	Pos = WorldData.GetPositionFromTileCoordinates(Tile);
	// The floor LEVEL (m_iRequestedFloor) is not written here. It is a ceiling the caller has
	// already honoured — LandingTile never picks a surface above it while one exists at or
	// below — and XComPathingPawn:991 caps its destination search at that ceiling whenever the
	// cursor is inside a building volume, so a cursor ABOVE it is the one contradiction the pawn
	// punishes (measured live 2026-08-31: cursor at Z=13, pawn destination Z=9, level 1, the
	// move key refusing a tile the reachable cache itself called Dash). A cursor below the
	// level is what a mouse player produces every time they hover the ground with the wheel
	// left on a roof, and the pawn accepts it. The level moves only where the game's own moves
	// it: the floor keys (AscendFloor/DescendFloor), a soldier switch, and an explicit jump
	// (AdoptFloor). CursorSnapToFloor is still called so the camera band, the building-cutdown
	// height and the effective floor are re-derived from the level — on the mouse cursor it
	// moves nothing (XCom3DCursorMouse:11). The exact position is pushed LAST, so a base-class
	// snap (which does move the cursor) could never displace the tile the reader spoke.
	Cursor.CursorSnapToFloor(Cursor.m_iRequestedFloor, eCursorSearch_ExactFloor);
	LastSpokenFloor = Cursor.m_iRequestedFloor;
	Cursor.CursorSetLocation(Pos, false, false);
	// Here rather than beside each DescribeIndoor call: every way the cursor lands on a tile —
	// a step, a floor key, the floor poll, a plant, a recenter, the scanner marker — passes
	// through this one function, and a crossing missed on any of them is a silent bug.
	CueSight(WorldData, Tile);
	CueWalls(WorldData, Tile);
}

// The wall tones (X2AccessWallTones has the design): every landing re-aims the four looping
// voices at the walls around the tile. The actor is spawned on the first landing of a mission
// and found by iteration after — never held here, this being a CDO. Nothing is spawned while
// the mode is Off, so a player who turned the bed off pays nothing for it.
private function CueWalls(XComWorldData WorldData, TTile Tile)
{
	local X2AccessWallTones Tones;

	if (class'X2AccessWallTones'.default.Mode == 0)
		return;
	Tones = class'X2AccessWallTones'.static.Get(true);
	if (Tones != none)
		Tones.OnCursorTile(WorldData, Tile);
}

// The line-of-sight earcon: the cursor crossed OUT of the squad's sight (a short, higher tone)
// or back INTO it (a longer, lower one) — WH40KRTAccess's fog cue, on the same two sounds.
// Sighted parity: this is the fog-of-war overlay itself, drawn on every tile at every moment,
// so the crossing is exactly what the eye sees when the cursor slides under the fog. "In
// sight" is eFOWTileStatus_Seen, the same status DescribeTile gates live units on, so the tone
// and the words can never disagree about a tile.
//
// Played through the game's own sound manager (the Highlander's SoundCue mapping, Issue #10),
// not through the screen reader: an earcon is not speech, and the game's mixer is where a
// player's volume settings live. Fired BEFORE the step's words are spoken — the call returns
// immediately and native speaks NativeResult after — which is the order RT plays it in.
//
// Silent when no sound manager is up (the sound managers are spawned per world; between
// worlds there is nothing to ask) and on the first tile after a plant, which adopts the
// state without a tone: the planted tile is the player's own soldier, always in sight, and a
// tone there would announce nothing.
private function CueSight(XComWorldData WorldData, TTile Tile)
{
	local FOWTileStatus FOW;
	local byte GameplayFlags;
	local XComSoundManager SoundMgr;
	local int Here;

	WorldData.GetTileFOWValue(Tile, FOW, GameplayFlags);
	Here = (FOW == eFOWTileStatus_Seen) ? 1 : 2;
	if (LastSightState != 0 && Here != LastSightState)
	{
		SoundMgr = `SOUNDMGR;
		if (SoundMgr != none)
			SoundMgr.PlaySoundEvent(Here == 2 ? CUE_SIGHT_LOST : CUE_SIGHT_GAINED);
		else
			`log("X2Access: sight cue skipped, no sound manager", , 'X2Access');
	}
	LastSightState = Here;
}

// An explicit jump — plant, recenter, the scanner marker — adopts the target tile's own floor
// as the level, the way the game's own soldier switch does (CursorMode_NoCollision.Reset →
// GetIdealPawnFloor), so a marker on a roof is reachable without a floor key first. Returns the
// spoken level when it changed, "" otherwise: a silently moved ceiling would make the next
// step's landing unexplainable. Called BEFORE SetCursorTile, which re-derives the camera band
// from the level.
private function string AdoptFloor(XCom3DCursor Cursor, XComWorldData WorldData, TTile Tile)
{
	local int NewFloor;

	NewFloor = Cursor.WorldZToFloor(WorldData.GetPositionFromTileCoordinates(Tile));
	if (NewFloor == Cursor.m_iRequestedFloor)
		return "";
	Cursor.m_iRequestedFloor = NewFloor;
	return FloorWord(Cursor) $ ".";
}

private function TTile CurTile()
{
	local TTile T;

	T.X = CurX;
	T.Y = CurY;
	T.Z = CurZ;
	return T;
}

// The tile the cursor lands on in column (X, Y) with the floor level at FloorIdx — the mouse
// trace's rule, transcribed from XComTacticalHUD.GetMouseInteractionInterface: of every surface
// the pick ray crosses it keeps "the first actor that is at or below the requested floor, unless
// there are no hits below our requested floor, in which case we take the lowest floor above".
// So the level is a CEILING: with the wheel left on a roof, hovering open ground picks the
// ground, a shed picks its roof, a three-storey block picks its second floor. The same three
// answers here — the highest floor tile whose band is at or below the level, else the lowest
// floor tile above it (the move key will refuse that one as the pawn clamps it, as it does for
// a mouse player), else, for a column with no surface at all, the current height, left hanging
// so it reads Impassable. Band membership is asked of the engine (WorldZToFloor) per tile
// rather than derived from the 4-tiles-per-band constant, so a map with an offset ground plane
// cannot skew it.
private function TTile LandingTile(XCom3DCursor Cursor, XComWorldData WorldData, int X, int Y,
	int FloorIdx)
{
	local TTile T;
	local int Z, BestBelow, BestAbove;

	BestBelow = -1;
	BestAbove = -1;
	T.X = X;
	T.Y = Y;
	for (Z = 0; Z < WorldData.NumZ; Z++)
	{
		T.Z = Z;
		if (!WorldData.IsFloorTile(T))
			continue;
		if (Cursor.WorldZToFloor(WorldData.GetPositionFromTileCoordinates(T)) <= FloorIdx)
			BestBelow = Z; // rising Z, so the last hit is the highest at or below
		else
		{
			BestAbove = Z; // the first hit above is the lowest above
			break;
		}
	}
	if (BestBelow >= 0)
		T.Z = BestBelow;
	else if (BestAbove >= 0)
		T.Z = BestAbove;
	else
		T.Z = Clamp(CurZ, 0, WorldData.NumZ - 1);
	return T;
}

// Does the GAME draw this tile's elevation? The fog gate for every vertical word this class
// speaks — the step's height change and the up/down leg of an offset.
//
// A never-seen tile is painted FogColor (R=5,G=5,B=25,A=255) OPAQUE — measured on the live
// XComFOWEffect, whose voxel grid is exactly one voxel per tile, and confirmed in pixels against
// a screenshot (surfaces/tactical.md, the fog proof). Its geometry is invisible, so the height
// our LandingTile reads out of the column is a fact the game shows nobody — except that the fog
// has a deliberate hole in it, and the hole is the exact shape of this test.
//
// Firaxis added bTranslucentIgnoreFOW to PrimitiveComponent (Engine/PrimitiveComponent.uc:248,
// marked FIRAXIS ADDITION) and set it on precisely the primitives that must stay visible over
// unexplored ground: XComPathingPawn's destination puck, its circle, the melee puck, the
// out-of-range puck and the path ribbon (:1721 .. :1918), plus XComMovementGridComponent:114,
// the move-range border. So a sighted player who drops the puck on a never-seen tile DOES see
// its elevation — the puck sits on the hidden surface and the ribbon climbs to it.
//
// The hole ends where the puck does. XComPathingPawn clamps its destination to the reachable
// area, and the marker it draws at the CURSOR instead is controller-only (:1228 gates it on the
// ISCONTROLLERACTIVE macro): on mouse and keyboard a cursor past the unit's reach is marked by
// nothing at all. Hence the two conditions, which are the same pair CursorMoveTo already asks
// before it will issue a move — can a unit STOP here, and is it priced by the reachable cache.
// The pawn's own LastDestinationTile is NOT asked: it is rebuilt on the pawn's Tick, one frame
// behind our push, so at step time it still answers about the previous tile (the same lag
// DescribeIndoor documents for XComPawnIndoorOutdoorInfo).
//
// ⚠ The answer therefore depends on WHO is selected: the same fogged tile speaks its height for
// a soldier who can reach it and goes quiet for one out of moves. That is what the screen does —
// the border and the puck are drawn for the active unit or not at all — but it reads like a bug
// in a speech ring, so a shape-count over a run will show the same tile both ways.
private function bool VerticalDrawn(XComWorldData WorldData, TTile Tile)
{
	local XComGameState_Unit UnitState;
	local XGUnit ActiveUnit;
	local FOWTileStatus FOW;
	local byte GameplayFlags;

	if (WorldData == none)
		return false;
	WorldData.GetTileFOWValue(Tile, FOW, GameplayFlags);
	if (FOW != eFOWTileStatus_NeverSeen)
		return true;   // explored ground is drawn, tinted 50% grey — its shape included

	UnitState = ActiveUnitState();
	if (UnitState == none)
		return false;
	if (!WorldData.IsFloorTileAndValidDestination(Tile, UnitState))
		return false;
	ActiveUnit = ActiveUnitVisualizer(UnitState);
	if (ActiveUnit == none || ActiveUnit.m_kReachableTilesCache == none)
		return false;
	return ActiveUnit.m_kReachableTilesCache.GetPathCostToTile(Tile) >= 0;
}

// The height change a step or a floor key made the cursor take, in tiles — "Up 1" for a stair,
// "Down 4" off a roof onto the ground, "" for none. A sighted player watches the cursor drop; a
// blind one hears it, and this is the whole cue that an edge was crossed now that the cursor
// follows the ground instead of hanging over the drop. ⛔ Every caller gates on VerticalDrawn:
// inside the fog this cue is not always one the game gives.
private static function string HeightWord(int FromZ, int ToZ)
{
	if (ToZ > FromZ)
		return class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.HeightUp, ToZ - FromZ);
	if (ToZ < FromZ)
		return class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.HeightDown, FromZ - ToZ);
	return "";
}

// "Floor N of M" in the game's own numbering — m_iRequestedFloor is what the camera band, the
// cutaway and the pathing clamp all key on, so the spoken number is the one every consumer
// agrees about. Read AFTER SetCursorTile, which re-derives it from the landed tile.
private function string FloorWord(XCom3DCursor Cursor)
{
	return class'X2AccessVocab'.static.Fill2(class'X2AccessVocab'.default.FloorOf,
		Cursor.m_iRequestedFloor, Cursor.GetMaxFloor());
}

// Where the cursor stands relative to buildings — "Inside building, floor 2 of 3" on entering,
// "Building floor 3 of 3" on changing floor within it, "On roof" on climbing onto it, "Outside"
// on leaving — spoken on the crossing and otherwise "", the way the game's own cutaway redraws
// only when the cursor passes a wall. bForce says the current state unconditionally (the verbose
// re-read). The first observation after a plant speaks only when inside: outside is what a
// player assumes.
//
// The oracle is the game's own, XComPawnIndoorOutdoorInfo.CheckForFloorVolumeEvents: inside
// means "in a FLOOR volume" (a building volume alone is outside — its comment says so), the
// building is the floor volume's CachedBuildingVolume, a building nested inside another wins
// over the outer one, and the floor is the HIGHEST floor number among the floor volumes touched.
// That object is maintained by the cursor pawn's Touch events, which land on the frame AFTER a
// push, so reading it here would trail one step behind. The same rule is asked of the same
// volumes with EncompassesPoint at the tile just adopted — synchronous, and state-first like the
// rest of this class. The walk goes floor volume first, exactly as the tracker does, because a
// modular block's sub-volumes (XComModularBuildingVolume) still list floor volumes of their own
// with no cached building and no cached band heights: walking buildings first could land on one
// of those and count its floors instead of the merged XComMegaBuildingVolume the game credits.
// A cached building whose author turned IsInside off (the whole-level volume of a tunnel plot)
// is not a building, as the native IsInside() also holds. The floor count is the building's own
// Floors array, NOT the plot-wide band count FloorWord speaks — the two number different things,
// hence the different phrasing.
//
// The roof: a modular block's LAST band is not a storey but the space over its roof — measured
// on the Abandoned mega block as a 784..2976 band over three 256-unit storeys, its volumes flagged
// no differently from a storey's (next-steps, "Inside / outside on the tile cursor"). The game's
// own IsOnRoof is "top band of a multi-band building", which a plain two-storey shop, whose two
// bands are both storeys, would fail. So the roof is recognized by HEIGHT: a last band taller
// than two storeys (fExtent is a half-height; CursorFloorHeight is one storey) is dropped from
// the count and spoken as its own state.
private function string DescribeIndoor(XComWorldData WorldData, TTile Tile, bool bForce)
{
	local WorldInfo WI;
	local XComBuildingVolume BV, Found;
	local XComFloorVolume FV;
	local vector Pos, BuildingPos;
	local int FloorNum, OrphanFloor, Count, Here, Last;
	local bool bChanged, bNewBuilding, bRoofBand;
	local string Result;
	local FOWTileStatus FOW;
	local byte GameplayFlags;

	// Fog parity, the same gate DescribeTile's "Unexplored" branch applies: a never-seen tile
	// reveals nothing, walls included. The change detector is left untouched, so the crossing
	// is still spoken on the first EXPLORED tile after the fog, compared with the last one
	// before it.
	WorldData.GetTileFOWValue(Tile, FOW, GameplayFlags);
	if (FOW == eFOWTileStatus_NeverSeen)
		return "";

	Pos = WorldData.GetPositionFromTileCoordinates(Tile);
	WI = class'WorldInfo'.static.GetWorldInfo();
	foreach WI.AllActors(class'XComFloorVolume', FV)
	{
		if (!FV.EncompassesPoint(Pos))
			continue;
		BV = FV.CachedBuildingVolume;
		if (BV == none)
		{
			// A floor with no building behind it: inside, of something the map does not name.
			if (FV.FloorNumber > OrphanFloor)
				OrphanFloor = FV.FloorNumber;
			continue;
		}
		if (!BV.IsInside)
			continue;
		// One building at a time, the nested one winning over the outer, as the game resolves it.
		if (Found == none || (BV.m_bIsInternalBuilding && !Found.m_bIsInternalBuilding))
		{
			Found = BV;
			FloorNum = FV.FloorNumber;
		}
		else if (BV == Found && FV.FloorNumber > FloorNum)
			FloorNum = FV.FloorNumber;
	}

	if (Found != none)
	{
		Here = 2;
		Count = Found.Floors.Length;
		BuildingPos = Found.Location;
		Last = Found.Floors.Length - 1;
		bRoofBand = Last > 0
			&& Found.Floors[Last].fExtent > class'XCom3DCursor'.default.CursorFloorHeight;
		if (bRoofBand)
		{
			Count--;
			if (FloorNum > Count)
				Here = 3;
		}
	}
	else if (OrphanFloor > 0)
	{
		Here = 2;
		FloorNum = OrphanFloor;
		Count = 0;
	}
	else
	{
		Here = 1;
		FloorNum = 0;
		Count = 0;
	}

	bNewBuilding = Here >= 2 && (LastIndoorState != Here || BuildingPos != LastIndoorBuildingPos);
	bChanged = Here != LastIndoorState || bNewBuilding
		|| (Here == 2 && (FloorNum != LastIndoorFloor || Count != LastIndoorFloorCount));
	if (LastIndoorState == 0 && Here == 1 && !bForce)
		bChanged = false;

	if (bForce || bChanged)
	{
		if (Here == 1)
			Result = class'X2AccessVocab'.default.Outside;
		else if (Here == 3)
			Result = class'X2AccessVocab'.default.OnRoof;
		else if (bForce || bNewBuilding)
		{
			if (Found != none && Found.IsUfo)
				Result = class'X2AccessVocab'.default.InsideUFO;
			else if (Found != none && Found.IsDropShip)
				Result = class'X2AccessVocab'.default.InsideDropship;
			else
				Result = class'X2AccessVocab'.default.InsideBuilding;
			if (Count > 0)
				AppendComma(Result, class'X2AccessVocab'.static.Fill2(
					class'X2AccessVocab'.default.BuildingFloorOf, FloorNum, Count));
			else
				AppendComma(Result, class'X2AccessVocab'.static.Fill1(
					class'X2AccessVocab'.default.BuildingFloor, FloorNum));
		}
		else if (Count > 0)
			Result = class'X2AccessVocab'.static.Fill2(
				class'X2AccessVocab'.default.BuildingFloorChangedOf, FloorNum, Count);
		else
			Result = class'X2AccessVocab'.static.Fill1(
				class'X2AccessVocab'.default.BuildingFloorChanged, FloorNum);
	}

	LastIndoorState = Here;
	LastIndoorFloor = FloorNum;
	LastIndoorFloorCount = Count;
	LastIndoorBuildingPos = BuildingPos;
	return Result;
}

// The world-axis delta for a compass cardinal — pure table composition, infallible by
// construction (a dead arrow can never again be a silent no-op).
static function bool CardinalDelta(int Cardinal, out int DX, out int DY)
{
	switch (Cardinal)
	{
	case DIR_NORTH: DX = NORTH_DX;  DY = NORTH_DY;  return true;
	case DIR_SOUTH: DX = -NORTH_DX; DY = -NORTH_DY; return true;
	case DIR_EAST:  DX = EAST_DX;   DY = EAST_DY;   return true;
	case DIR_WEST:  DX = -EAST_DX;  DY = -EAST_DY;  return true;
	}
	DX = 0;
	DY = 0;
	return false;
}

static function bool SameTile(TTile A, TTile B)
{
	return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
}

// Localized game strings routinely arrive with their own terminator ("Нет доступных целей."), and
// blindly adding ours produced ".." — which some screen readers pause on twice and others read as
// an ellipsis. The rule and its terminator set live on X2AccessHelper, the shared oracle, so every
// surface that joins game text to ours punctuates alike; these two are call-site sugar over it.
private function AppendPart(out string Result, string Part)
{
	class'X2AccessHelper'.static.AppendPart(Result, Part);
}

// private: anything outside this class wants X2AccessHelper.AppendComma directly rather than
// routing through a surface class.
private static function AppendComma(out string Result, string Part)
{
	class'X2AccessHelper'.static.AppendComma(Result, Part);
}

defaultproperties
{
	LastSpokenFloor=-1
}
