// X2AccessExplorePanel
// Invisible UIPanel spawned on UITacticalHUD.
// Uses Highlander SubscribeToOnInputForScreen to intercept keyboard input.

class X2AccessExplorePanel extends UIPanel;

var UIScreenListener_TacticalHUD Listener;
var bool bSubscribed;

simulated function UIPanel InitPanel(optional name InitName, optional name InitLibID)
{
	super.InitPanel(InitName, InitLibID);
	return self;
}

simulated function OnInit()
{
	super.OnInit();
	SubscribeInput();
}

function SubscribeInput()
{
	local UIScreenStack Stack;
	local UIScreen TacScreen;

	if (bSubscribed)
		return;

	Stack = `SCREENSTACK;
	if (Stack == none)
	{
		`log("X2Access: Panel SubscribeInput FAILED - no SCREENSTACK", , 'X2Access');
		return;
	}

	TacScreen = Stack.GetFirstInstanceOf(class'UITacticalHUD');
	if (TacScreen != none)
	{
		Stack.SubscribeToOnInputForScreen(TacScreen, OnExploreInput);
		bSubscribed = true;
		`log("X2Access: Panel subscribed to input for UITacticalHUD", , 'X2Access');
	}
	else
	{
		`log("X2Access: Panel SubscribeInput FAILED - no UITacticalHUD", , 'X2Access');
	}
}

simulated event Removed()
{
	local UIScreenStack Stack;
	local UIScreen TacScreen;

	if (bSubscribed)
	{
		Stack = `SCREENSTACK;
		if (Stack != none)
		{
			TacScreen = Stack.GetFirstInstanceOf(class'UITacticalHUD');
			if (TacScreen != none)
				Stack.UnsubscribeFromOnInputForScreen(TacScreen, OnExploreInput);
		}
		bSubscribed = false;
	}
	super.Removed();
}

// CHOnInputDelegateImproved signature — called by Highlander input hook.
simulated function bool OnExploreInput(UIScreen TargetScreen, int iInput, int ActionMask)
{
	`log("X2Access: OnExploreInput fired! cmd=" $ iInput $ " action=" $ ActionMask, , 'X2Access');

	if (Listener != none)
		return Listener.HandleExploreInput(TargetScreen, iInput, ActionMask);

	return false;
}

defaultProperties
{
	bIsNavigable = false
}
