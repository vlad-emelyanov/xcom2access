// X2AccessChallengeReader
// The Tactical Legacy Pack's Challenge Archives menu (UITLE_ChallengeModeMenu) - the half native
// cannot reach. Same contract as X2AccessTLEReader: the C++ layer owns the gate, the keys and the
// speech, calls DescribeChallengeMenu() on this class's default object via ProcessEvent (game
// thread, from the graph recipe's build) and reads NativeResult back through reflection right
// after the call returns. Nothing here runs on its own - no listeners, no timers, no polling.
//
// WHAT IS HERE AND WHY. The rows are NOT here: UITLEChallenge_ListItem retains its Data struct,
// the exact payload it hands Flash, and native reads the five cells off the widget. Two things
// native cannot reach are:
//
//   * the five SORT HEADERS. BuildHeaders spawns five UIFlipSortButtons and stores none of them,
//     so only GetChildrenOfType finds them - the same shape X2AccessPersonnelReader handles for
//     the personnel roster, and the same records (kind=sort index label selected desc).
//   * the three PROFILE FIGURES the details panel draws under the selected row (victories, high
//     score, challenges completed). OnSelectedChange reads them from the online profile and pushes
//     them into Flash with UITLEHub's own labels; the same read, the same labels, here.
//
// GC rule: this object is a CDO - value-typed state only, NEVER object references.

class X2AccessChallengeReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the screen's own top-to-bottom order:
//   kind=sort  index=<i>  label  selected  desc     (one per header, in child order)
//   kind=stat  label  value                         (victories, high score, completed)
function DescribeChallengeMenu()
{
	local UITLE_ChallengeModeMenu Screen;

	NativeResult = "";

	Screen = UITLE_ChallengeModeMenu(`SCREENSTACK.GetFirstInstanceOf(class'UITLE_ChallengeModeMenu'));
	if (Screen == none)
		return;

	AppendSortColumns(Screen);
	AppendStats();
}

// The five UIFlipSortButtons in the order BuildHeaders spawned them (mission, objective, map,
// squad, enemies) - the index is the child index, which is what ClickChallengeSort takes back.
private function AppendSortColumns(UITLE_ChallengeModeMenu Screen)
{
	local array<UIPanel> Buttons;
	local UIFlipSortButton SortButton;
	local string Rec;
	local int i;

	Screen.GetChildrenOfType(class'UIFlipSortButton', Buttons);
	for (i = 0; i < Buttons.Length; ++i)
	{
		SortButton = UIFlipSortButton(Buttons[i]);
		if (SortButton == none)
			continue;

		Rec = "kind=sort";
		AddField(Rec, "index", string(i));
		AddField(Rec, "label", SortButton.Label);
		if (SortButton.bArrowVisible)
		{
			AddField(Rec, "selected", "1");
			// bArrowFlipped is the arrow the game draws, which is the screen's m_bFlipSort -
			// its reversed-order flag. Reported as a bit; native gives it a word.
			if (SortButton.bArrowFlipped)
				AddField(Rec, "desc", "1");
		}
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	}
}

// OnSelectedChange's three figures, with the labels it pushes them under - UITLEHub's own
// localized words, read off that class's defaults exactly as the screen reads them.
private function AppendStats()
{
	local XComOnlineProfileSettings Profile;
	local string Rec;

	Profile = `XPROFILESETTINGS;
	if (Profile == none || Profile.Data == none)
		return;

	Rec = "kind=stat";
	AddField(Rec, "label", class'UITLEHub'.default.m_VictoryLabel);
	AddField(Rec, "value", string(Profile.Data.HubStats.NumOfflineChallengeVictories));
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	Rec = "kind=stat";
	AddField(Rec, "label", class'UITLEHub'.default.m_HighestScore);
	AddField(Rec, "value", string(Profile.Data.HubStats.OfflineChallengeHighScore));
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	Rec = "kind=stat";
	AddField(Rec, "label", class'UITLEHub'.default.m_CompletedLabel);
	AddField(Rec, "value", string(Profile.Data.HubStats.OfflineChallengeCompletion.Length));
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// ---------------------------------------------------------------------------------------
// Verbs
// ---------------------------------------------------------------------------------------

// Sort by header Index: the button's own mouse-up, not a poke at m_eSortType, because that
// handler owns three things a direct write would drop - it deselects every other header, it
// FLIPS the direction when the column was already the sort column (instead of resetting it),
// and it calls RefreshData() afterwards. Same verb, same reasoning as ClickPersonnelSort.
function ClickChallengeSort(int Index)
{
	local array<UIPanel> Buttons;
	local array<string> NoArgs;
	local UITLE_ChallengeModeMenu Screen;

	NativeResult = "";

	Screen = UITLE_ChallengeModeMenu(`SCREENSTACK.GetFirstInstanceOf(class'UITLE_ChallengeModeMenu'));
	if (Screen == none)
		return;

	Screen.GetChildrenOfType(class'UIFlipSortButton', Buttons);
	if (Index < 0 || Index >= Buttons.Length)
		return;

	// OnMouseEvent's args are the Flash payload, which this handler never reads. Cleared
	// explicitly rather than left default: the compiler warns on an unassigned local.
	NoArgs.Length = 0;
	Buttons[Index].OnMouseEvent(class'UIUtilities_Input'.const.FXS_L_MOUSE_UP, NoArgs);
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

// Empty values are DROPPED rather than written as an empty field: a missing key means "the
// screen is not showing this", which is exactly the distinction the recipe needs.
private function AddField(out string Rec, string Key, string Value)
{
	class'X2AccessWire'.static.AddField(Rec, Key, Value);
}
