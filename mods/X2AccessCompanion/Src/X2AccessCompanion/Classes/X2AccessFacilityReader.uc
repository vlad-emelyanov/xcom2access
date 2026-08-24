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
// THE SECOND THING NATIVE CANNOT READ is the Resistance Ring's covert-op status panel.
// UIFacility_CovertOpsStatus is a UIPanel with no text of its own: Refresh() pushes the header,
// the running action's name and the days left through MC.FunctionString("setTitle"/"setDaysValue"
// /...) and keeps NOTHING, and SetEmpty() does the same with the two empty-state strings. So the
// one number that says whether the Ring is busy — and therefore whether walking into it will
// achieve anything — is invisible to every widget read. Reported as part of the "Flash-only
// status panels" gap; this closes the covert-ops half of it (the Labs' m_kResearchProgressBar is
// the same shape and is still open).
//
// ⛔ It is re-derived by re-running the SCREEN'S OWN UpdateData — iterate the history for the
// XComGameState_CovertAction with bStarted, exactly as UIFacility_CovertActions.UpdateData does,
// and format with the same UIUtilities_Text.GetTimeValueAndLabel call Refresh makes. Same rule as
// X2AccessSaveReader: re-run the formatting the panel ran, do not invent a second one. The panel
// is refreshed only on InitScreen and OnReceiveFocus, so a value cached for as long as the recipe
// stays attached is exactly as fresh as what a sighted player is looking at.
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

// One record per bottom-bar button, then at most one for the covert-op status panel:
//   kind=func       index=N  label=<the screen's own localized string>
//   kind=covertops  label    title    time            (Resistance Ring only)
//
// The func index is RoomFuncs' own, which is also CreateFacilityButtons' call order, and is what
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
	if (Screen == none)
		return;

	// ⛔ Guarded separately from the status panel below, not shared with it: the two live on
	// different members, and an early return on m_kRoomContainer would have taken the Ring's
	// readout down with a room container that had not spawned yet.
	if (Screen.m_kRoomContainer != none)
	{
		for (i = 0; i < Screen.m_kRoomContainer.RoomFuncs.Length; i++)
		{
			Rec = "kind=func";
			Rec $= FLD_SEP $ "index=" $ string(i);
			Rec $= FLD_SEP $ "label=" $ ButtonLabel(Screen, i);
			Emit(Rec);
		}
	}

	// Only UIFacility_CovertActions ever calls CreateCovertOpsStatus, so the pointer IS the test
	// for "is the game showing this here". ⛔ Deliberately NOT gated on the panel's bIsVisible:
	// the game clears that in OnLoseFocus, and a screen we are attached to is by definition the
	// focused one — testing it could only ever produce silence at a panel that is on screen.
	if (Screen.m_kCovertOpsStatus != none)
		Emit(CovertOpsRecord());
}

// ---------------------------------------------------------------------------------------
// The covert-op status panel
// ---------------------------------------------------------------------------------------

// UIFacility_CovertActions.UpdateData's own search, and then UIFacility_CovertOpsStatus's own
// formatting for whichever branch it lands in. Three fields rather than one composed line,
// because the join belongs to native (X2Access::Graph::AppendPhrase) — the game ends
// CovertOpsStatus_EmptyLabel with a space and its headers with neither, and blind concatenation
// is what the never-join-with-a-bare-comma rule exists to stop.
static function string CovertOpsRecord()
{
	local XComGameStateHistory History;
	local XComGameState_CovertAction ActionState;
	local string Rec, TimeValue, TimeLabel, TimeStr;
	local int Hours;

	Rec = "kind=covertops";

	History = `XCOMHISTORY;
	foreach History.IterateByClassType(class'XComGameState_CovertAction', ActionState)
	{
		if (!ActionState.bStarted)
			continue;

		Hours = ActionState.GetNumHoursRemaining();
		class'UIUtilities_Text'.static.GetTimeValueAndLabel(Hours, TimeValue, TimeLabel, 0);

		// The panel replaces an overdue countdown with a literal "--" and blanks its unit — an
		// action that has finished but whose report has not been shown yet. "--" is a dash a
		// sighted eye reads as "no number"; spoken it is noise, so the time is dropped instead
		// and the readout keeps the header and the action's name, which is all that branch says.
		if (Hours < 0)
		{
			TimeValue = "";
			TimeLabel = "";
		}

		// ⛔ UICovertActions' copy of CovertActions_ScreenHeader, which is what the panel reads —
		// UICovertActionReport declares the SAME localized name with a DIFFERENT value
		// ("COVERT ACTION REPORT:"), and both are populated, so the class here is load-bearing.
		Rec $= FLD_SEP $ "label=" $ class'UICovertActions'.default.CovertActions_ScreenHeader;
		Rec $= FLD_SEP $ "title=" $ ActionState.GetDisplayName();
		// Value and unit joined the shared way rather than with `@`: either half can be empty
		// (the overdue branch above empties both) and `@` would leave the separator behind.
		TimeStr = "";
		class'X2AccessHelper'.static.AppendJoined(TimeStr, TimeValue, " ");
		class'X2AccessHelper'.static.AppendJoined(TimeStr, TimeLabel, " ");
		Rec $= FLD_SEP $ "time=" $ TimeStr;
		return Rec;
	}

	Rec $= FLD_SEP $ "label=" $ class'UIFacility_CovertOpsStatus'.default.CovertOpsStatus_EmptyLabel;
	Rec $= FLD_SEP $ "title=" $ class'UIFacility_CovertOpsStatus'.default.CovertOpsStatus_EmptyTitle;
	Rec $= FLD_SEP $ "time=";
	return Rec;
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
