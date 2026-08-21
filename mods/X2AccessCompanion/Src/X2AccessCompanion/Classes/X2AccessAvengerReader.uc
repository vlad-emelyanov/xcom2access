// X2AccessAvengerReader
// The Avenger's ant farm (UIFacilityGrid) — phase 1 of the strategy layer. Same contract as
// X2AccessTacticalReader: the C++ layer owns the gate, the keys and the speech, calls these
// functions on this class's default object via ProcessEvent (game thread, from the frame pump)
// and reads NativeResult back through reflection right after the call returns. Nothing here
// runs on its own — no listeners, no timers, no polling.
//
// WHY THIS LAYER CLAIMS NO ARROW KEYS. Every other navigable surface in this project needed a
// Graph recipe or a cursor because the game had no keyboard path at all. The ant farm is the
// opposite case: the game ships a complete eight-way spatial navigation graph for it and simply
// never installs it on mouse+keyboard. UIFacilityGrid.InitScreen calls InitializeNavigationGrid()
// inside `if( `ISCONTROLLERACTIVE)`, and `ISCONTROLLERACTIVE is not a hardware check — it is the
// 2D movie's mouse-active flag (Core/Globals.uci:207). So calling that one function ourselves
// gives a keyboard player the game's own room-to-room navigation, camera swoop and focus
// handling, with nothing displaced and no key taken. This class is then narrate-only: install
// the graph, watch Navigator.SelectedIndex, say what the selection landed on.
//
// See docs/avenger-view-recon.md §6.1 for the four source facts that make one call sufficient,
// and for the live confirmation that arrows then walk the hardcoded graph exactly.
//
// WHERE THE WORDS COME FROM: the overlay itself, not the game state. UIFacilityGrid_FacilityOverlay
// retains Label / Status / Queue / bLocked / bUnderConstruction / m_bClearing / IconTypes /
// BGColor — each written by a setter under a dirty check immediately before the matching Flash
// op, so the retained value is exactly what Flash was last told. That is the CLAUDE.md hard rule
// ("when a fact is on a screen, read it FROM that screen") in its easiest form, and here it buys
// parity for free: the room NAME is set under `if( bIsFocused )`, so it appears when the game
// decides to show it and is empty when the game decides not to. There is not one XCOMHISTORY
// access in this class.
//
// SIGHTED PARITY, per surface:
//   room name          -> kData.strTitle, drawn only for the focused/hovered room
//   countdown, PAUSED  -> kData.strLabel on a room under construction or being cleared
//   queue message      -> kData.strQueue, focused only
//   locked             -> eUIState_Disabled border + iAlpha 0, i.e. the room is visibly dimmed
//   needs attention    -> eUIState_Warning border, drawn whether or not the room is focused
//   staff slots        -> the staff icon strip, always drawn
//   which room is selected -> the highlight box, which follows the keyboard once ungated
//
// ⛔ A room the game gives no title is announced by POSITION only. During the tutorial the whole
// ship is bTutorialLocked and UpdateTutorialLocked() is the one update branch that sets no title
// in any case, focused or not — a sighted player selecting a room there is told nothing either
// (screenshot-verified 2026-08-20). Speaking a name we looked up ourselves would hand a blind
// player information the game is deliberately withholding from everyone.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

// dependson(UIStaffIcon): EUIStaffIconType is declared inside that class, not in the native
// UI_Definitions, so the staff-icon switch below needs it in scope from another package.
class X2AccessAvengerReader extends Object
	dependson(UIStaffIcon);

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// Change detection for the automatic poll. Value types only (CDO rule).
// INDEX_UNSET means "nothing announced yet", which is also the reset state.
var int LastAnnouncedIndex;
var bool bWasOnGrid;

// One warning per visit when the ship does not have the 19 rooms the game's own function
// hardcodes. Without it the line would repeat at the poll rate for as long as the grid is up.
var bool bWarnedOverlayCount;

const INDEX_UNSET = -2;

// The ant farm's 19 slots are a fixed, hardcoded layout: InitializeNavigationGrid names seven
// core rooms by index and wires a 3-wide by 4-tall buildable core between them. The buildable
// slots are indices 3..14 in row-major order, which is what makes "row R, column C" exact.
const CORE_FIRST = 3;
const CORE_LAST = 14;
const CORE_COLUMNS = 3;

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// The automatic narrator, called on the C++ tick while UIFacilityGrid is the current screen.
// Two jobs: keep the navigation graph installed, and announce the selection when it moves.
//
// Deliberately silent whenever the selection has not changed — this runs several times a second.
// AnnounceRoom below is the one that always answers.
function PollGrid()
{
	local UIFacilityGrid Grid;
	local int Index;

	NativeResult = "";

	Grid = GetGrid();
	if (Grid == none)
	{
		// Left the ant farm: forget what was announced so coming back reads in full.
		if (bWasOnGrid)
		{
			bWasOnGrid = false;
			bWarnedOverlayCount = false;
			LastAnnouncedIndex = INDEX_UNSET;
		}
		return;
	}

	EnsureNavigationGrid(Grid);

	Index = Grid.Navigator.SelectedIndex;

	// First sighting of this visit announces where the keyboard cursor already is. The game
	// leaves a room selected (OnInit does Navigator.SetSelected(FacilityOverlays[15])) but hides
	// the highlight until the first input — so strictly a sighted player sees no box yet. We
	// speak it anyway: arriving on a screen and hearing nothing at all is indistinguishable from
	// the layer being broken, and the very next arrow reveals the same box for everyone.
	if (!bWasOnGrid)
		bWasOnGrid = true;
	else if (Index == LastAnnouncedIndex)
		return;

	LastAnnouncedIndex = Index;
	NativeResult = DescribeSelection(Grid, Index);
}

// Re-announce the selected room on demand (Delete), mirroring the tile cursor's re-announce key.
// Unlike the poll this ALWAYS says something — a pressed key must never be silent.
function AnnounceRoom()
{
	local UIFacilityGrid Grid;

	Grid = GetGrid();
	if (Grid == none)
	{
		NativeResult = "Not on the Avenger grid";
		return;
	}

	EnsureNavigationGrid(Grid);

	LastAnnouncedIndex = Grid.Navigator.SelectedIndex;
	NativeResult = DescribeSelection(Grid, LastAnnouncedIndex);
	if (NativeResult == "")
		NativeResult = "No room selected";
}

// ---------------------------------------------------------------------------
// The ungate
// ---------------------------------------------------------------------------

// Install the game's own navigation graph if it is not installed already.
//
// Idempotent by construction — InitializeNavigationGrid starts by clearing every overlay's
// targets — but the emptiness test is what makes this cheap enough to run on every poll AND
// self-healing: a grid re-created by a map transition comes back with no targets and is
// re-armed on the next tick without us tracking screen identity (which a CDO must not do
// anyway, since holding the reference would block shell-world GC).
//
// The test reads the FIRST overlay's own navigator rather than the screen's, because that is
// where the graph actually lives: UINavigator.OnUnrealCommand tries SendToNavigationTargets
// first and that delegates to GetSelected().Navigator, so the targets hang off the overlays.
private function EnsureNavigationGrid(UIFacilityGrid Grid)
{
	// InitializeNavigationGrid hardcodes `i < 19` and indexes FacilityOverlays[0..18]. A mod
	// that changed the room count would break the game's own function, not ours — so refuse
	// rather than call it into an out-of-range read.
	if (Grid.FacilityOverlays.Length != 19)
	{
		if (bWarnedOverlayCount)
			return;
		bWarnedOverlayCount = true;
		`log("X2Access: Avenger grid has" @ string(Grid.FacilityOverlays.Length)
			@ "overlays, not 19 - leaving navigation alone", , 'X2Access');
		return;
	}

	if (Grid.FacilityOverlays[0] == none || Grid.FacilityOverlays[0].Navigator == none)
		return; // the overlays are still being built; the next tick will find them

	if (Grid.FacilityOverlays[0].Navigator.NavigationTargets.Length > 0)
		return; // already installed — by us on an earlier tick, or by the controller branch

	Grid.InitializeNavigationGrid();

	// The controller branch of InitScreen sets this on the SCREEN's navigator too. The graph
	// works without it (see above), but it suppresses UINavigator's linear Prev/Next fallback
	// at graph edges — without it, an arrow with no target in that direction jumps to an
	// unrelated room in map order instead of doing nothing.
	Grid.Navigator.OnlyUsesNavTargets = true;

	// A grid with nothing selected would swallow the first arrow. OnInit normally leaves
	// FacilityOverlays[15] selected, so this is a guard rather than the usual path — and it is
	// only ever taken when arrows would otherwise be dead, which is why moving the camera
	// unbidden is acceptable here and nowhere else.
	if (Grid.Navigator.SelectedIndex < 0)
		Grid.Navigator.SetSelected(Grid.FacilityOverlays[15]);

	`log("X2Access: Avenger navigation grid installed;"
		@ "overlay 0 targets=" $ string(Grid.FacilityOverlays[0].Navigator.NavigationTargets.Length)
		@ "selected=" $ string(Grid.Navigator.SelectedIndex), , 'X2Access');
	LogRoomGeometry(Grid);
}

// ---------------------------------------------------------------------------
// Describing a room
// ---------------------------------------------------------------------------

// Navigator.SelectedIndex indexes NavigableControls, and the two arrays are in the same order —
// confirmed live 2026-08-20 by six arrow steps whose indices matched the hardcoded graph exactly
// (docs/avenger-view-recon.md §6.1). So a selection index IS a FacilityOverlays index, which is
// what makes the fixed layout usable at runtime.
private function string DescribeSelection(UIFacilityGrid Grid, int Index)
{
	local UIFacilityGrid_FacilityOverlay Overlay;
	local string Result, Condition, Status;

	if (Index < 0 || Index >= Grid.FacilityOverlays.Length)
		return "";

	Overlay = Grid.FacilityOverlays[Index];
	if (Overlay == none)
		return "";

	Condition = DescribeCondition(Overlay);
	Status = Overlay.Status;

	// The condition word and the status line can be the SAME word, taken twice off the same
	// widget: the moment a room is merely un-excavated rather than tutorial-locked, the game
	// puts m_strLocked into the status line, and that is exactly what DescribeCondition read.
	// InStr rather than equality because the build-mode branch wraps it in GetColoredText
	// markup, which is still present at this point (native strips it after we return).
	if (Condition != "" && InStr(Status, Condition) != INDEX_NONE)
		Status = "";

	// The name first when the game is showing one, then always the position: the highlight box
	// is what a sighted player reads the selection off, and its place in the ship is the part
	// of that they can see even when no title is drawn.
	Result = Overlay.Label;
	AppendPart(Result, PositionWords(Index));
	AppendPart(Result, Condition);
	AppendPart(Result, Status);
	AppendPart(Result, Overlay.Queue);
	AppendPart(Result, DescribeStaffIcons(Overlay));

	return Result;
}

// The room's state, from the retained booleans the overlay's own setters wrote.
//
// "Locked" is the one word here that closes a real parity gap rather than mirroring text.
// UIFacilityGrid_FacilityOverlay.OnConfirm opens with a bare `if( bLocked ) return;` — no sound,
// no popup, nothing. A sighted player is not told either, but they can SEE the room is dimmed
// (eUIState_Disabled border, iAlpha 0) and so never presses it twice; a blind player gets pure
// silence and no way to tell "locked" from "the mod is broken". Same reasoning as the object
// scanner's tower category naming its own gate instead of claiming the map is empty.
private function string DescribeCondition(UIFacilityGrid_FacilityOverlay Overlay)
{
	// The two words the WIDGET already owns, localized. Reading them off the overlay instead of
	// writing our own English is the same rule as everything else in this class, and it matters
	// more here than it looks: the first live run spoke "Мостик. top right" and
	// "row 3, column 3. locked" to a player whose game is Russian. m_strLocked is literally what
	// the game prints in the status line once a room is un-excavated rather than tutorial-locked,
	// so on that room this stops being our gloss and becomes the screen's own text.
	if (Overlay.bLocked)
		return Overlay.m_strLocked;
	if (Overlay.bUnderConstruction)
		return Overlay.m_strUnderConstruction;

	// Clearing needs no word of ours: UpdateClearingRoom puts Room.GetClearingInProgressLabel()
	// straight into strTitle — unconditionally, not focus-gated — so Label already says
	// "РАСЧИСТКА ИНОПЛАНЕТНЫХ ОБЛОМКОВ" and repeating it would be noise. The fallback covers the
	// one case that leaves no title: a cleared flag with the project already gone.
	if (Overlay.m_bClearing)
		return Overlay.Label != "" ? "" : "being cleared";

	// The orange border. On a built or core facility eUIState_Warning means exactly
	// Facility.NeedsAttention() (UpdateFacility / UpdateCoreFacility), and unlike the room's
	// name it is drawn whether or not the room is focused — so it is part of the at-a-glance
	// read a sighted player gets from the ant farm without hovering anything.
	//
	// ⚠ English, unavoidably: this one is a gloss of a BORDER COLOUR and the game ships no
	// string for it — the whole overlay localization block has none. Same for the position and
	// staff phrasing below. Those are the words a companion-side vocabulary would own, which is
	// a decision rather than a fix; the two above needed neither.
	if (Overlay.BGColor == class'UIUtilities_Colors'.static.GetHexColorFromState(eUIState_Warning))
		return "needs attention";

	return "";
}

// The staff icon strip, which is always drawn and is the signal that answers "where can I put
// this engineer". IconTypes is the retained array RefreshStatusIcons was last handed, so the
// count and the kinds are exactly what is on screen.
//
// The empties are counted PER KIND rather than listed one per icon: an empty engineer slot and
// an empty scientist slot are different jobs, but two empty engineer slots are one job twice,
// and "engineer slot, engineer slot free" would be noise where "2 engineer slots free" is the
// fact. Five counters rather than an array indexed by the enum, because the five that exist are
// the whole set and naming them keeps the wording next to the count.
private function string DescribeStaffIcons(UIFacilityGrid_FacilityOverlay Overlay)
{
	local int i, Filled, Total, Upgrades;
	local int FreeGeneric, FreeScience, FreeEngineer, FreeSoldier, FreePsi;
	local string Result, Empties;

	for (i = 0; i < Overlay.IconTypes.Length; i++)
	{
		// eUIFG_Upgrade is the one entry that is not a staff slot at all — it marks a facility
		// that can be upgraded — so it is counted apart and never folded into the ratio.
		if (Overlay.IconTypes[i] == eUIFG_Upgrade)
		{
			Upgrades++;
			continue;
		}
		Total++;

		switch (Overlay.IconTypes[i])
		{
		case eUIFG_GenericStaff:
		case eUIFG_Science:
		case eUIFG_Engineer:
		case eUIFG_Soldier:
		case eUIFG_PsiSoldier:         Filled++;      break;

		case eUIFG_ScienceEmpty:       FreeScience++;  break;
		case eUIFG_EngineerEmpty:      FreeEngineer++; break;
		case eUIFG_SoldierEmpty:       FreeSoldier++;  break;
		case eUIFG_PsiSoldierEmpty:    FreePsi++;      break;
		default:                       FreeGeneric++;  break;
		}
	}

	if (Total > 0)
		Result = string(Filled) $ " of " $ string(Total) $ " staff slots filled";

	AppendComma(Empties, CountedSlots(FreeScience, "scientist"));
	AppendComma(Empties, CountedSlots(FreeEngineer, "engineer"));
	AppendComma(Empties, CountedSlots(FreeSoldier, "soldier"));
	AppendComma(Empties, CountedSlots(FreePsi, "psi soldier"));
	AppendComma(Empties, CountedSlots(FreeGeneric, "staff"));
	if (Empties != "")
		AppendPart(Result, Empties $ " free");

	if (Upgrades > 0)
		AppendPart(Result, string(Upgrades) $ " upgrade slot" $ (Upgrades == 1 ? "" : "s"));

	return Result;
}

private function string CountedSlots(int Count, string Kind)
{
	if (Count <= 0)
		return "";
	return string(Count) @ Kind @ "slot" $ (Count == 1 ? "" : "s");
}

// Where the highlight box is, in words.
//
// Derived from the CARDINAL half of InitializeNavigationGrid's own adjacency graph (the 51
// diagonal links are FXS_VIRTUAL_LSTICK_DIAG_* only and have no keyboard chord at all), so the
// buildable core is EXACT: it is 3 wide and 4 tall at indices 3..14 row-major, and every one of
// those rooms' Left/Right/Up/Down links agrees with the row and column printed here.
//
// ⚠ The seven fixed rooms are the graph's relations put into WORDS, and that step is a
// judgement rather than a measurement. The first live run caught one: slot 15 read "top right"
// while slot 2 read "right side, upper", even though the graph has 15 -> Right -> 2, so 15
// cannot be the rightmost thing on the top row. The row now reads left / middle / right across
// 0, 15, 2, and the right-hand column reads top / middle / bottom down 2, 18, 17.
//
// The durable fix is to stop guessing: LogRoomGeometry below dumps every overlay's own
// m_RoomLocation once per grid, so the next live run hands us the real coordinates and this
// table can be replaced with words derived from them.
//
// ⛔ These are POSITIONS, never facility names — and the live run showed why that matters more
// than it seemed. InitializeNavigationGrid's local variables call slot 1 "PowerCore", slot 2
// "Hangar" and slot 17 "Storage", but the player hears "Исследовательский отсек", "Оружейная"
// and "Инженерный отсек". Firaxis' own shortcutRoomIndices comments (1 //Research, 17
// //Engineering, 2 //Armory) agree with the player, not with their own variable names. A name
// table built from the source would have been wrong for three of seven rooms; the overlay's own
// Label is the only place this class takes a name from.
private function string PositionWords(int Index)
{
	local int Row, Column;

	if (Index >= CORE_FIRST && Index <= CORE_LAST)
	{
		Row = (Index - CORE_FIRST) / CORE_COLUMNS + 1;
		Column = (Index - CORE_FIRST) % CORE_COLUMNS + 1;
		return "row " $ string(Row) $ ", column " $ string(Column);
	}

	switch (Index)
	{
	case 0:  return "top left";
	case 15: return "top middle";
	case 2:  return "top right";
	case 16: return "upper left";
	case 1:  return "far left";
	case 18: return "middle right";
	case 17: return "lower right";
	}
	return "";
}

// One line naming every overlay's world position, emitted once when the graph is installed.
//
// This exists to retire the guesswork in PositionWords: the ant farm is a camera view of real
// rooms, m_RoomLocation is the room's own location (InitFacilityOverlay takes it straight from
// RoomState.GetLocation()), and a single dump of all 19 is enough to work out which world axis
// is screen-horizontal — the Left/Right links say which direction each axis runs — and to
// replace seven hand-written phrases with measured ones. RoomMapIndex rides along because the
// whole index scheme rests on FacilityOverlays being sorted into MapIndex order, and this is
// the cheapest possible check of that assumption.
private function LogRoomGeometry(UIFacilityGrid Grid)
{
	local int i;
	local string Line;
	local Vector Loc;

	for (i = 0; i < Grid.FacilityOverlays.Length; i++)
	{
		if (Grid.FacilityOverlays[i] == none)
			continue;
		Loc = Grid.FacilityOverlays[i].m_RoomLocation;
		Line $= " " $ string(i) $ "/" $ string(Grid.FacilityOverlays[i].RoomMapIndex)
			$ ":(" $ string(int(Loc.X)) $ "," $ string(int(Loc.Y)) $ "," $ string(int(Loc.Z)) $ ")";
	}

	`log("X2Access: Avenger room geometry (slot/mapIndex:x,y,z)" $ Line, , 'X2Access');
}

// ---------------------------------------------------------------------------
// Shared plumbing (mirrors X2AccessTacticalReader's)
// ---------------------------------------------------------------------------

// The ant farm, or none. The C++ gate has already decided UIFacilityGrid is the CURRENT screen
// before calling in — this lookup is the script-side half of the same question, and it also
// honours the grid's own visibility test, which its OnUnrealCommand consults before handling
// anything. (That is the documented exception to "never gate readiness on bIsVisible": here the
// game itself gates on it, so mirroring it is what keeps us agreeing with where arrows go.)
// ⛔ BUILD MODE PUTS A SCREEN ON TOP OF THE GRID, and the grid still owns the arrows.
// GetCurrentScreen() is the first stack entry, which in build mode is UIBuildFacilities — so a
// bare cast answers `none`, this function bails, and the ant farm goes silent on the ONE screen
// where a player is choosing a room to build in. Worse, the none branch clears bWasOnGrid, so it
// forgets where the cursor was as well. Measured live 2026-08-21: ten arrow presses in build
// mode, the gate reporting active throughout, and not one word spoken.
//
// Native's ReadAvengerGate already tolerates exactly this one screen; the companion never got
// the same rule, so the two halves disagreed about whether the player was on the grid.
//
// The tolerance is not a guess about layering: UIBuildFacilities.OnUnrealCommand ends by calling
// `HQPRES.m_kFacilityGrid.Navigator.OnUnrealCommand, so the grid its keys drive IS
// `HQPRES.m_kFacilityGrid — read from the same place the game reads it.
private function UIFacilityGrid GetGrid()
{
	local UIScreenStack Stack;
	local UIFacilityGrid Grid;
	local UIScreen Current;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return none;

	Current = Stack.GetCurrentScreen();
	Grid = UIFacilityGrid(Current);

	if (Grid == none && UIBuildFacilities(Current) != none)
		Grid = `HQPRES.m_kFacilityGrid;

	if (Grid == none || !Grid.bIsVisible || Grid.Navigator == none)
		return none;

	return Grid;
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
