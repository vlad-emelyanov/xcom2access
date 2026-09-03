// X2AccessWeaponUpgradeReader
// The weapon-upgrade screen (UIArmory_WeaponUpgrade) and its pattern picker (UIArmory_WeaponTrait)
// — the words half. Same contract as X2AccessLoadoutReader: C++ owns the gate, the keys and the
// speech, calls DescribeWeaponUpgrade() on this class's default object via ProcessEvent from the
// graph recipe's build, and reads NativeResult back right after the call returns.
//
// WHAT NATIVE ALREADY HAS: the two upgrade lists' rows retain their words — UIArmory_WeaponUpgradeItem
// keeps title (the upgrade's name, or EMPTY SLOT), subTitle (GetUpgradeEffectForUI, the "+N"
// effect line), Count and its three bits — and the customize rows are ordinary UIMechaListItems.
// What this class adds is everything the screen pushes into Flash and forgets:
//   * the two panel titles and the slots counter ("N/M" under SLOTS AVAILABLE), the weapon's name
//     and whose it is — locals in UpdateSlots / UpdateOwnerSoldier / SetWeaponReference;
//   * the UPGRADE DESCRIPTION BOX — the only place an upgrade's prose appears, pushed by
//     PreviewUpgrade for whichever row is selected and retained nowhere;
//   * the WEAPON STAT BLOCK — UIArmory_WeaponUpgradeStats is a class with no members at all, and
//     PreviewUpgrade feeds it the stats of a PENDING change state with the selected upgrade applied.
//     That preview is reproduced here line for line (a throwaway change state, never submitted),
//     because it is what a sighted player is looking at while they arrow through the list;
//   * a greyed row's reason — `disabledText` is declared on the row and never assigned, and this
//     screen does not even park the reason in a tooltip: it goes straight into Flash;
//   * the count sentence around a row's Count (UIArmory_LoadoutItem.m_strCount, the loadout's).
//
// ⛔ THE HIGHLANDER ADDS WeaponViewOffsets AT THE HEAD OF THE CLASS (Issue #832), and Issue #93
// routes the slot count through XComGameState_Item.GetNumUpgradeSlots. Both halves are written
// against the Highlander the mod requires.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessWeaponUpgradeReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

const FLD_SEP = "~:~";

// Records, in the order the recipe wants stops:
//   kind=header   who  weapon  equipped  slots  lefttitle  righttitle  customtitle  cycle=1
//   kind=row      list=slots|upgrades  index  count  reason
//   kind=preview  name  desc                  (the description box, for the selected row's upgrade)
//   kind=stat     index  label  value         (the stat block as previewed for that selection)
//   kind=trait    title                       (the pattern picker's heading; nothing else on it)
function DescribeWeaponUpgrade()
{
	local UIArmory_WeaponUpgrade Screen;
	local UIArmory_WeaponTrait Trait;
	local XComGameState_Item Weapon;

	NativeResult = "";

	Screen = UIArmory_WeaponUpgrade(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	// The pattern picker is a subclass that reuses SlotsList as a plain string list; its rows
	// retain their Text and native reads them. Only its heading is wanted from here.
	Trait = UIArmory_WeaponTrait(Screen);
	if (Trait != none)
	{
		AppendTraitHeader(Trait);
		return;
	}

	Weapon = XComGameState_Item(`XCOMHISTORY.GetGameStateForObjectID(Screen.WeaponRef.ObjectID));
	if (Weapon == none)
		return;

	AppendHeader(Screen, Weapon);
	AppendRows(Screen, Weapon);
	AppendPreview(Screen, Weapon);
}

// ---------------------------------------------------------------------------------------
// The header: the soldier, the weapon, the counter, the three panel titles
// ---------------------------------------------------------------------------------------

private function AppendHeader(UIArmory_WeaponUpgrade Screen, XComGameState_Item Weapon)
{
	local XComGameState_Unit Unit, Owner;
	local XGParamTag LocTag;
	local string Rec, WeaponName;
	local int NumUpgradeSlots, SlotsAvailable;

	Rec = "kind=header";

	// InitArmory accepts a unit OR a weapon ref; a weapon-opened screen has no unit to name.
	Unit = Screen.GetUnit();
	if (Unit != none)
		AddField(Rec, "who", class'X2AccessHelper'.static.DescribeArmorySoldier(Unit));

	// The customize title's form — the friendly name WITH the nickname (SetWeaponName's banner
	// passes no ObjectID and so shows the template name; the name a player gave it matters more).
	WeaponName = Weapon.GetMyTemplate().GetItemFriendlyName(Weapon.ObjectID);
	AddField(Rec, "weapon", WeaponName);

	// UpdateOwnerSoldier, branch for branch.
	Owner = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Weapon.OwnerStateObject.ObjectID));
	if (Owner != none)
		AddField(Rec, "equipped", Screen.m_strWeaponEquippedOn @ Owner.GetName(eNameType_Full));
	else
		AddField(Rec, "equipped", Screen.m_strWeaponNotEquipped);

	// UpdateSlots' own arithmetic (Issue #93's GetNumUpgradeSlots, then the two continent bonuses).
	NumUpgradeSlots = Weapon.GetNumUpgradeSlots();
	if (`XCOMHQ.bExtraWeaponUpgrade)
		NumUpgradeSlots++;
	if (`XCOMHQ.ExtraUpgradeWeaponCats.Find(X2WeaponTemplate(Weapon.GetMyTemplate()).WeaponCat) != INDEX_NONE)
		NumUpgradeSlots++;
	SlotsAvailable = NumUpgradeSlots - Weapon.GetMyWeaponUpgradeTemplates().Length;
	AddField(Rec, "slots", Screen.m_strSlotsAvailable @ (SlotsAvailable $ "/" $ NumUpgradeSlots));

	LocTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
	LocTag.StrValue0 = class'UIUtilities_Text'.static.CapsCheckForGermanScharfesS(WeaponName);
	AddField(Rec, "lefttitle", `XEXPAND.ExpandString(SlotsAvailable == 0
		? Screen.m_strWeaponFullyUpgraded : Screen.m_strUpgradeWeapon));

	if (Screen.UpgradesList != none)
		AddField(Rec, "righttitle", Screen.UpgradesList.ItemCount == 0
			? Screen.m_strNoUpgradeAvailable : Screen.m_strSelectUpgrade);

	LocTag.StrValue0 = Caps(WeaponName);
	AddField(Rec, "customtitle", `XEXPAND.ExpandString(Screen.m_strCustomizeWeaponTitle));

	// The base class's own gate for Tab / LeftShift, with this screen's narrower CanCycleTo.
	if (Unit != none && Screen.m_bAllowAbilityToCycle
		&& class'XComGameState_HeadquartersXCom'.static.IsObjectiveCompleted('T0_M2_WelcomeToArmory')
		&& Screen.IsAllowedToCycleSoldiers()
		&& class'UIUtilities_Strategy'.static.HasSoldiersToCycleThrough(Screen.UnitReference, CanCycleTo))
	{
		Rec $= FLD_SEP $ "cycle=1";
	}

	Emit(Rec);
}

// The eligibility delegate HasSoldiersToCycleThrough wants — the screen's own static test, which
// narrows the armory's to soldiers with an upgradable weapon and an upgrade to fit.
function bool CanCycleTo(XComGameState_Unit Unit)
{
	return class'UIArmory_WeaponUpgrade'.static.CanCycleTo(Unit);
}

// ---------------------------------------------------------------------------------------
// The rows: what the widget does not keep
// ---------------------------------------------------------------------------------------

// UpdateSlots and UpdateUpgrades hand SetDisabled a reason the row throws away, so both are
// re-run: an equipped slot past the current slot count (a continent bonus that lapsed) and an
// upgrade that CanApplyUpgradeToWeapon refuses. The count sentence is the loadout's.
private function AppendRows(UIArmory_WeaponUpgrade Screen, XComGameState_Item Weapon)
{
	local UIArmory_WeaponUpgradeItem Row;
	local XGParamTag LocTag;
	local string Rec;
	local int i, NumUpgradeSlots;

	NumUpgradeSlots = Weapon.GetNumUpgradeSlots();
	if (`XCOMHQ.bExtraWeaponUpgrade)
		NumUpgradeSlots++;
	if (`XCOMHQ.ExtraUpgradeWeaponCats.Find(X2WeaponTemplate(Weapon.GetMyTemplate()).WeaponCat) != INDEX_NONE)
		NumUpgradeSlots++;

	if (Screen.SlotsList != none)
	{
		for (i = 0; i < Screen.SlotsList.ItemCount; ++i)
		{
			Row = UIArmory_WeaponUpgradeItem(Screen.SlotsList.GetItem(i));
			if (Row == none)
				continue;
			Rec = "kind=row" $ FLD_SEP $ "list=slots" $ FLD_SEP $ "index=" $ i;
			if (Row.bIsDisabled && Row.UpgradeTemplate != none && Row.SlotIndex > NumUpgradeSlots - 1)
				AddField(Rec, "reason", Screen.m_strRequiresContinentBonus);
			Emit(Rec);
		}
	}

	if (Screen.UpgradesList != none)
	{
		LocTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
		for (i = 0; i < Screen.UpgradesList.ItemCount; ++i)
		{
			Row = UIArmory_WeaponUpgradeItem(Screen.UpgradesList.GetItem(i));
			if (Row == none)
				continue;
			Rec = "kind=row" $ FLD_SEP $ "list=upgrades" $ FLD_SEP $ "index=" $ i;
			if (!Row.bIsInfinite && Row.Count >= 0)
			{
				LocTag.IntValue0 = Row.Count;
				AddField(Rec, "count", `XEXPAND.ExpandString(class'UIArmory_LoadoutItem'.default.m_strCount));
			}
			if (Row.bIsDisabled)
				AddField(Rec, "reason", Screen.m_strInvalidUpgrade);
			Emit(Rec);
		}
	}
}

// ---------------------------------------------------------------------------------------
// The preview: the description box and the stat block for the SELECTED row
// ---------------------------------------------------------------------------------------

// PreviewUpgrade, reproduced: the ACTIVE list's selected row names the upgrade, the SLOTS list's
// selected row names the slot (⛔ even when the active list is the upgrades — that is the game's
// own reading), the change state applies it and is thrown away, and the stats come off the
// modified weapon with the template highlighted. With no upgrade under the cursor the block is
// the weapon as it is (ChangeActiveList's branch) and there is no description box.
private function AppendPreview(UIArmory_WeaponUpgrade Screen, XComGameState_Item Weapon)
{
	local UIArmory_WeaponUpgradeItem Row, SlotRow;
	local XComGameState ChangeState;
	local XComGameState_Item Preview;
	local array<UISummary_ItemStat> Stats;
	local string Rec;
	local int SlotIndex;

	Row = (Screen.ActiveList != none)
		? UIArmory_WeaponUpgradeItem(Screen.ActiveList.GetSelectedItem()) : none;

	if (Row == none || Row.UpgradeTemplate == none)
	{
		Stats = Weapon.GetUISummary_WeaponStats();
	}
	else
	{
		SlotRow = (Screen.SlotsList != none)
			? UIArmory_WeaponUpgradeItem(Screen.SlotsList.GetSelectedItem()) : none;
		SlotIndex = (SlotRow != none) ? SlotRow.SlotIndex : 0;

		ChangeState = class'XComGameStateContext_ChangeContainer'.static.CreateChangeState("X2Access: preview weapon upgrade");
		Preview = XComGameState_Item(ChangeState.ModifyStateObject(class'XComGameState_Item', Screen.WeaponRef.ObjectID));
		Preview.DeleteWeaponUpgradeTemplate(SlotIndex);
		Preview.ApplyWeaponUpgradeTemplate(Row.UpgradeTemplate, SlotIndex);
		Stats = Preview.GetUISummary_WeaponStats(Row.UpgradeTemplate);
		`XCOMHISTORY.CleanupPendingGameState(ChangeState);

		Rec = "kind=preview";
		AddField(Rec, "name", Row.UpgradeTemplate.GetItemFriendlyName());
		AddField(Rec, "desc", Row.UpgradeTemplate.GetItemBriefSummary());
		Emit(Rec);
	}

	EmitStats(Stats);
}

// The stat block's label/value pairs, in the game's own order. A value arrives with
// AddStatModifier's colour markup, which native's CleanText strips.
private function EmitStats(array<UISummary_ItemStat> Stats)
{
	local string Rec;
	local int i;

	for (i = 0; i < Stats.Length; ++i)
	{
		if (Stats[i].Label == "" && Stats[i].Value == "")
			continue;
		Rec = "kind=stat" $ FLD_SEP $ "index=" $ i;
		AddField(Rec, "label", Stats[i].Label);
		AddField(Rec, "value", Stats[i].Value);
		Emit(Rec);
	}
}

// ---------------------------------------------------------------------------------------
// The pattern picker
// ---------------------------------------------------------------------------------------

// UpdateTrait pushes its heading through SetSlotsListTitle and keeps it nowhere the recipe can
// name; the caller (CustomizeWeaponPattern) passed UICustomize_Props' own WEAPON PATTERN, which
// is therefore the heading this picker always has.
private function AppendTraitHeader(UIArmory_WeaponTrait Trait)
{
	local string Rec;

	Rec = "kind=trait";
	AddField(Rec, "title", class'UICustomize_Props'.default.m_strWeaponPattern);
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// Verbs
// ---------------------------------------------------------------------------------------

// Vanilla's Tab / LeftShift, behind the screen's own gate.
function CycleWeaponUpgradeSoldier(int Direction)
{
	local UIArmory_WeaponUpgrade Screen;

	NativeResult = "";

	Screen = UIArmory_WeaponUpgrade(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.GetUnit() == none)
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
