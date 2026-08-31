// X2AccessCommodityReader
// The commodity list — UIInventory and everything derived from it — the words half. Same
// contract as X2AccessSaveReader and X2AccessRoomReader: C++ owns the gate, the keys and the
// speech, calls DescribeCommodity() on this class's default object via ProcessEvent from the
// graph recipe's build, and reads NativeResult back through reflection right after the call
// returns. No listeners, no timers.
//
// ONE CLASS FOR ABOUT A DOZEN SCREENS, because Firaxis built them as one. Choose research,
// choose project, choose a psi ability, view objectives, the three archives, build items,
// storage, implants, the XCOM database, the black market's buy side, resistance goods and the
// officer school are all a UIList of UIInventory_ListItems on the left and a UIItemCard on the
// right. That is why this is the highest-value single recipe left in the strategy layer.
//
// ⛔ WHY THE WORDS LIVE HERE, and it is worse than the usual case — BOTH halves are Flash-only:
//   * The CARD retains nothing at all. PopulateData and PopulateCostData resolve the title, the
//     description, the cost, the build time and the requirements into LOCALS and push them
//     through MC.BeginFunctionOp. UIItemCard's only retained vars are a Texture2D and two flags.
//     It is also in no navigator — no keyboard in the game can focus it — so for a tech, every
//     fact that decides whether to research it is unreachable twice over.
//   * The ROWS are half gone the same way: UIInventory_ListItem.PopulateData writes the name
//     through MC.BeginFunctionOp("populateData"), BYPASSING UIListItemString.SetText, so the
//     retained Text is EMPTY on a row that is visibly showing a name. Same trap the debris
//     room's personnel picker sets.
//
// What each row DOES retain is the data it was built from — ItemComodity (the whole Commodity
// struct), ItemTemplate, ItemRef, Quantity, XComDatabaseEntry — so every string below is
// re-derived from that, exactly the way UIItemCard derived it. No XCOMHISTORY beyond the one
// lookup PopulateResearchCostData itself makes, and no re-query of anything the screen already
// asked for.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessCommodityReader extends Object
	dependson(X2StrategyGameRulesetDataStructures);

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other companion reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants stops:
//   kind=header  title, subtitle
//   kind=tab     index, label, selected, disabled     (UIInventory_BuildItems only)
//   kind=row     index, name, qty
//   kind=card    title, costLabel, cost, timeLabel, time, req, desc
//
// The card describes the SELECTED row only, which is what the screen itself shows: the card is
// repopulated by UIInventory.SelectedItemChanged on every selection move, so asking about
// anything else would describe a card nobody is looking at.
function DescribeCommodity()
{
	local UIInventory Screen;

	NativeResult = "";

	// The same definition native's FindCommodityScreen uses — the topmost stack entry — so the
	// two halves can never be reading different screens.
	Screen = UIInventory(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	AppendHeader(Screen);
	AppendTabs(Screen);
	AppendRows(Screen);
	AppendCard(Screen);
}

// ---------------------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------------------

// Read off TitleHeader rather than off the screen's own m_strTitle, and the difference is real:
// UIInventory.PopulateData REPLACES the subtitle with m_strEmptyListTitle when the list came
// back empty. UIX2PanelHeader keeps both strings under a dirty check immediately before the
// Flash op that draws them (the same shape the Avenger grid overlay uses), so the retained pair
// IS what the header is showing.
private function AppendHeader(UIInventory Screen)
{
	local string Rec;

	if (Screen.TitleHeader == none)
		return;

	Rec = "kind=header";
	AddField(Rec, "title", Screen.TitleHeader.title);
	AddField(Rec, "subtitle", Screen.TitleHeader.label);
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// The tab strip (UIInventory_BuildItems only)
// ---------------------------------------------------------------------------------------

// Three categories — weapons, armour, utility — and a sighted keyboard player can already
// switch them with Left/Right (UIInventory_BuildItems.OnUnrealCommand handles FXS_ARROW_LEFT
// and _RIGHT at screen level). What they CANNOT do is focus one: CreateTabButton calls
// DisableNavigation() on every button. And the name of the category they switched to goes
// straight into Flash through UIInventory.SetCategory, which retains nothing. So the tab strip
// is not unreachable — it is unspeakable, and that is exactly what these records restore.
private function AppendTabs(UIInventory Screen)
{
	local UIInventory_BuildItems Build;

	Build = UIInventory_BuildItems(Screen);
	if (Build == none)
		return;

	// ⛔ EUIItemType IS NOT THE ORDER THE CODE READS AS. UIInventory_BuildItems.uc:13 declares
	// it Misc=0, Weapon=1, Armor=2 — while BuildScreen CALLS CreateTabButton in weapon, armor,
	// misc order, indexing TabButtons by the enum. So Flash slot 0 is Utility, and the strip
	// draws ПРЕДМЕТЫ, ОРУЖИЕ, БРОНЯ left to right (screenshot-verified live 2026-08-21).
	// Pairing index 0 with the weapon label shifted every caption AND every disabled flag by
	// one, and made Enter switch to a different category from the one just spoken. The index is
	// what SwitchTab and SetTabHighlight are expressed in, so it is the index that must lead.
	AppendTab(0, Build.m_strMiscTab, Build.SelectedTab, Build.MiscItems.Length == 0);
	AppendTab(1, Build.m_strWeaponTab, Build.SelectedTab, Build.WeaponItems.Length == 0);
	AppendTab(2, Build.m_strArmorTab, Build.SelectedTab, Build.ArmorItems.Length == 0);
}

private function AppendTab(int Index, string Label, int Selected, bool bDisabled)
{
	local string Rec;

	Rec = "kind=tab" $ FLD_SEP $ "index=" $ Index;
	AddField(Rec, "label", Label);
	if (Index == Selected)
		AddField(Rec, "selected", "1");
	if (bDisabled)
		AddField(Rec, "disabled", "1");
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------------------

// One record per row, carrying the row's own list INDEX rather than relying on array position:
// native walks the same container and skips anything that is not a list item, and matching on
// the index the row itself stamps is what keeps the two halves from drifting over a hole.
private function AppendRows(UIInventory Screen)
{
	local UIInventory_ListItem Row;
	local string Rec;
	local int i;

	if (Screen.List == none)
		return;

	for (i = 0; i < Screen.List.ItemCount; ++i)
	{
		Row = UIInventory_ListItem(Screen.List.GetItem(i));
		if (Row == none)
			continue; // padding panels and anything else the list is holding

		Rec = "kind=row" $ FLD_SEP $ "index=" $ i;
		AddField(Rec, "name", RowName(Screen, Row));

		// The quantity column, which the row draws to the right of the name. Only the item
		// branches ever show it: PopulateData blanks it for a commodity and for an archive
		// entry, so speaking it there would invent a column the screen does not have.
		if (Row.Quantity > 0 && Row.ItemTemplate != none
			&& !ClassIsChildOf(Screen.Class, class'UISimpleCommodityScreen'))
		{
			AddField(Rec, "qty", string(Row.Quantity));
		}

		Emit(Rec);
	}
}

// UIInventory_ListItem.PopulateData's own branch order, which is also the only place these
// names exist — the row pushed exactly this string into Flash and kept none of it.
private function string RowName(UIInventory Screen, UIInventory_ListItem Row)
{
	local string Label;

	if (Row.XComDatabaseEntry != none)
		return Row.XComDatabaseEntry.GetListTitle();

	if (!ClassIsChildOf(Screen.Class, class'UISimpleCommodityScreen') && Row.ItemTemplate != none)
	{
		Label = Row.ItemTemplate.GetItemFriendlyName(Row.ItemRef.ObjectID);

		// PopulateData appends the PRIORITY marker for a priority build item and colours the
		// whole row amber for it. The colour is a channel speech loses; the word is not.
		if (Screen.Class == class'UIInventory_BuildItems' && Row.ItemTemplate.bPriority)
			Label $= class'UIUtilities_Text'.default.m_strPriority;

		return Label;
	}

	return Row.ItemComodity.Title;
}

// ---------------------------------------------------------------------------------------
// The card
// ---------------------------------------------------------------------------------------

// UIInventory.SelectedItemChanged's branch, followed by whichever UIItemCard populator it
// reaches. Anything this says differently from the card is a bug in this function.
private function AppendCard(UIInventory Screen)
{
	local UIInventory_ListItem Row;
	local string Rec;

	if (Screen.List == none || Screen.ItemCard == none)
		return;

	// PopulateItemCard() with no template calls ItemCard.Hide(), and UIInventory_LootRecovered
	// never shows one at all. A hidden card is not a card with nothing in it — it is a screen
	// that has no card, and the recipe must not offer a stop for it.
	if (!Screen.ItemCard.bIsVisible)
		return;

	Row = UIInventory_ListItem(Screen.List.GetItem(Screen.List.SelectedIndex));
	if (Row == none)
		return;

	Rec = "kind=card";

	if (Row.XComDatabaseEntry != none)
	{
		// UIInventory_XComDatabase overrides SelectedItemChanged entirely: the archive card is
		// a title and a body, with no cost block.
		AddField(Rec, "title", Row.XComDatabaseEntry.GetDescriptionTitle());
		AddField(Rec, "desc", Row.XComDatabaseEntry.GetDescriptionEntry());
	}
	else if (Row.ItemTemplate != none)
	{
		AddField(Rec, "title", Row.ItemTemplate.GetItemFriendlyName());
		AddField(Rec, "desc",
			FormatReq(Row.ItemTemplate.GetItemBriefSummary(Row.ItemRef.ObjectID)));
		AppendItemCost(Screen, Row, Rec);
	}
	else
	{
		AddField(Rec, "title", Row.ItemComodity.Title);
		AddField(Rec, "desc", FormatReq(Row.ItemComodity.Desc));
		if (Screen.bUseSimpleCard)
			AppendSimpleCommodityCost(Screen, Row, Rec);
		else if (UIResearchArchives(Screen) == none)
			// PopulateResearchCard's own exception: the archives show the write-up and no
			// numbers, because there is nothing left to spend on a tech already researched.
			AppendResearchCost(Screen, Row, Rec);
	}

	Emit(Rec);
}

// UIItemCard.PopulateCostData, which only UIInventory_BuildItems reaches (its PopulateItemCard
// override is the single call site). On every other item screen the card carries no numbers,
// and neither does this.
private function AppendItemCost(UIInventory Screen, UIInventory_ListItem Row, out string Rec)
{
	local UIFacility_Storage FacilityUI;
	local float EngBonus;
	local int Hours;
	local string Cost, Time, Requirements;

	if (Screen.Class != class'UIInventory_BuildItems')
		return;

	EngBonus = class'UIUtilities_Strategy'.static.GetEngineeringDiscount(
		Row.ItemTemplate.Requirements.RequiredEngineeringScore);
	Cost = class'UIUtilities_Strategy'.static.GetStrategyCostString(
		Row.ItemTemplate.Cost, `XCOMHQ.ItemBuildCostScalars, EngBonus);

	// The build TIME depends on which engineering bay is asking, which is why the card reaches
	// back up the stack for it rather than asking the template. Zero when there is no bay,
	// exactly as the card does — not an error case.
	FacilityUI = UIFacility_Storage(Screen.Movie.Stack.GetFirstInstanceOf(class'UIFacility_Storage'));
	Hours = FacilityUI != none ? `XCOMHQ.GetItemBuildTime(Row.ItemTemplate, FacilityUI.FacilityRef) : 0;

	if (Hours < 0)
		Time = Screen.ItemCard.m_strNeedEngineers;
	else if (Hours == 0)
		Time = Screen.ItemCard.m_strInstant;
	else
		Time = class'UIUtilities_Text'.static.GetTimeRemainingString(Hours);

	Requirements = class'UIUtilities_Strategy'.static.GetStrategyReqString(Row.ItemTemplate.Requirements);
	if (EngBonus > 0)
	{
		if (Requirements != "")
			Requirements $= ", ";
		Requirements $= class'UIUtilities_Strategy'.static.GetEngineeringDiscountString(
			Row.ItemTemplate.Requirements.RequiredEngineeringScore);
	}

	AddCostFields(Screen, Rec, Cost, Screen.ItemCard.m_strTimeToBuildLabel, Time, Requirements);
}

// UIItemCard.PopulateSimpleCommodityCostData.
private function AppendSimpleCommodityCost(UIInventory Screen, UIInventory_ListItem Row,
	out string Rec)
{
	local string Cost, Time, TimeLabel;

	Cost = class'UIUtilities_Strategy'.static.GetStrategyCostString(
		Row.ItemComodity.Cost, Row.ItemComodity.CostScalars, Row.ItemComodity.DiscountPercent);

	// Zero hours here means "no time row at all", NOT "instant" — that is the research card's
	// reading of the same field, and the two genuinely differ.
	if (Row.ItemComodity.OrderHours != 0)
	{
		Time = class'UIUtilities_Text'.static.GetTimeRemainingString(Row.ItemComodity.OrderHours);
		TimeLabel = Screen.ItemCard.m_strTimeToBuildLabel;
	}

	AddCostFields(Screen, Rec, Cost, TimeLabel, Time, CommodityReqString(Row));
}

// UIItemCard.PopulateResearchCostData.
private function AppendResearchCost(UIInventory Screen, UIInventory_ListItem Row, out string Rec)
{
	local XComGameState_Tech TechState;
	local string Cost, Time, TimeLabel;

	Cost = class'UIUtilities_Strategy'.static.GetStrategyCostString(
		Row.ItemComodity.Cost, Row.ItemComodity.CostScalars, Row.ItemComodity.DiscountPercent);

	TimeLabel = Screen.ItemCard.m_strTimeToBuildLabel;
	if (Row.ItemComodity.OrderHours < 0)
	{
		TimeLabel = "";
	}
	else if (Row.ItemComodity.OrderHours == 0)
	{
		Time = Screen.ItemCard.m_strInstant;
	}
	else
	{
		Time = class'UIUtilities_Text'.static.GetTimeRemainingString(Row.ItemComodity.OrderHours);
	}

	// A finished, non-repeatable tech shows no time at all — the card blanks BOTH strings, and
	// so must this, or a researched tech would keep advertising a duration.
	TechState = XComGameState_Tech(`XCOMHISTORY.GetGameStateForObjectID(Row.ItemRef.ObjectID));
	if (Row.ItemComodity.bTech && `XCOMHQ.TechIsResearched(Row.ItemRef)
		&& TechState != none && !TechState.GetMyTemplate().bRepeatable)
	{
		TimeLabel = "";
		Time = "";
	}

	AddCostFields(Screen, Rec, Cost, TimeLabel, Time, CommodityReqString(Row));
}

// bTech picks a DIFFERENT requirement formatter, not a different wording of the same one:
// GetTechReqString folds the cost's own science and engineering scores into the sentence, which
// GetStrategyReqString never sees.
private function string CommodityReqString(UIInventory_ListItem Row)
{
	if (Row.ItemComodity.bTech)
		return class'UIUtilities_Strategy'.static.GetTechReqString(
			Row.ItemComodity.Requirements, Row.ItemComodity.Cost);

	return class'UIUtilities_Strategy'.static.GetStrategyReqString(Row.ItemComodity.Requirements);
}

// The card's three cost lines, each dropped when the card itself would draw it empty — every
// populator above guards Cost the same way, and PopulateData queues "" for a missing label.
//
// ⚠ The labels come off the LIVE UIItemCard instance rather than a class default, because a
// `localized` var can be declared on several classes and populated on only one (m_strCostLabel
// is also declared by UIArmory_PromotionHero). The instance that pushed the string into Flash
// is the only object guaranteed to be holding it.
private function AddCostFields(UIInventory Screen, out string Rec,
	string Cost, string TimeLabel, string Time, string Requirements)
{
	if (Cost != "")
	{
		AddField(Rec, "costLabel", Screen.ItemCard.m_strCostLabel);
		AddField(Rec, "cost", FormatReq(Cost));
	}
	AddField(Rec, "timeLabel", TimeLabel);
	AddField(Rec, "time", Time);
	AddField(Rec, "req", FormatReq(Requirements));
}

// UIItemCard.FormatRequirements — the game's own comma-list tidying, applied to exactly the
// strings the card applies it to.
private function string FormatReq(string Requirements)
{
	return class'UIUtilities_Text'.static.FormatCommaSeparatedNouns(Requirements);
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
