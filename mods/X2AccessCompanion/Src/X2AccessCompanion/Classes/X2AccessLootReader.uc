// X2AccessLootReader
// The mid-mission loot receipt (UIInventoryTactical) — the words half. Same contract as
// X2AccessCommodityReader: C++ owns the gate, the keys and the speech, calls DescribeLoot() on this
// class's default object via ProcessEvent from the graph recipe's build, and reads NativeResult
// back through reflection right after the call returns.
//
// WHAT THE SCREEN IS: the window a soldier's Loot ability (or a crate, a chest, a successful
// hack) opens over the HUD once the items are ALREADY in their inventory — a receipt, not a
// picker. TAKE ALL and DUMP ALL are commented out in the shipped source; OK is the whole verb, and
// the game's visualizer sleeps until it is pressed.
//
// ⛔ A ROW RETAINS ONE INT. UIInventoryTactical_LootingItem is a plain UIPanel whose SetText pushes
// the name straight into Flash and whose description is a mouse-following tooltip; what it keeps is
// GameStateObjectID. Every word below is re-derived from that id exactly the way UpdateData derived
// it: the template's friendly name, the " xN" quantity suffix the row draws, and the loot tooltip
// with the brief summary as its fallback. The title line and the source (the corpse or container)
// are Flash pushes off the screen's own retained m_Looter and LootTarget.
//
// ⚠ Lootable.GetLootingName dereferences the target's visualizer unguarded (object-scanner recon).
// A corpse's visualizer exists for as long as this screen can — the ability just played on it.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessLootReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

const FLD_SEP = "~:~";

// Records:
//   kind=header  title  looter  source     (LOOT RECOVERY; the soldier; the corpse or container)
//   kind=row     index  name  desc         (the list's own index; name carries the " xN" suffix)
function DescribeLoot()
{
	local UIInventoryTactical Screen;
	local UIInventoryTactical_LootingItem Row;
	local XComGameState_Item Item;
	local X2ItemTemplate ItemTemplate;
	local string Rec, ItemName, Desc;
	local int i;

	NativeResult = "";

	Screen = UIInventoryTactical(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	Rec = "kind=header";
	AddField(Rec, "title", Screen.m_strTitle);
	if (Screen.m_Looter != none)
		AddField(Rec, "looter", Screen.m_Looter.GetName(eNameType_RankFull));
	if (Screen.LootTarget != none)
		AddField(Rec, "source", Screen.LootTarget.GetLootingName());
	Emit(Rec);

	if (Screen.m_kLootList == none)
		return;

	for (i = 0; i < Screen.m_kLootList.ItemCount; ++i)
	{
		Row = UIInventoryTactical_LootingItem(Screen.m_kLootList.GetItem(i));
		if (Row == none)
			continue;

		Item = XComGameState_Item(`XCOMHISTORY.GetGameStateForObjectID(Row.GameStateObjectID));
		if (Item == none)
			continue;
		ItemTemplate = Item.GetMyTemplate();
		if (ItemTemplate == none)
			continue;

		// UpdateData's own composition, suffix included: "Alien Alloy x3".
		ItemName = ItemTemplate.GetItemFriendlyName();
		if (Item.Quantity > 1)
			ItemName @= "x" $ string(Item.Quantity);

		Desc = ItemTemplate.GetItemLootTooltip();
		if (Desc == "")
			Desc = ItemTemplate.GetItemBriefSummary(Item.ObjectID);

		Rec = "kind=row" $ FLD_SEP $ "index=" $ i;
		AddField(Rec, "name", ItemName);
		AddField(Rec, "desc", Desc);
		Emit(Rec);
	}
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
