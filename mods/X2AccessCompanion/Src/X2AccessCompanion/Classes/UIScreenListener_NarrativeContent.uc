// UIScreenListener_NarrativeContent
// Announces focused items on the DLC/narrative content selection screen
// (UIShellNarrativeContent) for blind accessibility via TTS.
//
// Screen layout:
//   m_XpacknarrativeMechaItem — WOTC narrative checkbox (NOT in m_List)
//   m_List index 0  — Integrated DLC checkbox
//   m_List index 1+ — per-DLC narrative checkboxes
//   m_StartButton   — Continue button
//
// IMPORTANT: Do NOT store UIScreen references as class variables.
// Screen listeners run on CDOs, and persistent UObject references prevent
// garbage collection of the shell world during map transitions.

class UIScreenListener_NarrativeContent extends UIScreenListener;

var string AccessibilityData;
var string NavHelpData;
var int LastFocusedIndex;
var bool bPolling;
var bool bMapsBuilt;
var array<string> ItemLabels;
var array<string> ItemDescriptions;

const IDX_XPACK = 998;
const IDX_START = 999;

event OnInit(UIScreen Screen)
{
	local UIShellNarrativeContent NarrScreen;

	NarrScreen = UIShellNarrativeContent(Screen);
	if (NarrScreen == none)
		return;

	LastFocusedIndex = -1;
	bMapsBuilt = false;

	// Enable Start button navigation (vanilla disables it for controller)
	if (NarrScreen.m_StartButton != none)
		NarrScreen.m_StartButton.EnableNavigation();

	AccessibilityData = NarrScreen.NarrativeContentTitle;
	Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	StartPolling(Screen);

	`log("X2Access: NarrativeContent OnInit, data=" $ AccessibilityData, , 'X2Access');
}

event OnReceiveFocus(UIScreen Screen)
{
	local UIShellNarrativeContent NarrScreen;

	NarrScreen = UIShellNarrativeContent(Screen);
	if (NarrScreen == none)
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
	local UIShellNarrativeContent NarrScreen;
	local int FocusedIdx;
	local string Announcement;

	Screen = FindMyScreen();
	if (Screen == none || Screen.bIsRemoved || !Screen.bIsInited)
	{
		bPolling = false;
		return;
	}

	NarrScreen = UIShellNarrativeContent(Screen);
	if (NarrScreen == none)
	{
		bPolling = false;
		return;
	}

	// Lazily build maps after BuildMenu has populated the list
	if (!bMapsBuilt && NarrScreen.m_List != none && NarrScreen.m_List.ItemCount > 0)
	{
		BuildItemMaps(NarrScreen);
		NavHelpData = class'X2AccessHelper'.static.CollectNavHelp(NarrScreen.NavHelp);
		bMapsBuilt = true;
	}

	FocusedIdx = FindFocusedItem(NarrScreen);

	if (FocusedIdx != -1 && FocusedIdx != LastFocusedIndex)
	{
		LastFocusedIndex = FocusedIdx;
		Announcement = BuildAnnouncement(NarrScreen, FocusedIdx);
		`log("X2Access: NarrativeContent focus=" $ FocusedIdx $ " announce=" $ Announcement, , 'X2Access');
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
	return Stack.GetFirstInstanceOf(class'UIShellNarrativeContent');
}

function int FindFocusedItem(UIShellNarrativeContent NarrScreen)
{
	local int i;
	local UIMechaListItem ListItem;

	// WOTC narrative item (not in m_List — it's a standalone panel)
	if (NarrScreen.m_XpacknarrativeMechaItem != none && NarrScreen.m_XpacknarrativeMechaItem.bIsFocused)
		return IDX_XPACK;

	// Start button
	if (NarrScreen.m_StartButton != none && NarrScreen.m_StartButton.bIsFocused)
		return IDX_START;

	// List items (Integrated DLC at 0, DLC items at 1+)
	if (NarrScreen.m_List != none)
	{
		for (i = 0; i < NarrScreen.m_List.ItemCount; i++)
		{
			ListItem = UIMechaListItem(NarrScreen.m_List.GetItem(i));
			if (ListItem != none && ListItem.bIsFocused)
				return i;
		}
	}

	return -1;
}

function string BuildAnnouncement(UIShellNarrativeContent NarrScreen, int FocusedIdx)
{
	local UIMechaListItem ListItem;
	local string Label;
	local string CheckState;
	local string Desc;

	// WOTC narrative item
	if (FocusedIdx == IDX_XPACK)
	{
		Label = NarrScreen.XPACKNarrativeOption;
		CheckState = class'X2AccessHelper'.static.GetCheckboxState(NarrScreen.m_XpacknarrativeMechaItem);
		if (NarrScreen.m_bTutorial)
			Desc = NarrScreen.XPACKNarrativeOptionDesc $ " " $ NarrScreen.XPACKNarrativeWithTutorialOptionDesc;
		else
			Desc = NarrScreen.XPACKNarrativeOptionDesc;

		if (Label == "")
			return "";
		return Label $ ". " $ CheckState $ ". " $ Desc;
	}

	// Start button
	if (FocusedIdx == IDX_START)
	{
		if (NarrScreen.m_StartButton != none)
			return NarrScreen.m_StartButton.Text;
		return "";
	}

	// List item
	if (NarrScreen.m_List == none || FocusedIdx >= NarrScreen.m_List.ItemCount)
		return "";

	ListItem = UIMechaListItem(NarrScreen.m_List.GetItem(FocusedIdx));
	if (ListItem == none)
		return "";

	// Label from pre-built map
	if (FocusedIdx < ItemLabels.Length)
		Label = ItemLabels[FocusedIdx];
	if (Label == "")
		Label = "Item " $ (FocusedIdx + 1);

	// Checkbox state
	CheckState = class'X2AccessHelper'.static.GetCheckboxState(ListItem);

	// Description from pre-built map
	if (FocusedIdx < ItemDescriptions.Length)
		Desc = ItemDescriptions[FocusedIdx];

	if (CheckState != "")
		Label $= ". " $ CheckState;
	if (Desc != "")
		Label $= ". " $ Desc;

	return Label;
}

// Build label/description arrays matching m_List indices.
// Called lazily from PollFocus after BuildMenu has populated the list.
// DLCInfos on the screen is private, so we query the online event manager directly.
function BuildItemMaps(UIShellNarrativeContent NarrScreen)
{
	local XComOnlineEventMgr OnlineMgr;
	local array<X2DownloadableContentInfo> AllDLCInfos;
	local int i;

	ItemLabels.Length = 0;
	ItemDescriptions.Length = 0;

	// Index 0: Integrated DLC (always first when list is non-empty)
	ItemLabels.AddItem(NarrScreen.IntegratedDLCOption);
	ItemDescriptions.AddItem(NarrScreen.IntegratedDLCDesc);

	// Index 1+: DLC items with optional narrative content
	OnlineMgr = `ONLINEEVENTMGR;
	AllDLCInfos = OnlineMgr.GetDLCInfos(false);
	for (i = 0; i < AllDLCInfos.Length; i++)
	{
		if (AllDLCInfos[i].bHasOptionalNarrativeContent)
		{
			ItemLabels.AddItem(AllDLCInfos[i].NarrativeContentLabel);
			ItemDescriptions.AddItem(AllDLCInfos[i].NarrativeContentSummary);
		}
	}
}

function X2Access_Speak()
{
	// Intentionally empty — native hook reads AccessibilityData
}

defaultProperties
{
	ScreenClass = class'UIShellNarrativeContent'
}
