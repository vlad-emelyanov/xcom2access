// UIScreenListener_SoundtrackPicker
// Announces focused items on the soundtrack picker screen
// (UIShellSoundtrackPicker) for blind accessibility via TTS.
//
// IMPORTANT: Do NOT store UIScreen references as class variables.
// Screen listeners run on CDOs, and persistent UObject references prevent
// garbage collection of the shell world during map transitions.

class UIScreenListener_SoundtrackPicker extends UIScreenListener;

var string AccessibilityData;
var string NavHelpData;
var int LastSelectedIndex;
var bool bPolling;

event OnInit(UIScreen Screen)
{
	local UIShellSoundtrackPicker Picker;
	local string Title;

	Picker = UIShellSoundtrackPicker(Screen);
	if (Picker == none)
		return;

	LastSelectedIndex = -1;

	// m_chooseSoundtrack is not accessible; use the same label from UIOptionsPCScreen
	Title = class'UIOptionsPCScreen'.default.m_strAudioLabel_Soundtrack;
	if (Title != "")
		AccessibilityData = Title;
	else
        AccessibilityData = class'X2AccessVocab'.default.ScreenSoundtrackFallback;

	Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	StartPolling(Screen);

	`log("X2Access: SoundtrackPicker OnInit, data=" $ AccessibilityData, , 'X2Access');
}

event OnReceiveFocus(UIScreen Screen)
{
	if (UIShellSoundtrackPicker(Screen) == none)
		return;

	StartPolling(Screen);
}

event OnLoseFocus(UIScreen Screen)
{
	StopPolling(Screen);
}

event OnRemoved(UIScreen Screen)
{
	StopPolling(Screen);
	LastSelectedIndex = -1;
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
	local UIShellSoundtrackPicker Picker;
	local int SelIdx;
	local string Announcement;

	Screen = FindMyScreen();
	if (Screen == none || Screen.bIsRemoved || !Screen.bIsInited)
	{
		bPolling = false;
		return;
	}

	Picker = UIShellSoundtrackPicker(Screen);
	if (Picker == none || Picker.m_List == none)
	{
		bPolling = false;
		return;
	}

	SelIdx = Picker.m_List.SelectedIndex;

	if (SelIdx != -1 && SelIdx != LastSelectedIndex)
	{
		LastSelectedIndex = SelIdx;
		Announcement = GetSoundtrackName(SelIdx);

		if (Announcement != "")
		{
			AccessibilityData = Announcement;
			Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
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
	return Stack.GetFirstInstanceOf(class'UIShellSoundtrackPicker');
}

function string GetSoundtrackName(int Index)
{
	local array<string> Names;

	Names = class'UIOptionsPCScreen'.default.m_kstr_SoundtrackStrings;

	if (Index >= 0 && Index < Names.Length)
		return Names[Index];

	return "";
}

function X2Access_Speak()
{
	// Intentionally empty — native hook reads AccessibilityData
}

defaultProperties
{
	ScreenClass = class'UIShellSoundtrackPicker'
}
