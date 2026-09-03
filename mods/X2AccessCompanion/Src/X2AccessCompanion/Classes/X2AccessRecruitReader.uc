// X2AccessRecruitReader
// The Armory's recruit list (UIRecruitSoldiers) — the words half. Same contract as
// X2AccessPersonnelReader and X2AccessCommodityReader: C++ owns the gate, the keys and the speech,
// calls DescribeRecruits() on this class's default object via ProcessEvent from the graph recipe's
// build, and reads NativeResult back through reflection right after the call returns. No
// listeners, no timers, no polling.
//
// ⛔ A ROW RETAINS NOTHING IDENTIFYING. UIRecruitmentListItem.InitRecruitItem pushes the country
// flag and the name through MC.BeginFunctionOp("populateData") and never calls SetText, never
// writes metadataInt — so Text is EMPTY on a row visibly showing a recruit, and the only join
// between a row and its recruit is POSITION: UIRecruitSoldiers.UpdateData clears m_arrRecruits and
// the list together and refills them in one loop, so m_arrRecruits[i] IS row i. Every word below
// is re-derived from that array exactly the way InitRecruitItem and OnRecruitChanged derived it.
//
// The right-hand panel — cost, time, the bio, the headshot — is painted by OnRecruitChanged straight
// into Flash on every selection change and retained nowhere; it is re-run here for the SELECTED
// row from the same three sources: the screen's own localized captions (off the live instance, the
// localized-var trap's answer), the resistance HQ's GetRecruitSupplyCost through the same XGParam
// expansion, and the unit's own GetBackground().
//
// ⚠ The time line is what the screen draws — m_strInstant, unconditionally — and not what
// GiveRecruit does with RecruitOrderTime. Parity is defined against the screen.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessRecruitReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// Records, in the order the recipe wants stops:
//   kind=header  title                                    (m_strListTitle — "RECRUITS")
//   kind=row     index  name  country                     (the flag is the country: its name is said)
//   kind=empty   text                                     (m_strNoRecruits, when the list is empty)
//   kind=card    costLabel  cost  timeLabel  time  desc   (the SELECTED row's panel)
//
// Every index is the GAME's — the list's own item index, which is also m_arrRecruits' — because
// that is what SetSelectedIndex and OnRecruitSelected are expressed in.
function DescribeRecruits()
{
	local UIRecruitSoldiers Screen;

	NativeResult = "";

	// The same definition native's finder uses: the recruit screen is the current screen while
	// its own not-enough-supplies dialog is up too, since UIDialogueBox pushes no screen.
	Screen = UIRecruitSoldiers(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	AppendHeader(Screen);
	AppendRows(Screen);
	AppendCard(Screen);
}

private function AppendHeader(UIRecruitSoldiers Screen)
{
	local string Rec;

	Rec = "kind=header";
	AddField(Rec, "title", Screen.m_strListTitle);
	Emit(Rec);
}

// One record per row, walking the LIST's count against the array's — both are rebuilt together,
// and the shorter of the two bounds the walk so a half-built screen cannot index past either.
private function AppendRows(UIRecruitSoldiers Screen)
{
	local XComGameState_Unit Recruit;
	local X2CountryTemplate Country;
	local string Rec;
	local int i;

	if (Screen.List == none)
		return;

	if (Screen.List.ItemCount == 0 || Screen.m_arrRecruits.Length == 0)
	{
		// The line UpdateData pushes through setEmpty — the game's own words for "nobody".
		Emit("kind=empty" $ FLD_SEP $ "text=" $ Screen.m_strNoRecruits);
		return;
	}

	for (i = 0; i < Screen.List.ItemCount && i < Screen.m_arrRecruits.Length; ++i)
	{
		Recruit = Screen.m_arrRecruits[i];
		if (Recruit == none)
			continue;

		Rec = "kind=row" $ FLD_SEP $ "index=" $ i;
		// InitRecruitItem's own name, minus the GetColoredText wrapper it draws it in.
		AddField(Rec, "name", Recruit.GetName(eNameType_Full));
		// The row's flag icon is the country; the icon's meaning is its name.
		Country = Recruit.GetCountryTemplate();
		if (Country != none)
			AddField(Rec, "country", Country.DisplayName);
		Emit(Rec);
	}
}

// OnRecruitChanged, operand for operand minus the picture: the cost caption and the expanded
// supplies figure, the time caption and the word the screen always draws for it, the bio.
private function AppendCard(UIRecruitSoldiers Screen)
{
	local XComGameState_Unit Recruit;
	local XComGameState_HeadquartersResistance ResistanceHQ;
	local XGParamTag LocTag;
	local string Rec;
	local int Index;

	if (Screen.List == none)
		return;

	Index = Screen.List.SelectedIndex;
	if (Index < 0 || Index >= Screen.m_arrRecruits.Length)
		return;

	Recruit = Screen.m_arrRecruits[Index];
	if (Recruit == none)
		return;

	ResistanceHQ = class'UIUtilities_Strategy'.static.GetResistanceHQ();
	LocTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
	LocTag.IntValue0 = ResistanceHQ.GetRecruitSupplyCost();

	Rec = "kind=card";
	AddField(Rec, "costLabel", Screen.m_strCost);
	AddField(Rec, "cost", `XEXPAND.ExpandString(Screen.m_strSupplies));
	AddField(Rec, "timeLabel", Screen.m_strTime);
	AddField(Rec, "time", Screen.m_strInstant);
	AddField(Rec, "desc", Recruit.GetBackground());
	Emit(Rec);
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
