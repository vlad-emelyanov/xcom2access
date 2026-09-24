// UIScreenListener_KeyBindings
// Announces category changes and binding mode transitions on the key
// bindings screen (UIKeybindingsPCScreen) for blind accessibility via TTS.
//
// Limitation: The keybindings list is Flash-driven (no UIList), so we
// cannot detect individual row focus during browsing. We CAN detect:
//   - Category tab changes (General / Tactical / Avenger)
//   - Binding mode start (user presses Enter on a slot)
//   - Binding mode end (key was bound or binding was cancelled)
//
// IMPORTANT: Do NOT store UIScreen references as class variables.
// Screen listeners run on CDOs, and persistent UObject references prevent
// garbage collection of the shell world during map transitions.

class UIScreenListener_KeyBindings extends UIScreenListener;

var string AccessibilityData;
var string NavHelpData;
var int LastCategory;
var bool bWasAwaitingInput;
var int LastBoundSlot;
var bool bPolling;

event OnInit(UIScreen Screen)
{
	local UIKeybindingsPCScreen KBScreen;
	local string Title;
	local string Category;

	KBScreen = UIKeybindingsPCScreen(Screen);
	if (KBScreen == none)
		return;

	LastCategory = KBScreen.m_eBindingCategory;
	bWasAwaitingInput = false;
	LastBoundSlot = -1;

	Title = KBScreen.m_strTitle;
	Category = GetCategoryName(KBScreen, LastCategory);

	if (Title != "" && Category != "")
		AccessibilityData = Title $ ". " $ Category;
	else if (Title != "")
		AccessibilityData = Title;
	else
        AccessibilityData = class'X2AccessVocab'.default.KeyBindingsFallback;

	Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	StartPolling(Screen);

	`log("X2Access: KeyBindings OnInit, category=" $ LastCategory $ " data=" $ AccessibilityData, , 'X2Access');
}

event OnReceiveFocus(UIScreen Screen)
{
	if (UIKeybindingsPCScreen(Screen) == none)
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
	LastCategory = -1;
	bWasAwaitingInput = false;
	LastBoundSlot = -1;
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
	local UIKeybindingsPCScreen KBScreen;
	local int CurrentCategory;
	local bool bNowAwaiting;
	local int SlotIdx;
	local string Announcement;

	Screen = FindMyScreen();
	if (Screen == none || Screen.bIsRemoved || !Screen.bIsInited)
	{
		bPolling = false;
		return;
	}

	KBScreen = UIKeybindingsPCScreen(Screen);
	if (KBScreen == none)
	{
		bPolling = false;
		return;
	}

	// Category tab changed
	CurrentCategory = KBScreen.m_eBindingCategory;
	if (CurrentCategory != LastCategory)
	{
		LastCategory = CurrentCategory;
		Announcement = GetCategoryName(KBScreen, CurrentCategory);
		if (Announcement != "")
		{
			AccessibilityData = Announcement;
			Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
		}
	}

	// Binding mode transitions
	bNowAwaiting = KBScreen.m_bAwaitingInputForBind;
	SlotIdx = KBScreen.m_iKeySlotBeingBound;

	if (bNowAwaiting && !bWasAwaitingInput)
	{
		// Entered binding mode — announce what is being bound
		LastBoundSlot = SlotIdx;
		Announcement = GetBindingLabel(KBScreen, SlotIdx);
		if (Announcement != "")
            AccessibilityData = class'X2AccessVocab'.static.FillS(
                class'X2AccessVocab'.default.KeyBindingPrompt, Announcement) $ " " $ KBScreen.m_strPressKeyLabel;
		else
			AccessibilityData = KBScreen.m_strPressKeyLabel;
		Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	}
	else if (!bNowAwaiting && bWasAwaitingInput)
	{
		// Exited binding mode — key was bound or cancelled
        AccessibilityData = class'X2AccessVocab'.default.KeyBound;
		Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	}

	bWasAwaitingInput = bNowAwaiting;

	// Re-schedule
	if (bPolling && Screen != none && !Screen.bIsRemoved)
		Screen.SetTimer(0.15, false, nameof(PollFocus), self);
}

function UIScreen FindMyScreen()
{
	local UIScreenStack Stack;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return none;
	return Stack.GetFirstInstanceOf(class'UIKeybindingsPCScreen');
}

// Return the localized category name for the given category index.
function string GetCategoryName(UIKeybindingsPCScreen KBScreen, int Category)
{
	switch (Category)
	{
		case 0: return KBScreen.m_strGeneralBindingsCategoryLabel;
		case 1: return KBScreen.m_strTacticalBindingsCategoryLabel;
		case 2: return KBScreen.m_strAvengerBindingsCategoryLabel;
	}
	return "";
}

// Return the user-facing label for the binding at the given slot index.
function string GetBindingLabel(UIKeybindingsPCScreen KBScreen, int SlotIdx)
{
	if (SlotIdx >= 0 && SlotIdx < KBScreen.m_arrBindings.Length)
		return KBScreen.m_arrBindings[SlotIdx].UserLabel;
	return "";
}

function X2Access_Speak()
{
	// Intentionally empty — native hook reads AccessibilityData
}

defaultProperties
{
	ScreenClass = class'UIKeybindingsPCScreen'
}
