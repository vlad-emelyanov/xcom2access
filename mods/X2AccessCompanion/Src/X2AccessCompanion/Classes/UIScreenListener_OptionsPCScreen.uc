// UIScreenListener_OptionsPCScreen
// Announces focused items, tab changes, and value changes on the settings
// screen (UIOptionsPCScreen) for blind accessibility via TTS.
//
// Handles all 5 tabs: Audio, Video, Graphics, Gameplay, Interface.
// Detects slider step changes, spinner cycles, checkbox toggles,
// and dropdown selections to announce each value change.
//
// IMPORTANT: Do NOT store UIScreen references as class variables.
// Screen listeners run on CDOs, and persistent UObject references prevent
// garbage collection of the shell world during map transitions.

class UIScreenListener_OptionsPCScreen extends UIScreenListener;

var string AccessibilityData;
var string NavHelpData;
var int LastTab;
var int LastSelectedIndex;
var string LastValueString;
var bool bPolling;

// Tab indices matching EUI_PCOptions_Tabs
const TAB_AUDIO     = 0;
const TAB_VIDEO     = 1;
const TAB_GRAPHICS  = 2;
const TAB_GAMEPLAY  = 3;
const TAB_INTERFACE = 4;

event OnInit(UIScreen Screen)
{
	local UIOptionsPCScreen OptsScreen;
	local string TabName;

	OptsScreen = UIOptionsPCScreen(Screen);
	if (OptsScreen == none)
		return;

	LastTab = OptsScreen.m_iCurrentTab;
	LastSelectedIndex = -1;
	LastValueString = "";

	NavHelpData = class'X2AccessHelper'.static.CollectNavHelp(OptsScreen.NavHelp);

	TabName = GetTabName(OptsScreen, LastTab);
	if (TabName != "")
		AccessibilityData = OptsScreen.m_strTitle $ ". " $ TabName $ " tab";
	else
		AccessibilityData = OptsScreen.m_strTitle;

	Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
	StartPolling(Screen);

	`log("X2Access: OptionsPCScreen OnInit, tab=" $ LastTab $ " data=" $ AccessibilityData, , 'X2Access');
}

event OnReceiveFocus(UIScreen Screen)
{
	if (UIOptionsPCScreen(Screen) == none)
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
	LastTab = -1;
	LastSelectedIndex = -1;
	LastValueString = "";
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
	local UIOptionsPCScreen OptsScreen;
	local int CurrentTab;
	local int SelIdx;
	local string CurrentValue;
	local string Label;
	local UIMechaListItem Item;

	Screen = FindMyScreen();
	if (Screen == none || Screen.bIsRemoved || !Screen.bIsInited)
	{
		bPolling = false;
		return;
	}

	OptsScreen = UIOptionsPCScreen(Screen);
	if (OptsScreen == none || OptsScreen.List == none)
	{
		bPolling = false;
		return;
	}

	CurrentTab = OptsScreen.m_iCurrentTab;

	// Tab changed — announce and return; item focus will be picked up next poll cycle
	if (CurrentTab != LastTab)
	{
		LastTab = CurrentTab;
		LastSelectedIndex = -1;
		LastValueString = "";

		NavHelpData = class'X2AccessHelper'.static.CollectNavHelp(OptsScreen.NavHelp);

		Label = GetTabName(OptsScreen, CurrentTab);
		if (Label != "")
		{
			AccessibilityData = Label $ " tab";
			Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
		}

		if (bPolling && Screen != none && !Screen.bIsRemoved)
			Screen.SetTimer(0.15, false, nameof(PollFocus), self);
		return;
	}

	SelIdx = OptsScreen.List.SelectedIndex;

	// Focus is on the tab bar (SelectedIndex == -1): nothing to announce beyond the tab change
	if (SelIdx >= 0 && SelIdx < OptsScreen.m_arrMechaItems.Length)
	{
		Item = OptsScreen.m_arrMechaItems[SelIdx];
		CurrentValue = class'X2AccessHelper'.static.GetItemValueString(Item);

		if (SelIdx != LastSelectedIndex)
		{
			// New item focused — announce label + value
			LastSelectedIndex = SelIdx;
			LastValueString = CurrentValue;

			Label = GetItemLabel(OptsScreen, CurrentTab, SelIdx);
			if (Label != "")
			{
				if (CurrentValue != "")
					AccessibilityData = Label $ ". " $ CurrentValue;
				else
					AccessibilityData = Label;
				Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
			}
		}
		else if (CurrentValue != LastValueString && CurrentValue != "")
		{
			// Same item, value changed (slider step, spinner cycle, checkbox toggle)
			LastValueString = CurrentValue;
			AccessibilityData = CurrentValue;
			Screen.SetTimer(0.01, false, nameof(X2Access_Speak), self);
		}
	}
	else if (SelIdx == -1 && LastSelectedIndex != -1)
	{
		// Moved from list to tab bar — reset so re-entering list announces the item
		LastSelectedIndex = -1;
		LastValueString = "";
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
	return Stack.GetFirstInstanceOf(class'UIOptionsPCScreen');
}

// Return the localized tab name for the given tab index.
function string GetTabName(UIOptionsPCScreen OptsScreen, int TabIdx)
{
	switch (TabIdx)
	{
		case TAB_AUDIO:     return OptsScreen.m_strTabAudio;
		case TAB_VIDEO:     return OptsScreen.m_strTabVideo;
		case TAB_GRAPHICS:  return OptsScreen.m_strTabGraphics;
		case TAB_GAMEPLAY:  return OptsScreen.m_strTabGameplay;
		case TAB_INTERFACE: return OptsScreen.m_strTabInterface;
	}
	return "";
}

// Return the localized label for the item at the given tab + list index.
// Gameplay tab has dynamic PartPack sliders after the static items.
function string GetItemLabel(UIOptionsPCScreen OptsScreen, int TabIdx, int ItemIdx)
{
	switch (TabIdx)
	{
		case TAB_AUDIO:
			return GetAudioItemLabel(OptsScreen, ItemIdx);
		case TAB_VIDEO:
			return GetVideoItemLabel(OptsScreen, ItemIdx);
		case TAB_GRAPHICS:
			return GetGraphicsItemLabel(OptsScreen, ItemIdx);
		case TAB_GAMEPLAY:
			return GetGameplayItemLabel(OptsScreen, ItemIdx);
		case TAB_INTERFACE:
			return GetInterfaceItemLabel(OptsScreen, ItemIdx);
	}

	return GetDescFallback(OptsScreen, ItemIdx);
}

function string GetAudioItemLabel(UIOptionsPCScreen S, int Idx)
{
	switch (Idx)
	{
		case 0: return S.m_strAudioLabel_MasterVolume;
		case 1: return S.m_strAudioLabel_VoiceVolume;
		case 2: return S.m_strAudioLabel_SoundEffectVolume;
		case 3: return S.m_strAudioLabel_MusicVolume;
		case 4: return S.m_strAudioLabel_VOIPVolume;
		case 5: return S.m_strAudioLabel_VOIPPushToTalk;
		case 6: return S.m_strAudioLabel_EnableSoldierSpeech;
		case 7: return S.m_strAudioLabel_ForeignLanguages;
		case 8: return S.m_strAudioLabel_AmbientVO;
		case 9: return S.m_strGameplayLabel_LadderNarrative;  // TLE item
		case 10: return S.m_strAudioLabel_Soundtrack;          // TLE item
	}
	return GetDescFallback(S, Idx);
}

function string GetVideoItemLabel(UIOptionsPCScreen S, int Idx)
{
	switch (Idx)
	{
		case 0: return S.m_strVideoLabel_Mode;
		case 1: return S.m_strVideoLabel_Resolution;
		case 2: return S.m_strVideoLabel_Gamma;
		case 3: return S.m_strVideolabel_MouseLock;
		case 4: return S.m_strVideoLabel_VSyncToggle;
		case 5: return S.m_strVideoLabel_FRSmoothingToggle;
	}
	return GetDescFallback(S, Idx);
}

function string GetGraphicsItemLabel(UIOptionsPCScreen S, int Idx)
{
	switch (Idx)
	{
		case 0:  return S.m_strGraphicsLabel_Preset;
		case 1:  return S.m_strGraphicsLabel_AntiAliasing;
		case 2:  return S.m_strGraphicsLabel_AmbientOcclusion;
		case 3:  return S.m_strGraphicsLabel_Decals;
		case 4:  return S.m_strGraphicsLabel_Shadow;
		case 5:  return S.m_strGraphicsLabel_ShadowQuality;
		case 6:  return S.m_strGraphicsLabel_TextureDetail;
		case 7:  return S.m_strGraphicsLabel_TextureFiltering;
		case 8:  return S.m_strGraphicsLabel_DepthOfField;
		case 9:  return S.m_strGraphicsLabel_MaxDrawDistance;
		case 10: return S.m_strGraphicsLabel_Effects;
		case 11: return S.m_strGraphicsLabel_Bloom;
		case 12: return S.m_strGraphicsLabel_DirtyLens;
		case 13: return S.m_strGraphicsLabel_SubsurfaceScattering;
		case 14: return S.m_strGraphicsLabel_ScreenSpaceReflections;
	}
	return GetDescFallback(S, Idx);
}

function string GetGameplayItemLabel(UIOptionsPCScreen S, int Idx)
{
	// Static items (enum indices 0-4)
	switch (Idx)
	{
		case 0: return S.m_strGameplayLabel_GlamCam;
		case 1: return S.m_strGameplayLabel_AutosaveToggle;
		case 2: return S.m_strGameplayLabel_ShowEnemyHealth;
		case 3: return S.m_strGameplayLabel_EnableZipMode;
		case 4: return S.m_strGameplayLabel_TargetPreviewMode;
	}

	// Dynamic PartPack sliders (index 5+): read from the item's Desc text
	return GetDescFallback(S, Idx);
}

function string GetInterfaceItemLabel(UIOptionsPCScreen S, int Idx)
{
	switch (Idx)
	{
		case 0: return S.m_strInterfaceLabel_KeyBindings;
		case 1: return S.m_strInterfaceLabel_ShowSubtitles;
		case 2: return S.m_strInterfaceLabel_EdgescrollSpeed;
		case 3: return S.m_strInterfaceLabel_InputDevice;
		case 4: return S.m_strInterfaceLabel_GeoscapeSpeed;
	}
	return GetDescFallback(S, Idx);
}

// Fallback: read label from UIMechaListItem.Desc scrolling text widget.
function string GetDescFallback(UIOptionsPCScreen S, int Idx)
{
	local UIMechaListItem Item;

	if (Idx >= 0 && Idx < S.m_arrMechaItems.Length)
	{
		Item = S.m_arrMechaItems[Idx];
		if (Item != none && Item.Desc != none)
			return Item.Desc.Text;
	}
	return "Item " $ (Idx + 1);
}

function X2Access_Speak()
{
	// Intentionally empty — native hook reads AccessibilityData
}

defaultProperties
{
	ScreenClass = class'UIOptionsPCScreen'
}
