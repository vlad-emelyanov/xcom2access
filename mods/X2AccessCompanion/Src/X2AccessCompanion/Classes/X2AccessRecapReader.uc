// X2AccessRecapReader
// The post-mission rewards recap (UIRewardsRecap) — the whole screen, because it retains no
// widget at all. Same contract as X2AccessReportReader: C++ owns the gate, the keys and the
// speech, calls DescribeRewardsRecap() on this class's default object via ProcessEvent from the
// graph recipe's build, and reads NativeResult back right after the call returns.
//
// WHAT THE SCREEN IS: the tail of the post-mission chain — loot recovered → (research available)
// → this → the geoscape — the council's after-action letter: the operation's name and region, a
// paragraph, the rewards the mission granted, and the global effects it had (region control,
// supply income, Avatar progress). It is NOT the monthly council report's chain, which ends at
// the resistance orders.
//
// ⛔ THERE IS NO WIDGET TREE. UIRewardsRecap declares no UIList, no UIText, no UIButton — not even
// its own Continue (the nav help's, whose label lives in the movie). BuildScreen resolves every
// string into locals and pushes them through MC.BeginFunctionOp, so a widget read finds nothing
// and a Flash read finds only pixels. What the screen DOES have is a set of public accessors that
// ARE what BuildScreen called — GetOpNameString, GetOpLocationString, GetOpSummaryString,
// GetRewardStrings, GetGlobalEffectsStrings — so every line below is one of those re-run on the
// live screen. Not one XCOMHISTORY lookup of our own.
//
// ⛔ READ BEFORE CONTINUE, NEVER AFTER. OnContinue → ExitPostMissionSequence →
// ResistanceHQ.ClearRewardsRecapData() empties the three arrays the rewards and effects come from,
// and clears XComHQ.MissionRef, which GetMission dereferences. The entry point guards both, and
// native caches the payload on the screen pointer so nothing re-asks on the way out.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessRecapReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

const FLD_SEP = "~:~";

// Records, in the screen's own reading order:
//   kind=header   mission  location  result  greeble  rewardlabel  effectslabel
//   kind=summary  text
//   kind=reward   index  text            (only the non-empty ones, in the screen's own slot order)
//   kind=effect   index  text  bad=1     (the good effects first, then the bad — BuildScreen's order)
//
// `result` is the game's own MISSION COMPLETE / MISSION FAILED from one screen earlier
// (UIMissionSummary), because the recap states the outcome in COLOUR alone — the name is green or
// red — and a mission's custom success text need not say which it was. `rewardlabel` and
// `effectslabel` ride on the header only when the screen would draw them: BuildScreen paints the
// rewards caption as "" when no reward string was non-empty, and skips the effects call entirely
// when both arrays are empty. The objective-rewards caption is always painted over an EMPTY value
// in the shipped game and is not carried — a heading with nothing under it is noise.
function DescribeRewardsRecap()
{
	local UIRewardsRecap Screen;
	local XComGameState_HeadquartersResistance ResistanceHQ;
	local array<string> Rewards;
	local string Rec;
	local int i, Slot;
	local bool bAnyReward;

	NativeResult = "";

	Screen = UIRewardsRecap(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	// The two things Continue destroys. Both are what GetOpNameString would dereference, so an
	// empty answer here is "the screen is on its way out", never a crash.
	if (Screen.XComHQ == none || Screen.XComHQ.MissionRef.ObjectID == 0 || Screen.GetMission() == none)
		return;

	ResistanceHQ = class'UIUtilities_Strategy'.static.GetResistanceHQ();
	Rewards = Screen.GetRewardStrings();
	for (i = 0; i < Rewards.Length; ++i)
	{
		if (Rewards[i] != "")
			bAnyReward = true;
	}

	Rec = "kind=header";
	AddField(Rec, "mission", Screen.GetOpNameString());
	AddField(Rec, "location", Screen.GetOpLocationString());
	AddField(Rec, "result", Screen.WasMissionSuccessful()
		? class'UIMissionSummary'.default.m_strMissionComplete
		: class'UIMissionSummary'.default.m_strMissionFailed);
	AddField(Rec, "greeble", Screen.m_strSecureTransmission);
	if (bAnyReward)
		AddField(Rec, "rewardlabel", Screen.m_strReward);
	if (ResistanceHQ != none
		&& ResistanceHQ.RecapGlobalEffectsGood.Length + ResistanceHQ.RecapGlobalEffectsBad.Length > 0)
	{
		AddField(Rec, "effectslabel", Screen.m_strGlobalEffects);
	}
	Emit(Rec);

	Rec = "kind=summary";
	AddField(Rec, "text", Screen.GetOpSummaryString());
	Emit(Rec);

	// BuildScreen compacts the slots: an empty string is skipped and the NEXT reward takes its
	// slot. The index carried is that slot number, so it names what the screen drew.
	Slot = 0;
	for (i = 0; i < Rewards.Length; ++i)
	{
		if (Rewards[i] == "")
			continue;
		Rec = "kind=reward" $ FLD_SEP $ "index=" $ Slot;
		AddField(Rec, "text", Rewards[i]);
		Emit(Rec);
		Slot++;
	}

	// The PLAIN arrays rather than GetGlobalEffectsStrings(): the screen colours each one green or
	// red and then fuses them into one space-joined string, and the colour is exactly the fact the
	// bad=1 flag carries. Good first, then bad — the order BuildScreen paints them in.
	if (ResistanceHQ == none)
		return;

	Slot = 0;
	for (i = 0; i < ResistanceHQ.RecapGlobalEffectsGood.Length; ++i)
	{
		Rec = "kind=effect" $ FLD_SEP $ "index=" $ Slot;
		AddField(Rec, "text", ResistanceHQ.RecapGlobalEffectsGood[i]);
		Emit(Rec);
		Slot++;
	}
	for (i = 0; i < ResistanceHQ.RecapGlobalEffectsBad.Length; ++i)
	{
		Rec = "kind=effect" $ FLD_SEP $ "index=" $ Slot $ FLD_SEP $ "bad=1";
		AddField(Rec, "text", ResistanceHQ.RecapGlobalEffectsBad[i]);
		Emit(Rec);
		Slot++;
	}
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
