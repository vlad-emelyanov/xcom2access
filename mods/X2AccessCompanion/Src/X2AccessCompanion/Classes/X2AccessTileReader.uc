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
		// Who would see the soldier there and whether the cover holds — the eye markers and the
		// red shield the game draws on this very destination while the puck sits on it.
		AppendPart(Desc, DescribeSight(Dest, WorldData.GetPositionFromTileCoordinates(Dest)));
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
			NativeResult = "No unit selected";
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
	// The evac zone, ahead of cover: on the turn it matters, "am I standing in it" outranks
	// every other fact this line carries. Not fog-gated — the never-seen branch above already
	// returned, and inside explored ground the painted footprint is something a sighted player
	// simply sees. The oracle is X2AccessHelper's, shared with the object scanner's ring so the
	// two surfaces cannot disagree about where the zone is.
	AppendPart(Result, class'X2AccessHelper'.static.DescribeEvacZone(Tile));
	AppendPart(Result, DescribeCoverEdges(TilePos, bVerbose));
	// Straight after the cover it qualifies: who would see a soldier here, and whether that
	// cover holds against them. Impassable tiles skip it for the reason they skip reachability.
	if (!bImpassable)
		AppendPart(Result, DescribeSight(Tile, TilePos));

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

	// The game's own Gameplay option "Show Target Preview" keeps the strip up on every hover
	// instead of only while Alt is held; on, the strip rides every step here too. LAST, because
	// it is the long clause and everything before it is what a step is for.
	if (!bImpassable && TargetPreviewAlwaysOn())
		AppendPart(Result, DescribeTargetPreview(Tile, false));

	if (Result == "")
		Result = "Clear";
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
	Result = "Seen by" @ string(Count);
	if (bFlanked)
		AppendComma(Result, "flanked");
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
		return bSpeakEmpty ? "No unit selected" : "";

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
			AppendComma(Entry, "not targetable");
		else
		{
			if (AffSquadsight[j] != 0)
				AppendComma(Entry, "via squadsight");
			if (AffCover[j] == CT_None)
				AppendComma(Entry, "flanked");
			else if (AffCover[j] >= 0)
				AppendComma(Entry, class'X2AccessTacticalReader'.static.CoverWords(ECoverType(AffCover[j])));
		}
		AppendPart(Result, Entry);
	}

	if (Result == "")
	{
		if (!bSpeakEmpty)
			return "";
		Result = "No targets";
	}

	// The panel's own heading ("TARGET PREVIEW:"), localized by the game. Verified populated: the
	// class that declares it is the section XComGame.<loc> ships it under.
	Heading = class'UITacticalHUD_EnemyPreview'.default.Label;
	if (Heading == "")
		Heading = "Targets:";
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
			return "No cover";
		return "";
	}
	Result = CoverEdgeWords(CP.Flags);
	if (Result == "" && bVerbose)
		Result = "No cover";
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
		if ((Flags & (class'XComWorldData'.const.COVER_NLow << Dir)) != 0)
			class'X2AccessHelper'.static.AppendPart(Result, "Half cover" @ CardinalWord(Dir));
		else
			class'X2AccessHelper'.static.AppendPart(Result, "Full cover" @ CardinalWord(Dir));
	}
	return Result;
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
