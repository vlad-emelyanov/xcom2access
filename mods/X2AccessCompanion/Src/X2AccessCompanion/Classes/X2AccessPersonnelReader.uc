// X2AccessPersonnelReader
// The personnel roster (UIPersonnel and its ten subclasses) — the words half, plus the two verbs
// vanilla leaves on the mouse. Same contract as X2AccessCommodityReader and X2AccessSaveReader:
// the C++ layer owns the gate, the keys and the speech, calls an entry point here on this class's
// default object via ProcessEvent from the graph recipe's build, and reads NativeResult back
// through reflection right after the call returns. No listeners, no timers, no polling.
//
// ONE SCREEN, TEN DOORS. UIPersonnel is the crew list wherever the game asks "which person?" —
// the Living Quarters' PERSONNEL, the Bar's obituaries (BOEBЫE ПОТЕРИ), the Armory's soldier
// list, choosing a scientist for a research project, staffing a facility, picking a squad for a
// covert action, the training centre, boosting a soldier. The subclasses change which array is
// filled and which tabs exist; the layout, the row widgets and the input path are all the base
// class's, so one reader covers every door.
//
// ⛔ THE ROWS RETAIN ONLY A UnitRef. UIPersonnel_ListItem extends UIButton, but its UpdateData
// resolves the name, the status and the location into LOCALS and pushes them through
// MC.BeginFunctionOp("UpdateData") — bypassing UIButton.SetText, so the retained Text is EMPTY on
// a row that is visibly showing a soldier's name. (Same trap as the debris room's personnel
// picker and the commodity list's rows; it is the house style for this game's list items.) What
// the row DOES keep is the StateObjectReference it was built from, so every line below is
// re-derived from that unit exactly the way the row derived it — UpdateData for UpdateData, one
// per row class:
//
//   UIPersonnel_SoldierListItem  rank, name, class, status(+time), location, mental state, promote
//   UIPersonnel_DeceasedListItem name, kills, missions, last operation, date of death
//   UIPersonnel_ListItem         name, status, location            (scientists and engineers)
//
// The numeric columns get their LABELS from m_strButtonLabels[], which is the same localized
// array the sort headers are drawn from — so "3" reads as "УБИЙСТВА: 3" rather than as a bare
// number a sighted player would have read off a column heading.
//
// ⛔ NO Caps(). Every row calls Caps() on the name before handing it to Flash; that is a visual
// style, not information, and a screen reader that spells out all-caps words would turn every
// soldier into an acronym.
//
// WHAT IS ALREADY REACHABLE, and what is not. Unlike the debris room, this screen is not a
// rescue: UIPersonnel.OnUnrealCommand already walks the list on Up/Down (via the default branch,
// into m_kList.Navigator), switches tabs on Left/Right, accepts on Enter/Space and backs out on
// Escape — all on mouse+keyboard. Two things are NOT reachable:
//   * The tab STRIP has no voice. CreateTabButton calls DisableNavigation() and sets
//     bIsNavigable = false on every tab, and SwitchTab announces the change only by pushing
//     "isSelected" into Flash. Left/Right work and say nothing.
//   * SORTING is mouse-only. The keyboard branches for it are FXS_BUTTON_X and FXS_DPAD_LEFT /
//     FXS_DPAD_RIGHT — controller constants, all three — so on mouse+keyboard the only way to
//     change the sort column is to CLICK a UIFlipSortButton header. On a roster of twenty
//     soldiers the sort order IS the reading order, which makes this the one verb here worth
//     re-providing.
//
// ⛔ Sorting is re-provided through the game's OWN mouse handler
// (UIFlipSortButton.OnMouseEvent with FXS_L_MOUSE_UP), not by poking SetSortType. That handler
// carries the parts that are easy to forget: deselecting every OTHER header, flipping the
// direction when the column was already selected rather than resetting it, and calling
// RefreshData() on the screen afterwards.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessPersonnelReader extends Object
	dependson(UIPersonnel);

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants stops:
//   kind=header title                                        (only when the strip is hidden)
//   kind=tab    tab=<EUIPersonnelType>  label  selected      (omitted when there is one tab)
//   kind=sort   index=<i>  label  selected  desc             (the VISIBLE header's columns)
//   kind=row    index=<i>  name rank class status time mental loc kills missions op date promote
//   kind=empty  text                                          (the game's own empty-list line)
//
// Every index is the GAME's — the list's own item index for a row, the header's own child index
// for a sort column — because that is what SetSelectedIndex, OnButtonCallback and our own
// activation calls are expressed in.
function DescribePersonnel()
{
	local UIPersonnel Screen;

	NativeResult = "";

	Screen = UIPersonnel(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	AppendHeader(Screen);
	AppendTabs(Screen);
	AppendSortColumns(Screen);
	AppendRows(Screen);
}

// ⛔ Title is set by SetScreenHeader, which RefreshTitle calls ONLY when a single tab is needed
// — with a strip up there is no header on screen at all, and the tab names are the heading a
// sighted player reads instead. So this record is the screen's own text or nothing; it never
// invents a name for a screen that is not showing one.
private function AppendHeader(UIPersonnel Screen)
{
	if (Screen.Title != "")
		Emit("kind=header" $ FLD_SEP $ "title=" $ Screen.Title);
}

// ---------------------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------------------

// ⛔ Mirrors RefreshTitle: when only ONE tab is needed the game HIDES the whole strip and puts
// that tab's name in the screen header instead. Emitting a single tab record would offer a stop
// for a control that is not on screen and cannot be switched away from.
private function AppendTabs(UIPersonnel Screen)
{
	local string Rec;
	local int i, Tab;

	if (Screen.m_arrNeededTabs.Length < 2)
		return;

	for (i = 0; i < Screen.m_arrNeededTabs.Length; i++)
	{
		Tab = Screen.m_arrNeededTabs[i];

		Rec = "kind=tab";
		Rec $= FLD_SEP $ "tab=" $ string(Tab);
		Rec $= FLD_SEP $ "label=" $ TabLabel(Screen, Tab);
		if (Screen.m_eCurrentTab == Tab)
			Rec $= FLD_SEP $ "selected=1";
		Emit(Rec);
	}
}

private function string TabLabel(UIPersonnel Screen, int Tab)
{
	switch (Tab)
	{
	case eUIPersonnel_Soldiers:   return Screen.m_strSoldierTab;
	case eUIPersonnel_Scientists: return Screen.m_strScientistTab;
	case eUIPersonnel_Engineers:  return Screen.m_strEngineerTab;
	case eUIPersonnel_Deceased:   return Screen.m_strDeceasedTab;
	}
	return "";
}

// ---------------------------------------------------------------------------------------
// Sort columns
// ---------------------------------------------------------------------------------------

// The sort headers are three sibling panels, exactly one of which is shown for the current tab
// (SwitchTab does the Show/Hide). Each holds UIFlipSortButtons that RETAIN everything worth
// speaking — Label and Value under dirty checks, plus bArrowVisible for "this is the sort
// column" and bArrowFlipped for its direction. So unlike the rows, no re-derivation is needed
// here: the widget really does know what it says.
private function AppendSortColumns(UIPersonnel Screen)
{
	local array<UIPanel> Buttons;
	local UIFlipSortButton SortButton;
	local UIPanel Header;
	local string Rec;
	local int i;

	Header = VisibleSortHeader(Screen);
	if (Header == none)
		return;

	Header.GetChildrenOfType(class'UIFlipSortButton', Buttons);

	for (i = 0; i < Buttons.Length; i++)
	{
		SortButton = UIFlipSortButton(Buttons[i]);
		if (SortButton == none || SortButton.Label == "")
			continue;

		Rec = "kind=sort";
		Rec $= FLD_SEP $ "index=" $ string(i);
		Rec $= FLD_SEP $ "label=" $ SortButton.Label;
		if (SortButton.bArrowVisible)
		{
			Rec $= FLD_SEP $ "selected=1";
			// bArrowFlipped is the arrow the game draws, which is m_bFlipSort — the screen's
			// own reversed-order flag. Reported as a bit; native gives it a word.
			if (SortButton.bArrowFlipped)
				Rec $= FLD_SEP $ "desc=1";
		}
		Emit(Rec);
	}
}

private function UIPanel VisibleSortHeader(UIPersonnel Screen)
{
	switch (Screen.m_eCurrentTab)
	{
	case eUIPersonnel_Soldiers: return Screen.m_kSoldierSortHeader;
	case eUIPersonnel_Deceased: return Screen.m_kDeceasedSortHeader;
	case eUIPersonnel_Scientists:
	case eUIPersonnel_Engineers: return Screen.m_kPersonnelSortHeader;
	}
	return none;
}

// ---------------------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------------------

private function AppendRows(UIPersonnel Screen)
{
	local UIPersonnel_ListItem Row;
	local XComGameState_Unit Unit;
	local string Rec;
	local int i;

	if (Screen.m_kList == none)
		return;

	if (Screen.m_kList.ItemCount == 0)
	{
		Emit("kind=empty" $ FLD_SEP $ "text=" $ EmptyText(Screen));
		return;
	}

	for (i = 0; i < Screen.m_kList.ItemCount; i++)
	{
		Row = UIPersonnel_ListItem(Screen.m_kList.GetItem(i));
		if (Row == none)
			continue;

		Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Row.UnitRef.ObjectID));
		if (Unit == none)
			continue;

		Rec = "kind=row" $ FLD_SEP $ "index=" $ string(i);

		if (UIPersonnel_DeceasedListItem(Row) != none)
			Rec $= DeceasedFields(Screen, Unit);
		else if (UIPersonnel_SoldierListItem(Row) != none)
			Rec $= SoldierFields(Unit);
		else
			Rec $= StaffFields(Unit);

		Emit(Rec);
	}
}

// UIPersonnel_SoldierListItem.UpdateData, re-derived. Its status is the SEPARATE form — status,
// a time label and a time value — because an active soldier's mental state overwrites the last
// two, which is how "СОСРЕДОТОЧЕН" and its countdown reach the same columns a wounded soldier's
// recovery days use.
private function string SoldierFields(XComGameState_Unit Unit)
{
	local X2SoldierClassTemplate SoldierClass;
	local string Out, Status, TimeLabel, TimeValue, Mental;
	local int TimeNum;

	SoldierClass = Unit.GetSoldierClassTemplate();

	class'UIUtilities_Strategy'.static.GetPersonnelStatusSeparate(Unit, Status, TimeLabel, TimeValue);

	if (Unit.IsActive())
	{
		Unit.GetMentalStateStringsSeparate(Mental, TimeLabel, TimeNum);
		TimeValue = (TimeNum == 0) ? "" : string(TimeNum);
	}

	// The row itself reads SoldierClass.DataName unguarded and SoldierClass.DisplayName guarded,
	// which is a shipped inconsistency rather than a promise; both are guarded here, because an
	// Accessed None from OUR class in the game's log is noise in the one place a bug gets
	// diagnosed. A rank name resolves without the class — GetShortRankName falls back to the
	// generic ladder — so nothing is lost by asking for it that way.
	Out = FLD_SEP $ "rank=" $ class'X2ExperienceConfig'.static.GetShortRankName(
		Unit.GetRank(), SoldierClass != none ? SoldierClass.DataName : '');
	Out $= FLD_SEP $ "name=" $ Unit.GetName(eNameType_FullNick);
	if (SoldierClass != none)
		Out $= FLD_SEP $ "class=" $ SoldierClass.DisplayName;
	Out $= FLD_SEP $ "status=" $ Status;
	// The two halves the row draws stacked in one column: "5" over "ДНЕЙ ДО ВОЗВРАЩЕНИЯ".
	// Value first, then the label that gives it a unit — the order the number is read in.
	if (TimeValue != "")
		Out $= FLD_SEP $ "time=" $ TimeValue @ TimeLabel;
	if (Mental != "")
		Out $= FLD_SEP $ "mental=" $ Mental;
	Out $= FLD_SEP $ "loc=" $ LocationOf(Unit);
	if (Unit.ShowPromoteIcon())
		Out $= FLD_SEP $ "promote=1";

	return Out;
}

// UIPersonnel_DeceasedListItem.UpdateData. The three numbers and the date are bare columns on
// screen, under headings a blind player never reaches — so each carries the heading the sort
// header uses for it, out of the screen's own localized m_strButtonLabels.
private function string DeceasedFields(UIPersonnel Screen, XComGameState_Unit Unit)
{
	local string Out;

	Out = FLD_SEP $ "name=" $ Unit.GetName(eNameType_FullNick);
	Out $= FLD_SEP $ "killsLabel=" $ Screen.m_strButtonLabels[ePersonnelSoldierSortType_Kills];
	Out $= FLD_SEP $ "kills=" $ string(Unit.GetNumKills());
	Out $= FLD_SEP $ "missionsLabel=" $ Screen.m_strButtonLabels[ePersonnelSoldierSortType_Missions];
	Out $= FLD_SEP $ "missions=" $ string(Unit.GetNumMissions());
	Out $= FLD_SEP $ "opLabel=" $ Screen.m_strButtonLabels[ePersonnelSoldierSortType_Operations];
	Out $= FLD_SEP $ "op=" $ Unit.GetKIAOp();
	Out $= FLD_SEP $ "dateLabel=" $ Screen.m_strButtonLabels[ePersonnelSoldierSortType_Time];
	Out $= FLD_SEP $ "date=" $ class'X2StrategyGameRulesetDataStructures'.static.GetDateString(Unit.GetKIADate(), true);

	return Out;
}

// UIPersonnel_ListItem.UpdateData — scientists and engineers. The skill column the row draws is
// a hardcoded "0" in the shipped source, so there is nothing to read there and nothing is said.
private function string StaffFields(XComGameState_Unit Unit)
{
	local string Out;

	Out = FLD_SEP $ "name=" $ Unit.GetName(eNameType_Full);
	Out $= FLD_SEP $ "status=" $ class'UIUtilities_Strategy'.static.GetPersonnelStatus(Unit);
	Out $= FLD_SEP $ "loc=" $ LocationOf(Unit);

	return Out;
}

// The row's own rule, not ours: an unstaffed person shows no location at all.
private function string LocationOf(XComGameState_Unit Unit)
{
	if (class'UIUtilities_Strategy'.static.DisplayLocation(Unit))
		return class'UIUtilities_Strategy'.static.GetPersonnelLocation(Unit);

	return "";
}

// PopulateListInstantly pushes m_strEmptyListLabels[m_eCurrentTab] into Flash for an empty list
// — except in the Bar, which spawns its own UIText and uses its own EmptyListMessage. Both are
// the game's own words for "there is nobody here", and which one is on screen depends on the
// screen class, so both are checked in that order.
private function string EmptyText(UIPersonnel Screen)
{
	local UIPersonnel_BarMemorial Memorial;

	Memorial = UIPersonnel_BarMemorial(Screen);
	if (Memorial != none && Memorial.EmptyListMessage != "")
		return Memorial.EmptyListMessage;

	if (Screen.m_eCurrentTab >= 0 && Screen.m_eCurrentTab < eUIPersonnel_All)
		return Screen.m_strEmptyListLabels[Screen.m_eCurrentTab];

	return "";
}

// ---------------------------------------------------------------------------------------
// Verbs
// ---------------------------------------------------------------------------------------

// The game's own SwitchTab, which does the whole job: re-points m_kList.OnItemClicked at the
// right selection delegate, swaps the sort header, rebuilds the sort-column order, refreshes the
// data and moves the list back to the top. ⛔ Never assign m_eCurrentTab directly — every one of
// those would be skipped, and the next Enter would run the PREVIOUS tab's delegate against the
// new tab's list.
function SwitchPersonnelTab(int Tab)
{
	local UIPersonnel Screen;

	NativeResult = "";

	Screen = UIPersonnel(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.m_arrNeededTabs.Find(Tab) == INDEX_NONE)
		return;

	Screen.SwitchTab(Tab);
}

// Press a sort column the way a mouse does. ⛔ Through UIFlipSortButton.OnMouseEvent rather than
// through the screen's SetSortType, because that handler owns three things a direct poke would
// drop: it deselects every other header, it FLIPS the direction when the column was already the
// sort column (instead of resetting it), and it calls RefreshData() afterwards.
function ClickPersonnelSort(int Index)
{
	local array<UIPanel> Buttons;
	local array<string> NoArgs;
	local UIPersonnel Screen;
	local UIPanel Header;

	NativeResult = "";

	Screen = UIPersonnel(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	Header = VisibleSortHeader(Screen);
	if (Header == none)
		return;

	Header.GetChildrenOfType(class'UIFlipSortButton', Buttons);
	if (Index < 0 || Index >= Buttons.Length)
		return;

	Buttons[Index].OnMouseEvent(class'UIUtilities_Input'.const.FXS_L_MOUSE_UP, NoArgs);
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

private function Emit(string Rec)
{
	if (NativeResult != "")
		NativeResult $= REC_SEP;
	NativeResult $= Rec;
}

defaultproperties
{
	NativeResult = ""
}
