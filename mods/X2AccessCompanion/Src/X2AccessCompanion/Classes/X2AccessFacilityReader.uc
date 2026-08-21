// X2AccessFacilityReader
// A built room on the Avenger (UIFacility and its 22 subclasses) — the words half, and a very
// small one. Same contract as X2AccessSaveReader and X2AccessRoomReader: the C++ layer owns the
// gate, the keys and the speech, calls DescribeFacility() on this class's default object via
// ProcessEvent from the graph recipe's build, and reads NativeResult back through reflection
// right after the call returns. No listeners, no timers, no polling.
//
// WHY IT EXISTS AT ALL. Almost everything the "avenger room" recipe speaks is a retained widget
// read that native does for itself — the header's room name, the crew line, the shortcut
// submenu's rows, the upgrade button's own UIButton.Text. Exactly one thing is not:
//
//   UIFacility_RoomFunc.InitRoomFunc(label, delegate)
//       MC.FunctionString("update", label);      // <- straight into Flash
//       onRoomFunctionDelegate = onRoomFunctionDel;
//
// The label is a PARAMETER. It is pushed into the movie and then dropped; the widget retains the
// delegate and nothing else. So the buttons on a room's own bottom bar — the Bar's "БОЕВЫЕ
// ПОТЕРИ", the Living Quarters' "ПЕРСОНАЛ", the Proving Ground's "ВЫБЕРИТЕ ИССЛЕДОВАНИЕ" — are
// visible to a sighted player and invisible to every read the widget tree offers. That is the
// same trap the debris room's personnel picker sets, and the answer is the same one: go back to
// where the string came FROM.
//
// ⛔ NOT the delegate's name. It is the one other thing the widget keeps, and reading it would
// hand a Russian player "OnShowObituaries".
//
// ⛔ NOT a table of our own words either. Each label is the SCREEN's own `localized` property,
// read off the live instance — m_strObituaries, m_strPersonnel, m_strViewUnlocks and so on, all
// four of which ship values in XComGame.<loc> under their own class sections (checked, per the
// declared-but-never-populated trap that made UITacticalHUD_InfoPanel's captions read empty).
// The switch below only ROUTES: it decides which class declares the property, never what the
// property says.
//
// WHY THIS MATTERS MORE THAN THE BUTTON COUNT SUGGESTS. Only six facilities call
// CreateFacilityButtons, but two of them are FIXED rooms a new commander walks into on day one,
// and for those two the bottom bar is the ONLY content on the screen — no staff slots, and no
// shortcut submenu either (UIAvengerShortcuts.CurrentCategory reads -1 in both). Missing it is
// what made the Bar/Memorial announce "nothing to do here" at a screen that visibly had a
// button, reported live 2026-08-21.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessFacilityReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// One record per bottom-bar button:
//   kind=func  index=N  label=<the screen's own localized string>
//
// The index is RoomFuncs' own, which is also CreateFacilityButtons' call order, and is what
// native matches on rather than trusting array position — the same rule the save rows follow.
function DescribeFacility()
{
	local UIFacility Screen;
	local string Rec;
	local int i;

	NativeResult = "";

	// The same screen native resolved: topmost, matching GetCurrentScreen. A UIFacility further
	// down the stack is one the player has navigated away from.
	Screen = UIFacility(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.m_kRoomContainer == none)
		return;

	for (i = 0; i < Screen.m_kRoomContainer.RoomFuncs.Length; i++)
	{
		Rec = "kind=func";
		Rec $= FLD_SEP $ "index=" $ string(i);
		Rec $= FLD_SEP $ "label=" $ ButtonLabel(Screen, i);
		Emit(Rec);
	}
}

// ---------------------------------------------------------------------------------------
// The label
// ---------------------------------------------------------------------------------------

// ⛔ Index 0 is the whole shipped vocabulary: every one of the six CreateFacilityButtons
// overrides makes exactly one AddFacilityButton call. A mod that adds a second button gets an
// EMPTY label rather than a repeat of the first — native names such a node generically, which is
// a gap a player can hear and act on, where a confidently wrong word is one they cannot.
static function string ButtonLabel(UIFacility Screen, int Index)
{
	if (Index != 0)
		return "";

	if (UIFacility_BarMemorial(Screen) != none)
		return UIFacility_BarMemorial(Screen).m_strObituaries;

	if (UIFacility_LivingQuarters(Screen) != none)
		return UIFacility_LivingQuarters(Screen).m_strPersonnel;

	if (UIFacility_Academy(Screen) != none)
		return UIFacility_Academy(Screen).m_strViewUnlocks;

	if (UIFacility_ProvingGround(Screen) != none)
		return UIFacility_ProvingGround(Screen).m_strStartProject;

	if (UIFacility_TrainingCenter(Screen) != none)
		return UIFacility_TrainingCenter(Screen).m_strTrainAbilities;

	if (UIFacility_CovertActions(Screen) != none)
		return UIFacility_CovertActions(Screen).m_strViewActions;

	return "";
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
