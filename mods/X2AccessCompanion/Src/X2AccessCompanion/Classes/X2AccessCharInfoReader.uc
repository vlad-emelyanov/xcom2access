// X2AccessCharInfoReader
// The tactical soldier sheet (UITacticalCharInfoScreen) — the OPENING half only. The screen itself
// retains every word it draws (each label keeps its Text, each effect row keeps the whole
// UISummary_UnitEffect it was built from) and native reads it directly. What native cannot do is
// put it on the stack: the game's only route in is the gamepad's L3 — XComTacticalInput.Stick_L3
// in state ActiveUnit_Moving, a state function on the input object, which is not reachable by name
// through ProcessEvent. F1 in tactical is a commented-out no-op, and the mouse-controls strip
// carries no soldier-info button on mouse+keyboard.
//
// OpenCharInfo reproduces Stick_L3's body in its own order — spawn over the current screen, hide
// the HUD, init, push — behind the HUD's own CanCharInfoShow gate. Nothing else here: the sheet's
// Escape is the game's, and its list has no verb.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessCharInfoReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns. Always
// empty here — the verb has nothing to say — but the bridge reads it regardless.
var string NativeResult;

function OpenCharInfo()
{
	local XComPresentationLayer Pres;
	local UITacticalCharInfoScreen Screen;
	local UITacticalHUD HUD;

	NativeResult = "";

	Pres = `PRES;
	if (Pres == none || Pres.ScreenStack == none)
		return;
	if (Pres.ScreenStack.IsInStack(class'UITacticalCharInfoScreen'))
		return;

	HUD = Pres.GetTacticalHUD();
	if (HUD == none || !HUD.CanCharInfoShow())
		return;

	// Stick_L3, line for line. ⛔ InitCharacterInfoScreen BEFORE the push, as the game does it:
	// OnInit (which builds the sheet) fires only when the Flash package loads, after the push.
	// (Spawn's return is coerced to the class it was handed — the compiler rejects a cast.)
	Screen = Pres.Spawn(class'UITacticalCharInfoScreen', Pres.ScreenStack.GetCurrentScreen());
	if (Screen == none)
		return;
	HUD.Hide();
	Screen.InitCharacterInfoScreen(XComPlayerController(Pres.Owner), Pres.Get2DMovie());
	Pres.ScreenStack.Push(Screen);
}

defaultproperties
{
	NativeResult = ""
}
