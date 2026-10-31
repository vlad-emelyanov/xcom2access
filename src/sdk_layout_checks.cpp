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
static_assert(offsetof(AUIPanel, MCPath) == 0x2B0, "UIPanel::MCPath");
static_assert(offsetof(AUIPanel, Movie) == 0x2B8, "UIPanel::Movie");

// ---- UIScreen (its OWN flag dword at 0x3E0 is bitfields; Package and the float after it pin
// the layout on both sides). bShowDuringCinematic (0x10) is what MenuNavigator reads to tell a
// screen a cutscene HIDES from one it leaves live — the game's own test in HideForCinematics.
static_assert(offsetof(AUIScreen, Package) == 0x3D8, "UIScreen::Package");
static_assert(offsetof(AUIScreen, IdleDistortionAnimationLength) == 0x3E4,
    "UIScreen flag dword precedes IdleDistortionAnimationLength at 0x3E4");

// ---- UIMovie / presentation / tooltip chain ----
static_assert(offsetof(UUIMovie, PRES) == 0x204, "UIMovie::PRES");

// ---- the Flash command-queue capture (src/flash_shadow.cpp) ----
//
// The movie-wide queue every panel's MC.QueueString lands in, read at the one instant it is
// whole: inside UIMovie.CopyCommandQueue, before ProcessQueuedCommands clears it. The panel
// identity a batch is keyed by is UIMCController::CacheIndex, reached through UIPanel::MC.
static_assert(offsetof(UUIMovie, CommandQueue) == 0x270, "UIMovie::CommandQueue");
static_assert(offsetof(AUIPanel, MC) == 0x2D0, "UIPanel::MC");
static_assert(offsetof(UUIMCController, CacheIndex) == 0x68, "UIMCController::CacheIndex");
// FASValue is the queue's element, and the decoder strides over it by hand. Type is a byte, B a
// one-bit field in the dword at 0x04, N a float, S an FString — pinned here because a stride or
// a member that moved would make the decoder read one operand as another's type.
static_assert(sizeof(FASValue) == 0x1C, "FASValue stride");
static_assert(offsetof(FASValue, Type) == 0x00, "FASValue::Type");
static_assert(offsetof(FASValue, N) == 0x08, "FASValue::N");
static_assert(offsetof(FASValue, S) == 0x0C, "FASValue::S");
// The panel every Build*Alert pushes its words through, so also the cache index those words are
// keyed by in the capture (game_ui.cpp's ReadAlertCaptured).
static_assert(offsetof(AUIAlert, LibraryPanel) == 0x1EE8, "UIAlert::LibraryPanel");
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
// The only link back to a tooltip registered straight on the manager, with no panel to key on.
static_assert(offsetof(FTTextTooltipData, targetPath) == 0x1C, "TTextTooltipData::targetPath");
// The per-element hover callback ReadCallbackTooltip fires; FScriptDelegate is Object then
// FunctionName (pinned by its stride above).
static_assert(offsetof(FTTextTooltipData, del_OnMouseIn) == 0x6C, "TTextTooltipData::del_OnMouseIn");
static_assert(offsetof(FScriptDelegate, FunctionName) == 0x08, "FScriptDelegate::FunctionName");
// UIListItemString forwards SetTooltipText (and SetDisabled's reason) to this child.
static_assert(offsetof(AUIListItemString, ButtonBG) == 0x454, "UIListItemString::ButtonBG");

// ---- promotion / level-up (UIArmory_Promotion + subclasses) ----
//
// The flag dwords (UIArmory_PromotionItem at 0x420, UIArmory_PromotionHeroColumn at 0x40C) are
// bitfields and cannot be offsetof'd, so they are pinned by BRACKETING: the field immediately
// before and the one immediately after. If the dword moves, one of those four fails.
static_assert(offsetof(AUIArmory, UnitReference) == 0x450, "UIArmory::UnitReference");
static_assert(offsetof(AUIArmory_Promotion, ClassRowItem) == 0x53C, "promotion class row");
static_assert(offsetof(AUIArmory_Promotion, SelectedAbilityIndex) == 0x614, "promotion branch");
static_assert(offsetof(AUIArmory_Promotion, List) == 0x618, "promotion list");
static_assert(offsetof(AUIArmory_PromotionItem, Rank) == 0x3D8, "promotion row rank");
static_assert(offsetof(AUIArmory_PromotionItem, AbilityIcon1) == 0x404, "promotion row icon 1");
static_assert(offsetof(AUIArmory_PromotionItem, AbilityIcon2) == 0x40C, "promotion row icon 2");
static_assert(offsetof(AUIArmory_PromotionItem, SelectedAbility) == 0x41C, "promotion row cursor");
static_assert(offsetof(AUIArmory_PromotionItem, m_strNewRank) == 0x424, "promotion row flags end");
static_assert(offsetof(AUIArmory_PromotionHero, Columns) == 0x624, "hero promotion columns");
static_assert(offsetof(AUIArmory_PromotionHero, m_iCurrentlySelectedColumn) == 0x6E4,
    "hero promotion column cursor");
static_assert(offsetof(AUIArmory_PromotionHeroColumn, Rank) == 0x3D8, "hero column rank");
static_assert(offsetof(AUIArmory_PromotionHeroColumn, AbilityIcons) == 0x3FC, "hero column icons");
static_assert(offsetof(AUIArmory_PromotionHeroColumn, m_strNewRank) == 0x410,
    "hero column flags end");
static_assert(offsetof(AUIArmory_PromotionHeroColumn, m_iPanelIndex) == 0x420,
    "hero column panel cursor");

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
static_assert(offsetof(AUIMechaListItem, ColorChip) == 0x420, "row ColorChip");
static_assert(offsetof(AUIBGBox, BGColor) == 0x3D8, "UIBGBox::BGColor");
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

// ---- the Tactical Legacy Pack hub (UITLEHub) ----
static_assert(offsetof(AUITLEHub, m_strTitle) == 0x418, "TLE hub title");
static_assert(offsetof(AUITLEHub, m_strSubtitle) == 0x428, "TLE hub subtitle");
static_assert(offsetof(AUITLEHub, m_LadderTitle) == 0x438, "TLE hub ladder title");
static_assert(offsetof(AUITLEHub, m_LadderDetails) == 0x448, "TLE hub ladder details");
static_assert(offsetof(AUITLEHub, m_SkirmishTitle) == 0x458, "TLE hub skirmish title");
static_assert(offsetof(AUITLEHub, m_SkirmishDetails) == 0x468, "TLE hub skirmish details");
static_assert(offsetof(AUITLEHub, m_ChallengeTitle) == 0x478, "TLE hub challenge title");
static_assert(offsetof(AUITLEHub, m_ChallengeDetails) == 0x488, "TLE hub challenge details");
static_assert(offsetof(AUITLEHub, m_New) == 0x498, "TLE hub NEW badge");
static_assert(offsetof(AUITLEHub, m_Launch) == 0x4A8, "TLE hub LAUNCH word");
static_assert(offsetof(AUITLEHub, m_LadderWins) == 0x4B8, "TLE hub ladder wins label");
static_assert(offsetof(AUITLEHub, m_HighestScore) == 0x4C8, "TLE hub high score label");
static_assert(offsetof(AUITLEHub, m_UnlockLabel) == 0x4D8, "TLE hub unlock label");
static_assert(offsetof(AUITLEHub, m_CompletedLabel) == 0x4E8, "TLE hub completed label");
static_assert(offsetof(AUITLEHub, m_VictoryLabel) == 0x4F8, "TLE hub victories label");
static_assert(offsetof(AUITLEHub, m_CreatedLabel) == 0x508, "TLE hub created label");
static_assert(offsetof(AUITLEHub, m_MissionsLabel) == 0x518, "TLE hub missions label");
static_assert(offsetof(AUITLEHub, ChallengeButton) == 0x548, "TLE hub challenge button");
static_assert(offsetof(AUITLEHub, LadderButton) == 0x550, "TLE hub ladder button");
static_assert(offsetof(AUITLEHub, SkirmishButton) == 0x558, "TLE hub skirmish button");

// ---- the Legacy Operations menu (UITLE_LadderModeMenu) and its row ----
static_assert(offsetof(AUITLE_LadderModeMenu, List) == 0x420, "ladder menu procedural list");
static_assert(offsetof(AUITLE_LadderModeMenu, NarrativeList) == 0x428, "ladder menu narrative list");
static_assert(offsetof(AUITLE_LadderModeMenu, ClickedList) == 0x430, "ladder menu clicked list");
static_assert(offsetof(AUITLE_LadderModeMenu, clickedIndex) == 0x448, "ladder menu clicked index");
static_assert(offsetof(AUITLE_LadderModeMenu, LadderSoldierInfoButtons) == 0x498,
    "ladder menu soldier buttons");
static_assert(offsetof(AUITLE_LadderModeMenu, m_ScreenTitle) == 0x4B0, "ladder menu title");
static_assert(offsetof(AUITLE_LadderModeMenu, m_strAbandonLadder) == 0x4C0, "ladder menu abandon");
static_assert(offsetof(AUITLE_LadderModeMenu, m_strDeleteLadder) == 0x4D0, "ladder menu delete");
static_assert(offsetof(AUITLE_LadderModeMenu, m_strBeginLadder) == 0x4E0, "ladder menu begin");
static_assert(offsetof(AUITLE_LadderModeMenu, m_strContinueLadder) == 0x4F0, "ladder menu continue");
static_assert(offsetof(AUITLE_LadderModeMenu, m_Medals) == 0x530, "ladder menu medals label");
static_assert(offsetof(AUITLE_LadderModeMenu, m_RandomLadderHeader) == 0x5A0,
    "ladder menu random header");
static_assert(offsetof(AUITLE_LadderModeMenu, m_NarrativeLaddersHeader) == 0x5C0,
    "ladder menu narrative header");
// bDisabled (0x3E0, bit 0) is a bitfield and cannot be offsetof'd; it is bracketed by the state
// pointer before it and the first localized string after it.
static_assert(offsetof(AUITLENarrativeLadder_ListItem, m_LadderData) == 0x3D8, "ladder row state");
static_assert(offsetof(AUITLENarrativeLadder_ListItem, m_LadderInprogress) == 0x3E4,
    "ladder row in-progress word (bDisabled dword sits at 0x3E0)");
static_assert(offsetof(AUITLE_LadderModeMenu_execOnSelectedChange_Params, ContainerList) == 0x0
        && offsetof(AUITLE_LadderModeMenu_execOnSelectedChange_Params, ItemIndex) == 0x8,
    "ladder menu OnSelectedChange params");
static_assert(offsetof(AUITLE_LadderModeMenu_execOnLadderClicked_Params, ContainerList) == 0x0
        && offsetof(AUITLE_LadderModeMenu_execOnLadderClicked_Params, ItemIndex) == 0x8,
    "ladder menu OnLadderClicked params");
static_assert(offsetof(AUITLE_LadderModeMenu_execOnClickSoldierInfo_Params, Panel) == 0x0
        && offsetof(AUITLE_LadderModeMenu_execOnClickSoldierInfo_Params, Cmd) == 0x8,
    "ladder menu OnClickSoldierInfo params");

// ---- the ladder difficulty page (UITLE_LadderDifficulty) ----
static_assert(offsetof(AUITLE_LadderDifficulty, m_DifficultyRookieMechaItem) == 0x420,
    "ladder difficulty rookie row");
static_assert(offsetof(AUITLE_LadderDifficulty, m_DifficultyLegendMechaItem) == 0x438,
    "ladder difficulty legend row");
static_assert(offsetof(AUITLE_LadderDifficulty, m_FirstTimeVOMechaItem) == 0x448,
    "ladder difficulty narrative row");
static_assert(offsetof(AUITLE_LadderDifficulty, m_IronmanMechaItem) == 0x450,
    "ladder difficulty ironman row");
static_assert(offsetof(AUITLE_LadderDifficulty, m_StartButton) == 0x468, "ladder difficulty start");
static_assert(offsetof(AUITLE_LadderDifficulty, m_iSelectedDifficulty) == 0x470,
    "ladder difficulty selected");

// ---- the Legacy Pack's Challenge Archives menu (UITLE_ChallengeModeMenu) ----
static_assert(offsetof(AUITLE_ChallengeModeMenu, List) == 0x420, "challenge menu list");
static_assert(offsetof(AUITLE_ChallengeModeMenu, m_ScreenTitle) == 0x44C, "challenge menu title");
static_assert(offsetof(AUITLE_ChallengeModeMenu, m_strButtonLabels) == 0x500,
    "challenge menu sort labels");
static_assert(sizeof(AUITLE_ChallengeModeMenu::m_strButtonLabels) == 0x50,
    "challenge menu sort labels: five FStrings at 0x10 stride");
// m_bFlipSort (0x550, bit 0) is a bitfield and cannot be offsetof'd; it is bracketed by the
// labels array ending at 0x550 and m_eSortType at 0x554.
static_assert(offsetof(AUITLE_ChallengeModeMenu, m_eSortType) == 0x554, "challenge menu sort type");
static_assert(offsetof(AUITLEChallenge_ListItem, Data) == 0x3D8, "challenge row data");
static_assert(offsetof(FChallengeListItemData, ChallengeListDataIndex) == 0x00,
    "challenge row data index");
static_assert(offsetof(FChallengeListItemData, MissionName) == 0x04, "challenge row mission");
static_assert(offsetof(FChallengeListItemData, MissionObjective) == 0x14,
    "challenge row objective");
static_assert(offsetof(FChallengeListItemData, MapName) == 0x24, "challenge row map");
static_assert(offsetof(FChallengeListItemData, Squad) == 0x44, "challenge row squad");
static_assert(offsetof(FChallengeListItemData, Enemies) == 0x54, "challenge row enemies");
static_assert(offsetof(AUITLE_ChallengeModeMenu_execOnChallengeClicked_Params, ContainerList) == 0x0,
    "OnChallengeClicked list");
static_assert(offsetof(AUITLE_ChallengeModeMenu_execOnChallengeClicked_Params, ListItemIndex) == 0x8,
    "OnChallengeClicked index");

// ---- the My2K screen (UIFiraxisLiveLogin, game_ui_my2k.cpp) ----
static_assert(offsetof(AUIFiraxisLiveLogin, m_Generic1Button) == 0x440, "My2K generic 1 button");
static_assert(offsetof(AUIFiraxisLiveLogin, m_Generic2Button) == 0x448, "My2K generic 2 button");
static_assert(offsetof(AUIFiraxisLiveLogin, m_Generic4Button) == 0x450, "My2K generic 4 button");
static_assert(offsetof(AUIFiraxisLiveLogin, m_UsernameButton) == 0x458, "My2K username button");
static_assert(offsetof(AUIFiraxisLiveLogin, m_PasswordButton) == 0x460, "My2K password button");
static_assert(offsetof(AUIFiraxisLiveLogin, m_eulaTitle) == 0x470, "My2K document title");
static_assert(offsetof(AUIFiraxisLiveLogin, m_eulaText) == 0x478, "My2K document text");
static_assert(offsetof(AUIFiraxisLiveLogin, m_eulaButtons) == 0x488, "My2K document rows");
static_assert(offsetof(AUIFiraxisLiveLogin, m_InitOKLabel) == 0x548, "My2K OK label");
static_assert(offsetof(AUIFiraxisLiveLogin, m_2KTitle) == 0x6CC, "My2K title");
static_assert(offsetof(AUIFiraxisLiveLogin, m_2kMessage) == 0x6DC, "My2K message");

// ---- the Avenger Report (UIFacilitySummary) ----
// m_bFlipSort (0x410, bit 0) is a bitfield and cannot be offsetof'd; m_eSortType follows at 0x414.
static_assert(offsetof(AUIFacilitySummary, m_eSortType) == 0x414, "facility summary sort type");
static_assert(offsetof(AUIFacilitySummary, m_kHeader) == 0x418, "facility summary header");
static_assert(offsetof(AUIFacilitySummary, m_kList) == 0x428, "facility summary list");
static_assert(offsetof(AUIFacilitySummary, m_strTitle) == 0x45C, "facility summary title");
static_assert(offsetof(AUIFacilitySummary_HeaderButton, SortType) == 0x4CC,
    "facility sort header type");
static_assert(offsetof(AUIFacilitySummary_HeaderButton, m_strButtonLabels) == 0x4D8,
    "facility sort header labels");
static_assert(sizeof(AUIFacilitySummary_HeaderButton::m_strButtonLabels) == 0x50,
    "facility sort header labels: five FStrings at 0x10 stride");
static_assert(offsetof(AUIFacilitySummary_ListItem, FacilityRef) == 0x3D8, "facility row ref");
static_assert(offsetof(AUIFacilitySummary_ListItem, m_FacilityName) == 0x3DC,
    "facility row template name");
static_assert(offsetof(AUIFacilitySummary_HeaderButton_execOnMouseEvent_Params, Cmd) == 0x0
        && offsetof(AUIFacilitySummary_HeaderButton_execOnMouseEvent_Params, args) == 0x4,
    "facility sort header OnMouseEvent");
static_assert(offsetof(AUIFacilitySummary_ListItem_execOnUnrealCommand_Params, Arg) == 0x4,
    "facility row OnUnrealCommand (SendRowCommand's block)");

// ---- the Tactical Legacy Pack's Skirmish Mode menu (UITLE_SkirmishModeMenu) ----
static_assert(offsetof(AUITLE_SkirmishModeMenu, List) == 0x420, "skirmish list");
static_assert(offsetof(AUITLE_SkirmishModeMenu, ContinueButton) == 0x428, "skirmish start button");
static_assert(offsetof(AUITLE_SkirmishModeMenu, m_CurrentState) == 0x48C, "skirmish state");
static_assert(sizeof(AUITLE_SkirmishModeMenu::m_CurrentState) == 1, "skirmish state is a byte");
static_assert(offsetof(AUITLE_SkirmishModeMenu, m_arrSubtitles) == 0x540, "skirmish subtitles");
static_assert(sizeof(AUITLE_SkirmishModeMenu::m_arrSubtitles) == 21 * 0x10,
    "skirmish subtitles: 21 x FString");
static_assert(offsetof(AUITLE_SkirmishModeMenu, m_StartGame) == 0x690, "skirmish START GAME word");
static_assert(offsetof(AUITLE_SkirmishModeMenu, m_ScreenTitle) == 0x710, "skirmish title");

// ---- Tactical Legacy Pack, mission side (game_ui_tle_mission.cpp) ----
static_assert(offsetof(AUILadderModeScoringDialog, m_ContinueButton) == 0x410, "ladder scoring continue");
static_assert(offsetof(AUILadderModeScoringDialog, m_TotalLabel) == 0x418, "ladder scoring total");
static_assert(offsetof(AUILadderModeScoringDialog, m_ScoreDescription1) == 0x4C8, "ladder scoring desc 1");
static_assert(offsetof(AUILadderModeScoringDialog, m_ScoreDescription2) == 0x4D8, "ladder scoring desc 2");
static_assert(offsetof(AUILadderModeScoringDialog, m_LadderHeader) == 0x528, "ladder scoring header");
static_assert(offsetof(AUIChallengeModeScoringDialog, m_ContinueButton) == 0x410, "challenge scoring continue");
static_assert(offsetof(AUIChallengeModeScoringDialog, m_Header) == 0x418, "challenge scoring header");
static_assert(offsetof(AUIChallengeModeScoringDialog, m_TotalLabel) == 0x428, "challenge scoring total");
static_assert(offsetof(AUIChallengeModeScoringDialog, m_ScoreDescription1) == 0x4D8, "challenge scoring desc 1");
static_assert(offsetof(AUIChallengeModeScoringDialog, m_ScoreDescription2) == 0x4E8, "challenge scoring desc 2");
static_assert(offsetof(AUILadderUpgradeScreen, Upgrade0Button) == 0x410, "ladder upgrade button 0");
static_assert(offsetof(AUILadderUpgradeScreen, Upgrade1Button) == 0x418, "ladder upgrade button 1");
static_assert(offsetof(AUILadderUpgradeScreen, SelectedColumn) == 0x420, "ladder upgrade column");
static_assert(offsetof(AUILadderUpgradeScreen, m_strChooseUpgrade) == 0x42C, "ladder upgrade choose");
static_assert(offsetof(AUILadderUpgradeScreen, m_strEquipUpgrade) == 0x43C, "ladder upgrade equip");
static_assert(offsetof(AUILadderSoldierInfo, m_SoldierStartIndex) == 0x410, "ladder squad start");
static_assert(offsetof(AUILadderSoldierInfo, m_NumSoldiers) == 0x414, "ladder squad count");
static_assert(offsetof(AUILadderSoldierInfo, PrevButton) == 0x464, "ladder squad prev");
static_assert(offsetof(AUILadderSoldierInfo, NextButton) == 0x46C, "ladder squad next");
static_assert(offsetof(AUILadderSoldierInfo, LadderSoldierInfoButtons) == 0x474, "ladder squad info buttons");
static_assert(offsetof(AUILadderSoldierInfo, m_NextMission) == 0x49C, "ladder squad next mission");
static_assert(offsetof(AUILadderSoldierInfo, m_MissionLabel) == 0x4AC, "ladder squad mission label");
static_assert(offsetof(AUILadderSoldierInfo, m_MissionComplete) == 0x4BC, "ladder squad complete");
static_assert(offsetof(AUILadderSoldierInfo, m_NewAbilities) == 0x4EC, "ladder squad new abilities");
static_assert(offsetof(AUILadderSoldierInfo, m_NewItems) == 0x50C, "ladder squad new items");
static_assert(offsetof(AUILadderSoldierInfo, m_strHelpPrevPage) == 0x51C, "ladder squad help prev");
static_assert(offsetof(AUILadderSoldierInfo, m_strHelpNextPage) == 0x52C, "ladder squad help next");
static_assert(offsetof(AUILadderSoldierInfo, m_strHelpSoldierInfo) == 0x53C, "ladder squad help info");
static_assert(offsetof(AUITLELadderMedalScreen, m_RewardsLabel) == 0x420, "ladder medal rewards");
static_assert(offsetof(AUITLELadderMedalScreen, m_ScoreLabel) == 0x430, "ladder medal score label");
static_assert(offsetof(AUITLELadderMedalScreen, m_MedalScoreLabel) == 0x440, "ladder medal threshold label");
static_assert(offsetof(AUITLELadderMedalScreen, m_MedalUnlockedString) == 0x450, "ladder medal unlocked");
static_assert(offsetof(AUITLELadderMedalScreen, m_VictoryString) == 0x460, "ladder medal victory");
static_assert(offsetof(AUITLELadderMedalScreen, m_UnlockedNextLadder) == 0x470, "ladder medal next ladder");
static_assert(offsetof(AUITLELadderMedalScreen, m_LadderUnlockDescription) == 0x4A0, "ladder medal unlock desc");
static_assert(offsetof(AUITLELadderEndScreen, m_RunStats) == 0x420, "ladder end stats");
static_assert(offsetof(AUITLELadderEndScreen, m_Description) == 0x430, "ladder end description");
static_assert(offsetof(AUIChallengePostScreen, m_Header) == 0x410, "challenge post header");
static_assert(offsetof(AUIChallengePostScreen, m_HeaderLose) == 0x420, "challenge post header lose");
static_assert(offsetof(AUIChallengePostScreen, m_ScoreLabel) == 0x430, "challenge post score label");
static_assert(offsetof(AUIChallengePostScreen, m_RankLabel) == 0x440, "challenge post rank label");
static_assert(offsetof(AUIChallengePostScreen, m_LeaderboardLabel) == 0x450, "challenge post leaderboard label");
static_assert(offsetof(AUIChallengePostScreen, m_Description) == 0x460, "challenge post description");
static_assert(offsetof(AUIChallengePostScreen, m_DescriptionLose) == 0x470, "challenge post description lose");
// The flags dword at 0x498 (bAllSoldiersDead bit 1, bMissionFailed bit 2) is a bitfield the dump
// cannot offsetof; the pin on m_ImagePath brackets it.
static_assert(offsetof(AUIChallengePostScreen, m_ImagePath) == 0x49C, "challenge post flags dword precedes m_ImagePath");
static_assert(offsetof(AUIChallengePostScreen, m_ContinueButton) == 0x4AC, "challenge post continue");
static_assert(offsetof(AUIChallengePostScreen, m_LeaderboardButton) == 0x4B4, "challenge post leaderboard");
static_assert(offsetof(AUIChallengeModeEventNotify, m_TitleHeader) == 0x418, "challenge event header");
static_assert(offsetof(AUIChallengeModeEventNotify, m_CloseButton) == 0x420, "challenge event close");
static_assert(offsetof(AUIChallengeModeEventNotify, m_NextButton) == 0x428, "challenge event next");
static_assert(offsetof(AUIChallengeModeEventNotify, m_PreviousButton) == 0x430, "challenge event previous");
static_assert(offsetof(AUIChallengeModeEventNotify, m_kDetailText) == 0x438, "challenge event detail");
static_assert(offsetof(AUIX2PanelHeader, Title) == 0x3D8, "panel header title");
static_assert(offsetof(AUITextContainer, Text) == 0x3E0, "text container text");
static_assert(offsetof(AUIText, Text) == 0x3D8, "UIText text");
static_assert(offsetof(AUIText, HtmlText) == 0x3E8, "UIText html");
static_assert(offsetof(AUILadderUpgradeScreen_execSelectColumn_Params, iCol) == 0x0,
    "ladder upgrade SelectColumn params");
static_assert(offsetof(AUILadderSoldierInfo_execDisplaySoldierInfo_Params, Index) == 0x0,
    "ladder squad DisplaySoldierInfo params");

// ---- the soldier loadout popup (UITLEUnitStats) ----
static_assert(offsetof(AUITLEUnitStats, m_UnitArray) == 0x438, "unit stats units");
static_assert(offsetof(AUITLEUnitStats, m_CurrentIndex) == 0x450, "unit stats index");
static_assert(offsetof(AUITLEUnitStats, m_ScreenTitle) == 0x454, "unit stats title");

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

// ---- the one static native we call WITH an argument (game_ui_tactical.cpp's Chosen gate) ----
// An optional parameter still occupies its slot ahead of the return, so a block holding only
// the pointer handed the engine HistoryIndex 0 and took its copy-back four bytes past the struct.
static_assert(offsetof(UXComGameState_Unit_execGetActivatedChosen_Params, HistoryIndex) == 0x00,
    "GetActivatedChosen history index precedes the return");
static_assert(offsetof(UXComGameState_Unit_execGetActivatedChosen_Params, ReturnValue) == 0x04,
    "GetActivatedChosen return follows a packed int32");

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
static_assert(offsetof(AUIPanel, ParentPanel) == 0x2E0, "UIPanel::ParentPanel");

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

// ---- the objectives list (UIObjectiveList / UIObjectiveListItem) ----
// The owning field on each layer's HUD, then the list, the row, and the struct the row retains.
static_assert(offsetof(AUITacticalHUD, m_kObjectivesControl) == 0x444,
    "UITacticalHUD::m_kObjectivesControl");
static_assert(offsetof(AUIAvengerHUD, Objectives) == 0x44C, "UIAvengerHUD::Objectives");
static_assert(offsetof(AUIObjectiveList, Items) == 0x3D8, "UIObjectiveList::Items");
static_assert(offsetof(AUIObjectiveListItem, Data) == 0x3DC, "UIObjectiveListItem::Data");
static_assert(offsetof(AUIObjectiveListItem, m_strObjectivesListTitle) == 0x47C,
    "UIObjectiveListItem::m_strObjectivesListTitle");
static_assert(offsetof(AUIObjectiveListItem, m_strInProgress) == 0x4AC,
    "UIObjectiveListItem::m_strInProgress");
// FObjectiveDisplayInfo, read at offsets RELATIVE to Data — so these pin the whole row payload.
static_assert(offsetof(FObjectiveDisplayInfo, MissionType) == 0x0C,
    "ObjectiveDisplayInfo::MissionType");
static_assert(offsetof(FObjectiveDisplayInfo, DisplayLabel) == 0x1C,
    "ObjectiveDisplayInfo::DisplayLabel");
// ShowCompleted/Failed/CheckBox/Warning/Header/bIsDarkEvent are bitfields in the dword at 0x2C;
// the plain member after them pins it.
static_assert(offsetof(FObjectiveDisplayInfo, Timer) == 0x30,
    "ObjectiveDisplayInfo flag dword precedes Timer at 0x2C");
static_assert(offsetof(FObjectiveDisplayInfo, CounterHaveImage) == 0x34,
    "ObjectiveDisplayInfo::CounterHaveImage");
static_assert(offsetof(FObjectiveDisplayInfo, CounterAvailableImage) == 0x44,
    "ObjectiveDisplayInfo::CounterAvailableImage");
static_assert(offsetof(FObjectiveDisplayInfo, CounterLostImage) == 0x54,
    "ObjectiveDisplayInfo::CounterLostImage");
static_assert(offsetof(FObjectiveDisplayInfo, CounterHaveAmount) == 0x64,
    "ObjectiveDisplayInfo::CounterHaveAmount");
static_assert(offsetof(FObjectiveDisplayInfo, CounterHaveMin) == 0x68,
    "ObjectiveDisplayInfo::CounterHaveMin");
static_assert(offsetof(FObjectiveDisplayInfo, CounterAvailableAmount) == 0x6C,
    "ObjectiveDisplayInfo::CounterAvailableAmount");
static_assert(offsetof(FObjectiveDisplayInfo, CounterLostAmount) == 0x70,
    "ObjectiveDisplayInfo::CounterLostAmount");

// ---- the mission stage counters (UISpecialMissionHUD / _TurnCounter) ----
static_assert(offsetof(AUISpecialMissionHUD, m_kGenericTurnCounter) == 0x418,
    "UISpecialMissionHUD::m_kGenericTurnCounter");
static_assert(offsetof(AUISpecialMissionHUD, m_kTurnCounter2) == 0x420,
    "UISpecialMissionHUD::m_kTurnCounter2");
static_assert(offsetof(AUISpecialMissionHUD_TurnCounter, m_sLabel) == 0x3FC,
    "UISpecialMissionHUD_TurnCounter::m_sLabel");
static_assert(offsetof(AUISpecialMissionHUD_TurnCounter, m_sSubLabel) == 0x40C,
    "UISpecialMissionHUD_TurnCounter::m_sSubLabel");
static_assert(offsetof(AUISpecialMissionHUD_TurnCounter, m_sCounter) == 0x41C,
    "UISpecialMissionHUD_TurnCounter::m_sCounter");
// m_bLocked/m_bExpired/m_bInfinity are bitfields in the dword at 0x42C; the plain member after
// them pins it.
static_assert(offsetof(AUISpecialMissionHUD_TurnCounter, m_fLastUpdateTime) == 0x430,
    "TurnCounter flag dword precedes m_fLastUpdateTime at 0x42C");

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

// ---- the ability bar (UITacticalHUD_AbilityContainer / UITacticalHUD_Ability) ----
// The icon's IsAvailable/bIsShiney dword at 0x438 is bitfields and cannot be offsetof'd; the
// hotkey string before it and Index after it bracket it.
static_assert(offsetof(AUITacticalHUD, m_kAbilityHUD) == 0x42C, "UITacticalHUD::m_kAbilityHUD");
static_assert(offsetof(AUITacticalHUD_AbilityContainer, m_iCurrentIndex) == 0x3DC,
    "UITacticalHUD_AbilityContainer::m_iCurrentIndex");
static_assert(offsetof(AUITacticalHUD_AbilityContainer, ActiveAbilities) == 0x3EC,
    "UITacticalHUD_AbilityContainer::ActiveAbilities");
static_assert(offsetof(AUITacticalHUD_AbilityContainer, m_arrAbilities) == 0x3F8,
    "UITacticalHUD_AbilityContainer::m_arrAbilities (the signature walk and the companion's names)");
static_assert(offsetof(FAvailableAction, AbilityObjectRef) == 0x0,
    "AvailableAction::AbilityObjectRef (ObjectID is its first member; the stride is read live)");
static_assert(offsetof(AUITacticalHUD_AbilityContainer, m_arrUIAbilities) == 0x408,
    "UITacticalHUD_AbilityContainer::m_arrUIAbilities");
static_assert(offsetof(AUITacticalHUD_Ability, m_strCooldown) == 0x3F8,
    "UITacticalHUD_Ability::m_strCooldown");
static_assert(offsetof(AUITacticalHUD_Ability, m_strCharge) == 0x408,
    "UITacticalHUD_Ability::m_strCharge");
static_assert(offsetof(AUITacticalHUD_Ability, m_strAntennaText) == 0x418,
    "UITacticalHUD_Ability::m_strAntennaText");
static_assert(offsetof(AUITacticalHUD_Ability, m_strHotkeyLabel) == 0x428,
    "UITacticalHUD_Ability::m_strHotkeyLabel");
static_assert(offsetof(AUITacticalHUD_Ability, Index) == 0x43C,
    "UITacticalHUD_Ability::Index (the IsAvailable dword precedes it at 0x438)");

// ---- the controller-mode command buttons (game_ui_tactical.cpp: ReadPadCommands) ----
// Dump-derived, NOT measured live. EndTurnButton is a UIButton* spawned in the controller branch
// of UITacticalHUD.OnInit; PC is UIPanel's controller (an XComTacticalController in a mission).
static_assert(offsetof(AUITacticalHUD, CharInfoButton) == 0x4E8, "UITacticalHUD::CharInfoButton");
static_assert(offsetof(AUITacticalHUD, EndTurnButton) == 0x4F0, "UITacticalHUD::EndTurnButton");
static_assert(offsetof(AUITacticalHUD, SkyrangerButton) == 0x4F8,
    "UITacticalHUD::SkyrangerButton (live: visible, text retained)");
static_assert(offsetof(AUITacticalHUD, m_strNavHelpEndTurn) == 0x540,
    "UITacticalHUD::m_strNavHelpEndTurn");
static_assert(offsetof(AUIPanel, PC) == 0x2C8, "UIPanel::PC");
// XComTacticalController.PerformEndTurn's block: the enum byte at 0 and the bool return's DWORD at
// 4 (the dump's own comment: `ReturnValue : 1` at 0x0004, 0x0004 wide; CPF_ReturnParm). The
// generated struct declares the bool as a bitfield of `bool`, so its compiled sizeof is not the
// engine's and the return slot cannot be offsetof'd or sized here — only the first member can.
// game_ui_tactical.cpp hand-rolls { uint8_t; uint32_t } under pack(4), which is that layout.
static_assert(offsetof(AXComTacticalController_execPerformEndTurn_Params, eEndTurnType) == 0x0,
    "XComTacticalController::PerformEndTurn params: eEndTurnType");
static_assert(static_cast<int>(EPlayerEndTurnType::ePlayerEndTurnType_PlayerInput) == 1,
    "EPlayerEndTurnType::ePlayerEndTurnType_PlayerInput (kEndTurnPlayerInput)");

// ---- the command buttons (UITacticalHUD_MouseControls) ----
// The one tactical panel besides objectives that RETAINS its words: SetButtonItem assigns
// Label/key/UIState into ButtonItems before the Flash push. All four offsets confirmed live
// 2026-08-31 via /props against the running class.
static_assert(offsetof(AUITacticalHUD, m_kMouseControls) == 0x44C,
    "UITacticalHUD::m_kMouseControls");
static_assert(offsetof(AUITacticalHUD_MouseControls, m_iCurrentSelection) == 0x46C,
    "UITacticalHUD_MouseControls::m_iCurrentSelection (OnAccept reads THIS, not a parameter)");
static_assert(offsetof(AUITacticalHUD_MouseControls, ButtonItems) == 0x474,
    "UITacticalHUD_MouseControls::ButtonItems");
static_assert(offsetof(AUITacticalHUD_MouseControls, CommandAbilities) == 0x484,
    "UITacticalHUD_MouseControls::CommandAbilities");
// The element layout is solid; the element STRIDE is not derivable from this compiled struct
// alone — the ScriptStruct's runtime PropertiesSize is 0x21 and whether the engine's
// UStructProperty aligns that to 4 is its business, so ReadMouseControls takes the stride from
// the live UArrayProperty's own ElementSize (UE3Reflect::ArrayElementSize) and only sanity-bounds
// it. sizeof here documents what the pack(4) compile believes; it is deliberately NOT used as
// the stride.
static_assert(offsetof(FTButtonItems, Label) == 0x00, "TButtonItems::Label");
static_assert(offsetof(FTButtonItems, Key) == 0x10, "TButtonItems::Key");
static_assert(offsetof(FTButtonItems, uistate) == 0x20, "TButtonItems::uistate");
static_assert(sizeof(FTButtonItems) == 0x24, "TButtonItems compiled size (see stride note)");

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

// ---- after action: UIAfterAction + its soldier panels ----
//
// The screen's flag dword (isBondIconFocused / bWalkupStarted / bRecievedShowHUDRemoteEvent /
// bForceHelpActivate) is bitfields at 0x410; FlyoverPriorityLevel right after it pins it.
static_assert(offsetof(AUIAfterAction, FlyoverPriorityLevel) == 0x414,
    "UIAfterAction flag dword precedes FlyoverPriorityLevel at 0x410");
static_assert(offsetof(AUIAfterAction, m_kSlotList) == 0x4E4, "after action card list");

// The card's flag dword (bShowPortrait / m_bCanPromote / m_bKIA) is at 0x464, pinned by
// MemorialButton before it and BondIcon after. MemorialButton itself is a dead field — never
// assigned in either the vanilla or Highlander source; the KIA card reuses PromoteButton.
static_assert(offsetof(AUIAfterAction_ListItem, UnitReference) == 0x3D8,
    "after action card unit ref");
static_assert(offsetof(AUIAfterAction_ListItem, PromoteButton) == 0x454,
    "after action card promote/memorial button");
static_assert(offsetof(AUIAfterAction_ListItem, MemorialButton) == 0x45C,
    "UIAfterAction_ListItem flag dword sits between MemorialButton and BondIcon at 0x464");
static_assert(offsetof(AUIAfterAction_ListItem, BondIcon) == 0x468, "after action card bond icon");
static_assert(offsetof(AUIAfterAction_ListItem, Index) == 0x470, "after action card index");

// ---- inside a room: UIFacility, the Avenger HUD's header and its shortcut submenu ----
//
// This layer reads ACROSS two screens: the room screen on top and UIAvengerHUD below it, which
// is where the game keeps the room's name and the room's menu. Both halves are pinned here.
static_assert(offsetof(AUIFacility, m_kStaffSlotContainer) == 0x608, "facility staff container");
static_assert(offsetof(AUIFacility, m_kRoomContainer) == 0x610, "facility room container");
static_assert(offsetof(AUIFacility, UpgradeButton) == 0x648, "facility upgrade button");
static_assert(offsetof(AUIRoomContainer, RoomFuncs) == 0x3DC, "room bottom-bar buttons");

// ---- the Skyranger load screen (UIDropShipBriefing_MissionStart / _MissionEnd) ----
//
// Two sibling classes over one base, each declaring its own strings from 0x420 in its own order,
// so nothing here is shared but AUIDropShipBriefingBase's array. Pinning matters more than usual
// on this pair: m_strConsoleLaunch carries a `<workshop> UI_CONSOLE_LOAD_SCREEN_FIXES` marker in
// SrcOrig, i.e. it was added by a patch — exactly the kind of field that can differ between the
// stock package and the Highlander's recompile (UIShellSoundtrackPicker all over again). It sits
// immediately BEFORE TipCycle, so a divergence there would silently move the two fields the
// reader depends on: LaunchButton (readiness) and m_strCurrentTip.
static_assert(offsetof(AUIDropShipBriefingBase, TipCounters) == 0x410, "dropship tip counters");

static_assert(offsetof(AUIDropShipBriefing_MissionStart, m_strLaunch) == 0x460,
    "dropship launch prompt");
static_assert(offsetof(AUIDropShipBriefing_MissionStart, m_strLoadingText) == 0x470,
    "dropship loading text");
static_assert(offsetof(AUIDropShipBriefing_MissionStart, m_strConsoleLaunch) == 0x4A0,
    "dropship console prompt (never read — pins the patched field ahead of LaunchButton)");
static_assert(offsetof(AUIDropShipBriefing_MissionStart, LaunchButton) == 0x4B4,
    "dropship launch button (its bIsVisible is the map-loaded signal)");
static_assert(offsetof(AUIDropShipBriefing_MissionStart, m_strCurrentTip) == 0x4C4,
    "dropship current tip");

static_assert(offsetof(AUIDropShipBriefing_MissionEnd, LaunchButton) == 0x424,
    "post-mission dropship continue button");
static_assert(offsetof(AUIDropShipBriefing_MissionEnd, m_strPressKeyToContinue) == 0x43C,
    "post-mission dropship prompt");
static_assert(offsetof(AUIDropShipBriefing_MissionEnd, m_strLoadingText) == 0x4DC,
    "post-mission dropship loading text");

// ---- the personnel roster (UIPersonnel and its ten subclasses) ----
//
// One offset for the whole family. Everything else is either a companion read (the rows retain
// only a UnitRef) or a function call — the screen's own SwitchTab, and the sort header's own
// mouse handler.
static_assert(offsetof(AUIPersonnel, m_kList) == 0x45C, "personnel roster list");

// ---- choosing what to build in a room (UIChooseFacility) ----
//
// One offset again. The rows are ordinary UIListItemStrings (pinned above), and every word of the
// detail panel comes from the companion, because UpdateSelection retains none of it.
static_assert(offsetof(AUIChooseFacility, m_List) == 0x418, "choose-facility list");
static_assert(offsetof(AUIListItemString, AttentionIcon) == 0x440, "list item attention icon");
// UIChooseUpgrade — the screen every facility-upgrade path opens (UIFacilityUpgrade is dead
// code) — is read by the same function. Pinned on its OWN class: the two lists sharing 0x418 is
// a coincidence of declaration order, and a Highlander bump could break one without the other.
static_assert(offsetof(AUIChooseUpgrade, m_List) == 0x418, "choose-upgrade list");

// ---- covert actions (UICovertActions) ----
//
// The rows are ordinary UIListItemStrings (Text and the flag dword pinned above); what is new is
// metadataInt, which carries the action's ObjectID and is the ONLY thing distinguishing one row's
// action from another's — the companion joins on it.
//
// ⚠ The state object behind them is a documented counter-example to "the Highlander APPENDS":
// XComGameState_CovertAction gains AmbushMissionSource (#485) and RewardsNotGivenOnCompletion
// (#810) BEFORE its struct declarations, so StaffSlots/CostSlots/Risks/NegatedRisks all move
// against a vanilla dump. Nothing here reads them — every covert-action state field is the
// companion's — but that is why, and it is the case `just sdk-adopt`'s vanilla refusal exists for.
static_assert(offsetof(AUIListItemString, metadataInt) == 0x3D8, "list item metadataInt");
static_assert(offsetof(AUICovertActions, ActionRef) == 0x580, "covert actions selected action");
static_assert(offsetof(AUICovertActions, arrActions) == 0x584, "covert actions state array");
// The flag dword at 0x5A4 is bitfields (bActionInProgress 0x01 .. bIsSelectingSlots 0x20); List is
// its nearest plain neighbour and pins the same layout.
static_assert(offsetof(AUICovertActions, List) == 0x5A8, "covert actions flags precede List");
static_assert(offsetof(AUICovertActions, LaunchButton) == 0x5B0, "covert actions launch button");

// ---- resistance orders (UIStrategyPolicy) ----
//
// Both flag dwords are bitfields and are pinned by BRACKETING — the field immediately before and
// the one immediately after — so a dword that moves fails one of the four.
static_assert(offsetof(AUIStrategyPolicy, Columns) == 0x4D0, "policy columns");
static_assert(offsetof(AUIStrategyPolicy, Hand) == 0x4E0, "policy hand list");
static_assert(offsetof(AUIStrategyPolicy, DeckCatcher) == 0x4F0, "policy flags precede 0x4F8");
static_assert(offsetof(AUIStrategyPolicy, FilterFaction) == 0x4FC, "policy flags end at 0x4FC");
static_assert(offsetof(AUIStrategyPolicy, InspectingCard) == 0x538, "policy picked-up card");
static_assert(offsetof(AUIStrategyPolicy_Card, CardRef) == 0x3E8, "policy card ref");
static_assert(offsetof(AUIStrategyPolicy_Card, eFaction) == 0x3F0, "policy card flags end");

static_assert(offsetof(AUIAvengerHUD, ResourceContainer) == 0x420, "avenger HUD resource strip");
static_assert(offsetof(AUIAvengerHUD, Shortcuts) == 0x43C, "avenger HUD shortcut bar");
static_assert(offsetof(AUIAvengerHUD, FacilityHeader) == 0x454, "avenger HUD facility header");

// ---- the ant farm's Tab layer: the HUD's other panels (game_ui_avenger.cpp ReadShortcutBar /
// ReadToDoWidget / ReadEventQueue) ----
static_assert(offsetof(AUIAvengerHUD, NavHelp) == 0x418, "avenger HUD nav help");
static_assert(offsetof(AUIAvengerHUD, EventQueue) == 0x42C, "avenger HUD event queue");
static_assert(offsetof(AUIAvengerHUD, ToDoWidget) == 0x434, "avenger HUD to-do widget");
// The shortcut bar's categories: a TArray of structs walked by stride. bAlert is the bitfield
// at 0x3C, bracketed by OnItemClicked before it and the struct size after.
static_assert(offsetof(AUIAvengerShortcuts, Categories) == 0x82C, "shortcut categories");
static_assert(sizeof(FUIAvengerShortcutMessageCategory) == 0x40, "shortcut category stride");
static_assert(offsetof(FUIAvengerShortcutMessageCategory, Button) == 0x00, "shortcut category button");
static_assert(offsetof(FUIAvengerShortcutMessageCategory, OnItemClicked) == 0x1C,
    "shortcut category flag dword (bAlert) follows OnItemClicked at 0x3C");
// The nav help's hologlobe button and its label. bGeoscapeButton is bit 0x4 of the dword at
// 0x434, bracketed by RightContainer before it and VerticalHelpCount after.
static_assert(offsetof(AUINavigationHelp, m_strGeoscapeLabel) == 0x3F4, "nav help geoscape label");
static_assert(offsetof(AUINavigationHelp, RightContainer) == 0x42C,
    "nav help flag dword (bGeoscapeButton) follows RightContainer at 0x434");
static_assert(offsetof(AUINavigationHelp, VerticalHelpCount) == 0x438,
    "nav help flag dword ends before VerticalHelpCount");
// The click delegates, one per help button in creation order — the index is the button's Flash
// name (`buttonNavHelp_N`) and therefore its tooltip path, which is how the hologlobe's is found.
static_assert(offsetof(AUINavigationHelp, m_arrButtonClickDelegates) == 0x43C,
    "nav help button delegates");
// The to-do widget: category labels (a TArray<FString>), the message categories and the
// message struct, plus the list and current-category index the activation path drives.
static_assert(offsetof(AUIToDoWidget, m_arrCategory_Labels) == 0x3D8, "to-do category labels");
static_assert(offsetof(AUIToDoWidget, List) == 0x780, "to-do list");
static_assert(offsetof(AUIToDoWidget, Categories) == 0x7AC, "to-do categories");
static_assert(offsetof(AUIToDoWidget, CurrentCategory) == 0x7BC, "to-do current category");
static_assert(sizeof(FUIToDoMessageCategory) == 0x18, "to-do category stride");
static_assert(offsetof(FUIToDoMessageCategory, Icon) == 0x00, "to-do category icon");
static_assert(offsetof(FUIToDoMessageCategory, Messages) == 0x08, "to-do category messages");
static_assert(sizeof(FUIToDoMessage) == 0x5C, "to-do message stride");
static_assert(offsetof(FUIToDoMessage, Label) == 0x00, "to-do message label");
static_assert(offsetof(FUIToDoMessage, Description) == 0x10, "to-do message description");
static_assert(offsetof(FUIToDoMessage, Urgency) == 0x28, "to-do message urgency");
// The event queue and its rows. bIsExpanded is the bitfield at 0x418, bracketed by
// ShrinkButtonLabel before it and the first delegate after.
static_assert(offsetof(AUIEventQueue, List) == 0x3D8, "event queue list");
static_assert(offsetof(AUIEventQueue, ExpandButton) == 0x3F0, "event queue expand button");
static_assert(offsetof(AUIEventQueue, ExpandButtonLabel) == 0x3F8, "event queue expand label");
static_assert(offsetof(AUIEventQueue, ShrinkButtonLabel) == 0x408,
    "event queue flag dword (bIsExpanded) follows ShrinkButtonLabel at 0x418");
static_assert(offsetof(AUIEventQueue, __OnUpButtonClicked__Delegate) == 0x41C,
    "event queue flag dword ends before the first delegate");
static_assert(offsetof(AUIEventQueue_ListItem, Title) == 0x3D8, "event row title");
static_assert(offsetof(AUIEventQueue_ListItem, daysLabel) == 0x3E8, "event row days label");
static_assert(offsetof(AUIEventQueue_ListItem, DaysValue) == 0x3F8, "event row days value");

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

// ---- new research available (UIResearchUnlocked) ----
// The deck's words come from the Flash capture; these two ints are the whole property read —
// which card is showing and how many there are (game_ui.cpp's ReadResearchUnlocked).
static_assert(offsetof(AUIResearchUnlocked, CurrentTechIndex) == 0x460,
    "UIResearchUnlocked::CurrentTechIndex");
static_assert(offsetof(AUIResearchUnlocked, NumUnlockedTechs) == 0x464,
    "UIResearchUnlocked::NumUnlockedTechs");

// ---- the mission bookends (UIMissionSummary) ----
//
// Two offsets for both screens put together: everything else either screen shows went into
// Flash and comes back from the capture (docs/mission-summary-recon.md §3), and
// UIChosenMissionSummary needs no property read at all. m_PosterButton is read for its
// VISIBILITY as much as its label — Hide() is how the game suppresses the photobooth hotlink in
// multiplayer and after a total squad wipe — and m_strContinue is the one localized string on
// either screen that XComGame.int really does populate on the class that declares it.
static_assert(offsetof(AUIMissionSummary, m_PosterButton) == 0x464,
    "UIMissionSummary::m_PosterButton");
static_assert(offsetof(AUIMissionSummary, m_strContinue) == 0x4C4,
    "UIMissionSummary::m_strContinue");

// ---- the geoscape: UIStrategyMap, its mission tray and the navigator behind it ----
//
// UINavigator is pinned here for the first time. Every other recipe reaches a list through
// UIList, whose own selection accessor does the work; the geoscape has no list at all — the
// tray is a bare navigator over twenty spawned icons — so this is the one screen where the
// navigator's own fields have to be read directly.
static_assert(offsetof(AUIPanel, Navigator) == 0x2D8, "panel navigator");
static_assert(offsetof(UUINavigator, SelectedIndex) == 0x6C, "navigator selection");
static_assert(offsetof(UUINavigator, NavigableControls) == 0x74,
    "navigator controls — NOT NavigationTargets, which is empty on this screen");

static_assert(offsetof(AUIStrategyMap, m_eUIState) == 0x620, "strategy map state");
static_assert(offsetof(AUIStrategyMap, MissionItemUI) == 0x624, "strategy map tray struct");
static_assert(offsetof(FTStrategyMapMissionItemUI, MissionIcons) == 0x0,
    "tray icons lead the struct, so the array offset is the struct's own");
static_assert(offsetof(AUIStrategyMap, SelectedMapItem) == 0x78C, "strategy map selection");

static_assert(offsetof(AUIStrategyMap_MissionIcon, ScanSite) == 0x49C, "tray icon scan site");
static_assert(offsetof(AUIStrategyMap_MissionIcon, MissionSite) == 0x4A4, "tray icon mission");
static_assert(offsetof(AUIStrategyMap_MissionIcon, MapItem) == 0x4AC, "tray icon globe pin");
static_assert(offsetof(AUIStrategyMap_MissionIcon, ToolTip) == 0x4B4, "tray icon tooltip");
static_assert(offsetof(AUIStrategyMap_MissionIcon, idNum) == 0x4C0, "tray icon id");

static_assert(offsetof(AUIStrategyMap_MissionIconTooltip, Title) == 0x484, "tray tooltip title");
static_assert(offsetof(AUIStrategyMap_MissionIconTooltip, Body) == 0x494, "tray tooltip body");

// The pin's entity reference is what keys a tray cursor across a rebuild — an ObjectID is
// stable over the campaign history where a slot index is not.
static_assert(offsetof(AUIStrategyMapItem, GeoscapeEntityRef) == 0x494, "globe pin entity ref");
// MapPin_Header is a CPF_Localized class default — a generic noun for the kind of place, and
// the only word available for a tray slot the game gave no tooltip.
static_assert(offsetof(AUIStrategyMapItem, MapPin_Header) == 0x4A0, "globe pin generic label");
// The tray icon's ART, which is how an unmet faction's placeholder slot is told apart: its
// ScanSite is set and it IS navigable, so only the Faction_unknown image distinguishes it.
static_assert(offsetof(AUIIcon, ImagePath) == 0x3DC, "icon art path");

// The HUD's four keyboard-unreachable verbs. The flag dword at 0x7C8 is bitfields
// (m_bResNetForcedOn among them); SelectedMapItem before it and LeftBumperIcon after it pin the
// layout around it, the same way every other bitfield in this file is pinned.
static_assert(offsetof(AUIStrategyMap, StrategyMapHUD) == 0x610, "strategy map HUD panel");
static_assert(offsetof(AUIStrategyMap, DarkEventsButton) == 0x654, "dark events button");
static_assert(offsetof(AUIStrategyMap, DarkEventsContainer) == 0x65C, "dark events container");
static_assert(offsetof(AUIStrategyMap, m_strDarkEventsLabel) == 0x6AC, "dark events label");
static_assert(offsetof(AUIStrategyMap, m_strToggleResNet) == 0x6BC, "resnet toggle label");
static_assert(offsetof(AUIStrategyMap, m_srResistanceOrders) == 0x6CC, "resistance orders label");
static_assert(offsetof(AUIStrategyMap, m_srChosenInfo) == 0x6DC, "chosen info label");
static_assert(offsetof(AUIStrategyMap, LeftBumperIcon) == 0x79C,
    "UIStrategyMap flag dword (m_bResNetForcedOn) precedes LeftBumperIcon at 0x7C8");
static_assert(offsetof(AUIStrategyMap_HUD, m_kChosenInfoButton) == 0x474, "chosen info button");
static_assert(offsetof(AUIStrategyMap_HUD, m_kResistanceInfoButton) == 0x47C,
    "resistance orders button");
// The Avatar meter (game_ui_strategy.cpp ReadDoomMeter). The flag dword at 0x470 carries
// bDoomCounterVisible (0x4) and is bitfields; m_kDoomOverlay before and m_kChosenInfoButton
// after pin it.
static_assert(offsetof(AUIStrategyMap_HUD, m_strDoomCounterLabel) == 0x3D8, "doom counter label");
static_assert(offsetof(AUIStrategyMap_HUD, m_strDoomDays) == 0x3E8, "doom days word");
static_assert(offsetof(AUIStrategyMap_HUD, m_strDoomHours) == 0x3F8, "doom hours word");
static_assert(offsetof(AUIStrategyMap_HUD, m_strDoomMinutes) == 0x408, "doom minutes word");
static_assert(offsetof(AUIStrategyMap_HUD, m_strDoomSeconds) == 0x418, "doom seconds word");
static_assert(offsetof(AUIStrategyMap_HUD, m_strDoomTitle) == 0x428, "doom meter title");
static_assert(offsetof(AUIStrategyMap_HUD, m_kDoomOverlay) == 0x468,
    "UIStrategyMap_HUD flag dword (bDoomCounterVisible) follows m_kDoomOverlay at 0x470");
static_assert(offsetof(AUIStrategyMap_HUD, CachedDoom) == 0x4C4, "doom meter cached pip count");

// ---- the mission blade family (UIMission — docs/mission-blade-recon.md) ----
//
// The family's ONE hand-transcribed offset: MissionRef, the wire-cache discriminator that
// catches GOps rewriting the displayed mission in place. Everything else on these screens is
// read companion-side through the screen's own getters, so no button or panel pointer is ever
// dereferenced natively.
static_assert(offsetof(AUIMission, MissionRef) == 0x600, "UIMission::MissionRef");

// The dark events board's one outbound call: OnRevealClicked(int Idx), the handler the mouse
// click and the controller's X both end in. Idx is the only parameter (the rest of the generated
// struct is locals); the hand-rolled block in game_ui_popup.cpp is padded past the struct's
// first locals and writes Idx at 0.
static_assert(offsetof(AUIAdventOperations_execOnRevealClicked_Params, Idx) == 0x0,
    "dark events OnRevealClicked params");

// ---- the Chosen dossier and the bond screens (docs/chosen-bond-recon.md §2) ----
//
// UIChosenInfo keeps only the tab index and the array it populated Flash from — the wire-cache
// discriminator and the tab count. UISoldierBondScreen's flag dword at 0x410 is bitfields
// (m_bFlipSort, m_bDirtySortHeaders, bSquadOnly), so m_eSortType right after it pins the layout
// from the far side. UISoldierBondConfirmScreen, UIChosenEncountered and UISoldierCaptured are
// read entirely companion-side and need no pin.
static_assert(offsetof(AUIChosenInfo, CurrentChosenIndex) == 0x520,
    "UIChosenInfo::CurrentChosenIndex");
static_assert(offsetof(AUIChosenInfo, AllActiveChosen) == 0x524, "UIChosenInfo::AllActiveChosen");
static_assert(offsetof(AUISoldierBondScreen, m_eSortType) == 0x414,
    "UISoldierBondScreen::m_eSortType (the flag dword precedes it at 0x410)");
static_assert(offsetof(AUISoldierBondScreen, UnitRef) == 0x4E4, "UISoldierBondScreen::UnitRef");
static_assert(offsetof(AUISoldierBondScreen, List) == 0x4EC, "UISoldierBondScreen::List");
static_assert(offsetof(AUISoldierBondListItem, UnitRef) == 0x4C4, "UISoldierBondListItem::UnitRef");
static_assert(offsetof(AUISoldierBondListItem, ActivateBondButton) == 0x4CC,
    "UISoldierBondListItem::ActivateBondButton");

// ---- the monthly report's Chosen page (docs/resistance-report-recon.md) ----
//
// Two reads: the ChosenRefs count and the flag dword at 0x650 (bSimpleView, the one-time knowledge
// warning). The dword is bitfields, so its two plain neighbours pin it. UIResistanceReport itself
// is read entirely companion-side and needs no pin.
static_assert(offsetof(AUIResistanceReport_ChosenEvents, ChosenRefs) == 0x630,
    "UIResistanceReport_ChosenEvents::ChosenRefs");
static_assert(offsetof(AUIResistanceReport_ChosenEvents, ChosenLevelUpRefs) == 0x640,
    "UIResistanceReport_ChosenEvents::ChosenLevelUpRefs (the flag dword follows at 0x650)");
static_assert(offsetof(AUIResistanceReport_ChosenEvents, UIDisplayMIC) == 0x654,
    "UIResistanceReport_ChosenEvents::UIDisplayMIC (the flag dword precedes at 0x650)");

// ---- the black market (game_ui_avenger.cpp: UIBlackMarket, UIBlackMarket_Sell, _SellItem) ----
//
// The entry menu: its title and the interest caption are read as FStrings, its two buttons
// through UIButton's generic Text / flag dword (pinned above), and LibraryPanel is the Flash
// panel whose cache index keys the welcome line's capture.
static_assert(offsetof(AUIBlackMarket, m_strTitle) == 0x410, "UIBlackMarket::m_strTitle");
static_assert(offsetof(AUIBlackMarket, m_strInterestTitle) == 0x520,
    "UIBlackMarket::m_strInterestTitle");
static_assert(offsetof(AUIBlackMarket, LibraryPanel) == 0x550, "UIBlackMarket::LibraryPanel");
static_assert(offsetof(AUIBlackMarket, Button1) == 0x558, "UIBlackMarket::Button1 (buy)");
static_assert(offsetof(AUIBlackMarket, Button2) == 0x560, "UIBlackMarket::Button2 (sell)");
// The trading post: the running total, the confirm button, six localized captions, the list.
static_assert(offsetof(AUIBlackMarket_Sell, SaleAmount) == 0x410, "UIBlackMarket_Sell::SaleAmount");
static_assert(offsetof(AUIBlackMarket_Sell, ConfirmButton) == 0x418,
    "UIBlackMarket_Sell::ConfirmButton");
static_assert(offsetof(AUIBlackMarket_Sell, m_strInventoryLabel) == 0x430,
    "UIBlackMarket_Sell::m_strInventoryLabel");
static_assert(offsetof(AUIBlackMarket_Sell, m_strSellLabel) == 0x440,
    "UIBlackMarket_Sell::m_strSellLabel");
static_assert(offsetof(AUIBlackMarket_Sell, m_strTotalLabel) == 0x450,
    "UIBlackMarket_Sell::m_strTotalLabel");
static_assert(offsetof(AUIBlackMarket_Sell, m_strCostLabel) == 0x460,
    "UIBlackMarket_Sell::m_strCostLabel");
static_assert(offsetof(AUIBlackMarket_Sell, m_strInterestedLabel) == 0x470,
    "UIBlackMarket_Sell::m_strInterestedLabel");
static_assert(offsetof(AUIBlackMarket_Sell, m_strQuantityLabel) == 0x480,
    "UIBlackMarket_Sell::m_strQuantityLabel");
static_assert(offsetof(AUIBlackMarket_Sell, List) == 0x4C0, "UIBlackMarket_Sell::List");
// A sell row: the three numbers it retains (its words all went into Flash).
static_assert(offsetof(AUIBlackMarket_SellItem, NumSelling) == 0x400,
    "UIBlackMarket_SellItem::NumSelling");
static_assert(offsetof(AUIBlackMarket_SellItem, ItemRef) == 0x404,
    "UIBlackMarket_SellItem::ItemRef");
static_assert(offsetof(AUIBlackMarket_SellItem, Price) == 0x410, "UIBlackMarket_SellItem::Price");

// ---- recruiting (game_ui_avenger.cpp: UIRecruitSoldiers) ----
//
// The list, the recruit array (its COUNT only — the elements are the companion's) and the two
// captions read as FStrings. A row is a UIRecruitmentListItem, read through UIListItemString's
// generic flag dword; it retains no words and no metadata of its own.
static_assert(offsetof(AUIRecruitSoldiers, List) == 0x410, "UIRecruitSoldiers::List");
static_assert(offsetof(AUIRecruitSoldiers, m_arrRecruits) == 0x418,
    "UIRecruitSoldiers::m_arrRecruits");
static_assert(offsetof(AUIRecruitSoldiers, m_strListTitle) == 0x428,
    "UIRecruitSoldiers::m_strListTitle");
static_assert(offsetof(AUIRecruitSoldiers, m_strNoRecruits) == 0x438,
    "UIRecruitSoldiers::m_strNoRecruits");

// ---- the covert action report (game_ui_strategy.cpp: UICovertActionReport) ----
//
// The action's ref and the CONTINUE button (a UILargeButton, read through UIButton::Text above).
// SlotContainer is pinned as the neighbour that proves the packed layout — the 8-byte button
// pointer really does sit 4 bytes after the ref — though the container itself is walked
// companion-side. UIRewardsRecap needs no pin: a finder only, every word companion-side.
static_assert(offsetof(AUICovertActionReport, ActionRef) == 0x440,
    "UICovertActionReport::ActionRef");
static_assert(offsetof(AUICovertActionReport, ContinueButton) == 0x444,
    "UICovertActionReport::ContinueButton");
static_assert(offsetof(AUICovertActionReport, SlotContainer) == 0x44C,
    "UICovertActionReport::SlotContainer");

// ---- the armory hub (game_ui_strategy.cpp: UIArmory_MainMenu) ----
//
// The list, plus the unit ref inherited from UIArmory (pinned under promotion). A row is a
// UIListItemString read through its generic Text / flag dword / AttentionIcon, all pinned above;
// its MCName (UIPanel::MCName, pinned at the top) is the row's identity. DismissButton is the
// class's tail, pinned as the neighbour that proves the Highlander's eight named fields are in
// the dump this build was cut from.
static_assert(offsetof(AUIArmory_MainMenu, List) == 0x6F0, "UIArmory_MainMenu::List");
static_assert(offsetof(AUIArmory_MainMenu, DismissButton) == 0x740,
    "UIArmory_MainMenu::DismissButton (Highlander Issue #47 fields)");

// ---- weapon upgrades (game_ui_strategy.cpp: UIArmory_WeaponUpgrade, _WeaponTrait, _WeaponUpgradeItem) ----
//
// The three lists, the active-list pointer and the weapon ref on the screen — every one of them
// 0x10 past vanilla because the Highlander's WeaponViewOffsets heads the class (Issue #832), so
// the first pin is also the Highlander detector. The row is the loadout row's layout through
// Count (its flag dword at 0x43C is bracketed by Count and UpgradeTemplate); the picker subclass
// adds the landing index.
static_assert(offsetof(AUIArmory_WeaponUpgrade, WeaponViewOffsets) == 0x4FC,
    "UIArmory_WeaponUpgrade::WeaponViewOffsets (Highlander Issue #832 — a vanilla dump lacks it)");
static_assert(offsetof(AUIArmory_WeaponUpgrade, ActiveList) == 0x510, "UIArmory_WeaponUpgrade::ActiveList");
static_assert(offsetof(AUIArmory_WeaponUpgrade, SlotsList) == 0x520, "UIArmory_WeaponUpgrade::SlotsList");
static_assert(offsetof(AUIArmory_WeaponUpgrade, UpgradesList) == 0x530,
    "UIArmory_WeaponUpgrade::UpgradesList");
static_assert(offsetof(AUIArmory_WeaponUpgrade, WeaponRef) == 0x538, "UIArmory_WeaponUpgrade::WeaponRef");
static_assert(offsetof(AUIArmory_WeaponUpgrade, CustomizeList) == 0x574,
    "UIArmory_WeaponUpgrade::CustomizeList");
static_assert(offsetof(AUIArmory_WeaponTrait, StartingIndex) == 0x704,
    "UIArmory_WeaponTrait::StartingIndex");
static_assert(offsetof(AUIArmory_WeaponUpgradeItem, Title) == 0x3D8, "UIArmory_WeaponUpgradeItem::Title");
static_assert(offsetof(AUIArmory_WeaponUpgradeItem, Subtitle) == 0x3E8,
    "UIArmory_WeaponUpgradeItem::Subtitle");
static_assert(offsetof(AUIArmory_WeaponUpgradeItem, Count) == 0x438, "UIArmory_WeaponUpgradeItem::Count");
static_assert(offsetof(AUIArmory_WeaponUpgradeItem, UpgradeTemplate) == 0x440,
    "UIArmory_WeaponUpgradeItem::UpgradeTemplate (the flag dword precedes at 0x43C)");
static_assert(offsetof(AUIArmory_WeaponUpgradeItem, SlotIndex) == 0x450,
    "UIArmory_WeaponUpgradeItem::SlotIndex");

// ---- the loot receipt (game_ui_tactical.cpp: UIInventoryTactical) ----
//
// The list, the OK button (read through UIButton::Text above) and the title; a row is a plain
// UIPanel keeping the item's ObjectID and nothing else.
static_assert(offsetof(AUIInventoryTactical, m_kLootList) == 0x418, "UIInventoryTactical::m_kLootList");
static_assert(offsetof(AUIInventoryTactical, m_kButton_OK) == 0x428,
    "UIInventoryTactical::m_kButton_OK");
static_assert(offsetof(AUIInventoryTactical, m_strTitle) == 0x458, "UIInventoryTactical::m_strTitle");
static_assert(offsetof(AUIInventoryTactical_LootingItem, GameStateObjectID) == 0x3D8,
    "UIInventoryTactical_LootingItem::GameStateObjectID");

// ---- soldier info (game_ui_tactical.cpp: UITacticalCharInfoScreen) ----
//
// The effects list, and the HUD's hint label the soldier stop's button is named with. The stat
// labels and the name are found by MCName (pinned above) and read as UIScrollingText — UIText
// shares that offset, pinned here because the section headings are bare UITexts. An effect row
// holds its UISummary_UnitEffect BY VALUE, so the struct's members are pinned as the row's own.
static_assert(offsetof(AUITacticalCharInfoScreen, ListPerks) == 0x410,
    "UITacticalCharInfoScreen::ListPerks");
static_assert(offsetof(AUITacticalHUD, m_strNavHelpCharShow) == 0x510,
    "UITacticalHUD::m_strNavHelpCharShow");
static_assert(offsetof(AUIText, Text) == 0x3D8, "UIText::Text");
static_assert(offsetof(AUIListItemPerk, Perk) == 0x3E0, "UIListItemPerk::Perk");
static_assert(offsetof(AUIListItemPerk, TitlePanel) == 0x420,
    "UIListItemPerk::TitlePanel (the struct ends before it)");
static_assert(offsetof(FUISummary_UnitEffect, Name) == 0x00, "UISummary_UnitEffect::Name");
static_assert(offsetof(FUISummary_UnitEffect, Description) == 0x10,
    "UISummary_UnitEffect::Description");
static_assert(offsetof(FUISummary_UnitEffect, AbilitySourceName) == 0x38,
    "UISummary_UnitEffect::AbilitySourceName");

// ---- soldier customization (game_ui_strategy.cpp: UICustomize family, UIColorSelector, UIColorChip) ----
//
// The unit ref and pointer, the list and the colour selector on the base class; IdleAnimName is
// the neighbour that pins the flag dword at 0x424 (bInArmory bit 0). The picker subclass adds the
// landing index (Data heads its tail). The selector keeps its chips and the colour worn now; a
// chip keeps its BG box (whose BGColor is pinned under the row above) and its grid position.
static_assert(offsetof(AUICustomize, UnitRef) == 0x418, "UICustomize::UnitRef");
static_assert(offsetof(AUICustomize, Unit) == 0x41C, "UICustomize::Unit");
static_assert(offsetof(AUICustomize, IdleAnimName) == 0x428,
    "UICustomize::IdleAnimName (the flag dword precedes at 0x424)");
static_assert(offsetof(AUICustomize, List) == 0x440, "UICustomize::List");
static_assert(offsetof(AUICustomize, ColorSelector) == 0x47C, "UICustomize::ColorSelector");
static_assert(offsetof(AUICustomize_Trait, Data) == 0x514, "UICustomize_Trait::Data");
static_assert(offsetof(AUICustomize_Trait, StartingIndex) == 0x544,
    "UICustomize_Trait::StartingIndex");
static_assert(offsetof(AUICustomize_Voice, m_strPreviewLabel) == 0x5C8,
    "UICustomize_Voice::m_strPreviewLabel");
static_assert(sizeof(AUIList_execOnChildMouseEvent_Params) == 0x0C
        && offsetof(AUIList_execOnChildMouseEvent_Params, Control) == 0x00
        && offsetof(AUIList_execOnChildMouseEvent_Params, Cmd) == 0x08,
    "UIList.OnChildMouseEvent parameter block (PCS single-click input)");
static_assert(offsetof(AUIColorSelector, ColorChips) == 0x400, "UIColorSelector::ColorChips");
static_assert(offsetof(AUIColorSelector, Colors) == 0x410, "UIColorSelector::Colors");
static_assert(offsetof(AUIColorSelector, InitialSelection) == 0x420,
    "UIColorSelector::InitialSelection");
static_assert(offsetof(AUIColorChip, BG) == 0x3D8, "UIColorChip::BG");
static_assert(offsetof(AUIColorChip, Index) == 0x3F8, "UIColorChip::Index");
static_assert(offsetof(AUIColorChip, Row) == 0x3FC, "UIColorChip::Row");
static_assert(offsetof(AUIColorChip, Col) == 0x400, "UIColorChip::Col");

// ---- manage equipment (game_ui_strategy.cpp: UIManageEquipmentMenu) ----
static_assert(offsetof(AUIManageEquipmentMenu, List) == 0x410, "UIManageEquipmentMenu::List");
static_assert(offsetof(AUIManageEquipmentMenu, m_strTitleLabel) == 0x440,
    "UIManageEquipmentMenu::m_strTitleLabel");

// ---- the character pool (game_ui_shell.cpp: UICharacterPool, UICharacterPool_ListPools, UIX2PanelHeader) ----
//
// The list, the header, the six buttons, the two labels the recipe speaks and the options list;
// on the files screen m_strTitle is the neighbour that pins the flag dword at 0x468.
static_assert(offsetof(AUICharacterPool, List) == 0x420, "UICharacterPool::List");
static_assert(offsetof(AUICharacterPool, TitleHeader) == 0x428, "UICharacterPool::TitleHeader");
static_assert(offsetof(AUICharacterPool, CreateButton) == 0x430, "UICharacterPool::CreateButton");
static_assert(offsetof(AUICharacterPool, ImportButton) == 0x438, "UICharacterPool::ImportButton");
static_assert(offsetof(AUICharacterPool, ExportButton) == 0x440, "UICharacterPool::ExportButton");
static_assert(offsetof(AUICharacterPool, DeleteButton) == 0x448, "UICharacterPool::DeleteButton");
static_assert(offsetof(AUICharacterPool, SelectAllButton) == 0x450,
    "UICharacterPool::SelectAllButton");
static_assert(offsetof(AUICharacterPool, DeselectAllButton) == 0x458,
    "UICharacterPool::DeselectAllButton");
static_assert(offsetof(AUICharacterPool, m_strEditCharacter) == 0x4D4,
    "UICharacterPool::m_strEditCharacter");
static_assert(offsetof(AUICharacterPool, m_strNoCharacters) == 0x554,
    "UICharacterPool::m_strNoCharacters");
static_assert(offsetof(AUICharacterPool, OptionsList) == 0x5F8, "UICharacterPool::OptionsList");
static_assert(offsetof(AUICharacterPool_ListPools, List) == 0x420,
    "UICharacterPool_ListPools::List");
static_assert(offsetof(AUICharacterPool_ListPools, TitleHeader) == 0x430,
    "UICharacterPool_ListPools::TitleHeader");
static_assert(offsetof(AUICharacterPool_ListPools, m_strTitle) == 0x46C,
    "UICharacterPool_ListPools::m_strTitle (the flag dword precedes at 0x468)");
static_assert(offsetof(AUIX2PanelHeader, Title) == 0x3D8, "UIX2PanelHeader::Title");
static_assert(offsetof(AUIX2PanelHeader, Label) == 0x3E8, "UIX2PanelHeader::Label");

// ---- the input object: PlayerController -> PlayerInput, and the XComInputBase gate bits ----
// FindLocalPlayerInput reads the pointer at 0x4C0. bAllowAllInput and UIMovie.MouseActive are
// bitfields and cannot be offsetof'd: the first sits in the dword at 0x20C (mask 0x80, confirmed by
// /props on the live class), pinned by the float after it; the second in the dword at 0x214 (mask
// 0x04), pinned by UI_RES_X after it.
static_assert(offsetof(APlayerController, PlayerInput) == 0x4C0, "PlayerController::PlayerInput");
static_assert(offsetof(UXComInputBase, fAutoBaseY) == 0x210,
    "XComInputBase flag dword (bAllowAllInput, mask 0x80) sits at 0x20C, before fAutoBaseY");
static_assert(offsetof(UUIMovie, UI_RES_X) == 0x218,
    "UIMovie flag dword (MouseActive, mask 0x04) sits at 0x214, before UI_RES_X");
// XComInputBase.InputEvent(int cmd, optional int ActionMask): both slots are hand-rolled, the
// optional one included.
static_assert(offsetof(UXComInputBase_execInputEvent_Params, Cmd) == 0x0
        && offsetof(UXComInputBase_execInputEvent_Params, Actionmask) == 0x4,
    "XComInputBase.InputEvent params");

} // namespace
