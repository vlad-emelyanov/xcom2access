// X2AccessRoomReader
// The un-excavated room screen (UIRoom) — the words half. Same contract as X2AccessSaveReader
// and X2AccessSquadReader: C++ owns the gate, the keys and the speech, calls DescribeRoom() on
// this class's default object via ProcessEvent from the graph recipe's build, and reads
// NativeResult back through reflection right after the call returns. No listeners, no timers.
//
// WHY THIS SCREEN NEEDS A COMPANION AT ALL. Almost everything on it is Flash-only:
// ExcavationPanel takes five strings through MC.BeginFunctionOp("UpdateData") and retains none;
// UIStaffSlot resolves the occupant's name in UpdateData and keeps only StaffSlotRef; and the
// personnel drop-down's rows call AS_UpdateData directly, BYPASSING UIButton.SetText, so even
// the retained UIButton.Text is empty on them. The widget tree here holds structure and nothing
// else — the tactical HUD's problem, in the strategy layer.
//
// ⛔ WHAT THIS SCREEN IS, and why it is not UIFacility. An un-built room has no facility, so it
// gets UIRoom instead: one or more BUILD slots (Room.BuildSlots, not Facility.StaffSlots), a
// status panel, and an excavate button. Both halves of "assign an engineer and clear the debris"
// live here, and both dead-end on mouse+keyboard in vanilla:
//   * ExcavateButton calls DisableNavigation(), so no keyboard can focus it, and UIRoom.OnAccept
//     — what Enter and Space reach — is an EMPTY FUNCTION. StartExcavation is wired only to
//     FXS_BUTTON_X, a controller button. Clicking the button is the only way in.
//   * The staff slots move on FXS_DPAD_LEFT/RIGHT, which are controller constants; the ARROW
//     constants are not in that switch at all.
// So this is not a screen we are merely narrating: the recipe is what makes it operable.
//
// Every string below is taken from the SCREEN's own accessors (GetExcavationStatusString,
// GetExcavationTimeEstimateString, GetExcavationRewardString) or re-derived exactly the way the
// widget that hid it derived it — UIPersonnel_DropDownListItem.UpdateData, line for line. That
// keeps one source of truth for words a sighted player is reading off the same pixels.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

// dependson: StaffUnitInfo and EStaffStatus are both declared INSIDE
// X2StrategyGameRulesetDataStructures, not in a global header, so a class in another package
// needs it in scope by name — the same reason X2AccessAvengerReader dependson(UIStaffIcon).
class X2AccessRoomReader extends Object
	dependson(X2StrategyGameRulesetDataStructures);

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching X2AccessSaveReader's and X2AccessSquadReader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants stops:
//   kind=status    header, title, time, rewardLabel, reward
//   kind=slot      index, label, bonus, filled, locked
//   kind=excavate  label, enabled
//   kind=candidate index, name, status          (only while the drop-down is open)
//
// The drop-down records are emitted LAST and are what tells native the picker is up: while it
// is, the room's own stops are not reachable anyway (the game routes input into the list), so
// the recipe swaps to a candidate-only graph rather than showing both.
function DescribeRoom()
{
	local UIRoom Room;

	NativeResult = "";

	Room = UIRoom(`SCREENSTACK.GetCurrentScreen());
	if (Room == none)
		return;

	AppendStatus(Room);
	AppendSlots(Room);
	AppendExcavate(Room);
	AppendCandidates(Room);
}

// ---------------------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------------------

// The status panel, which DisableNavigation() puts beyond every keyboard in the game. Its five
// strings come from the screen's own accessors, so what we speak is what Flash was told — and
// the accessors already branch on under-construction / clearing / staffed / no-engineers, which
// is the whole state machine of this screen expressed once, by the game.
private function AppendStatus(UIRoom Room)
{
	local XComGameState_HeadquartersRoom RoomState;
	local XComGameState_FacilityXCom Building;
	local string Rec;

	RoomState = Room.GetRoom();
	if (RoomState == none)
		return;

	Rec = "kind=status";
	// ⛔ The room's NAME has to come from here, not from UIAvengerHUD.FacilityHeader: UIRoom
	// never sets that header — only UIFacility.RealizeFacility does — so it still holds whatever
	// the previous screen put there. Live 2026-08-21 that made entering a debris room announce
	// "РЕЗУЛЬТАТЫ ЗАДАНИЯ" (MISSION RESULTS), left over from the post-mission screen.
	// These three branches are the grid overlay's own, so the room is called the same thing here
	// as it is one screen up.
	// ⛔ The FACILITY BEING BUILT, not m_strFacilityLabel — that is the word "ОТСЕК", a column
	// heading, so naming the room with it announced a construction site as, literally, "ROOM".
	// The grid overlay one screen up calls it GetFacility().GetMyTemplate().DisplayName
	// (UpdateConstructionFacility), and so does GetExcavationRewardString's own construction
	// branch; this is the same read, so all three agree on what the room is called.
	if (RoomState.UnderConstruction && RoomState.GetBuildFacilityProject() != none)
	{
		Building = XComGameState_FacilityXCom(`XCOMHISTORY.GetGameStateForObjectID(
			RoomState.GetBuildFacilityProject().ProjectFocus.ObjectID));
		if (Building != none && Building.GetMyTemplate() != none)
			Rec $= FLD_SEP $ "room=" $ Building.GetMyTemplate().DisplayName;
	}
	else if (RoomState.ClearingRoom)
		Rec $= FLD_SEP $ "room=" $ RoomState.GetClearingInProgressLabel();
	else if (RoomState.HasSpecialFeature())
		Rec $= FLD_SEP $ "room=" $ RoomState.GetSpecialFeatureLabel();

	Rec $= FLD_SEP $ "header=" $ Room.m_strInfoLabel;
	Rec $= FLD_SEP $ "title=" $ Room.GetExcavationStatusString();
	Rec $= FLD_SEP $ "time=" $ Room.GetExcavationTimeEstimateString();

	// UpdateData picks the reward header the same way; a room under construction is showing the
	// facility being built, not loot.
	if (RoomState.UnderConstruction)
		Rec $= FLD_SEP $ "rewardLabel=" $ Room.m_strFacilityLabel;
	else
		Rec $= FLD_SEP $ "rewardLabel=" $ Room.m_strExcavationReward;

	Rec $= FLD_SEP $ "reward=" $ Room.GetExcavationRewardString();
	Push(Rec);
}

// One record per BUILD slot. UIRoomStaffContainer.Refresh shows only the first while the room is
// under construction, and this mirrors that rather than listing slots the player cannot see.
private function AppendSlots(UIRoom Room)
{
	local XComGameState_HeadquartersRoom RoomState;
	local XComGameState_StaffSlot SlotState;
	local UIStaffSlot Slot;
	local string Rec;
	local int i;

	if (Room.m_kStaffSlotContainer == none)
		return;

	RoomState = Room.GetRoom();

	for (i = 0; i < Room.m_kStaffSlotContainer.StaffSlots.Length; i++)
	{
		Slot = Room.m_kStaffSlotContainer.StaffSlots[i];
		if (Slot == none)
			continue;

		SlotState = XComGameState_StaffSlot(`XCOMHISTORY.GetGameStateForObjectID(Slot.StaffSlotRef.ObjectID));
		if (SlotState == none)
			continue;

		Rec = "kind=slot";
		Rec $= FLD_SEP $ "index=" $ string(i);
		// GetNameDisplayString is what the widget put in its name field: the occupant when the
		// slot is filled, the template's own "open slot" wording when it is not. Taking it from
		// the state object rather than inventing an empty-slot phrase keeps us in the player's
		// language for free.
		Rec $= FLD_SEP $ "label=" $ SlotState.GetNameDisplayString();
		Rec $= FLD_SEP $ "bonus=" $ SlotState.GetBonusDisplayString();
		Rec $= FLD_SEP $ "filled=" $ (SlotState.IsSlotFilled() ? "1" : "0");
		Rec $= FLD_SEP $ "locked=" $ (SlotState.IsLocked() ? "1" : "0");
		// The disabled state the widget itself computes (UIStaffSlot.UpdateData → SetDisabled):
		// no unit of the right kind is free, so clicking opens an empty picker.
		Rec $= FLD_SEP $ "available=" $ (SlotState.IsUnitAvailableForThisSlot() ? "1" : "0");
		Push(Rec);

		if (RoomState != none && RoomState.UnderConstruction)
			break;
	}
}

// The excavate button. Its label is genuinely ambiguous in vanilla: Enable/DisableExcavateButton
// call SetText only under `if(!Movie.IsMouseActive())`, so on mouse+keyboard the button keeps
// whatever InitLargeButton gave it. Report the retained Text when there is one and the screen's
// own m_strExcavate when there is not, so the word is always the game's rather than ours.
private function AppendExcavate(UIRoom Room)
{
	local string Rec, Label;

	if (Room.ExcavateButton == none || !Room.ExcavateButton.bIsVisible)
		return;

	Label = Room.ExcavateButton.Text;
	if (Label == "")
		Label = Room.m_strExcavate;

	Rec = "kind=excavate";
	Rec $= FLD_SEP $ "label=" $ Label;
	Rec $= FLD_SEP $ "enabled=" $ (Room.ExcavateButton.IsDisabled ? "0" : "1");
	// What the game would say instead of the verb if it were in keyboard mode — the reason the
	// button is refused, which a sighted player reads off the status panel's red line.
	if (Room.ExcavateButton.IsDisabled)
		Rec $= FLD_SEP $ "reason=" $ Room.m_strEngineerRequired;
	Push(Rec);
}

// The personnel picker, when it is open. Mirrors UIPersonnel_DropDownListItem.UpdateData: the
// same name (rank-qualified for soldiers, ghost-renamed for a gremlin), the same localized
// status, and the same m_strEmptyStaff for the "take them out" row the drop-down puts first
// when the slot is already filled.
private function AppendCandidates(UIRoom Room)
{
	local UIPersonnel_DropDown Drop;
	local XComGameState_Unit Unit, PairUnit;
	local StaffUnitInfo Info;
	local EStaffStatus StafferStatus;
	local string Rec, UnitName, UnitStatus;
	local int i;

	if (Room.m_kStaffSlotContainer == none)
		return;

	Drop = Room.m_kStaffSlotContainer.m_kPersonnelDropDown;
	if (Drop == none || !Drop.bIsVisible)
		return;

	for (i = 0; i < Drop.m_staff.Length; i++)
	{
		Info = Drop.m_staff[i];
		Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Info.UnitRef.ObjectID));

		if (Unit == none)
		{
			// An empty ref is the "remove the current staffer" row, and the drop-down really
			// does put one there whenever the slot is filled. It has to be spoken, or the only
			// way to un-assign somebody is silence.
			UnitName = class'UIUtilities_Strategy'.default.m_strEmptyStaff;
			UnitStatus = "";
		}
		else
		{
			StafferStatus = class'X2StrategyGameRulesetDataStructures'.static.GetStafferStatus(Info);
			UnitStatus = class'UIUtilities_Strategy'.default.m_strStaffStatus[StafferStatus];

			if (Unit.IsSoldier())
			{
				UnitName = Unit.GetName(eNameType_RankFull);
				PairUnit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Info.PairUnitRef.ObjectID));
				if (PairUnit != none && PairUnit.IsSoldier())
					UnitName @= class'UIUtilities_Text'.default.m_strAmpersand @ PairUnit.GetName(eNameType_RankFull);
			}
			else
			{
				UnitName = Unit.GetFullName();
				// A ghost (a Gremlin standing in for an engineer) is renamed by its slot's
				// template, exactly as the row does it — otherwise two entries read alike.
				if (Info.bGhostUnit && Unit.GetStaffSlot() != none)
					UnitName = Repl(Unit.GetStaffSlot().GetMyTemplate().GhostName, "%UNITNAME", UnitName);
			}
		}

		Rec = "kind=candidate";
		Rec $= FLD_SEP $ "index=" $ string(i);
		Rec $= FLD_SEP $ "name=" $ UnitName;
		Rec $= FLD_SEP $ "status=" $ UnitStatus;
		Push(Rec);
	}
}

// ---------------------------------------------------------------------------------------
// Wire
// ---------------------------------------------------------------------------------------

private function Push(string Rec)
{
	if (NativeResult != "")
		NativeResult = NativeResult $ REC_SEP;
	NativeResult = NativeResult $ Rec;
}
