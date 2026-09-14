// X2AccessSkyrangerReader
// The Skyranger-arrived prompt (UISkyrangerArrives) — the words half. Same contract as
// X2AccessMissionReader: C++ owns the gate, the keys and the speech, calls
// DescribeSkyrangerArrives() on this class's default object via ProcessEvent from the graph
// recipe's build, and reads NativeResult back through reflection right after the call returns.
//
// WHAT THE SCREEN IS. XComGameState_MissionSite.DestinationReached pushes it the moment the
// Skyranger reaches the mission pin: a left blade ("Skyranger" over "landing procedure
// initiated" — the two localized m_strSkyrangerArrivesTitle and -Subtitle strings), a right
// blade with the operation name, the "arrived, awaiting orders" body, and two buttons — begin
// the mission, or return to the Avenger.
//
// WHERE THE WORDS COME FROM. Nothing on it retains its text: both blades are pushed through
// LibraryPanel.MC (UpdateSkyrangerInfoBlade and UpdateSkyrangerButtonBlade) straight into Flash,
// and the two UIButtons are InitButton'd with an empty caption — their labels ride the blade op,
// not SetText. What the class HOLDS is every one of those words as a localized default plus
// GetOpName(), the screen's own getter for the operation name, so each record below re-derives
// a Flash push by reading the same field or calling the same function.
//
// ⛔ The cancel button gates on Button2's OWN bIsVisible, never on a recomputed test.
// BuildScreen already decided — the tutorial's first mission and every bCannotBackOutSquadSelect
// source hide it — and OnCancelClicked repeats the same guard, so a hidden button is one that
// does nothing. A child widget's bIsVisible is deliberately toggled and does mean hidden (the
// documented exception in CLAUDE.md).
//
// Enter and Escape are the SCREEN's own input path: OnUnrealCommand answers FXS_KEY_ENTER with
// OnLaunchClicked (ConfirmMission, and the screen deliberately stays up so the facility grid
// cannot flash before the load) and FXS_KEY_ESCAPE with OnCancelClicked. Neither reads a
// selected button, so the recipe sends the key, not a Click().
//
// ⛔ COMMENTS IN THIS PACKAGE MAY NOT CONTAIN A SLASH-STAR OR A BACKTICK. The macro
// preprocessor runs before the lexer and tracks block comments on its own: a slash-star inside a
// line comment opens a block it never sees closed, and every backtick macro after it goes
// unexpanded — the SCREENSTACK line below then fails with "Bad or missing expression in '='",
// which names nothing near the cause. A backtick in a comment is a macro invocation for the
// same reason. This file's first two compiles both died of one such slash-star in this header.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessSkyrangerReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format's field separator (see X2AccessWire); records are appended through it.
const FLD_SEP = "~:~";

// Records, in the order the recipe wants stops:
//   kind=header  title subtitle          (the left blade — names the screen)
//   kind=mission op body                 (the right blade's heading and paragraph)
//   kind=button  index label cmd         (Enter = begin mission; Escape = return, only if shown)
function DescribeSkyrangerArrives()
{
	local UISkyrangerArrives Screen;
	local string Rec;

	NativeResult = "";

	Screen = UISkyrangerArrives(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	Rec = "kind=header";
	class'X2AccessWire'.static.AddField(Rec, "title", Screen.m_strSkyrangerArrivesTitle);
	class'X2AccessWire'.static.AddField(Rec, "subtitle", Screen.m_strSkyrangerArrivesSubtitle);
	Emit(Rec);

	// BuildOptionsPanel's own pushes, in its order: GetOpName() is the screen's getter over
	// HQ.MissionRef, the body its localized default.
	Rec = "kind=mission";
	class'X2AccessWire'.static.AddField(Rec, "op", Screen.GetOpName());
	class'X2AccessWire'.static.AddField(Rec, "body", Screen.m_strBody);
	Emit(Rec);

	Rec = "kind=button" $ FLD_SEP $ "index=0";
	class'X2AccessWire'.static.AddField(Rec, "label", Screen.m_strConfirmMission);
	Rec $= FLD_SEP $ "cmd=" $ string(class'UIUtilities_Input'.const.FXS_KEY_ENTER);
	Emit(Rec);

	if (Screen.Button2 != none && Screen.Button2.bIsVisible)
	{
		Rec = "kind=button" $ FLD_SEP $ "index=1";
		class'X2AccessWire'.static.AddField(Rec, "label", Screen.m_strReturnToBase);
		Rec $= FLD_SEP $ "cmd=" $ string(class'UIUtilities_Input'.const.FXS_KEY_ESCAPE);
		Emit(Rec);
	}
}

private function Emit(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

defaultproperties
{
	NativeResult = ""
}
