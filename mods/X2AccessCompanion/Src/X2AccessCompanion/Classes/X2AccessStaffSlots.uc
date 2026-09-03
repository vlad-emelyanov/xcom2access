// X2AccessStaffSlots
// The staff strip's words — one implementation for the TWO screens that draw one. A built
// facility (UIFacility) staffs over its own StaffSlots and a debris room or construction site
// (UIRoom) over the room's BuildSlots, but the widgets are one class (UIStaffSlot in a
// UIStaffContainer), the picker is one class (UIPersonnel_DropDown on that container), and the
// state object under both is XComGameState_StaffSlot. So the records are emitted here and both
// readers call in, which is what keeps the two rooms describing a slot in the same words.
//
// ⛔ Two copies of this was the bug. X2AccessRoomReader had a working slot-and-picker readout
// for the debris room; X2AccessFacilityReader had none, and native built no slot nodes for a
// built room — so an engineer could be assigned to CLEAR a room by keyboard and then could not
// be assigned to the Power Relay built in it (reported 2026-09-03).
//
// WHY THE COMPANION HAS TO SUPPLY THESE AT ALL. UIStaffSlot.UpdateData resolves the occupant's
// name and the bonus and pushes them through MC.BeginFunctionOp("update"), keeping only
// StaffSlotRef; UIPersonnel_DropDownListItem.UpdateData calls AS_UpdateData directly, bypassing
// UIButton.SetText, so even its retained Text is empty. The widget tree holds structure and
// nothing else. Every string below is re-derived exactly the way the widget that hid it derived
// it — GetNameDisplayString / GetBonusDisplayString for a slot, the drop-down row's own name and
// status logic for a candidate — and the hover tooltip a sighted player gets on a facility slot
// is re-run through the same branches as UIFacility_StaffSlot.RefreshTooltip.
//
// Static over `out string`, the X2AccessWire pattern: storage stays on the reader that native
// called, because native reads NativeResult off that CDO.

// dependson: StaffUnitInfo and EStaffStatus are both declared INSIDE
// X2StrategyGameRulesetDataStructures, not in a global header, so a class in another package
// needs it in scope by name — the same reason X2AccessAvengerReader dependson(UIStaffIcon).
class X2AccessStaffSlots extends Object abstract
	dependson(X2StrategyGameRulesetDataStructures);

// A local copy of the wire grammar's field separator — an UnrealScript const cannot alias
// another class's, so this must match X2AccessWire's character for character.
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------------------

// One record per slot widget the container SPAWNED, in container order:
//   kind=slot  index  label  bonus  filled  locked  available  [hint]
//
// ⛔ `index` is the CONTAINER's index, which is what native matches against the widget array it
// read, and it is not the facility's slot index: UIFacilityStaffContainer.Refresh skips a slot
// that is locked with no upgrade available and one whose template hides it. The widget's own
// StaffSlotRef is the only link to the state object, and it is the one used.
//
// `bFirstOnly` mirrors UIRoomStaffContainer.Refresh, which shows exactly one slot while a room
// is under construction; a facility never sets it.
static function AppendSlots(UIStaffContainer Container, bool bFirstOnly, out string Result)
{
	local XComGameState_StaffSlot SlotState;
	local UIStaffSlot Slot;
	local string Rec;
	local int i;

	if (Container == none)
		return;

	for (i = 0; i < Container.StaffSlots.Length; i++)
	{
		Slot = Container.StaffSlots[i];
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
		// language for free. Emitted even when empty, so a blank never reads as "no label".
		Rec $= FLD_SEP $ "label=" $ SlotState.GetNameDisplayString();
		// The widget Caps() this on the way into Flash; that is a visual style, not information.
		Rec $= FLD_SEP $ "bonus=" $ SlotState.GetBonusDisplayString();
		Rec $= FLD_SEP $ "filled=" $ (SlotState.IsSlotFilled() ? "1" : "0");
		Rec $= FLD_SEP $ "locked=" $ (SlotState.IsLocked() ? "1" : "0");
		// The disabled state the widget itself computes (UIStaffSlot.UpdateData → SetDisabled):
		// no unit of the right kind is free, so clicking opens an empty picker.
		Rec $= FLD_SEP $ "available=" $ (SlotState.IsUnitAvailableForThisSlot() ? "1" : "0");
		// The hover tooltip, when the slot class has one (facility slots do, build slots do
		// not). Dropped when empty; native falls back to its own wording only for the
		// no-one-available case, which is the one a build slot can be in.
		class'X2AccessWire'.static.AddField(Rec, "hint", SlotHint(Slot, SlotState));
		class'X2AccessWire'.static.AddRecord(Result, Rec);

		if (bFirstOnly)
			break;
	}
}

// UIFacility_StaffSlot.RefreshTooltip and UIFacility_BondSlot.RefreshTooltip, branch for
// branch — the words a sighted player gets by hovering the slot: why it is padlocked, why it is
// greyed, or what filling it would do. Re-run here rather than read off the tooltip manager,
// because the game only writes that text on MOUSE-IN (SetMouseDelegates → RefreshTooltip), so
// the stored tooltip of a slot nobody has hovered is the class default, whatever the slot's
// state.
//
// ⛔ The bond slot is tested FIRST: it extends UIFacility_StaffSlot and overrides the whole
// function, and its m_strNoSoldiersTooltip is a different sentence under [UIFacility_BondSlot].
static function string SlotHint(UIStaffSlot Slot, XComGameState_StaffSlot SlotState)
{
	local UIFacility_StaffSlot FacilitySlot;
	local UIFacility_BondSlot BondSlot;

	BondSlot = UIFacility_BondSlot(Slot);
	if (BondSlot != none)
	{
		if (SlotState.IsUnitAvailableForThisSlot())
			return SlotState.IsSlotEmpty() ? BondSlot.m_strBondsAvailableTooltip : BondSlot.m_strCurrentlyBondingTooltip;
		return BondSlot.m_strNoSoldiersTooltip;
	}

	// UIRoom_StaffSlot and UICovertActionStaffSlot declare no tooltip strings of their own.
	FacilitySlot = UIFacility_StaffSlot(Slot);
	if (FacilitySlot == none)
		return "";

	if (SlotState.IsLocked())
		return FacilitySlot.m_strSlotLockedTooltip;
	if (SlotState.IsSoldierSlot())
		return SlotState.IsUnitAvailableForThisSlot() ? FacilitySlot.m_strSoldiersAvailableTooltip : FacilitySlot.m_strNoSoldiersTooltip;
	if (SlotState.IsEngineerSlot())
		return SlotState.IsUnitAvailableForThisSlot() ? FacilitySlot.m_strEngineersAvailableTooltip : FacilitySlot.m_strNoEngineersTooltip;
	if (SlotState.IsScientistSlot())
		return SlotState.IsUnitAvailableForThisSlot() ? FacilitySlot.m_strScientistsAvailableTooltip : FacilitySlot.m_strNoScientistsTooltip;
	return "";
}

// ---------------------------------------------------------------------------------------
// The picker
// ---------------------------------------------------------------------------------------

// One record per row of the personnel picker, only while it is OPEN:
//   kind=candidate  index  name  status
//
// Emitted LAST by both readers, and it is what tells native the picker is up: while it is, the
// room's own stops are not reachable anyway (the game routes every key into the list), so the
// recipe swaps to a candidate-only graph rather than showing both.
//
// Mirrors UIPersonnel_DropDownListItem.UpdateData: the same name (rank-qualified for soldiers,
// ghost-renamed for a gremlin), the same localized status, and the same m_strEmptyStaff for the
// "take them out" row the drop-down puts first when the slot is already filled.
static function AppendCandidates(UIStaffContainer Container, out string Result)
{
	local UIPersonnel_DropDown Drop;
	local XComGameState_Unit Unit, PairUnit;
	local StaffUnitInfo Info;
	local EStaffStatus StafferStatus;
	local string Rec, UnitName, UnitStatus;
	local int i;

	if (Container == none)
		return;

	Drop = Container.m_kPersonnelDropDown;
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
		class'X2AccessWire'.static.AddRecord(Result, Rec);
	}
}
