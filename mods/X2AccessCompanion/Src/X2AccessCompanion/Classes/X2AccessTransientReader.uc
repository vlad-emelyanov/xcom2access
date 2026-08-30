// X2AccessTransientReader
// The transient surfaces — docs/popup-inventory.md §4. These are the things the game puts on
// screen for a few seconds and takes away again: the radio subtitle, the toast stack, the help
// blade. They are NOT popups and they deliberately get no graph recipe:
//
//   * nothing here is focusable, takes input, or can be dismissed, so there is nothing to
//     navigate — a tab stop over a line that is about to vanish is worse than useless;
//   * they appear WHILE the player is doing something else, so a recipe attaching would take
//     the arrows away from the screen they are actually on.
//
// So this is a POLL that speaks once per new line, the same shape as the tutorial reader's
// UIWorldMessageMgr watch. C++ calls PollTransient() on a timer and speaks NativeResult.
//
// ⛔ CHANGE DETECTION LIVES HERE, not in native, and that is the whole design. Every one of these
// surfaces re-renders continuously — the commlink retypes its subtitle as the VO scrolls, the
// notice list is rebuilt on every frame a notice is alive — so "what is on screen" is true for
// hundreds of polls in a row. Only the model can say what is NEW, and the last-spoken markers
// below are what turn a level signal into an edge.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references. The markers
// are strings and ints for exactly that reason.

class X2AccessTransientReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// Last-spoken markers. Value types only (CDO rule). Each is the exact text last announced for
// that surface, so a surface that redraws the same words stays silent and one that changes
// speaks — the cheapest correct edge detector for a level signal.
var private string LastCommLinkText;
var private string LastTutorialText;
var private array<string> LastNotices;

// Lines are joined with this and split by native, which then speaks them as separate
// utterances. PRINTABLE, matching every other companion reader's separators.
const LINE_SEP = "~|~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Everything new since the last call, newest surface last. Empty when nothing changed, which is
// the overwhelmingly common case — this runs several times a second.
function PollTransient()
{
	NativeResult = "";

	PollCommLink();
	PollEventNotices();
	PollTutorialBox();
}

// ---------------------------------------------------------------------------------------
// The radio subtitle — the single highest-value item in this file
// ---------------------------------------------------------------------------------------

// UINarrativeCommLink is how essentially ALL story dialogue in XCOM 2 reaches the player: Central,
// Tygan, Shen and the faction leaders talk over a portrait in the corner with a subtitle under it.
// Without this a blind player gets the voice acting and nothing else — and in a language whose VO
// they may not have, nothing at all.
//
// ⛔ Do NOT go looking for UINarrativePopup, the modal that LOOKS like the right class. It is DEAD
// CODE: both of its spawn sites in XComPresentationLayerBase are inside a commented-out block, and
// it handles only FXS_BUTTON_A and a Flash click anyway, so it was never keyboard-dismissable.
//
// The words are one hop away on the narrative manager, which retains them in a plain struct
// (TCurrentOutput) — the same one the commlink reads to populate Flash. No XCOMHISTORY.
private function PollCommLink()
{
	local UINarrativeCommLink CommLink;
	local UINarrativeMgr Mgr;
	local XComEngine kEngine;
	local string Line;

	// ⛔ `PRESBASE, not `PRES. The latter casts to XComPresentationLayer — the TACTICAL layer —
	// and answers none everywhere else, which would have made this silent in the Avenger and on
	// the geoscape, i.e. in most of the game's dialogue. The commlink hangs off UIMovie_2D, which
	// belongs to the base layer, and GetUIComm() is declared there.
	CommLink = `PRESBASE.GetUIComm();
	if (CommLink == none)
	{
		LastCommLinkText = "";
		return;
	}

	// bIsVisible is meaningful here for the reason the tile cursor's rule allows: the GAME
	// toggles it deliberately (Show/Hide around a conversation), so it means "a line is being
	// spoken", not "the widget happens to be hidden".
	if (!CommLink.bIsVisible)
	{
		// Cleared rather than kept, so the SAME line delivered again later still announces. The
		// game really does repeat a barks-style moment verbatim.
		LastCommLinkText = "";
		return;
	}

	// ⛔ THE SUBTITLE SETTING IS A SETTING ABOUT THIS TEXT, and turning it off is the player
	// asking not to be given the dialogue in writing. Speech is our rendering of the subtitle,
	// not a separate feature, so it obeys the same switch — otherwise the one option in the
	// game that means "stop showing me the words" is the one option the mod ignores.
	//
	// This is the exact test the widget makes: UINarrativeCommLink.RefreshSubtitleVisibility
	// calls AS_ShowSubtitles / AS_HideSubtitles off `XComEngine.bSubtitlesEnabled`, so with it
	// clear the portrait and the speaker name stay on screen and the TEXT BLOCK is gone. One
	// hop from the screen's own state, which is as close as the read-the-screen rule can get
	// here — the visibility itself lives in Flash and nothing retains it.
	//
	// ⚠ The flag is the ENGINE's, not the profile's. `XPROFILESETTINGS.Data.m_bSubtitles is
	// where the checkbox writes, and XComOnlineProfileSettings.ApplyOptions copies it into the
	// engine — so the profile value can be a toggle ahead of what is actually being drawn, and
	// the drawn one is the one parity is defined against.
	//
	// The PIE half of the game's condition (`|| WorldInfo.IsPlayInEditor()`) is deliberately
	// not mirrored: it forces subtitles on for developers in the editor, which no player build
	// ever reaches, and this class is a plain Object with no WorldInfo to hand.
	kEngine = `XENGINE;
	if (kEngine == none || !kEngine.bSubtitlesEnabled)
	{
		// Cleared for the same reason as above, and one more: the player can turn subtitles
		// back on mid-conversation from the options screen, and the line still being spoken
		// should then announce rather than be swallowed as already-said.
		LastCommLinkText = "";
		return;
	}

	// ⛔ The commlink keeps NO reference to the narrative manager — RefreshDisplay reads
	// Movie.Pres.m_kNarrativeUIMgr into a LOCAL every time it repaints. So the manager comes from
	// the presentation layer, the same place the widget gets it.
	Mgr = `PRESBASE.m_kNarrativeUIMgr;
	if (Mgr == none)
		return;

	Line = Mgr.CurrentOutput.strText;
	if (Line == "" || Line == LastCommLinkText)
		return;

	LastCommLinkText = Line;

	// The speaker leads. A subtitle with no attribution is ambiguous the moment two characters
	// trade lines, which is most of this game's dialogue — and the title is what the commlink
	// draws above the portrait, so naming it is parity rather than an addition.
	if (Mgr.CurrentOutput.strTitle != "")
		Push(Mgr.CurrentOutput.strTitle $ ": " $ Line);
	else
		Push(Line);
}

// ---------------------------------------------------------------------------------------
// The toast stack
// ---------------------------------------------------------------------------------------

// ⚠ UIEventNotices RETAINS EVERYTHING, which corrects the note in CLAUDE.md saying it retains
// nothing. `Notices` is an array<UIEventNoticeItem> on the screen holding DisplayString, Label,
// Value, eState and MaxDisplayTime — so this needs no Flash read and no history lookup, exactly
// like UIWorldMessageMgr.m_arrMsgs.
//
// Matching is by TEXT against everything spoken last time rather than by index: the list is
// rebuilt as notices expire, so indices shift under us while the surviving entries are unchanged.
private function PollEventNotices()
{
	local UIEventNotices Notices;
	local array<string> Current;
	local string Line;
	local int i;

	// ⛔ There is exactly ONE notices screen and it serves both layers: XComPresentationLayerBase
	// spawns a single UIEventNotices and pushes it on the stack (hidden until it has something).
	// Do NOT also look for UIEventNoticesTactical — despite the name it is not a screen at all
	// but `extends Object config(GameData)`, a table of localized notice CAPTIONS, so casting a
	// stack entry to it can only ever answer none.
	Notices = UIEventNotices(`SCREENSTACK.GetFirstInstanceOf(class'UIEventNotices'));
	if (Notices == none)
	{
		LastNotices.Length = 0;
		return;
	}

	for (i = 0; i < Notices.Notices.Length; i++)
	{
		Line = Notices.Notices[i].DisplayString;
		if (Line == "")
			continue;

		// A banner carries a subtitle and a value the plain notice does not — "REINFORCEMENTS"
		// with its count, say — and dropping them would lose the number, which is the part that
		// decides what a player does next.
		if (Notices.Notices[i].Label != "")
			Line = Line $ " " $ Notices.Notices[i].Label;
		if (Notices.Notices[i].Value != "")
			Line = Line $ " " $ Notices.Notices[i].Value;

		Current.AddItem(Line);
		if (LastNotices.Find(Line) == INDEX_NONE)
			Push(Line);
	}

	LastNotices = Current;
}

// ---------------------------------------------------------------------------------------
// The strategy help blade
// ---------------------------------------------------------------------------------------

// UIStrategyTutorialBox is the Avenger/geoscape counterpart of the tactical UITutorialBox, and it
// is the cheapest item in the whole popup inventory: it retains its text outright in one plain
// string var, which the screen sets through SetNewHelpText and then pushes to Flash.
private function PollTutorialBox()
{
	// ⚠ Not named `Box`: Object declares a struct of that name, and the compiler warns on the
	// shadowing ("Variable declaration: 'Box' conflicts with struct defined in 'Object'").
	local UIStrategyTutorialBox HelpBox;

	HelpBox = UIStrategyTutorialBox(`SCREENSTACK.GetFirstInstanceOf(class'UIStrategyTutorialBox'));
	if (HelpBox == none || !HelpBox.bIsVisible || HelpBox.m_strHelpText == "")
	{
		// Same reasoning as the commlink: forgetting on the way down is what lets the SAME hint
		// be re-shown later and still be announced.
		LastTutorialText = "";
		return;
	}

	if (HelpBox.m_strHelpText == LastTutorialText)
		return;

	LastTutorialText = HelpBox.m_strHelpText;
	Push(HelpBox.m_strHelpText);
}

// ---------------------------------------------------------------------------------------
// Wire
// ---------------------------------------------------------------------------------------

private function Push(string Line)
{
	if (NativeResult != "")
		NativeResult = NativeResult $ LINE_SEP;
	NativeResult = NativeResult $ Line;
}
