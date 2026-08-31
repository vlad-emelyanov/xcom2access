// X2AccessCovertActionReader
// The covert-actions picker (UICovertActions) — the words half, which here is almost the whole
// screen. Same contract as X2AccessBuildFacilityReader: the C++ layer owns the gate, the keys and
// the speech, calls DescribeCovertActions() on this class's default object via ProcessEvent from
// the graph recipe's build, and reads NativeResult back through reflection right after the call
// returns. No listeners, no timers, no polling. docs/covert-actions-recon.md is the survey.
//
// WHAT NATIVE ALREADY HAS, and why this file does not repeat it: the action ROWS. They are
// ordinary UIListItemStrings and InitListItem goes through SetText, so their words really are
// retained — along with bDisabled and, uniquely useful, `metadataInt = ActionState.ObjectID`
// (UICovertActions.uc:368), written unconditionally for every action row. That is a REAL join
// key, so `kind=row` below carries `action=` and nothing about position.
//
// WHAT IS LEFT IS EVERYTHING ELSE, and none of it is readable. The detail card, the faction
// panel, the risk rows and the four slots are all `MC.BeginFunctionOp` pipes whose arguments are
// LOCALS on both sides — and the slots are not even widgets: `UICovertActionSlot extends Object`,
// so they never appear in the panel tree at all. What a sighted player is reading exists nowhere
// a widget read can find it.
//
// ⛔ EVERY VALUE COMES FROM THE SCREEN'S OWN FUNCTIONS, not from a reimplementation.
// GetActionName / GetActionDescription / GetWorldLocation / GetDurationString / GetRewardString /
// GetRewardDetailsString / GetActionRisks / GetFactionTitle / IsInfluenceMet /
// IsCovertActionInProgress / IsActionInfluenceMet are all public on UICovertActions and are the
// exact calls RefreshMainPanel, RefreshFactionPanel and RefreshRisksPanel make. The screen already
// resolved them once; asking IT again is the only way the panel and the speech cannot disagree.
// Not one `XCOMHISTORY` lookup is needed for the card.
//
// ⛔ THE SLOTS ARE ENUMERATED, NOT RE-FILTERED. UICovertActionSlotContainer.Refresh decides which
// staff and cost slots to show (three separate conditions, two of them keyed on a tutorial
// objective) and then BUILDS one UICovertActionSlot per survivor. Re-deriving that filter here
// would be a second opinion about which slots exist; walking `SlotContainer.ActionSlots` is the
// screen's own answer, and the array index IS the MCIndex, which is what the verbs below take.
// The per-slot STRINGS still have to be recomputed — Update() passes them straight through to
// Flash on this screen — but every input to that recomputation is a ref the slot object retained.
//
// ⛔ A SLOT'S GATE IS `IsDisabled`, WHICH THE GAME SETS AND WE DO NOT SECOND-GUESS.
// UICovertActionStaffSlot.HandleClick only tests `!GetAction().bStarted`, so with ANOTHER action
// in progress it would happily open the personnel picker for an action that cannot be run —
// vanilla is saved from that by `!bActionInProgress` in OnUnrealCommand and by DisableMouseHit().
// UpdateData records the same decision on the slot as `IsDisabled` (DisableAllSlots/EnableAllSlots),
// so the verbs test THAT rather than re-deriving the condition. One gate, the game's own.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessCovertActionReader extends Object;

// Written by the entry points; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the screen's own reading order:
//
//   kind=screen   header  listheader
//   kind=row      action=<ObjectID>  head  reason
//   kind=card     name  narrative  loclabel  location  durlabel  duration
//                 rewardlabel  reward  rewarddetails  locklabel  lockreason
//   kind=faction  title  name  inflabel  influence
//   kind=risk     index=<i>  label  level
//   kind=ambush   text
//   kind=slot     index=<i>  slot=staff|cost  req  fame  label  reward  unavailable
//                 button  button2  state=normal|good|disabled
//   kind=confirm  label
//
// `head` is the group HEADING the game would draw above that row, empty when it draws none — a
// mirror of BuildScreen's own loop rather than a re-derivation of it, because that loop's
// LastFactionName bookkeeping has a wrinkle (see BuildRowRecords). Every field is NAMED, so an
// empty one is simply omitted; nothing here is positional except the repeating `index=`, which is
// always emitted.
function DescribeCovertActions()
{
	local UICovertActions Screen;

	NativeResult = "";

	// The same screen native resolved: topmost, matching GetCurrentScreen.
	Screen = UICovertActions(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	// ⛔ All three are CLASS DEFAULTS of the screen, so they belong on the screen's own record and
	// are never asked of a row — the rule an empty-dropping wire format turns on. `risktitle` is
	// here rather than beside the risks for the same reason: RefreshRisksPanel decides whether to
	// DRAW it from DoesActionHaveRisks(), which native can see for itself from whether any
	// kind=risk record arrived.
	Emit("kind=screen"
		$ FLD_SEP $ "header=" $ Screen.CovertActions_ScreenHeader
		$ FLD_SEP $ "listheader=" $ Screen.CovertActions_ListHeader
		$ FLD_SEP $ "risktitle=" $ Screen.CovertActions_RiskTitle);

	BuildRowRecords(Screen);

	// Everything below describes the SELECTED action, and GetAction() answers none before the
	// first UpdateData — the screen builds its list before it has an ActionRef.
	if (Screen.GetAction() == none)
		return;

	BuildCardRecord(Screen);
	BuildFactionRecord(Screen);
	BuildRiskRecords(Screen);
	BuildSlotRecords(Screen);

	Emit("kind=confirm" $ FLD_SEP $ "label=" $ Screen.CovertActions_LaunchAction);
}

// ---------------------------------------------------------------------------------------
// The list
// ---------------------------------------------------------------------------------------

// One record per action, carrying the two things the retained row Text cannot: which group
// heading stands above it, and why it is greyed.
//
// ⛔ The heading logic is BuildScreen's, copied rather than tidied. Note what it actually does:
// `LastFactionName` is assigned only in the ELSE branch, so when row 0 is a started action the
// "IN PROGRESS" heading is drawn and LastFactionName stays '' — which makes row 1 draw its
// faction's heading even though row 0 belonged to the same faction. That is what a sighted player
// sees, so it is what we say.
private function BuildRowRecords(UICovertActions Screen)
{
	local XComGameState_CovertAction ActionState;
	local name LastFactionName;
	local string Rec, Head, Reason;
	local int idx;

	for (idx = 0; idx < Screen.arrActions.Length; idx++)
	{
		ActionState = Screen.arrActions[idx];
		if (ActionState == none)
			continue;

		Head = "";
		if (ActionState.GetFaction().GetMyTemplateName() != LastFactionName)
		{
			if (idx == 0 && ActionState.bStarted)
			{
				Head = Screen.CovertActions_CurrentActiveHeader;
			}
			else
			{
				LastFactionName = ActionState.GetFaction().GetMyTemplateName();
				Head = Caps(ActionState.GetFaction().GetFactionTitle());
			}
		}

		// The same pair BuildScreen hands SetDisabled, in the same order — the tooltip a sighted
		// player gets on hover, which is the only statement of WHY a row is refused.
		Reason = "";
		if (Screen.IsCovertActionInProgress() && !ActionState.bStarted)
			Reason = Screen.CovertActions_ActionInProgressTooltip;
		else if (!Screen.IsActionInfluenceMet(idx))
			Reason = Screen.CovertActions_InfluenceRequiredTooltip;

		Rec = "kind=row" $ FLD_SEP $ "action=" $ string(ActionState.ObjectID);
		if (Head != "")
			Rec $= FLD_SEP $ "head=" $ Head;
		if (Reason != "")
			Rec $= FLD_SEP $ "reason=" $ Reason;
		Emit(Rec);
	}
}

// ---------------------------------------------------------------------------------------
// The detail card
// ---------------------------------------------------------------------------------------

// RefreshMainPanel's own calls, in the order AS_SetInfoData / AS_UpdateCost / AS_SetLockData
// receive them. The labels are the screen's own localized headers rather than words of ours.
private function BuildCardRecord(UICovertActions Screen)
{
	local XComGameState_CovertAction ActionState;
	local string Rec, Location;

	ActionState = Screen.GetAction();

	Rec = "kind=card";
	Rec $= FLD_SEP $ "name=" $ Screen.GetActionName();
	Rec $= FLD_SEP $ "narrative=" $ Screen.GetActionDescription();

	// ⛔ The location header is drawn only when there IS a location, because AS_SetInfoData passes
	// "" for the header whenever WorldLocation is empty — and it is passed "" for BOTH whenever
	// the action has no rewards. Both branches reproduced, or the card grows a label with nothing
	// under it on an action the game shows bare.
	if (Screen.DoesActionHaveRewards())
	{
		Rec $= FLD_SEP $ "rewardlabel=" $ Screen.CovertActions_RewardHeader;
		Rec $= FLD_SEP $ "reward=" $ Screen.GetRewardString();
		Rec $= FLD_SEP $ "rewarddetails=" $ Screen.GetRewardDetailsString();

		Location = Screen.GetWorldLocation();
		if (Location != "")
		{
			Rec $= FLD_SEP $ "loclabel=" $ Screen.CovertActions_LocationHeader;
			Rec $= FLD_SEP $ "location=" $ Caps(Location);
		}
	}

	// AS_UpdateCost blanks both halves when bShowCosts is off, and swaps the label once the action
	// is running.
	if (Screen.bShowCosts)
	{
		Rec $= FLD_SEP $ "durlabel="
			$ (ActionState.bStarted ? Screen.CovertActions_TimeRemaining
									: Screen.CovertActions_Duration);
		Rec $= FLD_SEP $ "duration=" $ Screen.GetDurationString();
	}

	// The lock overlay, which is the SAME refusal the row carried but stated over the card the
	// player is now reading. Native decides whether to say it twice.
	if (Screen.IsCovertActionInProgress() && !ActionState.bStarted)
	{
		Rec $= FLD_SEP $ "locklabel=" $ Screen.CovertActions_Unavailable;
		Rec $= FLD_SEP $ "lockreason=" $ Screen.CovertActions_ActionInProgressTooltip;
	}
	else if (!Screen.IsInfluenceMet())
	{
		Rec $= FLD_SEP $ "locklabel=" $ Screen.CovertActions_Unavailable;
		Rec $= FLD_SEP $ "lockreason=" $ Screen.CovertActions_InfluenceRequiredTooltip;
	}

	Emit(Rec);
}

// RefreshFactionPanel's four strings. The subtitle arrives HTML-coloured in vanilla; the colour
// is native's CleanText problem, not ours, so the plain value goes on the wire.
private function BuildFactionRecord(UICovertActions Screen)
{
	Emit("kind=faction"
		$ FLD_SEP $ "title=" $ Caps(Screen.GetFactionTitle())
		$ FLD_SEP $ "name=" $ Caps(Screen.GetFactionName())
		$ FLD_SEP $ "inflabel=" $ Screen.CovertActions_InfluenceLabel
		$ FLD_SEP $ "influence=" $ Caps(Screen.GetAction().GetFaction().GetInfluenceString()));
}

// ---------------------------------------------------------------------------------------
// Risks
// ---------------------------------------------------------------------------------------

// ⛔ ALL of them, which is deliberately MORE than the screen draws. RefreshRisksPanel bounds its
// loop by `arrActions.Length` where it means `Labels.Length` (UICovertActions.uc:524) — the number
// of ACTIONS on offer, not of risks — so with fewer actions listed than the selected action has
// risks, the extra rows are blanked and hidden from a sighted player too. The Highlander does not
// fix it. This goes the opposite way to the WOTC initiative order (which we deliberately withhold)
// because the direction of the error is opposite: that overlay is cheat-gated ON PURPOSE, this is
// a typo losing information the design means to show. docs/covert-actions-recon.md §6.1.
//
// The tutorial gate IS reproduced: before the first covert action is completed the game shows no
// risks at all, and that is a design statement rather than a slip.
private function BuildRiskRecords(UICovertActions Screen)
{
	local array<string> Labels, Values;
	local int idx;

	if (!class'XComGameState_HeadquartersXCom'.static.IsObjectiveCompleted('XP2_M0_FirstCovertActionTutorial'))
		return;

	Screen.GetActionRisks(Labels, Values);

	for (idx = 0; idx < Labels.Length; idx++)
	{
		// Values carries the difficulty BUCKET (Low / Moderate / High), HTML-coloured. The raw
		// ChanceToOccur behind it is never shown to anyone and is not ours to leak.
		Emit("kind=risk"
			$ FLD_SEP $ "index=" $ string(idx)
			$ FLD_SEP $ "label=" $ Caps(Labels[idx])
			$ FLD_SEP $ "level=" $ Caps(Values[idx]));
	}

	if (Screen.HasAmbushRisk())
		Emit("kind=ambush" $ FLD_SEP $ "text=" $ Screen.CovertActions_WarningAmbush);
}

// ---------------------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------------------

// One record per slot the container BUILT, in its own order — staff slots first, then cost slots,
// which is the order Refresh creates them and therefore the order the four cards are drawn in.
private function BuildSlotRecords(UICovertActions Screen)
{
	local UICovertActionStaffSlot StaffSlot;
	local UICovertActionCostSlot CostSlot;
	local int i;

	if (Screen.SlotContainer == none)
		return;

	for (i = 0; i < Screen.SlotContainer.ActionSlots.Length; i++)
	{
		StaffSlot = UICovertActionStaffSlot(Screen.SlotContainer.ActionSlots[i]);
		if (StaffSlot != none)
		{
			Emit(StaffSlotRecord(Screen, StaffSlot, i));
			continue;
		}

		CostSlot = UICovertActionCostSlot(Screen.SlotContainer.ActionSlots[i]);
		if (CostSlot != none)
			Emit(CostSlotRecord(Screen, CostSlot, i));
	}
}

// UICovertActionStaffSlot.UpdateData, minus the portrait work. The parts stay SEPARATE fields
// rather than the single Label/Value pair the game composes: it glues the required/famous
// prefixes with `@` and the no-unit warning with a newline, and a part that CleanText later
// reduces to nothing would leave its separator behind — the join belongs to native's AppendPhrase.
private function string StaffSlotRecord(UICovertActions Screen, UICovertActionStaffSlot Slot, int Index)
{
	local XComGameStateHistory History;
	local XComGameState_CovertAction ActionState;
	local XComGameState_StaffSlot SlotState;
	local XComGameState_Reward RewardState;
	local XComGameState_Unit Unit;
	local string Rec, Reward;

	History = `XCOMHISTORY;
	ActionState = Screen.GetAction();
	SlotState = XComGameState_StaffSlot(History.GetGameStateForObjectID(Slot.StaffSlotRef.ObjectID));
	RewardState = XComGameState_Reward(History.GetGameStateForObjectID(Slot.RewardRef.ObjectID));

	Rec = "kind=slot" $ FLD_SEP $ "index=" $ string(Index) $ FLD_SEP $ "slot=staff";

	if (SlotState == none)
		return Rec; // the slot exists but its state does not — say the card, not a wrong word

	// The per-slot bonus, which is the whole reason optional slots are worth filling.
	// Reward_DecreaseRisk states itself and takes no "Soldier Reward:" prefix, exactly as
	// UpdateData has it.
	if (RewardState != none && !ActionState.bCompleted)
	{
		Reward = RewardState.GetRewardPreviewString();
		if (Reward != "" && RewardState.GetMyTemplateName() != 'Reward_DecreaseRisk')
			Reward = class'UICovertActionStaffSlot'.default.m_strSoldierReward @ Reward;
		if (Reward != "")
			Rec $= FLD_SEP $ "reward=" $ Reward;
	}

	// Filled: the label IS the soldier, rank and all (GetNameDisplayStringDefault returns
	// GetName(eNameType_RankFull)), so no separate unit field is wanted.
	Rec $= FLD_SEP $ "label=" $ SlotState.GetNameDisplayString();

	if (!SlotState.IsSlotFilled())
	{
		if (Slot.bOptional)
			Rec $= FLD_SEP $ "req=" $ class'UICovertActionStaffSlot'.default.m_strOptionalSlot;
		else
			Rec $= FLD_SEP $ "req=" $ class'UICovertActionStaffSlot'.default.m_strRequiredSlot;

		if (Slot.bFame)
			Rec $= FLD_SEP $ "fame=" $ class'UICovertActionStaffSlot'.default.m_strFamous;

		if (SlotState.IsEngineerSlot())
			Rec $= FLD_SEP $ "button=" $ class'UICovertActionStaffSlot'.default.m_strAddEngineer;
		else if (SlotState.IsScientistSlot())
			Rec $= FLD_SEP $ "button=" $ class'UICovertActionStaffSlot'.default.m_strAddScientist;
		else
			Rec $= FLD_SEP $ "button=" $ class'UICovertActionStaffSlot'.default.m_strAddSoldier;

		if (!SlotState.IsUnitAvailableForThisSlot())
		{
			Rec $= FLD_SEP $ "unavailable="
				$ class'UICovertActionStaffSlot'.default.m_strNoUnitAvailable;
			Rec $= FLD_SEP $ "state=disabled";
		}
		else
		{
			Rec $= FLD_SEP $ "state=normal";
		}

		return Rec;
	}

	Rec $= FLD_SEP $ "state=good";

	if (!ActionState.bCompleted && !ActionState.bStarted)
		Rec $= FLD_SEP $ "button=" $ class'UICovertActionStaffSlot'.default.m_strClearSoldier;

	// ⛔ EDIT is gamepad-only in vanilla (FXS_BUTTON_Y), so this label is the ONLY notice a
	// keyboard player gets that the verb exists at all — and it exists exactly when the action can
	// be ambushed, i.e. when the soldier's loadout is about to matter.
	Unit = XComGameState_Unit(History.GetGameStateForObjectID(SlotState.GetAssignedStaffRef().ObjectID));
	if (Unit != none && Unit.IsSoldier() && ActionState.HasAmbushRisk() && !ActionState.bStarted)
		Rec $= FLD_SEP $ "button2=" $ class'UICovertActionStaffSlot'.default.m_strEditLoadout;

	return Rec;
}

// UICovertActionCostSlot.UpdateData. The label is the COST ("50 Intel") and the reward is what
// paying it buys — the game colours the latter by affordability, which is the only signal that a
// cost is out of reach until the explicit warning below fires.
private function string CostSlotRecord(UICovertActions Screen, UICovertActionCostSlot Slot, int Index)
{
	local XComGameState_CovertAction ActionState;
	local XComGameState_Reward RewardState;
	local XComGameState_HeadquartersXCom XComHQ;
	local array<StrategyCostScalar> CostScalars;
	local string Rec;

	ActionState = Screen.GetAction();
	RewardState = XComGameState_Reward(`XCOMHISTORY.GetGameStateForObjectID(Slot.RewardRef.ObjectID));
	XComHQ = class'UIUtilities_Strategy'.static.GetXComHQ();
	CostScalars.Length = 0;

	Rec = "kind=slot" $ FLD_SEP $ "index=" $ string(Index) $ FLD_SEP $ "slot=cost";
	Rec $= FLD_SEP $ "label="
		$ class'UIUtilities_Strategy'.static.GetStrategyCostString(Slot.Cost, CostScalars);

	if (RewardState != none)
	{
		Rec $= FLD_SEP $ "reward="
			$ (ActionState.bCompleted ? RewardState.GetRewardString()
									  : RewardState.GetRewardPreviewString());
	}

	if (!Slot.IsCostPurchased())
	{
		if (!ActionState.bStarted)
		{
			Rec $= FLD_SEP $ "button=" $ class'UICovertActionCostSlot'.default.m_strPayCost;
			if (!XComHQ.CanAffordAllStrategyCosts(Slot.Cost, CostScalars))
			{
				Rec $= FLD_SEP $ "unavailable="
					$ class'UICovertActionCostSlot'.default.m_strNoResourcesAvailable;
				Rec $= FLD_SEP $ "state=disabled";
				return Rec;
			}
		}
		else
		{
			Rec $= FLD_SEP $ "state=disabled";
			return Rec;
		}

		Rec $= FLD_SEP $ "state=normal";
		return Rec;
	}

	Rec $= FLD_SEP $ "state=good";
	if (!ActionState.bStarted)
		Rec $= FLD_SEP $ "button=" $ class'UICovertActionCostSlot'.default.m_strClearCost;

	return Rec;
}

// ---------------------------------------------------------------------------------------
// Verbs — the two the keyboard cannot reach
// ---------------------------------------------------------------------------------------

// The slot's primary button: add or clear a soldier, allocate or refund a cost. This is the same
// call UICovertActions.OnUnrealCommand makes for Enter once it is in slot mode, and the same one
// OnMouseEvent makes for a click — so every side effect the game expects still happens, including
// the personnel picker it pushes and the sounds it plays.
function ClickCovertActionSlot(int Index)
{
	local UICovertActionSlot Slot;

	Slot = ResolveSlot(Index);
	if (Slot != none)
		Slot.HandleClick("theButton");
}

// The slot's second button: open the armory loadout for the soldier standing in it. Bound to
// FXS_BUTTON_Y in vanilla and therefore unreachable without a gamepad.
function EditCovertActionLoadout(int Index)
{
	local UICovertActionSlot Slot;

	Slot = ResolveSlot(Index);
	if (Slot != none && Slot.CanEditLoadout())
		Slot.HandleClick("theButton2");
}

// ⛔ IsDisabled is the gate, and it is the GAME's own record of the decision — DisableAllSlots
// sets it from the same UpdateData branch that hides the launch button. Testing it here rather
// than re-deriving "is another action running" keeps this to one gate; see the header for why
// HandleClick's own `!bStarted` check is not enough on its own.
private function UICovertActionSlot ResolveSlot(int Index)
{
	local UICovertActions Screen;

	Screen = UICovertActions(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.SlotContainer == none)
		return none;
	if (Index < 0 || Index >= Screen.SlotContainer.ActionSlots.Length)
		return none;
	if (Screen.SlotContainer.ActionSlots[Index].IsDisabled)
		return none;

	return Screen.SlotContainer.ActionSlots[Index];
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
