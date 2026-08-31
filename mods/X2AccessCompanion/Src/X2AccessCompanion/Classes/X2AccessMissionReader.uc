// X2AccessMissionReader
// The geoscape mission blade family (UIMission + its 16 subclasses) — the words half. Same
// contract as X2AccessPopupReader: C++ owns the gate, the keys and the speech, calls
// DescribeMissionBlade() on this class's default object via ProcessEvent from the graph recipe's
// build, and reads NativeResult back through reflection right after the call returns.
//
// WHERE THE WORDS COME FROM. Nothing on a blade retains its text (the pushes all go through
// LibraryPanel.MC straight into Flash), but the screen retains MissionRef and OWNS every getter
// its own Build*Panel called — GetOpName, GetObjectiveString, GetMissionDescString (which does
// the XGParam region expansion; reading m_strFlavorText raw would speak the unexpanded tag),
// GetRewardString, GetDifficultyString. So each branch below re-derives its class's Flash pushes
// by calling the same functions, in the same visual order — docs/mission-blade-recon.md §5 is
// the map. The default branch reads an unknown subclass (the two DLC blades, a mod's blade)
// through the base getters alone.
//
// ⛔ The OPTIONAL sections gate on the PANEL'S OWN bIsVisible, never on a recomputed game-state
// test. UpdateShadowChamber / UpdateSitreps / UpdateChosen already decided — and
// UIMission_ChosenAmbush overrides UpdateChosen to hide the strip unconditionally, so re-running
// the base gate would speak a warning the game deliberately blanked. A child widget's bIsVisible
// is deliberately toggled and does mean hidden (the documented exception in CLAUDE.md).
//
// ⛔ NO Caps(). The retaliation blade CapsChecks the region before handing it to Flash; that is
// visual style, not information (X2AccessPopupReader's rule).
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

// dependson: StrategyCostReward (the GPIntelOptions rows) is declared INSIDE
// X2StrategyGameRulesetDataStructures — same reason X2AccessPopupReader dependsons it.
class X2AccessMissionReader extends Object
	dependson(X2StrategyGameRulesetDataStructures);

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other companion reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ActivateMissionButton's index space. ⛔ Wire contract with the recipe — reorder nothing.
const BTN_BUTTON1 = 0;
const BTN_BUTTON2 = 1;
const BTN_BUTTON3 = 2;
const BTN_CONFIRM = 3;
const BTN_LOCKED = 4;

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants stops:
//   kind=blade    class, title, region       (always first; title is the blade's own heading)
//   kind=line     text                       (one per tab stop, in the blade's visual order)
//   kind=button   index, label               (index is the BTN_* constant Click() resolves)
function DescribeMissionBlade()
{
	local UIMission Screen;

	NativeResult = "";

	Screen = UIMission(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	switch (Screen.Class.Name)
	{
	case 'UIMission_Retaliation':
		DescribeRetaliation(UIMission_Retaliation(Screen));
		break;
	case 'UIMission_Council':
		DescribeCouncil(UIMission_Council(Screen));
		break;
	case 'UIMission_SupplyRaid':
		DescribeSupplyRaid(UIMission_SupplyRaid(Screen));
		break;
	case 'UIMission_LandedUFO':
		DescribeLandedUFO(UIMission_LandedUFO(Screen));
		break;
	case 'UIMission_GOps':
		DescribeGOps(UIMission_GOps(Screen));
		break;
	case 'UIMission_AlienFacility':
		DescribeAlienFacility(UIMission_AlienFacility(Screen));
		break;
	case 'UIMission_GoldenPath':
		DescribeGoldenPath(UIMission_GoldenPath(Screen));
		break;
	case 'UIMission_GPIntelOptions':
		DescribeGPIntelOptions(UIMission_GPIntelOptions(Screen));
		break;
	case 'UIMission_ResOps':
		DescribeFactionBlade(Screen, UIMission_ResOps(Screen).m_strResOpsMission);
		break;
	case 'UIMission_RescueSoldier':
		DescribeFactionBlade(Screen, UIMission_RescueSoldier(Screen).m_strRescueMission);
		break;
	case 'UIMission_LostAndAbandoned':
		DescribeFactionBlade(Screen, UIMission_LostAndAbandoned(Screen).m_strLostAndAbandonedMission);
		break;
	case 'UIMission_ChosenAmbush':
		DescribeChosenAmbush(UIMission_ChosenAmbush(Screen));
		break;
	case 'UIMission_ChosenStronghold':
		DescribeChosenStronghold(UIMission_ChosenStronghold(Screen));
		break;
	case 'UIMission_ChosenAvengerAssault':
		DescribeChosenAvengerAssault(UIMission_ChosenAvengerAssault(Screen));
		break;
	default:
		// DLC blades (AlienNest, LostTowers) and any mod's subclass: the base getters carry the
		// core facts, which beats swallowing the commit-to-a-mission screen entirely.
		DescribeGeneric(Screen);
		break;
	}

	// The sections the BASE class draws on every blade that shows them, in their on-screen
	// position (below the mission info, above the Chosen strip).
	PushShadowChamber(Screen);
	PushSitreps(Screen);
	PushChosenStrip(Screen);
	PushButtons(Screen);
}

// ---------------------------------------------------------------------------------------
// Per-class blades — each mirrors its class's Build*Panel pushes (recon §5)
// ---------------------------------------------------------------------------------------

private function DescribeRetaliation(UIMission_Retaliation Screen)
{
	PushHeader("UIMission_Retaliation", Screen.m_strRetaliationMission, Screen.GetRegionName());
	// The right-hand blade: the warning heading and the expanded flavor text under it.
	PushPair(Screen.m_strRetaliationWarning, Screen.GetMissionDescString());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
}

private function DescribeCouncil(UIMission_Council Screen)
{
	PushHeader("UIMission_Council", Screen.m_strCouncilMission, Screen.GetRegionName());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
	PushValuePair(Screen.m_strReward, Screen.GetRewardString());
}

private function DescribeSupplyRaid(UIMission_SupplyRaid Screen)
{
	PushHeader("UIMission_SupplyRaid", Screen.m_strSupplyMission, Screen.GetRegionName());
	PushLine(Screen.GetRaidDesc());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
}

private function DescribeLandedUFO(UIMission_LandedUFO Screen)
{
	PushHeader("UIMission_LandedUFO", Screen.m_strLandedUFOMission, Screen.GetRegionName());
	PushLine(Screen.GetRaidDesc());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
}

private function DescribeGOps(UIMission_GOps Screen)
{
	PushHeader("UIMission_GOps", Screen.m_strGOpsTitle, Screen.GetRegionName());
	PushPair(Screen.m_strMissionLabel, Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
	PushValuePair(Screen.m_strMissionDifficulty, Screen.GetDifficultyString());
	PushValuePair(Screen.m_strReward, Screen.GetRewardString());

	// The dark event block, exactly as BuildMissionPanel branches it.
	if (Screen.GetMission().HasDarkEvent())
	{
		PushValuePair(Screen.m_strDarkEventLabel, Screen.GetMission().GetDarkEvent().GetDisplayName());
		PushLine(Screen.GetMission().GetDarkEvent().GetPreMissionText());
	}

	// "N of M sites unlocked" style help for regions without radio contact — empty when all
	// sites are reachable, and PushLine drops empties.
	PushLine(Screen.GetUnlockHelpString());
}

private function DescribeAlienFacility(UIMission_AlienFacility Screen)
{
	PushHeader("UIMission_AlienFacility", Screen.GetMissionTitle(), Screen.GetRegionName());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
	PushLine(Screen.GetMissionDescString());
	PushValuePair(Screen.m_strReward, Screen.GetMission().GetRewardAmountString());
	if (!Screen.CanTakeMission())
	{
		PushPair(Screen.m_strLocked, Screen.m_strLockedHelp);
	}
}

private function DescribeGoldenPath(UIMission_GoldenPath Screen)
{
	PushHeader("UIMission_GoldenPath", Screen.GetMissionTitle(), Screen.GetRegionName());
	PushLine(Screen.GetMissionSubtitle());
	PushLine(Screen.GetOpName());
	PushPair(Screen.GetObjectiveHeader(), Screen.GetObjectiveString());
	PushLine(Screen.GetMissionDescString());
	PushValuePair(Screen.m_strReward, Screen.GetMission().GetRewardAmountString());
	if (!Screen.CanTakeMission())
	{
		// The game's own branch: the final mission is gated on the Avatar Project autopsy
		// chain and says so with different words.
		if (Screen.IsFinalMission())
			PushPair(Screen.m_strAvatarLocked, Screen.m_strAvatarLockedHelp);
		else
			PushPair(Screen.m_strLocked, Screen.m_strLockedHelp);
	}
}

private function DescribeGPIntelOptions(UIMission_GPIntelOptions Screen)
{
	local array<StrategyCostReward> IntelOptions;
	local X2HackRewardTemplateManager HackMgr;
	local X2HackRewardTemplate OptionTemplate;
	local int i;

	PushHeader("UIMission_GPIntelOptions", Screen.GetMissionTitle(), Screen.GetRegionName());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
	PushLine(Screen.GetMissionDescString());

	// The intel purchase list, read the way UpdateDisplay builds its rows. Read-only for now:
	// the rows are UIMechaListItem checkboxes and the purchase toggle is owed
	// (docs/next-steps.md) — the blade still launches and cancels without it.
	PushLine(Screen.IntelOptionsLabel);
	HackMgr = class'X2HackRewardTemplateManager'.static.GetHackRewardTemplateManager();
	IntelOptions = Screen.GetMissionIntelOptions();
	for (i = 0; i < IntelOptions.Length; i++)
	{
		OptionTemplate = HackMgr.FindHackRewardTemplate(IntelOptions[i].Reward);
		if (OptionTemplate != none)
			PushLine(OptionTemplate.GetFriendlyName() $ ": " $ string(Screen.GetIntelCost(IntelOptions[i])));
	}
	PushPair(Screen.IntelAvailableLabel, string(Screen.GetAvailableIntel()));
	PushPair(Screen.IntelTotalLabel, string(Screen.GetTotalIntelCost()));

	if (!Screen.CanTakeMission())
	{
		PushPair(Screen.m_strLocked, Screen.m_strLockedHelp);
	}
}

// ResOps / RescueSoldier / LostAndAbandoned share one Flash blade (XPACK_Alert_MissionBlades)
// and one push order: faction identity, op name, objective, reward.
private function DescribeFactionBlade(UIMission Screen, string Heading)
{
	local XComGameState_ResistanceFaction FactionState;

	PushHeader(string(Screen.Class.Name), Heading, Screen.GetRegionName());

	FactionState = Screen.GetMission().GetResistanceFaction();
	if (FactionState != none)
		PushPair(FactionState.GetFactionTitle(), FactionState.GetFactionName());

	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
	PushValuePair(Screen.m_strReward, Screen.GetRewardString());
}

private function DescribeChosenAmbush(UIMission_ChosenAmbush Screen)
{
	PushHeader("UIMission_ChosenAmbush", Screen.m_strTitle, Screen.GetRegionName());
	PushChosenIdentity(Screen.GetChosen());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strChosenAmbushObjective, Screen.m_strChosenAmbushObjectiveDesc);
	PushLine(Screen.m_strBody);
}

private function DescribeChosenStronghold(UIMission_ChosenStronghold Screen)
{
	PushHeader("UIMission_ChosenStronghold", Screen.GetMissionTitle(), Screen.GetRegionName());
	PushChosenIdentity(Screen.GetChosen());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
	PushLine(Screen.GetMissionDescString());
	if (!Screen.CanTakeMission())
	{
		PushPair(Screen.m_strLocked, Screen.m_strLockedHelp);
	}
}

private function DescribeChosenAvengerAssault(UIMission_ChosenAvengerAssault Screen)
{
	PushHeader("UIMission_ChosenAvengerAssault", Screen.m_strTitle, Screen.GetRegionName());
	PushChosenIdentity(Screen.GetChosen());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strAvengerAssaultObjective, Screen.m_strAvengerAssaultObjectiveDesc);
	PushLine(Screen.GetMissionDescString());
}

private function DescribeGeneric(UIMission Screen)
{
	PushHeader(string(Screen.Class.Name), Screen.GetMissionTitle(), Screen.GetRegionName());
	PushLine(Screen.GetOpName());
	PushPair(Screen.m_strMissionObjective, Screen.GetObjectiveString());
	PushLine(Screen.GetMissionDescString());
	PushValuePair(Screen.m_strMissionDifficulty, Screen.GetDifficultyString());
	PushValuePair(Screen.m_strReward, Screen.GetRewardString());
	if (!Screen.CanTakeMission())
	{
		PushLine(Screen.m_strLocked);
	}
}

// ---------------------------------------------------------------------------------------
// The base-class sections
// ---------------------------------------------------------------------------------------

// Chosen identity on the Chosen-themed blades, where it is part of the info blade itself.
private function PushChosenIdentity(XComGameState_AdventChosen ChosenState)
{
	local string Line;

	if (ChosenState == none)
		return;

	Line = "";
	class'X2AccessHelper'.static.AppendComma(Line, ChosenState.GetChosenClassName());
	class'X2AccessHelper'.static.AppendComma(Line, ChosenState.GetChosenName());
	class'X2AccessHelper'.static.AppendComma(Line, ChosenState.GetChosenNickname());
	PushLine(Line);
}

private function PushShadowChamber(UIMission Screen)
{
	if (Screen.ShadowChamber == none || !Screen.ShadowChamber.bIsVisible)
		return;

	PushLine(Screen.m_strShadowChamberTitle);
	// m_strEnemiesDetected ends in ":" so AppendJoined takes a plain space, matching the panel.
	PushPair(Screen.m_strEnemiesDetected, Screen.GetCrewCount());
	PushLine(Screen.GetCrewString());
}

private function PushSitreps(UIMission Screen)
{
	local X2SitRepTemplateManager SitRepMgr;
	local X2SitRepTemplate SitRep;
	local GeneratedMissionData MissionData;
	local name SitRepName;

	if (Screen.SitrepPanel == none || !Screen.SitrepPanel.bIsVisible)
		return;

	PushLine(Screen.m_strSitrepTitle);

	MissionData = class'UIUtilities_Strategy'.static.GetXComHQ().GetGeneratedMissionData(
		Screen.MissionRef.ObjectID);
	SitRepMgr = class'X2SitRepTemplateManager'.static.GetSitRepTemplateManager();
	foreach MissionData.SitReps(SitRepName)
	{
		SitRep = SitRepMgr.FindSitRepTemplate(SitRepName);
		if (SitRep == none)
			continue;
		// The same name+description pair UpdateSitreps pushes into the panel (`Description` is
		// protectedwrite, read is legal — the sitrep popup reader's note).
		PushLine(SitRep.GetFriendlyName() @ SitRep.Description);
	}
}

// The strip along the bottom of a blade in a Chosen-controlled region. The identity is
// re-derived the way UpdateChosen derived it; the gate is the panel the game showed or hid.
private function PushChosenStrip(UIMission Screen)
{
	local XComGameState_AdventChosen ChosenState;
	local XComGameState_WorldRegion RegionState;

	if (Screen.ChosenPanel == none || !Screen.ChosenPanel.bIsVisible)
		return;

	RegionState = Screen.GetRegion();
	if (RegionState == none)
		return;
	ChosenState = RegionState.GetControllingChosen();
	if (ChosenState == none)
		return;

	PushChosenIdentity(ChosenState);
	PushPair(Screen.m_strChosenWarning2, Screen.m_strChosenWarning);
}

// ---------------------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------------------

// Which buttons are LIVE is the widgets' own state: RefreshNavigation removes the hidden ones,
// the locked path removes Button1-3 and leaves only LockedButton, the ambush hides Button2 in
// retail. So visibility decides emission, and the per-class labels mirror what each class put
// on screen — Button.Text where the class SetText'd (the GOps mission selectors), the localized
// var it pushed to Flash where it did not.
private function PushButtons(UIMission Screen)
{
	local string Confirm, Cancel;

	// The per-class label pair for the plain confirm/cancel blades (recon §5).
	switch (Screen.Class.Name)
	{
	case 'UIMission_Retaliation':
	case 'UIMission_ChosenAmbush':
	case 'UIMission_ChosenStronghold':
	case 'UIMission_ChosenAvengerAssault':
	case 'UIMission_AlienFacility':
	case 'UIMission_GoldenPath':
	case 'UIMission_GPIntelOptions':
		// These SetText their buttons (Retaliation) or push explicit confirm wording
		// (m_strConfirmMission / m_strLaunchMission) with the generic cancel.
		Confirm = FirstNonEmpty(ButtonText(Screen.Button1), Screen.m_strLaunchMission);
		Cancel = FirstNonEmpty(ButtonText(Screen.Button2),
			class'UIUtilities_Text'.default.m_strGenericCancel);
		break;
	default:
		Confirm = FirstNonEmpty(ButtonText(Screen.Button1), Screen.m_strLaunchMission);
		Cancel = FirstNonEmpty(ButtonText(Screen.Button2), Screen.m_strIgnore);
		break;
	}

	if (Screen.Class.Name == 'UIMission_GOps')
	{
		// Button1-3 SELECT one of the 1-3 guerrilla ops (mouse-only in vanilla — parity, recon
		// §3); their SetText'd labels name each site. ConfirmButton launches the selected one.
		PushButton(BTN_BUTTON1, ButtonText(Screen.Button1));
		PushButton(BTN_BUTTON2, ButtonText(Screen.Button2));
		PushButton(BTN_BUTTON3, ButtonText(Screen.Button3));
		if (IsLive(Screen.ConfirmButton))
			PushButton(BTN_CONFIRM, FirstNonEmpty(ButtonText(Screen.ConfirmButton),
				class'UIUtilities_Text'.default.m_strGenericConfirm));
	}
	else
	{
		if (IsLive(Screen.Button1))
			PushButton(BTN_BUTTON1, Confirm);
		if (IsLive(Screen.Button2))
			PushButton(BTN_BUTTON2, Cancel);
		if (IsLive(Screen.Button3))
			PushButton(BTN_BUTTON3, FirstNonEmpty(ButtonText(Screen.Button3),
				class'UIUtilities_Text'.default.m_strGenericConfirm));
		if (IsLive(Screen.ConfirmButton))
			PushButton(BTN_CONFIRM, FirstNonEmpty(ButtonText(Screen.ConfirmButton),
				Screen.m_strLaunchMission));
	}

	// The locked blade's only way out. SetText'd (m_strBack) by every class that spawns it.
	if (IsLive(Screen.LockedButton))
		PushButton(BTN_LOCKED, FirstNonEmpty(ButtonText(Screen.LockedButton), Screen.m_strBack));
}

// Fire one button's own click path. Called by native from the recipe's activate handler (game
// thread, input drain). Click() carries the game's gates — visible, enabled, delegate bound —
// so a stale index degrades to a no-op rather than a wrong action.
function ActivateMissionButton(int Index)
{
	local UIMission Screen;
	local UIButton Btn;

	Screen = UIMission(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	switch (Index)
	{
	case BTN_BUTTON1: Btn = Screen.Button1; break;
	case BTN_BUTTON2: Btn = Screen.Button2; break;
	case BTN_BUTTON3: Btn = Screen.Button3; break;
	case BTN_CONFIRM: Btn = Screen.ConfirmButton; break;
	case BTN_LOCKED: Btn = Screen.LockedButton; break;
	default:
		return;
	}

	if (Btn != none)
		Btn.Click();
}

// ---------------------------------------------------------------------------------------
// Wire
// ---------------------------------------------------------------------------------------

private function bool IsLive(UIButton Btn)
{
	return Btn != none && Btn.bIsVisible && !Btn.IsDisabled;
}

// A button's retained words, when the class went through SetText (native CleanTexts the <img>
// markup ResOps and RescueSoldier inject). Empty on the classes whose labels only went to Flash.
private function string ButtonText(UIButton Btn)
{
	if (Btn == none || !Btn.bIsVisible)
		return "";
	return Btn.Text;
}

private function string FirstNonEmpty(string A, string B)
{
	if (A != "")
		return A;
	return B;
}

private function PushHeader(string ClassName, string Title, string Region)
{
	local string Rec;

	Rec = "kind=blade";
	Rec $= FLD_SEP $ "class=" $ ClassName;
	Rec $= FLD_SEP $ "title=" $ Title;
	Rec $= FLD_SEP $ "region=" $ Region;
	Push(Rec);
}

// Blank lines are dropped here rather than at the recipe (the popup reader's rule): a localized
// var a class declares and never populates would otherwise become an empty tab stop.
private function PushLine(string Text)
{
	if (Text == "")
		return;
	Push("kind=line" $ FLD_SEP $ "text=" $ Text);
}

// A label+value pair as ONE line, joined the kernel way (AppendJoined tests the accumulated
// tail, so a label ending in ":" takes a plain space, anything else the comma).
private function PushPair(string Label, string Value)
{
	local string Line;

	Line = "";
	class'X2AccessHelper'.static.AppendComma(Line, Label);
	class'X2AccessHelper'.static.AppendComma(Line, Value);
	PushLine(Line);
}

// A pair whose LABEL means nothing without its value — НАГРАДА, СЛОЖНОСТЬ — dropped whole when
// the value is absent. The game's own blade draws no reward row on a rewardless mission, and the
// first live pass (2026-08-31, a locked alien facility) spoke a naked "НАГРАДА" where a sighted
// player saw nothing. Not used for the locked pair: "ЗАДАНИЕ НЕДОСТУПНО!" stands on its own.
private function PushValuePair(string Label, string Value)
{
	if (Value == "")
		return;
	PushPair(Label, Value);
}

private function PushButton(int Index, string Label)
{
	local string Rec;

	if (Label == "")
		return;

	Rec = "kind=button";
	Rec $= FLD_SEP $ "index=" $ string(Index);
	Rec $= FLD_SEP $ "label=" $ Label;
	Push(Rec);
}

private function Push(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}
