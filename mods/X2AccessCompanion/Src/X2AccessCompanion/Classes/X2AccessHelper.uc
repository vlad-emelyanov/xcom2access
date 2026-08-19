// X2AccessHelper
// Shared static utility functions for accessibility screen listeners.
// Provides common helpers for collecting nav help text, reading widget
// values from UIMechaListItem, etc.

class X2AccessHelper extends Object;

// Collect all visible button labels from a UINavigationHelp bar.
// Returns a period-separated string of button text.
static function string CollectNavHelp(UINavigationHelp NavHelp)
{
	local string Result;

	if (NavHelp == none)
		return "";

	Result = CollectContainerButtons(NavHelp.LeftContainer);

	if (NavHelp.CenterContainer != none)
	{
		if (Result != "")
			Result $= ". ";
		Result $= CollectContainerButtons(NavHelp.CenterContainer);
	}

	if (NavHelp.RightContainer != none)
	{
		if (Result != "")
			Result $= ". ";
		Result $= CollectContainerButtons(NavHelp.RightContainer);
	}

	return Result;
}

// Collect text from all UIButton children in a container panel.
static function string CollectContainerButtons(UIPanel Container)
{
	local string Result;
	local int i;
	local UIButton Btn;

	if (Container == none)
		return "";

	for (i = 0; i < Container.ChildPanels.Length; i++)
	{
		Btn = UIButton(Container.ChildPanels[i]);
		if (Btn != none && Btn.Text != "")
		{
			if (Result != "")
				Result $= ". ";
			Result $= Btn.Text;
		}
	}

	return Result;
}

// Return "Checked" or "Unchecked" for a UIMechaListItem with a checkbox.
static function string GetCheckboxState(UIMechaListItem Item)
{
	if (Item != none && Item.Checkbox != none)
	{
		if (Item.Checkbox.bChecked)
			return "Checked";
		return "Unchecked";
	}
	return "";
}

// Return slider value as "X percent" string.
// UE3/Scaleform sliders store percent as 0-100 (not 0.0-1.0).
static function string GetSliderPercent(UIMechaListItem Item)
{
	if (Item != none && Item.Slider != none)
		return string(int(Item.Slider.percent)) $ " percent";
	return "";
}

// Return current spinner display value.
static function string GetSpinnerValue(UIMechaListItem Item)
{
	if (Item != none && Item.Spinner != none)
		return Item.Spinner.Value;
	return "";
}

// Return current dropdown selection text.
static function string GetDropdownValue(UIMechaListItem Item)
{
	if (Item != none && Item.Dropdown != none)
	{
		if (Item.Dropdown.SelectedItem >= 0 && Item.Dropdown.SelectedItem < Item.Dropdown.Items.Length)
			return Item.Dropdown.Items[Item.Dropdown.SelectedItem];
	}
	return "";
}

// Auto-detect widget type and return its current value as a TTS string.
static function string GetItemValueString(UIMechaListItem Item)
{
	if (Item == none)
		return "";

	// Check each sub-widget; only the active one is non-none after UpdateData*
	if (Item.Slider != none)
		return GetSliderPercent(Item);
	if (Item.Checkbox != none)
		return GetCheckboxState(Item);
	if (Item.Spinner != none)
		return GetSpinnerValue(Item);
	if (Item.Dropdown != none)
		return GetDropdownValue(Item);

	return "";
}

// Return tile coordinates as "X, Y" string.
static function string FormatTileCoordinates(TTile Tile)
{
	return string(Tile.X) $ ", " $ string(Tile.Y);
}

// ---------------------------------------------------------------------------
// Tactical tile description helpers
// ---------------------------------------------------------------------------

// Return "Full cover", "Half cover", or "No cover" for a world position.
static function string DescribeCover(XComWorldData WorldData, vector TilePos)
{
	if (WorldData.IsLocationHighCover(TilePos))
		return "Full cover";
	if (WorldData.IsLocationLowCover(TilePos))
		return "Half cover";
	return "No cover";
}

// Return period-separated hazard names for a tile (e.g. "Fire. Poison.").
static function string DescribeHazards(XComWorldData WorldData, TTile Tile)
{
	local string Result;

	if (WorldData.TileContainsFire(Tile))
		Result = "Fire";
	if (WorldData.TileContainsPoison(Tile))
	{
		if (Result != "")
			Result $= ". ";
		Result $= "Poison";
	}
	if (WorldData.TileContainsSmoke(Tile))
	{
		if (Result != "")
			Result $= ". ";
		Result $= "Smoke";
	}
	if (WorldData.TileContainsAcid(Tile))
	{
		if (Result != "")
			Result $= ". ";
		Result $= "Acid";
	}

	return Result;
}

// Return description of units on a tile (e.g. "Enemy: Sectoid" or "Ally: Sgt. Kelly").
static function string DescribeUnitsOnTile(TTile Tile)
{
	local XComWorldData WorldData;
	local array<StateObjectReference> UnitRefs;
	local XComGameState_Unit UnitState;
	local XComGameStateHistory History;
	local string Result;
	local string Prefix;
	local int i;

	WorldData = `XWORLD;
	History = `XCOMHISTORY;
	UnitRefs = WorldData.GetUnitsOnTile(Tile);

	for (i = 0; i < UnitRefs.Length; i++)
	{
		UnitState = XComGameState_Unit(History.GetGameStateForObjectID(UnitRefs[i].ObjectID));
		if (UnitState == none)
			continue;
		if (!UnitState.IsAlive())
			continue;

		if (UnitState.IsFriendlyToLocalPlayer())
			Prefix = "Ally: ";
		else
			Prefix = "Enemy: ";

		if (Result != "")
			Result $= ". ";
		Result $= Prefix $ UnitState.GetName(eNameType_Full);
	}

	return Result;
}
