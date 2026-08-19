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

// Step one floor level up (+1) or down (-1): find the next Z in this column that has a floor
// tile and jump the cursor there. Says so when there is none.
function CursorFloor(int Delta)
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Probe;
	local int Z;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (EnsurePlanted(TC, Cursor, WorldData))
		return;

	bMovePending = false;

	Probe.X = CurX;
	Probe.Y = CurY;
	for (Z = CurZ + Delta; Z >= 0 && Z < WorldData.NumZ; Z += Delta)
	{
		Probe.Z = Z;
		if (WorldData.IsFloorTile(Probe))
		{
			SetCursorTile(Cursor, WorldData, Probe);
			if (Delta > 0)
				NativeResult = "Up." @ DescribeTile(Probe, false);
			else
				NativeResult = "Down." @ DescribeTile(Probe, false);
			return;
		}
	}
	if (Delta > 0)
		NativeResult = "No floor above";
	else
		NativeResult = "No floor below";
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
	NativeResult = UnitState.GetName(eNameType_Full) $ "." @ DescribeTile(UnitState.TileLocation, false);
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
			Desc @= ", dash";
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
	if (UnitPart == "" && !WorldData.CanUnitsEnterTile(Tile))
		AppendPart(Result, "Impassable");
	AppendPart(Result, UnitPart);

	AppendPart(Result, DescribeInteractives(Tile));
	AppendPart(Result, DescribeCoverEdges(TilePos, bVerbose));

	if (FOW == eFOWTileStatus_Seen)
		AppendPart(Result, class'X2AccessHelper'.static.DescribeHazards(WorldData, Tile));

	if (WorldData.IsWaterTile(Tile))
		AppendPart(Result, "Water");
	if (WorldData.IsRampTile(Tile))
		AppendPart(Result, "Ramp");

	AppendPart(Result, DescribeReachability(Tile));
	AppendPart(Result, DescribeOffset(Tile));

	if (bVerbose)
		AppendPart(Result, "Tile" @ string(Tile.X) $ "," @ string(Tile.Y) $ "," @ string(Tile.Z));

	if (Result == "")
		Result = "Clear";
	return Result;
}

// Per-edge cover in compass words, named directly by the engine's own cover-flag order
// (COVER_North bit 0 → "north", low cover the same order four bits up).
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
		if ((CP.Flags & (class'XComWorldData'.const.COVER_North << Dir)) != 0)
			AppendPart(Result, "High cover" @ CardinalWord(Dir));
		else if ((CP.Flags & (class'XComWorldData'.const.COVER_NLow << Dir)) != 0)
			AppendPart(Result, "Low cover" @ CardinalWord(Dir));
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

// Offset from the active unit in compass words ("3 north, 2 east, 1 up"). Always spoken — it
// proves the step registered even when the tile itself has nothing to say.
private function string DescribeOffset(TTile Tile)
{
	local XComGameState_Unit UnitState;
	local int DX, DY, DZ, NDX, NDY, EDX, EDY, NorthAmt, EastAmt;
	local string Result;

	UnitState = ActiveUnitState();
	if (UnitState == none)
		return "";

	DX = Tile.X - UnitState.TileLocation.X;
	DY = Tile.Y - UnitState.TileLocation.Y;
	DZ = Tile.Z - UnitState.TileLocation.Z;
	if (DX == 0 && DY == 0 && DZ == 0)
		return "At" @ UnitState.GetName(eNameType_Full);

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

private function XComTacticalController TacCtrl()
{
	return XComTacticalController(class'WorldInfo'.static.GetWorldInfo().GetALocalPlayerController());
}

// The selected unit's state. GetActiveUnit() merely returns the controller's
// ControllingUnitVisualizer VAR, which lags or sits empty in stretches of a mission (the first
// live test lost every offset and reachability line to it) — the ControllingUnit
// StateObjectReference on the same controller is the durable source, so fall back to it.
private function XComGameState_Unit ActiveUnitState()
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
private function XGUnit ActiveUnitVisualizer(XComGameState_Unit UnitState)
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
	local XComGameState_BattleData BattleData;
	local XComGameState_Unit UnitState;
	local int MissionID;

	BattleData = XComGameState_BattleData(
		`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData', true));
	MissionID = -1;
	if (BattleData != none)
		MissionID = BattleData.m_iMissionID;
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

// Adopt a tile as the authoritative cursor state and push the game's 3D cursor onto it —
// exact tile center, snap search OFF. Every historical "the push never lands" measurement
// was the game's own per-frame mouse re-push (XComTacticalInput.Mouse_CheckForPathing)
// reverting it one frame later; the C++ mouse gate now suppresses that while the tile
// cursor owns world mode, and with it gone an exact push is strictly better than a snapped
// one: GetClosestValidCursorPosition slid a floor-up push two tiles sideways (measured live
// 2026-08-20), and the pathing pawn must preview exactly the tile the reader spoke.
private function SetCursorTile(XCom3DCursor Cursor, XComWorldData WorldData, TTile Tile)
{
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
	Cursor.CursorSetLocation(WorldData.GetPositionFromTileCoordinates(Tile), false, false);
}

private function TTile CurTile()
{
	local TTile T;

	T.X = CurX;
	T.Y = CurY;
	T.Z = CurZ;
	return T;
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
private function bool CardinalDelta(int Cardinal, out int DX, out int DY)
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

private function string CardinalWord(int Cardinal)
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

private function bool SameTile(TTile A, TTile B)
{
	return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
}

private function AppendPart(out string Result, string Part)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= ". ";
	Result $= Part;
}

private function AppendComma(out string Result, string Part)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= ", ";
	Result $= Part;
}
