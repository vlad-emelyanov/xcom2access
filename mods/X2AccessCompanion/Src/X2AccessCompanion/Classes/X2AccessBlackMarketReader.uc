// X2AccessBlackMarketReader
// The black market's two screens that are NOT the commodity list — the words half. Same contract
// as X2AccessCommodityReader and X2AccessPersonnelReader: C++ owns the gate, the keys and the
// speech, calls an entry point here on this class's default object via ProcessEvent from the graph
// recipe's build, and reads NativeResult back through reflection right after the call returns. No
// listeners, no timers, no polling. (The BUY side, UIBlackMarket_Buy, is a UISimpleCommodityScreen
// and rides X2AccessCommodityReader untouched.)
//
// THE ENTRY MENU (UIBlackMarket) needs exactly one sentence from script: the month's interests.
// The screen builds it in GetInterestsString() — the market's interest items folded into
// m_strInterests[] through the XGParamTag expansion — and pushes it straight into Flash beside its
// caption. Nothing retains it, and it is a game-state expansion native cannot run, so it is
// re-asked of the LIVE SCREEN's own function: the same words the screen drew, from the same code.
// Everything else on that menu is native — the two buttons keep their Text and IsDisabled, and the
// greyed-out reason is the tooltip RefreshButtons parks on them.
//
// THE TRADING POST (UIBlackMarket_Sell) is the same trap as every list in this game: a
// UIBlackMarket_SellItem pushes its name, the quantity owned, the unit value, the number selling
// and the line total through MC.BeginFunctionOp("populateData") and keeps only NumSelling, Price
// and ItemRef. The NUMBERS native reads off the row; the WORDS — the item's name, how many the
// player owns, whether the market wants it, and the card — are re-derived here exactly the way
// PopulateData and PopulateItemCard derived them.
//
// ⛔ The owned count is emitted RAW (Item.Quantity), not as the row draws it (Quantity minus
// NumSelling): native has the live NumSelling and does the subtraction itself, so a +/- press
// changes nothing this payload carries and costs no outbound call.
//
// ⛔ No Caps(). PopulateItemCard runs the title through CapsCheckForGermanScharfesS, which is a
// visual style; a reader that spells out all-caps words would turn every item into an acronym.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessBlackMarketReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// The entry menu
// ---------------------------------------------------------------------------------------

// One record:
//   kind=interest  text     (the sentence; the field is absent when the market wants nothing)
function DescribeBlackMarket()
{
	local UIBlackMarket Screen;
	local string Rec;

	NativeResult = "";

	// The same definition native's finder uses — the entry menu is the CURRENT screen only while
	// neither of its own screens is over it.
	Screen = UIBlackMarket(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	Rec = "kind=interest";
	AddField(Rec, "text", Screen.GetInterestsString());
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// The trading post
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants them:
//   kind=header  title  prefix                          (the name; the credits prefix)
//   kind=row     index  name  qty  interested            (qty RAW — see above; interested=1)
//   kind=card    title  costLabel  cost  interested  desc (the SELECTED row's card)
//
// Every index is the GAME's — the list's own item index — because that is what SetSelectedIndex
// and native's row walk are expressed in.
function DescribeBlackMarketSell()
{
	local UIBlackMarket_Sell Screen;

	NativeResult = "";

	Screen = UIBlackMarket_Sell(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	AppendHeader(Screen);
	AppendRows(Screen);
	AppendCard(Screen);
}

// ⛔ The screen has no title of its own: [UIBlackMarket_Sell] in the loc file ships an m_strTitle
// (TRADING POST), but the CLASS declares no such var, so that string exists nowhere at runtime.
// The name that matches what the player just did is the entry menu's own button label — SELL
// INVENTORY — a class default, read without the entry screen having to be on the stack.
private function AppendHeader(UIBlackMarket_Sell Screen)
{
	local string Rec;

	Rec = "kind=header";
	AddField(Rec, "title", class'UIBlackMarket'.default.m_strSell);
	AddField(Rec, "prefix", class'UIUtilities_Strategy'.default.m_strCreditsPrefix);
	Emit(Rec);
}

// One record per row, carrying the row's own list index rather than relying on array position,
// so a hole in the list cannot shift every entry after it.
private function AppendRows(UIBlackMarket_Sell Screen)
{
	local UIBlackMarket_SellItem Row;
	local XComGameState_Item Item;
	local string Rec;
	local int i;

	if (Screen.List == none)
		return;

	for (i = 0; i < Screen.List.ItemCount; ++i)
	{
		Row = UIBlackMarket_SellItem(Screen.List.GetItem(i));
		if (Row == none || Row.ItemTemplate == none)
			continue;

		Rec = "kind=row" $ FLD_SEP $ "index=" $ i;
		// PopulateData's own name, minus the GetColoredText wrapper it draws it in.
		AddField(Rec, "name", Row.ItemTemplate.GetItemFriendlyName());
		// RAW — the row draws Item.Quantity - NumSelling, and native has the live NumSelling.
		Item = Row.GetItem();
		if (Item != none)
			AddField(Rec, "qty", string(Item.Quantity));
		// The screen's own test, the one PopulateItemCard uses for the VERY INTERESTED line.
		if (Screen.IsInterested(Row.ItemTemplate))
			AddField(Rec, "interested", "1");
		Emit(Rec);
	}
}

// PopulateItemCard, operand for operand: the title, the cost caption, the price, the interest
// line (empty when the market is indifferent), the brief summary. The image and the empty warning
// slot are not words. Describes the SELECTED row only — that is the card the screen is showing.
private function AppendCard(UIBlackMarket_Sell Screen)
{
	local UIBlackMarket_SellItem Row;
	local string Rec;

	if (Screen.List == none || Screen.List.SelectedIndex < 0)
		return;

	Row = UIBlackMarket_SellItem(Screen.List.GetItem(Screen.List.SelectedIndex));
	if (Row == none || Row.ItemTemplate == none)
		return;

	Rec = "kind=card";
	AddField(Rec, "title", Row.ItemTemplate.GetItemFriendlyName());
	AddField(Rec, "costLabel", Screen.m_strCostLabel);
	AddField(Rec, "cost", class'UIUtilities_Strategy'.default.m_strCreditsPrefix $ string(Row.Price));
	if (Screen.IsInterested(Row.ItemTemplate))
		AddField(Rec, "interested", Screen.m_strInterestedLabel);
	AddField(Rec, "desc", Row.ItemTemplate.GetItemBriefSummary(Row.ItemRef.ObjectID));
	Emit(Rec);
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
