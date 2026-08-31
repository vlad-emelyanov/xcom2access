// X2AccessLoadoutReader
// The armory loadout (UIArmory_Loadout) — the words half. Same contract as X2AccessSquadReader:
// the C++ layer owns the gate, the keys and the speech, calls DescribeLoadout() on this class's
// default object via ProcessEvent (game thread, from the graph recipe's build) and reads
// NativeResult back through reflection right after the call returns. Nothing here runs on its
// own — no listeners, no timers, no polling.
//
// ⛔ THIS CLASS IS DELIBERATELY SMALL, and the reason is the interesting part. Unlike squad
// select — where every string on a card goes into Flash and is retained nowhere — a
// UIArmory_LoadoutItem KEEPS its words: Title, Subtitle, SlotType, Count and its five state bits
// are real script properties, each written under a dirty check immediately before the matching
// Flash op. So native reads the rows directly (game_ui.cpp's ReadArmoryLoadout) and this class
// supplies only the four things the screen computes and then throws away:
//
//   count      SetCount pushes `XEXPAND.ExpandString(m_strCount)` into Flash and retains only the
//              NUMBER, so the localized wording around it has to be rebuilt here
//   reason     `disabledText` is declared on the row and NEVER ASSIGNED — SetDisabled queues the
//              reason straight into Flash — so the only way back to it is to ask the screen the
//              same question it asked: GetDisabledReason for the locker, CHUIItemSlotEnumerator's
//              LockedReason for an equipped slot
//   detail     the hover tooltip's stat block (UIArmory_LoadoutItemTooltip.RefreshData), which on
//              mouse+keyboard is the ONLY place an item's numbers appear — spoken on the
//              navigator's tooltip key rather than on every arrow step
//   delta      what changes if this item is equipped, which is what a sighted player watches the
//              soldier header do while they move down the locker. Attached to the ROW rather than
//              to the header on purpose: our focus write-back lands AFTER the render that
//              announces, so a header-sourced delta would always describe the PREVIOUS row.
//
// ⚠ Equipped-row order is CHUIItemSlotEnumerator order, because that is what the Highlander's
// UpdateEquippedList builds the rows from — the mod requires the Highlander, and matching its
// order is what lets a locked slot's reason be found by list POSITION instead of by guessing
// which slot a row meant.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessLoadoutReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// One header record, then one record per row of the EQUIPPED list, then — only while the locker
// is the active list — one per locker row. Native matches them to its own reads by position
// within each `list` group, which is exact because both sides walk the same UILists.
function DescribeLoadout()
{
	local UIArmory_Loadout Screen;
	local UIArmory_LoadoutItem Row;
	local array<string> LockedReasons;
	local int i;

	NativeResult = "";

	Screen = GetLoadout();
	if (Screen == none)
		return;

	NativeResult = DescribeHeader(Screen);

	CollectLockedReasons(Screen, LockedReasons);
	for (i = 0; i < Screen.EquippedList.ItemCount; ++i)
	{
		Row = UIArmory_LoadoutItem(Screen.EquippedList.GetItem(i));
		if (Row == none)
			continue;
		NativeResult $= REC_SEP $ DescribeRow(Screen, Row, "eq",
			i < LockedReasons.Length ? LockedReasons[i] : "");
	}

	// The locker only exists as a thing to read while it is the list the game is driving; the
	// rest of the time it holds whatever the last slot's choices were.
	if (Screen.ActiveList != Screen.LockerList)
		return;

	for (i = 0; i < Screen.LockerList.ItemCount; ++i)
	{
		Row = UIArmory_LoadoutItem(Screen.LockerList.GetItem(i));
		if (Row == none)
			continue;
		NativeResult $= REC_SEP $ DescribeRow(Screen, Row, "lk", "");
	}
}

// ---------------------------------------------------------------------------------------
// The soldier header
// ---------------------------------------------------------------------------------------

// Whose loadout this is, plus the stat block UISoldierHeader draws beside the pawn. No focus
// visits either on mouse+keyboard, and after two screens of arrowing "which soldier am I even
// editing" is a real question.
private function string DescribeHeader(UIArmory_Loadout Screen)
{
	local XComGameState_Unit Unit;
	local string Rec, StatusDesc, TimeLabel, TimeValue;

	Rec = "kind=header";

	// The screen's own unit, never a fresh lookup of "the selected soldier": the armory can be
	// walked with the prev/next soldier keys and UnitReference is what it moved.
	Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(
		Screen.UnitReference.ObjectID));
	if (Unit == none)
		return Rec;

	AddField(Rec, "who", DescribeSoldier(Unit));

	if (Unit.IsAlive())
	{
		class'UIUtilities_Strategy'.static.GetPersonnelStatusSeparate(Unit, StatusDesc,
			TimeLabel, TimeValue, , true);
		if (TimeValue != "")
			StatusDesc @= TimeValue @ TimeLabel;
		AddField(Rec, "status", StatusDesc);
	}

	AddField(Rec, "stats", DescribeStats(Unit, Screen));

	// The three strip verbs are localized on UISquadSelect even here, and their labels are the
	// one thing native cannot read off THIS screen.
	AddField(Rec, "stripitems", class'UISquadSelect'.default.m_strStripItems);
	AddField(Rec, "stripweapons", class'UISquadSelect'.default.m_strStripWeapons);
	AddField(Rec, "stripgear", class'UISquadSelect'.default.m_strStripGear);
	return Rec;
}

private function string DescribeSoldier(XComGameState_Unit Unit)
{
	local string Result;

	Result = Unit.GetName(eNameType_FullNick);
	if (Unit.GetRank() > 0)
	{
		AppendComma(Result, class'X2ExperienceConfig'.static.GetRankName(Unit.GetRank(),
			Unit.GetSoldierClassTemplateName()));
		if (Unit.GetSoldierClassTemplate() != none)
			AppendComma(Result, Unit.GetSoldierClassTemplate().DisplayName);
	}
	return Result;
}

// The header's own stat column, mirroring UISoldierHeader.PopulateData: the base stat plus what
// abilities give, then the inventory's contribution as the signed bonus the screen prints beside
// it. Psi is included only for a psi operative, exactly as the header gates it.
private function string DescribeStats(XComGameState_Unit Unit, UIArmory_Loadout Screen)
{
	local string Result;
	local XComGameState CheckState;

	CheckState = Screen.CheckGameState;

	AppendComma(Result, StatLine(class'UISoldierHeader'.default.m_strAimLabel, Unit, CheckState,
		eStat_Offense));
	AppendComma(Result, StatLine(class'UISoldierHeader'.default.m_strHealthLabel, Unit, CheckState,
		eStat_HP));
	AppendComma(Result, StatLine(class'UISoldierHeader'.default.m_strMobilityLabel, Unit,
		CheckState, eStat_Mobility));
	AppendComma(Result, StatLine(class'UISoldierHeader'.default.m_strWillLabel, Unit, CheckState,
		eStat_Will));
	AppendComma(Result, StatLine(class'UISoldierHeader'.default.m_strArmorLabel, Unit, CheckState,
		eStat_ArmorMitigation));
	AppendComma(Result, StatLine(class'UISoldierHeader'.default.m_strDodgeLabel, Unit, CheckState,
		eStat_Dodge));
	AppendComma(Result, StatLine(class'UISoldierHeader'.default.m_strTechLabel, Unit, CheckState,
		eStat_Hacking));
	if (Unit.IsPsiOperative())
		AppendComma(Result, StatLine(class'UISoldierHeader'.default.m_strPsiLabel, Unit,
			CheckState, eStat_PsiOffense));

	return Result;
}

private function string StatLine(string Label, XComGameState_Unit Unit, XComGameState CheckState,
	ECharStatType Stat)
{
	local int Base, Bonus;
	local string Result;

	Base = int(Unit.GetCurrentStat(Stat)) + Unit.GetUIStatFromAbilities(Stat);
	Bonus = Unit.GetUIStatFromInventory(Stat, CheckState);

	Result = Label @ string(Base);
	if (Bonus > 0)
		Result $= " +" $ string(Bonus);
	else if (Bonus < 0)
		Result $= " " $ string(Bonus);
	return Result;
}

// ---------------------------------------------------------------------------------------
// One row
// ---------------------------------------------------------------------------------------

private function string DescribeRow(UIArmory_Loadout Screen, UIArmory_LoadoutItem Row,
	string ListKind, string LockedReason)
{
	local XComGameState_Item Item;
	local XGParamTag Tag;
	local string Rec;

	Rec = "kind=row";
	AddField(Rec, "list", ListKind);

	Item = XComGameState_Item(`XCOMHISTORY.GetGameStateForObjectID(Row.ItemRef.ObjectID));

	// The wording SetCount pushed into Flash. The number itself is retained and native already
	// has it; what is lost is the localized sentence around it, and a bare "3" after an item
	// name does not say what three of it means.
	if (ListKind == "lk" && Item != none && !Row.IsInfinite)
	{
		Tag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
		Tag.IntValue0 = Row.Count;
		AddField(Rec, "count", `XEXPAND.ExpandString(Row.m_strCount));
	}

	if (Row.IsDisabled)
	{
		if (ListKind == "lk" && Item != none)
			AddField(Rec, "reason", Screen.GetDisabledReason(Item, Screen.GetSelectedSlot()));
		else
			AddField(Rec, "reason", LockedReason);
	}

	if (Item != none)
	{
		AddField(Rec, "detail", DescribeItemStats(Item));
		if (ListKind == "lk")
			AddField(Rec, "delta", DescribeDelta(Screen, Item));
	}
	return Rec;
}

// The tooltip's own stat block, from the item's own summary — the same array
// UIArmory_LoadoutItemTooltip hands to its UIStatList.
private function string DescribeItemStats(XComGameState_Item Item)
{
	local array<UISummary_ItemStat> Stats;
	local string Result;
	local int i;

	Stats = Item.GetUISummary_ItemBasicStats();
	for (i = 0; i < Stats.Length; ++i)
	{
		if (Stats[i].Label == "" && Stats[i].Value == "")
			continue;
		AppendComma(Result, Stats[i].Label @ Stats[i].Value);
	}
	return Result;
}

// What equipping this would do to the soldier, computed the way UISoldierHeader computes the
// numbers it recolours: the candidate's markup minus the markup of whatever currently occupies
// the slot the locker was opened for.
private function string DescribeDelta(UIArmory_Loadout Screen, XComGameState_Item Candidate)
{
	local UIArmory_LoadoutItem SelectedSlotRow;
	local XComGameState_Item Current;
	local string Result;

	// ⛔ The item being REPLACED is the one in the selected equipped ROW, not
	// GetEquippedItem(GetSelectedSlot()): a soldier with two utility slots has two rows sharing
	// eInvSlot_Utility, and that getter answers with the first of them whichever row is open —
	// so on the second slot the delta would have been measured against the wrong item.
	SelectedSlotRow = UIArmory_LoadoutItem(Screen.EquippedList.GetSelectedItem());
	if (SelectedSlotRow != none)
		Current = XComGameState_Item(`XCOMHISTORY.GetGameStateForObjectID(
			SelectedSlotRow.ItemRef.ObjectID));

	if (Current != none && Current.ObjectID == Candidate.ObjectID)
		return ""; // the row for what is already in the slot

	AppendComma(Result, DeltaLine(class'UISoldierHeader'.default.m_strAimLabel, Candidate,
		Current, eStat_Offense));
	AppendComma(Result, DeltaLine(class'UISoldierHeader'.default.m_strHealthLabel, Candidate,
		Current, eStat_HP));
	AppendComma(Result, DeltaLine(class'UISoldierHeader'.default.m_strMobilityLabel, Candidate,
		Current, eStat_Mobility));
	AppendComma(Result, DeltaLine(class'UISoldierHeader'.default.m_strWillLabel, Candidate,
		Current, eStat_Will));
	AppendComma(Result, DeltaLine(class'UISoldierHeader'.default.m_strArmorLabel, Candidate,
		Current, eStat_ArmorMitigation));
	AppendComma(Result, DeltaLine(class'UISoldierHeader'.default.m_strDodgeLabel, Candidate,
		Current, eStat_Dodge));
	AppendComma(Result, DeltaLine(class'UISoldierHeader'.default.m_strTechLabel, Candidate,
		Current, eStat_Hacking));
	return Result;
}

private function string DeltaLine(string Label, XComGameState_Item Candidate,
	XComGameState_Item Current, ECharStatType Stat)
{
	local int Change;

	Change = StatMarkup(Candidate, Stat) - StatMarkup(Current, Stat);
	if (Change == 0)
		return "";
	return Label @ (Change > 0 ? "+" $ string(Change) : string(Change));
}

// ⛔ The exclusion list is the header's, not ours: a sword's own boost is already inside the
// weapon's stats and counting it here would double it, which is exactly why UISoldierHeader
// carries EquipmentExcludedFromStatBoosts at all.
private function int StatMarkup(XComGameState_Item Item, ECharStatType Stat)
{
	local X2EquipmentTemplate Equipment;

	if (Item == none)
		return 0;
	Equipment = X2EquipmentTemplate(Item.GetMyTemplate());
	if (Equipment == none)
		return 0;
	if (class'UISoldierHeader'.default.EquipmentExcludedFromStatBoosts.Find(Equipment.DataName)
		!= INDEX_NONE)
		return 0;
	return Equipment.GetUIStatMarkup(Stat, Item);
}

// ---------------------------------------------------------------------------------------
// Locked equipped slots
// ---------------------------------------------------------------------------------------

// One entry per equipped ROW, in row order, holding the refusal that row was built with. The
// Highlander's UpdateEquippedList creates the rows straight out of this enumerator, so walking it
// again lines up by position — which matters because a soldier can have two utility slots with
// only the second locked, and nothing on the row itself says which is which.
private function CollectLockedReasons(UIArmory_Loadout Screen, out array<string> Reasons)
{
	local CHUIItemSlotEnumerator En;
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(
		Screen.UnitReference.ObjectID));
	if (Unit == none)
		return;

	En = class'CHUIItemSlotEnumerator'.static.CreateEnumerator(Unit, Screen.CheckGameState);
	while (En.HasNext())
	{
		En.Next();
		if (Screen.CannotEditSlotsList.Find(En.Slot) != INDEX_NONE)
			Reasons.AddItem(Screen.m_strCannotEdit);
		else if (En.IsLocked)
			Reasons.AddItem(En.LockedReason);
		else
			Reasons.AddItem("");
	}
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

// The screen through the game's own stack, never cached: a UIScreen reference held on a CDO is
// the crash this project already paid for once.
private function UIArmory_Loadout GetLoadout()
{
	local UIScreenStack Stack;
	local UIArmory_Loadout Screen;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return none;

	Screen = UIArmory_Loadout(Stack.GetFirstInstanceOf(class'UIArmory_Loadout'));
	if (Screen == none || Screen.EquippedList == none || Screen.LockerList == none)
		return none;

	return Screen;
}

// Empty values are DROPPED rather than written as an empty field: a missing key means "the game
// is not showing this", which is the distinction the recipe needs to decide whether a node or an
// announcement part exists at all.
private function AddField(out string Rec, string Key, string Value)
{
	class'X2AccessWire'.static.AddField(Rec, Key, Value);
}

// Call-site sugar over X2AccessHelper.AppendComma, which owns the ⛔ never-a-bare-separator rule.
private function AppendComma(out string Result, string Part)
{
	class'X2AccessHelper'.static.AppendComma(Result, Part);
}
