// UIScreenListener_Difficulty
// Announces focused items and their state on the difficulty selection screen
// (UIShellDifficulty) for blind accessibility via TTS.
//
// IMPORTANT: Do NOT store UIScreen references as class variables.
// Screen listeners run on CDOs, and persistent UObject references prevent
// garbage collection of the shell world during map transitions.

class UIScreenListener_Difficulty extends UIScreenListener;

var string AccessibilityData;
var string NavHelpData;
var int LastFocusedIndex;
var bool bPolling;
var bool bWasSecondWaveOpen;

// Focus indices for known items
const IDX_ROOKIE    = 0;
const IDX_VETERAN   = 1;
const IDX_COMMANDER = 2;
const IDX_LEGEND    = 3;
const IDX_SECONDWAVE_BTN = 4;
const IDX_TUTORIAL  = 5;
const IDX_FIRSTVO   = 6;
const IDX_SUBTITLES = 7;
const IDX_SOUNDTRACK = 8;
const IDX_START     = 9;
const IDX_SECONDWAVE_BASE = 100;

event OnInit(UIScreen Screen)
{
	local UIShellDifficulty DiffScreen;
	local string DiffName;

	DiffScreen = UIShellDifficulty(Screen);
	if (DiffScreen == none)
		return;

	LastFocusedIndex = -1;

	NavHelpData = class'X2AccessHelper'.static.CollectNavHelp(DiffScreen.NavHelp);

	// Announce screen title + current difficulty
	if (DiffScreen.m_iSelectedDifficulty >= 0
		&& DiffScreen.m_iSelectedDifficulty < DiffScreen.m_arrDifficultyTypeStrings.Length)
	{
		DiffName = DiffScreen.m_arrDifficultyTypeStrings[DiffScreen.m_iSelectedDifficulty];
	}

	if (DiffName != "")
		AccessibilityData = DiffScreen.m_strSelectDifficulty $ ". " $ DiffName $ " selected";
	else
		AccessibilityData = DiffScreen.m_strSelectDifficulty;

	// Enable Start button navigation (vanilla disables it — keyboard users need it)
	if (DiffScreen.m_StartButton != none)
		DiffScreen.m_StartButton.EnableNavigation();

	Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	StartPolling(Screen);

	`log("X2Access: Difficulty OnInit, data=" $ AccessibilityData, , 'X2Access');
}

event OnReceiveFocus(UIScreen Screen)
{
	local UIShellDifficulty DiffScreen;

	DiffScreen = UIShellDifficulty(Screen);
	if (DiffScreen == none)
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
	local UIShellDifficulty DiffScreen;
	local int FocusedIdx;
	local string Announcement;

	Screen = FindMyScreen();
	if (Screen == none || Screen.bIsRemoved || !Screen.bIsInited)
	{
		bPolling = false;
		return;
	}

	DiffScreen = UIShellDifficulty(Screen);
	if (DiffScreen == none)
	{
		bPolling = false;
		return;
	}

	// Fix vanilla bug: OpenSecondWave() calls Navigator.Clear() which breaks
	// keyboard navigation. The Enter handler only routes to the Second Wave
	// list for controllers (ISCONTROLLERACTIVE check). Fix: put m_SecondWaveList
	// directly in the screen navigator so Enter/arrows reach UIList.OnUnrealCommand.
	// Also fix checkbox items: UICheckbox.OnUnrealCommand always returns true
	// (consuming Enter) even when bReadOnly=true, preventing UIList.OnItemClicked
	// from firing. Change item Type so the checkbox handler is bypassed and set
	// OnClickDelegate to the same toggle function used by the controller path.
	if (DiffScreen.m_bSecondWaveOpen && !bWasSecondWaveOpen)
	{
		DiffScreen.Navigator.AddControl(DiffScreen.m_SecondWaveList);
		DiffScreen.Navigator.SetSelected(DiffScreen.m_SecondWaveList);
		FixSecondWaveItems(DiffScreen);
		bWasSecondWaveOpen = true;
	}
	else if (!DiffScreen.m_bSecondWaveOpen && bWasSecondWaveOpen)
	{
		bWasSecondWaveOpen = false;
	}

	FocusedIdx = FindFocusedItem(DiffScreen);

	if (FocusedIdx != -1 && FocusedIdx != LastFocusedIndex)
	{
		LastFocusedIndex = FocusedIdx;
		Announcement = BuildAnnouncement(DiffScreen, FocusedIdx);
		`log("X2Access: Difficulty focus=" $ FocusedIdx $ " announce=" $ Announcement, , 'X2Access');
		if (Announcement != "")
		{
			AccessibilityData = Announcement;
			Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
		}
	}

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
	return Stack.GetFirstInstanceOf(class'UIShellDifficulty');
}

// Check all known items for bIsFocused, return the index of the focused one
function int FindFocusedItem(UIShellDifficulty DiffScreen)
{
	local int i;
	local UIMechaListItem ListItem;

	// Difficulty radio buttons
	if (DiffScreen.m_DifficultyRookieMechaItem != none && DiffScreen.m_DifficultyRookieMechaItem.bIsFocused)
		return IDX_ROOKIE;
	if (DiffScreen.m_DifficultyVeteranMechaItem != none && DiffScreen.m_DifficultyVeteranMechaItem.bIsFocused)
		return IDX_VETERAN;
	if (DiffScreen.m_DifficultyCommanderMechaItem != none && DiffScreen.m_DifficultyCommanderMechaItem.bIsFocused)
		return IDX_COMMANDER;
	if (DiffScreen.m_DifficultyLegendMechaItem != none && DiffScreen.m_DifficultyLegendMechaItem.bIsFocused)
		return IDX_LEGEND;

	// Bottom panel items
	if (DiffScreen.m_SecondWaveButton != none && DiffScreen.m_SecondWaveButton.bIsFocused)
		return IDX_SECONDWAVE_BTN;
	if (DiffScreen.m_TutorialMechaItem != none && DiffScreen.m_TutorialMechaItem.bIsFocused)
		return IDX_TUTORIAL;
	if (DiffScreen.m_FirstTimeVOMechaItem != none && DiffScreen.m_FirstTimeVOMechaItem.bIsFocused)
		return IDX_FIRSTVO;
	if (DiffScreen.m_SubtitlesMechaItem != none && DiffScreen.m_SubtitlesMechaItem.bIsFocused)
		return IDX_SUBTITLES;
	if (DiffScreen.m_SoundtrackMechaItem != none && DiffScreen.m_SoundtrackMechaItem.bIsFocused)
		return IDX_SOUNDTRACK;

	// Start button
	if (DiffScreen.m_StartButton != none && DiffScreen.m_StartButton.bIsFocused)
		return IDX_START;

	// Second Wave list items (when panel is open)
	if (DiffScreen.m_bSecondWaveOpen && DiffScreen.m_SecondWaveList != none)
	{
		for (i = 0; i < DiffScreen.m_SecondWaveList.ItemCount; i++)
		{
			ListItem = UIMechaListItem(DiffScreen.m_SecondWaveList.GetItem(i));
			if (ListItem != none && ListItem.bIsFocused)
				return IDX_SECONDWAVE_BASE + i;
		}
	}

	return -1;
}

// Build the TTS announcement string for the focused item
function string BuildAnnouncement(UIShellDifficulty DiffScreen, int FocusedIdx)
{
	local string Label;
	local string StatusText;
	local int SWIdx;
	local UIMechaListItem ListItem;

	// Difficulty radio buttons (indices 0-3)
	if (FocusedIdx >= IDX_ROOKIE && FocusedIdx <= IDX_LEGEND)
	{
		if (FocusedIdx < DiffScreen.m_arrDifficultyTypeStrings.Length)
			Label = DiffScreen.m_arrDifficultyTypeStrings[FocusedIdx];

		if (DiffScreen.m_iSelectedDifficulty == FocusedIdx)
			StatusText = "Selected";

		if (Label == "")
			return "";

		if (StatusText != "")
			return Label $ ". " $ StatusText;
		return Label;
	}

	// Bottom panel items — read labels from localized strings (Desc.Text is empty for these)
	switch (FocusedIdx)
	{
		case IDX_SECONDWAVE_BTN:
			return DiffScreen.m_strDifficulty_SecondWaveButtonLabel;

		case IDX_TUTORIAL:
			return DiffScreen.m_strDifficultyEnableTutorial $ ". " $ class'X2AccessHelper'.static.GetCheckboxState(DiffScreen.m_TutorialMechaItem);

		case IDX_FIRSTVO:
			return DiffScreen.m_strDifficultySuppressFirstTimeNarrativeVO $ ". " $ class'X2AccessHelper'.static.GetCheckboxState(DiffScreen.m_FirstTimeVOMechaItem);

		case IDX_SUBTITLES:
			Label = class'UIOptionsPCScreen'.default.m_strInterfaceLabel_ShowSubtitles;
			return Label $ ". " $ class'X2AccessHelper'.static.GetCheckboxState(DiffScreen.m_SubtitlesMechaItem);

		case IDX_SOUNDTRACK:
			return class'UIOptionsPCScreen'.default.m_strAudioLabel_Soundtrack;

		case IDX_START:
			return DiffScreen.m_strDifficulty_Accept;
	}

	// Second Wave list items — labels from SecondWaveDescriptions array
	if (FocusedIdx >= IDX_SECONDWAVE_BASE)
	{
		SWIdx = FocusedIdx - IDX_SECONDWAVE_BASE;
		if (SWIdx < DiffScreen.SecondWaveDescriptions.Length)
		{
			Label = DiffScreen.SecondWaveDescriptions[SWIdx];
			if (DiffScreen.m_SecondWaveList != none && SWIdx < DiffScreen.m_SecondWaveList.ItemCount)
			{
				ListItem = UIMechaListItem(DiffScreen.m_SecondWaveList.GetItem(SWIdx));
				if (ListItem != none)
					Label $= ". " $ class'X2AccessHelper'.static.GetCheckboxState(ListItem);
			}
			return Label;
		}
	}

	return "";
}

// Fix SecondWave items for keyboard Enter: change Type so HandleCustomControls
// skips checkbox routing (which consumes Enter without toggling when bReadOnly=true),
// and set OnClickDelegate to the controller toggle function.
function FixSecondWaveItems(UIShellDifficulty DiffScreen)
{
	local int i;
	local UIMechaListItem SWItem;

	for (i = 0; i < DiffScreen.m_SecondWaveList.ItemCount; i++)
	{
		SWItem = UIMechaListItem(DiffScreen.m_SecondWaveList.GetItem(i));
		if (SWItem != none)
		{
			SWItem.Type = EUILineItemType_Description;
			SWItem.OnClickDelegate = DiffScreen.OnAdvancedOptionsClicked;
		}
	}
}

function X2Access_Speak()
{
	// Intentionally empty — native hook reads AccessibilityData
}

defaultProperties
{
	ScreenClass = class'UIShellDifficulty'
}
