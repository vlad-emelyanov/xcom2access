// X2AccessSquadReader
// The pre-mission squad select (UISquadSelect) — the words half. Same contract as
// X2AccessTacticalReader and X2AccessAvengerReader: the C++ layer owns the gate, the keys and
// the speech, calls DescribeSquad() on this class's default object via ProcessEvent (game
// thread, from the graph recipe's build) and reads NativeResult back through reflection right
// after the call returns. Nothing here runs on its own — no listeners, no timers, no polling.
//
// WHY THE WORDS LIVE HERE. This screen is the tactical HUD's pattern, not the ant farm's: its
// widget tree carries the whole SELECTION MODEL and not one word about the soldiers. Every
// string on a card goes into Flash through a single AS_SetFilled(...) call and is retained
// nowhere, and UISquadSelectMissionInfo.UpdateData is four QueueStrings that keep only the
// sit-rep. So native reads which card and which control (game_ui.cpp's ReadSquadSelect), and
// everything spoken below is re-resolved exactly the way the card resolved it.
// See docs/squad-select-recon.md §3.
//
// WHERE EACH FACT COMES FROM, in the order the hard rule prefers:
//   the screen's own fields   -> XComHQ (the state the screen cached), m_kMissionInfo.SitRepText
//   the card's own widgets    -> UtilitySlots, walked in list order with each item's SlotType /
//                                SlotIndex — the Highlander rebuilds that list through
//                                CHUIItemSlotEnumerator (issue #118), so counting pockets
//                                ourselves would be wrong the moment a slot-adding mod loads
//   a history lookup          -> the unit states, which is what UpdateData itself does
//
// SIGHTED PARITY, per field:
//   rank / name / nickname / class -> AS_SetFilled, the card's own top and bottom lines
//   psi gift marker / faction crest-> PsiMarkup.SetVisible / AS_SetFactionIcon, icon-only on the
//                                     card, so nothing but a word of ours can carry them
//   health bar, will bar           -> AS_SetUnitHealth / AS_SetUnitWill
//   mental state warning           -> AS_SetUnitStatus, drawn only while BelowReadyWillState
//   primary + heavy weapon         -> AS_SetFilled's weapon name/label pairs
//   utility slots                  -> the icon strip, one padlock per locked slot
//   promote chevron / bond icon    -> drawn only when the game draws them, hence the flags below
//   mission op / objective /       -> UISquadSelectMissionInfo.UpdateData, the top-left block a
//   difficulty / rewards / sit-rep    sighted player takes in at a glance and no focus visits
//
// ⛔ The class line is suppressed for rank 0 on purpose: UpdateData prints it only for
// Unit.GetRank() > 0, "Don't show class label for rookies since their rank is shown which would
// result in a duplicate string". Speaking it anyway would say "Rookie ... Rookie".
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessSquadReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching X2AccessNarrator's ENTRY_SEP — UnrealScript string
// literals do not process escapes, so a "backslash n" here would be those two literal characters
// and would split on nothing. A key=value shape means native can ignore a field it does not know
// yet, which is what lets the two halves be extended one at a time.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// One record for the mission header, then one per CARD in the screen's own list order — the
// order native's ReadSquadSelect walks, so the two sides index the same way without either
// having to know about SlotListOrder.
function DescribeSquad()
{
	local UISquadSelect Screen;
	local UISquadSelect_ListItem Card;
	local int i;

	NativeResult = "";

	Screen = GetSquadSelect();
	if (Screen == none)
		return;

	NativeResult = DescribeMission(Screen);

	for (i = 0; i < Screen.m_kSlotList.ItemCount; ++i)
	{
		Card = UISquadSelect_ListItem(Screen.m_kSlotList.GetItem(i));
		if (Card == none)
			continue;
		NativeResult $= REC_SEP $ DescribeCard(Screen, Card, i);
	}
}

// ---------------------------------------------------------------------------------------
// The mission header
// ---------------------------------------------------------------------------------------

// Mirrors UISquadSelect.UpdateMissionInfo exactly — same four values, from the same two objects
// the screen used — plus the sit-rep, which is the one string this screen retains.
private function string DescribeMission(UISquadSelect Screen)
{
	local GeneratedMissionData MissionData;
	local XComGameState_MissionSite MissionState;
	local string Rec;

	Rec = "kind=mission";
	if (Screen.XComHQ == none)
		return Rec;

	MissionData = Screen.XComHQ.GetGeneratedMissionData(Screen.XComHQ.MissionRef.ObjectID);
	MissionState = Screen.GetMissionState();

	AddField(Rec, "op", MissionData.BattleOpName);
	if (MissionState != none)
	{
		AddField(Rec, "objective", MissionState.GetMissionObjectiveText());
		AddField(Rec, "difficulty", MissionState.GetMissionDifficultyLabel());
		AddField(Rec, "rewards", MissionState.GetRewardAmountString());
	}
	// The LABELS the panel prints beside each value. Ours were English constants in native, which
	// put "objective, Уничтожить передатчик" three words from the game's own word for it — the same
	// defect X2AccessHelper.GetInputVocabulary exists to prevent on the tutorial side.
	AddField(Rec, "objlabel", class'UISquadSelectMissionInfo'.default.m_strObjectives);
	AddField(Rec, "difflabel", class'UISquadSelectMissionInfo'.default.m_strDifficulty);
	AddField(Rec, "rewlabel", class'UISquadSelectMissionInfo'.default.m_strRewards);
	if (Screen.m_kMissionInfo != none)
	{
		AddField(Rec, "sitrep", Screen.m_kMissionInfo.SitRepText);
		// What a sighted player gets by hovering the sit-rep panel, rebuilt the way UpdateSitRep
		// builds it. Without it a sit-rep names an effect and never says what the effect does.
		AddField(Rec, "sitrepdesc", DescribeSitReps(Screen));
	}
	// The game has no localized noun for "the squad" on this screen, so the count borrows the
	// personnel list's own tab caption — the word the game already uses for a roster of soldiers.
	AddField(Rec, "squadlabel", class'UIPersonnel'.default.m_strSoldierTab);
	AddField(Rec, "squadsize", DescribeSquadSize(Screen));

	// The two card verbs are localized on the card class, so they ride along here rather than
	// costing native two more transcribed offsets.
	AddField(Rec, "edit", class'UISquadSelect_ListItem'.default.m_strEdit);
	AddField(Rec, "dismiss", class'UISquadSelect_ListItem'.default.m_strDismiss);
	AddField(Rec, "promotelabel", class'UISquadSelect_ListItem'.default.m_strPromote);
	AddField(Rec, "selectunit", class'UISquadSelect_ListItem'.default.m_strSelectUnit);
	return Rec;
}

// ---------------------------------------------------------------------------------------
// One card
// ---------------------------------------------------------------------------------------

private function string DescribeCard(UISquadSelect Screen, UISquadSelect_ListItem Card,
	int ListIndex)
{
	local XComGameState_Unit Unit;
	local string Rec;

	Rec = "kind=card";
	// ⛔ The KEY is the card's position in m_kSlotList, NOT its SlotIndex. UpdateData bails at
	// `if(bDisabled) return;` BEFORE `SlotIndex = Index != -1 ? Index : SlotIndex`, so a locked
	// "increase squad size" card never gets one and keeps the default 0 — live 2026-08-23 three
	// of six cards read SlotIndex 0, and the two locked ones collided with the real squad-slot-0
	// soldier, who then wore the locked card's words. This is the loop index both halves walk in
	// the same order, so it is unique by construction; a `Card == none` skip above leaves a HOLE
	// rather than shifting every later card, which is the whole reason it is the list position
	// and not a running counter.
	AddField(Rec, "pos", string(ListIndex));
	// Kept for the readout and for diagnosis; never matched on.
	AddField(Rec, "slot", string(Card.SlotIndex));

	// A card the game greyed out ("increase squad size in the Guerrilla Tactics School"):
	// UpdateData returns before touching it, so everything on it is stale and the only honest
	// thing to say is its own locked label.
	if (Card.bDisabled)
	{
		AddField(Rec, "locked", class'UISquadSelect_ListItem'.default.m_strIncreaseSquadSize);
		return Rec;
	}

	Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Card.GetUnitRef().ObjectID));
	if (Unit == none)
	{
		AddField(Rec, "empty", "1");
		return Rec;
	}

	// Rank / name / nickname / class as SEPARATE fields, joined by native after CleanText has run
	// over each. Pre-joining them here meant a nickname that cleaned away to nothing left its
	// separator behind — live 2026-08-21 every rookie read "Новобранец, Sokolova,,".
	AddField(Rec, "rank", class'X2ExperienceConfig'.static.GetRankName(Unit.GetRank(),
		Unit.GetSoldierClassTemplateName()));
	AddField(Rec, "name", DescribeName(Unit));
	AddField(Rec, "nick", Unit.GetName(eNameType_Nick));
	// Rookies are told their rank and nothing else — see the header note.
	if (Unit.GetRank() > 0 && Unit.GetSoldierClassTemplate() != none)
		AddField(Rec, "class", Unit.GetSoldierClassTemplate().DisplayName);

	AddField(Rec, "hp", DescribeHealth(Unit));
	AddField(Rec, "will", DescribeWill(Unit));
	AddField(Rec, "weapon", DescribeWeapon(Unit, eInvSlot_PrimaryWeapon));
	AddField(Rec, "weaponlabel",
		class'UIArmory_Loadout'.default.m_strInventoryLabels[eInvSlot_PrimaryWeapon]);

	// The heavy-weapon control exists on the card only when the armour grants the slot —
	// AS_HasHeavyWeapon(false) hides the widget outright — so its absence here is what tells
	// native not to emit that node at all.
	if (Card.HasHeavyWeapon())
	{
		AddField(Rec, "heavy", Card.GetHeavyWeaponName());
		AddField(Rec, "heavylabel", Card.GetHeavyWeaponDesc());
	}

	// Same rule for the other two optional widgets: UpdateData passes the promote label as ""
	// unless ShowPromoteIcon(), and hides the bond icon (SetBondLevel(-1)) for a class that
	// cannot bond. A node we emit for a widget the game is not drawing would be a node a
	// sighted player has no counterpart for.
	if (Unit.ShowPromoteIcon())
		AddField(Rec, "promote", "1");
	if (Unit.GetSoldierClassTemplate() != none && Unit.GetSoldierClassTemplate().bCanHaveBonds)
	{
		// Two fields, because the ICON and its WORDS answer different questions: the icon exists
		// (and is clickable, and carries the bondmate tooltip) whenever the class can bond, while
		// the words exist only for the states the icon actually lights up for.
		AddField(Rec, "canbond", "1");
		AddField(Rec, "bond", DescribeBond(Screen, Unit));
	}

	// Two markers the card draws as ICONS and nothing else, so nothing above would ever have named
	// them. PsiMarkup.SetVisible(HasPsiGift()) is the psi one; the faction crest is drawn whenever
	// the soldier belongs to a faction, which on a Reaper also explains the missing utility slot.
	if (Unit.HasPsiGift())
		AddField(Rec, "psi", PsiWord());
	if (Unit.GetResistanceFaction() != none)
		AddField(Rec, "faction", Unit.GetResistanceFaction().GetFactionTitle());

	AppendUtilitySlots(Card, Unit, Rec);
	return Rec;
}

// The name line, falling back to the first name exactly as UpdateData does.
private function string DescribeName(XComGameState_Unit Unit)
{
	local string NameStr;

	NameStr = Unit.GetName(eNameType_Last);
	if (NameStr == "")
		NameStr = Unit.GetName(eNameType_First);
	return NameStr;
}

// The word for the psi-gift markup. That icon has no caption anywhere, so the honest label is the
// game's own name for what it means — the Psi Operative class — rather than a coinage of ours.
private function string PsiWord()
{
	local X2SoldierClassTemplate Psi;

	Psi = class'X2SoldierClassTemplateManager'.static.GetSoldierClassTemplateManager()
		.FindSoldierClassTemplate('PsiOperative');
	return Psi != none ? Psi.DisplayName : "";
}

// "4 of 6" — how many of the mission's slots hold a soldier. A sighted player counts the filled
// cards at a glance; serially, the empty ones are several arrow presses apart.
private function string DescribeSquadSize(UISquadSelect Screen)
{
	local int i, Filled, Total;

	if (Screen.XComHQ == none)
		return "";

	Total = Screen.GetTotalSlots();
	if (Total <= 0)
		return "";

	for (i = 0; i < Screen.XComHQ.Squad.Length && i < Total; ++i)
	{
		if (Screen.XComHQ.Squad[i].ObjectID > 0)
			Filled++;
	}
	return string(Filled) @ "of" @ string(Total);
}

// The sit-rep tooltip, rebuilt the way UISquadSelect.UpdateSitRep builds it: one "name: effect"
// line per template the mission actually shows.
private function string DescribeSitReps(UISquadSelect Screen)
{
	local GeneratedMissionData MissionData;
	local XComGameState_MissionSite MissionState;
	local X2SitRepTemplateManager SitRepManager;
	local X2SitRepTemplate SitRepTemplate;
	local name SitRepName;
	local string Result;

	if (Screen.XComHQ == none)
		return "";

	MissionState = Screen.GetMissionState();
	if (MissionState == none || MissionState.GetMissionSource().bBlockSitrepDisplay)
		return "";

	MissionData = Screen.XComHQ.GetGeneratedMissionData(Screen.XComHQ.MissionRef.ObjectID);
	SitRepManager = class'X2SitRepTemplateManager'.static.GetSitRepTemplateManager();
	foreach MissionData.SitReps(SitRepName)
	{
		SitRepTemplate = SitRepManager.FindSitRepTemplate(SitRepName);
		if (SitRepTemplate == none || SitRepTemplate.bExcludeFromStrategy)
			continue;
		if (Result != "")
			Result $= ". ";
		Result $= SitRepTemplate.GetFriendlyName() $ ":" @ SitRepTemplate.Description;
	}
	return Result;
}

// The health bar, in the same numbers UIUtilities_Strategy hands the bar itself. Worded exactly
// as X2AccessTacticalReader.DescribeTargetHealth words it, so a soldier reads the same here as
// in the mission that follows.
private function string DescribeHealth(XComGameState_Unit Unit)
{
	local int Cur, Max;

	Cur = class'UIUtilities_Strategy'.static.GetUnitCurrentHealth(Unit);
	Max = class'UIUtilities_Strategy'.static.GetUnitMaxHealth(Unit);
	if (Max <= 0)
		return "";

	return string(Cur) @ "of" @ string(Max) @ "health";
}

// The will bar plus the warning line under the name. The label is spoken only while the game
// draws it (AS_SetUnitStatus is called with "" otherwise), but the PERCENT is on screen as a
// bar whenever the unit uses the will system, so it is always worth saying.
private function string DescribeWill(XComGameState_Unit Unit)
{
	local string Result;

	if (!Unit.UsesWillSystem())
		return "";

	Result = string(class'UIUtilities_Strategy'.static.GetUnitWillPercent(Unit)) $ "% will";
	if (Unit.BelowReadyWillState())
		AppendComma(Result, Unit.GetMentalStateLabel());

	return Result;
}

private function bool IsOnSquad(UISquadSelect Screen, StateObjectReference UnitRef)
{
	local int i;

	if (Screen.XComHQ == none || UnitRef.ObjectID <= 0)
		return false;

	for (i = 0; i < Screen.XComHQ.Squad.Length; ++i)
	{
		if (Screen.XComHQ.Squad[i].ObjectID == UnitRef.ObjectID)
			return true;
	}
	return false;
}

private function string DescribeWeapon(XComGameState_Unit Unit, EInventorySlot Slot)
{
	local XComGameState_Item Item;

	Item = Unit.GetItemInSlot(Slot);
	if (Item == none)
		return "";

	// The per-instance name, not the template's: a weapon carries its upgrades and its nickname.
	return Item.GetMyTemplate().GetItemFriendlyName(Item.ObjectID);
}

// The bond icon's own three states, which is all the icon shows: bonded (with the level and
// whether the bondmate is coming along), available, or nothing yet.
private function string DescribeBond(UISquadSelect Screen, XComGameState_Unit Unit)
{
	local StateObjectReference BondmateRef;
	local SoldierBond BondData;
	local string Result;

	if (Unit.HasSoldierBond(BondmateRef, BondData))
	{
		Result = "bond level" @ string(BondData.BondLevel);
		// ⛔ NOT Screen.IsUnitOnSquad(): that one starts by rewriting XComHQ through
		// UpdateState.ModifyStateObject, so a reader calling it would both mutate a pending
		// state and access none whenever no edit has been started. The question is a plain
		// lookup in the squad the screen already cached.
		if (IsOnSquad(Screen, BondmateRef))
			AppendComma(Result, "bondmate on the squad");
		return Result;
	}

	if (Unit.HasSoldierBondAvailable(BondmateRef, BondData))
		return class'XComHQPresentationLayer'.default.m_strBannerBondAvailable;

	// ⛔ The third state gets NO words. UpdateData leaves the icon at SetBondLevel(0) with
	// AnimateCohesion(false) — an unlit slot a sighted eye passes straight over — and saying
	// "SOLDIER BOND" about it shouted a label on every rookie in the squad (live 2026-08-21: four
	// cards, four times). The icon's own node still carries it as a tooltip, which is exactly the
	// parity a mouse player has; the card header stays quiet.
	return "";
}

// ---------------------------------------------------------------------------------------
// Utility slots
// ---------------------------------------------------------------------------------------

// One `util=` field per slot, in the card's own list order — the order native walks and the
// order the game's Left/Right walks, EMITTED EVEN WHEN EMPTY so the two sides stay aligned. The panel is asked what KIND of slot it is and which index
// within that kind, because that pair is what UpdateData filled it from; asking the unit how
// many pockets it has would disagree with the Highlander's CHUIItemSlotEnumerator rebuild.
private function AppendUtilitySlots(UISquadSelect_ListItem Card, XComGameState_Unit Unit,
	out string Rec)
{
	local UISquadSelect_UtilityItem Slot;
	local array<XComGameState_Item> Equipped;
	local string Text;
	local int i;

	if (Card.UtilitySlots == none)
		return;

	for (i = 0; i < Card.UtilitySlots.ItemCount; ++i)
	{
		Slot = UISquadSelect_UtilityItem(Card.UtilitySlots.GetItem(i));
		if (Slot == none)
			continue;

		Equipped = class'UIUtilities_Strategy'.static.GetEquippedItemsInSlot(Unit, Slot.SlotType);
		if (Slot.SlotIndex >= 0 && Slot.SlotIndex < Equipped.Length
			&& Equipped[Slot.SlotIndex] != none)
		{
			Text = Equipped[Slot.SlotIndex].GetMyTemplate().GetItemFriendlyName(
				Equipped[Slot.SlotIndex].ObjectID);
		}
		else
		{
			// An empty slot and a LOCKED one look different on screen (an outline versus a
			// padlock) and native already knows which from the item's own Button being
			// disabled — so the word here is only ever "what would go in it".
			Text = SlotKindLabel(Slot);
		}
		AddPositionalField(Rec, "util", Text);
	}
}

// What the slot is FOR, from the slot panel's own localized labels where it has them and the
// loadout screen's inventory labels otherwise — the same strings the game prints under the icon.
private function string SlotKindLabel(UISquadSelect_UtilityItem Slot)
{
	switch (Slot.SlotType)
	{
	case eInvSlot_GrenadePocket:
		return Slot.GrenadeSlot;
	case eInvSlot_AmmoPocket:
		return Slot.AmmoSlot;
	default:
		return class'UIArmory_Loadout'.default.m_strInventoryLabels[Slot.SlotType];
	}
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

// The screen through the game's own stack, never cached: a UIScreen reference held on a CDO is
// the crash this project already paid for once.
private function UISquadSelect GetSquadSelect()
{
	local UIScreenStack Stack;
	local UISquadSelect Screen;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return none;

	Screen = UISquadSelect(Stack.GetFirstInstanceOf(class'UISquadSelect'));
	if (Screen == none || Screen.m_kSlotList == none)
		return none;

	return Screen;
}

// Empty values are DROPPED rather than written as an empty field: a missing key means "the game
// is not showing this", which is exactly the distinction the recipe needs to decide whether a
// node exists at all.
private function AddField(out string Rec, string Key, string Value)
{
	if (Value == "")
		return;
	// Markup is left in: native runs every value through GameUI::CleanText on the way out, so
	// there is exactly one implementation of "strip the HTML the game colours its text with".
	Rec $= FLD_SEP $ Key $ "=" $ Value;
}

// The exception to the drop-empties rule, for the one field that repeats: `util` is POSITIONAL
// — the nth one describes the nth widget in the card's utility row, which is exactly what
// native's SquadControl::utilityIndex counts. Dropping an empty one shifts every later slot up by
// one, so an empty pocket in front of a grenade made the grenade answer for the pocket. Emit the
// blank and let native decide what to say about it.
private function AddPositionalField(out string Rec, string Key, string Value)
{
	Rec $= FLD_SEP $ Key $ "=" $ Value;
}

// Call-site sugar over X2AccessHelper.AppendComma, which owns the ⛔ never-a-bare-separator rule.
private function AppendComma(out string Result, string Part)
{
	class'X2AccessHelper'.static.AppendComma(Result, Part);
}
