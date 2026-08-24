// X2AccessPolicyReader
// The resistance-orders board (UIStrategyPolicy) — the words half, and the four verbs, because
// this screen has NO keyboard at all in vanilla. Same contract as every other reader: the C++
// layer owns the gate, the keys and the speech, calls DescribePolicy() on this class's default
// object via ProcessEvent from the graph recipe's build, and reads NativeResult back through
// reflection right after the call returns. No listeners, no timers, no polling.
//
// ⛔ WHY THIS SCREEN IS DIFFERENT FROM EVERY OTHER LIST WE HAVE READ. UIStrategyPolicy.OnUnrealCommand
// gives a mouse+keyboard player exactly TWO verbs, and they are the same one: Escape and Enter
// both reach OnClickedContinue(). Everything else in that switch is either a controller constant
// (FXS_BUTTON_X to remove a card, FXS_BUTTON_Y to change the filter, the DPAD to navigate) or
// wrapped in `ISCONTROLLERACTIVE. Placing a card is DRAG AND DROP — UIStrategyPolicy_Card's own
// OnMouseEvent is the only path into SelectCard. So a keyboard player can read nothing and change
// nothing; they can only leave. This file plus the recipe are what make the screen exist.
//
// ✅ THE CARDS RETAIN THEIR STRUCTURE, so native reads which card sits in which slot for itself:
// CardRef, ColumnIndex, ColumnSlot, bLocked and bVital are real fields on UIStrategyPolicy_Card.
// What they retain is not one WORD — title, summary, quote and slot hint are all parameters to
// MC.BeginFunctionOp("SetCardData") — so those are re-derived here from the
// XComGameState_StrategyCard the ref names.
//
// ⛔ THE CARDS ARE WALKED OFF THE SCREEN'S OWN LISTS, never off the game state. Screen.Columns[i]
// and Screen.Hand are what the screen BUILT, in the order it built them, and each item keeps the
// CardRef it was built from. Re-deriving the layout from ResHQ.GetHandCards() and
// FactionState.GetCardSlots() would be a second opinion about which card is where, and the two
// halves address cards by (column, slot) — so the moment they disagreed, a verb would move the
// wrong card. Same rule the squad-select key learned the hard way.
//
// ⛔ SelectCard IS A TWO-STEP STATE MACHINE, and reproducing the MOUSE gesture means reproducing
// BOTH of its gates, which are not the same gate:
//
//   picking a card UP  -> the card's own CanDragCard() must pass (blank, locked and vital cards
//                         refuse), exactly as FXS_L_MOUSE_DOWN requires;
//   putting one DOWN   -> NO CanDragCard() check at all, because the drop path is
//                         FXS_L_MOUSE_IN under `if( PolicyScreen.bDragging )`, which tests
//                         nothing about the target. A blank slot is the normal drop target and
//                         CanDragCard() returns FALSE on one.
//
// Getting that backwards makes every empty slot unfillable, which is the whole screen.
//
// ⛔ bEnableDragging IS THE OTHER GATE AND SelectCard DOES NOT CHECK IT. Outside the monthly
// report the card's mouse handler refuses and raises UICantChangeOrders(); SelectCard itself
// only tests it on the CONTROLLER path, so calling it from here without the gate would let a
// player rearrange their orders on a day the game does not allow it. The refusal is reproduced
// in full — negative sound and the game's own popup — because that popup is the only place the
// reason is stated, and the popup recipe already reads it.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessPolicyReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records:
//   kind=screen  decktitle  influencelabel  changeable=0|1
//   kind=column  index=<i>  label  influence
//   kind=card    column=<c>  slot=<s>  name  summary  quote  hint  attention  removable=0|1
//
// A HAND card carries column=-1 and slot=<its index in the hand list>; that pair is the address
// native joins on and the same pair the verbs below take. ⛔ It is not the card's ObjectID: an
// EMPTY slot has none, and empty slots are the things a player most needs to be able to aim at.
function DescribePolicy()
{
	local UIStrategyPolicy Screen;
	local int col, i;

	NativeResult = "";

	Screen = UIStrategyPolicy(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	// ⛔ `continue` is the GAME's own generic label, not a word of ours: UpdateNavHelp reaches the
	// button through NavHelp.AddContinueButton, whose caption lives in Flash and is unreadable —
	// but it is the same m_strGenericContinue every other confirm button in the game uses, so
	// sourcing it here keeps a Russian player from hearing an English "Continue". `removelabel`
	// is the screen's own m_strRemoveCard, which vanilla shows only in the CONTROLLER nav help.
	Emit("kind=screen"
		$ FLD_SEP $ "decktitle=" $ Screen.DeckTitle
		$ FLD_SEP $ "influencelabel=" $ Screen.InfluenceLabel
		$ FLD_SEP $ "continue=" $ class'UIUtilities_Text'.default.m_strGenericContinue
		$ FLD_SEP $ "removelabel=" $ Screen.m_strRemoveCard
		$ FLD_SEP $ "changeable=" $ (Screen.bEnableDragging ? "1" : "0"));

	for (col = 0; col < Screen.Columns.Length; col++)
	{
		Emit(ColumnRecord(Screen, col));

		for (i = 0; i < Screen.Columns[col].GetItemCount(); i++)
			Emit(CardRecord(Screen, UIStrategyPolicy_Card(Screen.Columns[col].GetItem(i)), col, i));
	}

	if (Screen.Hand != none)
	{
		for (i = 0; i < Screen.Hand.GetItemCount(); i++)
			Emit(CardRecord(Screen, UIStrategyPolicy_Card(Screen.Hand.GetItem(i)), -1, i));
	}
}

// ---------------------------------------------------------------------------------------
// Words
// ---------------------------------------------------------------------------------------

// RealizePolicyLabels' own choices: column 0 is the wild-card column and has no faction, and an
// unmet faction reads as UnknownFactionColumnLabel rather than by name — which is the game
// withholding, not us, so it is reproduced exactly.
private function string ColumnRecord(UIStrategyPolicy Screen, int Index)
{
	local XComGameState_ResistanceFaction Faction;
	local string Rec;

	Rec = "kind=column" $ FLD_SEP $ "index=" $ string(Index);

	if (Index == 0)
		return Rec $ FLD_SEP $ "label=" $ Screen.WildCardColumnLabel;

	if (!Screen.KnowsFaction(Screen.ColumnNames[Index]))
		return Rec $ FLD_SEP $ "label=" $ Screen.UnknownFactionColumnLabel;

	Faction = Screen.GetColumnFaction(Index);
	Rec $= FLD_SEP $ "label=" $ Caps(Faction.GetFactionTitle());
	// The screen's own GetInfluenceString, HTML colour and all — native's CleanText strips it.
	Rec $= FLD_SEP $ "influence=" $ Screen.GetInfluenceString(Index);
	return Rec;
}

// CreateCard's own branches. Three kinds of card share one widget class and they read completely
// differently: a real order, a LOCKED slot (which carries only the influence hint), and a BLANK
// slot (which carries nothing at all and is the thing a player is aiming at).
private function string CardRecord(UIStrategyPolicy Screen, UIStrategyPolicy_Card Card, int Column, int Slot)
{
	local XComGameState_StrategyCard CardState;
	local string Rec;

	Rec = "kind=card" $ FLD_SEP $ "column=" $ string(Column) $ FLD_SEP $ "slot=" $ string(Slot);
	if (Card == none)
		return Rec;

	if (Card.bLocked)
		return Rec $ FLD_SEP $ "hint=" $ Screen.LockedSlotLabel;

	CardState = Screen.GetCardState(Card.CardRef);
	if (CardState == none)
	{
		// A blank slot. RealizeColumn gives the WILD-CARD column's blanks a hint and every other
		// column's none at all, so this is the one record that can come back with nothing but its
		// address — native names it rather than skipping it, because an empty slot a player
		// cannot land on is an empty slot they cannot fill.
		if (Column == 0)
			Rec $= FLD_SEP $ "hint=" $ Screen.WildCardSlotLabel;
		return Rec;
	}

	Rec $= FLD_SEP $ "name=" $ CardState.GetDisplayName();
	Rec $= FLD_SEP $ "summary=" $ CardState.GetSummaryText();
	Rec $= FLD_SEP $ "quote=" $ CardState.GetQuote();
	Rec $= FLD_SEP $ "hint=" $ CardState.GetSlotHint();
	if (CardState.GetNeedsAttention())
		Rec $= FLD_SEP $ "attention=1";
	// bVital is the screen's own record of the same answer (MarkCardAsSpecial), but only for cards
	// in a slot; asking the CARD STATE covers a hand card too.
	Rec $= FLD_SEP $ "removable=" $ (CardState.CanBeRemoved() ? "1" : "0");
	return Rec;
}

// ---------------------------------------------------------------------------------------
// Verbs — all four of them, because vanilla's keyboard has none
// ---------------------------------------------------------------------------------------

// Pick a card up, or put the held one down. See the header for why the two directions test
// different things: the mouse's DOWN requires CanDragCard() and its drag-over drop requires
// nothing, and a blank slot fails CanDragCard() while being the normal drop target.
function SelectPolicyCard(int Column, int Slot)
{
	local UIStrategyPolicy Screen;
	local UIStrategyPolicy_Card Card;

	Screen = UIStrategyPolicy(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	Card = ResolveCard(Screen, Column, Slot);
	if (Card == none)
		return;

	// The card's own FXS_L_MOUSE_DOWN refusal, reproduced whole: the popup is the only statement
	// anywhere of why nothing happened, and the popup recipe reads it.
	if (!Screen.bEnableDragging)
	{
		`SOUNDMGR.PlaySoundEvent("Play_MenuClickNegative");
		`HQPRES.UICantChangeOrders();
		return;
	}

	if (Screen.InspectingCard == none && !Card.CanDragCard())
		return;

	Screen.SelectCard(Card);
}

// The FXS_BUTTON_X path, which is a gamepad button and therefore unreachable in vanilla without
// one. The order of operations is the game's: clear the inspector first, then remove, then
// rebuild — RefreshAllDecks is what makes the freed slot exist again.
function RemovePolicyCard(int Column, int Slot)
{
	local UIStrategyPolicy Screen;
	local UIStrategyPolicy_Card Card;
	local XComGameState_StrategyCard CardState;

	Screen = UIStrategyPolicy(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || !Screen.bEnableDragging || Column < 0)
		return;

	Card = ResolveCard(Screen, Column, Slot);
	if (Card == none)
		return;

	Screen.ClearInspector();

	CardState = Screen.GetCardState(Card.CardRef);
	if (CardState != none && CardState.CanBeRemoved())
		Screen.RemoveCardFromSlot(Card);
	else
		`SOUNDMGR.PlaySoundEvent("Play_MenuClickNegative");

	Screen.RefreshAllDecks();
}

// Put the held card back down without moving it — the gesture a drag has for free (release over
// nothing) and a two-step selection does not.
function ClearPolicySelection()
{
	local UIStrategyPolicy Screen;

	Screen = UIStrategyPolicy(`SCREENSTACK.GetCurrentScreen());
	if (Screen != none)
		Screen.ClearInspector();
}

// Leave. This is the ONE verb vanilla's keyboard already had (Escape and Enter both reach it),
// and it is re-provided rather than left to the game because our layer claims those keys — and
// because OnClickedContinue is not merely a close: it submits OnCardScreenConfirm and may raise
// the "you still have orders to play" dialog, which is a warning worth keeping.
function ContinuePolicy()
{
	local UIStrategyPolicy Screen;

	Screen = UIStrategyPolicy(`SCREENSTACK.GetCurrentScreen());
	if (Screen != none)
		Screen.OnClickedContinue();
}

// The (column, slot) address native and this file agree on: column -1 is the hand, and the slot
// is the item's index in whichever list holds it. ⛔ Resolved through the SCREEN'S OWN lists for
// the reason in the header — it is the only way the two halves cannot disagree about which card
// a verb is aimed at.
private function UIStrategyPolicy_Card ResolveCard(UIStrategyPolicy Screen, int Column, int Slot)
{
	if (Column < 0)
	{
		if (Screen.Hand == none || Slot < 0 || Slot >= Screen.Hand.GetItemCount())
			return none;
		return UIStrategyPolicy_Card(Screen.Hand.GetItem(Slot));
	}

	if (Column >= Screen.Columns.Length)
		return none;
	if (Slot < 0 || Slot >= Screen.Columns[Column].GetItemCount())
		return none;

	return UIStrategyPolicy_Card(Screen.Columns[Column].GetItem(Slot));
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

private function Emit(string Rec)
{
	if (NativeResult != "")
		NativeResult $= REC_SEP;
	NativeResult $= Rec;
}

defaultproperties
{
	NativeResult = ""
}
