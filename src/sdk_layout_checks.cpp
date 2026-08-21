// Compile-time cross-check of every hand-transcribed engine offset against the regenerated
// SDK's COMPILED layouts (pack 0x4 since the 2026-08-19 generator fixes — before that the
// compiled layouts drifted from the engine and only the dump comments could be trusted).
//
// This TU emits no code and changes no behavior: the runtime keeps reading through
// game_ui.cpp's SEH-guarded raw primitives with explicit offsets (the chokepoint the hard
// rules require). What this buys is that a future regeneration, game patch, or transcription
// typo turns into a BUILD ERROR here instead of a silent wrong read in-game.
//
// Bitfield flag dwords (panel/button/checkbox/dropdown state bits) cannot be offsetof'd;
// for those the nearest plain neighbor is asserted instead, which pins the same layout.

#include "SdkHeaders.hpp"

#include <cstddef>
#include <type_traits>

namespace {

// ---- core types ----
// GameDefines.hpp defines these OUTSIDE any pack region (unlike every SDK_HEADERS file, which
// packs itself), so their layout follows whatever pack the including TU happens to be in.
static_assert(offsetof(FNameEntry, HashNext) == 0x0C, "FNameEntry::HashNext (needs pack 4)");
static_assert(offsetof(FNameEntry, Name) == 0x14, "FNameEntry::Name (needs pack 4)");

static_assert(std::is_same_v<FString::ElementType, const wchar_t>,
    "FString must be wide (UTF16_FSTRING) — XCOM 2's runtime FString is TArray<wchar_t>");
static_assert(sizeof(FScriptDelegate) == 0x20, "engine delegate stride (see generator notes)");

// ---- UFunction dispatch fields (mouse_gate.cpp rewrites FunctionFlags + Func) ----
// Only FunctionFlags can be pinned: the generated header declares it uint64_t where the
// engine's field is a DWORD (the dump comment says 0x0004), so everything after 0xD0 in the
// COMPILED class sits 4 bytes late — the dump comments are authoritative, the compiled
// layout is not. The runtime offsets mouse_gate.cpp uses (flags dword 0xD0, Func 0xF0) come
// from the dump comments + the live-process verification in project_ue3_offsets, and every
// FUNC_Native call the mod makes exercises the same pair.
static_assert(offsetof(UFunction, FunctionFlags) == 0xD0, "UFunction::FunctionFlags");

// ---- UIPanel (flag dword at 0x284 is bitfields; MCName pins the layout up to it) ----
static_assert(offsetof(AUIPanel, MCName) == 0x27C, "UIPanel::MCName");
static_assert(offsetof(AUIPanel, CachedTooltipId) == 0x2AC, "UIPanel::CachedTooltipId");
static_assert(offsetof(AUIPanel, Movie) == 0x2B8, "UIPanel::Movie");

// ---- UIMovie / presentation / tooltip chain ----
static_assert(offsetof(UUIMovie, PRES) == 0x204, "UIMovie::PRES");
static_assert(offsetof(AXComPresentationLayerBase, m_kTooltipMgr) == 0x55C,
    "XComPresentationLayerBase::m_kTooltipMgr");
static_assert(offsetof(AUITooltipMgr, TextTooltip) == 0x414, "UITooltipMgr::TextTooltip");
static_assert(offsetof(AUITooltipMgr, Tooltips) == 0x41C, "UITooltipMgr::Tooltips");
static_assert(offsetof(AUITooltip, Id) == 0x3D8, "UITooltip::Id");
static_assert(offsetof(AUITextTooltip, sTitle) == 0x48C, "UITextTooltip::sTitle");
static_assert(offsetof(AUITextTooltip, sBody) == 0x49C, "UITextTooltip::sBody");
static_assert(sizeof(FTTextTooltipData) == 0xAC, "TTextTooltipData stride");
static_assert(offsetof(FTTextTooltipData, Id) == 0x00, "TTextTooltipData::Id");
static_assert(offsetof(FTTextTooltipData, sTitle) == 0x44, "TTextTooltipData::sTitle");
static_assert(offsetof(FTTextTooltipData, sBody) == 0x54, "TTextTooltipData::sBody");

// ---- buttons / text widgets ----
static_assert(offsetof(AUIButton, Text) == 0x3D8, "UIButton::Text");
static_assert(offsetof(AUIScrollingText, Text) == 0x3D8, "UIScrollingText::Text");
static_assert(offsetof(AUIScrollingText, HtmlText) == 0x3E8, "UIScrollingText::HtmlText");

// ---- main menu shell ----
static_assert(offsetof(AUIShell, MAINMENU) == 0x4CC, "UIShell::MAINMENU");
static_assert(offsetof(AUIShell, TickerText) == 0x4F4, "UIShell::TickerText");
static_assert(offsetof(AUIFinalShell, m_my2KButton) == 0x5C8, "UIFinalShell::m_my2KButton");
static_assert(offsetof(AUIFinalShell, FiraxisLiveClient) == 0x608,
    "UIFinalShell::FiraxisLiveClient");

// ---- settings screen ----
static_assert(offsetof(AUIOptionsPCScreen, m_strTitle) == 0x42C, "options title");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabAudio) == 0x4DC, "audio tab name");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabVideo) == 0x4BC, "video tab name");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabGraphics) == 0x4CC, "graphics tab name");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabGameplay) == 0x4EC, "gameplay tab name");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabInterface) == 0x4FC, "interface tab name");
static_assert(offsetof(AUIOptionsPCScreen, List) == 0x140C, "options List");
static_assert(offsetof(AUIOptionsPCScreen, m_arrMechaItems) == 0x1414, "options rows");
static_assert(offsetof(AUIOptionsPCScreen, m_iCurrentTab) == 0x1424, "options current tab");
static_assert(offsetof(AUIOptionsPCScreen, GPUAutoDetectButton) == 0x1434, "GPU button");
static_assert(offsetof(AUIOptionsPCScreen, SaveAndExitButton) == 0x1444, "save button");
static_assert(offsetof(AUIOptionsPCScreen, ExitWitoutChangesButton) == 0x144C, "discard button");
static_assert(offsetof(AUIOptionsPCScreen, CreditsButton) == 0x1454, "credits button");
static_assert(offsetof(AUIList, SelectedIndex) == 0x404, "UIList::SelectedIndex");

// ---- settings rows (flag dword at 0x414 is bitfields; neighbors pin it) ----
static_assert(offsetof(AUIMechaListItem, Desc) == 0x404, "row Desc");
static_assert(offsetof(AUIMechaListItem, BG) == 0x40C, "row BG");
static_assert(offsetof(AUIMechaListItem, Value) == 0x418, "row Value");
static_assert(offsetof(AUIMechaListItem, Spinner) == 0x428, "row Spinner");
static_assert(offsetof(AUIMechaListItem, dropdown) == 0x430, "row Dropdown");
static_assert(offsetof(AUIMechaListItem, Button) == 0x438, "row Button");
static_assert(offsetof(AUIMechaListItem, Checkbox) == 0x440, "row Checkbox");
static_assert(offsetof(AUIMechaListItem, Slider) == 0x448, "row Slider");
static_assert(offsetof(AUIMechaListItem, Type) == 0x450, "row Type");

// ---- new game flow: difficulty screen (flag dword at 0x8A8 is bitfields; neighbors pin it) ----
static_assert(offsetof(AUIShellDifficulty, m_strSelectDifficulty) == 0x410, "difficulty title");
static_assert(offsetof(AUIShellDifficulty, m_strChangeDifficulty) == 0x420,
    "difficulty title (in game)");
static_assert(offsetof(AUIShellDifficulty, SecondWavePanelTitle) == 0x770, "second wave title");
static_assert(offsetof(AUIShellDifficulty, SecondWaveToolTips) == 0x810, "second wave tooltips");
static_assert(offsetof(AUIShellDifficulty, m_DifficultyRookieMechaItem) == 0x828, "Rookie row");
static_assert(offsetof(AUIShellDifficulty, m_DifficultyVeteranMechaItem) == 0x830, "Veteran row");
static_assert(offsetof(AUIShellDifficulty, m_DifficultyCommanderMechaItem) == 0x838,
    "Commander row");
static_assert(offsetof(AUIShellDifficulty, m_DifficultyLegendMechaItem) == 0x840, "Legend row");
static_assert(offsetof(AUIShellDifficulty, m_SecondWaveButton) == 0x860, "second wave button");
static_assert(offsetof(AUIShellDifficulty, m_SecondWaveList) == 0x870, "second wave list");
static_assert(offsetof(AUIShellDifficulty, m_TutorialMechaItem) == 0x880, "tutorial row");
static_assert(offsetof(AUIShellDifficulty, m_FirstTimeVOMechaItem) == 0x888, "first-time VO row");
static_assert(offsetof(AUIShellDifficulty, m_SubtitlesMechaItem) == 0x890, "subtitles row");
static_assert(offsetof(AUIShellDifficulty, m_SoundtrackMechaItem) == 0x898, "soundtrack row");
static_assert(offsetof(AUIShellDifficulty, m_StartButton) == 0x8A0, "difficulty start button");
static_assert(offsetof(AUIShellDifficulty, m_iSelectedDifficulty) == 0x8AC,
    "difficulty flags precede m_iSelectedDifficulty at 0x8A8");

// ---- new game flow: Ironman choice (flags at 0x498; DevStrategyShell pins them) ----
static_assert(offsetof(AUIChooseIronMan, m_strIronmanTitle) == 0x420, "ironman title");
static_assert(offsetof(AUIChooseIronMan, m_strIronmanBody) == 0x430, "ironman body");
static_assert(offsetof(AUIChooseIronMan, m_StartWithoutIronmanButton) == 0x480,
    "start-without-ironman button");
static_assert(offsetof(AUIChooseIronMan, m_StartButton) == 0x490, "enable-ironman button");

// ---- new game flow: optional narrative content ----
static_assert(offsetof(AUIShellNarrativeContent, NarrativeContentTitle) == 0x410,
    "narrative title");
static_assert(offsetof(AUIShellNarrativeContent, IntegratedDLCDesc) == 0x4B0,
    "integrated DLC description");
static_assert(offsetof(AUIShellNarrativeContent, m_List) == 0x4E0, "narrative DLC list");
static_assert(offsetof(AUIShellNarrativeContent, m_StartButton) == 0x4E8,
    "narrative start button");
static_assert(offsetof(AUIShellNarrativeContent, m_XpacknarrativeMechaItem) == 0x4F0,
    "xpack narrative row");
static_assert(offsetof(AUIShellNarrativeContent, DLCInfos) == 0x510, "narrative DLC infos");
static_assert(offsetof(UX2DownloadableContentInfo, NarrativeContentSummary) == 0xB4,
    "DLC narrative summary");

// ---- new game flow: soundtrack picker ----
static_assert(offsetof(AUIShellSoundtrackPicker, m_List) == 0x410, "soundtrack list");
static_assert(offsetof(AUIShellSoundtrackPicker, m_StartButton) == 0x418,
    "soundtrack start button");
// No title assert: the retail package's localized m_chooseSoundtrack is absent from the
// Community Highlander's rebuild (the SDK's SrcOrig for this class never declared it), so the
// field is not one a single offset can name — see the reader's note in game_ui.cpp.

// ---- in-game pause menu (flags at 0x418 are bitfields; List pins them) ----
static_assert(offsetof(AUIPauseMenu, m_iCurrentSelection) == 0x410, "pause menu selection");
static_assert(offsetof(AUIPauseMenu, List) == 0x41C, "pause menu list");
static_assert(offsetof(AUIPauseMenu, m_sPauseMenu) == 0x42C, "pause menu title");

// ---- save / load (docs/save-load-recon.md §4) ----
// The two screens declare the same three fields first, which is why one set of offsets reads
// both; the asserts pin that coincidence so a future divergence is a build error.
static_assert(offsetof(AUILoadGame, m_iCurrentSelection) == 0x410, "load screen selection");
static_assert(offsetof(AUILoadGame, m_arrListItems) == 0x424, "load screen rows");
static_assert(offsetof(AUISaveGame, m_iCurrentSelection) == 0x410, "save screen selection");
static_assert(offsetof(AUISaveGame, m_arrListItems) == 0x424, "save screen rows");
static_assert(offsetof(AUILoadGame, m_sLoadTitle) == 0x448, "load screen title");
// m_bLoadInProgress is a bit in the dword at 0x434; List is its nearest plain neighbour.
static_assert(offsetof(AUILoadGame, List) == 0x438, "UILoadGame flags precede List at 0x434");
static_assert(offsetof(AUISaveGame, m_SaveStage) == 0x504, "save stage");
static_assert(offsetof(AUISaveGame, m_sSaveTitle) == 0x508, "save screen title");
static_assert(offsetof(AUISaveLoadGameListItem, AcceptButton) == 0x3D8, "save row accept button");
static_assert(offsetof(AUISaveLoadGameListItem, DeleteButton) == 0x3E0, "save row delete button");
static_assert(offsetof(AUISaveLoadGameListItem, Index) == 0x4B0, "save row index");
static_assert(offsetof(AUISaveLoadGameListItem, Id) == 0x4B4, "save row id");

// ---- the Scaleform bridge (game_ui.cpp's FlashCallRaw: one FString in, the answer at 0x10) ----
// One block shape serves every GFxMoviePlayer path function, which is what makes the whole
// ActionScript surface reachable through a single helper. Pinned against all four.
static_assert(offsetof(UGFxMoviePlayer_execActionScriptString_Params, ReturnValue) == 0x10,
    "ActionScriptString return");
static_assert(offsetof(UGFxMoviePlayer_execActionScriptInt_Params, ReturnValue) == 0x10,
    "ActionScriptInt return");
static_assert(offsetof(UGFxMoviePlayer_execGetVariableString_Params, ReturnValue) == 0x10,
    "GetVariableString return");
static_assert(offsetof(UGFxMoviePlayer_execActionScriptString_Params, Path) == 0x00,
    "ActionScript path argument");
// The numeric returns the dev harness's /flash offers on top of those. GetVariableBool's return
// is a BITFIELD, so offsetof is not available on it — the block size pins it instead: an FString
// path is 0x10, so a 0x14 struct can only be the return dword sitting at 0x10.
static_assert(offsetof(UGFxMoviePlayer_execGetVariableNumber_Params, ReturnValue) == 0x10,
    "GetVariableNumber return");
static_assert(offsetof(UGFxMoviePlayer_execActionScriptFloat_Params, ReturnValue) == 0x10,
    "ActionScriptFloat return");
static_assert(sizeof(UGFxMoviePlayer_execGetVariableBool_Params) == 0x14,
    "GetVariableBool return dword follows the path FString");

// ---- text input dialog (m_kData is a struct; its fields are offsets INTO it) ----
static_assert(offsetof(AUIInputDialogue, m_kData) == 0x410, "input dialog data");
static_assert(offsetof(FTInputDialogData, strTitle) == 0x00, "input dialog prompt");
static_assert(offsetof(FTInputDialogData, strInputBoxText) == 0x10, "input dialog prefill");
static_assert(offsetof(FTInputDialogData, iMaxChars) == 0x20, "input dialog max chars");
static_assert(offsetof(FTInputDialogData, DialogType) == 0x24, "input dialog type");

// ---- UIListItemString (flags at 0x414 are bitfields; ConfirmText pins them) ----
static_assert(offsetof(AUIListItemString, Text) == 0x3F4, "UIListItemString::Text");
static_assert(offsetof(AUIListItemString, ConfirmText) == 0x418,
    "UIListItemString flags precede ConfirmText at 0x414");

// ---- modal dialog ----
static_assert(offsetof(AUIDialogueBox, m_arrData) == 0x434, "dialog queue");
static_assert(offsetof(AUIDialogueBox, strAccept) == 0x444, "dialog accept label");
static_assert(offsetof(AUIDialogueBox, strCancel) == 0x454, "dialog cancel label");
static_assert(sizeof(FTDialogueBoxData) == 0xD0, "TDialogueBoxData stride");
static_assert(offsetof(FTDialogueBoxData, strTitle) == 0x08, "TDialogueBoxData::strTitle");
static_assert(offsetof(FTDialogueBoxData, strText) == 0x18, "TDialogueBoxData::strText");

// ---- the game's own screen stack (index 0 is the TOP: Push inserts at 0) ----
static_assert(offsetof(UUIScreenStack, Screens) == 0x6C, "UIScreenStack::Screens");
static_assert(offsetof(UUIScreenStack, PRES) == 0x64, "UIScreenStack::PRES");

// ---- generic UIList / UIPanel traversal ----
static_assert(offsetof(AUIList, ItemContainer) == 0x3D8, "UIList::ItemContainer");
static_assert(offsetof(AUIPanel, ChildPanels) == 0x2E8, "UIPanel::ChildPanels");

// ---- row widgets ----
static_assert(offsetof(AUICheckbox, Text) == 0x3D8, "UICheckbox::Text (bChecked bits follow)");
static_assert(offsetof(AUISlider, Percent) == 0x3E8, "UISlider::Percent");
static_assert(offsetof(AUIListItemSpinner, Value) == 0x3DC, "UIListItemSpinner::Value");
static_assert(offsetof(AUIDropdown, Items) == 0x3F8, "UIDropdown::Items");
static_assert(offsetof(AUIDropdown, SelectedItem) == 0x418, "UIDropdown::SelectedItem");

// ---- tutorial mission: the modal popup ----
static_assert(offsetof(AUITutorialBox, Title) == 0x410, "UITutorialBox::Title");
static_assert(offsetof(AUITutorialBox, Desc) == 0x420, "UITutorialBox::Desc");
static_assert(offsetof(AUITutorialBox, ButtonHelp) == 0x430, "UITutorialBox::ButtonHelp");

// ---- tutorial mission: world messages (the readable flyover channel) ----
static_assert(offsetof(AXComPresentationLayerBase, m_kWorldMessageManager) == 0x344,
    "XComPresentationLayerBase::m_kWorldMessageManager");
static_assert(offsetof(AUIWorldMessageMgr, m_arrMsgs) == 0x450, "UIWorldMessageMgr::m_arrMsgs");
static_assert(offsetof(AUIWorldMessageMgr, m_arrActiveMessages) == 0x460,
    "UIWorldMessageMgr::m_arrActiveMessages");
static_assert(offsetof(AUIWorldMessageMgr, BladeHUDMessage) == 0x480,
    "UIWorldMessageMgr::BladeHUDMessage");
static_assert(sizeof(FTHUDMsg) == 0x78, "THUDMsg stride");
static_assert(offsetof(FTHUDMsg, m_sId) == 0x00, "THUDMsg::m_sId");
static_assert(offsetof(FTHUDMsg, m_txtMsg) == 0x10, "THUDMsg::m_txtMsg");
static_assert(offsetof(FTHUDMsg, m_kFollowObject) == 0x30, "THUDMsg::m_kFollowObject");
// The visible/important bits live in the dword at 0x34; the plain member after it pins them.
static_assert(offsetof(FTHUDMsg, m_eBehavior) == 0x38,
    "THUDMsg flag dword precedes m_eBehavior at 0x34");
static_assert(offsetof(FTHUDMsg, m_eType) == 0x74, "THUDMsg::m_eType");

// ---- tutorial mission: the replay manager ----
static_assert(offsetof(AXComReplayMgr, CurrentHistoryFrame) == 0x274,
    "XComReplayMgr::CurrentHistoryFrame");
// bInReplay/bInTutorial are bitfields in the dword at 0x278; the plain member after them pins it.
static_assert(offsetof(AXComReplayMgr, StepForwardStopFrame) == 0x27C,
    "XComReplayMgr flag dword precedes StepForwardStopFrame at 0x278");
// bIsTutorialAllowingControls is a bitfield in the dword at 0x294; NextPlayerInputFrame pins it.
static_assert(offsetof(AXComTutorialMgr, NextPlayerInputFrame) == 0x298,
    "XComTutorialMgr flag dword precedes NextPlayerInputFrame at 0x294");
static_assert(offsetof(AXComTutorialMgr, InvalidSubmissionCnt) == 0x31C,
    "XComTutorialMgr::InvalidSubmissionCnt");
static_assert(offsetof(AXComTutorialMgr, NextPlayerAbilityContext) == 0x338,
    "XComTutorialMgr::NextPlayerAbilityContext");

// ---- tactical HUD (m_isMenuRaised is a bitfield in the dword at 0x410; the plain struct
// member right after it pins the layout) ----
static_assert(offsetof(AUITacticalHUD, CachedVisibility) == 0x414,
    "UITacticalHUD::CachedVisibility (m_isMenuRaised dword precedes it at 0x410)");

// ---- squad select (docs/squad-select-recon.md §3) ----
// The screen's own bitfield sits at 0x478 (bLaunched / bReceivedWalkupEvent / bNoCancel /
// bDisableLoadout); SquadMinimums is the plain member before it and SoldierSlotCount the one
// before that, so the pair pins the dword from both sides.
// UIArmory_Loadout: the two lists, the panel headings, the strip tooltips and the flag dword
// at 0x0A74 — pinned by LocTag before it and StrippedUnits after, since a bitfield has no
// offsetof of its own. m_strInventoryLabels is a SIXTY-SEVEN entry array here rather than the
// stock seventeen, which is a second independent confirmation that this dump is Highlander-built
// (CHItemSlot extends EInventorySlot).
static_assert(offsetof(AUIArmory_Loadout, ActiveList) == 0x4FC, "loadout active list");
static_assert(offsetof(AUIArmory_Loadout, EquippedList) == 0x50C, "loadout equipped list");
static_assert(offsetof(AUIArmory_Loadout, LockerList) == 0x51C, "loadout locker list");
static_assert(offsetof(AUIArmory_Loadout, m_strInventoryTitle) == 0x53C, "loadout left title");
static_assert(offsetof(AUIArmory_Loadout, m_strLockerTitle) == 0x54C, "loadout right title");
static_assert(offsetof(AUIArmory_Loadout, m_strTooltipStripItems) == 0x9DC, "loadout tip items");
static_assert(offsetof(AUIArmory_Loadout, m_strTooltipStripItemsDisabled) == 0x9EC,
    "loadout tip items off");
static_assert(offsetof(AUIArmory_Loadout, m_strTooltipStripGear) == 0x9FC, "loadout tip gear");
static_assert(offsetof(AUIArmory_Loadout, m_strTooltipStripGearDisabled) == 0xA0C,
    "loadout tip gear off");
static_assert(offsetof(AUIArmory_Loadout, m_strTooltipStripWeapons) == 0xA1C, "loadout tip wpn");
static_assert(offsetof(AUIArmory_Loadout, m_strTooltipStripWeaponsDisabled) == 0xA2C,
    "loadout tip wpn off");
static_assert(offsetof(AUIArmory_Loadout, m_strMakeAvailable) == 0xA4C, "loadout make available");
static_assert(offsetof(AUIArmory_Loadout, LocTag) == 0xA6C,
    "UIArmory_Loadout flag dword follows LocTag at 0xA74");
static_assert(offsetof(AUIArmory_Loadout, StrippedUnits) == 0xA78,
    "UIArmory_Loadout flag dword precedes StrippedUnits at 0xA74");

// UIArmory_LoadoutItem: every word the row RETAINS, plus the dword at 0x043C pinned between
// Count and EquipmentSlot. `disabledText` is declared at 0x03F8 and never assigned by any setter
// — the reason goes straight into Flash — which is why the companion recomputes it.
static_assert(offsetof(AUIArmory_LoadoutItem, Title) == 0x3D8, "loadout row title");
static_assert(offsetof(AUIArmory_LoadoutItem, Subtitle) == 0x3E8, "loadout row subtitle");
static_assert(offsetof(AUIArmory_LoadoutItem, SlotType) == 0x428, "loadout row slot label");
static_assert(offsetof(AUIArmory_LoadoutItem, Count) == 0x438, "loadout row count");
static_assert(offsetof(AUIArmory_LoadoutItem, EquipmentSlot) == 0x440,
    "UIArmory_LoadoutItem flag dword sits between Count and EquipmentSlot at 0x43C");
static_assert(offsetof(AUIArmory_LoadoutItem, DropItemButton) == 0x458, "loadout row drop button");

static_assert(offsetof(AUISquadSelect, m_iSelectedSlot) == 0x410, "squad select slot");
static_assert(offsetof(AUISquadSelect, m_kSlotList) == 0x420, "squad select card list");
static_assert(offsetof(AUISquadSelect, LaunchButton) == 0x428, "squad select launch button");
static_assert(offsetof(AUISquadSelect, SoldierSlotCount) == 0x460, "squad select slot count");
static_assert(offsetof(AUISquadSelect, SquadMinimums) == 0x468,
    "UISquadSelect flag dword follows SquadMinimums at 0x478");
static_assert(offsetof(AUISquadSelect, m_strLaunch) == 0x480, "squad select launch label");
static_assert(offsetof(AUISquadSelect, m_strMission) == 0x490, "squad select mission label");
static_assert(offsetof(AUISquadSelect, m_strStripGear) == 0x4C0, "squad select strip gear");
static_assert(offsetof(AUISquadSelect, m_strStripWeapons) == 0x4F0, "squad select strip weapons");
static_assert(offsetof(AUISquadSelect, m_strStripItems) == 0x520, "squad select strip items");
static_assert(offsetof(AUISquadSelect, m_strBuildItems) == 0x550, "squad select build items");
static_assert(offsetof(AUISquadSelect, m_strBoostSoldier) == 0x570, "squad select boost soldier");

// The card's bitfield is at 0x3DC (bDisabled / bDisabledLoadout / bDisabledEdit /
// bDisabledDismiss); SlotIndex before it and CannotEditSlots after it pin the dword.
static_assert(offsetof(AUISquadSelect_ListItem, SlotIndex) == 0x3D8, "squad card slot index");
static_assert(offsetof(AUISquadSelect_ListItem, CannotEditSlots) == 0x3E0,
    "UISquadSelect_ListItem flag dword precedes CannotEditSlots at 0x3DC");
static_assert(offsetof(AUISquadSelect_ListItem, UtilitySlots) == 0x408, "squad card utility list");
static_assert(offsetof(AUISquadSelect_ListItem, BondIcon) == 0x410, "squad card bond icon");
static_assert(offsetof(AUISquadSelect_ListItem, m_eActiveButton) == 0x4E0,
    "squad card active button");

static_assert(offsetof(AUISquadSelect_UtilityItem, Button) == 0x3E0, "utility slot button");
static_assert(offsetof(AUISquadSelect_UtilityItem, SlotIndex) == 0x3F0, "utility slot index");
static_assert(offsetof(AUISquadSelect_UtilityItem, SlotType) == 0x3F4, "utility slot type");

// ---- inside a room: UIFacility, the Avenger HUD's header and its shortcut submenu ----
//
// This layer reads ACROSS two screens: the room screen on top and UIAvengerHUD below it, which
// is where the game keeps the room's name and the room's menu. Both halves are pinned here.
static_assert(offsetof(AUIFacility, m_kStaffSlotContainer) == 0x608, "facility staff container");
static_assert(offsetof(AUIFacility, m_kRoomContainer) == 0x610, "facility room container");
static_assert(offsetof(AUIFacility, UpgradeButton) == 0x648, "facility upgrade button");
static_assert(offsetof(AUIRoomContainer, RoomFuncs) == 0x3DC, "room bottom-bar buttons");

// ---- the personnel roster (UIPersonnel and its ten subclasses) ----
//
// One offset for the whole family. Everything else is either a companion read (the rows retain
// only a UnitRef) or a function call — the screen's own SwitchTab, and the sort header's own
// mouse handler.
static_assert(offsetof(AUIPersonnel, m_kList) == 0x45C, "personnel roster list");

static_assert(offsetof(AUIAvengerHUD, Shortcuts) == 0x43C, "avenger HUD shortcut bar");
static_assert(offsetof(AUIAvengerHUD, FacilityHeader) == 0x454, "avenger HUD facility header");

static_assert(offsetof(AUIX2ScreenHeader, LocationField) == 0x3D8, "screen header location");
static_assert(offsetof(AUIX2ScreenHeader, RoomField) == 0x3E8, "screen header room name");

static_assert(offsetof(AUIAvengerShortcuts, CategoryLabels) == 0x3F0, "shortcut category labels");
static_assert(sizeof(((AUIAvengerShortcuts*)nullptr)->CategoryLabels) == 0x50,
    "CategoryLabels is five FStrings, stride 0x10 — the reader indexes it by hand");
static_assert(offsetof(AUIAvengerShortcuts, List) == 0x7F8, "shortcut submenu list");
static_assert(offsetof(AUIAvengerShortcuts, CurrentCategory) == 0x83C, "shortcut current room");

static_assert(offsetof(AUIStaffContainer, Title) == 0x3D8, "staff container title");
static_assert(offsetof(AUIStaffContainer, Skill) == 0x408, "staff container crew count");
static_assert(offsetof(AUIStaffContainer, StaffSlots) == 0x418, "staff container slots");

// UIListItemString's bitfield is at 0x414 (bDisabled / bIsBad / bIsGood / bIsWarning); Text
// before it and ConfirmText after it pin the dword, the same way the squad card's is pinned.
static_assert(offsetof(AUIListItemString, Text) == 0x3F4, "list item text");
static_assert(offsetof(AUIListItemString, ConfirmText) == 0x418,
    "UIListItemString flag dword precedes ConfirmText at 0x414");

// ---- an un-excavated room: UIRoom, its build-slot container and the personnel picker ----
//
// UIRoom is NOT a UIFacility subclass and shares none of its layout, so these are pinned
// separately even though the field names rhyme.
static_assert(offsetof(AUIRoom, RoomRef) == 0x600, "room state reference");
static_assert(offsetof(AUIRoom, m_kStaffSlotContainer) == 0x608, "room build-slot container");
static_assert(offsetof(AUIRoom, ExcavateButton) == 0x628, "room excavate button");
static_assert(offsetof(AUIRoom, m_strExcavate) == 0x640, "room excavate label");

static_assert(offsetof(AUIStaffContainer, m_kPersonnelDropDown) == 0x428, "staff picker");
static_assert(offsetof(AUIPersonnel_DropDown, m_kList) == 0x3E0, "staff picker list");
static_assert(offsetof(AUIPersonnel_DropDown, m_staff) == 0x3F8, "staff picker candidates");

// ---- the commodity list (UIInventory and its dozen subclasses) ----
//
// Exactly one offset for the whole family. Everything else that recipe needs is either a
// function call (SwitchTab, SetSelectedIndex, the row's OnUnrealCommand) or a localized string
// the companion reads by name — which is what makes this the cheapest screen in the layer to
// keep working across a Highlander bump or a mod that rebuilds it.
static_assert(offsetof(AUIInventory, List) == 0x498, "commodity list");

} // namespace
