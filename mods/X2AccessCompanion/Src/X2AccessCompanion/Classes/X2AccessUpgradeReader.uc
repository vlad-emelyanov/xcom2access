// X2AccessUpgradeReader
// The choose-a-facility-upgrade screen (UIChooseUpgrade) — the words half of its DETAIL PANEL.
// Same contract as X2AccessBuildFacilityReader, of which this is the twin: the C++ layer owns
// the gate, the keys and the speech, calls DescribeUpgradeChoice() on this class's default
// object via ProcessEvent from the graph recipe's build, and reads NativeResult back through
// reflection right after the call returns.
//
// ⛔ THE SCREEN IS UIChooseUpgrade, NOT UIFacilityUpgrade. Every path the game has to a
// facility's upgrades — the room's UPGRADE button (UIFacility.UpgradeFacility), a padlocked
// staff slot (UIStaffSlot.ShowUpgradeFacility), the to-do widget, the ant farm overlay — calls
// XComHQPresentationLayer.UIFacilityUpgrade(), and that function spawns a UIChooseUpgrade
// (XComHQPresentationLayer.uc:1994). UIFacilityUpgrade ships in the package with a list of
// UIFacilityUpgrade_ListItems whose UIText IS retained, which is what made the census file it
// as "natively readable" — and it is never instantiated.
//
// WHY ONLY HALF, again. The LIST is the good case: PopulateList calls UIListItemString.SetText,
// so the upgrade names are retained and native reads them off the rows, with bDisabled
// (UpgradeAlreadyPerformed || !HasEnoughPower || !CanBuildUpgrade) and the attention icon
// (bPriority). The PANEL is the bad case:
//
//   UpdateSelection(list, itemIndex)
//       ... Requirements, Summary, UpkeepCostStr, InsufficientResourcesWarning ...
//       AS_SetDescription(Summary, warning);      // -> MC.BeginFunctionOp, retained NOWHERE
//       AS_SetResources(Requirements, "");        // -> same
//
// ⛔ EVERY VALUE IS RE-DERIVED THROUGH THE SCREEN'S OWN FUNCTIONS: GetStrategyCostString with
// the screen's own scalars and discount, GetPowerRequirement (the one that knows an upgrade on
// a power coil costs nothing), GetStrategyReqString, HasEnoughPower, UpgradeAlreadyPerformed,
// CanBuildUpgrade. The screen already resolved them once; asking it again is the only way the
// panel and the speech cannot disagree.
//
// ⛔ It reads m_selectedUpgrade rather than re-indexing m_arrUpgrades: that field IS what
// UpdateSelection set, and m_arrUpgrades[SelectedIndex] is what OnAccept will buy.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessUpgradeReader extends Object;

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
//   kind=card    title  cost  power  reqs  upkeep  warning  status  desc
//
// Fields the screen is not drawing are omitted, and native drops a missing line rather than
// giving it a stop. There is no time line: an upgrade is installed the moment it is paid for
// (UIChooseUpgrade.StartUpgradeFacility adds it to Facility.Upgrades in the same state).
function DescribeUpgradeChoice()
{
	local UIChooseUpgrade Screen;
	local X2FacilityUpgradeTemplate Upgrade;
	local XComGameState_FacilityXCom Facility;
	local XComGameState_HeadquartersXCom XComHQ;
	local XGParamTag kTag;
	local string Rec, Title, Reqs, Upkeep;
	local bool bHasPower, bAlreadyUpgraded;
	local int Power;

	NativeResult = "";

	Screen = UIChooseUpgrade(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	Facility = XComGameState_FacilityXCom(`XCOMHISTORY.GetGameStateForObjectID(Screen.m_FacilityRef.ObjectID));

	// m_strTitle ships as "UPGRADE <XGParam:StrValue0/!UpgradeName/>" and the screen hands it
	// to its panel header UNEXPANDED (InitPanelHeader → SetText, no ExpandString anywhere), so
	// Flash draws whatever it makes of a stray tag. The only shipped use of the same key —
	// UIFacilityUpgrade.InitScreen — fills StrValue0 with the facility's own name, so that is
	// what the spoken title carries.
	Title = Screen.m_strTitle;
	if (Facility != none && Facility.GetMyTemplate() != none)
	{
		kTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
		kTag.StrValue0 = Facility.GetMyTemplate().DisplayName;
		Title = `XEXPAND.ExpandString(Screen.m_strTitle);
	}
	Emit("kind=header" $ FLD_SEP $ "title=" $ Title
		$ FLD_SEP $ "subtitle=" $ Screen.m_strListLabel);

	Upgrade = Screen.m_selectedUpgrade;
	if (Upgrade == none)
		return;

	XComHQ = class'UIUtilities_Strategy'.static.GetXComHQ();
	bHasPower = Screen.HasEnoughPower(Upgrade);
	bAlreadyUpgraded = Screen.UpgradeAlreadyPerformed(Upgrade);

	Rec = "kind=card";
	Rec $= FLD_SEP $ "title=" $ Upgrade.DisplayName;

	// UpdateSelection's first line, verbatim — including its choice of FacilityBuildCostScalars,
	// which is what the panel DRAWS even though CanBuildUpgrade tests FacilityUpgradeCostScalars.
	// The drawn number is the parity target; the test is the game's to make on Enter.
	Rec $= FLD_SEP $ "cost=" $ class'UIUtilities_Strategy'.static.GetStrategyCostString(
		Upgrade.Cost, XComHQ.FacilityBuildCostScalars,
		XComHQ.GetPendingFacilityDiscountPercent(Upgrade.DataName));

	// POWER is drawn as an icon and a bare number with the SIGN carrying the meaning — produced
	// when positive (a conduit), consumed when negative, absolute value shown either way — so
	// speech gets the word the icon stood in for. ⛔ int(Abs(...)): UnrealScript's Abs is FLOAT
	// and string() on it prints four decimals (the build-facility reader's live lesson).
	Power = Screen.GetPowerRequirement(Upgrade);
	if (Power >= 0)
		Rec $= FLD_SEP $ "power=" $ Screen.m_strPowerLabel @ "+" $ string(Power);
	else
		Rec $= FLD_SEP $ "power=" $ Screen.m_strPowerLabel @ "-" $ string(int(Abs(Power)));

	Reqs = class'UIUtilities_Strategy'.static.GetStrategyReqString(Upgrade.Requirements);
	if (Reqs != "")
		Rec $= FLD_SEP $ "reqs=" $ Reqs;

	if (Upgrade.UpkeepCost > 0)
	{
		Upkeep = Screen.m_strUpkeepCostLabel
			@ class'UIUtilities_Strategy'.default.m_strCreditsPrefix $ string(Upgrade.UpkeepCost);
		Rec $= FLD_SEP $ "upkeep=" $ Upkeep;
	}

	// UpdateSelection's one warning branch, under the same test it makes.
	if (!bHasPower && !bAlreadyUpgraded)
		Rec $= FLD_SEP $ "warning=" $ Screen.m_strInsufficientPowerWarning;

	// An upgrade already installed: the game greys its row and says nothing in words, and a
	// greyed row here can mean either "cannot afford" or "already have". The word is the game's
	// own — UIFacilityUpgrade_ListItem ships COMPLETED under its own loc section even though
	// the class is never spawned — and it is derivable from what is on screen (the room's
	// button reads REVIEW UPGRADES and the upgrade is visibly built), so it hands a blind player
	// nothing a sighted one lacks.
	if (bAlreadyUpgraded)
		Rec $= FLD_SEP $ "status=" $ class'UIFacilityUpgrade_ListItem'.default.m_strCompleted;

	if (Upgrade.Summary != "")
		Rec $= FLD_SEP $ "desc=" $ Upgrade.Summary;

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
