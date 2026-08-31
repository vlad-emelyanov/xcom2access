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

// The floor value the player last heard (or our own code last set), CursorFloorPoll's change
// detector. -1 until the first observation. The game's m_iRequestedFloor is the truth; this
// only remembers what was spoken, so external changes — the mouse wheel, the game re-flooring
// on a soldier switch — announce exactly once. Every SetCursorTile adopts it silently.
var int LastSpokenFloor;

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
		NativeResult = "Map edge";
		return;
	}

	// Follow the terrain: resolve the target column's floor near the current height. When
	// there is no floor to resolve (a wall interior, a pit), the tile is visited at the
	// current level anyway — it reads "Impassable" rather than blocking the survey.
	ResolveFloor(WorldData, Target);

	SetCursorTile(Cursor, WorldData, Target);
	NativeResult = DescribeTile(Target, false);
}

// Step one floor level up (+1) or down (-1) by driving the game's OWN floor verbs — the same
// AscendFloor/DescendFloor the mouse wheel and the controller D-pad call. The floor is shared,
// load-bearing state with four consumers (the camera band, the building cutaway, WorldCascadeZ,
// and the pathing pawn's MaxZ destination clamp, XComPathingPawn:991), so an invented per-column
// Z hop that bypassed it left the pawn refusing tiles the reachable cache called Dash (live
// 2026-08-31, the evac-zone roof). One press = one 4-tile band, the game's own granularity.
// After the game accepts the change, land our cursor in the new band of the current column and
// read it out; the only refusal in X2 is the game's own top/bottom clamp (DisableFloorChanges
// is hardwired false).
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
			NativeResult = "Top floor";
		else
			NativeResult = "Bottom floor";
		return;
	}

	Target = FloorBandTile(Cursor, WorldData, Cursor.m_iRequestedFloor);
	SetCursorTile(Cursor, WorldData, Target);
	NativeResult = FloorWord(Cursor) $ "." @ DescribeTile(Target, false);
}

// Passive poll — native calls this on its tick while the tile cursor owns world mode. When the
// game's floor changed OUTSIDE our own pushes (the mouse wheel, the game re-flooring on a
// soldier switch via MoveToUnit's GetIdealPawnFloor), speak it and pull the planted cursor into
// the new band, so the game's floor and our cursor can never stay diverged — the exact
// contradiction the pathing clamp punishes. Our own writes adopt LastSpokenFloor inside
// SetCursorTile and never echo here. Before the cursor is planted there is nothing to sync,
// and announcing the game's own camera housekeeping would be noise: adopt silently.
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
	Target = FloorBandTile(Cursor, WorldData, Cursor.m_iRequestedFloor);
	SetCursorTile(Cursor, WorldData, Target);
	NativeResult = FloorWord(Cursor) $ "." @ DescribeTile(Target, false);
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
		NativeResult = "No unit selected";
		return;
	}
	SetCursorTile(Cursor, WorldData, UnitState.TileLocation);

	// No name prefix: DescribeTile's own DescribeUnitsOnTile leads with "Ally: <name>" for the
	// unit standing here, which on a recenter is always this one. Prefixing it said the name
	// three times in one line ("Shreya Bose. Ally: Shreya Bose. ... At Shreya Bose").
	NativeResult = DescribeTile(UnitState.TileLocation, false);
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
	NativeResult = DescribeTile(Tile, true);
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
		NativeResult = "No unit selected";
		return;
	}
	if (PathingPawn == none || PathingPawn.bHidden)
	{
		NativeResult = "No move available";
		bMovePending = false;
		return;
	}

	CursorTile = CurTile();
	if (SameTile(CursorTile, UnitState.TileLocation))
	{
		NativeResult = "Already here";
		bMovePending = false;
		return;
	}

	// The pathing pawn's destination is clamped to the reachable area; when it disagrees with
	// the cursor the game shows its out-of-range marker — we speak it instead.
	Dest = PathingPawn.LastDestinationTile;
	if (!SameTile(Dest, CursorTile))
	{
		NativeResult = "Out of range";
		bMovePending = false;
		return;
	}

	if (!bMovePending || !SameTile(PendingMoveTile, Dest))
	{
		// First press: preview. The path is the one the pathing pawn already built for this
		// cursor position — exactly what the game would run.
		Desc = string(Max(PathingPawn.PathTiles.Length - 1, 0)) @ "tiles";
		Cost = ActiveUnit.m_kReachableTilesCache.GetPathCostToTile(Dest);
		if (Cost >= 0 && Cost > ActiveUnit.GetMobility())
			Desc $= ", dash";
		// The path-marker warnings, BEFORE the commit prompt — a blind player must hear "breaks
		// concealment" while the second press is still theirs to withhold.
		class'X2AccessHelper'.static.AppendComma(Desc, DescribePathRisks(PathingPawn, UnitState));
		AppendPart(Desc, DescribeCoverEdges(WorldData.GetPositionFromTileCoordinates(Dest), true));
		AppendPart(Desc, "Press again to move");
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
		NativeResult = "Moving";
	else
		NativeResult = "Cannot move there";
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
	local bool bImpassable;
	local int LedgeDrop;

	WorldData = `XWORLD;
	TilePos = WorldData.GetPositionFromTileCoordinates(Tile);
	WorldData.GetTileFOWValue(Tile, FOW, GameplayFlags);

	if (FOW == eFOWTileStatus_NeverSeen)
	{
		Result = "Unexplored";
		AppendPart(Result, DescribeOffset(Tile));
		return Result;
	}

	Result = "";
	UnitPart = "";
	if (FOW == eFOWTileStatus_Seen)
		UnitPart = class'X2AccessHelper'.static.DescribeUnitsOnTile(Tile);

	// Occupied is not impassable: a tile with a live unit on it fails CanUnitsEnterTile, and
	// speaking "Impassable" over an ally read as a bug in the first live test.
	bImpassable = (UnitPart == "" && !WorldData.CanUnitsEnterTile(Tile));
	if (bImpassable)
	{
		// A LEDGE outranks "Impassable", because the two are not the same fact and only one of
		// them is what a sighted player sees. Sweeping the cursor off a roof, a catwalk or the
		// top of a container lands on a tile with no floor at this Z and open air below it — the
		// railing a sighted player is looking at. "Impassable" describes that identically to a
		// wall, which is exactly the information a blind player most needs to tell apart.
		// Ported from WrathAccess's grid.edge_drop (Pathfinder: WotR), which has to infer the
		// drop from a physics linecast plus a navmesh-validity test; XCOM answers it outright.
		LedgeDrop = LedgeDepth(WorldData, Tile);
		if (LedgeDrop > 0)
			AppendPart(Result, "Ledge," @ string(LedgeDrop) @ "down");
		else
			AppendPart(Result, "Impassable");
	}
	AppendPart(Result, UnitPart);

	AppendPart(Result, DescribeInteractives(Tile));
	AppendPart(Result, DescribeCoverEdges(TilePos, bVerbose));

	// The red detection overlay, per tile (docs/concealment-recon.md §3). No FOW gate of our own:
	// the game paints red over explored fog too, and a never-seen tile already returned above.
	// The helper itself goes silent the moment the active unit is not concealed — the same edge
	// on which the game hides the overlay. Impassable tiles skip it for the same reason they skip
	// reachability: nobody can stand there, so the warning has no move to warn about.
	if (!bImpassable)
		AppendPart(Result, class'X2AccessHelper'.static.DescribeDetection(ActiveUnitState(), Tile));

	if (FOW == eFOWTileStatus_Seen)
		AppendPart(Result, class'X2AccessHelper'.static.DescribeHazards(WorldData, Tile));

	if (WorldData.IsWaterTile(Tile))
		AppendPart(Result, "Water");
	if (WorldData.IsRampTile(Tile))
		AppendPart(Result, "Ramp");

	// "Impassable. Out of range." says one thing twice — a tile nobody can stand on has no
	// reachability to report, and this was the single noisiest line of the 2026-08-21 run
	// (~99 of them, against 23 actual moves). Sweeping the cursor along a wall should cost one
	// word per tile, not three.
	if (!bImpassable)
		AppendPart(Result, DescribeReachability(Tile));
	AppendPart(Result, DescribeOffset(Tile));

	if (bVerbose)
		AppendPart(Result, "Tile" @ string(Tile.X) $ "," @ string(Tile.Y) $ "," @ string(Tile.Z));

	if (Result == "")
		Result = "Clear";
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
private function string DescribeCoverEdges(vector TilePos, bool bVerbose)
{
	local XComWorldData WorldData;
	local XComCoverPoint CP;
	local string Result;
	local int Dir;

	WorldData = `XWORLD;
	if (!WorldData.GetCoverPointAtFloor(TilePos, CP))
	{
		if (bVerbose)
			return "No cover";
		return "";
	}
	Result = "";
	for (Dir = 0; Dir < 4; Dir++)
	{
		if ((CP.Flags & (class'XComWorldData'.const.COVER_North << Dir)) == 0)
			continue; // no cover at all on this edge

		// FULL/HALF, not HIGH/LOW. The engine's flag is named "low" but the game's own UI, the
		// shot readout's DescribeTargetCover and the player all say full/half cover — and this
		// surface and that one describe the same wall. Two vocabularies for one fact is how you
		// get a player who cannot tell whether the mod is contradicting itself.
		if ((CP.Flags & (class'XComWorldData'.const.COVER_NLow << Dir)) != 0)
			AppendPart(Result, "Half cover" @ CardinalWord(Dir));
		else
			AppendPart(Result, "Full cover" @ CardinalWord(Dir));
	}
	if (Result == "" && bVerbose)
		Result = "No cover";
	return Result;
}

// Doors, hackable objectives, loot — the static map features a sighted player recognizes on
// sight. Interactive objects are few per mission, so the history walk is cheap at key rate.
private function string DescribeInteractives(TTile Tile)
{
	local XComGameState_InteractiveObject IObj;
	local string Result, ObjName;

	foreach `XCOMHISTORY.IterateByClassType(class'XComGameState_InteractiveObject', IObj)
	{
		if (!SameTile(IObj.TileLocation, Tile))
			continue;
		if (IObj.IsDoor())
			ObjName = "Door";
		else if (IObj.MustBeHacked())
			ObjName = "Hackable object";
		else if (IObj.GetAvailableLoot().Length > 0)
			ObjName = "Loot";
		else
			ObjName = "Interactive object";
		if (IObj.LockStrength > 0)
			ObjName @= "(locked)";
		AppendPart(Result, ObjName);
	}
	return Result;
}

// "1 action" / "Dash" / "Out of range" for the active unit — the spoken version of the
// blue/yellow move highlight, answered from the same cache the pathing pawn uses.
private function string DescribeReachability(TTile Tile)
{
	local XGUnit ActiveUnit;
	local XComGameState_Unit UnitState;
	local float Cost;

	UnitState = ActiveUnitState();
	if (UnitState == none)
		return "";
	if (SameTile(Tile, UnitState.TileLocation))
		return "Your tile";
	ActiveUnit = ActiveUnitVisualizer(UnitState);
	if (ActiveUnit == none || ActiveUnit.m_kReachableTilesCache == none)
		return "";
	Cost = ActiveUnit.m_kReachableTilesCache.GetPathCostToTile(Tile);
	if (Cost < 0)
		return "Out of range";
	if (Cost > ActiveUnit.GetMobility())
		return "Dash";
	return "1 action";
}

// The spoken form of the path preview's warning icons (docs/concealment-recon.md §3): "noise"
// where the game would pin its noise marker, "breaks concealment" where it would pin the eye.
// Both from the game's own primitives rather than the pawn's marker arrays, which are private
// to script — and one hover STALE at step time anyway, since the pawn rebuilds its path on its
// own Tick after our cursor push. By the time this runs, CursorMoveTo has already proven the
// path settled (LastDestinationTile agreed with the cursor).
//
// "Noise" is spoken concealed or not, like the game's marker — a revealed squad smashing glass
// still calls patrols. The concealment half only while concealed, and destruction implies it
// directly (XComGameState_Unit.uc:8940 — no enemy required), so the per-tile oracle is only
// consulted when no smashed window already answered.
private function string DescribePathRisks(XComPathingPawn PathingPawn, XComGameState_Unit UnitState)
{
	local XComWorldData WorldData;
	local XComDestructibleActor Destructible;
	local array<TTile> Tiles;
	local string Result;
	local bool bLarge, bNoise, bBreaks;
	local int i, Kind;

	if (UnitState == none)
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

	if (UnitState.IsConcealed())
	{
		bBreaks = bNoise;
		for (i = 1; i < Tiles.Length && !bBreaks; i++)
		{
			bBreaks = class'X2AccessHelper'.static.TileBreaksConcealment(
				UnitState, Tiles[i], Kind);
		}
	}

	if (bBreaks)
		Result = "breaks concealment";
	if (bNoise)
		class'X2AccessHelper'.static.AppendComma(Result, "noise");
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
	return OffsetWords(UnitState.TileLocation, Tile);
}

// The compass words for one tile relative to another ("3 north, 2 east, 1 up"); empty when the
// two are the same tile. Public and static because the object scanner speaks the same bearings
// about the same world — sharing this keeps ONE calibration of NORTH_DX/EAST_DX (see the header),
// so the two surfaces can never drift into disagreeing about which way north is.
static function string OffsetWords(TTile FromTile, TTile ToTile)
{
	local int DX, DY, DZ, NDX, NDY, EDX, EDY, NorthAmt, EastAmt;
	local string Result;

	DX = ToTile.X - FromTile.X;
	DY = ToTile.Y - FromTile.Y;
	DZ = ToTile.Z - FromTile.Z;
	if (DX == 0 && DY == 0 && DZ == 0)
		return "";

	if (!CardinalDelta(DIR_NORTH, NDX, NDY) || !CardinalDelta(DIR_EAST, EDX, EDY))
		return "";
	NorthAmt = DX * NDX + DY * NDY;
	EastAmt = DX * EDX + DY * EDY;

	Result = "";
	if (NorthAmt > 0)
		Result = string(NorthAmt) @ CardinalWord(DIR_NORTH);
	else if (NorthAmt < 0)
		Result = string(-NorthAmt) @ CardinalWord(DIR_SOUTH);
	if (EastAmt > 0)
		AppendComma(Result, string(EastAmt) @ CardinalWord(DIR_EAST));
	else if (EastAmt < 0)
		AppendComma(Result, string(-EastAmt) @ CardinalWord(DIR_WEST));
	if (DZ > 0)
		AppendComma(Result, string(DZ) @ "up");
	else if (DZ < 0)
		AppendComma(Result, string(-DZ) @ "down");
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
		NativeResult = "No unit selected";
		return true; // consumed: spoke the refusal, nothing planted
	}

	bPlanted = true;
	PlantedMissionID = MissionID;
	bMovePending = false;

	SetCursorTile(Cursor, WorldData, UnitState.TileLocation);
	NativeResult = "Cursor on." @ DescribeTile(UnitState.TileLocation, false);
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
		NativeResult = "No marker selected";
		return;
	}

	Target.X = Clamp(class'X2AccessObjectScanner'.default.SelX, 0, WorldData.NumX - 1);
	Target.Y = Clamp(class'X2AccessObjectScanner'.default.SelY, 0, WorldData.NumY - 1);
	Target.Z = Clamp(class'X2AccessObjectScanner'.default.SelZ, 0, WorldData.NumZ - 1);

	bPlanted = true;
	PlantedMissionID = CurrentMissionID();
	bMovePending = false;
	SetCursorTile(Cursor, WorldData, Target);
	NativeResult = "Cursor." @ DescribeTile(Target, false);
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
	// The FLOOR STATE has to travel with the position. XComPathingPawn:991 caps its
	// destination search at m_iRequestedFloor's ceiling whenever the cursor is inside a
	// building volume, so a push onto a roof with the floor state still at ground makes the
	// pawn clamp to a lower tile and the move key refuse a tile the reachable cache itself
	// calls Dash (measured live 2026-08-31: cursor at Z=13, pawn destination Z=9,
	// m_iRequestedFloor 1). No mouse player can produce that contradiction — the cutaway
	// hides a roof until camera-level-up raises the floor. The field write alone is only half
	// the sync: CursorSnapToFloor is what derives the camera band, the building-cutdown height
	// and the effective floor from m_iRequestedFloor — on the mouse cursor it moves nothing
	// (XCom3DCursorMouse:11). The exact position is pushed LAST, so a base-class snap (which
	// does move the cursor) could never displace the tile the reader spoke.
	Cursor.m_iRequestedFloor = Cursor.WorldZToFloor(Pos);
	Cursor.CursorSnapToFloor(Cursor.m_iRequestedFloor, eCursorSearch_ExactFloor);
	LastSpokenFloor = Cursor.m_iRequestedFloor;
	Cursor.CursorSetLocation(Pos, false, false);
}

private function TTile CurTile()
{
	local TTile T;

	T.X = CurX;
	T.Y = CurY;
	T.Z = CurZ;
	return T;
}

// The tile our cursor should occupy in floor band FloorIdx of the current column: the lowest
// tile of the band with a real floor (the walking surface of that level), else the band's
// lowest tile, left hanging — which reads as a ledge or impassable, the honest answer for a
// level this column has nothing on, and the same "feel the edge" rule stepping off a roof
// follows. Band membership is asked of the engine (WorldZToFloor) per tile rather than derived
// from the 4-tiles-per-band constant, so a map with an offset ground plane cannot skew it.
private function TTile FloorBandTile(XCom3DCursor Cursor, XComWorldData WorldData, int FloorIdx)
{
	local TTile T, FirstInBand;
	local bool bFoundBand;
	local int Z;

	T.X = CurX;
	T.Y = CurY;
	for (Z = 0; Z < WorldData.NumZ; Z++)
	{
		T.Z = Z;
		if (Cursor.WorldZToFloor(WorldData.GetPositionFromTileCoordinates(T)) != FloorIdx)
		{
			if (bFoundBand)
				break; // past the band's top — bands are contiguous in Z
			continue;
		}
		if (!bFoundBand)
		{
			bFoundBand = true;
			FirstInBand = T;
		}
		if (WorldData.IsFloorTile(T))
			return T;
	}
	if (bFoundBand)
		return FirstInBand;
	// The requested band holds no tile of this map at all (asked above NumZ); stay put.
	T.Z = Clamp(CurZ, 0, WorldData.NumZ - 1);
	return T;
}

// "Floor N of M" in the game's own numbering — m_iRequestedFloor is what the camera band, the
// cutaway and the pathing clamp all key on, so the spoken number is the one every consumer
// agrees about. Read AFTER SetCursorTile, which re-derives it from the landed tile.
private function string FloorWord(XCom3DCursor Cursor)
{
	return "Floor" @ string(Cursor.m_iRequestedFloor) @ "of" @ string(Cursor.GetMaxFloor());
}

// How far the ground is below a tile that has no floor of its own — 0 when this is not a ledge.
//
// The two conditions are what separate "you are standing over a drop" from "there is a wall
// here", and both come from the engine rather than from a rule of ours. NO FLOOR AT THIS Z is
// the whole reason the cursor is left hanging: CursorStep's ResolveFloor searches only one tile
// down (GetFloorTileForPosition's bUnlimitedSearch = false), so stepping off a roof keeps the
// cursor's height instead of falling — deliberately, so the edge can be felt. GetFloorTileZ with
// the UNLIMITED search then answers how deep, and documents its own miss by returning the source
// Z, which is why the comparison is strictly-less-than rather than a bool.
//
// A wall at ground level is not caught by this and must not be: buildings sit on the ground grid,
// so a tile inside a wall still HAS a floor and never reaches here. That is what keeps the word
// honest — a ledge is open air, not merely something you cannot walk into.
private function int LedgeDepth(XComWorldData WorldData, TTile Tile)
{
	local int FloorZ;

	if (WorldData.IsFloorTile(Tile))
		return 0;

	FloorZ = WorldData.GetFloorTileZ(Tile, true);
	if (FloorZ >= Tile.Z)
		return 0;

	return Tile.Z - FloorZ;
}

// Snap a target tile to the floor of its column near the current height, so stepping follows
// terrain (up a slope, down off a roof edge). Leaves the tile untouched when no floor resolves.
private function ResolveFloor(XComWorldData WorldData, out TTile Tile)
{
	local TTile Floor;
	local vector Pos;

	Pos = WorldData.GetPositionFromTileCoordinates(Tile);
	if (WorldData.GetFloorTileForPosition(Pos, Floor, false))
	{
		Tile.X = Clamp(Floor.X, 0, WorldData.NumX - 1);
		Tile.Y = Clamp(Floor.Y, 0, WorldData.NumY - 1);
		Tile.Z = Clamp(Floor.Z, 0, WorldData.NumZ - 1);
	}
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

static function string CardinalWord(int Cardinal)
{
	switch (Cardinal)
	{
	case DIR_NORTH: return "north";
	case DIR_SOUTH: return "south";
	case DIR_EAST:  return "east";
	case DIR_WEST:  return "west";
	}
	return "";
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

// private: OffsetWords is the only caller in this class, and anything outside it wants
// X2AccessHelper.AppendComma directly rather than routing through a surface class.
private static function AppendComma(out string Result, string Part)
{
	class'X2AccessHelper'.static.AppendComma(Result, Part);
}

defaultproperties
{
	LastSpokenFloor=-1
}
