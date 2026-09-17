// X2AccessSkirmishReader
// The Tactical Legacy Pack's Skirmish Mode menu (UITLE_SkirmishModeMenu) - the panel half. Same
// contract as X2AccessSaveReader and X2AccessTLEReader: the C++ layer owns the gate, the keys and
// the speech, calls DescribeSkirmish() on this class's default object via ProcessEvent (game
// thread, from the graph recipe's build) and reads NativeResult back through reflection right
// after the call returns. Nothing here runs on its own - no listeners, no timers, no polling.
//
// WHAT NATIVE READS AND WHAT THIS SUPPLIES. The list on the left is UIMechaListItems, which
// retain their label and value, so native reads every row itself. The panel on the right is
// pushed into Flash by UpdateGeneralMissionInfoPanel, UpdateDetailsGeneric, UpdateDataSoldierData
// (with SetSoldierStats and SetSoldierGear), UpdateAbilityInfo and UpdateEnemyPortraits, and
// retained nowhere - but every one of those functions reads the screen's OWN cached objects:
// m_CurrentSquad, BattleDataState, ChallengeMissionSite, CampaignSettings, m_SelectedSoldier,
// m_SelectedEnemy, and the rows' metadata. So every line below is that function's arithmetic
// over those same objects, in the order it pushes them - no XCOMHISTORY, no re-selection, and
// none of the state MUTATION UpdateEnemyPortraits performs on the way (SelectEnemyForces): this
// reader asks GetPossibleEnemiesInPod, which is the query half, and nothing else.
//
// WHICH PANEL the screen is drawing is decided from m_CurrentState the way the screen decides
// it: the mission panel on the base menu and the map pages, the squad blurb or one soldier on
// the squad page (RefreshSquadDetailsPanel's own test), the soldier on every soldier page and
// picker, the selected ability's text on the abilities page, the opponent pod on the enemies
// page.
//
// GC rule: this object is a CDO - value-typed state only, NEVER object references.

class X2AccessSkirmishReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

function DescribeSkirmish()
{
	local UITLE_SkirmishModeMenu Screen;

	NativeResult = "";

	Screen = UITLE_SkirmishModeMenu(`SCREENSTACK.GetFirstInstanceOf(class'UITLE_SkirmishModeMenu'));
	if (Screen == none || Screen.BattleDataState == none)
		return;

	switch (Screen.m_CurrentState)
	{
	case eUISkirmishScreen_Base:
	case eUISkirmishScreen_MapData:
	case eUISkirmishScreen_Map:
	case eUISkirmishScreen_Mission:
	case EUISkirmishScreen_Biome:
	case eUISkirmishScreen_Difficulty:
		DescribeMissionPanel(Screen);
		break;

	case eUISkirmishScreen_Squad:
		// RefreshSquadDetailsPanel: the blurb on the ADD NEW UNIT row (only there while the
		// squad has room) or with nothing selected; a soldier otherwise.
		if ((Screen.m_CurrentSquad.Length < 6 && Screen.List.SelectedIndex == 0) || Screen.List.SelectedIndex < 0)
			DescribeSquadInfo(Screen);
		else
			DescribeSoldierPanel(Screen);
		break;

	case eUISkirmishScreen_SoldierAbilities:
		DescribeAbilities(Screen);
		break;

	case eUISkirmishScreen_Enemies:
		DescribePod(Screen);
		break;

	default:
		// Soldier data, equipment and every picker under them leave the soldier panel up.
		DescribeSoldierPanel(Screen);
		break;
	}
}

// ---------------------------------------------------------------------------------------
// The mission panel (UpdateGeneralMissionInfoPanel)
// ---------------------------------------------------------------------------------------

private function DescribeMissionPanel(UITLE_SkirmishModeMenu Screen)
{
	local X2MissionTemplateManager MissionTemplateManager;
	local X2MissionTemplate MissionTemplate;
	local array<X2ChallengeTemplate> Templates;
	local X2ChallengeEnemyForces Forces;
	local XComGameState_Unit Unit;
	local string Rec;
	local int i;

	MissionTemplateManager = class'X2MissionTemplateManager'.static.GetMissionTemplateManager();
	MissionTemplate = MissionTemplateManager.FindMissionTemplate(Screen.BattleDataState.MapData.ActiveMission.MissionName);
	if (Screen.ChallengeTemplateManager != none)
	{
		Templates = Screen.ChallengeTemplateManager.GetAllTemplatesOfClass(class'X2ChallengeEnemyForces');
		if (Screen.m_SelectedEnemy >= 0 && Screen.m_SelectedEnemy < Templates.Length)
			Forces = X2ChallengeEnemyForces(Templates[Screen.m_SelectedEnemy]);
	}

	Rec = "kind=mission";
	AddField(Rec, "opname", Screen.BattleDataState.m_strOpName);
	AddField(Rec, "enemylabel", Screen.m_EnemyLabel);
	if (Forces != none)
	{
		AddField(Rec, "enemy", class'UIUtilities_Text'.static.CapsCheckForGermanScharfesS(Forces.DisplayName));
		AddField(Rec, "enemydesc", Forces.Description);
	}
	AddField(Rec, "objlabel", Screen.m_ObjectiveLabel);
	if (MissionTemplate != none)
		AddField(Rec, "objective", MissionTemplate.DisplayName);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	// The six soldier slots, filled in squad order; the rank is drawn as an icon and named here.
	for (i = 0; i < Screen.m_CurrentSquad.Length; ++i)
	{
		Unit = Screen.m_CurrentSquad[i];
		if (Unit == none || Unit.bMissionProvided)
			continue;
		Rec = "kind=member";
		AddField(Rec, "index", string(i));
		AddField(Rec, "name", Unit.GetFullName());
		AddField(Rec, "rank", `GET_RANK_STR(Unit.GetRank(), Unit.GetSoldierClassTemplateName()));
		AddField(Rec, "class", class'UIUtilities_Text'.static.CapsCheckForGermanScharfesS(Unit.GetSoldierClassTemplate().DisplayName));
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	}
}

// ---------------------------------------------------------------------------------------
// The squad blurb (UpdateDetailsGeneric)
// ---------------------------------------------------------------------------------------

private function DescribeSquadInfo(UITLE_SkirmishModeMenu Screen)
{
	local X2SoldierClassTemplateManager ClassMgr;
	local X2SoldierClassTemplate SoldierClass;
	local XComGameState_ResistanceFaction FactionState;
	local array<name> ClassNames;
	local string Rec;
	local int i;

	Rec = "kind=squadinfo";
	AddField(Rec, "label", Screen.m_SquadLabel);
	AddField(Rec, "desc", Screen.m_SquadGeneralDescription);
	AddField(Rec, "factionlabel", Screen.m_SoldiersFactionLabel);
	AddField(Rec, "factiondesc", Screen.m_SoldiersFactionDescription);

	// The six class captions, in the order the panel draws them.
	ClassNames.AddItem('Ranger');
	ClassNames.AddItem('Sharpshooter');
	ClassNames.AddItem('Specialist');
	ClassNames.AddItem('Grenadier');
	ClassNames.AddItem('PsiOperative');
	ClassNames.AddItem('Spark');
	ClassMgr = class'X2SoldierClassTemplateManager'.static.GetSoldierClassTemplateManager();
	for (i = 0; i < ClassNames.Length; ++i)
	{
		SoldierClass = ClassMgr.FindSoldierClassTemplate(ClassNames[i]);
		if (SoldierClass != none)
			AddField(Rec, "class", class'UIUtilities_Text'.static.CapsCheckForGermanScharfesS(SoldierClass.DisplayName));
	}

	// The faction crests, captioned with each faction's title - iterated off the screen's own
	// History, which is the start state it built.
	if (Screen.History != none)
	{
		foreach Screen.History.IterateByClassType(class'XComGameState_ResistanceFaction', FactionState)
			AddField(Rec, "faction", class'UIUtilities_Text'.static.CapsCheckForGermanScharfesS(FactionState.GetFactionTitle()));
	}
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// ---------------------------------------------------------------------------------------
// One soldier (UpdateDataSoldierData: SetSoldierData, SetSoldierStats, SetSoldierGear)
// ---------------------------------------------------------------------------------------

private function XComGameState_Unit SelectedSoldier(UITLE_SkirmishModeMenu Screen)
{
	if (Screen.m_SelectedSoldier < 0 || Screen.m_SelectedSoldier >= Screen.m_CurrentSquad.Length)
		return none;
	return Screen.m_CurrentSquad[Screen.m_SelectedSoldier];
}

private function DescribeSoldierPanel(UITLE_SkirmishModeMenu Screen)
{
	local XComGameState_Unit Unit;
	local string Rec;

	Unit = SelectedSoldier(Screen);
	if (Unit == none)
		return;

	Rec = "kind=unit";
	AddField(Rec, "rank", `GET_RANK_STR(Unit.GetSoldierRank(), Unit.GetSoldierClassTemplateName()));
	AddField(Rec, "name", Unit.GetFullName());
	AddField(Rec, "class", Unit.GetSoldierClassTemplate().DisplayName);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	DescribeStats(Screen, Unit);
	DescribeGear(Screen, Unit);
}

// SetSoldierStats, stat for stat and in its order. The value is the base figure (current stat
// plus what abilities add), the will as current/max, and the item bonus appended with its sign
// - the screen colours the bonus and concatenates it; speech gets a space instead of a colour.
private function DescribeStats(UITLE_SkirmishModeMenu Screen, XComGameState_Unit Unit)
{
	AddStat(Screen.m_strHealthLabel, StatValue(Screen, Unit, eStat_HP, false));
	AddStat(Screen.m_strMobilityLabel, StatValue(Screen, Unit, eStat_Mobility, false));
	AddStat(Screen.m_strAimLabel, StatValue(Screen, Unit, eStat_Offense, false));
	AddStat(Screen.m_strWillLabel, StatValue(Screen, Unit, eStat_Will, true));
	AddStat(Screen.m_strArmorLabel, StatValue(Screen, Unit, eStat_ArmorMitigation, false));
	AddStat(Screen.m_strDodgeLabel, StatValue(Screen, Unit, eStat_Dodge, false));
	AddStat(Screen.m_strTechLabel, StatValue(Screen, Unit, eStat_Hacking, false));
	if (Unit.IsPsiOperative())
		AddStat(Screen.m_strPsiLabel, StatValue(Screen, Unit, eStat_PsiOffense, false));
}

private function string StatValue(UITLE_SkirmishModeMenu Screen, XComGameState_Unit Unit, ECharStatType Stat, bool bWithMax)
{
	local int Base, Bonus;
	local string Result;

	Base = int(Unit.GetCurrentStat(Stat)) + Unit.GetUIStatFromAbilities(Stat);
	Result = string(Base);
	if (bWithMax)
		Result $= "/" $ string(int(Unit.GetMaxStat(Stat)));

	Bonus = Unit.GetUIStatFromInventory(Stat, Screen.StartState);
	if (Bonus > 0)
		Result $= " +" $ string(Bonus);
	else if (Bonus < 0)
		Result $= " " $ string(Bonus);
	return Result;
}

private function AddStat(string Label, string Value)
{
	local string Rec;

	Rec = "kind=stat";
	AddField(Rec, "label", Label);
	AddField(Rec, "value", Value);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// SetSoldierGear plus the heavy-weapon and PCS pushes that follow it, slot for slot in its
// order. A slot the screen draws nothing for (no second utility item, no heavy weapon, no PCS)
// sends nothing: the recipe keys each line on its label, so an absent slot cannot shift another.
private function DescribeGear(UITLE_SkirmishModeMenu Screen, XComGameState_Unit Unit)
{
	local XComGameState_Item Item;
	local array<XComGameState_Item> UtilItems;
	local X2EquipmentTemplate ClaymoreTemplate;

	Item = Unit.GetItemInSlot(eInvSlot_Armor, Screen.StartState, true);
	if (Item != none)
		AddGear(Screen.m_strArmorLabel, Item.GetMyTemplate().GetItemFriendlyNameNoStats());

	Item = Unit.GetItemInSlot(eInvSlot_PrimaryWeapon, Screen.StartState, true);
	if (Item != none)
		AddGear(Screen.m_strPrimaryLabel, Item.GetMyTemplate().GetItemFriendlyNameNoStats());

	// The Reaper's claymore is drawn from its template rather than an inventory item.
	if (Unit.GetSoldierClassTemplateName() == 'Reaper')
	{
		ClaymoreTemplate = X2EquipmentTemplate(class'X2ItemTemplateManager'.static.GetItemTemplateManager().FindItemTemplate('Reaper_Claymore'));
		if (ClaymoreTemplate != none)
			AddGear(Screen.m_strSecondaryLabel, ClaymoreTemplate.GetItemFriendlyName());
	}
	else
	{
		Item = Unit.GetItemInSlot(eInvSlot_SecondaryWeapon, Screen.StartState, true);
		if (Item != none)
			AddGear(Screen.m_strSecondaryLabel, Item.GetMyTemplate().GetItemFriendlyNameNoStats());
	}

	UtilItems = Unit.GetAllItemsInSlot(eInvSlot_Utility, Screen.StartState, , true);
	if (UtilItems.Length > 0 && UtilItems[0] != none)
		AddGear(Screen.m_strUtilLabel, UtilItems[0].GetMyTemplate().GetItemFriendlyNameNoStats());
	if (UtilItems.Length > 1 && UtilItems[1] != none)
		AddGear(Screen.m_strUtilLabel, UtilItems[1].GetMyTemplate().GetItemFriendlyNameNoStats());

	Item = Unit.GetItemInSlot(eInvSlot_GrenadePocket, Screen.StartState, true);
	if (Item != none)
		AddGear(Screen.m_GrenadierGrenade, Item.GetMyTemplate().GetItemFriendlyNameNoStats());

	Item = Unit.GetItemInSlot(eInvSlot_HeavyWeapon, Screen.StartState, true);
	if (Item != none && Unit.HasHeavyWeapon())
		AddGear(Screen.m_HeavyWeaponLabel, Item.GetMyTemplate().GetItemFriendlyNameNoStats());

	Item = Unit.GetItemInSlot(eInvSlot_CombatSim, Screen.StartState, true);
	if (Item != none)
		AddGear(Screen.m_PCSLabel, Item.GetMyTemplate().GetItemFriendlyName(Item.ObjectID));
}

private function AddGear(string Label, string ItemName)
{
	local string Rec;

	Rec = "kind=gear";
	AddField(Rec, "label", Label);
	AddField(Rec, "item", ItemName);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// ---------------------------------------------------------------------------------------
// The abilities page (UpdateAbilityInfo)
// ---------------------------------------------------------------------------------------

// One record per row with that row's ability text (for Space on any row), and the selected
// row's as the panel record - both through the same mirror of UpdateAbilityInfo, so the two
// can never disagree.
private function DescribeAbilities(UITLE_SkirmishModeMenu Screen)
{
	local UIMechaListItem ListItem;
	local string Rec;
	local int i;

	for (i = 0; i < Screen.List.ItemCount; ++i)
	{
		ListItem = UIMechaListItem(Screen.List.GetItem(i));
		if (ListItem == none)
			continue;
		Rec = "kind=abil";
		AddField(Rec, "index", string(i));
		DescribeAbilityFor(Screen, ListItem, Rec);
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

		if (i == Screen.List.SelectedIndex)
		{
			Rec = "kind=ability";
			DescribeAbilityFor(Screen, ListItem, Rec);
			class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
		}
	}
}

// UpdateAbilityInfo's resolution, including its fallback for an AWC row: the row's metadata
// names a rank and branch in the soldier's ability tree, and a branch that resolves to no
// template (the AWC rows carry a negative metadataInt) falls back to the first unlock template
// the class allows - which is what the panel shows, right or wrong.
private function DescribeAbilityFor(UITLE_SkirmishModeMenu Screen, UIMechaListItem ListItem, out string Rec)
{
	local XComGameState_Unit Unit;
	local X2SoldierClassTemplate SoldierClassTemplate;
	local array<SoldierRankAbilities> AbilityTree;
	local X2AbilityTemplate AbilityTemplate;
	local X2AbilityTemplateManager AbilityTemplateManager;
	local X2StrategyElementTemplateManager TemplateMan;
	local array<X2StrategyElementTemplate> AWCUnlockTemplates;
	local X2SoldierAbilityUnlockTemplate Template;
	local UISummary_Ability AbilityData;
	local int TreeIndex, AbilityIndex, j;

	Unit = SelectedSoldier(Screen);
	if (Unit == none)
		return;

	SoldierClassTemplate = class'X2SoldierClassTemplateManager'.static.GetSoldierClassTemplateManager().FindSoldierClassTemplate(Unit.GetSoldierClassTemplateName());
	TreeIndex = int(ListItem.metadataString);
	AbilityIndex = ListItem.metadataInt;
	AbilityTree = Unit.AbilityTree;
	AbilityTemplateManager = class'X2AbilityTemplateManager'.static.GetAbilityTemplateManager();

	if (TreeIndex >= 0 && TreeIndex < AbilityTree.Length && AbilityIndex >= 0 && AbilityIndex < AbilityTree[TreeIndex].Abilities.Length)
		AbilityTemplate = AbilityTemplateManager.FindAbilityTemplate(AbilityTree[TreeIndex].Abilities[AbilityIndex].AbilityName);

	if (AbilityTemplate == none && SoldierClassTemplate != none)
	{
		TemplateMan = class'X2StrategyElementTemplateManager'.static.GetStrategyElementTemplateManager();
		AWCUnlockTemplates = TemplateMan.GetAllTemplatesOfClass(class'X2SoldierAbilityUnlockTemplate');
		for (j = 0; j < AWCUnlockTemplates.Length; ++j)
		{
			Template = X2SoldierAbilityUnlockTemplate(AWCUnlockTemplates[j]);
			if (Template.AllowedClasses.Find(SoldierClassTemplate.DataName) != INDEX_NONE)
			{
				AbilityTemplate = AbilityTemplateManager.FindAbilityTemplate(Template.AbilityName);
				break;
			}
		}
	}

	if (AbilityTemplate == none)
		return;

	AbilityData = AbilityTemplate.GetUISummary_Ability();
	AddField(Rec, "name", AbilityData.Name);
	AddField(Rec, "desc", AbilityData.Description);
}

// ---------------------------------------------------------------------------------------
// The opponent pod (UpdateEnemyPortraits, the query half only)
// ---------------------------------------------------------------------------------------

private function DescribePod(UITLE_SkirmishModeMenu Screen)
{
	local array<X2ChallengeTemplate> Templates;
	local X2ChallengeEnemyForces Forces;
	local X2CharacterTemplateManager CharMgr;
	local X2CharacterTemplate Enemy, Other;
	local array<name> EnemyTemplates;
	local string Rec;
	local int i;

	if (Screen.ChallengeTemplateManager == none)
		return;
	Templates = Screen.ChallengeTemplateManager.GetAllTemplatesOfClass(class'X2ChallengeEnemyForces');
	if (Screen.m_SelectedEnemy < 0 || Screen.m_SelectedEnemy >= Templates.Length)
		return;
	Forces = X2ChallengeEnemyForces(Templates[Screen.m_SelectedEnemy]);
	if (Forces == none)
		return;

	Rec = "kind=pod";
	AddField(Rec, "name", Forces.DisplayName);
	AddField(Rec, "desc", Forces.Description);

	// The portraits, grouped the way the panel groups them: each character, then every other
	// of its character group pulled up beside it.
	Screen.GetPossibleEnemiesInPod(Forces.DataName, EnemyTemplates);
	CharMgr = class'X2CharacterTemplateManager'.static.GetCharacterTemplateManager();
	while (EnemyTemplates.Length > 0)
	{
		Enemy = CharMgr.FindCharacterTemplate(EnemyTemplates[0]);
		EnemyTemplates.Remove(0, 1);
		if (Enemy == none)
			continue;
		AddField(Rec, "enemy", Enemy.strCharacterName);

		i = 0;
		while (i < EnemyTemplates.Length)
		{
			Other = CharMgr.FindCharacterTemplate(EnemyTemplates[i]);
			if (Other != none && Other.CharacterGroupName == Enemy.CharacterGroupName)
			{
				EnemyTemplates.Remove(i, 1);
				AddField(Rec, "enemy", Other.strCharacterName);
			}
			else
			{
				++i;
			}
		}
	}
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

// Empty values are DROPPED rather than written as an empty field: a missing key means "the
// panel is not showing this". The stat and gear records are keyed pairs, never positional, so
// nothing here needs the positional variant.
private function AddField(out string Rec, string Key, string Value)
{
	class'X2AccessWire'.static.AddField(Rec, Key, Value);
}
