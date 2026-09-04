// X2AccessCustomizeReader
// The soldier customization family (UICustomize and every screen derived from it) — the words
// half, plus the one verb vanilla leaves off the keyboard our layer takes. Same contract as
// X2AccessArmoryReader: C++ owns the gate, the keys and the speech, calls DescribeCustomize() on
// this class's default object via ProcessEvent from the graph recipe's build, and reads
// NativeResult back right after the call returns.
//
// WHAT NATIVE ALREADY HAS: every row. The family builds its rows through UpdateDataDescription /
// UpdateDataValue / UpdateDataColorChip / UpdateDataCheckbox / UpdateDataButton, so the label,
// the current choice, a checkbox's state, a swatch's colour and a button's label are all retained
// on the UIMechaListItem, and a greyed row's reason is the tooltip SetDisabled parks on its BG.
// The picker's options are the same rows. What this class adds is what the screen pushes into
// Flash and forgets:
//   * the soldier header — Header.PopulateData is a Flash push; composed with the loadout's
//     DescribeArmorySoldier so every armory screen names a soldier the same way;
//   * the screen's TITLE, a localized string each subclass declares (the picker has none, and
//     native names it by the row that opened it);
//   * the info screen's BIOGRAPHY box (AS_SetCharacterBio, retained nowhere).
//
// The verb: the soldier cycle (Tab / LeftShift in vanilla, both taken by the layer), through the
// screen's own NextSoldier / PrevSoldier behind the screen's own gate.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessCustomizeReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants stops:
//   kind=header  who  title  cycle=1
//   kind=bio     label  text            (the info screen only)
function DescribeCustomize()
{
	local UICustomize Screen;
	local UICustomize_Info Info;
	local XComGameState_Unit Unit;
	local string Rec;

	NativeResult = "";

	Screen = UICustomize(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	Unit = HeaderUnit(Screen);

	Rec = "kind=header";
	if (Unit != none)
		AddField(Rec, "who", class'X2AccessHelper'.static.DescribeArmorySoldier(Unit));
	AddField(Rec, "title", ScreenTitle(Screen));
	if (CanCycle(Screen))
		Rec $= FLD_SEP $ "cycle=1";
	Emit(Rec);

	Info = UICustomize_Info(Screen);
	if (Info != none && Unit != none)
	{
		Rec = "kind=bio";
		AddField(Rec, "label", Info.m_strBiographyLabel);
		AddField(Rec, "text", Unit.GetBackground());
		Emit(Rec);
	}
}

// The unit whose values the rows show. GetCategoryDisplay reads the manager's UpdatedUnitState —
// the pending copy every change is written to before CommitChanges — and a name edit repaints
// the header from it (OnNameInputBoxClosed), so it is the copy that matches Flash whenever it
// exists; the screen's own Unit is the original.
private function XComGameState_Unit HeaderUnit(UICustomize Screen)
{
	if (Screen.CustomizeManager != none && Screen.CustomizeManager.UpdatedUnitState != none)
		return Screen.CustomizeManager.UpdatedUnitState;
	return Screen.Unit;
}

// Each direct subclass declares its own m_strTitle; the picker and the base declare none. The
// Templar / Skirmisher / SPARK variants extend these and inherit theirs.
private function string ScreenTitle(UICustomize Screen)
{
	if (UICustomize_Menu(Screen) != none)
		return UICustomize_Menu(Screen).m_strTitle;
	if (UICustomize_Info(Screen) != none)
		return UICustomize_Info(Screen).m_strTitle;
	if (UICustomize_Head(Screen) != none)
		return UICustomize_Head(Screen).m_strTitle;
	if (UICustomize_Body(Screen) != none)
		return UICustomize_Body(Screen).m_strTitle;
	if (UICustomize_Props(Screen) != none)
		return UICustomize_Props(Screen).m_strTitle;
	if (UICustomize_Weapon(Screen) != none)
		return UICustomize_Weapon(Screen).m_strTitle;
	return "";
}

// UICustomize.OnUnrealCommand's own gate for Tab / LeftShift: inside the armory, with a soldier
// to cycle to under the base class's eligibility test.
private function bool CanCycle(UICustomize Screen)
{
	if (!Screen.bInArmory)
		return false;
	return class'UIUtilities_Strategy'.static.HasSoldiersToCycleThrough(Screen.UnitRef, CanCycleTo);
}

// The eligibility delegate HasSoldiersToCycleThrough wants — the screen's own static test.
function bool CanCycleTo(XComGameState_Unit Unit)
{
	return class'UICustomize'.static.CanCycleTo(Unit);
}

// ---------------------------------------------------------------------------------------
// Verbs
// ---------------------------------------------------------------------------------------

// Vanilla's Tab / LeftShift, behind the screen's own gate. NextSoldier / PrevSoldier refresh
// every customize screen on the stack and may POP them all for a unit of another customization
// class (a SPARK after a human), pushing a fresh menu — the recipe re-probes either way.
function CycleCustomizeSoldier(int Direction)
{
	local UICustomize Screen;

	NativeResult = "";

	Screen = UICustomize(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || !CanCycle(Screen))
		return;

	if (Direction > 0)
		Screen.NextSoldier();
	else
		Screen.PrevSoldier();
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

private function Emit(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

private function AddField(out string Rec, string Key, string Value)
{
	class'X2AccessWire'.static.AddField(Rec, Key, Value);
}

defaultproperties
{
	NativeResult = ""
}
