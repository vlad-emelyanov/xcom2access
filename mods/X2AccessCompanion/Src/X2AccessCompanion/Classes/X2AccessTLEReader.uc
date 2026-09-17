// X2AccessTLEReader
// The Tactical Legacy Pack hub (UITLEHub) - the numbers half. Same contract as
// X2AccessSaveReader: the C++ layer owns the gate, the keys and the speech, calls DescribeHub()
// on this class's default object via ProcessEvent (game thread, from the graph recipe's build)
// and reads NativeResult back through reflection right after the call returns. Nothing here runs
// on its own - no listeners, no timers, no polling.
//
// WHY ONLY THE NUMBERS. Every word on the hub is a localized string on the screen instance, and
// native reads those at their offsets. Every figure - ladder wins, high scores, the bronze-medal
// "N/4", the completion meters, and the condition behind each card's NEW tag - is read by
// UITLEHub.OnInit out of the online profile, pushed into Flash through SetButtonInfo /
// SetSmallStat / SetLargeStat / SetMeterStat, and retained nowhere. So every line below is that
// function's arithmetic over the same profile object, field for field: not one XCOMHISTORY
// access, and the same denominators (28 narrative missions, 109 challenges) it hard-codes.
//
// One record per card, keyed by the card's on-screen INDEX (0 ladder, 1 skirmish, 2 challenge),
// which is the order the screen draws them and the order native reads its own fields in.
//
// GC rule: this object is a CDO - value-typed state only, NEVER object references.

class X2AccessTLEReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

function DescribeHub()
{
	local XComOnlineProfileSettings Profile;
	local string Rec;
	local int HighestLadder;

	NativeResult = "";

	Profile = `XPROFILESETTINGS;
	if (Profile == none || Profile.Data == none)
		return;

	// Legacy Operations. NEW while no ladder has ever scored (OnInit's own test).
	HighestLadder = Profile.Data.GetHighestLadderScore();
	Rec = "index=0";
	if (HighestLadder <= 0)
		AddField(Rec, "new", "1");
	AddField(Rec, "wins", string(Profile.Data.HubStats.SuccessfulLadders));
	AddField(Rec, "high", string(HighestLadder));
	AddField(Rec, "unlock", BuildUnlockString(Profile));
	AddField(Rec, "done", string(Profile.Data.HubStats.LadderCompletions.Length));
	AddField(Rec, "pct", string(100 * Profile.Data.HubStats.LadderCompletions.Length / 28));
	// The meter's label is the ladder menu's own header, not one of the hub's strings - the one
	// word on the screen that lives on another class, so it travels with the number it labels.
	AddField(Rec, "meter", class'UITLE_LadderModeMenu'.default.m_NarrativeLaddersHeader);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	// Skirmish. NEW while none has been created.
	Rec = "index=1";
	if (Profile.Data.HubStats.NumSkirmishes <= 0)
		AddField(Rec, "new", "1");
	AddField(Rec, "wins", string(Profile.Data.HubStats.NumSkirmishVictories));
	AddField(Rec, "created", string(Profile.Data.HubStats.NumSkirmishes));
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	// Challenge Archives. NEW while no challenge has scored.
	Rec = "index=2";
	if (Profile.Data.HubStats.OfflineChallengeHighScore <= 0)
		AddField(Rec, "new", "1");
	AddField(Rec, "wins", string(Profile.Data.HubStats.NumOfflineChallengeVictories));
	AddField(Rec, "high", string(Profile.Data.HubStats.OfflineChallengeHighScore));
	AddField(Rec, "done", string(Profile.Data.HubStats.OfflineChallengeCompletion.Length));
	AddField(Rec, "pct", string(100 * Profile.Data.HubStats.OfflineChallengeCompletion.Length / 109));
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// ---------------------------------------------------------------------------------------
// The Legacy Operations menu (UITLE_LadderModeMenu) - the words its widgets do not retain
// ---------------------------------------------------------------------------------------
//
// Three record kinds. kind=row (one per narrative ladder row, keyed by index): the ladder's
// name, which lives on the XComGameState_LadderProgress the row retains, its IN PROGRESS /
// COMPLETE status and how many of its three medals the profile has earned. kind=panel (at most
// one): the details panel for the selected ladder. kind=soldier (one per squad member drawn).
//
// WHERE THE PANEL'S WORDS COME FROM. UpdateData reads a ladder history FILE into the history
// and pushes everything from the state objects it finds there - so the history, at the moment
// native asks, IS the screen's cache of the selected ladder, and every line below reads those
// same objects the way UpdateData did. It never re-reads the file: that is the screen's job,
// and asking again is exactly what the read-the-screen rule forbids. Consistency is checked
// rather than assumed: the LadderProgress in the history must carry the index the screen's own
// selection resolves to, or no panel is emitted at all (the "[+] generate" row blanks the panel
// without touching the history, which is the case that check exists for).

function DescribeLadderMenu()
{
	local UIScreenStack Stack;
	local UITLE_LadderModeMenu Screen;
	local UITLENarrativeLadder_ListItem Row;
	local XComOnlineProfileSettings Profile;
	local XComGameStateHistory History;
	local XComGameState_LadderProgress LadderData;
	local XComGameState_BattleData BattleData;
	local XComGameState_HeadquartersXCom XComHQ;
	local XComGameState_CampaignSettings Campaign;
	local XComGameState_Unit Unit;
	local StateObjectReference UnitRef;
	local X2SoldierClassTemplate SoldierClass;
	local X2MissionTemplate MissionTemplate;
	local string Rec, Value;
	local int i, HighScore, Threshold, Earned, ListIndex, LadderIndex, SoldierIndex;
	local bool bRandomSelected, bHasSave;

	NativeResult = "";

	Stack = `SCREENSTACK;
	if (Stack == none)
		return;
	Screen = UITLE_LadderModeMenu(Stack.GetFirstInstanceOf(class'UITLE_LadderModeMenu'));
	if (Screen == none || Screen.NarrativeList == none || Screen.List == none)
		return;

	Profile = `XPROFILESETTINGS;
	if (Profile == none || Profile.Data == none)
		return;

	// ---- the narrative rows ----
	for (i = 0; i < Screen.NarrativeList.GetItemCount(); ++i)
	{
		Row = UITLENarrativeLadder_ListItem(Screen.NarrativeList.GetItem(i));
		if (Row == none || Row.m_LadderData == none)
			continue;

		LadderIndex = Row.m_LadderData.LadderIndex;
		Rec = "kind=row";
		AddField(Rec, "index", string(i));
		if (LadderIndex >= 0 && LadderIndex < Row.m_LadderData.NarrativeLadderNames.Length)
			AddField(Rec, "name", Row.m_LadderData.NarrativeLadderNames[LadderIndex]);

		// PopulateData's own three-way status: a save in progress, a score on record, nothing.
		HighScore = Profile.Data.GetLadderHighScore(LadderIndex);
		if (Screen.HasLadderSave(LadderIndex))
			AddField(Rec, "status", Row.m_LadderInprogress);
		else if (HighScore > 0)
			AddField(Rec, "status", Row.m_LadderComplete);

		// The row's three medal icons: lit while the high score reaches the threshold.
		Earned = 0;
		for (Threshold = 0; Threshold < 3; ++Threshold)
		{
			if (HighScore >= class'XComGameState_LadderProgress'.static.GetLadderMedalThreshold(LadderIndex, Threshold))
				++Earned;
		}
		// The game's own medal words: None, Bronze, Silver, Gold - the highest one lit.
		if (Earned >= 0 && Earned < Screen.m_MedalTypes.Length)
			AddField(Rec, "medal", Screen.m_MedalTypes[Earned]);
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	}

	// ---- which ladder the panel shows ----
	bRandomSelected = (Screen.ClickedList == Screen.List);
	if (bRandomSelected && Screen.clickedIndex == 0)
		return; // the "[+] generate" row: the panel is blanked and the history is stale

	ListIndex = Screen.clickedIndex;
	if (bRandomSelected)
		ListIndex += Screen.NarrativeList.GetItemCount() - 1;
	if (ListIndex < 0 || ListIndex >= Screen.m_LadderList.Length)
		return;
	LadderIndex = int(Screen.m_LadderList[ListIndex]);

	History = `XCOMHISTORY;
	LadderData = XComGameState_LadderProgress(History.GetSingleGameStateObjectForClass(class'XComGameState_LadderProgress', true));
	if (LadderData == none || LadderData.LadderIndex != LadderIndex)
		return; // the history does not hold the ladder the screen says it selected
	BattleData = XComGameState_BattleData(History.GetSingleGameStateObjectForClass(class'XComGameState_BattleData', true));
	XComHQ = XComGameState_HeadquartersXCom(History.GetSingleGameStateObjectForClass(class'XComGameState_HeadquartersXCom', true));
	Campaign = XComGameState_CampaignSettings(History.GetSingleGameStateObjectForClass(class'XComGameState_CampaignSettings', true));

	HighScore = Profile.Data.GetLadderHighScore(LadderIndex);
	bHasSave = Screen.HasLadderSave(LadderIndex);

	Rec = "kind=panel";
	AddField(Rec, "index", string(ListIndex));

	// The lock line, narrative ladders only: the label and the "requires a bronze medal from
	// the previous ladder" sentence UpdateData composes.
	if (!bRandomSelected)
	{
		Row = UITLENarrativeLadder_ListItem(Screen.NarrativeList.GetItem(ListIndex));
		if (Row != none && Row.bDisabled)
		{
			AddField(Rec, "locked", Screen.m_LockedLabel);
			if (ListIndex > 0 && LadderData.LadderIndex - 1 >= 0 && LadderData.LadderIndex - 1 < LadderData.NarrativeLadderNames.Length)
				AddField(Rec, "lockedtext", Repl(Screen.m_LockedValue, "%sPREVIOUSLADDER", LadderData.NarrativeLadderNames[LadderData.LadderIndex - 1]));
			else
				AddField(Rec, "lockedtext", Screen.m_LockedValue);
		}
	}

	// OBJECTIVE: the mission template's display name.
	if (BattleData != none)
	{
		MissionTemplate = class'X2MissionTemplateManager'.static.GetMissionTemplateManager().FindMissionTemplate(BattleData.MapData.ActiveMission.MissionName);
		if (MissionTemplate != none && MissionTemplate.DisplayName != "")
		{
			AddField(Rec, "objectivelabel", Screen.m_ObjectiveLabel);
			AddField(Rec, "objective", MissionTemplate.DisplayName);
		}
	}

	// MISSION PROGRESS: "MISSION: rung / size".
	AddField(Rec, "progresslabel", Screen.m_ProgressLabel);
	AddField(Rec, "progress", Screen.m_MissionLabel @ LadderData.LadderRung @ "/" @ LadderData.LadderSize);

	// The medal thresholds along the meter, each with its word and whether the score reaches it.
	for (i = 0; i < 3; ++i)
	{
		Threshold = LadderData.GetLadderMedalThreshold(LadderData.LadderIndex, i);
		Value = Screen.m_MedalTypes[i] @ Threshold;
		class'X2AccessWire'.static.AddPositionalField(Rec, "medal", Value);
		class'X2AccessWire'.static.AddPositionalField(Rec, "medalwon", (Threshold <= HighScore) ? "1" : "");
	}

	// HIGHEST SCORE: the profile's, or the ladder's running score while none is on record.
	if (HighScore <= 0)
		HighScore = LadderData.CumulativeScore;
	AddField(Rec, "scorelabel", Screen.m_ScoreLabel);
	AddField(Rec, "score", string(HighScore));

	// DIFFICULTY: the Legacy Pack's own difficulty words for a narrative ladder, the campaign's
	// for a procedural one - the same switch UpdateData makes.
	if (Campaign != none)
	{
		AddField(Rec, "difficultylabel", Screen.m_DifficultyLabel);
		if (LadderData.LadderIndex < 10 && Campaign.DifficultySetting < Screen.m_arrDifficultyTypeStrings.Length)
			AddField(Rec, "difficulty", Screen.m_arrDifficultyTypeStrings[Campaign.DifficultySetting]);
		else if (Campaign.DifficultySetting < class'UIShellDifficulty'.default.m_arrDifficultyTypeStrings.Length)
			AddField(Rec, "difficulty", class'UIShellDifficulty'.default.m_arrDifficultyTypeStrings[Campaign.DifficultySetting]);
	}

	// The narrative paragraph - indexed by LIST position, exactly as SetNarrativeLadderText is
	// fed, so a procedural ladder has none.
	if (ListIndex < Screen.m_arrNarrativeStrings.Length)
		AddField(Rec, "narrative", Screen.m_arrNarrativeStrings[ListIndex]);

	// The verbs the panel offers: CONTINUE over BEGIN while a save exists (and ABANDON with it),
	// DELETE on procedural ladders only - RefreshButtonHelpLabels' own three tests.
	if (bHasSave)
		AddField(Rec, "hassave", "1");
	if (LadderData.LadderIndex >= 10)
		AddField(Rec, "deletable", "1");
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	// ---- the squad ----
	if (XComHQ == none)
		return;
	SoldierIndex = 0;
	foreach XComHQ.Squad(UnitRef)
	{
		Unit = XComGameState_Unit(History.GetGameStateForObjectID(UnitRef.ObjectID));
		if (Unit == none || Unit.bMissionProvided)
			continue; // UpdateData skips mission-provided units (VIPs) the same way

		Rec = "kind=soldier";
		AddField(Rec, "index", string(SoldierIndex));
		if (LadderData.LadderIndex < 10)
			AddField(Rec, "name", class'XComGameState_LadderProgress'.static.GetUnitName(SoldierIndex, LadderData.LadderIndex));
		else
			AddField(Rec, "name", Unit.GetFullName());
		// The panel draws a rank ICON and a class icon with the class name under it.
		AddField(Rec, "rank", class'X2ExperienceConfig'.static.GetRankName(Unit.GetRank(), Unit.GetSoldierClassTemplateName()));
		SoldierClass = Unit.GetSoldierClassTemplate();
		if (SoldierClass != none)
			AddField(Rec, "class", SoldierClass.DisplayName);
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
		++SoldierIndex;
	}
}

// ---------------------------------------------------------------------------------------
// The ladder difficulty page (UITLE_LadderDifficulty) - its title and the description line
// ---------------------------------------------------------------------------------------
//
// The screen pushes UIShellDifficulty's own title into Flash and retains nothing; the paragraph
// under the list is m_arrDifficultyDescStrings[m_iSelectedDifficulty] (RefreshDescInfo's
// mouse-and-keyboard branch). Both are class defaults, read here so native need not resolve a
// second class's CDO for two strings.

function DescribeLadderDifficulty()
{
	local UIScreenStack Stack;
	local UITLE_LadderDifficulty Screen;
	local string Rec;

	NativeResult = "";

	Stack = `SCREENSTACK;
	if (Stack == none)
		return;
	Screen = UITLE_LadderDifficulty(Stack.GetFirstInstanceOf(class'UITLE_LadderDifficulty'));
	if (Screen == none)
		return;

	Rec = "kind=header";
	AddField(Rec, "title", class'UIShellDifficulty'.default.m_strSelectDifficulty);
	if (Screen.m_iSelectedDifficulty >= 0 && Screen.m_iSelectedDifficulty < class'UIShellDifficulty'.default.m_arrDifficultyDescStrings.Length)
		AddField(Rec, "desc", class'UIShellDifficulty'.default.m_arrDifficultyDescStrings[Screen.m_iSelectedDifficulty]);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// ---------------------------------------------------------------------------------------
// The soldier loadout popup (UITLEUnitStats) - everything on it
// ---------------------------------------------------------------------------------------
//
// The screen retains the unit array and the index; every word is pushed into Flash from the
// unit state. Each block below mirrors the screen's own populate function over the same unit
// and the same check state: UpdateData (the header), PopulateStats, PopulateSoldierGear,
// PopulateAbilities. Gear slots are POSITIONAL (an empty utility pocket must keep its place).

function DescribeUnitStats()
{
	local UIScreenStack Stack;
	local UITLEUnitStats Screen;
	local XComGameState_Unit Unit;
	local XComGameState CheckState;
	local XComGameStateHistory History;
	local XComGameState_LadderProgress LadderData;
	local X2SoldierClassTemplate SoldierClass;
	local string Rec;

	NativeResult = "";

	Stack = `SCREENSTACK;
	if (Stack == none)
		return;
	Screen = UITLEUnitStats(Stack.GetFirstInstanceOf(class'UITLEUnitStats'));
	if (Screen == none)
		return;
	if (Screen.m_CurrentIndex < 0 || Screen.m_CurrentIndex >= Screen.m_UnitArray.Length)
		return;
	Unit = Screen.m_UnitArray[Screen.m_CurrentIndex];
	if (Unit == none)
		return;
	CheckState = Screen.m_CheckGameState;

	History = `XCOMHISTORY;
	LadderData = XComGameState_LadderProgress(History.GetSingleGameStateObjectForClass(class'XComGameState_LadderProgress', true));
	SoldierClass = Unit.GetSoldierClassTemplate();

	// ---- the header: name, rank, class ----
	Rec = "kind=header";
	AddField(Rec, "title", Screen.m_ScreenTitle);
	AddField(Rec, "subtitle", class'XLocalizedData'.default.AbilityListHeader);
	if (LadderData != none && LadderData.LadderIndex < 10)
		AddField(Rec, "name", class'XComGameState_LadderProgress'.static.GetUnitName(Screen.m_CurrentIndex, LadderData.LadderIndex));
	else
		AddField(Rec, "name", Unit.GetFullName());
	AddField(Rec, "rank", class'X2ExperienceConfig'.static.GetRankName(Unit.GetSoldierRank(), Unit.GetSoldierClassTemplateName()));
	if (SoldierClass != none)
		AddField(Rec, "class", SoldierClass.DisplayName);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	DescribeUnitStatLines(Unit, CheckState);
	DescribeUnitGear(Unit, CheckState);
	DescribeUnitAbilities(Unit, CheckState);
}

// PopulateStats, line for line: current stat plus ability modifiers, the inventory bonus as a
// signed suffix, psi only for a psi operative. The screen colours the bonus; native strips the
// markup and keeps the sign.
private function DescribeUnitStatLines(XComGameState_Unit Unit, XComGameState CheckState)
{
	local string Will;

	Will = string(int(Unit.GetCurrentStat(eStat_Will)) + Unit.GetUIStatFromAbilities(eStat_Will)) $ "/" $ string(int(Unit.GetMaxStat(eStat_Will)));

	AddStat(class'UISoldierHeader'.default.m_strHealthLabel, string(int(Unit.GetCurrentStat(eStat_HP)) + Unit.GetUIStatFromAbilities(eStat_HP)), Unit.GetUIStatFromInventory(eStat_HP, CheckState));
	AddStat(class'UISoldierHeader'.default.m_strMobilityLabel, string(int(Unit.GetCurrentStat(eStat_Mobility)) + Unit.GetUIStatFromAbilities(eStat_Mobility)), Unit.GetUIStatFromInventory(eStat_Mobility, CheckState));
	AddStat(class'UISoldierHeader'.default.m_strAimLabel, string(int(Unit.GetCurrentStat(eStat_Offense)) + Unit.GetUIStatFromAbilities(eStat_Offense)), Unit.GetUIStatFromInventory(eStat_Offense, CheckState));
	AddStat(class'UISoldierHeader'.default.m_strWillLabel, Will, Unit.GetUIStatFromInventory(eStat_Will, CheckState));
	AddStat(class'UISoldierHeader'.default.m_strArmorLabel, string(int(Unit.GetCurrentStat(eStat_ArmorMitigation)) + Unit.GetUIStatFromAbilities(eStat_ArmorMitigation)), Unit.GetUIStatFromInventory(eStat_ArmorMitigation, CheckState));
	AddStat(class'UISoldierHeader'.default.m_strDodgeLabel, string(int(Unit.GetCurrentStat(eStat_Dodge)) + Unit.GetUIStatFromAbilities(eStat_Dodge)), Unit.GetUIStatFromInventory(eStat_Dodge, CheckState));
	AddStat(class'UISoldierHeader'.default.m_strTechLabel, string(int(Unit.GetCurrentStat(eStat_Hacking)) + Unit.GetUIStatFromAbilities(eStat_Hacking)), Unit.GetUIStatFromInventory(eStat_Hacking, CheckState));
	if (Unit.IsPsiOperative())
		AddStat(class'UISoldierHeader'.default.m_strPsiLabel, string(int(Unit.GetCurrentStat(eStat_PsiOffense)) + Unit.GetUIStatFromAbilities(eStat_PsiOffense)), Unit.GetUIStatFromInventory(eStat_PsiOffense, CheckState));
}

private function AddStat(string Label, string Value, int Bonus)
{
	local string Rec;

	if (Bonus > 0)
		Value $= "+" $ Bonus;
	else if (Bonus < 0)
		Value $= string(Bonus);

	Rec = "kind=stat";
	AddField(Rec, "label", Label);
	AddField(Rec, "value", Value);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// PopulateSoldierGear: armor, primary, secondary (the Reaper's claymore stands in for its
// secondary, as on screen), two utility pockets, the grenade pocket, then the PCS line.
private function DescribeUnitGear(XComGameState_Unit Unit, XComGameState CheckState)
{
	local XComGameState_Item Item;
	local array<XComGameState_Item> UtilItems;
	local X2EquipmentTemplate Claymore;
	local X2SoldierClassTemplate SoldierClass;
	local string Rec;

	Item = Unit.GetItemInSlot(eInvSlot_Armor, CheckState, true);
	AddGear(class'UITLE_SkirmishModeMenu'.default.m_strArmorLabel, Item);

	Item = Unit.GetItemInSlot(eInvSlot_PrimaryWeapon, CheckState, true);
	AddGear(class'UITLE_SkirmishModeMenu'.default.m_strPrimaryLabel, Item);

	SoldierClass = Unit.GetSoldierClassTemplate();
	if (SoldierClass != none && SoldierClass.DataName == 'Reaper')
	{
		Claymore = X2EquipmentTemplate(class'X2ItemTemplateManager'.static.GetItemTemplateManager().FindItemTemplate('Reaper_Claymore'));
		Rec = "kind=gear";
		AddField(Rec, "label", class'UITLE_SkirmishModeMenu'.default.m_strSecondaryLabel);
		class'X2AccessWire'.static.AddPositionalField(Rec, "item", Claymore != none ? Claymore.GetItemFriendlyName() : "");
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	}
	else
	{
		Item = Unit.GetItemInSlot(eInvSlot_SecondaryWeapon, CheckState, true);
		AddGear(class'UITLE_SkirmishModeMenu'.default.m_strSecondaryLabel, Item);
	}

	UtilItems = Unit.GetAllItemsInSlot(eInvSlot_Utility, CheckState, , true);
	AddGear(class'UITLE_SkirmishModeMenu'.default.m_strUtilLabel, UtilItems.Length > 0 ? UtilItems[0] : none);
	AddGear(class'UITLE_SkirmishModeMenu'.default.m_strUtilLabel, UtilItems.Length > 1 ? UtilItems[1] : none);

	Item = Unit.GetItemInSlot(eInvSlot_GrenadePocket, CheckState, true);
	AddGear(class'UITLE_SkirmishModeMenu'.default.m_strUtilLabel, Item);

	// The PCS line: the implant's name, or an empty slot the screen greys.
	Item = Unit.GetItemInSlot(eInvSlot_CombatSim, CheckState, true);
	Rec = "kind=pcs";
	class'X2AccessWire'.static.AddPositionalField(Rec, "item", Item != none ? Item.GetMyTemplate().GetItemFriendlyName(Item.ObjectID) : "");
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// One gear slot. POSITIONAL: the slot is emitted with an empty item rather than dropped, so an
// empty pocket keeps its place in front of the grenade.
private function AddGear(string Label, XComGameState_Item Item)
{
	local string Rec;

	Rec = "kind=gear";
	AddField(Rec, "label", Label);
	class'X2AccessWire'.static.AddPositionalField(Rec, "item", Item != none ? Item.GetMyTemplate().GetItemFriendlyNameNoStats() : "");
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// PopulateAbilities: the earned soldier abilities, then the character template's, each with
// the same name and long-description fallbacks the screen uses.
private function DescribeUnitAbilities(XComGameState_Unit Unit, XComGameState CheckState)
{
	local array<SoldierClassAbilityType> AbilityTree;
	local X2AbilityTemplateManager Manager;
	local X2AbilityTemplate Template;
	local X2CharacterTemplate CharacterTemplate;
	local name AbilityName;
	local int i;

	Manager = class'X2AbilityTemplateManager'.static.GetAbilityTemplateManager();
	AbilityTree = Unit.GetEarnedSoldierAbilities();
	for (i = 0; i < AbilityTree.Length; ++i)
	{
		Template = Manager.FindAbilityTemplate(AbilityTree[i].AbilityName);
		if (Template != none && !Template.bDontDisplayInAbilitySummary)
			AddAbility(Template, Unit, CheckState);
	}

	CharacterTemplate = Unit.GetMyTemplate();
	if (CharacterTemplate == none)
		return;
	foreach CharacterTemplate.Abilities(AbilityName)
	{
		Template = Manager.FindAbilityTemplate(AbilityName);
		if (Template != none && !Template.bDontDisplayInAbilitySummary && Template.ConditionsEverValidForUnit(Unit, true))
			AddAbility(Template, Unit, CheckState);
	}
}

private function AddAbility(X2AbilityTemplate Template, XComGameState_Unit Unit, XComGameState CheckState)
{
	local string Rec;

	Rec = "kind=ability";
	AddField(Rec, "name", Template.LocFriendlyName != "" ? Template.LocFriendlyName : string(Template.DataName));
	if (Template.HasLongDescription())
		AddField(Rec, "desc", Template.GetMyLongDescription(, Unit, CheckState));
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// UITLEHub.BuildUnlockString, unchanged: one bronze medal per ladder whose high score reaches
// the ladder's first medal threshold, shown as "N/4". It is a simulated function on the screen
// INSTANCE, so it is re-run here rather than called there.
private function string BuildUnlockString(XComOnlineProfileSettings Profile)
{
	local int BronzeMedals, i;
	local int BronzeScore, HighScore;

	BronzeMedals = 0;

	for (i = 1; i <= 4; ++i)
	{
		BronzeScore = class'XComGameState_LadderProgress'.static.GetLadderMedalThreshold(i, 0);
		HighScore = Profile.Data.GetLadderHighScore(i);

		if (BronzeScore <= HighScore)
			++BronzeMedals;
	}

	return string(BronzeMedals) $ "/4";
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

// Empty values are DROPPED rather than written as an empty field; here that can only be the
// meter label, and a missing label makes native drop that stat rather than voice a bare number.
private function AddField(out string Rec, string Key, string Value)
{
	class'X2AccessWire'.static.AddField(Rec, Key, Value);
}
