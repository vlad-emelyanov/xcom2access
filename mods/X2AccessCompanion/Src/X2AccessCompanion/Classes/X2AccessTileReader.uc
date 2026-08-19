// X2AccessTileReader
// The tactical tile cursor's game-state half. The C++ layer owns the keys (input arbiter
// claims) and speech; it calls these functions on this class's default object via ProcessEvent
// (game thread, from the tick drain) and reads NativeResult back through reflection right after
// the call returns. Nothing here runs on its own — no listeners, no timers, no polling.
//
// The cursor is the game's OWN XCom3DCursor, driven through its own functions
// (CursorSetLocation / CursorSnapToFloor / MoveToUnit), so the pathing pawn preview, the
// camera follow, and everything else a mouse move causes still happens. The move order is the
// game's own click path (XComTacticalInput.ClickToPath), behind a two-press preview/confirm.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessTileReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

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

// Step one tile. ScreenDX/ScreenDY are camera-relative screen axes: +Y = up on screen (away
// from the camera), +X = right on screen. The world-axis mapping follows the camera's yaw
// quadrant, so the arrows track what a sighted player sees after Q/E rotation.
function CursorStep(int ScreenDX, int ScreenDY)
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile CurTile, Target, Landed;
	local vector Pos;
	local int FwdX, FwdY, RightX, RightY;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (EnsurePlanted(TC, Cursor, WorldData))
		return;

	bMovePending = false;

	GetScreenDirs(FwdX, FwdY, RightX, RightY);
	CurTile = WorldData.GetTileCoordinatesFromPosition(Cursor.Location);
	Target = CurTile;
	Target.X += ScreenDY * FwdX + ScreenDX * RightX;
	Target.Y += ScreenDY * FwdY + ScreenDX * RightY;
	Target.X = Clamp(Target.X, 0, WorldData.NumX - 1);
	Target.Y = Clamp(Target.Y, 0, WorldData.NumY - 1);
	if (Target.X == CurTile.X && Target.Y == CurTile.Y)
	{
		NativeResult = "Map edge";
		return;
	}

	// Resolve the target column's floor at the cursor's current height, then set the location
	// with the snap SEARCH OFF: bSnapToValidPosition ran CursorSnapToFixedFloors, whose search
	// slid the cursor several tiles to the nearest "valid" spot — one press read as a two-to-
	// three-tile jump (first live test, 2026-08-19). One press moves exactly one tile now; a
	// tile without a floor is still visited (it reads "Impassable" rather than blocking the
	// survey, RT wall-parity).
	Pos = WorldData.GetPositionFromTileCoordinates(Target);
	Pos.Z = Cursor.Location.Z;
	if (WorldData.GetFloorTileForPosition(Pos, Landed, false))
		Target = Landed;
	Pos = WorldData.GetPositionFromTileCoordinates(Target);
	Cursor.CursorSetLocation(Pos, true, false);

	// Describe where the cursor ACTUALLY is — if the engine refused or drifted the set, the
	// spoken tile must still be the true one.
	Landed = WorldData.GetTileCoordinatesFromPosition(Cursor.Location);
	NativeResult = DescribeTile(Landed, false);
}

// Step one floor up (+1) or down (-1) through the cursor's own floor snap, which also moves
// the camera cutaway the way the game expects.
function CursorFloor(int Delta)
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local CursorSearchResult Result;
	local TTile Landed;
	local int Desired;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (EnsurePlanted(TC, Cursor, WorldData))
		return;

	bMovePending = false;

	Desired = Cursor.WorldZToFloor(Cursor.Location) + Delta;
	if (Delta > 0)
		Result = Cursor.CursorSnapToFloor(Desired, eCursorSearch_Up);
	else
		Result = Cursor.CursorSnapToFloor(Desired, eCursorSearch_Down);

	Landed = WorldData.GetTileCoordinatesFromPosition(Cursor.Location);
	NativeResult = "Floor" @ string(Result.m_iEffectiveFloor + 1) $ "." @ DescribeTile(Landed, false);
}

// Recenter the cursor on the active unit and read its tile.
function CursorRecenter()
{
	local XComTacticalController TC;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local XGUnit ActiveUnit;
	local TTile Tile;

	NativeResult = "";
	if (!GetContext(TC, Cursor, WorldData))
		return;
	if (EnsurePlanted(TC, Cursor, WorldData))
		return;

	bMovePending = false;

	ActiveUnit = TC.GetActiveUnit();
	if (ActiveUnit == none || ActiveUnit.GetPawn() == none)
	{
		NativeResult = "No unit selected";
		return;
	}
	Cursor.MoveToUnit(ActiveUnit.GetPawn());
	Tile = WorldData.GetTileCoordinatesFromPosition(Cursor.Location);
	NativeResult = ActiveUnitName() $ "." @ DescribeTile(Tile, false);
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

	Tile = WorldData.GetTileCoordinatesFromPosition(Cursor.Location);
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

	ActiveUnit = TC.GetActiveUnit();
	if (ActiveUnit == none)
	{
		NativeResult = "No unit selected";
		return;
	}
	UnitState = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(ActiveUnit.ObjectID));
	PathingPawn = TC.m_kPathingPawn;
	if (UnitState == none || PathingPawn == none || PathingPawn.bHidden)
	{
		NativeResult = "No move available";
		bMovePending = false;
		return;
	}

	CursorTile = WorldData.GetTileCoordinatesFromPosition(Cursor.Location);
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
	local string Result, Part;
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
	// A tile units cannot occupy (wall interior, hole, blocked) — the RT reader's "wall".
	if (!WorldData.CanUnitsEnterTile(Tile))
		AppendPart(Result, "Impassable");
	if (FOW == eFOWTileStatus_Seen)
		AppendPart(Result, class'X2AccessHelper'.static.DescribeUnitsOnTile(Tile));

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

// Per-edge cover in camera-relative words. GetCoverDirection maps a world axis delta to the
// cover-flag bit index (N/S/E/W order — COVER_North is bit 0, low cover the same order at
// bit 4), so the same quadrant math that steps the cursor names the edges.
private function string DescribeCoverEdges(vector TilePos, bool bVerbose)
{
	local XComWorldData WorldData;
	local XComCoverPoint CP;
	local string Result;
	local int FwdX, FwdY, RightX, RightY;

	WorldData = `XWORLD;
	if (!WorldData.GetCoverPointAtFloor(TilePos, CP))
	{
		if (bVerbose)
			return "No cover";
		return "";
	}
	GetScreenDirs(FwdX, FwdY, RightX, RightY);
	Result = "";
	AppendCoverEdge(Result, CP.Flags, FwdX, FwdY, "ahead");
	AppendCoverEdge(Result, CP.Flags, -FwdX, -FwdY, "behind");
	AppendCoverEdge(Result, CP.Flags, RightX, RightY, "right");
	AppendCoverEdge(Result, CP.Flags, -RightX, -RightY, "left");
	if (Result == "" && bVerbose)
		Result = "No cover";
	return Result;
}

private function AppendCoverEdge(out string Result, int Flags, int DX, int DY, string Word)
{
	local int CoverDir;

	if (!class'XComWorldData'.static.GetCoverDirection(CoverDir, DX, DY))
		return;
	if ((Flags & (class'XComWorldData'.const.COVER_North << CoverDir)) != 0)
		AppendPart(Result, "High cover" @ Word);
	else if ((Flags & (class'XComWorldData'.const.COVER_NLow << CoverDir)) != 0)
		AppendPart(Result, "Low cover" @ Word);
}

// Doors, hackable objectives, loot — the static map features a sighted player recognizes on
// sight. Interactive objects are few per mission, so the history walk is cheap at key rate.
private function string DescribeInteractives(TTile Tile)
{
	local XComGameState_InteractiveObject IObj;
	local string Result, Name;

	foreach `XCOMHISTORY.IterateByClassType(class'XComGameState_InteractiveObject', IObj)
	{
		if (!SameTile(IObj.TileLocation, Tile))
			continue;
		if (IObj.IsDoor())
			Name = "Door";
		else if (IObj.MustBeHacked())
			Name = "Hackable object";
		else if (IObj.GetAvailableLoot().Length > 0)
			Name = "Loot";
		else
			Name = "Interactive object";
		if (IObj.LockStrength > 0)
			Name @= "(locked)";
		AppendPart(Result, Name);
	}
	return Result;
}

// "1 action" / "Dash" / "Out of range" for the active unit — the spoken version of the
// blue/yellow move highlight, answered from the same cache the pathing pawn uses.
private function string DescribeReachability(TTile Tile)
{
	local XComTacticalController TC;
	local XGUnit ActiveUnit;
	local XComGameState_Unit UnitState;
	local float Cost;

	TC = TacCtrl();
	if (TC == none)
		return "";
	ActiveUnit = TC.GetActiveUnit();
	if (ActiveUnit == none || ActiveUnit.m_kReachableTilesCache == none)
		return "";
	UnitState = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(ActiveUnit.ObjectID));
	if (UnitState == none)
		return "";
	if (SameTile(Tile, UnitState.TileLocation))
		return "Your tile";
	Cost = ActiveUnit.m_kReachableTilesCache.GetPathCostToTile(Tile);
	if (Cost < 0)
		return "Out of range";
	if (Cost > ActiveUnit.GetMobility())
		return "Dash";
	return "1 action";
}

// Offset from the active unit in camera-relative words ("3 ahead, 2 right, 1 up"). Always
// spoken — it proves the step registered even when the tile itself has nothing to say.
private function string DescribeOffset(TTile Tile)
{
	local XComTacticalController TC;
	local XGUnit ActiveUnit;
	local XComGameState_Unit UnitState;
	local int DX, DY, DZ, FwdX, FwdY, RightX, RightY, FwdAmt, RightAmt;
	local string Result;

	TC = TacCtrl();
	if (TC == none)
		return "";
	ActiveUnit = TC.GetActiveUnit();
	if (ActiveUnit == none)
		return "";
	UnitState = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(ActiveUnit.ObjectID));
	if (UnitState == none)
		return "";

	DX = Tile.X - UnitState.TileLocation.X;
	DY = Tile.Y - UnitState.TileLocation.Y;
	DZ = Tile.Z - UnitState.TileLocation.Z;
	if (DX == 0 && DY == 0 && DZ == 0)
		return "At" @ ActiveUnitName();

	GetScreenDirs(FwdX, FwdY, RightX, RightY);
	FwdAmt = DX * FwdX + DY * FwdY;
	RightAmt = DX * RightX + DY * RightY;

	Result = "";
	if (FwdAmt > 0)
		Result = string(FwdAmt) @ "ahead";
	else if (FwdAmt < 0)
		Result = string(-FwdAmt) @ "behind";
	if (RightAmt > 0)
		AppendComma(Result, string(RightAmt) @ "right");
	else if (RightAmt < 0)
		AppendComma(Result, string(-RightAmt) @ "left");
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

// Plant the cursor on the active unit on the first press of a mission and read that tile
// instead of acting. Returns true when this call did the planting (callers stop there).
private function bool EnsurePlanted(XComTacticalController TC, XCom3DCursor Cursor,
	XComWorldData WorldData)
{
	local XComGameState_BattleData BattleData;
	local XGUnit ActiveUnit;
	local TTile Tile;
	local int MissionID;

	BattleData = XComGameState_BattleData(
		`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData', true));
	MissionID = -1;
	if (BattleData != none)
		MissionID = BattleData.m_iMissionID;
	if (bPlanted && MissionID == PlantedMissionID)
		return false;

	bPlanted = true;
	PlantedMissionID = MissionID;
	bMovePending = false;

	ActiveUnit = TC.GetActiveUnit();
	if (ActiveUnit != none && ActiveUnit.GetPawn() != none)
		Cursor.MoveToUnit(ActiveUnit.GetPawn());
	Tile = WorldData.GetTileCoordinatesFromPosition(Cursor.Location);
	NativeResult = "Cursor on." @ DescribeTile(Tile, false);
	return true;
}

// The camera-relative screen axes as world-axis deltas, from the camera yaw quantized to the
// nearest quadrant (the tactical camera rotates in 45-degree steps; ties break consistently).
// If in-game testing shows left/right mirrored, flip the two Right signs — this is the single
// flip point for the whole reader (steps, cover words, offsets all route through here).
private function GetScreenDirs(out int FwdX, out int FwdY, out int RightX, out int RightY)
{
	local PlayerController PC;
	local int Quadrant;

	Quadrant = 0;
	PC = class'WorldInfo'.static.GetWorldInfo().GetALocalPlayerController();
	if (PC != none && PC.PlayerCamera != none)
		Quadrant = (((PC.PlayerCamera.CameraCache.POV.Rotation.Yaw & 65535) + 8192) >> 14) & 3;

	switch (Quadrant)
	{
	case 0: FwdX = 1;  FwdY = 0;  break;
	case 1: FwdX = 0;  FwdY = 1;  break;
	case 2: FwdX = -1; FwdY = 0;  break;
	case 3: FwdX = 0;  FwdY = -1; break;
	}
	RightX = FwdY;
	RightY = -FwdX;
}

private function string ActiveUnitName()
{
	local XComTacticalController TC;
	local XGUnit ActiveUnit;
	local XComGameState_Unit UnitState;

	TC = TacCtrl();
	if (TC == none)
		return "unit";
	ActiveUnit = TC.GetActiveUnit();
	if (ActiveUnit == none)
		return "unit";
	UnitState = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(ActiveUnit.ObjectID));
	if (UnitState == none)
		return "unit";
	return UnitState.GetName(eNameType_Full);
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
