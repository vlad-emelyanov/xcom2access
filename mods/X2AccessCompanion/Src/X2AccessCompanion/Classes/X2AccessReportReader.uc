// X2AccessReportReader
// The monthly council report (UIResistanceReport) — the words half. Same contract as
// X2AccessChosenReader: C++ owns the gate, the keys and the speech, calls
// DescribeResistanceReport() on this class's default object via ProcessEvent from the recipe's
// build, and reads NativeResult back right after the call returns.
//
// ⛔ THE SCREEN RETAINS NO WORDS. BuildScreen composes every line into locals and pushes them
// through MC.BeginFunctionOp (UpdateCouncilReportCardInfo / Rewards / ResistanceActivity /
// AlienActivity / AvatarProgress). But every VALUE it composes comes off a function the screen
// itself owns (GetDateString, GetSupplyRewardString) or the two HQ states its UIX2SimpleScreen base
// hands it (RESHQ(), ALIENHQ()), so this class re-runs BuildScreen's own arithmetic over the same
// sources — the read-the-screen rule's first and second tiers, and not one lookup of our own.
//
// The staff block (UpdateCouncilReportCardStaff) is pushed with FIVE EMPTY STRINGS in the shipped
// source — the code that filled it is commented out — so nothing is emitted for it: an empty
// answer is the game's, not a gap in the read.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

// dependson: TResistanceActivity lives inside XComGameState_HeadquartersResistance, not in a
// global header.
class X2AccessReportReader extends Object
	dependson(XComGameState_HeadquartersResistance);

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format's field separator (see X2AccessWire); records are appended through it.
const FLD_SEP = "~:~";

// Records, in the order the recipe wants them:
//   kind=header   title subtitle           (m_strReportTitle, the date the screen itself formats)
//   kind=line     index label value max    (one per line the card draws, in BuildScreen's order;
//                                           `max` only on the Avatar meter, which is drawn as pips)
//   kind=continue label
function DescribeResistanceReport()
{
	local UIResistanceReport Screen;
	local XComGameState_HeadquartersResistance ResHQ;
	local XComGameState_HeadquartersAlien AlienHQ;
	local string Rec;
	local int Index;

	NativeResult = "";

	Screen = UIResistanceReport(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	ResHQ = Screen.RESHQ();
	AlienHQ = Screen.ALIENHQ();

	Rec = "kind=header";
	class'X2AccessWire'.static.AddField(Rec, "title", Screen.m_strReportTitle);
	class'X2AccessWire'.static.AddField(Rec, "subtitle", Screen.GetDateString());
	Emit(Rec);

	Index = 0;

	// "SECURE TRANSMISSION" — the card's own greeble, drawn under the date. Flavour, but on screen.
	EmitLine(Index, Screen.m_strResistanceReportGreeble, "", "");

	if (ResHQ != none)
	{
		// UpdateCouncilReportCardRewards, branch for branch: the income or loss title by sign,
		// the amount through the screen's own formatter, then the two optional modifiers.
		EmitLine(Index,
			ResHQ.GetSuppliesReward(true) > 0 ? Screen.m_strSupplyTitle : Screen.m_strSupplyLossTitle,
			Screen.GetSupplyRewardString(), "");
		if (ResHQ.SupplyDropPercentIncrease > 0)
			EmitLine(Index, Screen.m_strBonusSupply, "+" $ ResHQ.SupplyDropPercentIncrease $ "%", "");
		if (ResHQ.SavedSupplyDropPercentDecrease > 0)
			EmitLine(Index, Screen.m_strDarkEventPenalty,
				"-" $ Round(ResHQ.SavedSupplyDropPercentDecrease * 100.0) $ "%", "");

		EmitLine(Index, Screen.m_strResistanceActivity, ActivityList(ResHQ.GetMonthlyActivity()), "");
		EmitLine(Index, Screen.m_strAlienActivity, ActivityList(ResHQ.GetMonthlyActivity(true)), "");
	}

	// UpdateAvatarProgress: the meter is drawn only once the player has seen the doom meter at
	// all; before that the label is pushed empty and the pips at -1, i.e. nothing on screen.
	if (AlienHQ != none && AlienHQ.bHasSeenDoomMeter)
		EmitLine(Index, Screen.m_strAvatarProgressLabel, string(AlienHQ.GetCurrentDoom()),
			string(AlienHQ.GetMaxDoom()));

	Emit("kind=continue" $ FLD_SEP $ "label=" $ class'UIUtilities_Text'.default.m_strGenericContinue);
}

// UpdateResistanceActivity / UpdateAlienActivity's own composition: "Title N" per activity,
// comma-joined, then the localized serial-comma pass. The colour markup GetColoredText wraps each
// entry in is kept for native's CleanText to strip, exactly like every other wire value.
private function string ActivityList(array<TResistanceActivity> Actions)
{
	local string List;
	local int i;

	for (i = 0; i < Actions.Length; i++)
	{
		List $= class'UIUtilities_Text'.static.GetColoredText(
			Actions[i].Title @ string(Actions[i].Count), Actions[i].Rating);
		if (i < Actions.Length - 1)
			List $= ", ";
	}

	return class'UIUtilities_Text'.static.FormatCommaSeparatedNouns(List);
}

// A blank line is not emitted, so the recipe never lands a player on nothing.
private function EmitLine(out int Index, string Label, string Value, string Max)
{
	local string Rec;

	if (Label == "" && Value == "")
		return;

	Rec = "kind=line" $ FLD_SEP $ "index=" $ string(Index);
	class'X2AccessWire'.static.AddField(Rec, "label", Label);
	class'X2AccessWire'.static.AddField(Rec, "value", Value);
	class'X2AccessWire'.static.AddField(Rec, "max", Max);
	Emit(Rec);
	Index++;
}

private function Emit(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

defaultproperties
{
	NativeResult = ""
}
