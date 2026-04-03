// UIScreenListener_MainMenu
// Detects focused menu items on the main menu (UIFinalShell) and signals
// the native X2Access ASI to speak them via TTS.
//
// IMPORTANT: Do NOT store UIScreen references as class variables (var UIScreen ...).
// Screen listeners run on CDOs, and any persistent UObject reference prevents the
// engine from garbage-collecting the shell world during map transitions, causing a crash.
// Always find screens dynamically via the screen stack.

class UIScreenListener_MainMenu extends UIScreenListener;

var string AccessibilityData;
var string NavHelpData;
var int LastFocusedIndex;
var bool bPolling;

event OnInit(UIScreen Screen)
{
	local UIShell Shell;
	local string FocusedText;

	Shell = UIShell(Screen);
	if (Shell == none)
		return;

	LastFocusedIndex = -1;

	NavHelpData = class'X2AccessHelper'.static.CollectNavHelp(Shell.NavHelp);
	FocusedText = GetFocusedButtonText(Shell);

	if (FocusedText != "")
		AccessibilityData = "Main Menu. " $ FocusedText;
	else
		AccessibilityData = "Main Menu";

	Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	StartPolling(Screen);

	`log("X2Access: MainMenu OnInit, data=" $ AccessibilityData, , 'X2Access');
}

event OnReceiveFocus(UIScreen Screen)
{
	local UIShell Shell;
	local string FocusedText;

	Shell = UIShell(Screen);
	if (Shell == none)
		return;

	FocusedText = GetFocusedButtonText(Shell);

	if (FocusedText != "")
	{
		AccessibilityData = "Main Menu. " $ FocusedText;
		Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	}

	StartPolling(Screen);
}

event OnLoseFocus(UIScreen Screen)
{
	StopPolling(Screen);
}

event OnRemoved(UIScreen Screen)
{
	StopPolling(Screen);
	LastFocusedIndex = -1;
}

function StartPolling(UIScreen Screen)
{
	if (Screen != none && !bPolling)
	{
		bPolling = true;
		Screen.SetTimer(0.15, false, nameof(PollFocus), self);
	}
}

function StopPolling(UIScreen Screen)
{
	bPolling = false;
	if (Screen != none)
	{
		Screen.ClearTimer(nameof(PollFocus), self);
		Screen.ClearTimer(nameof(X2Access_Speak), self);
	}
}

function PollFocus()
{
	local UIScreen Screen;
	local UIShell Shell;
	local int i;
	local UIButton Btn;

	Screen = FindMyScreen();
	if (Screen == none || Screen.bIsRemoved || !Screen.bIsInited)
	{
		bPolling = false;
		return;
	}

	Shell = UIShell(Screen);
	if (Shell == none)
	{
		bPolling = false;
		return;
	}

	for (i = 0; i < Shell.MAINMENU.Length; i++)
	{
		Btn = Shell.MAINMENU[i];
		if (Btn != none && Btn.bIsFocused)
		{
			if (i != LastFocusedIndex)
			{
				LastFocusedIndex = i;
				AccessibilityData = Btn.Text;
				Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
			}
			if (bPolling)
				Screen.SetTimer(0.15, false, nameof(PollFocus), self);
			return;
		}
	}

	if (bPolling && Screen != none && !Screen.bIsRemoved)
		Screen.SetTimer(0.15, false, nameof(PollFocus), self);
}

function UIScreen FindMyScreen()
{
	local UIScreenStack Stack;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return none;
	return Stack.GetFirstInstanceOf(class'UIFinalShell');
}

function string GetFocusedButtonText(UIShell Shell)
{
	local int i;
	local UIButton Btn;

	for (i = 0; i < Shell.MAINMENU.Length; i++)
	{
		Btn = Shell.MAINMENU[i];
		if (Btn != none && Btn.bIsFocused)
		{
			LastFocusedIndex = i;
			return Btn.Text;
		}
	}

	if (Shell.MAINMENU.Length > 0 && Shell.MAINMENU[0] != none)
	{
		LastFocusedIndex = 0;
		return Shell.MAINMENU[0].Text;
	}

	return "";
}

function X2Access_Speak()
{
	// Intentionally empty — native hook reads AccessibilityData
}

defaultProperties
{
	ScreenClass = class'UIFinalShell'
}
