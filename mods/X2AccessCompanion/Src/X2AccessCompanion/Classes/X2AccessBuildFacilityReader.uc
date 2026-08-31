// X2AccessBuildFacilityReader
// The choose-a-facility screen (UIChooseFacility) — the words half of its DETAIL PANEL. Same
// contract as X2AccessCommodityReader: the C++ layer owns the gate, the keys and the speech,
// calls DescribeFacilityChoice() on this class's default object via ProcessEvent from the graph
// recipe's build, and reads NativeResult back through reflection right after the call returns.
//
// WHY ONLY HALF. This screen's two halves fail in opposite directions. The LIST is the good case:
// PopulateList calls UIListItemString.SetText, so the facility names really are retained and
// native reads them straight off the widgets, along with bDisabled and the attention icon. The
// PANEL beside it is the bad case, and it is the half that matters:
//
//   UpdateSelection(list, itemIndex)
//       ... Requirements, Summary, BuildTime, UpkeepCostStr, InsufficientResourcesWarning ...
//       AS_SetDescription(Summary, warning);        // -> MC.BeginFunctionOp, retained NOWHERE
//       AS_SetResources(Requirements, BuildTime);   // -> same
//
// Cost, power, strategy requirements, build time, upkeep and the description — every number that
// decides whether to spend a month's supplies on this room — are locals on their way into Flash.
// The UIItemCard problem again, on the screen where it costs the most.
//
// ⛔ EVERY VALUE IS RE-DERIVED THROUGH THE SCREEN'S OWN FUNCTIONS, not through a reimplementation:
// GetStrategyCostString with the screen's own discount, GetPowerRequirement (which is the one
// that knows a shielded power coil doubles a generator and zeroes everything else),
// GetStrategyReqString, GetBuildTime, CanBuildFacility, HasEnoughPower. The screen already
// resolved them once; asking it again is the only way the panel and the speech cannot disagree.
//
// ⛔ It reads m_selectedFacility rather than re-indexing m_arrFacilities: that field IS what
// UpdateSelection set, and it is what OnAccept will build. Re-deriving the index would introduce
// a second opinion about which row is current, which is the exact bug class this file exists to
// avoid.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessBuildFacilityReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Two records:
//   kind=header  title  subtitle
//   kind=card    title  cost  power  reqs  timeLabel  time  upkeep  warning  desc
//
// Fields the screen is not drawing are omitted, and native drops a missing line rather than
// giving it a stop — a card node that reads as nothing is worse than one that is not there.
function DescribeFacilityChoice()
{
	local UIChooseFacility Screen;
	local X2FacilityTemplate Facility;
	local XComGameState_HeadquartersRoom RoomState;
	local XComGameState_HeadquartersXCom XComHQ;
	local string Rec, Reqs, Warning, Upkeep;
	local int Power;

	NativeResult = "";

	Screen = UIChooseFacility(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	Facility = Screen.m_selectedFacility;
	if (Facility == none)
		return;

	XComHQ = class'UIUtilities_Strategy'.static.GetXComHQ();
	RoomState = XComGameState_HeadquartersRoom(
		`XCOMHISTORY.GetGameStateForObjectID(Screen.m_RoomRef.ObjectID));

	// The screen's own two headings. Both are plain localized properties rather than anything the
	// panel resolved, so they are the one part of this screen that never goes stale.
	Emit("kind=header" $ FLD_SEP $ "title=" $ Screen.m_strTitle
		$ FLD_SEP $ "subtitle=" $ Screen.m_strListLabel);

	Rec = "kind=card";
	Rec $= FLD_SEP $ "title=" $ Facility.DisplayName;

	// The supply/item cost, with the screen's own pending discount applied — UpdateSelection's
	// first line, verbatim.
	Rec $= FLD_SEP $ "cost=" $ class'UIUtilities_Strategy'.static.GetStrategyCostString(
		Facility.Cost, XComHQ.FacilityBuildCostScalars,
		XComHQ.GetPendingFacilityDiscountPercent(Facility.DataName));

	// POWER is drawn as an icon and a bare number, with the SIGN carrying the meaning: a positive
	// figure is power produced (a generator, or anything on a shielded coil), a negative one is
	// power consumed, and the panel shows the absolute value either way. Speech gets the word the
	// icon was standing in for.
	// ⛔ int(Abs(...)), not Abs(...). UnrealScript's Abs is FLOAT, so string() on its result
	// prints four decimals — live 2026-08-21 this read "ЭНЕРГИЯ: -3.0000". The game's own line
	// wraps it the same way (string(int(Abs(power)))); dropping the cast was the whole bug.
	Power = Screen.GetPowerRequirement(Facility);
	if (Power >= 0)
		Rec $= FLD_SEP $ "power=" $ Screen.m_strPowerLabel @ "+" $ string(Power);
	else
		Rec $= FLD_SEP $ "power=" $ Screen.m_strPowerLabel @ "-" $ string(int(Abs(Power)));

	Reqs = class'UIUtilities_Strategy'.static.GetStrategyReqString(Facility.Requirements);
	if (Reqs != "")
		Rec $= FLD_SEP $ "reqs=" $ Reqs;

	Rec $= FLD_SEP $ "timeLabel=" $ Screen.m_strTimeLabel;
	Rec $= FLD_SEP $ "time=" $ Screen.GetBuildTime(Facility);

	if (Facility.UpkeepCost > 0)
	{
		Upkeep = Screen.m_strUpkeepCostLabel
			@ class'UIUtilities_Strategy'.default.m_strCreditsPrefix $ string(Facility.UpkeepCost);
		Rec $= FLD_SEP $ "upkeep=" $ Upkeep;
	}

	// UpdateSelection's own branch, and the ORDER matters: the no-power warning is a refusal, the
	// power-coil lines are a bonus, and the game only ever shows one of them.
	if (!Screen.CanBuildFacility(Facility))
	{
		if (!Screen.HasEnoughPower(Facility))
			Warning = Screen.m_strInsufficientPowerWarning;
		else if (RoomState != none && RoomState.HasShieldedPowerCoil())
			Warning = (Power > 0) ? Screen.m_strPowerCoilBenefitDouble
								  : Screen.m_strPowerCoilBenefitNoPower;
	}
	if (Warning != "")
		Rec $= FLD_SEP $ "warning=" $ Warning;

	if (Facility.Summary != "")
		Rec $= FLD_SEP $ "desc=" $ Facility.Summary;

	Emit(Rec);
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
