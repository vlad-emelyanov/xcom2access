// X2AccessArmoryReader
// The armory hub (UIArmory_MainMenu) — the words half, plus the two verbs vanilla leaves off the
// keyboard our layer takes. Same contract as X2AccessLoadoutReader: C++ owns the gate, the keys and
// the speech, calls DescribeArmoryMenu() on this class's default object via ProcessEvent from the
// graph recipe's build, and reads NativeResult back right after the call returns.
//
// ⛔ THE HIGHLANDER REWROTE THIS SCREEN (Issue #47), and this reader is written against ITS class:
// eight named UIListItemString fields (CustomizeButton … DismissButton), each row's MCName set to
// `ArmoryMainMenu_<Name>Button`, an OnAccept that re-sends Enter to the list, and an
// `OnArmoryMainMenuUpdate` event any mod may insert rows through. A row is therefore identified by
// its MCName and never by its index, and a row this reader does not know is described as far as
// its retained Text allows and no further.
//
// WHAT NATIVE ALREADY HAS: the rows' Text (InitListItem → SetText, retained — the promote row's
// carries an <img> prefix native strips), bDisabled, the attention icon, the selection, and the
// unit's ObjectID. What this class adds is everything the screen pushes into Flash and forgets:
//   * the soldier header — the same composers the loadout uses (X2AccessHelper), plus the extended
//     panel this screen shows (bond, combat intelligence, ability points) mirrored from
//     UISoldierHeader.ShowExtendedData branch for branch;
//   * each row's DESCRIPTION (OnSelectionChanged writes it straight into a Flash child) and, for
//     a greyed row, the REASON — SetDisabled parks it in the tooltip manager, so the cascades in
//     UpdateData (GetPCSAvailability, GetWeaponUpgradeAvailability, the rookie promote lock) are
//     re-run here against the same data;
//   * the ability / trait summary panel on the right — UIUtilities_Strategy.PopulateAbilitySummary
//     and _Traits are MC ops on the screen with nothing retained, mirrored entry for entry.
//
// The two verbs: the soldier cycle (Tab / LeftShift in vanilla; E / Q under our XComInput.ini,
// which the game handles natively underneath the layer — this verb is the spinner's copy) through
// the screen's own NextSoldier/PrevSoldier behind the screen's own gate, and the abilities/traits
// toggle, which on mouse+keyboard is a nav-help button and on a gamepad FXS_BUTTON_X — no key.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessArmoryReader extends Object
	dependson(UIUtilities_Strategy, X2SoldierClassTemplate);

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants stops:
//   kind=header   who  status  stats  bond  intel  ap  cycle=1
//   kind=row      key=<MCName>  desc  reason
//   kind=summary  title  toggle                 (absent when the game hides the panel)
//   kind=ability  index  name  desc             (the panel's entries, in its own order)
function DescribeArmoryMenu()
{
	local UIArmory_MainMenu Screen;
	local XComGameState_Unit Unit;

	NativeResult = "";

	Screen = UIArmory_MainMenu(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	Unit = Screen.GetUnit();
	if (Unit == none)
		return;

	AppendHeader(Screen, Unit);
	AppendRows(Screen, Unit);
	AppendSummary(Screen, Unit);
}

// ---------------------------------------------------------------------------------------
// The soldier header
// ---------------------------------------------------------------------------------------

private function AppendHeader(UIArmory_MainMenu Screen, XComGameState_Unit Unit)
{
	local string Rec, StatusDesc, TimeLabel, TimeValue;

	Rec = "kind=header";

	AddField(Rec, "who", class'X2AccessHelper'.static.DescribeArmorySoldier(Unit));

	if (Unit.IsAlive())
	{
		class'UIUtilities_Strategy'.static.GetPersonnelStatusSeparate(Unit, StatusDesc,
			TimeLabel, TimeValue, , true);
		if (TimeValue != "")
			StatusDesc @= TimeValue @ TimeLabel;
		AddField(Rec, "status", StatusDesc);
	}

	AddField(Rec, "stats", class'X2AccessHelper'.static.DescribeArmoryStats(Unit, Screen.CheckGameState));

	AppendExtendedHeader(Screen, Unit, Rec);

	// The base class's own gate for Tab / LeftShift, term for term (UIArmory.OnUnrealCommand).
	if (Screen.m_bAllowAbilityToCycle
		&& class'XComGameState_HeadquartersXCom'.static.IsObjectiveCompleted('T0_M2_WelcomeToArmory')
		&& Screen.IsAllowedToCycleSoldiers()
		&& class'UIUtilities_Strategy'.static.HasSoldiersToCycleThrough(Screen.UnitReference, CanCycleTo))
	{
		Rec $= FLD_SEP $ "cycle=1";
	}

	Emit(Rec);
}

// UISoldierHeader.ShowExtendedData, branch for branch: the bond line, combat intelligence and the
// ability points — the panel this screen shows (bShowExtendedHeaderData) and the loadout does not.
// Hidden as a whole for a class that cannot bond and during the tutorial; the two numeric lines
// hidden again for a unit outside the will system.
private function AppendExtendedHeader(UIArmory_MainMenu Screen, XComGameState_Unit Unit, out string Rec)
{
	local UISoldierHeader Header;
	local SoldierBond BondData;
	local StateObjectReference BondmateRef;
	local XComGameState_Unit Bondmate;
	local XComGameState_HeadquartersXCom XComHQ;
	local string Bond;

	Header = Screen.Header;
	if (Header == none || Unit.GetSoldierClassTemplate() == none)
		return;
	if (!Unit.GetSoldierClassTemplate().bCanHaveBonds || Header.bAlwaysHideXPackPanel
		|| !class'XComGameState_HeadquartersXCom'.static.IsObjectiveCompleted('T0_M2_WelcomeToArmory'))
		return;

	if (Unit.HasSoldierBond(BondmateRef, BondData))
	{
		Bondmate = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(BondmateRef.ObjectID));
		Bond = Header.m_strBondStatus @ string(BondData.BondLevel);
		if (Bondmate != none)
			class'X2AccessHelper'.static.AppendComma(Bond, Bondmate.GetName(eNameType_Full));
	}
	else
	{
		Bond = Header.m_strUnbondedLabel;
	}
	AddField(Rec, "bond", Bond);

	if (!Unit.UsesWillSystem())
		return;

	AddField(Rec, "intel", Header.m_strCombatIntel @ Unit.GetCombatIntelligenceLabel());

	XComHQ = class'UIUtilities_Strategy'.static.GetXComHQ();
	if (Unit.IsResistanceHero() || XComHQ.HasFacilityByName('RecoveryCenter'))
		AddField(Rec, "ap", Header.m_strSoldierAP @ string(Unit.AbilityPoints));
	else
		AddField(Rec, "ap", Header.m_strSoldierAP @ Header.m_strSoldierAPNotUnlocked);
}

// The eligibility delegate HasSoldiersToCycleThrough wants, answering with the screen's own
// static test rather than a copy of it.
function bool CanCycleTo(XComGameState_Unit Unit)
{
	return class'UIArmory'.static.CanCycleTo(Unit);
}

// ---------------------------------------------------------------------------------------
// The rows
// ---------------------------------------------------------------------------------------

// One record per row the list holds, keyed on the row's MCName — the Highlander's stable identity
// for its eight and the only one a mod-inserted row has. The description is
// OnSelectionChanged's own mapping; the reason is the tooltip UpdateData handed SetDisabled.
private function AppendRows(UIArmory_MainMenu Screen, XComGameState_Unit Unit)
{
	local UIListItemString Row;
	local string Rec;
	local int i;

	if (Screen.List == none)
		return;

	for (i = 0; i < Screen.List.ItemCount; ++i)
	{
		Row = UIListItemString(Screen.List.GetItem(i));
		if (Row == none)
			continue;

		Rec = "kind=row" $ FLD_SEP $ "key=" $ string(Row.MCName);
		AddField(Rec, "desc", RowDescription(Screen, Unit, Row));
		if (Row.bDisabled)
			AddField(Rec, "reason", RowReason(Screen, Unit, Row));
		Emit(Rec);
	}
}

// OnSelectionChanged's switch, on the same object identities.
private function string RowDescription(UIArmory_MainMenu Screen, XComGameState_Unit Unit, UIListItemString Row)
{
	local string CustomizeDesc;

	if (Row == Screen.CustomizeButton)
	{
		CustomizeDesc = Unit.GetMyTemplate().strCustomizeDesc;
		return CustomizeDesc != "" ? CustomizeDesc : Screen.m_strCustomizeSoldierDesc;
	}
	if (Row == Screen.LoadoutButton)
		return Screen.m_strLoadoutDesc;
	if (Row == Screen.PCSButton)
		return Screen.m_strImplantsDesc;
	if (Row == Screen.WeaponUpgradeButton)
		return Screen.m_strCustomizeWeaponDesc;
	if (Row == Screen.PromotionButton)
		return Screen.m_strPromoteDesc;
	if (Row == Screen.PropagandaButton)
		return Screen.m_strPropagandaDesc;
	if (Row == Screen.SoldierBondsButton)
		return Screen.m_strSoldierBondsDesc;
	if (Row == Screen.DismissButton)
		return Screen.m_strDismissDesc;
	return "";
}

// UpdateData's tooltip cascades, re-run: SetDisabled parked the words in the tooltip manager and
// the row keeps only the bit. The tutorial-locked rows are greyed with an EMPTY tooltip in the
// game and say nothing here either.
private function string RowReason(UIArmory_MainMenu Screen, XComGameState_Unit Unit, UIListItemString Row)
{
	local TPCSAvailabilityData PCS;
	local TWeaponUpgradeAvailabilityData Weapon;

	if (Row == Screen.PCSButton)
	{
		class'UIUtilities_Strategy'.static.GetPCSAvailability(Unit, PCS);
		if (!PCS.bHasAchievedCombatSimsRank)
			return Screen.m_strInsufficientRankForImplantsTooltip;
		if (!PCS.bCanEquipCombatSims)
			return Screen.m_strCantEquiqPCSTooltip;
		if (!PCS.bHasCombatSimsSlotsAvailable)
			return Screen.m_strCombatSimsSlotsFull;
		if (!PCS.bHasNeurochipImplantsInInventory)
			return Screen.m_strNoImplantsTooltip;
		if (!PCS.bHasGTS)
			return Screen.m_strNoGTSTooltip;
		return "";
	}
	if (Row == Screen.WeaponUpgradeButton)
	{
		class'UIUtilities_Strategy'.static.GetWeaponUpgradeAvailability(Unit, Weapon);
		if (!Weapon.bHasModularWeapons)
			return Screen.m_strNoModularWeaponsTooltip;
		if (!Weapon.bCanWeaponBeUpgraded)
			return Screen.m_strCannotUpgradeWeaponTooltip;
		if (!Weapon.bHasWeaponUpgrades)
			return Screen.m_strNoWeaponUpgradesTooltip;
		return "";
	}
	if (Row == Screen.PromotionButton)
	{
		if (Unit.GetRank() < 1 && !Unit.CanRankUpSoldier())
			return Screen.m_strRookiePromoteTooltip;
		return "";
	}
	return "";
}

// ---------------------------------------------------------------------------------------
// The ability / trait summary panel
// ---------------------------------------------------------------------------------------

// RefreshAbilitySummary, mirrored: traits when the screen is listing them and the soldier has
// any, else abilities — and nothing at all for a rookie, where the game hides the panel. The
// toggle label is UpdateNavHelp's own, offered only when the soldier has a trait to toggle to.
private function AppendSummary(UIArmory_MainMenu Screen, XComGameState_Unit Unit)
{
	local string Rec;
	local bool bTraits;

	if (Unit.GetRank() == 0)
		return;

	bTraits = Screen.bListingTraits && Unit.AcquiredTraits.Length > 0;

	Rec = "kind=summary";
	AddField(Rec, "title", bTraits ? class'UIUtilities_Strategy'.default.m_strTraitListTitle
								   : class'UIUtilities_Strategy'.default.m_strAbilityListTitle);
	if (Unit.AcquiredTraits.Length > 0 && Screen.bUseNavHelp)
		AddField(Rec, "toggle", Screen.bListingTraits ? Screen.m_strToggleAbilities
													  : Screen.m_strToggleTraits);
	Emit(Rec);

	if (bTraits)
		AppendTraits(Unit);
	else
		AppendAbilities(Screen, Unit);
}

// PopulateAbilitySummary's own order: the PCS in the combat-sim slots, then the earned class
// abilities, then the character template's abilities that are ever valid for this unit. The
// non-soldier branch is left out — nothing the armory hub opens on is not a soldier.
private function AppendAbilities(UIArmory_MainMenu Screen, XComGameState_Unit Unit)
{
	local array<XComGameState_Item> PCSItems;
	local XComGameState_Item Item;
	local array<SoldierClassAbilityType> AbilityTree;
	local X2AbilityTemplateManager Mgr;
	local X2AbilityTemplate Ability;
	local X2CharacterTemplate CharacterTemplate;
	local name AbilityName;
	local int i, Index;

	Index = 0;

	PCSItems = Unit.GetAllItemsInSlot(eInvSlot_CombatSim);
	foreach PCSItems(Item)
	{
		EmitAbility(Index++, Item.GetMyTemplate().GetItemFriendlyName(Item.ObjectID),
			Item.GetMyTemplate().GetItemBriefSummary(Item.ObjectID));
	}

	if (!Unit.IsSoldier())
		return;

	Mgr = class'X2AbilityTemplateManager'.static.GetAbilityTemplateManager();
	AbilityTree = Unit.GetEarnedSoldierAbilities();
	for (i = 0; i < AbilityTree.Length; ++i)
	{
		Ability = Mgr.FindAbilityTemplate(AbilityTree[i].AbilityName);
		if (Ability != none && !Ability.bDontDisplayInAbilitySummary)
			EmitAbilityTemplate(Index++, Ability, Unit, Screen.CheckGameState);
	}

	CharacterTemplate = Unit.GetMyTemplate();
	foreach CharacterTemplate.Abilities(AbilityName)
	{
		Ability = Mgr.FindAbilityTemplate(AbilityName);
		if (Ability != none && !Ability.bDontDisplayInAbilitySummary
			&& Ability.ConditionsEverValidForUnit(Unit, true))
		{
			EmitAbilityTemplate(Index++, Ability, Unit, Screen.CheckGameState);
		}
	}
}

// AddAbilityToSummary's two strings. The game's "Missing 'LocFriendlyName'" placeholders are
// developer text and are not reproduced; an unnamed ability is simply skipped.
private function EmitAbilityTemplate(int Index, X2AbilityTemplate Ability, XComGameState_Unit Unit,
	XComGameState CheckGameState)
{
	if (Ability.LocFriendlyName == "")
		return;
	EmitAbility(Index, Ability.LocFriendlyName,
		Ability.HasLongDescription() ? Ability.GetMyLongDescription(, Unit, CheckGameState) : "");
}

// PopulateAbilitySummary_Traits, entry for entry.
private function AppendTraits(XComGameState_Unit Unit)
{
	local X2EventListenerTemplateManager Mgr;
	local X2TraitTemplate Trait;
	local name TraitName;
	local int Index;

	Mgr = class'X2EventListenerTemplateManager'.static.GetEventListenerTemplateManager();
	Index = 0;
	foreach Unit.AcquiredTraits(TraitName)
	{
		Trait = X2TraitTemplate(Mgr.FindEventListenerTemplate(TraitName));
		if (Trait == none || Trait.TraitFriendlyName == "")
			continue;
		EmitAbility(Index++, Trait.TraitFriendlyName, Trait.TraitDescription);
	}
}

private function EmitAbility(int Index, string AbilityName, string Desc)
{
	local string Rec;

	Rec = "kind=ability" $ FLD_SEP $ "index=" $ Index;
	AddField(Rec, "name", AbilityName);
	AddField(Rec, "desc", Desc);
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// Verbs
// ---------------------------------------------------------------------------------------

// Vanilla's Tab / LeftShift (E / Q under our config), behind the screen's own gate. NextSoldier/PrevSoldier re-point every
// armory screen on the stack at the new soldier and re-populate this one.
function CycleArmorySoldier(int Direction)
{
	local UIArmory_MainMenu Screen;

	NativeResult = "";

	Screen = UIArmory_MainMenu(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;
	if (!Screen.m_bAllowAbilityToCycle
		|| !class'XComGameState_HeadquartersXCom'.static.IsObjectiveCompleted('T0_M2_WelcomeToArmory')
		|| !Screen.IsAllowedToCycleSoldiers()
		|| !class'UIUtilities_Strategy'.static.HasSoldiersToCycleThrough(Screen.UnitReference, CanCycleTo))
	{
		return;
	}

	if (Direction > 0)
		Screen.NextSoldier();
	else
		Screen.PrevSoldier();
}

// The abilities/traits toggle — a nav-help button on mouse+keyboard, FXS_BUTTON_X on a gamepad,
// and no key at all. The screen's own function, which refuses without nav help exactly as the
// button would not exist.
function ToggleArmoryTraits(int Unused)
{
	local UIArmory_MainMenu Screen;

	NativeResult = "";

	Screen = UIArmory_MainMenu(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;
	Screen.ToggleAbilitiesAndTraits();
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

private function Emit(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

private function AddField(out string Rec, string Key, string Value)
{
	class'X2AccessWire'.static.AddField(Rec, Key, Value);
}

defaultproperties
{
	NativeResult = ""
}
