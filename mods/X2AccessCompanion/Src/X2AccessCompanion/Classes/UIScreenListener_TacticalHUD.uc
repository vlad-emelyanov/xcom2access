// UIScreenListener_TacticalHUD
// Narrates tile information when the cursor moves during tactical gameplay.
// Speaks cover, hazards, units, floor level, and special tile properties.
//
// IMPORTANT: Do NOT store UIScreen references as class variables.
// Always find screens dynamically via the screen stack.

class UIScreenListener_TacticalHUD extends UIScreenListener;

var string AccessibilityData;
var string NavHelpData;
var TTile LastAnnouncedTile;
var bool bPolling;
var bool bExploring;
var TTile ExploreCursorTile;

event OnInit(UIScreen Screen)
{
	ResetState();
	SubscribeInput(Screen);
	StartPolling(Screen);
	`log("X2Access: TacticalHUD OnInit", , 'X2Access');
}

event OnReceiveFocus(UIScreen Screen)
{
	StartPolling(Screen);
}

event OnLoseFocus(UIScreen Screen)
{
	StopPolling(Screen);
}

event OnRemoved(UIScreen Screen)
{
	UnsubscribeInput(Screen);
	StopPolling(Screen);
	ResetState();
}

function ResetState()
{
	LastAnnouncedTile.X = -1;
	LastAnnouncedTile.Y = -1;
	LastAnnouncedTile.Z = -1;
	bExploring = false;
}

function SubscribeInput(UIScreen Screen)
{
	local X2AccessExplorePanel Panel;

	if (Screen == none)
		return;

	if (Screen.GetChildByName('X2AccessExplore', false) != none)
		return;

	Panel = Screen.Spawn(class'X2AccessExplorePanel', Screen);
	Panel.Listener = self;
	Panel.InitPanel('X2AccessExplore');
	`log("X2Access: Spawned explore panel on tactical HUD", , 'X2Access');
}

function UnsubscribeInput(UIScreen Screen)
{
	// Panel is a child of Screen — cleaned up automatically when screen is removed
}

function StartPolling(UIScreen Screen)
{
	if (Screen != none && !bPolling)
	{
		bPolling = true;
		Screen.SetTimer(0.15, false, nameof(PollTile), self);
	}
}

function StopPolling(UIScreen Screen)
{
	bPolling = false;
	if (Screen != none)
	{
		Screen.ClearTimer(nameof(PollTile), self);
		Screen.ClearTimer(nameof(X2Access_Speak), self);
	}
}

function PollTile()
{
	local UIScreen Screen;
	local TTile CurrentTile;
	local string Desc;

	Screen = FindMyScreen();
	if (Screen == none || Screen.bIsRemoved || !Screen.bIsInited)
	{
		bPolling = false;
		return;
	}

	// Exploration mode handles its own announcements via input handler
	if (bExploring)
	{
		if (bPolling)
			Screen.SetTimer(0.15, false, nameof(PollTile), self);
		return;
	}

	CurrentTile = GetCurrentCursorTile();

	if (CurrentTile.X != LastAnnouncedTile.X ||
		CurrentTile.Y != LastAnnouncedTile.Y ||
		CurrentTile.Z != LastAnnouncedTile.Z)
	{
		LastAnnouncedTile = CurrentTile;
		Desc = BuildTileDescription(CurrentTile);

		if (Desc != "")
		{
			AccessibilityData = Desc;
			Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
		}
	}

	if (bPolling)
		Screen.SetTimer(0.15, false, nameof(PollTile), self);
}

function TTile GetCurrentCursorTile()
{
	local XComTacticalController TacController;
	local XComPathingPawn PathingPawn;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Tile;

	TacController = XComTacticalController(class'WorldInfo'.static.GetWorldInfo().GetALocalPlayerController());
	if (TacController == none)
	{
		Tile.X = -1;
		Tile.Y = -1;
		Tile.Z = -1;
		return Tile;
	}

	// Prefer pathing pawn's destination tile (the actual reachable tile)
	PathingPawn = TacController.m_kPathingPawn;
	if (PathingPawn != none && !PathingPawn.bHidden &&
		PathingPawn.LastDestinationTile.X >= 0)
	{
		return PathingPawn.LastDestinationTile;
	}

	// Fall back to raw cursor position
	Cursor = TacController.GetCursor();
	if (Cursor != none)
	{
		WorldData = `XWORLD;
		if (!WorldData.GetFloorTileForPosition(Cursor.Location, Tile, false))
			Tile = WorldData.GetTileCoordinatesFromPosition(Cursor.Location);
		return Tile;
	}

	Tile.X = -1;
	Tile.Y = -1;
	Tile.Z = -1;
	return Tile;
}

function string BuildTileDescription(TTile Tile)
{
	local XComWorldData WorldData;
	local vector TilePos;
	local string Result;
	local string Part;
	local XComGameState_Unit ActiveUnit;

	if (Tile.X < 0)
		return "";

	WorldData = `XWORLD;
	TilePos = WorldData.GetPositionFromTileCoordinates(Tile);

	// Coordinates
	Result = class'X2AccessHelper'.static.FormatTileCoordinates(Tile);

	// Cover
	Result $= ". " $ class'X2AccessHelper'.static.DescribeCover(WorldData, TilePos);

	// Hazards
	Part = class'X2AccessHelper'.static.DescribeHazards(WorldData, Tile);
	if (Part != "")
		Result $= ". " $ Part;

	// Units on tile
	Part = class'X2AccessHelper'.static.DescribeUnitsOnTile(Tile);
	if (Part != "")
		Result $= ". " $ Part;

	// Floor level (relative to active unit)
	ActiveUnit = GetActiveUnit();
	if (ActiveUnit != none && Tile.Z != ActiveUnit.TileLocation.Z)
	{
		if (Tile.Z > ActiveUnit.TileLocation.Z)
			Result $= ". Up " $ string(Tile.Z - ActiveUnit.TileLocation.Z);
		else
			Result $= ". Down " $ string(ActiveUnit.TileLocation.Z - Tile.Z);
	}

	// Special tile properties
	if (WorldData.IsRampTile(Tile))
		Result $= ". Ramp";
	if (WorldData.IsWaterTile(Tile))
		Result $= ". Water";

	return Result;
}

function XComGameState_Unit GetActiveUnit()
{
	local XComTacticalController TacController;
	local XGUnit ActiveXGUnit;

	TacController = XComTacticalController(class'WorldInfo'.static.GetWorldInfo().GetALocalPlayerController());
	if (TacController == none)
		return none;

	ActiveXGUnit = TacController.GetActiveUnit();
	if (ActiveXGUnit == none)
		return none;

	return XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(ActiveXGUnit.ObjectID));
}

function UIScreen FindMyScreen()
{
	local UIScreenStack Stack;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return none;
	return Stack.GetFirstInstanceOf(class'UITacticalHUD');
}

function X2Access_Speak()
{
	// Intentionally empty — native hook reads AccessibilityData
}

// ---------------------------------------------------------------------------
// Exploration cursor — free-roaming tile inspection via keyboard
// ---------------------------------------------------------------------------

// Called by X2AccessExplorePanel when it receives input via OnUnrealCommand.
function bool HandleExploreInput(UIScreen Screen, int iInput, int ActionMask)
{
	local TTile NewTile;
	local TTile FloorTile;
	local XComWorldData WorldData;
	local vector WorldPos;
	local string Desc;
	local int DX, DY, DZ;

	// Toggle on I key (release only)
	if (iInput == class'UIUtilities_Input'.const.FXS_KEY_I)
	{
		bExploring = !bExploring;
		`log("X2Access: Inspect mode toggled, bExploring=" $ bExploring, , 'X2Access');
		if (bExploring)
		{
			InitExploreCursor();
			Desc = BuildTileDescription(ExploreCursorTile);
			AccessibilityData = "Inspect mode. " $ Desc;
		}
		else
		{
			AccessibilityData = "Inspect off";
			LastAnnouncedTile.X = -1;
		}
		if (Screen != none)
			Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
		return true;
	}

	if (!bExploring)
		return false;

	DX = 0;
	DY = 0;
	DZ = 0;

	switch (iInput)
	{
		case class'UIUtilities_Input'.const.FXS_ARROW_UP:
		case class'UIUtilities_Input'.const.FXS_DPAD_UP:
			DY = 1;
			break;
		case class'UIUtilities_Input'.const.FXS_ARROW_DOWN:
		case class'UIUtilities_Input'.const.FXS_DPAD_DOWN:
			DY = -1;
			break;
		case class'UIUtilities_Input'.const.FXS_ARROW_LEFT:
		case class'UIUtilities_Input'.const.FXS_DPAD_LEFT:
			DX = -1;
			break;
		case class'UIUtilities_Input'.const.FXS_ARROW_RIGHT:
		case class'UIUtilities_Input'.const.FXS_DPAD_RIGHT:
			DX = 1;
			break;
		case class'UIUtilities_Input'.const.FXS_KEY_PAGEUP:
			DZ = 1;
			break;
		case class'UIUtilities_Input'.const.FXS_KEY_PAGEDN:
			DZ = -1;
			break;
		default:
			return false;
	}

	if (Screen == none)
		return false;

	NewTile = ExploreCursorTile;
	NewTile.X += DX;
	NewTile.Y += DY;
	NewTile.Z += DZ;

	WorldData = `XWORLD;
	NewTile.X = Clamp(NewTile.X, 0, WorldData.NumX - 1);
	NewTile.Y = Clamp(NewTile.Y, 0, WorldData.NumY - 1);
	NewTile.Z = Clamp(NewTile.Z, 0, WorldData.NumZ - 1);

	WorldPos = WorldData.GetPositionFromTileCoordinates(NewTile);
	if (!WorldData.GetFloorTileForPosition(WorldPos, FloorTile, false))
	{
		AccessibilityData = "Edge";
		Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
		return true;
	}

	NewTile = FloorTile;

	if (NewTile.X == ExploreCursorTile.X &&
		NewTile.Y == ExploreCursorTile.Y &&
		NewTile.Z == ExploreCursorTile.Z)
	{
		return true;
	}

	ExploreCursorTile = NewTile;
	Desc = BuildTileDescription(NewTile);
	if (Desc != "")
	{
		AccessibilityData = Desc;
		Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	}
	return true;
}

function InitExploreCursor()
{
	local XComGameState_Unit ActiveUnit;

	ActiveUnit = GetActiveUnit();
	if (ActiveUnit != none)
	{
		ExploreCursorTile = ActiveUnit.TileLocation;
	}
	else
	{
		ExploreCursorTile = GetCurrentCursorTile();
	}
}

defaultProperties
{
	ScreenClass = class'UITacticalHUD'
}
