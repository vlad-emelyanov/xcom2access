// X2AccessChosenReader
// The Chosen dossier (UIChosenInfo) — the words half. Same contract as X2AccessPersonnelReader:
// C++ owns the gate, the keys and the speech, calls DescribeChosenInfo() on this class's default
// object via ProcessEvent from the graph recipe's build, and reads NativeResult back through
// reflection right after the call returns. No listeners, no timers, no polling.
//
// ⛔ THE SCREEN RETAINS NO WORDS. RefreshHeaders and RefreshDisplay resolve everything into locals
// and push it through MC.BeginFunctionOp (SetButton, SetInfoPanel, SetStrengthData,
// SetWeaknessData, SetWeaknessFaction) — the house style. What the screen KEEPS is the state it
// populated Flash from: AllActiveChosen (every Chosen with bMetXCom), SelectedChosen and
// CurrentChosenIndex. So every line below is re-derived from SelectedChosen exactly the way
// RefreshDisplay derives it — same getters, same reveal filter, same UNKNOWN CHOSEN placeholders
// for the empty tab slots. Not one XCOMHISTORY access: the read-the-screen rule's second tier.
//
// ⛔ AN UNREVEALED TRAIT SPEAKS THE UNKNOWN WORDING. RefreshDisplay substitutes
// ChosenInfo_UnknownChosenTraitName / Description for any trait whose AbilityRevealEvent has not
// fired (RevealedChosenTraits). A sighted player cannot read the real name there, so neither may
// we — the template would hand it over, and handing it on would leak intel the game withholds.
//
// ⛔ NO Caps(). The screen upper-cases the class name and the nickname for Flash; that is a visual
// style, and a reader spelling out all-caps words turns every Chosen into an acronym.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessChosenReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format's field separator (see X2AccessWire); records are appended through it.
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants stops:
//   kind=header    title subtitle                (ChosenInfo_ListTitle / _ListSubtitle)
//   kind=tab       index name nick selected=1    (a met Chosen)
//   kind=tab       index label unknown=1         (an empty slot — the Flash stage draws four)
//   kind=identity  class name nick defeated
//   kind=stat      label value                   (encounters, interrogated, captured — the screen's order)
//   kind=knowledge label percent action
//   kind=hunt      label level max
//   kind=section   which=strengths|weaknesses title
//   kind=trait     which index name desc unknown=1
//   kind=faction   label type name
//   kind=continue  label
function DescribeChosenInfo()
{
	local UIChosenInfo Screen;
	local XComGameState_AdventChosen Chosen;
	local array<X2AbilityTemplate> Traits;

	NativeResult = "";

	Screen = UIChosenInfo(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	AppendHeader(Screen);
	AppendTabs(Screen);

	Chosen = Screen.SelectedChosen;
	if (Chosen != none)
	{
		AppendIdentity(Screen, Chosen);
		AppendCounters(Screen, Chosen);
		AppendMeters(Screen, Chosen);
		Traits = Chosen.GetChosenStrengths();
		AppendTraits(Screen, Chosen, Traits, "strengths", Screen.ChosenInfo_StrengthsLabel);
		Traits = Chosen.GetChosenWeaknesses();
		AppendTraits(Screen, Chosen, Traits, "weaknesses", Screen.ChosenInfo_WeaknessesLabel);
		AppendFaction(Screen, Chosen);
	}

	// The Continue is a nav-help widget whose label lives in the movie; the generic word is the
	// game's own and is what the same widget says everywhere else.
	Emit("kind=continue" $ FLD_SEP $ "label=" $ class'UIUtilities_Text'.default.m_strGenericContinue);
}

private function AppendHeader(UIChosenInfo Screen)
{
	local string Rec;

	Rec = "kind=header";
	class'X2AccessWire'.static.AddField(Rec, "title", Screen.ChosenInfo_ListTitle);
	class'X2AccessWire'.static.AddField(Rec, "subtitle", Screen.ChosenInfo_ListSubtitle);
	Emit(Rec);
}

// RefreshHeaders' own loop: one button per met Chosen, then the UNKNOWN CHOSEN label for every
// slot up to the four the Flash stage draws. The placeholder label is borrowed from the monthly
// report's class exactly as the screen borrows it.
private function AppendTabs(UIChosenInfo Screen)
{
	local XComGameState_AdventChosen Chosen;
	local string Rec;
	local int i;

	for (i = 0; i < Screen.AllActiveChosen.Length; i++)
	{
		Chosen = Screen.AllActiveChosen[i];
		if (Chosen == none)
			continue;

		Rec = "kind=tab" $ FLD_SEP $ "index=" $ string(i);
		class'X2AccessWire'.static.AddField(Rec, "name", Chosen.GetChosenName());
		class'X2AccessWire'.static.AddField(Rec, "nick", Chosen.GetChosenNickname());
		if (i == Screen.CurrentChosenIndex)
			Rec $= FLD_SEP $ "selected=1";
		Emit(Rec);
	}

	for (i = Screen.AllActiveChosen.Length; i < 4; i++)
	{
		Rec = "kind=tab" $ FLD_SEP $ "index=" $ string(i) $ FLD_SEP $ "unknown=1";
		class'X2AccessWire'.static.AddField(Rec, "label",
			class'UIResistanceReport_ChosenEvents'.default.UnknownChosenLabel);
		Emit(Rec);
	}
}

private function AppendIdentity(UIChosenInfo Screen, XComGameState_AdventChosen Chosen)
{
	local string Rec;

	Rec = "kind=identity";
	class'X2AccessWire'.static.AddField(Rec, "class", Chosen.GetChosenClassName());
	class'X2AccessWire'.static.AddField(Rec, "name", Chosen.GetChosenName());
	class'X2AccessWire'.static.AddField(Rec, "nick", Chosen.GetChosenNickname());
	// ShowChosenDefeat is called only when bDefeated; otherwise the badge is hidden, and an icon
	// in its unlit state gets no words.
	if (Chosen.bDefeated)
		class'X2AccessWire'.static.AddField(Rec, "defeated", Screen.ChosenInfo_DefeatedLabel);
	Emit(Rec);
}

// SetInfoPanel's three counters, under the labels the panel draws beside them.
private function AppendCounters(UIChosenInfo Screen, XComGameState_AdventChosen Chosen)
{
	EmitStat(Screen.ChosenInfo_EncountersLabel, Chosen.GetNumEncounters());
	EmitStat(Screen.ChosenInfo_KillsLabel, Chosen.GetNumExtracts());
	EmitStat(Screen.ChosenInfo_CapturesLabel, Chosen.GetNumSoldiersCaptured());
}

private function EmitStat(string Label, int Value)
{
	local string Rec;

	Rec = "kind=stat";
	class'X2AccessWire'.static.AddField(Rec, "label", Label);
	class'X2AccessWire'.static.AddField(Rec, "value", string(Value));
	Emit(Rec);
}

// The two meters, RefreshDisplay's own arithmetic: knowledge is GetKnowledgePercent(true) with
// the month's action as its caption (prefixed only when there is one); the hunt meter is 3 when
// the stronghold mission is available, else the rival faction's influence level — and it has no
// text operand at all, so native supplies the words for its level.
private function AppendMeters(UIChosenInfo Screen, XComGameState_AdventChosen Chosen)
{
	local XComGameState_ResistanceFaction Faction;
	local string Rec, Action;
	local int FragmentLevel;

	Action = Chosen.GetCurrentMonthActionName();
	if (Action != "")
		Action = Screen.ChosenInfo_MonthActionPrefix @ Action;

	Rec = "kind=knowledge";
	class'X2AccessWire'.static.AddField(Rec, "label", Screen.ChosenInfo_ChosenAwarenessLabel);
	class'X2AccessWire'.static.AddField(Rec, "percent", string(Chosen.GetKnowledgePercent(true)));
	class'X2AccessWire'.static.AddField(Rec, "action", Action);
	Emit(Rec);

	if (Chosen.IsStrongholdMissionAvailable())
	{
		FragmentLevel = 3;
	}
	else
	{
		Faction = Chosen.GetRivalFaction();
		if (Faction != none)
			FragmentLevel = int(Faction.GetInfluence());
	}

	Rec = "kind=hunt";
	class'X2AccessWire'.static.AddField(Rec, "label", Screen.ChosenInfo_XComAwarenessLabel);
	class'X2AccessWire'.static.AddField(Rec, "level", string(FragmentLevel));
	class'X2AccessWire'.static.AddField(Rec, "max", "3");
	Emit(Rec);
}

// One trait section. The reveal test is RefreshDisplay's own, branch for branch.
private function AppendTraits(UIChosenInfo Screen, XComGameState_AdventChosen Chosen,
	array<X2AbilityTemplate> Traits, string Which, string Title)
{
	local string Rec;
	local int i;

	Rec = "kind=section" $ FLD_SEP $ "which=" $ Which;
	class'X2AccessWire'.static.AddField(Rec, "title", Title);
	Emit(Rec);

	for (i = 0; i < Traits.Length; i++)
	{
		if (Traits[i] == none)
			continue;

		Rec = "kind=trait" $ FLD_SEP $ "which=" $ Which $ FLD_SEP $ "index=" $ string(i);
		if (Traits[i].AbilityRevealEvent == ''
			|| Chosen.RevealedChosenTraits.Find(Traits[i].DataName) != INDEX_NONE)
		{
			class'X2AccessWire'.static.AddField(Rec, "name", Traits[i].LocFriendlyName);
			class'X2AccessWire'.static.AddField(Rec, "desc", Traits[i].LocLongDescription);
		}
		else
		{
			Rec $= FLD_SEP $ "unknown=1";
			class'X2AccessWire'.static.AddField(Rec, "name", Screen.ChosenInfo_UnknownChosenTraitName);
			class'X2AccessWire'.static.AddField(Rec, "desc", Screen.ChosenInfo_UnknownChosenTraitDescription);
		}
		Emit(Rec);
	}
}

private function AppendFaction(UIChosenInfo Screen, XComGameState_AdventChosen Chosen)
{
	local XComGameState_ResistanceFaction Faction;
	local string Rec;

	Faction = Chosen.GetRivalFaction();
	if (Faction == none)
		return;

	Rec = "kind=faction";
	class'X2AccessWire'.static.AddField(Rec, "label", Screen.ChosenInfo_FactionLabel);
	class'X2AccessWire'.static.AddField(Rec, "type", Faction.GetFactionTitle());
	class'X2AccessWire'.static.AddField(Rec, "name", Faction.GetFactionName());
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// Verbs
// ---------------------------------------------------------------------------------------

// The game's own tab switch: the click sound, the Flash focus handoff between buttons,
// RefreshDisplay. ⛔ Never assign SelectedChosen / CurrentChosenIndex directly — the panel would
// not repaint, and native's discriminator would say it had.
function SelectChosen(int Index)
{
	local UIChosenInfo Screen;

	NativeResult = "";

	Screen = UIChosenInfo(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Index < 0 || Index >= Screen.AllActiveChosen.Length)
		return;

	Screen.SelectChosen(Index);
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

private function Emit(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

defaultproperties
{
	NativeResult = ""
}
