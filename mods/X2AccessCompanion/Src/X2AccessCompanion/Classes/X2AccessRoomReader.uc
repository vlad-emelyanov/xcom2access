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
// widget that hid it derived it. That keeps one source of truth for words a sighted player is
// reading off the same pixels. The slot and picker records come from X2AccessStaffSlots, which
// serves the built room's strip as well — one implementation, so both rooms describe a slot in
// the same words.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessRoomReader extends Object;

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

	RoomState = Room.GetRoom();
	class'X2AccessStaffSlots'.static.AppendSlots(Room.m_kStaffSlotContainer,
		RoomState != none && RoomState.UnderConstruction, NativeResult);
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

// The personnel picker, when it is open — the shared reader, see X2AccessStaffSlots.
private function AppendCandidates(UIRoom Room)
{
	class'X2AccessStaffSlots'.static.AppendCandidates(Room.m_kStaffSlotContainer, NativeResult);
}

// ---------------------------------------------------------------------------------------
// Wire
// ---------------------------------------------------------------------------------------

private function Push(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}
