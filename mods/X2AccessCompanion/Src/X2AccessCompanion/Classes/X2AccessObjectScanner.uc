// X2AccessObjectScanner
// The object scanner's game-state half — the model. C++ (src/object_scanner.cpp) owns the keys
// and the speech; it calls these functions on this class's default object via ProcessEvent (game
// thread, from the tick drain) and reads NativeResult back through reflection right after the call
// returns. Nothing here runs on its own — no listeners, no timers, no polling.
//
// This is SLICE 1a: the `markers` category only — the world-space things the game points the
// player at. Three contributors, deliberately one ring rather than three:
//   1. the tutorial's required-action marker (XComTutorialMgr.NextPlayerAbilityContext),
//   2. objective-carrying objects (XComGameState_ObjectiveInfo components),
//   3. the evac zone.
// A tutorial waypoint and an objective glint are the same kind of thing to a player trying to work
// out where to go, and folding them together is what stops the tutorial being a special case.
//
// NO REGISTRY (and this is a deliberate departure from the RT scanner it ports). RT needs a ticked
// WorldModel because Unity entities are mutable references; XCOM's ObjectID is a stable int over a
// versioned history, so the candidate list is rebuilt from scratch on every keypress and the only
// thing that persists is the selection — an int key plus its tile. Cheaper to reason about, and it
// cannot go stale.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessObjectScanner extends Object;

// One candidate. Local to a scan; never stored (see NO REGISTRY above).
struct AccessMarker
{
	var int Key;        // stable identity across rebuilds: a state ObjectID, or KEY_* below
	var string Label;   // what gets spoken first
	var string Detail;  // optional second clause: health, cover, a countdown
	var TTile Tile;     // where it is
	var int Dist;       // tiles from the scan origin, for the sort

	// Is this somewhere the soldier would WALK to? Movement reachability is the wrong question
	// for a marker that points at something to shoot — an enemy 8 tiles away is perfectly
	// shootable from here, and saying "out of range" about it reads as "you cannot do this".
	var bool bIsDestination;
};

// Synthetic keys for the contributors that have no state ObjectID of their own. Negative so they
// can never collide with one (ObjectIDs are positive).
const KEY_TUTORIAL = -101;

// Categories. Each has its own cycle key on the native side and its own remembered position, so
// checking an enemy and then checking a marker does not lose your place in either.
const CAT_MARKERS = 0;
const CAT_ENEMIES = 1;
const NUM_CATEGORIES = 8;   // headroom for the remaining categories; a static array, so free

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The selection. Value types only (CDO rule). X2AccessTileReader.CursorGoToMarker reads these
// through `class'X2AccessObjectScanner'.default.*` — a cross-class default READ, which is legal;
// writing another class's defaults is not, which is why the plant lives over there and not here.
// Per-category, so each cycle key resumes where it left off. A STATIC array: fixed size, value
// typed, no allocation — the CDO rule again.
var int SelKeyByCategory[8];

// The last thing actually announced, in any category. This is what `/` plants the tile cursor on,
// which is the right semantics: it acts on what you just heard, not on some notion of "current
// category".
var int SelX, SelY, SelZ;
var bool bSelValid;

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// Advance the selection within one category by Delta and speak it. Delta 0 re-announces without
// moving, which is what makes a one-entry ring answerable without cycling through it.
//
// Selection survives a rebuild by KEY, not by index: entries appear and vanish as objectives
// complete, enemies die and the tutorial advances, so an index would silently point at a
// different thing. It is remembered PER CATEGORY, so alternating between two cycle keys does not
// reset either of them.
function ScanCategory(int Category, int Delta)
{
	local array<AccessMarker> Entries;
	local TTile Origin;
	local int Idx;

	NativeResult = "";

	if (Category < 0 || Category >= NUM_CATEGORIES)
	{
		NativeResult = "Bad category" @ string(Category);
		return;
	}

	if (!ScanOrigin(Origin))
	{
		NativeResult = "No scan origin";
		return;
	}

	BuildCategory(Category, Origin, Entries);
	if (Entries.Length == 0)
	{
		bSelValid = false;
		NativeResult = EmptyText(Category);
		return;
	}

	Idx = IndexOfKey(Entries, SelKeyByCategory[Category]);
	if (Idx == INDEX_NONE)
	{
		// Nothing selected yet, or what was selected is gone: enter the ring at the nearest
		// entry rather than wherever the old index happened to land.
		Idx = (Delta < 0) ? Entries.Length - 1 : 0;
	}
	else if (Delta != 0)
	{
		Idx = (Idx + Delta) % Entries.Length;
		if (Idx < 0)
			Idx += Entries.Length;
	}

	SelKeyByCategory[Category] = Entries[Idx].Key;
	SelX = Entries[Idx].Tile.X;
	SelY = Entries[Idx].Tile.Y;
	SelZ = Entries[Idx].Tile.Z;
	bSelValid = true;

	NativeResult = FormatMarker(Entries[Idx], Origin, Idx + 1, Entries.Length);
}

// ---------------------------------------------------------------------------
// Building the ring
// ---------------------------------------------------------------------------

// Insertion-sorted by distance as each contributor adds, so every ring reads nearest-first without
// needing a sort delegate on a CDO. The lists are a handful of entries — this is not a hot path.
private function BuildCategory(int Category, TTile Origin, out array<AccessMarker> Entries)
{
	switch (Category)
	{
	case CAT_MARKERS:
		AddTutorialMarker(Origin, Entries);
		AddObjectiveMarkers(Origin, Entries);
		AddEvacMarker(Origin, Entries);
		break;
	case CAT_ENEMIES:
		AddEnemies(Origin, Entries);
		break;
	}
}

// What an empty category says. Worth spelling out per category rather than saying "nothing here":
// "No enemies visible" carries the FOG rule in the wording, so a player is not left wondering
// whether the map is clear or the scanner is broken.
private function string EmptyText(int Category)
{
	switch (Category)
	{
	case CAT_MARKERS: return "No markers";
	case CAT_ENEMIES: return "No enemies visible";
	}
	return "Nothing here";
}

// Visible enemies. Units are NEVER fog-latched (docs/object-scanner-recon.md §3.1) — unlike the
// statics, an enemy that stepped out of sight leaves the ring entirely, exactly as its unit flag
// vanishes for a sighted player. Latching them would hand a blind player information the game
// deliberately takes away.
//
// bIsDestination is false throughout: an enemy tile is something to shoot, not somewhere to stand,
// and movement reachability spoken about it reads as "you cannot do this".
private function AddEnemies(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_Unit UnitState, ActiveUnit;
	local StateObjectReference ShooterRef;
	local AccessMarker M;

	History = `XCOMHISTORY;
	ActiveUnit = class'X2AccessTileReader'.static.ActiveUnitState();
	if (ActiveUnit != none)
		ShooterRef = ActiveUnit.GetReference();

	foreach History.IterateByClassType(class'XComGameState_Unit', UnitState)
	{
		if (UnitState.GetTeam() != eTeam_Alien || UnitState.IsDead())
			continue;
		if (!class'X2TacticalVisibilityHelpers'.static.CanXComSquadSeeTarget(UnitState.ObjectID))
			continue;

		M.Key = UnitState.ObjectID;
		M.Tile = UnitState.TileLocation;
		M.bIsDestination = false;
		M.Label = UnitState.GetName(eNameType_Full);

		// The same words the shot readout uses, from the same functions — see
		// X2AccessTacticalReader.DescribeTargetCover.
		M.Detail = "";
		class'X2AccessTacticalReader'.static.AppendComma(M.Detail,
			class'X2AccessTacticalReader'.static.DescribeTargetCover(UnitState, ShooterRef));
		class'X2AccessTacticalReader'.static.AppendComma(M.Detail,
			class'X2AccessTacticalReader'.static.DescribeTargetHealth(UnitState));

		InsertByDistance(Origin, M, Entries);
	}
}

// The tutorial's required-action marker.
//
// ⛔ Read the CONTEXT, never the mesh components. The six StaticMeshComponents on XComTutorialMgr
// are a presentation artifact and are routinely hidden at (0,0,0) while an action is still
// required — UseTurnBeginTargetPrompt is false in shipped GameCore, so markers exist only where a
// map author dropped a SeqAct_ShowTutorialTarget. NextPlayerAbilityContext is the authority.
//
// ⛔ PARITY GUARD: read only AbilityTemplateName, SourceObject, PrimaryTarget, TargetLocations and
// MovementPaths[0] — the fields the marker itself renders. The same context also carries
// ResultContext, the RECORDED hit/miss of a scripted shot; speaking that would leak the outcome of
// a shot the player has not taken yet.
//
// The replay gate is bIsTutorialAllowingControls rather than a CurrentHistoryFrame comparison: the
// tutorial is a replay, so the whole mission is already in the history, but NextPlayerAbilityContext
// is the manager's own live "what I am waiting for" var and is only meaningful while it has
// actually stopped and handed control over. Reading it mid-playback would describe the future.
private function AddTutorialMarker(TTile Origin, out array<AccessMarker> Markers)
{
	local XComTutorialMgr Tut;
	local XComGameStateContext_Ability Ctx;
	local X2AbilityTemplate Template;
	local XComGameState_Unit TargetUnit;
	local AccessMarker M;
	local string Verb;
	local vector TargetPos;
	local bool bHaveTile;

	Tut = TutorialMgr();
	if (Tut == none || !Tut.bIsTutorialAllowingControls)
		return;

	Ctx = Tut.NextPlayerAbilityContext;
	if (Ctx == none)
		return;

	Template = class'XComGameState_Ability'.static.GetMyTemplateManager()
		.FindAbilityTemplate(Ctx.InputContext.AbilityTemplateName);

	Verb = "";
	if (Template != none && Template.LocFriendlyName != "")
		Verb = Template.LocFriendlyName;
	else if (Ctx.InputContext.AbilityTemplateName != '')
		Verb = string(Ctx.InputContext.AbilityTemplateName);

	// Which marker the game would render follows the ability's targeting method — mirroring
	// XComTutorialMgr.SetMoveMarkers / SetTargetLocationMarkers exactly, movement first because a
	// move context carries a path regardless of what its targeting method says.
	bHaveTile = false;
	M.bIsDestination = true;
	if (Ctx.InputContext.MovementPaths.Length > 0
		&& Ctx.InputContext.MovementPaths[0].MovementTiles.Length > 0)
	{
		M.Tile = Ctx.InputContext.MovementPaths[0].MovementTiles[
			Ctx.InputContext.MovementPaths[0].MovementTiles.Length - 1];
		bHaveTile = true;
		if (Verb == "")
			Verb = "Move";
	}
	else if (Ctx.InputContext.PrimaryTarget.ObjectID > 0)
	{
		TargetUnit = XComGameState_Unit(
			`XCOMHISTORY.GetGameStateForObjectID(Ctx.InputContext.PrimaryTarget.ObjectID));
		if (TargetUnit != none)
		{
			M.Tile = TargetUnit.TileLocation;
			bHaveTile = true;
			// Something to shoot, not somewhere to stand.
			M.bIsDestination = false;
			Verb @= "-" @ TargetUnit.GetName(eNameType_Full);
		}
	}

	if (!bHaveTile && Ctx.InputContext.TargetLocations.Length > 0)
	{
		// Via a local: GetTileCoordinatesFromPosition takes its vector as `out`, and UnrealScript
		// refuses a dynamic-array element there.
		TargetPos = Ctx.InputContext.TargetLocations[0];
		M.Tile = `XWORLD.GetTileCoordinatesFromPosition(TargetPos);
		bHaveTile = true;
	}

	if (!bHaveTile)
		return;

	M.Key = KEY_TUTORIAL;
	M.Detail = "";
	M.Label = "Tutorial";
	if (Verb != "")
		M.Label $= ":" @ Verb;
	InsertByDistance(Origin, M, Markers);
}

// Objective-carrying objects. XComGameState_ObjectiveInfo is a COMPONENT — FindComponentObject
// with bSearchOwner walks from it back to the thing it is attached to, which is where the tile is.
//
// Deliberately NOT fog-gated. That is the game's own rule, not a shortcut: UIUnitFlag and
// UISimpleFlag both special-case an ObjectiveInfo component to keep the flag visible when
// everything else would hide it, so an objective marker is exactly the thing a sighted player can
// always see (see docs/object-scanner-recon.md §3.1).
private function AddObjectiveMarkers(TTile Origin, out array<AccessMarker> Markers)
{
	local XComGameStateHistory History;
	local XComGameState_ObjectiveInfo Info;
	local XComGameState_Unit ObjUnit;
	local XComGameState_Destructible ObjDest;
	local XComGameState_InteractiveObject ObjInteract;
	local XComTacticalMissionManager MissionMgr;
	local string MissionType;
	local AccessMarker M;

	History = `XCOMHISTORY;
	MissionMgr = `TACTICALMISSIONMGR;
	MissionType = "";
	if (MissionMgr != none)
		MissionType = MissionMgr.ActiveMission.sType;

	foreach History.IterateByClassType(class'XComGameState_ObjectiveInfo', Info)
	{
		// An objective spawned for a different mission type is inert scenery here.
		if (Info.MissionType != "" && MissionType != "" && Info.MissionType != MissionType)
			continue;

		ObjUnit = XComGameState_Unit(Info.FindComponentObject(class'XComGameState_Unit', true));
		if (ObjUnit != none)
		{
			if (ObjUnit.IsDead())
				continue;
			M.Key = ObjUnit.ObjectID;
			M.Tile = ObjUnit.TileLocation;
			M.bIsDestination = true;
			M.Detail = "";
			M.Label = "Objective:" @ ObjUnit.GetName(eNameType_Full);
			InsertByDistance(Origin, M, Markers);
			continue;
		}

		// InteractiveObject extends Destructible, so ask for the more specific one first — it is
		// what carries "already used up" and the friendly name.
		ObjInteract = XComGameState_InteractiveObject(
			Info.FindComponentObject(class'XComGameState_InteractiveObject', true));
		if (ObjInteract != none)
		{
			if (ObjInteract.bRemovedFromPlay)
				continue;
			M.Key = ObjInteract.ObjectID;
			M.Tile = ObjInteract.TileLocation;
			M.bIsDestination = true;
			M.Detail = "";
			M.Label = "Objective:" @ InteractiveLabel(ObjInteract);
			InsertByDistance(Origin, M, Markers);
			continue;
		}

		ObjDest = XComGameState_Destructible(
			Info.FindComponentObject(class'XComGameState_Destructible', true));
		if (ObjDest != none)
		{
			M.Key = ObjDest.ObjectID;
			M.Tile = ObjDest.TileLocation;
			M.bIsDestination = true;
			M.Detail = "";
			M.Label = "Objective";
			InsertByDistance(Origin, M, Markers);
		}
	}
}

private function AddEvacMarker(TTile Origin, out array<AccessMarker> Markers)
{
	local XComGameState_EvacZone Evac;
	local AccessMarker M;

	Evac = class'XComGameState_EvacZone'.static.GetEvacZone(eTeam_XCom);
	if (Evac == none)
		return;

	M.Key = Evac.ObjectID;
	M.Tile = Evac.CenterLocation;
	M.bIsDestination = true;
	M.Detail = "";
	M.Label = "Evac zone";
	InsertByDistance(Origin, M, Markers);
}

// ---------------------------------------------------------------------------
// Naming
// ---------------------------------------------------------------------------

// The interactive object's own words if it has any. GetLootingName() would be the game's answer
// but it dereferences GetVisualizer() unguarded and an object whose visualizer has not been
// created yet is a crash — so this does the same read with the null check the game omits.
private function string InteractiveLabel(XComGameState_InteractiveObject Obj)
{
	local XComInteractiveLevelActor VisActor;
	local X2AbilityTemplate InteractTemplate;

	VisActor = XComInteractiveLevelActor(`XCOMHISTORY.GetVisualizer(Obj.ObjectID));
	if (VisActor != none)
	{
		// Authored words win when a level designer supplied them. Measured live 2026-08-20:
		// often EMPTY (the tutorial's own objective read blank), so it cannot be the only source
		// — which answers docs/object-scanner-recon.md §6.1.
		if (VisActor.FriendlyDisplayName != "")
			return VisActor.FriendlyDisplayName;

		// Otherwise name it by what you DO with it, which is how a player thinks of it anyway
		// and is already localized: Interact_OpenDoor -> "Open Door", Interact_HackDoor, and so
		// on. Strictly better than a bare category word, and free of any new string of ours.
		if (VisActor.InteractionAbilityTemplateName != '')
		{
			InteractTemplate = class'XComGameState_Ability'.static.GetMyTemplateManager()
				.FindAbilityTemplate(VisActor.InteractionAbilityTemplateName);
			if (InteractTemplate != none && InteractTemplate.LocFriendlyName != "")
				return InteractTemplate.LocFriendlyName;
		}
	}

	if (Obj.IsDoor())
		return "door";
	if (Obj.MustBeHacked())
		return "hackable";
	return "object";
}

// ---------------------------------------------------------------------------
// The spoken line
// ---------------------------------------------------------------------------

// `<label>, <N> tiles, <compass offset>, <reach>, <i of n>` — the same shape and the same compass
// words the tile cursor uses, so the two surfaces sound like one mod and describe one world.
private function string FormatMarker(AccessMarker M, TTile Origin, int Position, int Total)
{
	local string Result;

	Result = M.Label;
	AppendComma(Result, M.Detail);
	AppendComma(Result, string(M.Dist) @ (M.Dist == 1 ? "tile" : "tiles"));
	AppendComma(Result, class'X2AccessTileReader'.static.OffsetWords(Origin, M.Tile));
	if (M.bIsDestination)
		AppendComma(Result, ReachWords(M.Tile));
	if (Total > 1)
		AppendComma(Result, string(Position) @ "of" @ string(Total));
	return Result;
}

// Can the selected soldier get there this turn? Same source as the tile cursor's answer — the
// active unit's own reachable-tiles cache — so a marker and the tile under it never disagree.
private function string ReachWords(TTile Tile)
{
	local XComGameState_Unit UnitState;
	local XGUnit Unit;
	local float Cost;

	UnitState = class'X2AccessTileReader'.static.ActiveUnitState();
	if (UnitState == none)
		return "";
	Unit = class'X2AccessTileReader'.static.ActiveUnitVisualizer(UnitState);
	if (Unit == none || Unit.m_kReachableTilesCache == none)
		return "";
	Cost = Unit.m_kReachableTilesCache.GetPathCostToTile(Tile);
	if (Cost < 0)
		return "out of range";
	if (Cost > Unit.GetMobility())
		return "dash";
	return "1 action";
}

// ---------------------------------------------------------------------------
// Shared plumbing
// ---------------------------------------------------------------------------

// Where distances and bearings are measured from. Same rule as RT's ScanFrom(): the planted tile
// cursor if there is one this mission, else the active soldier — so browsing the ring and stepping
// the cursor describe the same world from the same place.
private function bool ScanOrigin(out TTile Origin)
{
	local XComGameState_Unit UnitState;

	if (class'X2AccessTileReader'.default.bPlanted
		&& class'X2AccessTileReader'.default.PlantedMissionID == CurrentMissionID())
	{
		Origin.X = class'X2AccessTileReader'.default.CurX;
		Origin.Y = class'X2AccessTileReader'.default.CurY;
		Origin.Z = class'X2AccessTileReader'.default.CurZ;
		return true;
	}

	UnitState = class'X2AccessTileReader'.static.ActiveUnitState();
	if (UnitState == none)
		return false;
	Origin = UnitState.TileLocation;
	return true;
}

private function int CurrentMissionID()
{
	local XComGameState_BattleData BattleData;

	BattleData = XComGameState_BattleData(
		`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData', true));
	return (BattleData != none) ? BattleData.m_iMissionID : -1;
}

private function XComTutorialMgr TutorialMgr()
{
	local XComTacticalGRI GRI;

	GRI = XComTacticalGRI(class'WorldInfo'.static.GetWorldInfo().GRI);
	if (GRI == none)
		return none;
	return XComTutorialMgr(GRI.ReplayMgr);
}

// Straight-line tile distance, max-axis in the plane plus the height difference. Deliberately NOT
// path length: a path per candidate would mean a pathfind per marker per keypress, and the number
// a player wants here is "how far away is it", not "how far must I walk".
private function int TileDistance(TTile A, TTile B)
{
	local int DX, DY;

	DX = Abs(A.X - B.X);
	DY = Abs(A.Y - B.Y);
	return Max(DX, DY) + Abs(A.Z - B.Z);
}

private function InsertByDistance(TTile Origin, out AccessMarker M, out array<AccessMarker> Markers)
{
	local int i;

	M.Dist = TileDistance(Origin, M.Tile);
	for (i = 0; i < Markers.Length; i++)
	{
		if (M.Dist < Markers[i].Dist)
		{
			Markers.Insert(i, 1);
			Markers[i] = M;
			return;
		}
	}
	Markers.AddItem(M);
}

private function int IndexOfKey(out array<AccessMarker> Markers, int Key)
{
	local int i;

	for (i = 0; i < Markers.Length; i++)
	{
		if (Markers[i].Key == Key)
			return i;
	}
	return INDEX_NONE;
}

private function AppendComma(out string Result, string Part)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= ", ";
	Result $= Part;
}
