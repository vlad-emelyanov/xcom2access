// X2AccessAfterActionReader
// The post-mission squad walk-up (UIAfterAction) — the words half. Same contract as
// X2AccessSquadReader, and deliberately the same shape: the C++ layer owns the gate, the keys
// and the speech, calls DescribeAfterAction() on this class's default object via ProcessEvent
// (game thread, from the graph recipe's build) and reads NativeResult back through reflection
// right after the call returns. Nothing here runs on its own — no listeners, no timers.
//
// WHY THE WORDS LIVE HERE. Every string on a soldier panel goes into Flash through one
// AS_SetData(...) call and is retained nowhere; what the row keeps is UnitReference and two
// flags (m_bCanPromote / m_bKIA, which native reads off the card). So everything spoken below
// is re-resolved exactly the way UIAfterAction_ListItem.UpdateData resolved it, from the same
// unit state. See docs/after-action-recon.md §2 for the field-by-field parity map.
//
// SIGHTED PARITY, per field:
//   status + days out         -> AS_SetData's statusText/daysLabel/daysText — the colored header
//   rank / name / nick / class-> AS_SetData lines; class only for rank > 0 (the rookie rule)
//   missions / kills          -> AS_SetData label+value pairs; labels are class defaults, sent
//                                once on the mission record (strategy.md label rule)
//   health bar, will bar      -> AS_SetUnitHealth / AS_SetUnitWill
//   psi markup                -> PsiMarkup.Show(), alive branch only, icon-only on screen
//   bond icon state           -> SetBondLevel / the walk-up's own bond flyover strings
//   tired-shaken / cohesion   -> BuildFlyoverList's transient banners, folded into the card line
//   faction crest             -> SKIPPED: under the Highlander (#1134) it is a stacked CLASS
//                                icon, and the class is already spoken
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessAfterActionReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value, PRINTABLE separators (UnrealScript literals do not process escapes).
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// One record for the mission block, then one per CARD in m_kSlotList's own order — the order
// native walks, so both halves key on the list position. Unlike squad select there are no
// locked or empty cards: UpdateData creates an item only for an occupied squad slot.
function DescribeAfterAction()
{
	local UIAfterAction Screen;
	local UIAfterAction_ListItem Card;
	local int i, CohesionA, CohesionB;

	NativeResult = "";

	Screen = GetAfterAction();
	if (Screen == none)
		return;

	NativeResult = DescribeMission(Screen);

	// The top-cohesion pair is a property of the SQUAD, computed once and matched per card.
	FindTopCohesionPair(Screen, CohesionA, CohesionB);

	for (i = 0; i < Screen.m_kSlotList.ItemCount; ++i)
	{
		Card = UIAfterAction_ListItem(Screen.m_kSlotList.GetItem(i));
		if (Card == none)
			continue;
		NativeResult $= REC_SEP $ DescribeCard(Screen, Card, i, CohesionA, CohesionB);
	}
}

// ---------------------------------------------------------------------------------------
// The mission block
// ---------------------------------------------------------------------------------------

// Mirrors UIAfterAction.UpdateMissionInfo — which passes "" for difficulty and rewards, so this
// screen's block is the header, the operation name and the objective. The screen-wide labels
// ride here because they are class defaults, identical for every card: asking the card would
// answer "" and native skips an unlabelled node (strategy.md — squad select's founding defect).
private function string DescribeMission(UIAfterAction Screen)
{
	local GeneratedMissionData MissionData;
	local XComGameState_MissionSite MissionState;
	local string Rec;

	Rec = "kind=mission";

	// The banner UIAvengerHUD.FacilityHeader shows over this screen, and the screen's own name.
	AddField(Rec, "header", class'UIAfterAction'.default.m_strAfterActionReport);

	if (Screen.XComHQ != none)
	{
		MissionData = Screen.XComHQ.GetGeneratedMissionData(Screen.XComHQ.MissionRef.ObjectID);
		AddField(Rec, "op", MissionData.BattleOpName);
		MissionState = XComGameState_MissionSite(`XCOMHISTORY.GetGameStateForObjectID(
			Screen.XComHQ.MissionRef.ObjectID));
		if (MissionState != none)
			AddField(Rec, "objective", MissionState.GetMissionObjectiveText());
	}
	AddField(Rec, "objlabel", class'UISquadSelectMissionInfo'.default.m_strObjectives);

	// The verbs and per-card labels, once each. The promote label is the exact string
	// UpdateData passes into AS_SetData for the ПОВЫШЕНИЕ banner; Continue is the nav-help
	// button's own label (UINavigationHelp.AddContinueButton).
	AddField(Rec, "continue", class'UIUtilities_Text'.default.m_strGenericContinue);
	AddField(Rec, "promotelabel", class'UISquadSelect_ListItem'.default.m_strPromote);
	AddField(Rec, "memorial", class'UIAfterAction_ListItem'.default.m_strMemorialize);
	AddField(Rec, "missionslabel", class'UIAfterAction_ListItem'.default.m_strMissionsLabel);
	AddField(Rec, "killslabel", class'UIAfterAction_ListItem'.default.m_strKillsLabel);
	return Rec;
}

// ---------------------------------------------------------------------------------------
// One card
// ---------------------------------------------------------------------------------------

private function string DescribeCard(UIAfterAction Screen, UIAfterAction_ListItem Card,
	int ListIndex, int CohesionA, int CohesionB)
{
	local XComGameState_Unit Unit;
	local string Rec;

	Rec = "kind=card";
	// The join key: the card's position in m_kSlotList, the one index both halves walk in the
	// same order. A Card == none skip above leaves a HOLE rather than shifting later cards.
	AddField(Rec, "pos", string(ListIndex));

	Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Card.UnitReference.ObjectID));
	if (Unit == none)
	{
		AddField(Rec, "empty", "1");
		return Rec;
	}

	AddStatus(Rec, Unit);

	// Rank / name / nickname / class as SEPARATE fields, joined by native after CleanText has
	// run over each — the pre-join left a stray separator when a nickname cleaned to nothing.
	AddField(Rec, "rank", class'X2ExperienceConfig'.static.GetRankName(Unit.GetRank(),
		Unit.GetSoldierClassTemplateName()));
	AddField(Rec, "name", class'X2AccessHelper'.static.DescribeSoldierName(Unit));
	AddField(Rec, "nick", Unit.GetName(eNameType_Nick));
	// Rookies are told their rank and nothing else — UpdateData's own duplicate-string rule.
	if (Unit.GetRank() > 0 && Unit.GetSoldierClassTemplate() != none)
		AddField(Rec, "class", Unit.GetSoldierClassTemplate().DisplayName);

	AddField(Rec, "missions", string(Unit.GetNumMissions()));
	AddField(Rec, "kills", string(Unit.GetNumKills()));
	AddField(Rec, "hp", class'X2AccessHelper'.static.DescribeSoldierHealth(Unit));
	// Skip the will bar for a KIA or captured soldier: on the walk-up it means nothing.
	AddField(Rec, "will", class'X2AccessHelper'.static.DescribeSoldierWill(Unit, true));

	// The psi markup is shown in UpdateData's ALIVE branch only.
	if (Unit.IsAlive() && !Unit.bCaptured && Unit.HasPsiGift())
		AddField(Rec, "psi", class'X2AccessHelper'.static.PsiWord());

	// The walk-up's own "НАИБОЛЬШАЯ СПЛОЧЕННОСТЬ" flyover, persistent instead of transient.
	if (Card.UnitReference.ObjectID > 0
		&& (Card.UnitReference.ObjectID == CohesionA || Card.UnitReference.ObjectID == CohesionB))
		AddField(Rec, "cohesion", class'UIAfterAction'.default.m_strMostCohesion);

	// The bond icon exists (clickable, tooltip'd) exactly when UpdateData does not park it at
	// SetBondLevel(-1): a class that can bond, and not KIA. Two fields because the icon and its
	// words answer different questions — the unlit state gets NO words (the hard rule).
	if (Unit.GetSoldierClassTemplate() != none && Unit.GetSoldierClassTemplate().bCanHaveBonds
		&& !Unit.IsDead())
	{
		AddField(Rec, "canbond", "1");
		AddField(Rec, "bond", DescribeBond(Screen, Unit));
	}

	return Rec;
}

// The colored status header, worded exactly as UpdateData words it: MIA for captured, the
// severity-graded wound status plus days out while injured or healing, the active label
// otherwise, KIA for dead. Days are drawn label-then-number ("дн. 7"), hours rounded up.
private function AddStatus(out string Rec, XComGameState_Unit Unit)
{
	local int Days, InjuryHours;

	if (Unit.bCaptured)
	{
		AddField(Rec, "status", class'UIAfterAction_ListItem'.default.m_strMIA);
		return;
	}

	if (!Unit.IsAlive())
	{
		AddField(Rec, "status", class'UIAfterAction_ListItem'.default.m_strKIA);
		return;
	}

	if (Unit.IsInjured() || Unit.GetStatus() == eStatus_Healing)
	{
		AddField(Rec, "status", Caps(Unit.GetWoundStatus(InjuryHours, true)));
		if (InjuryHours > 0)
		{
			Days = InjuryHours / 24;
			if (InjuryHours % 24 > 0)
				Days += 1;
			AddField(Rec, "days",
				class'UIUtilities_Text'.static.GetDaysString(Days) @ string(Days));
		}
		return;
	}

	AddField(Rec, "status", class'UIAfterAction_ListItem'.default.m_strActive);
}

// The name / psi / health / will wording lives on X2AccessHelper's shared soldier block, so a
// soldier reads the same on both bookends of the mission by construction.

// The bond icon's states, in this screen's own words: the walk-up flyovers m_strBondAvailable /
// m_strBondLevelUpAvailable for the available states, and UpdateData draws the level for a
// standing bond. A bond whose bondmate is dead is left unlit by UpdateData and gets no words.
private function string DescribeBond(UIAfterAction Screen, XComGameState_Unit Unit)
{
	local StateObjectReference BondmateRef;
	local SoldierBond BondData;
	local XComGameState_Unit Bondmate;
	local string Result;

	if (Unit.HasSoldierBond(BondmateRef, BondData))
	{
		Bondmate = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(BondmateRef.ObjectID));
		if (Bondmate == none || !Bondmate.IsAlive())
			return "";

		Result = "bond level" @ string(BondData.BondLevel);
		if (BondData.bSoldierBondLevelUpAvailable)
			AppendComma(Result, class'UIAfterAction'.default.m_strBondLevelUpAvailable);
		return Result;
	}

	if (Unit.HasSoldierBondAvailable(BondmateRef, BondData))
		return class'UIAfterAction'.default.m_strBondAvailable;

	// The unlit icon: passed over by a sighted eye, so it gets no words. Its node still carries
	// the tooltip as its label, which is the parity a mouse player has.
	return "";
}

// ---------------------------------------------------------------------------------------
// The top-cohesion pair
// ---------------------------------------------------------------------------------------

// Mirrors UIAfterAction.AddMostCohesionFlyovers exactly, quirk included: MaxCohesion starts at
// 0, so the FIRST living pair becomes the candidate even at zero gain and later pairs must
// strictly beat it — whatever pair the game banners, these two IDs are it.
private function FindTopCohesionPair(UIAfterAction Screen, out int UnitA, out int UnitB)
{
	local XComGameStateHistory History;
	local XComGameState_Unit UnitStateA, UnitStateB;
	local SoldierBond BondData;
	local int i, j, MaxCohesion;

	UnitA = 0;
	UnitB = 0;
	if (Screen.XComHQ == none)
		return;

	History = `XCOMHISTORY;
	for (i = 0; i < Screen.XComHQ.Squad.Length - 1; i++)
	{
		if (Screen.XComHQ.Squad[i].ObjectID <= 0)
			continue;
		UnitStateA = XComGameState_Unit(History.GetGameStateForObjectID(
			Screen.XComHQ.Squad[i].ObjectID));
		if (UnitStateA == none || !UnitStateA.IsAlive() || UnitStateA.bCaptured)
			continue;

		for (j = i + 1; j < Screen.XComHQ.Squad.Length; j++)
		{
			if (Screen.XComHQ.Squad[j].ObjectID <= 0)
				continue;
			UnitStateB = XComGameState_Unit(History.GetGameStateForObjectID(
				Screen.XComHQ.Squad[j].ObjectID));
			if (UnitStateB == none || !UnitStateB.IsAlive() || UnitStateB.bCaptured)
				continue;

			UnitStateA.GetBondData(UnitStateB.GetReference(), BondData);
			if (MaxCohesion == 0 || BondData.MostRecentCohesionGain > MaxCohesion)
			{
				MaxCohesion = BondData.MostRecentCohesionGain;
				UnitA = UnitStateA.ObjectID;
				UnitB = UnitStateB.ObjectID;
			}
		}
	}
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

// The screen through the game's own stack, never cached: a UIScreen reference held on a CDO is
// the crash this project already paid for once.
private function UIAfterAction GetAfterAction()
{
	local UIScreenStack Stack;
	local UIAfterAction Screen;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return none;

	Screen = UIAfterAction(Stack.GetFirstInstanceOf(class'UIAfterAction'));
	if (Screen == none || Screen.m_kSlotList == none)
		return none;

	return Screen;
}

// Empty values are DROPPED rather than written as an empty field: a missing key means "the game
// is not showing this". No field on this screen repeats, so there is no positional exception.
private function AddField(out string Rec, string Key, string Value)
{
	class'X2AccessWire'.static.AddField(Rec, Key, Value);
}

// Call-site sugar over X2AccessHelper.AppendComma, which owns the never-a-bare-separator rule.
private function AppendComma(out string Result, string Part)
{
	class'X2AccessHelper'.static.AppendComma(Result, Part);
}
