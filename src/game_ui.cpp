#include "game_ui.h"
#include "log.h"
// For UIAlert's message text: its words live in the screen's own `localized` vars, reachable only
// by NAME — which is what reflection does and UnrealScript cannot. See ReadAlertLocalized.
#include "ue3_reflection.h"
#include "flash_shadow.h"
#include "Graph/GraphAnnouncer.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Core reflection types only (UObject, UClass, FName, TArray) — the same proven set the other
// game-facing TUs use. Deliberately NOT the full generated SDK: its class headers are emitted
// under pack(0x8) and their COMPILED layouts drift from the engine's real ones, so all XComGame
// field access below goes through explicit offsets taken from the generator's reflection-dump
// comments and verified against a live process (see kOffsets below).
#include "GameDefines.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GameUI {

namespace {

// Engine field offsets/masks, from the generated SDK's reflection-dump comments — the comments
// are authoritative, the compiled classes are not. Core set validated live (ReadProcessMemory
// against a running WOTC shell, 2026-08-18): flags dword read 0x279/0xA7B with expected bits;
// MAINMENU held 6 buttons; Text read the localized "НОВАЯ ИГРА". The shell-extras set (ticker,
// My2K, tooltip, attention mask) comes from the same dump; every read through it is SEH-guarded
// and gated on the target's panel-visible bit.
constexpr int32_t kPanelFlagsOffset = 0x284;    // UIPanel flag dword
constexpr uint32_t kPanelInited = 0x10;
constexpr uint32_t kPanelVisible = 0x20;
constexpr uint32_t kPanelFocused = 0x40;
// bIsRemoved. A removed screen KEEPS its inited/visible/focused bits — the engine never clears
// them — so this is the only flag that tells a corpse from the live screen.
constexpr uint32_t kPanelRemoved = 0x80;
// UEngine's flag dword. `bSubtitlesEnabled` (0x10) is the game's Show Subtitles option after
// XComOnlineProfileSettings.ApplyOptions has copied the profile value onto the engine — the copy
// the game itself tests before drawing any subtitle, so it is the one parity is defined against.
constexpr int32_t kEngineFlagsOffset = 0x46C;
constexpr uint32_t kEngineSubtitlesEnabled = 0x10;

// A localized line is as long as the translator made it; this is the runaway guard on a count
// read out of engine memory, not a working limit (the longest stock movie subtitle is ~250).
constexpr int32_t kMaxPlausibleLocalizedChars = 1 << 16;

constexpr int32_t kButtonTextOffset = 0x3D8;    // UIButton::Text (FString)
constexpr int32_t kButtonFlagsOffset = 0x400;   // UIButton flag dword
constexpr uint32_t kButtonDisabled = 0x01;      // IsDisabled
constexpr uint32_t kButtonAttention = 0x80;     // bNeedsAttention (the "!" badge)
constexpr int32_t kShellMainMenuOffset = 0x4CC; // UIShell::MAINMENU (TArray<UIButton*>)
constexpr int32_t kShellTickerOffset = 0x4F4;   // UIShell::TickerText (UIScrollingText*)
constexpr int32_t kShellMy2KOffset = 0x5C8;     // UIFinalShell::m_my2KButton (UIButton*)
constexpr int32_t kShellLiveClient = 0x608;     // UIFinalShell::FiraxisLiveClient
constexpr int32_t kScrollTextOffset = 0x3D8;    // UIScrollingText::Text (FString)
constexpr int32_t kScrollHtmlOffset = 0x3E8;    // UIScrollingText::HtmlText (FString)

// UIOptionsPCScreen (the settings screen) and its row/widget classes, same reflection-dump
// provenance. The tab-name strings are NOT in tab-index order in the class: the enum is
// Audio(0), Video(1), Graphics(2), Gameplay(3), Interface(4) while the strings declare
// Video, Graphics, Audio, Gameplay, Interface — kOptsTabNameOffsets below is in ENUM order.
constexpr int32_t kOptsTitleOffset = 0x42C;       // m_strTitle (FString)
constexpr int32_t kOptsTabNameOffsets[5] = {0x4DC, 0x4BC, 0x4CC, 0x4EC, 0x4FC};
constexpr int32_t kOptsListOffset = 0x140C;       // List (UIList*)
constexpr int32_t kOptsMechaItemsOffset = 0x1414; // m_arrMechaItems (TArray<UIMechaListItem*>)
constexpr int32_t kOptsCurrentTabOffset = 0x1424; // m_iCurrentTab (int)
constexpr int32_t kOptsGPUButtonOffset = 0x1434;  // GPUAutoDetectButton (UIButton*)
constexpr int32_t kOptsSaveButtonOffset = 0x1444; // SaveAndExitButton (UIButton*)
constexpr int32_t kOptsExitButtonOffset = 0x144C; // ExitWitoutChangesButton (UIButton*)
constexpr int32_t kOptsCreditsButtonOffset = 0x1454; // CreditsButton (UIButton*)
constexpr int32_t kListSelectedIndexOffset = 0x404;  // UIList::SelectedIndex (int)
constexpr int32_t kMechaDescOffset = 0x404;       // UIMechaListItem::Desc (UIScrollingText*)
constexpr int32_t kMechaFlagsOffset = 0x414;      // UIMechaListItem flag dword
constexpr uint32_t kMechaDisabled = 0x01;         // bDisabled
constexpr int32_t kMechaValueOffset = 0x418;      // UIMechaListItem::Value (UIScrollingText*)
constexpr int32_t kMechaSpinnerOffset = 0x428;    // UIMechaListItem::Spinner (UIListItemSpinner*)
constexpr int32_t kMechaDropdownOffset = 0x430;   // UIMechaListItem::Dropdown (UIDropdown*)
constexpr int32_t kMechaButtonOffset = 0x438;     // UIMechaListItem::Button (UIButton*)
constexpr int32_t kMechaCheckboxOffset = 0x440;   // UIMechaListItem::Checkbox (UICheckbox*)
constexpr int32_t kMechaSliderOffset = 0x448;     // UIMechaListItem::Slider (UISlider*)
constexpr int32_t kMechaTypeOffset = 0x450;       // UIMechaListItem::Type (byte, EUILineItemType)
constexpr int32_t kCheckboxFlagsOffset = 0x3E8;   // UICheckbox flag dword
constexpr uint32_t kCheckboxChecked = 0x01;       // bChecked
constexpr int32_t kSliderPercentOffset = 0x3E8;   // UISlider::Percent (float, 0-100)
constexpr int32_t kSpinnerValueOffset = 0x3DC;    // UIListItemSpinner::Value (FString)
constexpr int32_t kDropdownItemsOffset = 0x3F8;   // UIDropdown::Items (TArray<FString>)
constexpr int32_t kDropdownSelectedOffset = 0x418; // UIDropdown::SelectedItem (int)

// Tooltip plumbing (mouse-over text a sighted player gets on hover). A panel with a tooltip
// carries the has-tooltip bit and the id of its entry in ITS movie's tooltip manager; the
// manager is reached through the panel's own Movie → PRES → m_kTooltipMgr chain — never a
// global scan, because two managers can be alive at once (2D HUD + 3D strategy movie) with
// independently-counted ids.
constexpr uint32_t kPanelHasTooltip = 0x100;      // UIPanel flag dword bit
constexpr int32_t kPanelTooltipIdOffset = 0x2AC;  // UIPanel::CachedTooltipId
constexpr int32_t kPanelMovieOffset = 0x2B8;      // UIPanel::Movie (UIMovie*)
constexpr int32_t kMoviePresOffset = 0x204;       // UIMovie::PRES (XComPresentationLayerBase*)
constexpr int32_t kPresTooltipMgrOffset = 0x55C;  // XComPresentationLayerBase::m_kTooltipMgr
constexpr int32_t kMgrTooltipsOffset = 0x41C;     // UITooltipMgr::Tooltips (TArray<UITooltip*>)
constexpr int32_t kTooltipIdOffset = 0x3D8;       // UITooltip::Id
constexpr int32_t kTextTooltipTitleOffset = 0x48C; // UITextTooltip::sTitle (FString)
constexpr int32_t kTextTooltipBodyOffset = 0x49C;  // UITextTooltip::sBody (FString)

// SetTooltipText tooltips are NOT objects in the Tooltips array: AddNewTooltipTextBox appends
// a TTextTooltipData STRUCT to the manager's single shared TextTooltip object. That data array
// is where hover text actually lives (verified 2026-08-19 after the object-array lookup read
// empty on every settings row).
constexpr int32_t kMgrTextTooltipOffset = 0x414;  // UITooltipMgr::TextTooltip (UITextTooltip*)
constexpr int32_t kTextTooltipDataOffset = 0x4BC; // UITextTooltip::TextTooltipData (TArray)
constexpr int32_t kTTDataStride = 0xAC;           // sizeof(FTTextTooltipData)
constexpr int32_t kTTDataIdOffset = 0x00;         // FTTextTooltipData::Id
constexpr int32_t kTTDataTitleOffset = 0x44;      // FTTextTooltipData::sTitle (FString)
constexpr int32_t kTTDataBodyOffset = 0x54;       // FTTextTooltipData::sBody (FString)

// ⛔ A tooltip's id is not always retained ON a panel. `AddNewTooltipTextBox` can be called
// STRAIGHT on the manager, in which case nothing sets bHasTooltip or CachedTooltipId and the
// only link back is `targetPath` — the Flash path the tooltip was registered against.
// UIUtilities_Strategy.AddAbilitySummaryTooltip is the pattern: every ability in an armory
// screen's summary strip gets its full description that way, against
// `<screen MCPath>.abilitySummaryList.theObject.AbilitySummaryItem<N>`, and there is no UIPanel
// for those items at all. So a panel-keyed read finds nothing and the text is nevertheless
// sitting in the same array. Matching on the path is the way in.
constexpr int32_t kTTDataTargetPathOffset = 0x1C; // FTTextTooltipData::targetPath (FString)

constexpr int32_t kMechaBGOffset = 0x40C;         // UIMechaListItem::BG (UIPanel*)
// ⛔ UIListItemString OVERRIDES SetTooltipText to forward to its ButtonBG child ("Set tooltip on
// BG since that's where our mouse events originate from"), and SetDisabled(bDisabled, Reason)
// parks the why-disabled text there too. So a list row's tooltip is never on the row.
constexpr int32_t kListItemBGOffset = 0x454;      // UIListItemString::ButtonBG (UIButton*)
constexpr int32_t kPanelMCPathOffset = 0x2B0;     // UIPanel::MCPath (FName)

// ---- new game flow: UIShellDifficulty ----
constexpr int32_t kDiffSelectTitleOffset = 0x410;   // m_strSelectDifficulty (FString)
constexpr int32_t kDiffChangeTitleOffset = 0x420;   // m_strChangeDifficulty (FString)
constexpr int32_t kDiffSWPanelTitleOffset = 0x770;  // SecondWavePanelTitle (FString)
constexpr int32_t kDiffSWToolTipsOffset = 0x810;    // SecondWaveToolTips (TArray<FString>)
// The four difficulty rows, in on-screen order — separate members, not an array.
constexpr int32_t kDiffRowOffsets[4] = {0x828, 0x830, 0x838, 0x840};
constexpr int32_t kDiffSWListOffset = 0x870;        // m_SecondWaveList (UIList*)
// The bottom panel's option rows, in the order BuildMenu spawns them (= on-screen order).
constexpr int32_t kDiffOptionOffsets[5] = {
    0x860, // m_SecondWaveButton (opens the Second Wave panel)
    0x880, // m_TutorialMechaItem
    0x888, // m_FirstTimeVOMechaItem
    0x890, // m_SubtitlesMechaItem
    0x898, // m_SoundtrackMechaItem (TLE entitlement only — null otherwise)
};
constexpr int32_t kDiffStartButtonOffset = 0x8A0;   // m_StartButton (UILargeButton*)
constexpr int32_t kDiffFlagsOffset = 0x8A8;         // UIShellDifficulty flag dword
constexpr uint32_t kDiffSecondWaveOpen = 0x0001;    // m_bSecondWaveOpen
constexpr uint32_t kDiffIsPlayingGame = 0x0800;     // m_bIsPlayingGame

// ---- new game flow: UIChooseIronMan ----
constexpr int32_t kIronTitleOffset = 0x420;         // m_strIronmanTitle (FString)
constexpr int32_t kIronBodyOffset = 0x430;          // m_strIronmanBody (FString)
constexpr int32_t kIronNoIronButtonOffset = 0x480;  // m_StartWithoutIronmanButton
constexpr int32_t kIronStartButtonOffset = 0x490;   // m_StartButton (enable Ironman)

// ---- new game flow: UIShellNarrativeContent ----
constexpr int32_t kNarrTitleOffset = 0x410;         // NarrativeContentTitle (FString)
constexpr int32_t kNarrIntegratedDescOffset = 0x4B0; // IntegratedDLCDesc (FString)
constexpr int32_t kNarrListOffset = 0x4E0;          // m_List (UIList*)
constexpr int32_t kNarrStartButtonOffset = 0x4E8;   // m_StartButton (UILargeButton*)
constexpr int32_t kNarrXpackItemOffset = 0x4F0;     // m_XpacknarrativeMechaItem
constexpr int32_t kNarrDLCInfosOffset = 0x510;      // DLCInfos (TArray<X2DownloadableContentInfo*>)
constexpr int32_t kDLCNarrativeSummaryOffset = 0xB4; // X2DownloadableContentInfo::NarrativeContentSummary
constexpr int kMaxDLCInfos = 32; // one per installed DLC — far past the shipped set

// ---- new game flow: UIShellSoundtrackPicker ----
// No title offset here on purpose: the retail package's localized m_chooseSoundtrack (0x430)
// does NOT exist in the Community Highlander's rebuild — the SDK's SrcOrig for this class never
// declared it — so with the Highlander installed 0x430 is the int m_soundtrackChoice and
// everything after it sits 0x10 lower. The screen name falls back to Vocab instead.
constexpr int32_t kSndListOffset = 0x410;           // m_List (UIList*)
constexpr int32_t kSndStartButtonOffset = 0x418;    // m_StartButton (UILargeButton*)

// ---- tactical HUD (UITacticalHUD) ----
constexpr int32_t kTacHudFlagsOffset = 0x410;       // flag dword holding m_isMenuRaised
constexpr uint32_t kTacHudMenuRaised = 0x02;        // m_isMenuRaised (shot HUD up)
constexpr int32_t kTacHudObjectivesOffset = 0x444;  // m_kObjectivesControl (UIObjectiveList*)
constexpr int32_t kTacHudAbilityHudOffset = 0x42C;  // m_kAbilityHUD (UITacticalHUD_AbilityContainer*)

// ---- the ability bar (UITacticalHUD_AbilityContainer / UITacticalHUD_Ability) ----
constexpr int32_t kAbilityBarCurrentOffset = 0x3DC;   // m_iCurrentIndex
constexpr int32_t kAbilityBarActiveOffset = 0x3EC;    // ActiveAbilities (live icon count)
constexpr int32_t kAbilityBarWidgetsOffset = 0x408;   // m_arrUIAbilities (TArray<UITacticalHUD_Ability*>)
constexpr int32_t kAbilityIconCooldownOffset = 0x3F8; // m_strCooldown
constexpr int32_t kAbilityIconChargeOffset = 0x408;   // m_strCharge
constexpr int32_t kAbilityIconAntennaOffset = 0x418;  // m_strAntennaText (ability name, PC only)
constexpr int32_t kAbilityIconHotkeyOffset = 0x428;   // m_strHotkeyLabel
constexpr int32_t kAbilityIconFlagsOffset = 0x438;    // dword holding IsAvailable / bIsShiney
constexpr uint32_t kAbilityIconAvailable = 0x01;      // IsAvailable
// The game's own bar capacity (UITacticalHUD_AbilityContainer.MAX_NUM_ABILITIES) — m_arrUIAbilities
// is created once at that length, so a structural cap two above it can only clip if the game does.
constexpr int kAbilityBarMax = 32;

// ---- the command buttons (UITacticalHUD_MouseControls) ----
// All four confirmed live 2026-08-31 via /props against the running class.
constexpr int32_t kTacHudMouseControlsOffset = 0x44C;  // UITacticalHUD::m_kMouseControls
constexpr int32_t kMouseCtlSelectionOffset = 0x46C;    // m_iCurrentSelection (OnAccept reads it)
constexpr int32_t kMouseCtlButtonItemsOffset = 0x474;  // ButtonItems (TArray<TButtonItems>)
constexpr int32_t kMouseCtlCommandsOffset = 0x484;     // CommandAbilities (TArray<AvailableAction>)
// Inside one TButtonItems element. The element STRIDE is deliberately not a constant here: the
// ScriptStruct's runtime size is 0x21 and whether the engine rounds it to 4 when striding is the
// engine's call, so ReadMouseControls asks the live UArrayProperty for its Inner->ElementSize.
constexpr int32_t kMouseCtlNoKeyOffset = 0x408;        // m_strNoKeyBoundString (localized)
constexpr int32_t kButtonItemLabelOffset = 0x00;       // Label (FString)
constexpr int32_t kButtonItemKeyOffset = 0x10;         // key (FString)
constexpr int32_t kButtonItemStateOffset = 0x20;       // UIState (byte; 0 = eUIState_Normal)
// The world-mode button indices, from the class defaults (m_opt*): 0-4 fixed, 5 the first
// command-ability slot, 6 Chosen info. InitMouseControls sets ButtonItems.Length = 10 and
// nothing ever resizes it — the count doubles as the is-this-panel-sane check.
constexpr int kMouseCtlFixedButtons = 5;               // end turn, prev, next, cam left, cam right
constexpr int kMouseCtlCommandSlot = 5;                // m_optCallSkyranger
constexpr int kMouseCtlChosenSlot = 6;                 // m_optChosenInfo
constexpr int kMouseCtlButtonCount = 10;               // ButtonItems.Length, set once at init

// ---- the objectives list (UIObjectiveList / UIObjectiveListItem) ----
// Verified live 2026-08-23 against the running class on LostAndAbandonedB: m_kObjectivesControl
// 0x444, Items 0x3D8 (4 entries), Data 0x3DC. The Avenger's owner offset is from the dump only —
// the ship was not reachable in that session (see docs/surfaces/tactical.md).
constexpr int32_t kAvengerHudObjectivesOffset = 0x44C; // UIAvengerHUD::Objectives
constexpr int32_t kObjListItemsOffset = 0x3D8;        // UIObjectiveList::Items
constexpr int32_t kObjItemDataOffset = 0x3DC;         // UIObjectiveListItem::Data
// Localized, and CONFIRMED populated on the running class (unlike UITacticalHUD_InfoPanel's
// copies) — so the heading is spoken in the player's language rather than ours.
constexpr int32_t kObjItemListTitleOffset = 0x47C;    // ::m_strObjectivesListTitle
constexpr int32_t kObjItemDarkTitleOffset = 0x48C;    // ::m_strDarkEventsListTitle
constexpr int32_t kObjItemInProgressOffset = 0x4AC;   // ::m_strInProgress
// FObjectiveDisplayInfo, RELATIVE to Data.
constexpr int32_t kObjInfoMissionType = 0x0C;    // FString
constexpr int32_t kObjInfoDisplayLabel = 0x1C;   // FString
constexpr int32_t kObjInfoFlags = 0x2C;          // bitfield dword
constexpr uint32_t kObjInfoCompleted = 0x01;
constexpr uint32_t kObjInfoFailed = 0x02;
constexpr uint32_t kObjInfoCheckBox = 0x04;
constexpr uint32_t kObjInfoWarning = 0x08;
constexpr uint32_t kObjInfoHeader = 0x10;
constexpr uint32_t kObjInfoDarkEvent = 0x20;
constexpr int32_t kObjInfoTimer = 0x30;          // int, -1 = no timer
constexpr int32_t kObjInfoHaveImage = 0x34;      // FString
constexpr int32_t kObjInfoAvailImage = 0x44;     // FString
constexpr int32_t kObjInfoLostImage = 0x54;      // FString
constexpr int32_t kObjInfoHaveAmount = 0x64;     // int, -1 = not a counter row
constexpr int32_t kObjInfoHaveMin = 0x68;
constexpr int32_t kObjInfoAvailAmount = 0x6C;
constexpr int32_t kObjInfoLostAmount = 0x70;
// The list is short by construction (one row per objective plus a heading); 64 is a runaway
// guard, not an expected bound, and a list that hit it would be logged by the caller.
constexpr int kMaxObjectiveItems = 64;

// ---- the mission stage counters (UISpecialMissionHUD / _TurnCounter) ----
// Verified live 2026-08-23 on LostAndAbandonedB: counter 1 visible carrying
// "Приведите солдат на запасной маршрут" / "Осталось солдат" / "3", counter 2 inited and hidden.
constexpr int32_t kSpecHudCounter1Offset = 0x418; // UISpecialMissionHUD::m_kGenericTurnCounter
constexpr int32_t kSpecHudCounter2Offset = 0x420; // ::m_kTurnCounter2
constexpr int32_t kTurnCounterLabelOffset = 0x3FC;    // ::m_sLabel
constexpr int32_t kTurnCounterSubLabelOffset = 0x40C; // ::m_sSubLabel
constexpr int32_t kTurnCounterCounterOffset = 0x41C;  // ::m_sCounter
constexpr int32_t kTurnCounterFlagsOffset = 0x42C;    // bitfield dword
constexpr uint32_t kTurnCounterLocked = 0x01;
constexpr uint32_t kTurnCounterExpired = 0x02;
constexpr uint32_t kTurnCounterInfinity = 0x04;

// ---- tutorial mission: the modal popup (UITutorialBox) ----
constexpr int32_t kTutBoxTitleOffset = 0x410;      // Title (FString)
constexpr int32_t kTutBoxDescOffset = 0x420;       // Desc (FString)
constexpr int32_t kTutBoxButtonHelpOffset = 0x430; // ButtonHelp (FString)

// ---- tutorial mission: world messages (UIWorldMessageMgr, reached via the presentation layer) ----
// The manager is a UIScreen the stack never holds, so it is reached the way the game reaches it:
// XComPresentationLayerBase.m_kWorldMessageManager, off the tactical HUD's own Movie -> PRES.
constexpr int32_t kPresWorldMsgMgrOffset = 0x344; // XComPresentationLayerBase::m_kWorldMessageManager
constexpr int32_t kWorldMsgArrayOffset = 0x450;   // UIWorldMessageMgr::m_arrMsgs (TArray<FTHUDMsg>)
constexpr int32_t kWorldMsgActiveOffset = 0x460;  // ::m_arrActiveMessages (TArray<int>) — indices
constexpr int32_t kWorldMsgBladeOffset = 0x480;   // ::BladeHUDMessage (UIPanel*), the HUD blade
constexpr int32_t kHudMsgStride = 0x78;           // sizeof(FTHUDMsg)
constexpr int32_t kHudMsgIdOffset = 0x00;         // FTHUDMsg::m_sId (FString)
constexpr int32_t kHudMsgTextOffset = 0x10;       // FTHUDMsg::m_txtMsg (FString)
constexpr int32_t kHudMsgFollowOffset = 0x30;     // FTHUDMsg::m_kFollowObject.ObjectID
constexpr int32_t kHudMsgFlagsOffset = 0x34;      // FTHUDMsg bitfield dword
constexpr uint32_t kHudMsgVisible = 0x2;          // m_bVisible (0x1 is m_bFollowWorldLocation)
constexpr uint32_t kHudMsgImportant = 0x4;        // m_bIsImportantMessage
constexpr int32_t kHudMsgTypeOffset = 0x74;       // FTHUDMsg::m_eType (byte, EHUDMessageType)
// The pool is NUM_INITIAL_INACTIVE_MESSAGES (8) recycled slots plus one appended per NAMED
// message, so the array is small but not fixed; 64 is far past anything a mission produces.
constexpr int kMaxWorldMessages = 64;

// ---- tutorial mission: the replay manager (XComTutorialMgr : XComReplayMgr : Actor) ----
constexpr int32_t kReplayFrameOffset = 0x274;       // XComReplayMgr::CurrentHistoryFrame
constexpr int32_t kReplayFlagsOffset = 0x278;       // XComReplayMgr flag dword
constexpr uint32_t kReplayInTutorial = 0x2;         // bInTutorial (0x1 is bInReplay)
constexpr int32_t kTutFlagsOffset = 0x294;          // XComTutorialMgr flag dword
constexpr uint32_t kTutAllowingControls = 0x8;      // bIsTutorialAllowingControls
constexpr int32_t kTutNextInputFrameOffset = 0x298; // NextPlayerInputFrame
constexpr int32_t kTutInvalidCountOffset = 0x31C;   // InvalidSubmissionCnt
constexpr int32_t kTutNextAbilityCtxOffset = 0x338; // NextPlayerAbilityContext

// ---- modal dialog: UIDialogueBox ----
constexpr int32_t kDialogDataOffset = 0x434;        // m_arrData (TArray<FTDialogueBoxData>)
constexpr int32_t kDialogAcceptOffset = 0x444;      // strAccept (FString) — the LIVE label
constexpr int32_t kDialogCancelOffset = 0x454;      // strCancel (FString)
constexpr int32_t kDlgDataStride = 0xD0;            // sizeof(FTDialogueBoxData)
constexpr int32_t kDlgDataTitleOffset = 0x08;       // FTDialogueBoxData::strTitle
constexpr int32_t kDlgDataTextOffset = 0x18;        // FTDialogueBoxData::strText

// ---- in-game pause menu: UIPauseMenu (also the strategy layer's; UIShellStrategy extends it) ----
constexpr int32_t kPauseSelectionOffset = 0x410;    // m_iCurrentSelection (int)
constexpr int32_t kPauseListOffset = 0x41C;         // List (UIList*)
constexpr int32_t kPauseTitleOffset = 0x42C;        // m_sPauseMenu (FString)

// ---- save / load: UILoadGame + UISaveGame + their shared row (docs/save-load-recon.md §4) ----
//
// The two screens declare the same three fields first, so selection and rows sit at IDENTICAL
// offsets on both and one read covers them; only the title and the in-flight flag differ.
constexpr int32_t kSaveSelectionOffset = 0x410;     // m_iCurrentSelection (int) — BOTH screens
constexpr int32_t kSaveListItemsOffset = 0x424;     // m_arrListItems (TArray<row*>) — BOTH
constexpr int32_t kLoadTitleOffset = 0x448;         // UILoadGame::m_sLoadTitle (FString)
constexpr int32_t kLoadFlagsOffset = 0x434;         // UILoadGame flag dword
constexpr uint32_t kLoadInProgress = 0x0002;        // m_bLoadInProgress
constexpr int32_t kSaveTitleOffset = 0x508;         // UISaveGame::m_sSaveTitle (FString)
constexpr int32_t kSaveStageOffset = 0x504;         // UISaveGame::m_SaveStage (byte, ESaveStage)

// UISaveLoadGameListItem — one row, shared by both screens.
constexpr int32_t kSaveRowAcceptOffset = 0x3D8;     // AcceptButton (load / save / new save)
constexpr int32_t kSaveRowDeleteOffset = 0x3E0;     // DeleteButton — Hide()n on the new-save row
constexpr int32_t kSaveRowIndexOffset = 0x4B0;      // Index — the screen's SetSelection target
constexpr int32_t kSaveRowIdOffset = 0x4B4;         // ID — -1 on the save screen's new-save row

// ---- text input dialog: UIInputDialogue (m_kData, a TInputDialogData, starts at 0x410) ----
constexpr int32_t kInputTitleOffset = 0x410;        // m_kData.strTitle (FString)
constexpr int32_t kInputTextOffset = 0x420;         // m_kData.strInputBoxText — the PREFILL
constexpr int32_t kInputMaxCharsOffset = 0x430;     // m_kData.iMaxChars (int)
constexpr int32_t kInputTypeOffset = 0x434;         // m_kData.DialogType (byte, InputDialogType)

// ---- UIListItemString (the pause menu's rows — a plain text row, not a UIMechaListItem) ----
constexpr int32_t kStringItemTextOffset = 0x3F4;    // Text (FString)
constexpr int32_t kStringItemFlagsOffset = 0x414;   // bDisabled bit 0 (bIsBad/bIsGood/... follow)
constexpr uint32_t kStringItemDisabled = 0x0001;

// ---- squad select: UISquadSelect + its cards (docs/squad-select-recon.md §3) ----
constexpr int32_t kSquadSelectedSlotOffset = 0x410; // m_iSelectedSlot — LAGS, never the authority
constexpr int32_t kSquadSlotListOffset = 0x420;     // m_kSlotList (UIList_SquadEditor*)
constexpr int32_t kSquadLaunchButtonOffset = 0x428; // LaunchButton (UILargeButton*)
constexpr int32_t kSquadSoldierSlotsOffset = 0x460; // SoldierSlotCount (int)
constexpr int32_t kSquadFlagsOffset = 0x478;        // UISquadSelect flag dword
constexpr uint32_t kSquadLaunched = 0x0001;         // bLaunched
constexpr uint32_t kSquadWalkupDone = 0x0002;       // bReceivedWalkupEvent — input gate
constexpr uint32_t kSquadNoCancel = 0x0020;         // bNoCancel
constexpr uint32_t kSquadDisableLoadout = 0x0100;   // bDisableLoadout
constexpr int32_t kSquadLaunchLabelOffset = 0x480;  // m_strLaunch (FString)
constexpr int32_t kSquadMissionLabelOffset = 0x490; // m_strMission (FString)
constexpr int32_t kSquadStripGearOffset = 0x4C0;    // m_strStripGear (FString)
constexpr int32_t kSquadStripWeaponsOffset = 0x4F0; // m_strStripWeapons (FString)
constexpr int32_t kSquadStripItemsOffset = 0x520;   // m_strStripItems (FString)
constexpr int32_t kSquadBuildItemsOffset = 0x550;   // m_strBuildItems (FString)
constexpr int32_t kSquadBoostSoldierOffset = 0x570; // m_strBoostSoldier (FString)

// UISquadSelect_ListItem — one soldier card.
constexpr int32_t kCardSlotIndexOffset = 0x3D8;   // SlotIndex — into XComHQ.Squad, NOT visual
constexpr int32_t kCardFlagsOffset = 0x3DC;       // UISquadSelect_ListItem flag dword
constexpr uint32_t kCardDisabled = 0x0001;        // bDisabled — the greyed locked card
constexpr uint32_t kCardDisabledLoadout = 0x0002; // bDisabledLoadout
constexpr uint32_t kCardDisabledEdit = 0x0004;    // bDisabledEdit
constexpr uint32_t kCardDisabledDismiss = 0x0008; // bDisabledDismiss
constexpr int32_t kCardUtilitySlotsOffset = 0x408; // UtilitySlots (UIList*)
constexpr int32_t kCardBondIconOffset = 0x410;     // BondIcon (UIBondIcon*)
constexpr int32_t kCardActiveButtonOffset = 0x4E0; // m_eActiveButton (byte, ESquadSelectButton)

// UIArmory_Loadout / UIArmory_LoadoutItem. The flag dwords are pinned by the plain members
// on either side of them, the way every other bitfield in this file is.
constexpr int32_t kLoadoutActiveListOffset = 0x04FC;   // ActiveList (UIList*)
constexpr int32_t kLoadoutEquippedListOffset = 0x050C; // EquippedList
constexpr int32_t kLoadoutLockerListOffset = 0x051C;   // LockerList
constexpr int32_t kLoadoutInventoryTitleOffset = 0x053C;
constexpr int32_t kLoadoutLockerTitleOffset = 0x054C;
constexpr int32_t kLoadoutTipStripItemsOffset = 0x09DC;
constexpr int32_t kLoadoutTipStripItemsOffOffset = 0x09EC;
constexpr int32_t kLoadoutTipStripGearOffset = 0x09FC;
constexpr int32_t kLoadoutTipStripGearOffOffset = 0x0A0C;
constexpr int32_t kLoadoutTipStripWeaponsOffset = 0x0A1C;
constexpr int32_t kLoadoutTipStripWeaponsOffOffset = 0x0A2C;
constexpr int32_t kLoadoutMakeAvailableOffset = 0x0A4C;
constexpr int32_t kLoadoutFlagsOffset = 0x0A74; // between LocTag (0xA6C) and StrippedUnits (0xA78)
constexpr uint32_t kLoadoutGearStripped = 0x0001;
constexpr uint32_t kLoadoutItemsStripped = 0x0002;
constexpr uint32_t kLoadoutWeaponsStripped = 0x0004;

constexpr int32_t kLoadItemTitleOffset = 0x03D8;
constexpr int32_t kLoadItemSubtitleOffset = 0x03E8;
constexpr int32_t kLoadItemSlotTypeOffset = 0x0428;
constexpr int32_t kLoadItemCountOffset = 0x0438;
constexpr int32_t kLoadItemFlagsOffset = 0x043C; // between Count and EquipmentSlot
constexpr uint32_t kLoadItemLocked = 0x0001;
constexpr uint32_t kLoadItemInfinite = 0x0002;
constexpr uint32_t kLoadItemDisabled = 0x0004;
constexpr uint32_t kLoadItemCanBeCleared = 0x0008;
constexpr uint32_t kLoadItemLoadoutSlot = 0x0010;
constexpr int32_t kLoadItemEquipSlotOffset = 0x0440; // EInventorySlot (byte)
constexpr int32_t kLoadItemDropButtonOffset = 0x0458;

// UISquadSelect_UtilityItem — one small inventory slot on a card.
constexpr int32_t kUtilButtonOffset = 0x3E0;    // Button (UIButton*) — carries the disabled bit
constexpr int32_t kUtilSlotIndexOffset = 0x3F0; // SlotIndex (int)
constexpr int32_t kUtilSlotTypeOffset = 0x3F4;  // SlotType (byte, EInventorySlot)

// ---- after action: UIAfterAction + its soldier panels (docs/after-action-recon.md §3) ----
constexpr int32_t kAfterActionFlagsOffset = 0x410;  // UIAfterAction flag dword
constexpr uint32_t kAfterActionHudShown = 0x0004;   // bRecievedShowHUDRemoteEvent — input gate
constexpr int32_t kAfterActionSlotListOffset = 0x4E4; // m_kSlotList (UIList*)

// UIAfterAction_ListItem — one soldier panel. The words all went into Flash; what it keeps is
// the unit ref, two flags and the two buttons.
constexpr int32_t kAAItemUnitRefOffset = 0x3D8;     // UnitReference (StateObjectReference, int)
constexpr int32_t kAAItemPromoteButtonOffset = 0x454; // PromoteButton — ALSO the memorial button
constexpr int32_t kAAItemFlagsOffset = 0x464;       // UIAfterAction_ListItem flag dword
constexpr uint32_t kAAItemCanPromote = 0x0002;      // m_bCanPromote
constexpr uint32_t kAAItemKIA = 0x0004;             // m_bKIA — PromoteButton means Memorialize
constexpr int32_t kAAItemBondIconOffset = 0x468;    // BondIcon (UIBondIcon*)

// Caps for the squad-select / armory list reads. Each is a bound WE chose rather than one the
// game states, so each read goes through TakeListItemsRaw and says so if the game hands us more.
constexpr int kMaxSquadCards = 16;   // the squad's slot cards
constexpr int kMaxUtilitySlots = 16; // one card's utility row
constexpr int kMaxLoadoutRows = 64;  // the armory's inventory-slot rows

// ---- UIScreenStack: the game's own list of what is on screen ----
constexpr int32_t kStackFlagsOffset = 0x60;    // UIScreenStack flag dword
constexpr uint32_t kStackInputBlocked = 0x1;   // IsInputBlocked — "ignore input, system is gated"
constexpr uint32_t kStackScreensVisible = 0x2; // ScreensVisible — the whole UI is showing
constexpr uint32_t kStackCinematic = 0x8;      // bCinematicMode — screens hidden for cinematics
constexpr int32_t kStackScreensOffset = 0x6C;  // UIScreenStack::Screens (TArray<UIScreen*>)
constexpr int kMaxStackScreens = 32;
#ifdef X2ACCESS_DEVSERVER
// Classes derived from one named class, for the dev dumps' ?instance= lookup.
constexpr int kMaxDerivedClasses = 64;
#endif

// ---- generic UIList / UIPanel traversal ----
constexpr int32_t kListItemContainerOffset = 0x3D8; // UIList::ItemContainer (UIPanel*)
constexpr int32_t kPanelChildPanelsOffset = 0x2E8;  // UIPanel::ChildPanels (TArray<UIPanel*>)

// EUILineItemType values (must match UIMechaListItem.uc / Flash).
constexpr uint8_t kLineDescription = 0;
constexpr uint8_t kLineColorChip = 1;
constexpr uint8_t kLineSpinner = 2;
constexpr uint8_t kLineDropdown = 3;
constexpr uint8_t kLineSlider = 4;
constexpr uint8_t kLineCheckbox = 5;
constexpr uint8_t kLineButton = 6;
constexpr uint8_t kLineValue = 7;
constexpr uint8_t kLineSpinnerAndButton = 8;

// UIUtilities_Input action masks. The widgets gate input through the native
// CheckInputIsReleaseOrDirectionRepeat: NON-direction commands (Enter) act on the RELEASE
// phase, while DIRECTION commands (arrows) act on press/repeat and their release is
// deliberately excluded (releasing a held arrow must not add a step) — verified live
// 2026-08-19: arrows sent with RELEASE were swallowed by the gate (sliders/spinners never
// moved) while Enter with RELEASE worked.
constexpr int32_t kActionPress = 1;
constexpr int32_t kActionRelease = 32;

// ---- raw, SEH-safe primitives (no C++ objects with destructors inside __try) ----

// The only WRITE primitive into engine memory. Everything else here reads; a write is reserved
// for a field the GAME itself treats as a plain cursor (the hero promotion screen's column and
// panel indices), never for state the engine derives or reference-counts.
void WriteI32Raw(void* obj, int32_t offset, int32_t value)
{
    __try {
        *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(obj) + offset) = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

uint32_t ReadU32Raw(const void* obj, int32_t offset)
{
    __try {
        return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(obj) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void* ReadPtrRaw(const void* obj, int32_t offset)
{
    __try {
        return *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(obj) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// UE3 FString on Windows is TArray<wchar_t> despite the SDK declaring char:
// [wchar_t* data (0x00)] [int32 count (0x08)] [int32 max (0x0C)].
int CopyFStringRaw(const void* fstr, wchar_t* buf, int bufWchars)
{
    __try {
        auto* raw = reinterpret_cast<const uint8_t*>(fstr);
        auto* data = *reinterpret_cast<wchar_t* const*>(raw);
        int32_t count = *reinterpret_cast<const int32_t*>(raw + 8);
        if (!data || count <= 1)
            return 0;
        int len = count - 1; // count includes the terminator
        if (len >= bufWchars)
            len = bufWchars - 1;
        memcpy(buf, data, static_cast<size_t>(len) * sizeof(wchar_t));
        buf[len] = L'\0';
        return len;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buf[0] = L'\0';
        return 0;
    }
}

// Name-entry access rule: NEVER walk GNames linearly — the array can hold malformed slots whose
// access violations would be silently absorbed by our guards and poison the lookup. Only
// dereference entries via display indices taken from LIVE objects, with bounds checks.

// SEH-safe: the object's own ANSI name into `buf`; false on any fault or wide/missing entry.
bool ObjNameAnsiRaw(UObject* obj, char* buf, int bufLen)
{
    __try {
        int32_t idx = obj->Name.GetDisplayIndex();
        TArray<FNameEntry*>* names = FName::Names();
        if (!names || idx < 0 || idx >= names->size())
            return false;
        FNameEntry* entry = names->at(idx);
        if (!entry)
            return false;
        const char* ansi = entry->GetAnsiName();
        if (!ansi)
            return false;
        for (int i = 0; i < bufLen - 1; i++) {
            buf[i] = ansi[i];
            if (!ansi[i])
                return true;
        }
        buf[bufLen - 1] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A CLASS object: the object named `className` whose own class is named "Class". Class
// objects persist for the whole session, so these resolve once.
UObject* FindClassRaw(const char* className)
{
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return nullptr;
        char name[64];
        char clsName[64];
        for (int32_t i = 0; i < objects->size(); i++) {
            UObject* obj = objects->at(i);
            if (!obj)
                continue;
            if (!ObjNameAnsiRaw(obj, name, sizeof(name)) || strcmp(name, className) != 0)
                continue;
            UClass* cls = obj->Class;
            if (!cls)
                continue;
            if (ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), clsName, sizeof(clsName))
                && strcmp(clsName, "Class") == 0)
                return obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// The name of an object's CLASS, for matching without having resolved a UClass pointer first.
bool ObjClassNameAnsiRaw(void* obj, char* buf, int bufLen)
{
    __try {
        UClass* cls = reinterpret_cast<UObject*>(obj)->Class;
        return cls && ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), buf, bufLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A live INSTANCE: class matches by POINTER; its name is the class's display name, which
// excludes the class default object ("Default__<name>") for free.
bool IsInstanceRaw(UObject* obj, UObject* classObj, const char* className)
{
    __try {
        if (!obj || !classObj || reinterpret_cast<UObject*>(obj->Class) != classObj)
            return false;
        char name[64];
        return ObjNameAnsiRaw(obj, name, sizeof(name)) && strcmp(name, className) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The instance of `classObj` that `usable` accepts, searching from the end of GObjects.
//
// Searching from the end used to be justified as "newest allocation first", and that is WRONG:
// UE3 hands out freed slots again, so a freshly spawned screen routinely sits at a LOWER index
// than corpses of its own class. Live UIPauseMenu at 383831 with removed ones at 383887 and
// 383932 is a real observation, and since a removed screen keeps its inited/visible/focused
// bits, taking the last match handed back a corpse that passed every readiness test, read as
// empty, and left the layer silently detached with the menu open on screen. So the direction is
// now just a tie-break; `usable` is what actually decides.
UObject* ScanInstanceRaw(UObject* classObj, const char* className, bool (*usable)(void*))
{
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return nullptr;
        for (int32_t i = objects->size() - 1; i >= 0; i--) {
            UObject* obj = objects->at(i);
            if (obj && IsInstanceRaw(obj, classObj, className) && (!usable || usable(obj)))
                return obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// The same walk resolving SEVERAL classes at once: `out[i]` receives the newest instance of
// `classes[i]`. One pass, so N screens cost what one costs — the point of the shared new-game
// finder below.
// Returns false when the walk did not finish — GObjects unreadable, or a fault partway (the
// object array is mutated by the game thread during a world change, so this genuinely happens).
// The caller MUST NOT treat a partial result as "these screens are absent".
bool ScanInstancesRaw(UObject* const* classes, const char* const* names,
    bool (*const* usable)(void*), void** out, int count)
{
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return false;
        int remaining = 0;
        for (int i = 0; i < count; i++) {
            out[i] = nullptr;
            if (classes[i])
                remaining++;
        }
        for (int32_t i = objects->size() - 1; i >= 0 && remaining > 0; i--) {
            UObject* obj = objects->at(i);
            if (!obj)
                continue;
            for (int c = 0; c < count; c++) {
                if (out[c] || !classes[c] || !IsInstanceRaw(obj, classes[c], names[c]))
                    continue;
                if (usable[c] && !usable[c](obj))
                    continue; // a corpse or a half-built screen is not an answer
                out[c] = obj;
                remaining--;
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// Inited + focused, straight off the UIPanel flag dword. Same test as the public
// IsScreenFocused, available this early for the finders' usability check.
//
// bIsVisible is DELIBERATELY not tested, and that cost several rounds of testing to learn: the
// game leaves it false on screens that are plainly on screen and taking input. A live focused
// pause menu reads 0x259 and its exit-to-main-menu confirmation dialog reads 0x259 too — inited,
// navigable, focused, NOT visible. Requiring it meant staying silent at a modal question the
// player then had to answer blind. bIsFocused is the flag the screen stack maintains for "this
// is the screen on top", which is the thing we actually need to know.
bool IsFocusedRaw(void* screen)
{
    uint32_t flags = ReadU32Raw(screen, kPanelFlagsOffset);
    return (flags & kPanelInited) && (flags & kPanelFocused) && !(flags & kPanelRemoved);
}

// Bumped by HintScreenChange. A finder that scanned in an older epoch has a cache from before a
// known screen change, so it rescans immediately instead of waiting out its rate limit — the
// rate limit is there to bound idle cost, not to make a screen change take a second to notice.
uint64_t gScanEpoch = 1;

// The cached find-a-screen pattern shared by every screen recipe: resolve the class object once,
// keep the newest live instance, and rescan GObjects at most once a second.
//
// Revalidating the cache by TYPE alone is not enough, and getting that wrong cost 63 seconds of
// silence in testing: returning to the main menu builds a NEW UIFinalShell while the old one
// sits in GObjects unfocused until GC gets to it. The dead one still passes IsInstanceRaw, so
// the finder kept handing it back, every probe saw an unfocused screen, and nothing rescanned
// until the allocation was finally reused. So the cache short-circuits the scan only while it
// is USABLE (`usable`, e.g. still the focused screen); otherwise the rescan runs and takes
// whatever is newest now. A screen the scan cannot find clears the cache rather than lingering.
struct ScreenFinder {
    const char* className;
    bool (*usable)(void*) = IsFocusedRaw;
    UObject* classObj = nullptr;
    void* cached = nullptr;
    uint64_t lastScanMs = 0;
    uint64_t scanEpoch = 0;
    int emptyScans = 0;

    void* Find()
    {
        // The scan only ever returns a USABLE instance, so the cache holds one too and the
        // revalidation here is just "is it still that". Anything else — collected, removed,
        // covered by another screen — drops it and waits for the next scan rather than handing
        // back a pointer that already failed the test.
        if (cached
            && IsInstanceRaw(reinterpret_cast<UObject*>(cached), classObj, className)
            && (!usable || usable(cached)))
            return cached;
        cached = nullptr;

        uint64_t now = GetTickCount64();
        if (scanEpoch == gScanEpoch && now - lastScanMs < 1000)
            return cached;
        lastScanMs = now;
        scanEpoch = gScanEpoch;

        if (!classObj) {
            classObj = FindClassRaw(className);
            if (classObj)
                LOG_INFO("GameUI: %s class object at 0x%p", className, classObj);
            else if (++emptyScans % 30 == 1)
                LOG_DEBUG("GameUI: %s class object not found yet (scan #%d)", className, emptyScans);
            if (!classObj)
                return nullptr;
            emptyScans = 0;
        }

        void* found = ScanInstanceRaw(classObj, className, usable);
        // Only a CHANGE is worth a line. The rescan runs once a second for as long as the screen
        // exists but is covered (that is the whole point of it), so logging every hit would bury
        // the log in repeats of the same address.
        if (found && found != cached)
            LOG_INFO("GameUI: found %s instance at 0x%p", className, found);
        cached = found;
        if (!cached && ++emptyScans % 30 == 1)
            LOG_DEBUG("GameUI: no %s instance yet (scan #%d)", className, emptyScans);
        return cached;
    }
};

struct RawMenuItem {
    void* handle;
    wchar_t label[256];
    bool focused;
    bool disabled;
    bool attention;
};

int ReadMainMenuRaw(void* shell, RawMenuItem* out, int maxItems)
{
    int count = 0;
    __try {
        // MAINMENU is TArray<UIButton*>: [data ptr (0x00)] [count (0x08)] [max (0x0C)].
        auto* arr = reinterpret_cast<const uint8_t*>(shell) + kShellMainMenuOffset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 64)
            return 0;
        for (int32_t i = 0; i < n && count < maxItems; i++) {
            void* btn = data[i];
            if (!btn)
                continue;
            uint32_t panelFlags = ReadU32Raw(btn, kPanelFlagsOffset);
            if (!(panelFlags & kPanelVisible))
                continue;
            RawMenuItem& item = out[count];
            item.handle = btn;
            item.focused = (panelFlags & kPanelFocused) != 0;
            uint32_t buttonFlags = ReadU32Raw(btn, kButtonFlagsOffset);
            item.disabled = (buttonFlags & kButtonDisabled) != 0;
            item.attention = (buttonFlags & kButtonAttention) != 0;
            if (CopyFStringRaw(reinterpret_cast<const uint8_t*>(btn) + kButtonTextOffset,
                    item.label, 256) <= 0)
                continue; // a button with no text is scenery, not a control
            count++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return count;
}

// ---- settings screen raw readers ----

uint8_t ReadU8Raw(const void* obj, int32_t offset)
{
    __try {
        return *(reinterpret_cast<const uint8_t*>(obj) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFF;
    }
}

float ReadF32Raw(const void* obj, int32_t offset)
{
    __try {
        return *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(obj) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

int32_t ReadI32Raw(const void* obj, int32_t offset)
{
    return static_cast<int32_t>(ReadU32Raw(obj, offset));
}

// The dropdown's current selection text: Items[SelectedItem] out of a TArray<FString>
// (16-byte elements: data/count/max).
int ReadDropdownSelectionRaw(const void* dropdown, wchar_t* buf, int bufWchars)
{
    buf[0] = L'\0';
    __try {
        int32_t sel = *reinterpret_cast<const int32_t*>(
            reinterpret_cast<const uint8_t*>(dropdown) + kDropdownSelectedOffset);
        auto* arr = reinterpret_cast<const uint8_t*>(dropdown) + kDropdownItemsOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || sel < 0 || sel >= n || n > 4096)
            return 0;
        return CopyFStringRaw(data + static_cast<size_t>(sel) * 16, buf, bufWchars);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buf[0] = L'\0';
        return 0;
    }
}

// The m_arrMechaItems pointer array, indices preserved (row index IS the List selection index —
// rows are spawned into the list in array order).
int ReadOptionRowPtrsRaw(void* screen, void** out, int maxItems, bool* clipped = nullptr)
{
    if (clipped)
        *clipped = false;
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(screen) + kOptsMechaItemsOffset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0)
            return 0;
        if (n > maxItems) {
            n = maxItems;
            if (clipped)
                *clipped = true;
        }
        for (int32_t i = 0; i < n; i++)
            out[i] = data[i];
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Any TArray<T*> at `offset`: copies up to maxItems pointers out, indices preserved. Returns
// the count copied, or -1 if the array itself could not be read. `clipped`, when asked for, says
// whether the array held MORE than maxItems — a different fact from "the count came back equal
// to the cap", and the one TakeArrayRaw below has to report.
int ReadPtrArrayRaw(const void* obj, int32_t offset, void** out, int maxItems,
    bool* clipped = nullptr)
{
    if (clipped)
        *clipped = false;
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(obj) + offset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0)
            return 0;
        if (n > maxItems) {
            n = maxItems;
            if (clipped)
                *clipped = true;
        }
        for (int32_t i = 0; i < n; i++)
            out[i] = data[i];
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// A TArray's own count at `offset`, without reading one element: -1 if it could not be read.
// This is what lets a read be sized from the DATA rather than from a buffer we picked in
// advance — see ReadPtrArrayAllRaw.
int32_t ReadArrayCountRaw(const void* obj, int32_t offset)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(obj) + offset;
        if (!*reinterpret_cast<void* const* const*>(arr))
            return 0;
        return *reinterpret_cast<const int32_t*>(arr + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Element `index` of a TArray<FString> at `offset` (16-byte elements).
int CopyFStringElemRaw(const void* obj, int32_t offset, int index, wchar_t* buf, int bufWchars)
{
    buf[0] = L'\0';
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(obj) + offset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || index < 0 || index >= n || n > 4096)
            return 0;
        return CopyFStringRaw(data + static_cast<size_t>(index) * 16, buf, bufWchars);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buf[0] = L'\0';
        return 0;
    }
}

// A UIList's items in index order. GetItem(i) IS ItemContainer.ChildPanels[i] (UIList.uc), so
// reading the child array directly matches the game's own indexing — which is what
// SetSelectedIndex and the OnItemClicked delegates are expressed in.
int ReadListItemsRaw(void* list, void** out, int maxItems, bool* clipped = nullptr)
{
    if (clipped)
        *clipped = false;
    if (!list)
        return 0;
    void* container = ReadPtrRaw(list, kListItemContainerOffset);
    if (!container)
        return 0;
    return ReadPtrArrayRaw(container, kPanelChildPanelsOffset, out, maxItems, clipped);
}

// ---- bounded array reads that SAY SO when they clip ----
//
// ⛔ Size the buffer from the data, never the data to the buffer — and a cap that drops the tail
// must be LOUD. A silently truncated list reads to a blind player as *the screen has nothing
// more*: indistinguishable from the truth, and therefore never reported as a bug. That is the
// failure mode the 2051-character payload had, and every fixed-cap array read in this file is
// the same bug waiting for a long enough list.
//
// Most caps here are structural — a room has eight staff slots, the strategy map has
// MAX_NUM_STRATEGYICONS — but "structural" is a claim about the GAME that holds only until a mod
// or a patch disagrees, and the -1 the primitives above return on a FAULTED read is otherwise
// indistinguishable from an empty list. Both go through here.
//
// Once per SITE, not once per render: these run on every graph rebuild, ten times a second. The
// latch map is touched only on the report path, so the ordinary case costs one compare.
constexpr uint8_t kNoteFault = 0x1;       // the read faulted
constexpr uint8_t kNoteClip = 0x2;        // the tail was dropped
constexpr uint8_t kNoteImplausible = 0x4; // the count was too large to believe

// True the FIRST time this (site, kind) pair comes up. The latch is what makes a warning
// affordable on a path that runs ten times a second.
bool NoteOnce(const char* what, uint8_t kind)
{
    static std::unordered_map<std::string, uint8_t> sSaid;
    uint8_t& said = sSaid[what ? what : "?"];
    if (said & kind)
        return false;
    said |= kind;
    return true;
}

int NoteArrayRead(const char* what, int count, bool clipped, int maxItems)
{
    if (count < 0) {
        if (NoteOnce(what, kNoteFault))
            LOG_WARN("%s: the array could not be read — treating it as empty", what);
        return 0;
    }
    if (clipped && NoteOnce(what, kNoteClip))
        LOG_WARN("%s: more than %d entries — the tail is NOT being read", what, maxItems);
    return count;
}

// ReadPtrArrayRaw with the -1 sentinel honoured and a dropped tail logged. `what` names the site
// and doubles as the latch key, so pass a literal.
int TakeArrayRaw(const char* what, const void* obj, int32_t offset, void** out, int maxItems)
{
    bool clipped = false;
    const int count = ReadPtrArrayRaw(obj, offset, out, maxItems, &clipped);
    return NoteArrayRead(what, count, clipped, maxItems);
}

// ReadListItemsRaw under the same contract.
int TakeListItemsRaw(const char* what, void* list, void** out, int maxItems)
{
    bool clipped = false;
    const int count = ReadListItemsRaw(list, out, maxItems, &clipped);
    return NoteArrayRead(what, count, clipped, maxItems);
}

// The WHOLE of a TArray<T*>, sized from the array's own count. For a list whose length the GAME
// decides and nothing bounds — the save list is the one that really grows — a cap is not
// something to log about, it is something not to have: the entries it drops are saves the player
// made, and a list that stops reads exactly like a list that ended. A count too large to be a
// screen's list is refused with a line rather than walked.
std::vector<void*> ReadPtrArrayAllRaw(const char* what, const void* obj, int32_t offset)
{
    std::vector<void*> out;
    const int32_t n = ReadArrayCountRaw(obj, offset);
    if (n == 0)
        return out;
    if (n < 0) {
        NoteArrayRead(what, -1, false, 0);
        return out;
    }
    // Not a guess about this screen: past this a count is far likelier a bad offset than a long
    // list, and allocating for it would be the fault we are trying to avoid.
    constexpr int32_t kImplausibleCount = 65536;
    if (n > kImplausibleCount) {
        if (NoteOnce(what, kNoteImplausible))
            LOG_WARN("%s: the array claims %d entries — refusing to read it", what, n);
        return out;
    }
    out.resize(static_cast<size_t>(n));
    const int got = ReadPtrArrayRaw(obj, offset, out.data(), n);
    out.resize(static_cast<size_t>(NoteArrayRead(what, got, false, n)));
    return out;
}

// ---- the game's own screen stack ----
//
// This is the authoritative answer to "what is on screen", and it is why the GObjects scans
// below are now only a fallback. A popped screen is off the stack the instant it is popped,
// whereas as an OBJECT it lingers with all its state bits intact until GC — which is how a
// corpse ended up cached as the live pause menu (see ScanInstanceRaw).
//
// Screens is TOP-FIRST: Push does Screens.InsertItem(0, Screen) and GetCurrentScreen returns
// the first non-none entry.

// A stack worth trusting: it has screens and its top one is live. During a world change the
// outgoing stack is torn down, so this is what distinguishes it from the incoming one.
bool StackIsLiveRaw(void* stack)
{
    void* top[1] = {};
    if (ReadPtrArrayRaw(stack, kStackScreensOffset, top, 1) < 1 || !top[0])
        return false;
    uint32_t flags = ReadU32Raw(top[0], kPanelFlagsOffset);
    return (flags & kPanelInited) && !(flags & kPanelRemoved);
}

ScreenFinder gStackFinder{"UIScreenStack", StackIsLiveRaw};

// Screens, top first, into `out`; 0 when there is no live stack. Every walker below opens with
// this — one place to find the stack, one place that says so if the array outgrows the buffer.
int WalkStackRaw(void** out, int max)
{
    void* stack = gStackFinder.Find();
    if (!stack)
        return 0;
    return TakeArrayRaw("ScreenStack: Screens", stack, kStackScreensOffset, out, max);
}

// ⛔ THE READINESS PREDICATE FOR A SCREEN ON THE STACK, in one place, because it is spelled out
// at a dozen walkers and two hard rules are about it.
//
// `bIsInited && !bIsRemoved`, and NOT bIsVisible: the game leaves the visible bit false on
// screens it is plainly showing (a live pause menu reads 0x259), so testing it means silence at
// a modal the player then answers blind. bIsRemoved is the one flag a corpse does not keep —
// removal sets it and clears nothing else, so inited/visible/focused all still read true on a
// dead screen.
//
// This deliberately does NOT also require bIsFocused, which IS the right test for a GObjects
// scan (see IsFocusedRaw) and would be redundant here: position in this array already answers
// "is it on top", and it is the game's own answer rather than a bit we hope is in step.
bool StackEntryLiveRaw(void* screen)
{
    const uint32_t flags = ReadU32Raw(screen, kPanelFlagsOffset);
    return (flags & kPanelInited) && !(flags & kPanelRemoved);
}

// The GetCurrentScreen rule, which five of the finders below share: the TOPMOST non-null entry,
// and only when it matches and is live. A non-match on top is a REFUSAL, not a reason to keep
// looking — a screen under another screen has already lost the keys, and answering for it would
// put our stops in front of the screen the player is actually on.
//
// `matches` is handed the entry and its class name, so a matcher may compare either; `nameOut`
// receives that name when an entry is returned.
void* TopStackScreenRaw(bool (*matches)(void* screen, const char* className),
    char* nameOut = nullptr, int nameCap = 0)
{
    void* screens[kMaxStackScreens] = {};
    const int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            return nullptr;
        if (!matches(screens[i], name) || !StackEntryLiveRaw(screens[i]))
            return nullptr;
        if (nameOut && nameCap > 0)
            strncpy_s(nameOut, static_cast<size_t>(nameCap), name, _TRUNCATE);
        return screens[i];
    }
    return nullptr;
}

// The topmost live screen of `className` on the stack, or null if the stack does not hold one.
// Matching is by class NAME, so no UClass has to be resolved first — the stack is at most a few
// dozen entries, which makes the name compares cheaper than the class lookup would be.
//
// Unlike TopStackScreenRaw this keeps LOOKING past a non-match: the caller asked for a specific
// screen wherever it sits, not for the one on top.
void* FindStackScreenRaw(const char* className)
{
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (!screens[i] || !StackEntryLiveRaw(screens[i]))
            continue;
        char name[64];
        if (ObjClassNameAnsiRaw(screens[i], name, sizeof(name)) && strcmp(name, className) == 0)
            return screens[i];
    }
    return nullptr;
}

// Stack first, object scan second. The stack answers correctly and instantly for anything it
// manages; the fallback covers screens it does not (UIDialogueBox is permanent and never
// pushed) and the window during a world change while the stack itself is being rebuilt. The
// first time a class can only be found by the fallback we say so once — that is how we learn
// which screens are not stack-managed, instead of assuming.
void* PreferStack(const char* className, ScreenFinder& finder, bool& reportedFallback)
{
    if (void* onStack = FindStackScreenRaw(className))
        return onStack;
    void* found = finder.Find();
    if (found && !reportedFallback) {
        reportedFallback = true;
        LOG_INFO("GameUI: %s is not on the screen stack — falling back to an object scan",
            className);
    }
    return found;
}

// The TTextTooltipData element with this id in the manager's shared TextTooltip (where
// SetTooltipText text lands), or null.
const uint8_t* FindTextTooltipDataRaw(void* mgr, int32_t id)
{
    __try {
        void* shared = *reinterpret_cast<void* const*>(
            reinterpret_cast<const uint8_t*>(mgr) + kMgrTextTooltipOffset);
        if (!shared)
            return nullptr;
        auto* arr = reinterpret_cast<const uint8_t*>(shared) + kTextTooltipDataOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 4096)
            return nullptr;
        for (int32_t i = 0; i < n; i++) {
            const uint8_t* el = data + static_cast<size_t>(i) * kTTDataStride;
            if (*reinterpret_cast<const int32_t*>(el + kTTDataIdOffset) == id)
                return el;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// SEH-safe: the ANSI text of the FName stored AT `addr` (entry id, then instance number), the
// way the engine prints it — instance N shows as "_N-1", which is what makes a live panel's
// MCPath read "UIFinalShell_0". Distinct from ObjNameAnsiRaw, which reads an OBJECT's own name.
bool FNameAtAnsiRaw(const void* addr, char* buf, int bufLen)
{
    __try {
        const int32_t id = *reinterpret_cast<const int32_t*>(addr);
        const int32_t instance = *(reinterpret_cast<const int32_t*>(addr) + 1);
        TArray<FNameEntry*>* names = FName::Names();
        if (!names || id < 0 || id >= names->size())
            return false;
        FNameEntry* entry = names->at(id);
        if (!entry)
            return false;
        const char* ansi = entry->GetAnsiName();
        if (!ansi)
            return false;
        int i = 0;
        for (; i < bufLen - 1 && ansi[i]; i++)
            buf[i] = ansi[i];
        buf[i] = '\0';
        if (instance > 0 && i < bufLen - 1)
            snprintf(buf + i, static_cast<size_t>(bufLen - i), "_%d", instance - 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ⛔ RAW, not ReadFStringField: that one runs CleanText, which strips <...> and collapses
// whitespace. A Flash path is an identifier being COMPARED, not a phrase being spoken, and a
// cleaned-vs-raw mismatch would fail silently — which reads to the player as "this has no
// tooltip", indistinguishable from the truth.
std::string ReadFStringFieldRaw(const void* obj, int32_t offset); // defined with the readers below

// The bounds of the manager's shared tooltip-data array, PODs only. Split out from the search
// below because an SEH __try cannot live in a frame that needs object unwinding (C2712), and
// matching a path means building std::strings.
bool TextTooltipDataSpanRaw(void* mgr, const uint8_t** outData, int32_t* outCount)
{
    __try {
        void* shared = *reinterpret_cast<void* const*>(
            reinterpret_cast<const uint8_t*>(mgr) + kMgrTextTooltipOffset);
        if (!shared)
            return false;
        auto* arr = reinterpret_cast<const uint8_t*>(shared) + kTextTooltipDataOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 4096)
            return false;
        *outData = data;
        *outCount = n;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The TTextTooltipData element registered against exactly this Flash path, or null. Exact match
// rather than a suffix: the caller composes the whole path from a live panel's own MCPath, and a
// suffix test would happily answer with another screen's row of the same name.
const uint8_t* FindTextTooltipDataByPathRaw(void* mgr, const std::string& path)
{
    const uint8_t* data = nullptr;
    int32_t n = 0;
    if (!TextTooltipDataSpanRaw(mgr, &data, &n))
        return nullptr;
    // Newest LAST is how the array grows, so search backwards: a screen that re-populates
    // registers a second entry against the same path without removing the first, and the one
    // the player is actually hovering is the one added most recently.
    for (int32_t i = n - 1; i >= 0; i--) {
        const uint8_t* el = data + static_cast<size_t>(i) * kTTDataStride;
        if (ReadFStringFieldRaw(el, kTTDataTargetPathOffset) == path)
            return el;
    }
    return nullptr;
}

// The tooltip with this id in the manager's Tooltips array (preformed tooltips only), or null.
void* FindTooltipByIdRaw(void* mgr, int32_t id)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(mgr) + kMgrTooltipsOffset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 4096)
            return nullptr;
        for (int32_t i = 0; i < n; i++) {
            void* tip = data[i];
            if (tip
                && *reinterpret_cast<const int32_t*>(
                       reinterpret_cast<const uint8_t*>(tip) + kTooltipIdOffset)
                    == id)
                return tip;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

void* ReadObjectClassRaw(UObject* obj); // defined with the resolution cache below

// The object's class is named exactly `className` (no inheritance walk — used where the
// concrete class is known).
bool ObjectClassNamedRaw(void* obj, const char* className)
{
    void* cls = ReadObjectClassRaw(reinterpret_cast<UObject*>(obj));
    char name[64];
    return cls && ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), name, sizeof(name))
        && strcmp(name, className) == 0;
}

struct RawOptionRow {
    uint8_t type;
    bool disabled;
    bool checked;
    int percent;
    wchar_t label[256];
    wchar_t value[256];
};

// One row: label from Desc (SetHTMLText fills the HTML slot; plain Text is the fallback) and
// the active widget's current state, selected by the row's Type byte. False = the row is
// hidden or unreadable and should not become a node.
bool ReadOptionRowRaw(void* item, RawOptionRow* out)
{
    out->type = 0xFF;
    out->disabled = false;
    out->checked = false;
    out->percent = -1;
    out->label[0] = L'\0';
    out->value[0] = L'\0';

    if (!item || !(ReadU32Raw(item, kPanelFlagsOffset) & kPanelVisible))
        return false;

    out->type = ReadU8Raw(item, kMechaTypeOffset);
    out->disabled = (ReadU32Raw(item, kMechaFlagsOffset) & kMechaDisabled) != 0;

    // A row's declared Type is NOT a reliable answer to "is this a checkbox". Every Second Wave
    // row reads Type = Description while showing a live, visible UICheckbox whose bChecked is
    // the very bit the campaign start reads (GetSelectedSecondWaveOptionNames) — verified live
    // 2026-08-19 with the box ticked on screen. Reading state only in the Type branch made those
    // rows report "unchecked" forever, which looked exactly like a toggle that would not take.
    //
    // ⚠ WHERE THAT Description COMES FROM, since it would not reproduce for anyone reading
    // SrcOrig: the GAME types those rows Checkbox (UIShellDifficulty.uc:472, and again inside
    // UpdateDataCheckbox at UIMechaListItem.uc:549). OUR OWN companion rewrites them —
    // UIScreenListener_Difficulty.FixSecondWaveItems, once per Second Wave panel open, so that
    // Enter stops being eaten by a bReadOnly checkbox. So on this screen the Type tag is a field
    // two of our layers touch, and it must never be read as evidence about the engine.
    //
    // The rule survives that correction, because the reason to prefer the VISIBLE widget is the
    // GAME's: SetWidgetType hides the widgets it switches away from and leaves their pointers
    // behind, so presence alone means nothing while visibility means the row is showing one.
    // Reading the widget POINTER rather than the tag is also what makes this immune to a Type a
    // mod — ours included — has rewritten. State comes from the checkbox whenever there is one;
    // only the row's KIND needs it visible.
    void* checkbox = ReadPtrRaw(item, kMechaCheckboxOffset);
    if (checkbox) {
        out->checked = (ReadU32Raw(checkbox, kCheckboxFlagsOffset) & kCheckboxChecked) != 0;
        if (ReadU32Raw(checkbox, kPanelFlagsOffset) & kPanelVisible)
            out->type = kLineCheckbox;
    }

    void* desc = ReadPtrRaw(item, kMechaDescOffset);
    if (desc) {
        if (CopyFStringRaw(reinterpret_cast<const uint8_t*>(desc) + kScrollHtmlOffset,
                out->label, 256) <= 0)
            CopyFStringRaw(reinterpret_cast<const uint8_t*>(desc) + kScrollTextOffset,
                out->label, 256);
    }

    switch (out->type) {
    case kLineSpinner:
    case kLineSpinnerAndButton: {
        void* spinner = ReadPtrRaw(item, kMechaSpinnerOffset);
        if (spinner)
            CopyFStringRaw(reinterpret_cast<const uint8_t*>(spinner) + kSpinnerValueOffset,
                out->value, 256);
        break;
    }
    case kLineDropdown: {
        void* dropdown = ReadPtrRaw(item, kMechaDropdownOffset);
        if (dropdown)
            ReadDropdownSelectionRaw(dropdown, out->value, 256);
        break;
    }
    case kLineSlider: {
        void* slider = ReadPtrRaw(item, kMechaSliderOffset);
        if (slider)
            out->percent = static_cast<int>(ReadF32Raw(slider, kSliderPercentOffset));
        break;
    }
    case kLineCheckbox:
        break; // state already read above, from the widget rather than the type tag
    case kLineButton: {
        void* button = ReadPtrRaw(item, kMechaButtonOffset);
        if (button)
            CopyFStringRaw(reinterpret_cast<const uint8_t*>(button) + kButtonTextOffset,
                out->value, 256);
        break;
    }
    case kLineValue:
    case kLineColorChip: {
        void* value = ReadPtrRaw(item, kMechaValueOffset);
        if (value) {
            if (CopyFStringRaw(reinterpret_cast<const uint8_t*>(value) + kScrollHtmlOffset,
                    out->value, 256) <= 0)
                CopyFStringRaw(reinterpret_cast<const uint8_t*>(value) + kScrollTextOffset,
                    out->value, 256);
        }
        break;
    }
    default:
        break;
    }
    return out->label[0] != L'\0';
}

// Find a UFunction by name walking a class's field chain (super classes included) — avoids the
// SDK's whole-GObjects function cache, whose first build stalls the game thread for seconds.
// Field names resolve through ObjNameAnsiRaw (bounds-checked, per-field SEH), so one malformed
// field skips rather than aborting the whole walk.
UFunction* FindFunctionInClassRaw(UObject* obj, const char* funcName)
{
    __try {
        char name[64];
        for (UStruct* cur = reinterpret_cast<UStruct*>(obj->Class); cur;
             cur = reinterpret_cast<UStruct*>(cur->SuperField)) {
            for (UField* field = cur->Children; field; field = field->Next) {
                if (!ObjNameAnsiRaw(field, name, sizeof(name)) || strcmp(name, funcName) != 0)
                    continue;
                // Properties and functions share the field chain; require a Function object.
                UClass* fieldClass = field->Class;
                if (!fieldClass)
                    continue;
                if (ObjNameAnsiRaw(reinterpret_cast<UObject*>(fieldClass), name, sizeof(name))
                    && strcmp(name, "Function") == 0)
                    return reinterpret_cast<UFunction*>(field);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// One level of a UStruct's own field chain, matched by BOTH name and field class. The class
// check is what makes this usable for states: properties, functions and states all share the
// single `Children` chain, so a name match alone would happily hand back a UProperty.
UField* FindFieldInStructRaw(void* structObj, const char* fieldName, const char* fieldClass)
{
    __try {
        char name[64];
        for (UField* field = reinterpret_cast<UStruct*>(structObj)->Children; field;
             field = field->Next) {
            if (!ObjNameAnsiRaw(field, name, sizeof(name)) || strcmp(name, fieldName) != 0)
                continue;
            UClass* cls = field->Class;
            if (!cls)
                continue;
            if (ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), name, sizeof(name))
                && strcmp(name, fieldClass) == 0)
                return field;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// Same walk as FindFunctionInClassRaw, but starting AT the given UStruct instead of at its
// class — for looking a function up on a UClass object directly, which is how statics are
// called (the generated wrappers pass the class object as `this`).
UFunction* FindFunctionInClassStructRaw(void* classObj, const char* funcName)
{
    __try {
        char name[64];
        for (UStruct* cur = reinterpret_cast<UStruct*>(classObj); cur;
             cur = reinterpret_cast<UStruct*>(cur->SuperField)) {
            for (UField* field = cur->Children; field; field = field->Next) {
                if (!ObjNameAnsiRaw(field, name, sizeof(name)) || strcmp(name, funcName) != 0)
                    continue;
                UClass* fieldClass = field->Class;
                if (!fieldClass)
                    continue;
                if (ObjNameAnsiRaw(reinterpret_cast<UObject*>(fieldClass), name, sizeof(name))
                    && strcmp(name, "Function") == 0)
                    return reinterpret_cast<UFunction*>(field);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

void* ReadObjectClassRaw(UObject* obj)
{
    __try {
        return obj->Class;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Resolution cache + first-resolution logging (game thread only). Keyed per (class, name) —
// two classes can each define the same function name (OnUnrealCommand, Click), and a cache
// keyed by name alone would hand one class's UFunction to the other's instances. UFunction and
// class objects persist for the session. A NOT-FOUND is logged loudly because a silent
// write-back failure costs a debug session to find.
UFunction* ResolveFunctionCached(void* obj, const char* funcName)
{
    static std::unordered_map<std::string, UFunction*> sCache;
    void* cls = ReadObjectClassRaw(reinterpret_cast<UObject*>(obj));
    if (!cls)
        return nullptr; // unreadable object — don't resolve against it, don't cache
    std::string key = std::string(funcName) + "@" + std::to_string(reinterpret_cast<uintptr_t>(cls));
    auto it = sCache.find(key);
    if (it != sCache.end())
        return it->second;
    UFunction* fn = FindFunctionInClassRaw(reinterpret_cast<UObject*>(obj), funcName);
    if (fn)
        LOG_INFO("GameUI: resolved function \"%s\" (class 0x%p) at 0x%p", funcName, cls, fn);
    else
        LOG_WARN("GameUI: function \"%s\" NOT FOUND on the object's class chain", funcName);
    sCache.emplace(std::move(key), fn);
    return fn;
}

bool CallFunctionRaw(void* obj, UFunction* fn, void* params)
{
    __try {
        reinterpret_cast<UObject*>(obj)->ProcessEvent(fn, params, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallNamedFunction(void* obj, const char* funcName, void* params)
{
    if (!obj)
        return false;
    UFunction* fn = ResolveFunctionCached(obj, funcName);
    return fn && CallFunctionRaw(obj, fn, params);
}

// A `static native` script function, called on the CLASS object itself — what the generated
// wrappers pass as `this` for statics.
//
// Call it through plain ProcessEvent, exactly like a script function. The generated SDK wrappers
// clear FUNC_Native (0x400) around the call and copying that is a TRAP here: a native function's
// Script array holds a 2-byte stub, not a body (measured: IsAnyMoviePlaying has Script.Count 2,
// against 1663 for UIPauseMenu.OnChildClicked), so clearing the flag makes ProcessEvent run the
// stub instead of the native and return a zeroed param block — indistinguishable from a real
// "no". X2FiraxisLiveClient.IsLoggedIn is FUNC_Native too and has always worked here through
// plain ProcessEvent; that is the proof this is the right call.
bool CallStaticNative(const char* className, const char* funcName, void* params)
{
    static std::unordered_map<std::string, UObject*> sClasses;
    static std::unordered_map<std::string, UFunction*> sFuncs;
    static uint64_t sLastTryMs = 0;

    UObject*& cls = sClasses[className];
    if (!cls) {
        // Resolving a class is a full GObjects walk; if it is genuinely missing, don't pay that
        // on every poll.
        uint64_t now = GetTickCount64();
        if (now - sLastTryMs < 1000)
            return false;
        sLastTryMs = now;
        cls = FindClassRaw(className);
        if (!cls)
            return false;
        LOG_INFO("GameUI: %s class object at 0x%p", className, cls);
    }

    UFunction*& fn = sFuncs[std::string(className) + "." + funcName];
    if (!fn) {
        fn = FindFunctionInClassStructRaw(cls, funcName);
        if (!fn) {
            LOG_WARN("GameUI: static native \"%s.%s\" NOT FOUND", className, funcName);
            return false;
        }
        LOG_INFO("GameUI: resolved static native \"%s.%s\" at 0x%p", className, funcName, fn);
    }
    return CallFunctionRaw(cls, fn, params);
}

// The current world's sound manager instance. The GRI spawns it as a SUBCLASS: XComShellGRI
// extends XComTacticalGRI, so the shell (main menu) world's manager is an
// XComTacticalSoundManager too; the Avenger uses XComStrategySoundManager. Both register the
// UI cues (DefaultGameData [XComGame.XCom*SoundManager] includes SoundGlobalUI.Play_Mouseover).
void* FindSoundManager()
{
    static const char* const kClassNames[2]
        = {"XComTacticalSoundManager", "XComStrategySoundManager"};
    static UObject* sClasses[2] = {nullptr, nullptr};
    static void* sCached = nullptr;
    static int sCachedIdx = -1;
    static uint64_t sLastScanMs = 0;
    static uint64_t sScanEpoch = 0;

    // Type-revalidation alone is NOT enough here, for the same reason it is not enough for a
    // screen (see ScreenFinder): a world change spawns a new manager while the old one stays a
    // perfectly well-formed instance of its class until GC gets to it. A cue sent to the dead
    // one is swallowed in silence — which is why the arrow-key click stopped after exiting a
    // campaign to the main menu, while narration carried on fine. There is no focus bit to ask
    // a sound manager about, so instead the cache is trusted for only a second at a time and
    // every rescan takes the NEWEST instance of either subclass.
    uint64_t now = GetTickCount64();
    bool live = sCached && sCachedIdx >= 0
        && IsInstanceRaw(reinterpret_cast<UObject*>(sCached), sClasses[sCachedIdx],
            kClassNames[sCachedIdx]);
    if (!live) {
        sCached = nullptr;
        sCachedIdx = -1;
    }
    if (sScanEpoch == gScanEpoch && now - sLastScanMs < 1000)
        return sCached;
    sLastScanMs = now;
    sScanEpoch = gScanEpoch;

    for (int i = 0; i < 2; i++)
        if (!sClasses[i])
            sClasses[i] = FindClassRaw(kClassNames[i]);

    // One walk from the newest object down, matching either subclass — the first hit is the
    // newest manager of either kind, which is the current world's.
    void* found = nullptr;
    int foundIdx = -1;
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        for (int32_t o = objects ? objects->size() - 1 : -1; o >= 0 && !found; o--) {
            UObject* obj = objects->at(o);
            if (!obj)
                continue;
            for (int i = 0; i < 2; i++) {
                if (sClasses[i] && IsInstanceRaw(obj, sClasses[i], kClassNames[i])) {
                    found = obj;
                    foundIdx = i;
                    break;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    if (found && found != sCached)
        LOG_INFO("GameUI: sound manager (%s) at 0x%p", kClassNames[foundIdx], found);
    sCached = found;
    sCachedIdx = foundIdx;
    return sCached;
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (needed <= 0)
        return {};
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0],
        needed);
    return result;
}

std::string WcharToUtf8(const wchar_t* wbuf, int len)
{
    if (len <= 0)
        return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, wbuf, len, nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf, len, &result[0], needed, nullptr, nullptr);
    return result;
}

// An FString field read straight to cleaned UTF-8 (empty on fault or empty string).
std::string ReadFStringField(const void* obj, int32_t offset)
{
    wchar_t buf[1024];
    int len = CopyFStringRaw(reinterpret_cast<const uint8_t*>(obj) + offset, buf, 1024);
    return len > 0 ? CleanText(WcharToUtf8(buf, len)) : std::string();
}

// The same read with NO cleaning, for a field that is an identifier rather than a phrase — a
// Flash path being matched. CleanText would strip <...> and collapse whitespace, so a cleaned
// value can never be compared against a raw one.
std::string ReadFStringFieldRaw(const void* obj, int32_t offset)
{
    wchar_t buf[1024];
    int len = CopyFStringRaw(reinterpret_cast<const uint8_t*>(obj) + offset, buf, 1024);
    return len > 0 ? WcharToUtf8(buf, len) : std::string();
}

// The My2K link state, queried the way the game's own UpdateMy2KButtonStatus does (offline /
// link / unlink), through the shell's FiraxisLive client. TTL-cached at the 1 s cadence the
// game's own status timer runs at, so the 10 Hz graph rebuild stays cheap. Game thread only.
My2KStatus QueryMy2KStatusCached(void* shell)
{
    static void* sClient = nullptr;
    static My2KStatus sStatus = My2KStatus::Unknown;
    static uint64_t sLastMs = 0;

    void* client = ReadPtrRaw(shell, kShellLiveClient);
    if (!client)
        return My2KStatus::Unknown;
    uint64_t now = GetTickCount64();
    if (client == sClient && now - sLastMs < 1000)
        return sStatus;
    sClient = client;
    sLastMs = now;

    struct {
        uint32_t ReturnValue;
    } params{};
    if (!CallNamedFunction(client, "IsLoggedIn", &params)) {
        sStatus = My2KStatus::Unknown;
    } else if (!params.ReturnValue) {
        sStatus = My2KStatus::Offline;
    } else {
        params.ReturnValue = 0;
        if (!CallNamedFunction(client, "IsAccountFull", &params))
            sStatus = My2KStatus::Unknown;
        else
            sStatus = params.ReturnValue ? My2KStatus::Linked : My2KStatus::NotLinked;
    }
    return sStatus;
}

} // namespace

void* FindScriptFunction(const char* className, const char* funcName)
{
    // Same lazy class resolution + retry discipline as CallStaticNative; both caches hold
    // objects that persist for the session (class-owned, never GC'd mid-run).
    static std::unordered_map<std::string, UObject*> sClasses;
    static std::unordered_map<std::string, UFunction*> sFuncs;
    static uint64_t sLastTryMs = 0;

    UObject*& cls = sClasses[className];
    if (!cls) {
        uint64_t now = GetTickCount64();
        if (now - sLastTryMs < 1000)
            return nullptr;
        sLastTryMs = now;
        cls = FindClassRaw(className);
        if (!cls) {
            LOG_WARN("GameUI: class \"%s\" not found (FindScriptFunction)", className);
            return nullptr;
        }
    }

    UFunction*& fn = sFuncs[std::string(className) + "." + funcName];
    if (!fn) {
        fn = FindFunctionInClassStructRaw(cls, funcName);
        if (!fn) {
            LOG_WARN("GameUI: function \"%s.%s\" NOT FOUND", className, funcName);
            return nullptr;
        }
        LOG_INFO("GameUI: resolved \"%s.%s\" at 0x%p", className, funcName, fn);
    }
    return fn;
}

// A UnrealScript `state` is a UStruct in its own right, hanging off the class's field chain, and
// the functions declared inside it hang off ITS chain — one level deeper than FindScriptFunction
// looks. The distinction is not academic: XComTacticalInput.Mouse_CheckForPathing is class-scope
// (which is why the finder above resolves it) while Mouse_CheckForFreeAim is declared once per
// targeting state, so a class-chain walk returns nothing for it at all. Verified live
// 2026-08-20 — at class scope: NOT FOUND; under UsingTargetingMethod: found, flags 0x820102,
// Func = ProcessInternal, i.e. byte-identical to the function the mouse gate already flips.
//
// Note there is no super walk here: states are not inherited into the child's own Children
// chain by name in a way that would help us, and every caller so far wants one exact
// (class, state, function) triple. Cached per triple; class resolution shares FindClassRaw's
// once-a-second retry through the same path as FindScriptFunction.
void* FindStateFunction(const char* className, const char* stateName, const char* funcName)
{
    static std::unordered_map<std::string, UObject*> sClasses;
    static std::unordered_map<std::string, UFunction*> sFuncs;
    static uint64_t sLastTryMs = 0;

    UObject*& cls = sClasses[className];
    if (!cls) {
        uint64_t now = GetTickCount64();
        if (now - sLastTryMs < 1000)
            return nullptr;
        sLastTryMs = now;
        cls = FindClassRaw(className);
        if (!cls) {
            LOG_WARN("GameUI: class \"%s\" not found (FindStateFunction)", className);
            return nullptr;
        }
    }

    std::string key
        = std::string(className) + "." + stateName + "." + funcName;
    UFunction*& fn = sFuncs[key];
    if (!fn) {
        UField* state = FindFieldInStructRaw(cls, stateName, "State");
        if (!state) {
            LOG_WARN("GameUI: state \"%s.%s\" NOT FOUND", className, stateName);
            return nullptr;
        }
        UField* found = FindFieldInStructRaw(state, funcName, "Function");
        if (!found) {
            LOG_WARN("GameUI: function \"%s\" NOT FOUND in state \"%s.%s\"", funcName,
                className, stateName);
            return nullptr;
        }
        fn = reinterpret_cast<UFunction*>(found);
        LOG_INFO("GameUI: resolved \"%s\" at 0x%p", key.c_str(), fn);
    }
    return fn;
}

void* FindMainMenuShell()
{
    static ScreenFinder sFinder{"UIFinalShell"};
    static bool sReported = false;
    return PreferStack("UIFinalShell", sFinder, sReported);
}

bool IsScreenFocused(void* screen)
{
    if (!screen)
        return false;
    return IsFocusedRaw(screen); // see there for why bIsVisible is not part of this
}

uint32_t ScreenStateBits(void* screen)
{
    return screen ? ReadU32Raw(screen, kPanelFlagsOffset) : 0;
}

void HintScreenChange()
{
    gScanEpoch++;
}

// The Engine singleton, through its own `native static final function Engine GetEngine()`. Not
// cached: it is one ProcessEvent on a static native, and the GC rule says a UObject pointer held
// across frames must be revalidated anyway — asking again IS the revalidation.
void* FindEngine()
{
    struct {
        void* ReturnValue;
    } params{};
    if (!CallStaticNative("Engine", "GetEngine", &params))
        return nullptr;
    return params.ReturnValue;
}

bool SubtitlesEnabled()
{
    void* engine = FindEngine();
    if (!engine)
        return true; // unknown — err toward speaking rather than toward silent omission
    return (ReadU32Raw(engine, kEngineFlagsOffset) & kEngineSubtitlesEnabled) != 0;
}

std::string LocalizeString(const char* section, const char* key, const char* package)
{
    if (!section || !key || !package || !key[0])
        return {};

    // Object.Localize(string SectionName, string KeyName, string PackageName) : string — the
    // game's OWN localization lookup, so the answer comes back in the player's language with no
    // table of ours to fall out of date. Layout from the SDK dump comments, PACK(4): three
    // FStrings then the return, 0x10 apart.
#pragma pack(push, 4)
    struct LocalizeParams {
        wchar_t* SectionData; // FString SectionName  @0x00
        int32_t SectionCount;
        int32_t SectionMax;
        wchar_t* KeyData; // FString KeyName          @0x10
        int32_t KeyCount;
        int32_t KeyMax;
        wchar_t* PackageData; // FString PackageName  @0x20
        int32_t PackageCount;
        int32_t PackageMax;
        wchar_t* RetData; // FString ReturnValue      @0x30
        int32_t RetCount;
        int32_t RetMax;
    };
#pragma pack(pop)
    static_assert(sizeof(LocalizeParams) == 0x40, "Object.Localize params must be 0x40 bytes");
    static_assert(offsetof(LocalizeParams, KeyData) == 0x10, "Localize KeyName at 0x10");
    static_assert(offsetof(LocalizeParams, PackageData) == 0x20, "Localize PackageName at 0x20");
    static_assert(offsetof(LocalizeParams, RetData) == 0x30, "Localize ReturnValue at 0x30");

    // Ours to keep alive across the call and nobody else's to free — ProcessEvent shallow-copies
    // the block and its destroy pass skips everything below ParmsSize (see CLAUDE.md).
    std::wstring wSection = Utf8ToWide(section);
    std::wstring wKey = Utf8ToWide(key);
    std::wstring wPackage = Utf8ToWide(package);

    LocalizeParams params{};
    params.SectionData = wSection.data();
    params.SectionCount = static_cast<int32_t>(wSection.size()) + 1; // FString counts the NUL
    params.SectionMax = params.SectionCount;
    params.KeyData = wKey.data();
    params.KeyCount = static_cast<int32_t>(wKey.size()) + 1;
    params.KeyMax = params.KeyCount;
    params.PackageData = wPackage.data();
    params.PackageCount = static_cast<int32_t>(wPackage.size()) + 1;
    params.PackageMax = params.PackageCount;

    if (!CallStaticNative("Object", "Localize", &params))
        return {};

    // ⛔ Sized from the DATA. A localized line is whatever the translator wrote, so this is
    // exactly the class of read the fixed-buffer hard rule is about — the count comes first and
    // the buffer is built to fit.
    const int32_t count = params.RetCount;
    if (!params.RetData || count <= 1)
        return {}; // no such key — the caller decides what that means
    if (count > kMaxPlausibleLocalizedChars) {
        LOG_WARN("GameUI: refusing implausible Localize length %d for %s.%s", count, package, key);
        return {};
    }
    std::vector<wchar_t> buf(static_cast<size_t>(count));
    const int len = CopyFStringRaw(&params.RetData, buf.data(), count);
    if (len <= 0)
        return {};
    // Engine-allocated and deliberately leaked, the same trade ConsoleCommand documents: freeing
    // it needs appFree, and this runs once per subtitle CUE rather than per frame.
    return WcharToUtf8(buf.data(), len);
}

bool MoviePlaying()
{
    // XComEngine.IsAnyMoviePlaying — the same question the game's own rulesets ask before doing
    // anything UI-ish, and the only one that sees a full-screen movie. A movie is NOT a UIScreen:
    // it plays over the stack, so every UI-side flag (input gate, ScreensVisible, cinematic mode)
    // stays exactly as it was and the screen underneath keeps reporting itself inited, visible,
    // focused and topmost. UIShellDifficulty starts this very movie itself via PlayLoadMapMovie.
    struct {
        uint32_t ReturnValue;
    } params{0};
    if (!CallStaticNative("XComEngine", "IsAnyMoviePlaying", &params))
        return false;
    return params.ReturnValue != 0;
}

StackState ReadStackState()
{
    StackState st;
    void* stack = gStackFinder.Find();
    if (!stack)
        return st;
    uint32_t flags = ReadU32Raw(stack, kStackFlagsOffset);
    st.valid = true;
    st.inputBlocked = (flags & kStackInputBlocked) != 0;
    st.screensHidden = (flags & kStackScreensVisible) == 0;
    st.cinematic = (flags & kStackCinematic) != 0;
    return st;
}

std::vector<MenuItem> ReadMainMenu(void* shell)
{
    std::vector<MenuItem> items;
    if (!shell)
        return items;

    RawMenuItem raw[32];
    int count = ReadMainMenuRaw(shell, raw, 32);

    for (int i = 0; i < count; i++) {
        MenuItem item;
        item.handle = raw[i].handle;
        item.label = CleanText(WcharToUtf8(raw[i].label, static_cast<int>(wcslen(raw[i].label))));
        item.focused = raw[i].focused;
        item.disabled = raw[i].disabled;
        item.attention = raw[i].attention;
        if (!item.label.empty())
            items.push_back(std::move(item));
    }
    return items;
}

ShellExtras ReadShellExtras(void* shell)
{
    ShellExtras ex;
    if (!shell)
        return ex;

    void* my2k = ReadPtrRaw(shell, kShellMy2KOffset);
    if (my2k && (ReadU32Raw(my2k, kPanelFlagsOffset) & kPanelVisible)) {
        ex.my2kHandle = my2k;
        ex.my2kStatus = QueryMy2KStatusCached(shell);
    }

    // The ticker holds the MOTD news line; hidden (SetTicker("")) when there is none. Its
    // content lives in HtmlText (SetHtmlText path) — Text stays empty on this screen.
    void* ticker = ReadPtrRaw(shell, kShellTickerOffset);
    if (ticker && (ReadU32Raw(ticker, kPanelFlagsOffset) & kPanelVisible)) {
        ex.tickerText = ReadFStringField(ticker, kScrollHtmlOffset);
        if (ex.tickerText.empty())
            ex.tickerText = ReadFStringField(ticker, kScrollTextOffset);
        if (!ex.tickerText.empty())
            ex.tickerHandle = ticker;
    }
    return ex;
}

bool FocusButton(void* button)
{
    uint8_t params[16] = {}; // no parameters
    return CallNamedFunction(button, "SetSelectedNavigation", params);
}

bool PlayNavSound()
{
    void* mgr = FindSoundManager();
    if (!mgr)
        return false;
    // UE3 FString parameter: [wchar_t* data][count][max], count includes the terminator.
    // The callee only reads the key, and ProcessEvent destroys callee LOCALS only — parms
    // stay caller-owned — so pointing at our static buffer is the standard outbound pattern.
    static const wchar_t kKey[] = L"Play_Mouseover";
    constexpr int32_t kKeyCount = static_cast<int32_t>(sizeof(kKey) / sizeof(wchar_t));
    struct {
        const wchar_t* Data;
        int32_t Num;
        int32_t Max;
    } params{kKey, kKeyCount, kKeyCount};
    return CallNamedFunction(mgr, "PlaySoundEvent", &params);
}

bool ClickButton(void* button)
{
    struct {
        uint32_t ReturnValue;
    } params{};
    return CallNamedFunction(button, "Click", &params);
}

// ---- settings screen (UIOptionsPCScreen) ----

void* FindOptionsScreen()
{
    static ScreenFinder sFinder{"UIOptionsPCScreen"};
    static bool sReported = false;
    return PreferStack("UIOptionsPCScreen", sFinder, sReported);
}

namespace {

OptionWidget WidgetOf(uint8_t type)
{
    switch (type) {
    case kLineSpinner:
    case kLineSpinnerAndButton: return OptionWidget::Spinner;
    case kLineDropdown: return OptionWidget::Dropdown;
    case kLineSlider: return OptionWidget::Slider;
    case kLineCheckbox: return OptionWidget::Checkbox;
    case kLineButton: return OptionWidget::Button;
    case kLineValue:
    case kLineColorChip: return OptionWidget::Value;
    default: return OptionWidget::Description;
    }
}

// One UIMechaListItem as an OptionRow. `index` is the row's position in its owning UIList (the
// SetSelectedIndex target), or -1 for rows parented to a plain panel. Returns false for a row
// that is hidden or has no label — such a row must not become a node.
bool MakeRow(void* item, int index, OptionRow* out)
{
    RawOptionRow raw;
    if (!ReadOptionRowRaw(item, &raw))
        return false;
    out->handle = item;
    out->index = index;
    out->widget = WidgetOf(raw.type);
    out->label = CleanText(WcharToUtf8(raw.label, static_cast<int>(wcslen(raw.label))));
    out->value = CleanText(WcharToUtf8(raw.value, static_cast<int>(wcslen(raw.value))));
    out->percent = raw.percent;
    out->checked = raw.checked;
    out->disabled = raw.disabled;
    out->focused = (ReadU32Raw(item, kPanelFlagsOffset) & kPanelFocused) != 0;
    return !out->label.empty();
}

// The tallest of these lists is an options tab; 64 is well past what any of them draws.
constexpr int kMaxListRows = 64;

// Every visible row of a UIList, indices preserved.
std::vector<OptionRow> ReadListRows(void* list)
{
    std::vector<OptionRow> rows;
    void* items[kMaxListRows] = {};
    int count = TakeListItemsRaw("Options: list rows", list, items, kMaxListRows);
    for (int i = 0; i < count; i++) {
        OptionRow row;
        if (MakeRow(items[i], i, &row))
            rows.push_back(std::move(row));
    }
    return rows;
}

// A visible UIButton at `offset` on `screen` as a MenuItem; handle stays null when the button
// is absent, hidden, or unlabelled.
MenuItem ReadButtonField(void* screen, int32_t offset)
{
    MenuItem item;
    void* btn = ReadPtrRaw(screen, offset);
    if (!btn)
        return item;
    uint32_t flags = ReadU32Raw(btn, kPanelFlagsOffset);
    if (!(flags & kPanelVisible))
        return item;
    item.label = CleanText(ReadFStringField(btn, kButtonTextOffset));
    if (item.label.empty())
        return item;
    item.handle = btn;
    item.focused = (flags & kPanelFocused) != 0;
    item.disabled = (ReadU32Raw(btn, kButtonFlagsOffset) & kButtonDisabled) != 0;
    return item;
}

} // namespace

OptionsView ReadOptions(void* screen)
{
    OptionsView v;
    if (!screen)
        return v;

    v.title = ReadFStringField(screen, kOptsTitleOffset);
    v.currentTab = ReadI32Raw(screen, kOptsCurrentTabOffset);
    for (int i = 0; i < kOptionsTabCount; i++)
        v.tabNames[i] = ReadFStringField(screen, kOptsTabNameOffsets[i]);

    void* rows[kMaxListRows] = {};
    bool clippedRows = false;
    int rowCount = NoteArrayRead("Options: m_arrMechaItems",
        ReadOptionRowPtrsRaw(screen, rows, kMaxListRows, &clippedRows), clippedRows, kMaxListRows);
    for (int i = 0; i < rowCount; i++) {
        OptionRow row;
        if (MakeRow(rows[i], i, &row))
            v.rows.push_back(std::move(row));
    }

    void* list = ReadPtrRaw(screen, kOptsListOffset);
    v.selectedRow = list ? ReadI32Raw(list, kListSelectedIndexOffset) : -1;

    // Bottom buttons in on-screen order; only visible ones become nodes (GPU auto-detect only
    // exists on the Graphics tab, Credits only where the screen allows it).
    constexpr int32_t kButtonOffsets[4] = {kOptsSaveButtonOffset, kOptsExitButtonOffset,
        kOptsCreditsButtonOffset, kOptsGPUButtonOffset};
    for (int32_t offset : kButtonOffsets) {
        MenuItem item = ReadButtonField(screen, offset);
        if (item.handle)
            v.buttons.push_back(std::move(item));
    }
    return v;
}

int GetOptionsTab(void* screen)
{
    return screen ? ReadI32Raw(screen, kOptsCurrentTabOffset) : 0;
}

std::string GetOptionsTabName(void* screen, int tab)
{
    if (!screen || tab < 0 || tab >= kOptionsTabCount)
        return {};
    return ReadFStringField(screen, kOptsTabNameOffsets[tab]);
}

bool SetOptionsTab(void* screen, int tab)
{
    struct {
        int32_t iSelect;
        uint32_t bForce;
    } params{tab, 0};
    return CallNamedFunction(screen, "SetSelectedTab", &params);
}

namespace {

// UIList.SetSelectedIndex — the game's own row selection (fires the list's selection
// delegates, scrolls the view). -1 deselects.
bool SetListIndex(void* list, int index)
{
    if (!list)
        return false;
    struct {
        int32_t Index;
        uint32_t bForce;
        void* ReturnValue; // the function returns self
    } params{index, 0, nullptr};
    return CallNamedFunction(list, "SetSelectedIndex", &params);
}

} // namespace

bool SetOptionsListIndex(void* screen, int index)
{
    return SetListIndex(ReadPtrRaw(screen, kOptsListOffset), index);
}

bool SendRowCommand(void* rowItem, int cmd)
{
    // Direction commands act on the press phase, everything else on release (see the
    // kActionPress/kActionRelease comment).
    bool isDirection = cmd == kCmdArrowLeft || cmd == kCmdArrowRight;
    struct {
        int32_t Cmd;
        int32_t Arg;
        uint32_t ReturnValue;
    } params{cmd, isDirection ? kActionPress : kActionRelease, 0};
    return CallNamedFunction(rowItem, "OnUnrealCommand", &params);
}

namespace {

// Title and body are two spoken phrases, so they take the kernel's join rule rather than a bare
// ". " — the game punctuates plenty of these itself ("НАГРАДА:" is a tooltip title) and a blind
// concatenation voiced the doubled stop at the player.
std::string JoinTooltip(const std::string& title, const std::string& body)
{
    std::string out = title;
    X2Access::Graph::AppendPhrase(out, body);
    return out;
}

// The tooltip manager that owns `panel`'s movie. Never a global scan: two managers can be alive
// at once (2D HUD + 3D strategy movie) with independently-counted ids.
void* TooltipMgrFor(void* panel)
{
    void* movie = panel ? ReadPtrRaw(panel, kPanelMovieOffset) : nullptr;
    void* pres = movie ? ReadPtrRaw(movie, kMoviePresOffset) : nullptr;
    return pres ? ReadPtrRaw(pres, kPresTooltipMgrOffset) : nullptr;
}

} // namespace

std::string ReadPanelTooltip(void* panel)
{
    if (!panel || !(ReadU32Raw(panel, kPanelFlagsOffset) & kPanelHasTooltip))
        return {};
    int32_t id = ReadI32Raw(panel, kPanelTooltipIdOffset);

    void* mgr = TooltipMgrFor(panel);
    if (!mgr)
        return {};

    // SetTooltipText path: a data element in the manager's shared TextTooltip.
    if (const uint8_t* el = FindTextTooltipDataRaw(mgr, id))
        return JoinTooltip(
            ReadFStringField(el, kTTDataTitleOffset), ReadFStringField(el, kTTDataBodyOffset));

    // Preformed path: a real UITextTooltip object in the Tooltips array. Only text tooltips
    // carry sTitle/sBody at these offsets; other subclasses (ability tooltips) would read
    // garbage, so the class check gates the read.
    void* tip = FindTooltipByIdRaw(mgr, id);
    if (!tip || !ObjectClassNamedRaw(tip, "UITextTooltip"))
        return {};

    return JoinTooltip(ReadFStringField(tip, kTextTooltipTitleOffset),
        ReadFStringField(tip, kTextTooltipBodyOffset));
}

std::string ReadRowTooltip(void* rowItem)
{
    if (!rowItem)
        return {};
    // Row tooltips hang off the BG child panel (SetTooltipText targets it — mouse events
    // originate there); fall back to the row panel itself.
    std::string tip = ReadPanelTooltip(ReadPtrRaw(rowItem, kMechaBGOffset));
    if (tip.empty())
        tip = ReadPanelTooltip(rowItem);
    return tip;
}

std::string ReadListItemTooltip(void* rowItem)
{
    if (!rowItem)
        return {};
    // Same shape as ReadRowTooltip but a different child: UIListItemString forwards
    // SetTooltipText — and the why-disabled reason SetDisabled(bDisabled, Reason) parks there —
    // to ButtonBG. The fallback to the row itself covers a subclass that set one on itself.
    std::string tip = ReadPanelTooltip(ReadPtrRaw(rowItem, kListItemBGOffset));
    if (tip.empty())
        tip = ReadPanelTooltip(rowItem);
    return tip;
}

std::string ReadTooltipByPath(void* panel, const char* childPath)
{
    if (!panel || !childPath)
        return {};
    void* mgr = TooltipMgrFor(panel);
    if (!mgr)
        return {};

    // The panel's own path into the movie, which the game composed as ONE FName
    // (`name(ParentPanel.MCPath $ "." $ MCName)`).
    char base[256] = "";
    if (!FNameAtAnsiRaw(reinterpret_cast<const uint8_t*>(panel) + kPanelMCPathOffset, base,
            sizeof(base)))
        return {};
    if (!base[0] || strcmp(base, "None") == 0)
        return {}; // not inited, or a pure UnrealScript container — it has no path to match

    const uint8_t* el = FindTextTooltipDataByPathRaw(mgr, std::string(base) + childPath);
    if (!el)
        return {};
    return JoinTooltip(
        ReadFStringField(el, kTTDataTitleOffset), ReadFStringField(el, kTTDataBodyOffset));
}

bool AdjustDropdown(void* rowItem, int delta)
{
    // The row's own OnUnrealCommand refuses input while disabled; calling into the widget
    // directly must honor the same gate.
    if (ReadU32Raw(rowItem, kMechaFlagsOffset) & kMechaDisabled)
        return false;
    void* dropdown = ReadPtrRaw(rowItem, kMechaDropdownOffset);
    if (!dropdown)
        return false;
    // The widget's own interaction, minus the modal wait: Open() marks the revert point,
    // Prev()/Next() move the highlighted selection (Prev INCREMENTS — the Flash column lists
    // upward), and Enter through the row confirms: Close + SetSelected + the screen's
    // selection-changed delegate, exactly as a sighted keyboard user lands it.
    struct {
        void* ReturnValue; // returns self
    } open{};
    if (!CallNamedFunction(dropdown, "Open", &open))
        return false;
    uint8_t none[16] = {};
    if (!CallNamedFunction(dropdown, delta > 0 ? "Prev" : "Next", none))
        return false;
    return SendRowCommand(rowItem, kCmdEnter);
}

// ---- shared screen lookup ----

namespace {

enum TrackedScreen {
    kScrDifficulty,
    kScrIronMan,
    kScrNarrative,
    kScrSoundtrack,
    kScrDialog,
    kScrPauseMenu,
    kScrLoadScreen,
    kScrTacticalHUD,
    kScrTutorialBox,
    kScrArmoryLoadout,
    kScrSquadSelect,
    kScrAfterAction,
    kScrLoadGame,
    kScrSaveGame,
    kScrTextInput,
    kTrackedScreenCount,
};

const char* const kTrackedClassNames[kTrackedScreenCount] = {
    "UIShellDifficulty",
    "UIChooseIronMan",
    "UIShellNarrativeContent",
    "UIShellSoundtrackPicker",
    "UIDialogueBox",
    "UIPauseMenu",
    "UILoadScreenAnimation",
    "UITacticalHUD",
    "UITutorialBox",
    "UIArmory_Loadout",
    "UISquadSelect",
    "UIAfterAction",
    "UILoadGame",
    "UISaveGame",
    "UIInputDialogue",
};

// The top entry of a dialog box's queue — m_arrData.Length - 1, which is where OnAccept /
// OnCancel and GetTopDialogBoxData look. Null when nothing is queued.
const uint8_t* TopDialogDataRaw(void* screen)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(screen) + kDialogDataOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 64)
            return nullptr;
        return data + static_cast<size_t>(n - 1) * kDlgDataStride;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Is this box actually showing something? The box is PERMANENT — it exists from movie init
// and merely holds no data when idle — so this, not its presence, is the readiness test.
bool DialogIsShowingRaw(void* screen)
{
    // Same reason as IsFocusedRaw: the box's bIsVisible is false while it is on screen asking a
    // question. Its queue is the honest test — the game's own OnAccept/OnCancel read the same
    // top entry, so a box with something queued is a box the player is looking at.
    uint32_t flags = ReadU32Raw(screen, kPanelFlagsOffset);
    if (!(flags & kPanelInited) || (flags & kPanelRemoved))
        return false;
    return TopDialogDataRaw(screen) != nullptr;
}

// What makes a cached instance still worth short-circuiting the rescan for (see ScreenFinder
// for why type-revalidation alone is not enough). Every stacked screen must still be the
// focused one; the dialog box, which never takes stack focus, must still be showing something.
// The loading overlay is a screen nobody focuses — it is shown over whatever is transitioning
// out — so being inited and visible is the whole test.
bool IsShownRaw(void* screen)
{
    uint32_t flags = ReadU32Raw(screen, kPanelFlagsOffset);
    return (flags & kPanelInited) && (flags & kPanelVisible) && !(flags & kPanelRemoved);
}

// Positional and one entry per tracked screen: a short initializer leaves a NULL in the table,
// and FindTrackedScreen calls this through the pointer without a guard, so the static_assert
// below is what keeps "added a screen, forgot its predicate" from being a crash on first scan.
constexpr bool (*kTrackedUsable[kTrackedScreenCount])(void*) = {
    /*Difficulty*/ IsFocusedRaw,
    /*IronMan*/ IsFocusedRaw,
    /*Narrative*/ IsFocusedRaw,
    /*Soundtrack*/ IsFocusedRaw,
    /*Dialog*/ DialogIsShowingRaw,
    /*PauseMenu*/ IsFocusedRaw,
    /*LoadScreen*/ IsShownRaw,
    /*TacticalHUD*/ IsFocusedRaw,
    /*TutorialBox*/ IsFocusedRaw,
    /*ArmoryLoadout*/ IsFocusedRaw,
    /*SquadSelect*/ IsFocusedRaw,
    /*AfterAction*/ IsFocusedRaw,
    /*LoadGame*/ IsFocusedRaw,
    /*SaveGame*/ IsFocusedRaw,
    /*TextInput*/ IsFocusedRaw,
};
static_assert(kTrackedUsable[kTrackedScreenCount - 1] != nullptr,
    "kTrackedUsable must have one predicate per TrackedScreen");

// Every transient screen behind ONE rate-limited GObjects pass. Independent ScreenFinders would
// each scan once a second whenever their screen is absent — which is almost always, since these
// exist only while starting or pausing a campaign. Cached instances still revalidate per call,
// so the scan runs only while a wanted screen is genuinely missing.
void* FindTrackedScreen(TrackedScreen which)
{
    static UObject* sClasses[kTrackedScreenCount] = {};
    static void* sCached[kTrackedScreenCount] = {};
    static uint64_t sLastScanMs = 0;
    static uint64_t sScanEpoch = 0;

    void* cached = sCached[which];
    bool live = cached
        && IsInstanceRaw(
            reinterpret_cast<UObject*>(cached), sClasses[which], kTrackedClassNames[which]);
    if (live && kTrackedUsable[which](cached))
        return cached;
    if (!live)
        sCached[which] = cached = nullptr; // never hand out a GC'd pointer

    uint64_t now = GetTickCount64();
    if (sScanEpoch == gScanEpoch && now - sLastScanMs < 1000)
        return cached;
    sLastScanMs = now;
    sScanEpoch = gScanEpoch;

    // A class we never resolve is a screen we can never find, and that failure is otherwise
    // completely silent — so say it once, the first time each class comes back.
    for (int i = 0; i < kTrackedScreenCount; i++) {
        if (sClasses[i])
            continue;
        sClasses[i] = FindClassRaw(kTrackedClassNames[i]);
        if (sClasses[i])
            LOG_INFO("GameUI: %s class object at 0x%p", kTrackedClassNames[i], sClasses[i]);
    }

    // The scan itself takes the NEWEST instance of each class, unfiltered — that is exactly
    // what replaces a stale one after a world change.
    void* found[kTrackedScreenCount] = {};
    bool complete = ScanInstancesRaw(
        sClasses, kTrackedClassNames, kTrackedUsable, found, kTrackedScreenCount);
    for (int i = 0; i < kTrackedScreenCount; i++) {
        if (found[i] != sCached[i] && found[i])
            LOG_INFO("GameUI: found %s instance at 0x%p", kTrackedClassNames[i], found[i]);
        // A partial walk proves nothing about what it did not reach. Clearing on one would
        // drop live screens for a second at exactly the moment they appear — during a world
        // change, which is when the walk is most likely to be cut short.
        if (found[i] || complete)
            sCached[i] = found[i];
    }
    if (!complete)
        LOG_WARN("GameUI: object scan cut short — keeping the screens it did not reach");
    return sCached[which];
}

// Same stack-first rule as PreferStack, for the screens behind the shared lookup.
void* FindScreen(TrackedScreen which)
{
    static bool sReported[kTrackedScreenCount] = {};
    if (void* onStack = FindStackScreenRaw(kTrackedClassNames[which]))
        return onStack;
    void* found = FindTrackedScreen(which);
    if (found && !sReported[which]) {
        sReported[which] = true;
        LOG_INFO("GameUI: %s is not on the screen stack — falling back to an object scan",
            kTrackedClassNames[which]);
    }
    return found;
}

} // namespace

// ---- new game flow: difficulty (UIShellDifficulty) ----

void* FindDifficultyScreen()
{
    return FindScreen(kScrDifficulty);
}

DifficultyView ReadDifficulty(void* screen)
{
    DifficultyView v;
    if (!screen)
        return v;

    uint32_t flags = ReadU32Raw(screen, kDiffFlagsOffset);
    v.secondWaveOpen = (flags & kDiffSecondWaveOpen) != 0;
    // The screen doubles as the in-game difficulty changer; it says so in its own title.
    v.title = CleanText(ReadFStringField(
        screen, (flags & kDiffIsPlayingGame) ? kDiffChangeTitleOffset : kDiffSelectTitleOffset));

    for (int i = 0; i < 4; i++) {
        OptionRow row;
        if (MakeRow(ReadPtrRaw(screen, kDiffRowOffsets[i]), i, &row))
            v.difficulties.push_back(std::move(row));
    }

    // Bottom-panel rows hang off m_BottomPanel, not a UIList: index -1, so focus write-back
    // takes the SetSelectedNavigation path rather than SetSelectedIndex.
    for (int32_t offset : kDiffOptionOffsets) {
        OptionRow row;
        if (MakeRow(ReadPtrRaw(screen, offset), -1, &row))
            v.options.push_back(std::move(row));
    }

    v.start = ReadButtonField(screen, kDiffStartButtonOffset);

    // While the Second Wave panel is open the game clears the screen navigator and re-enables
    // only that panel, so it is the whole navigable surface — read it only then.
    if (v.secondWaveOpen) {
        v.secondWaveTitle = CleanText(ReadFStringField(screen, kDiffSWPanelTitleOffset));
        void* list = ReadPtrRaw(screen, kDiffSWListOffset);
        v.secondWave = ReadListRows(list);
        v.secondWaveSelected = list ? ReadI32Raw(list, kListSelectedIndexOffset) : -1;
    }
    return v;
}

std::string GetSecondWaveTooltip(void* screen, int index)
{
    if (!screen || index < 0)
        return {};
    wchar_t buf[512];
    int len = CopyFStringElemRaw(screen, kDiffSWToolTipsOffset, index, buf, 512);
    return len > 0 ? CleanText(WcharToUtf8(buf, len)) : std::string();
}

bool SetSecondWaveIndex(void* screen, int index)
{
    return SetListIndex(ReadPtrRaw(screen, kDiffSWListOffset), index);
}

// Toggling a Second Wave option runs the screen's own OnSecondWaveOptionClicked — the delegate
// UIList's mouse path fires, and the only path that also keeps the three faction-HQ options
// mutually exclusive. (This looked broken for a while and was not: the toggle always landed;
// the row's state was being READ wrong. See ReadOptionRowRaw.)
bool ToggleSecondWaveOption(void* screen, int index)
{
    void* list = ReadPtrRaw(screen, kDiffSWListOffset);
    if (!list || index < 0)
        return false;
    struct {
        void* ContainerList;
        int32_t ItemIndex;
        int32_t pad;
    } params{list, index, 0};
    return CallNamedFunction(screen, "OnSecondWaveOptionClicked", &params);
}

// ---- new game flow: Ironman choice (UIChooseIronMan) ----

void* FindIronManScreen()
{
    return FindScreen(kScrIronMan);
}

IronManView ReadIronMan(void* screen)
{
    IronManView v;
    if (!screen)
        return v;
    v.title = CleanText(ReadFStringField(screen, kIronTitleOffset));
    v.body = CleanText(ReadFStringField(screen, kIronBodyOffset));
    // On-screen order: the Ironman toggle sits above the plain launch button.
    for (int32_t offset : {kIronStartButtonOffset, kIronNoIronButtonOffset}) {
        MenuItem item = ReadButtonField(screen, offset);
        if (item.handle)
            v.buttons.push_back(std::move(item));
    }
    return v;
}

// ---- new game flow: optional narrative content (UIShellNarrativeContent) ----

void* FindNarrativeScreen()
{
    return FindScreen(kScrNarrative);
}

NarrativeView ReadNarrative(void* screen)
{
    NarrativeView v;
    if (!screen)
        return v;

    v.title = CleanText(ReadFStringField(screen, kNarrTitleOffset));

    void* xpack = ReadPtrRaw(screen, kNarrXpackItemOffset);
    if (MakeRow(xpack, -1, &v.xpack))
        v.xpackTooltip = ReadRowTooltip(xpack); // SetDisabled parks the why-not text here
    else
        v.xpack = OptionRow{};

    void* list = ReadPtrRaw(screen, kNarrListOffset);
    v.rows = ReadListRows(list);
    v.selectedRow = list ? ReadI32Raw(list, kListSelectedIndexOffset) : -1;

    // The side-panel description the game shows for the focused row: row 0 is the
    // integrated-DLC option, every later row maps to DLCInfos[index - 1].
    void* infos[kMaxDLCInfos] = {};
    int infoCount = TakeArrayRaw("Narrative: DLCInfos", screen, kNarrDLCInfosOffset, infos,
        kMaxDLCInfos);
    std::string integrated = CleanText(ReadFStringField(screen, kNarrIntegratedDescOffset));
    for (const OptionRow& row : v.rows) {
        if (row.index == 0) {
            v.summaries.push_back(integrated);
            continue;
        }
        int i = row.index - 1;
        v.summaries.push_back(i >= 0 && i < infoCount && infos[i]
                ? CleanText(ReadFStringField(infos[i], kDLCNarrativeSummaryOffset))
                : std::string());
    }

    v.start = ReadButtonField(screen, kNarrStartButtonOffset);
    return v;
}

bool SetNarrativeIndex(void* screen, int index)
{
    return SetListIndex(ReadPtrRaw(screen, kNarrListOffset), index);
}

// ---- new game flow: soundtrack picker (UIShellSoundtrackPicker) ----

void* FindSoundtrackScreen()
{
    return FindScreen(kScrSoundtrack);
}

SoundtrackView ReadSoundtrack(void* screen)
{
    SoundtrackView v;
    if (!screen)
        return v;
    void* list = ReadPtrRaw(screen, kSndListOffset);
    v.rows = ReadListRows(list);
    v.selectedRow = list ? ReadI32Raw(list, kListSelectedIndexOffset) : -1;
    v.start = ReadButtonField(screen, kSndStartButtonOffset);
    return v;
}

bool SetSoundtrackIndex(void* screen, int index)
{
    return SetListIndex(ReadPtrRaw(screen, kSndListOffset), index);
}

// ---- loading overlay (UILoadScreenAnimation) ----

void* FindLoadScreen()
{
    return FindScreen(kScrLoadScreen);
}

bool LoadScreenShowing()
{
    void* screen = FindLoadScreen();
    return screen && IsShownRaw(screen);
}

std::string DescribeTrackedScreens()
{
    std::string out;
    for (int i = 0; i < kTrackedScreenCount; i++) {
        void* screen = FindScreen(static_cast<TrackedScreen>(i));
        char line[128];
        snprintf(line, sizeof(line), "%s%s %p/0x%X", out.empty() ? "" : ", ",
            kTrackedClassNames[i], screen, ScreenStateBits(screen));
        out += line;
    }
    return out;
}

std::string DescribeScreenStack()
{
    if (!gStackFinder.Find())
        return "  (no live UIScreenStack)\n";

    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    if (count <= 0)
        return "  (stack is empty)\n";

    std::string out;
    for (int i = 0; i < count; i++) {
        char line[192];
        if (!screens[i]) {
            snprintf(line, sizeof(line), "  %2d (null)\n", i);
            out += line;
            continue;
        }
        char name[64] = "?";
        ObjClassNameAnsiRaw(screens[i], name, sizeof(name));
        uint32_t flags = ReadU32Raw(screens[i], kPanelFlagsOffset);
        // Spelled out rather than left as a hex dword: bIsVisible is the bit that looks like it
        // means "on screen" and does not (a live pause menu reads 0x259, no visible bit), so a
        // dump that only prints 0x259 invites exactly the wrong conclusion.
        snprintf(line, sizeof(line), "  %2d %-28s %p 0x%03X %s%s%s%s\n", i, name, screens[i],
            flags,
            (flags & kPanelInited) ? "inited " : "",
            (flags & kPanelFocused) ? "focused " : "",
            (flags & kPanelVisible) ? "visible " : "",
            (flags & kPanelRemoved) ? "REMOVED " : "");
        out += line;
    }
    return out;
}

// ---- in-game pause menu (UIPauseMenu) ----

void* FindPauseMenu()
{
    return FindScreen(kScrPauseMenu);
}

PauseMenuView ReadPauseMenu(void* screen)
{
    PauseMenuView v;
    if (!screen)
        return v;
    v.title = CleanText(ReadFStringField(screen, kPauseTitleOffset));

    void* list = ReadPtrRaw(screen, kPauseListOffset);
    v.selected = list ? ReadI32Raw(list, kListSelectedIndexOffset) : -1;

    // The rows are UIListItemStrings, not UIMechaListItems, so MakeRow does not apply: a row is
    // just its Text plus the disabled bit DisableListItem sets (save is greyed out in the
    // tutorial, restart outside the missions that allow it).
    void* items[kMaxListRows] = {};
    int count = TakeListItemsRaw("PauseMenu: rows", list, items, kMaxListRows);
    for (int i = 0; i < count; i++) {
        MenuItem item;
        item.handle = items[i];
        item.index = i;
        item.label = CleanText(ReadFStringField(items[i], kStringItemTextOffset));
        if (item.label.empty())
            continue;
        uint32_t flags = ReadU32Raw(items[i], kStringItemFlagsOffset);
        item.disabled = (flags & kStringItemDisabled) != 0;
        item.focused = i == v.selected;
        v.options.push_back(std::move(item));
    }
    return v;
}

bool SetPauseMenuIndex(void* screen, int index)
{
    void* list = ReadPtrRaw(screen, kPauseListOffset);
    if (!list)
        return false;
    // The screen's own SetSelected, not a bare list poke: it also writes m_iCurrentSelection,
    // the copy the screen's OWN Enter/Space handling acts on, and the two must never disagree
    // (the accept-handler-reads-its-own-selection rule). Parameter block is (UIList*, int)
    // padded to the engine's 4-byte pack.
    struct {
        void* ContainerList;
        int32_t ItemIndex;
        int32_t pad;
    } params{list, index, 0};
    return CallNamedFunction(screen, "SetSelected", &params);
}

bool ActivatePauseMenuItem(void* screen, int index)
{
    void* list = ReadPtrRaw(screen, kPauseListOffset);
    if (!list)
        return false;
    // Select, then send Enter to the LIST — never the screen's OnChildClicked by name. The
    // click target the game honours is the list's OnItemClicked/OnItemDoubleClicked delegate,
    // and mods that add pause-menu rows (Remove Missing Mods) install themselves by WRAPPING
    // that delegate and dispatching on the row's Tag; OnChildClicked itself switches on the
    // m_opt* indices and drops a row it did not build into its default-warn branch, which is
    // why Enter on a mod row did nothing. The delegate path runs the wrapper when one is
    // installed and vanilla OnChildClicked (sound, confirmation dialogs) when not.
    SetPauseMenuIndex(screen, index);
    return SendRowCommand(list, kCmdEnter);
}

// ---- modal dialog (UIDialogueBox) ----

void* FindDialogScreen()
{
    void* box = FindScreen(kScrDialog);
    return DialogShowing(box) ? box : nullptr;
}

bool DialogShowing(void* screen)
{
    return screen && DialogIsShowingRaw(screen);
}

DialogView ReadDialog(void* screen)
{
    DialogView v;
    if (!screen)
        return v;
    if (const uint8_t* top = TopDialogDataRaw(screen)) {
        v.title = CleanText(ReadFStringField(top, kDlgDataTitleOffset));
        v.body = CleanText(ReadFStringField(top, kDlgDataTextOffset));
    }
    // The live button labels: Realize() writes the queue entry's labels (or the screen
    // defaults) here, and OnUnrealCommand gates accept/cancel on these being non-empty.
    v.accept = CleanText(ReadFStringField(screen, kDialogAcceptOffset));
    v.cancel = CleanText(ReadFStringField(screen, kDialogCancelOffset));
    return v;
}

namespace {

// OnAccept/OnCancel(optional string strOption) — a zeroed FString is the empty default the
// script declares, and neither body reads it.
bool ResolveDialog(void* screen, const char* funcName)
{
    struct {
        const wchar_t* Data;
        int32_t Num;
        int32_t Max;
        uint32_t ReturnValue;
        uint32_t pad;
    } params{};
    return CallNamedFunction(screen, funcName, &params);
}

} // namespace

bool DialogAccept(void* screen)
{
    return ResolveDialog(screen, "OnAccept");
}

bool DialogCancel(void* screen)
{
    return ResolveDialog(screen, "OnCancel");
}

// ---- save / load (UILoadGame, UISaveGame) ----

void* FindSaveLoadScreen()
{
    // Never both: the save screen is reachable only from the pause menu and the load screen
    // pops it. Asking for the save screen first is arbitrary and safe either way — each finder
    // answers only for a screen that is inited, focused and not removed.
    if (void* saving = FindScreen(kScrSaveGame))
        return saving;
    return FindScreen(kScrLoadGame);
}

bool SaveScreenIsSaving(void* screen)
{
    return screen && ObjectClassNamedRaw(screen, "UISaveGame");
}

SaveLoadView ReadSaveLoad(void* screen)
{
    SaveLoadView v;
    if (!screen)
        return v;
    v.saving = SaveScreenIsSaving(screen);
    v.title = CleanText(ReadFStringField(screen, v.saving ? kSaveTitleOffset : kLoadTitleOffset));
    v.selected = ReadI32Raw(screen, kSaveSelectionOffset);
    // Both screens refuse ALL input while a save or load is in flight, so the layer must not
    // offer one either — and the player deserves to be told why the keys went dead.
    v.busy = v.saving ? ReadU8Raw(screen, kSaveStageOffset) != 0
                      : (ReadU32Raw(screen, kLoadFlagsOffset) & kLoadInProgress) != 0;

    // ⛔ Sized from the array, never from a cap we picked. This is the one list in this file
    // whose length is the PLAYER's — a long campaign runs to hundreds of saves — and the tail a
    // cap would drop is the OLDEST of them (the list is newest-first). Telling a player their
    // early saves do not exist is unrecoverable on the one screen where they would go looking.
    std::vector<void*> rows = ReadPtrArrayAllRaw("SaveLoad: the save list", screen,
        kSaveListItemsOffset);
    for (void* row : rows) {
        if (!row)
            continue;
        SaveSlot slot;
        slot.handle = row;
        // The row's OWN index, not our loop counter: it is what InitSaveLoadItem stamped and
        // what the screen's SetSelection / OnAccept path talks in.
        slot.index = ReadI32Raw(row, kSaveRowIndexOffset);
        slot.id = ReadI32Raw(row, kSaveRowIdOffset);

        // UIButton.Click() refuses an invisible button, so a hidden one is not a verb the row
        // is offering. That is the documented bIsVisible exception: on a child BUTTON the bit is
        // toggled deliberately (the save screen hides Delete on its new-save row).
        MenuItem accept = ReadButtonField(row, kSaveRowAcceptOffset);
        slot.accept = accept.handle;
        slot.acceptLabel = std::move(accept.label);
        slot.acceptDisabled = accept.disabled;

        MenuItem remove = ReadButtonField(row, kSaveRowDeleteOffset);
        slot.remove = remove.handle;
        slot.deleteLabel = std::move(remove.label);

        v.slots.push_back(std::move(slot));
    }
    return v;
}

bool SetSaveLoadSelection(void* screen, int index)
{
    if (!screen)
        return false;
    // The screen's own SetSelection: it moves the Flash highlight AND sets m_iCurrentSelection,
    // which is what the game's own Delete key acts on. Poking the UIList instead would leave
    // the two disagreeing about which save is selected.
    struct {
        int32_t currentSelection;
        int32_t pad;
    } params{index, 0};
    return CallNamedFunction(screen, "SetSelection", &params);
}

namespace {

// Select, then click — the order the game's own mouse path uses (the row's OnMouseIn fires
// SetSelection before the button's click delegate runs).
//
// ⛔ The selection step is NOT redundant, and on the save screen it is load-bearing:
// UISaveGame.OnAccept and OnDelete ignore the control they are handed and act on
// m_iCurrentSelection, so a click that arrived while the two disagreed would OVERWRITE OR
// DELETE THE WRONG SAVE. Focus write-back already keeps them in step; this makes it impossible
// for a stale render to matter. (UILoadGame.OnAccept re-selects from the control itself and
// would be safe either way.)
bool PressSaveRowButton(void* screen, const SaveSlot& slot, void* button)
{
    if (!screen || !button)
        return false;
    SetSaveLoadSelection(screen, slot.index);
    return ClickButton(button);
}

} // namespace

bool ActivateSaveSlot(void* screen, const SaveSlot& slot)
{
    // Through the row's own AcceptButton, exactly as a mouse click does. That button was spawned
    // with the rename delegate on the new-save row and the accept delegate everywhere else, so
    // clicking it dispatches "name a new save" versus "overwrite / load" for free.
    return PressSaveRowButton(screen, slot, slot.accept);
}

bool DeleteSaveSlot(void* screen, const SaveSlot& slot)
{
    return PressSaveRowButton(screen, slot, slot.remove);
}

// ---- text input dialog (UIInputDialogue) ----

void* FindTextInputScreen()
{
    return FindScreen(kScrTextInput);
}

TextInputView ReadTextInput(void* screen)
{
    TextInputView v;
    if (!screen)
        return v;
    v.prompt = CleanText(ReadFStringField(screen, kInputTitleOffset));
    v.prefill = CleanText(ReadFStringField(screen, kInputTextOffset));
    v.maxChars = ReadI32Raw(screen, kInputMaxCharsOffset);
    v.multiLine = ReadU8Raw(screen, kInputTypeOffset) != 0; // eDialogType_SingleLine == 0
    return v;
}

namespace {

// The parameter block EVERY GFxMoviePlayer path function takes: an FString in at 0x00, the
// answer at 0x10 (pinned in sdk_layout_checks.cpp against the generated param structs). One
// shape covers ActionScriptString / ActionScriptInt / GetVariableString / GetVariableNumber,
// which is what makes the whole Scaleform surface reachable through one helper.
//
// The path buffer is OURS and must outlive the call — and only the call: parameters are
// shallow-memcpy'd into the engine's frame and its destroy pass skips everything below
// ParmsSize, so the engine never frees it and never keeps it.
constexpr int32_t kFlashRetOffset = 0x10;

bool FlashCallRaw(void* movie, const char* funcName, const std::string& path, uint8_t* block,
    size_t blockSize)
{
    if (!movie || blockSize < 32)
        return false;
    memset(block, 0, blockSize);
    static std::wstring sPath; // one live buffer at a time; the call is synchronous
    sPath = Utf8ToWide(path);
    const int32_t num = static_cast<int32_t>(sPath.size()) + 1; // FString Num counts the null
    *reinterpret_cast<const wchar_t**>(block) = sPath.c_str();
    *reinterpret_cast<int32_t*>(block + 8) = num;
    *reinterpret_cast<int32_t*>(block + 12) = num;
    return CallNamedFunction(movie, funcName, block);
}

// Invoke or read a path that answers with a string. Empty on any failure — including AS2's own
// "null", which is what Selection.getFocus() says when no field has focus.
std::wstring FlashString(void* movie, const char* funcName, const std::string& path)
{
    uint8_t block[64] = {};
    if (!FlashCallRaw(movie, funcName, path, block, sizeof(block)))
        return std::wstring();
    // Read back through the SEH-guarded FString primitive rather than dereferencing by hand; the
    // buffer is engine-allocated and deliberately leaked (freeing it would need appFree).
    wchar_t buf[512];
    int len = CopyFStringRaw(block + kFlashRetOffset, buf, 512);
    return len > 0 ? std::wstring(buf, static_cast<size_t>(len)) : std::wstring();
}

// ... and one that answers with an int. `fallback` is returned on a failed call; note that a
// path that does not resolve answers 0, while AS2's Selection accessors answer -1 when nothing
// is focused — which is how the two were told apart when this was first probed.
int FlashInt(void* movie, const char* funcName, const std::string& path, int fallback)
{
    uint8_t block[64] = {};
    if (!FlashCallRaw(movie, funcName, path, block, sizeof(block)))
        return fallback;
    return *reinterpret_cast<const int32_t*>(block + kFlashRetOffset);
}

} // namespace

TextFieldState ReadFocusedTextField(void* screen)
{
    TextFieldState s;
    void* movie = screen ? ReadPtrRaw(screen, kPanelMovieOffset) : nullptr;
    if (!movie)
        return s;

    // Which field has keyboard focus, as a full AS path. This is the anchor for everything
    // else, and it is also the answer for a dialog with TWO fields (the crash reporter's title
    // and body), where "the text" alone would be ambiguous.
    const std::wstring focus = FlashString(movie, "ActionScriptString", "Selection.getFocus");
    if (focus.empty() || focus == L"null")
        return s;
    s.focusPath = WcharToUtf8(focus.c_str(), static_cast<int>(focus.size()));

    // RAW: no CleanText. The player typed this, and a save called "<untitled>" must come back
    // the way they typed it.
    s.text = FlashString(movie, "GetVariableString", s.focusPath + ".text");
    s.caret = FlashInt(movie, "ActionScriptInt", "Selection.getCaretIndex", -1);
    s.selBegin = FlashInt(movie, "ActionScriptInt", "Selection.getBeginIndex", -1);
    s.selEnd = FlashInt(movie, "ActionScriptInt", "Selection.getEndIndex", -1);
    s.valid = true;
    return s;
}

// ---- squad select (UISquadSelect) ----

void* FindArmoryLoadout()
{
    return FindScreen(kScrArmoryLoadout);
}

void* FindSquadSelect()
{
    return FindScreen(kScrSquadSelect);
}

namespace {

// A card's utility row, expanded into one control per slot. Each is separately focusable (the
// game's own Left/Right walks them) and separately locked, and the Highlander rebuilds this list
// through CHUIItemSlotEnumerator — so it is READ, never derived from the unit's pockets, which is
// what keeps a slot-adding mod from shifting every index under us (recon §3).
// One list into LoadoutRow structs, in the list's own order.
void ReadLoadoutRows(void* list, std::vector<LoadoutRow>& out)
{
    void* items[kMaxLoadoutRows] = {};
    const int count = TakeListItemsRaw("Loadout: slot rows", list, items, kMaxLoadoutRows);
    for (int i = 0; i < count; i++) {
        LoadoutRow row;
        row.handle = items[i];
        row.dropButton = ReadPtrRaw(items[i], kLoadItemDropButtonOffset);
        row.slotLabel = CleanText(ReadFStringField(items[i], kLoadItemSlotTypeOffset));
        row.title = CleanText(ReadFStringField(items[i], kLoadItemTitleOffset));
        row.subtitle = CleanText(ReadFStringField(items[i], kLoadItemSubtitleOffset));
        row.count = ReadI32Raw(items[i], kLoadItemCountOffset);
        row.equipmentSlot = static_cast<int>(ReadU8Raw(items[i], kLoadItemEquipSlotOffset));
        const uint32_t flags = ReadU32Raw(items[i], kLoadItemFlagsOffset);
        row.infinite = (flags & kLoadItemInfinite) != 0;
        row.disabled = (flags & kLoadItemDisabled) != 0;
        row.canBeCleared = (flags & kLoadItemCanBeCleared) != 0;
        row.loadoutSlot = (flags & kLoadItemLoadoutSlot) != 0;
        out.push_back(std::move(row));
    }
}

void AppendUtilityControls(void* card, std::vector<SquadControl>& out)
{
    void* list = ReadPtrRaw(card, kCardUtilitySlotsOffset);
    void* items[kMaxUtilitySlots] = {};
    const int count = TakeListItemsRaw("SquadSelect: utility slots", list, items,
        kMaxUtilitySlots);
    for (int i = 0; i < count; i++) {
        SquadControl c;
        c.handle = items[i];
        c.button = SquadButton::Utility;
        // The LIST position, which is what SetSelectedIndex and the game's own
        // ShiftHorizontalFocus count in; the item's own SlotIndex is its index WITHIN an
        // inventory slot type and repeats across types, so it is not a cursor.
        c.utilityIndex = i;
        c.utilitySlotType = static_cast<int>(ReadU8Raw(items[i], kUtilSlotTypeOffset));
        // The lock lives on the item's own Button (SetLocked disables it), not on the panel.
        void* button = ReadPtrRaw(items[i], kUtilButtonOffset);
        c.disabled = !button || (ReadU32Raw(button, kButtonFlagsOffset) & kButtonDisabled) != 0;
        // ⛔ SetLocked writes the reason and NOTHING ELSE — not SlotType, not SlotIndex — so a
        // locked slot is the one case the companion cannot name from the widget, and this
        // tooltip is the whole of what the game says about it.
        c.tooltip = ReadPanelTooltip(button);
        out.push_back(c);
    }
}

} // namespace

LoadoutView ReadArmoryLoadout(void* screen)
{
    LoadoutView v;
    if (!screen)
        return v;

    void* equippedList = ReadPtrRaw(screen, kLoadoutEquippedListOffset);
    void* lockerList = ReadPtrRaw(screen, kLoadoutLockerListOffset);
    void* activeList = ReadPtrRaw(screen, kLoadoutActiveListOffset);
    if (!equippedList || !activeList)
        return v; // still building — InitArmory spawns both before PopulateData

    v.active = (activeList == lockerList && lockerList) ? LoadoutList::Locker
                                                        : LoadoutList::Equipped;
    ReadLoadoutRows(activeList, v.rows);
    v.selectedRow = ReadI32Raw(activeList, kListSelectedIndexOffset);
    // The equipped list stays on screen while the locker is open ("openList" only slides it),
    // so it is read as a mirror — and ONLY then, since when it is the active list it is v.rows.
    if (v.active == LoadoutList::Locker)
        ReadLoadoutRows(equippedList, v.equipped);

    v.inventoryTitle = CleanText(ReadFStringField(screen, kLoadoutInventoryTitleOffset));
    v.lockerTitle = CleanText(ReadFStringField(screen, kLoadoutLockerTitleOffset));
    v.makeAvailable = CleanText(ReadFStringField(screen, kLoadoutMakeAvailableOffset));

    const uint32_t flags = ReadU32Raw(screen, kLoadoutFlagsOffset);
    v.gearStripped = (flags & kLoadoutGearStripped) != 0;
    v.itemsStripped = (flags & kLoadoutItemsStripped) != 0;
    v.weaponsStripped = (flags & kLoadoutWeaponsStripped) != 0;
    v.tipStripItems = CleanText(ReadFStringField(screen, kLoadoutTipStripItemsOffset));
    v.tipStripItemsOff = CleanText(ReadFStringField(screen, kLoadoutTipStripItemsOffOffset));
    v.tipStripGear = CleanText(ReadFStringField(screen, kLoadoutTipStripGearOffset));
    v.tipStripGearOff = CleanText(ReadFStringField(screen, kLoadoutTipStripGearOffOffset));
    v.tipStripWeapons = CleanText(ReadFStringField(screen, kLoadoutTipStripWeaponsOffset));
    v.tipStripWeaponsOff = CleanText(ReadFStringField(screen, kLoadoutTipStripWeaponsOffOffset));

    v.valid = true;
    return v;
}

bool SetLoadoutSelection(void* screen, int index)
{
    void* active = screen ? ReadPtrRaw(screen, kLoadoutActiveListOffset) : nullptr;
    if (!active || index < 0)
        return false;
    SetListIndex(active, index);
    return true;
}

bool ActivateLoadoutSelection(void* screen)
{
    if (!screen)
        return false;
    // ⛔ OnAccept() reads ActiveList.SelectedIndex and ignores everything else, so the caller
    // MUST have written the selection first — the same ordering the save/load rows earned, where
    // a click sent while the two disagreed deleted the wrong save.
    uint8_t noParams[16] = {};
    return CallNamedFunction(screen, "OnAccept", noParams);
}

bool ClearLoadoutRow(const LoadoutRow& row)
{
    if (!row.handle)
        return false;
    // The handler's UIButton parameter is dead weight in its body — it works from the row's own
    // ItemRef — but the real button is right there, so the frame is honest either way.
    void* parms[2] = {row.dropButton, nullptr};
    return CallNamedFunction(row.handle, "OnDropItemClicked", parms);
}

SquadSelectView ReadSquadSelect(void* screen)
{
    SquadSelectView v;
    if (!screen)
        return v;

    const uint32_t flags = ReadU32Raw(screen, kSquadFlagsOffset);
    v.walkupDone = (flags & kSquadWalkupDone) != 0;
    v.loadoutDisabled = (flags & kSquadDisableLoadout) != 0;
    v.canCancel = (flags & kSquadNoCancel) == 0;
    v.soldierSlots = ReadI32Raw(screen, kSquadSoldierSlotsOffset);

    v.launch = ReadPtrRaw(screen, kSquadLaunchButtonOffset);
    if (v.launch)
        v.launchDisabled = (ReadU32Raw(v.launch, kButtonFlagsOffset) & kButtonDisabled) != 0;
    // The button prints its two words on two lines; spoken they are one label.
    v.launchTitle = CleanText(ReadFStringField(screen, kSquadLaunchLabelOffset));
    const std::string mission = CleanText(ReadFStringField(screen, kSquadMissionLabelOffset));
    if (!mission.empty())
        v.launchTitle += (v.launchTitle.empty() ? "" : " ") + mission;

    void* list = ReadPtrRaw(screen, kSquadSlotListOffset);
    void* cards[kMaxSquadCards] = {};
    const int count = TakeListItemsRaw("SquadSelect: slot cards", list, cards, kMaxSquadCards);
    for (int i = 0; i < count; i++) {
        SquadSlot slot;
        slot.handle = cards[i];
        slot.slotIndex = ReadI32Raw(cards[i], kCardSlotIndexOffset);

        const uint32_t cardFlags = ReadU32Raw(cards[i], kCardFlagsOffset);
        slot.locked = (cardFlags & kCardDisabled) != 0;
        // bIsFocused, not m_iSelectedSlot: the screen's copy is written from OnReceiveFocus,
        // which RefreshFocus defers behind a 0.25 s timer, and it was caught live disagreeing.
        // This is the same bit the game's own GetActiveListItem() looks at (recon §5).
        slot.focused = (ReadU32Raw(cards[i], kPanelFlagsOffset) & kPanelFocused) != 0;
        slot.activeButton = static_cast<int>(ReadU8Raw(cards[i], kCardActiveButtonOffset));
        slot.bondIcon = ReadPtrRaw(cards[i], kCardBondIconOffset);
        // bHasTooltip on the CARD is set by CreateFlyoverTooltip and only for a tired soldier,
        // so this is empty exactly when the game shows no warning.
        slot.tooltip = ReadPanelTooltip(cards[i]);

        if (!slot.locked) {
            // Declaration order IS the card's top-to-bottom order. Edit and Dismiss are emitted
            // even when disabled — the game greys them and a sighted player still sees them, so
            // skipping (which is what vanilla's keyboard does) would lose parity. Promote, the
            // heavy weapon and the bond icon are NOT emitted here: those widgets are HIDDEN when
            // the soldier has none, and only the unit state knows which — the companion prunes
            // them, since it is already reading the unit for the words.
            SquadControl edit;
            edit.handle = cards[i];
            edit.button = SquadButton::Edit;
            edit.disabled = (cardFlags & kCardDisabledEdit) != 0;
            slot.controls.push_back(edit);

            SquadControl dismiss;
            dismiss.handle = cards[i];
            dismiss.button = SquadButton::Dismiss;
            dismiss.disabled = (cardFlags & kCardDisabledDismiss) != 0;
            slot.controls.push_back(dismiss);

            SquadControl primary;
            primary.handle = cards[i];
            primary.button = SquadButton::WeaponPrime;
            primary.disabled = (cardFlags & kCardDisabledLoadout) != 0;
            slot.controls.push_back(primary);

            AppendUtilityControls(cards[i], slot.controls);
        }
        v.slots.push_back(std::move(slot));
    }

    // Mouse-only on PC, every one of them (recon §4). Labels come from the screen's own localized
    // properties rather than from the nav help, which retains nothing but a delegate array.
    struct VerbDef {
        int32_t offset;
        const char* handler;
    };
    static constexpr VerbDef kVerbs[] = {
        {kSquadStripItemsOffset, "OnStripItems"},
        {kSquadStripWeaponsOffset, "OnStripWeapons"},
        {kSquadStripGearOffset, "OnStripGear"},
        {kSquadBuildItemsOffset, "OnBuildItems"},
        {kSquadBoostSoldierOffset, "OnBoostSoldier"},
    };
    for (const VerbDef& def : kVerbs) {
        SquadVerb verb;
        verb.label = CleanText(ReadFStringField(screen, def.offset));
        if (verb.label.empty())
            continue;
        verb.handler = def.handler;
        v.verbs.push_back(std::move(verb));
    }
    return v;
}

bool SetSquadSelectFocus(void* screen, int visualIndex, const SquadControl& control)
{
    void* list = ReadPtrRaw(screen, kSquadSlotListOffset);
    void* cards[kMaxSquadCards] = {};
    const int count = TakeListItemsRaw("SquadSelect: slot cards", list, cards, kMaxSquadCards);
    if (visualIndex < 0 || visualIndex >= count)
        return false;

    // The order the game's own ShiftFocus uses: drop the old card's focus, take the new one's,
    // then place the button. Doing it the other way round leaves two cards lit in Flash.
    for (int i = 0; i < count; i++) {
        if (i == visualIndex)
            continue;
        if (ReadU32Raw(cards[i], kPanelFlagsOffset) & kPanelFocused) {
            uint8_t none[16] = {};
            CallNamedFunction(cards[i], "OnLoseFocus", none);
        }
    }

    SetListIndex(list, visualIndex);
    uint8_t noParams[16] = {};
    CallNamedFunction(cards[visualIndex], "OnReceiveFocus", noParams);

    if (control.button == SquadButton::Utility) {
        // The utility row is a list of its own; the card's HandleUtilityItemFocus reads its
        // SelectedIndex back, so the row selection has to land before the button does.
        SetListIndex(ReadPtrRaw(cards[visualIndex], kCardUtilitySlotsOffset), control.utilityIndex);
    }

    // SelectUnit is ours, not ESquadSelectButton's (see the enum), so it must not be handed to a
    // function that switches on the game's own values. The card focus above is the whole of what
    // an empty card needs; enforcing that here rather than only documenting it.
    if (control.button == SquadButton::SelectUnit)
        return true;

    struct {
        int32_t ButtonEnumAsInt;
        uint32_t bReverseFocusIfUtility;
    } params{static_cast<int32_t>(control.button), 0};
    return CallNamedFunction(cards[visualIndex], "SetSquadSelectButtonFocus", &params);
}

bool ActivateSquadControl(void* screen, const SquadSlot& slot, const SquadControl& control)
{
    (void)screen;
    if (!slot.handle)
        return false;

    // Deliberately NOT HandleButtonSelect(): it switches on the card's own m_eActiveButton, i.e.
    // on the GAME's idea of which control is focused rather than on ours, so a single frame of
    // disagreement fires the wrong verb — dismiss where the player asked for edit. Every leaf has
    // a direct entry point that takes no arguments (recon §7).
    if (control.button == SquadButton::Utility) {
        return ClickButton(ReadPtrRaw(control.handle, kUtilButtonOffset));
    }

    uint8_t noParams[16] = {};
    switch (control.button) {
    case SquadButton::Promote:
        return CallNamedFunction(slot.handle, "OnClickedPromote", noParams);
    case SquadButton::Edit:
        return CallNamedFunction(slot.handle, "OnClickedEditUnitButton", noParams);
    case SquadButton::Dismiss:
        return CallNamedFunction(slot.handle, "OnClickedDismissButton", noParams);
    case SquadButton::SelectUnit:
        // The empty card's only verb, and the one the game reaches from `selectUnitButtonMC` and
        // from HandleButtonSelect's ListItemIsEmpty() branch. It sets m_iSelectedSlot, snaps the
        // camera and opens UIPersonnel_SquadSelect with the screen's own ChangeSlot/UpdateState
        // delegates — everything the mouse path does.
        return CallNamedFunction(slot.handle, "OnClickedSelectUnitButton", noParams);
    case SquadButton::WeaponPrime:
        return CallNamedFunction(slot.handle, "OnClickedPrimaryWeapon", noParams);
    case SquadButton::WeaponHeavy:
        return CallNamedFunction(slot.handle, "OnClickedHeavyWeapon", noParams);
    case SquadButton::Bond:
        // OnClickBondIcon(UIBondIcon Icon) is the card's own handler and the icon is the one
        // parameter it never reads — its body looks the unit up through GetUnitRef() and branches
        // on bCanHaveBonds. Passing the real icon anyway costs nothing and keeps the frame
        // honest, so the usual "never hand script a pointer it might deref" rule is not even
        // tested here.
        {
            void* parms[2] = {slot.bondIcon, nullptr};
            return CallNamedFunction(slot.handle, "OnClickBondIcon", parms);
        }
    default:
        return false;
    }
}

bool InvokeScreenVerb(void* screen, const char* funcName)
{
    if (!screen || !funcName)
        return false;
    uint8_t noParams[16] = {};
    return CallNamedFunction(screen, funcName, noParams);
}

// ---- after action (UIAfterAction) ----

void* FindAfterAction()
{
    return FindScreen(kScrAfterAction);
}

AfterActionView ReadAfterAction(void* screen)
{
    AfterActionView v;
    if (!screen)
        return v;

    v.hudShown = (ReadU32Raw(screen, kAfterActionFlagsOffset) & kAfterActionHudShown) != 0;

    void* list = ReadPtrRaw(screen, kAfterActionSlotListOffset);
    void* cards[kMaxSquadCards] = {};
    const int count = TakeListItemsRaw("AfterAction: soldier panels", list, cards,
        kMaxSquadCards);
    for (int i = 0; i < count; i++) {
        AfterActionSlot slot;
        slot.handle = cards[i];
        slot.focused = (ReadU32Raw(cards[i], kPanelFlagsOffset) & kPanelFocused) != 0;
        const uint32_t flags = ReadU32Raw(cards[i], kAAItemFlagsOffset);
        slot.canPromote = (flags & kAAItemCanPromote) != 0;
        slot.kia = (flags & kAAItemKIA) != 0;
        slot.promoteButton = ReadPtrRaw(cards[i], kAAItemPromoteButtonOffset);
        slot.bondIcon = ReadPtrRaw(cards[i], kAAItemBondIconOffset);
        v.slots.push_back(slot);
    }
    return v;
}

bool SetAfterActionFocus(void* screen, int visualIndex, AfterActionControl control)
{
    void* list = ReadPtrRaw(screen, kAfterActionSlotListOffset);
    void* cards[kMaxSquadCards] = {};
    const int count = TakeListItemsRaw("AfterAction: soldier panels", list, cards,
        kMaxSquadCards);
    if (visualIndex < 0 || visualIndex >= count)
        return false;

    // Same order the game's own focus path uses: drop the old card, take the new one, then
    // place the inner focus. OnReceiveFocus lands it on the promote button (or the bond icon
    // when there is no promote), so only the disagreeing case needs correcting after.
    for (int i = 0; i < count; i++) {
        if (i == visualIndex)
            continue;
        if (ReadU32Raw(cards[i], kPanelFlagsOffset) & kPanelFocused) {
            uint8_t none[16] = {};
            CallNamedFunction(cards[i], "OnLoseFocus", none);
        }
    }

    SetListIndex(list, visualIndex);
    uint8_t noParams[16] = {};
    CallNamedFunction(cards[visualIndex], "OnReceiveFocus", noParams);

    // Deterministic inner focus — NOT OnUpDownInputHandler, which toggles from whatever state
    // the game is in. Both calls are plain UIPanel focus functions; a control the card lacks
    // (null widget) leaves the card focus as the whole write.
    void* promote = ReadPtrRaw(cards[visualIndex], kAAItemPromoteButtonOffset);
    void* bond = ReadPtrRaw(cards[visualIndex], kAAItemBondIconOffset);
    if (control == AfterActionControl::Bond && bond) {
        if (promote)
            CallNamedFunction(promote, "OnLoseFocus", noParams);
        CallNamedFunction(bond, "OnReceiveFocus", noParams);
    } else if (control != AfterActionControl::Bond && promote) {
        if (bond)
            CallNamedFunction(bond, "OnLoseFocus", noParams);
        CallNamedFunction(promote, "OnReceiveFocus", noParams);
    }
    return true;
}

bool ActivateAfterActionControl(const AfterActionSlot& slot, AfterActionControl control)
{
    if (!slot.handle)
        return false;

    uint8_t noParams[16] = {};
    switch (control) {
    case AfterActionControl::Promote:
        // The zero-param wrapper the card's own Enter path lands on: calls
        // UIAfterAction.OnPromote(UnitReference), which slides the pawns away and pushes
        // UIArmory_Promotion with every side effect the game expects.
        return CallNamedFunction(slot.handle, "AfterActionPromote", noParams);
    case AfterActionControl::Memorial: {
        // The KIA card's verb — the same widget as promote with the delegate swapped. Its
        // UIButton parameter is never read; the real button rides along to keep the frame
        // honest. Opens UIArmory_Photobooth.
        void* parms[2] = {slot.promoteButton, nullptr};
        return CallNamedFunction(slot.handle, "OnClickedMemorialize", parms);
    }
    case AfterActionControl::Bond: {
        // OnClickBondIcon guards on bCanHaveBonds itself and never reads its parameter.
        void* parms[2] = {slot.bondIcon, nullptr};
        return CallNamedFunction(slot.handle, "OnClickBondIcon", parms);
    }
    }
    return false;
}

// ---- tactical HUD (UITacticalHUD) — tile reader support ----

void* FindTacticalHUD()
{
    return FindScreen(kScrTacticalHUD);
}

bool TacticalHUDMenuRaised(void* screen)
{
    return screen && (ReadU32Raw(screen, kTacHudFlagsOffset) & kTacHudMenuRaised) != 0;
}

// ---- the ability bar ----

AbilityBarView ReadAbilityBar(void* screen)
{
    AbilityBarView v;
    if (!screen)
        return v;
    void* container = ReadPtrRaw(screen, kTacHudAbilityHudOffset);
    if (!container)
        return v;

    void* widgets[kAbilityBarMax] = {};
    int held = TakeArrayRaw("ability bar", container, kAbilityBarWidgetsOffset, widgets,
        kAbilityBarMax);

    // ⛔ The live count is the container's own ActiveAbilities, clamped to the widgets actually
    // held — never the widget array's length (a constant 30) and never "which icons hold text"
    // (ClearData leaves the previous soldier's antenna text behind). See game_ui.h.
    int active = ReadI32Raw(container, kAbilityBarActiveOffset);
    if (active < 0)
        active = 0;
    if (active > held)
        active = held;

    v.valid = true;
    v.selected = ReadI32Raw(container, kAbilityBarCurrentOffset);
    v.entries.reserve(static_cast<size_t>(active));
    for (int i = 0; i < active; i++) {
        AbilityBarEntry e;
        e.handle = widgets[i];
        e.name = ReadFStringField(widgets[i], kAbilityIconAntennaOffset);
        e.hotkey = ReadFStringField(widgets[i], kAbilityIconHotkeyOffset);
        e.cooldown = ReadFStringField(widgets[i], kAbilityIconCooldownOffset);
        e.charges = ReadFStringField(widgets[i], kAbilityIconChargeOffset);
        e.available = (ReadU32Raw(widgets[i], kAbilityIconFlagsOffset) & kAbilityIconAvailable) != 0;
        v.entries.push_back(std::move(e));
    }
    return v;
}

bool ActivateBarAbility(void* screen, int index)
{
    if (!screen || index < 0)
        return false;
    void* container = ReadPtrRaw(screen, kTacHudAbilityHudOffset);
    if (!container)
        return false;
    // The game's own mouse path for an icon click. Its parameter block is the int plus the bool
    // return slot, in declaration order — the same hand-rolled layout every outbound call uses.
    struct {
        int32_t Index;
        uint32_t ReturnValue;
    } params{index, 0};
    if (!CallNamedFunction(container, "AbilityClicked", &params))
        return false;
    return params.ReturnValue != 0;
}

// ---- the command buttons (UITacticalHUD_MouseControls) ----

namespace {

// PODs only (SEH): the ButtonItems span. The count is set once at InitMouseControls (Length =
// 10) and nothing ever resizes it, so any other count means the panel — or our offset — is not
// what we think it is, and the read refuses rather than stride into it.
bool MouseControlSpanRaw(const void* controls, const uint8_t** outData)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(controls) + kMouseCtlButtonItemsOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n != kMouseCtlButtonCount)
            return false;
        *outData = data;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The engine's OWN stride over TButtonItems, from the live UArrayProperty. Metadata on the
// class, so resolved once and kept — but only an IMPLAUSIBLE answer is latched as refusal,
// because the plausible band [script size .. one padded pointer over] is where the truth must
// live whichever way the engine rounds. -1 = resolved and refused, 0 = not yet resolved.
int32_t ButtonItemStride(void* controls)
{
    static int32_t sStride = 0;
    if (sStride == 0) {
        int32_t stride = UE3Reflect::ArrayElementSize(
            reinterpret_cast<UObject*>(controls), "ButtonItems");
        if (stride < 0x21 || stride > 0x30) {
            LOG_WARN("GameUI: TButtonItems stride reads 0x%X — implausible, command buttons "
                     "disabled",
                stride);
            sStride = -1;
        } else {
            LOG_INFO("GameUI: TButtonItems stride 0x%X (compiled sizeof 0x24)", stride);
            sStride = stride;
        }
    }
    return sStride;
}

// Is a Chosen activated on this map — the exact gate UpdateControls runs before it shows the
// Chosen Info button, asked of the same static.
bool ChosenActivatedRawCall()
{
    struct {
        void* ReturnValue;
    } params{nullptr};
    if (!CallStaticNative("XComGameState_Unit", "GetActivatedChosen", &params))
        return false;
    return params.ReturnValue != nullptr;
}

} // namespace

MouseControlsView ReadMouseControls(void* screen)
{
    MouseControlsView v;
    if (!screen)
        return v;
    void* controls = ReadPtrRaw(screen, kTacHudMouseControlsOffset);
    if (!controls)
        return v;

    const uint8_t* data = nullptr;
    if (!MouseControlSpanRaw(controls, &data))
        return v;
    int32_t stride = ButtonItemStride(controls);
    if (stride <= 0)
        return v;

    // The visible set, reconstructed from the same three inputs UpdateControls used (the count
    // itself is pushed straight to Flash and retained nowhere). Slots outside this set retain
    // stale labels forever — an unused command slot, a Chosen button after the Chosen left —
    // so membership comes from the inputs, never from "does the slot hold text".
    int indices[kMouseCtlFixedButtons + 2];
    int count = 0;
    for (int i = 0; i < kMouseCtlFixedButtons; i++)
        indices[count++] = i;
    if (ReadI32Raw(controls, kMouseCtlCommandsOffset + 8) > 0)
        indices[count++] = kMouseCtlCommandSlot;
    if (ChosenActivatedRawCall())
        indices[count++] = kMouseCtlChosenSlot;

    v.valid = true;
    v.noKeyBound = ReadFStringField(controls, kMouseCtlNoKeyOffset);
    v.buttons.reserve(static_cast<size_t>(count));
    for (int k = 0; k < count; k++) {
        const uint8_t* el = data + static_cast<size_t>(indices[k]) * stride;
        CommandButtonEntry e;
        e.index = indices[k];
        e.label = ReadFStringField(el, kButtonItemLabelOffset);
        e.key = ReadFStringField(el, kButtonItemKeyOffset);
        // OnAccept's own test is != eUIState_Normal (0), not == eUIState_Disabled — mirror the
        // gate the activation will actually hit so the spoken state can never disagree with it.
        e.enabled = ReadU8Raw(el, kButtonItemStateOffset) == 0;
        v.buttons.push_back(std::move(e));
    }
    return v;
}

bool ActivateMouseControl(void* screen, int index)
{
    if (!screen || index < 0 || index >= kMouseCtlButtonCount)
        return false;
    void* controls = ReadPtrRaw(screen, kTacHudMouseControlsOffset);
    if (!controls)
        return false;
    // Selection FIRST, then the click — OnAccept acts on m_iCurrentSelection and ignores
    // everything else (the screen-accept-trap rule). This is byte-for-byte what the panel's own
    // FXS_L_MOUSE_UP handler does.
    WriteI32Raw(controls, kMouseCtlSelectionOffset, index);
    struct {
        uint32_t ReturnValue;
    } params{0};
    return CallNamedFunction(controls, "OnAccept", &params);
}

// ---- the objectives list (UIObjectiveList) ----

namespace {

// The owning screen differs per layer and nothing else does, so the walk answers both at once.
// Stack order decides: in a mission UITacticalHUD is the one that matters even if an Avenger HUD
// corpse is still allocated, and the stack is the only structure that knows which is live.
void* FindObjectiveListPanel(bool* tacticalOut)
{
    void* stack = gStackFinder.Find();
    if (!stack)
        return nullptr;

    void* screens[kMaxStackScreens] = {};
    int count = ReadPtrArrayRaw(stack, kStackScreensOffset, screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        // Same readiness rule the tactical gate uses, and for the same reason: a removed screen
        // keeps every state bit it had, so bIsRemoved is the only bit that settles it.
        uint32_t flags = ReadU32Raw(screens[i], kPanelFlagsOffset);
        if (!(flags & kPanelInited) || (flags & kPanelRemoved))
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;

        bool tactical = strcmp(name, "UITacticalHUD") == 0;
        if (!tactical && strcmp(name, "UIAvengerHUD") != 0)
            continue;

        void* panel = ReadPtrRaw(screens[i],
            tactical ? kTacHudObjectivesOffset : kAvengerHudObjectivesOffset);
        if (!panel)
            continue;
        if (tacticalOut)
            *tacticalOut = tactical;
        return panel;
    }
    return nullptr;
}

} // namespace

bool HasObjectiveList()
{
    return FindObjectiveListPanel(nullptr) != nullptr;
}

ObjectiveListView ReadObjectives()
{
    ObjectiveListView v;
    bool tactical = false;
    void* panel = FindObjectiveListPanel(&tactical);
    if (!panel)
        return v;
    v.found = true;
    v.tactical = tactical;

    void* items[kMaxObjectiveItems] = {};
    int count = ReadPtrArrayRaw(panel, kObjListItemsOffset, items, kMaxObjectiveItems);
    int hidden = 0;
    for (int i = 0; i < count; i++) {
        if (!items[i])
            continue;

        uint32_t flags = ReadU32Raw(items[i], kPanelFlagsOffset);
        if (flags & kPanelRemoved)
            continue;
        // The documented child-widget exception: on a ROW the visible bit is toggled
        // deliberately — UIObjectiveListItem.ToggleVisibility hides the members of a collapsed
        // group — so an invisible row is one a sighted player cannot read either. Counted rather
        // than swallowed, because a list that silently shrank is the failure mode that reads as
        // "the mission has fewer objectives" and never gets reported as a bug.
        if (!(flags & kPanelVisible)) {
            hidden++;
            continue;
        }

        const int32_t data = kObjItemDataOffset;
        ObjectiveLine line;
        // RAW for MissionType: it is matched against the game's own literals, and CleanText
        // would be free to alter a token we are about to strcmp.
        line.missionType = ReadFStringFieldRaw(items[i], data + kObjInfoMissionType);
        line.title = line.missionType == "TITLE" || line.missionType == "DARKEVENTTITLE";
        line.label = ReadFStringField(items[i], data + kObjInfoDisplayLabel);

        uint32_t f = ReadU32Raw(items[i], data + kObjInfoFlags);
        line.completed = (f & kObjInfoCompleted) != 0;
        line.failed = (f & kObjInfoFailed) != 0;
        line.checkbox = (f & kObjInfoCheckBox) != 0;
        line.warning = (f & kObjInfoWarning) != 0;
        line.header = (f & kObjInfoHeader) != 0;
        line.darkEvent = (f & kObjInfoDarkEvent) != 0;

        line.timer = ReadI32Raw(items[i], data + kObjInfoTimer);
        line.haveAmount = ReadI32Raw(items[i], data + kObjInfoHaveAmount);
        line.haveMin = ReadI32Raw(items[i], data + kObjInfoHaveMin);
        line.availableAmount = ReadI32Raw(items[i], data + kObjInfoAvailAmount);
        line.lostAmount = ReadI32Raw(items[i], data + kObjInfoLostAmount);
        if (line.haveAmount >= 0) {
            line.haveImage = ReadFStringFieldRaw(items[i], data + kObjInfoHaveImage);
            line.availableImage = ReadFStringFieldRaw(items[i], data + kObjInfoAvailImage);
            line.lostImage = ReadFStringFieldRaw(items[i], data + kObjInfoLostImage);
        }

        // The heading words live on the ITEM class, not in Data — the title row pushes
        // m_strObjectivesListTitle straight into Flash. Any row can supply them (they are class
        // defaults), so take them from the first row that answers rather than requiring the
        // title row to be present.
        if (v.heading.empty())
            v.heading = ReadFStringField(items[i], kObjItemListTitleOffset);
        if (v.darkHeading.empty())
            v.darkHeading = ReadFStringField(items[i], kObjItemDarkTitleOffset);
        if (v.inProgress.empty())
            v.inProgress = ReadFStringField(items[i], kObjItemInProgressOffset);

        v.lines.push_back(std::move(line));
    }

    if (hidden > 0)
        LOG_DEBUG("Objectives: %d row(s) hidden by a collapsed group, not spoken", hidden);
    if (count >= kMaxObjectiveItems)
        LOG_WARN("Objectives: list hit the %d-row cap — rows beyond it were NOT read",
            kMaxObjectiveItems);
    return v;
}

// ---- the mission stage counters (UISpecialMissionHUD) ----

namespace {

// One counter, or false when this slot is empty / hidden / wordless.
bool ReadCounterPanel(void* panel, MissionCounter* out)
{
    if (!panel)
        return false;
    uint32_t flags = ReadU32Raw(panel, kPanelFlagsOffset);
    // ⛔ Visible is REQUIRED, not optional: the panel shows itself only when data is pumped in
    // (every AS_Set* ends in Show()), so a hidden counter is one the player is not being shown.
    // The second slot is routinely inited-and-hidden, and reading it would invent a counter.
    if (!(flags & kPanelInited) || (flags & kPanelRemoved) || !(flags & kPanelVisible))
        return false;

    out->label = ReadFStringField(panel, kTurnCounterLabelOffset);
    out->subLabel = ReadFStringField(panel, kTurnCounterSubLabelOffset);
    out->counter = ReadFStringField(panel, kTurnCounterCounterOffset);

    uint32_t f = ReadU32Raw(panel, kTurnCounterFlagsOffset);
    out->locked = (f & kTurnCounterLocked) != 0;
    out->expired = (f & kTurnCounterExpired) != 0;
    out->infinity = (f & kTurnCounterInfinity) != 0;

    // A visible panel with nothing in any of the three fields has no readout to give. Not an
    // error — the MP turn-timer path blanks the sub-label to a single space.
    return !out->label.empty() || !out->subLabel.empty() || !out->counter.empty();
}

} // namespace

std::vector<MissionCounter> ReadMissionCounters()
{
    std::vector<MissionCounter> counters;
    void* stack = gStackFinder.Find();
    if (!stack)
        return counters;

    void* screens[kMaxStackScreens] = {};
    int count = ReadPtrArrayRaw(stack, kStackScreensOffset, screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        uint32_t flags = ReadU32Raw(screens[i], kPanelFlagsOffset);
        if (!(flags & kPanelInited) || (flags & kPanelRemoved))
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UISpecialMissionHUD") != 0)
            continue;

        // Screen order, so the readout matches the column the player sees.
        const int32_t slots[] = {kSpecHudCounter1Offset, kSpecHudCounter2Offset};
        for (int32_t slot : slots) {
            MissionCounter c;
            if (ReadCounterPanel(ReadPtrRaw(screens[i], slot), &c))
                counters.push_back(std::move(c));
        }
        return counters; // only one such HUD is ever up
    }
    return counters;
}

// ---- outbound calls into companion-mod script ----

namespace {

// The __try body lives in its own function with no C++ objects (C2712). One full pass, oldest
// first — a CDO is created once at package load, so there is exactly one and age is moot.
void* ScanForObjectNamedRaw(const char* objName)
{
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        int32_t count = objects ? objects->size() : 0;
        char name[96];
        for (int32_t i = 0; i < count; i++) {
            UObject* obj = objects->at(i);
            if (!obj)
                continue;
            if (ObjNameAnsiRaw(obj, name, sizeof(name)) && strcmp(name, objName) == 0)
                return obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// SEH-isolated revalidation of a cached CDO pointer (name re-read).
bool ObjectStillNamedRaw(void* obj, const char* objName)
{
    char name[96];
    return ObjNameAnsiRaw(reinterpret_cast<UObject*>(obj), name, sizeof(name))
        && strcmp(name, objName) == 0;
}

} // namespace

void* FindCompanionCDO(const char* className)
{
    static std::unordered_map<std::string, void*> sCache;
    static uint64_t sLastScanMs = 0;

    std::string expected = std::string("Default__") + className;

    auto it = sCache.find(expected);
    if (it != sCache.end() && it->second) {
        if (ObjectStillNamedRaw(it->second, expected.c_str()))
            return it->second;
        it->second = nullptr; // never hand out a pointer whose name no longer matches
    }

    uint64_t now = GetTickCount64();
    if (now - sLastScanMs < 1000)
        return nullptr;
    sLastScanMs = now;

    void* found = ScanForObjectNamedRaw(expected.c_str());
    // A missing companion CDO means the script mod is not loaded — every feature riding this
    // call is silently dead, so say it loudly (once a second at worst, and only while used).
    if (found)
        LOG_INFO("GameUI: companion CDO %s at 0x%p", expected.c_str(), found);
    else
        LOG_WARN("GameUI: companion CDO %s NOT FOUND (companion mod not loaded?)",
            expected.c_str());
    sCache[expected] = found;
    return found;
}

bool CallScriptFunction(void* obj, const char* funcName, void* params)
{
    return CallNamedFunction(obj, funcName, params);
}

// ---- tactical activation gate ----

namespace {

// Screens that legitimately sit ABOVE UITacticalHUD during normal play (live stack dump,
// 2026-08-19: UIReplay is the permanent top screen and holds the focus bit for the whole
// mission). Anything not listed blocks the tile cursor — the conservative direction: an
// unknown modal (mission summary, a mod's screen) keeps its arrow keys, and the log names it
// so this list grows from observation, not guesswork.
const char* const kTacticalPassives[] = {
    "UIReplay",
    "UISpecialMissionHUD",
    "UITurnOverlay",
    "UIMouseGuard",
};

bool IsTacticalPassive(const char* name)
{
    for (const char* passive : kTacticalPassives)
        if (strcmp(name, passive) == 0)
            return true;
    return false;
}

} // namespace

TacticalGate ReadTacticalGate()
{
    TacticalGate gate;
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        // A corpse above us is SKIPPED rather than treated as a blocker: it has already been
        // popped and is holding no keys. That is this gate's own policy, deliberately not the
        // one the two GetCurrentScreen gates below use.
        if (!screens[i] || !StackEntryLiveRaw(screens[i]))
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UITacticalHUD") == 0) {
            // Reached the HUD. If nothing above it claimed the surface, this is also the live
            // gate; if something did, we came here only to answer "is a mission running".
            gate.missionActive = true;
            if (!gate.blocked) {
                gate.hudOnStack = true;
                gate.menuRaised
                    = (ReadU32Raw(screens[i], kTacHudFlagsOffset) & kTacHudMenuRaised) != 0;
            }
            return gate;
        }
        if (!gate.blocked && !IsTacticalPassive(name)) {
            // Whatever this is owns the surface — the cursor stands down under it whether or
            // not a HUD sits further below. Keep walking rather than returning: whether a
            // MISSION is running underneath is a different question from whether we may act,
            // and only the entries below this one can answer it. The extra work is a few class
            // name reads on a stack that is a handful of entries deep.
            gate.blocked = true;
            strncpy_s(gate.blocker, name, _TRUNCATE);
        }
    }
    return gate; // no tactical HUD anywhere on the stack: not in a mission
}

AvengerGate ReadAvengerGate()
{
    AvengerGate gate;
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);

    // GetCurrentScreen is the FIRST non-none entry — it applies no inited/removed filter of its
    // own, so neither do we. Filtering here would let us call the grid "current" on a frame the
    // game's own OnUnrealCommand would refuse, which is the one disagreement that matters.
    bool sawTop = false;
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UIFacilityGrid") == 0) {
            gate.gridOnStack = true;
            gate.current = !sawTop;
            if (sawTop)
                break;
            // Still refuse a grid that is on the stack but torn down: the game reaches it
            // through GetCurrentScreen either way, but a removed screen's navigator is not
            // something to narrate.
            if (!StackEntryLiveRaw(screens[i]))
                gate.current = false;
            break;
        }
        // ⛔ ONE screen may sit on top of the grid without blocking it: UIBuildFacilities.
        // Its OnUnrealCommand ends by forwarding every unhandled key to
        // `HQPRES.m_kFacilityGrid.Navigator.OnUnrealCommand — so in build mode the arrows still
        // drive the ant farm, and treating it as a blocker went silent exactly where a player is
        // choosing a room to excavate or build in. This is the game's own delegation, read off
        // its own source, not an exception we invented.
        if (!sawTop && strcmp(name, "UIBuildFacilities") == 0) {
            gate.buildMode = true;
            continue;
        }
        if (!sawTop) {
            sawTop = true;
            strncpy_s(gate.blocker, name, _TRUNCATE);
        }
    }
    return gate;
}

// ---- the Skyranger load screen (UIDropShipBriefing_MissionStart / _MissionEnd) ----
//
// The two variants share a base but not a layout: UIDropShipBriefingBase adds a TArray<int> at
// 0x410, and each subclass then declares its own strings from 0x420, in its own order. So the
// offsets below cannot be folded into one set, and the little table is what keeps the two apart.
//
// ⛔ Read the MOUSE+KEYBOARD prompt, never the console one. Both classes carry a second,
// controller-worded string (m_strConsoleLaunch / m_strConsolePressKeyToContinue) that the game
// substitutes a button GLYPH into via a %A token — speaking it would say "%A LAUNCH MISSION".
// The game picks between them on `ISCONTROLLERACTIVE and so, implicitly, do we by never
// transcribing the console one's offset at all.

constexpr int32_t kDropStartLaunchOffset = 0x460;  // m_strLaunch
constexpr int32_t kDropStartLoadingOffset = 0x470; // m_strLoadingText
constexpr int32_t kDropStartButtonOffset = 0x4B4;  // LaunchButton
constexpr int32_t kDropStartTipOffset = 0x4C4;     // m_strCurrentTip

constexpr int32_t kDropEndButtonOffset = 0x424;  // LaunchButton
constexpr int32_t kDropEndPromptOffset = 0x43C;  // m_strPressKeyToContinue
constexpr int32_t kDropEndLoadingOffset = 0x4DC; // m_strLoadingText

DropshipView ReadDropship()
{
    DropshipView v;

    // Stack lookup, not a ScreenFinder: this screen lives on the TRANSITION map, where the
    // presentation layer is destroyed and rebuilt around it (XComPlayerController:1298). A
    // cached instance is exactly the corpse the hard rules warn about, and the stack — which
    // StackIsLiveRaw re-finds per call — is the one handle that survives the rebuild.
    //
    // ONE walk for both classes rather than FindStackScreenRaw twice: this poll runs for the
    // whole session and finds nothing in almost all of it, and taking whichever variant is
    // TOPMOST is also the more honest answer than preferring one by name.
    void* screens[kMaxStackScreens] = {};
    const int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count && !v.screen; i++) {
        // Skip past anything that is not one of ours — unlike the GetCurrentScreen finders, a
        // screen over the briefing does not make the briefing the wrong answer.
        if (!screens[i] || !StackEntryLiveRaw(screens[i]))
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UIDropShipBriefing_MissionStart") == 0) {
            v.screen = screens[i];
            v.missionStart = true;
        } else if (strcmp(name, "UIDropShipBriefing_MissionEnd") == 0) {
            v.screen = screens[i];
        }
    }
    if (!v.screen)
        return v;

    // ⛔ The VISIBLE bit alone, not IsShownRaw. The game's readiness signal is one statement —
    // `LaunchButton.bIsVisible = true;` in OnCheckLoading — and nothing else on this screen ever
    // writes it, so that bit is the whole answer. Also requiring bIsInited would tie our
    // announcement to the button's Flash init callback, and if that callback never arrived we
    // would simply never say "ready": a silent failure indistinguishable from a slow load, which
    // is precisely the failure mode this feature exists to remove.
    const int32_t buttonOffset = v.missionStart ? kDropStartButtonOffset : kDropEndButtonOffset;
    if (void* button = ReadPtrRaw(v.screen, buttonOffset))
        v.ready = (ReadU32Raw(button, kPanelFlagsOffset) & kPanelVisible) != 0;

    if (v.missionStart) {
        v.loadingText = ReadFStringField(v.screen, kDropStartLoadingOffset);
        v.prompt = ReadFStringField(v.screen, kDropStartLaunchOffset);
        v.tip = ReadFStringField(v.screen, kDropStartTipOffset);
    } else {
        v.loadingText = ReadFStringField(v.screen, kDropEndLoadingOffset);
        v.prompt = ReadFStringField(v.screen, kDropEndPromptOffset);
    }
    return v;
}

// ---- inside a room (UIFacility and its 22 subclasses) ----
//
// The room SCREEN is not the room's content, and that is the whole shape of this reader.
// UIFacility carries a staff strip, optional room-function buttons and an upgrade button — and
// on the seven FIXED rooms it carries none of them: CommandersQuarters, CIC, PowerCore
// (Research), Hangar (Armory), LivingQuarters, BarMemorial and Storage (Engineering) each
// declare ZERO StaffSlotDefs, so their screens are empty and their navigators hold nothing.
// Measured live 2026-08-21 in the Research lab: 3 panels total, staff container hidden.
//
// The content is one screen DOWN. UIFacility.OnUnrealCommand returns false when it has nothing,
// and UIScreenStack.OnInput walks LIFO, so the keys fall through to UIAvengerHUD →
// Shortcuts.OnUnrealCommand → List. Entering a room auto-opens that list for the room's own
// category (SelectCategoryForFacilityScreen → ShowList → List.SetSelectedNavigation), which is
// where the Armory's "Soldiers / Recruit / Memorial" and the Lab's "New Research / View
// Scientists" actually live. Confirmed live 2026-08-21: two Downs moved List.SelectedIndex
// 2 → 0 → 1 with ItemCount 4 (three rows plus the padding row) — Next with wrap, padding skipped.
//
// Everything here is a RETAINED widget read: no XCOMHISTORY, no Flash. FacilityHeader keeps
// LocationField/RoomField, the staff container keeps Title/Icon/Skill, and each row keeps its
// own localized Text. Verified live: RoomField="Исследовательский отсек",
// Text="Показать ученых", Title="УЧЕНЫЕ", Skill="0".

namespace {

constexpr int32_t kAvengerHudShortcutsOffset = 0x43C;      // UIAvengerHUD::Shortcuts
constexpr int32_t kAvengerHudFacilityHeaderOffset = 0x454; // UIAvengerHUD::FacilityHeader
constexpr int32_t kScreenHeaderLocationOffset = 0x3D8;     // UIX2ScreenHeader::LocationField
constexpr int32_t kScreenHeaderRoomOffset = 0x3E8;         // UIX2ScreenHeader::RoomField
constexpr int32_t kShortcutsListOffset = 0x7F8;            // UIAvengerShortcuts::List
constexpr int32_t kShortcutsCategoryLabelsOffset = 0x3F0;  // ::CategoryLabels[5], stride 0x10
constexpr int32_t kShortcutsCurrentCategoryOffset = 0x83C; // ::CurrentCategory
constexpr int32_t kFacilityStaffContainerOffset = 0x608;   // UIFacility::m_kStaffSlotContainer
constexpr int32_t kFacilityRoomContainerOffset = 0x610;    // UIFacility::m_kRoomContainer
constexpr int32_t kFacilityUpgradeButtonOffset = 0x648;    // UIFacility::UpgradeButton
constexpr int32_t kRoomContainerFuncsOffset = 0x3DC;       // UIRoomContainer::RoomFuncs
constexpr int32_t kStaffContainerTitleOffset = 0x3D8;      // UIStaffContainer::Title
constexpr int32_t kStaffContainerSkillOffset = 0x408;      // UIStaffContainer::Skill
constexpr int32_t kStaffContainerSlotsOffset = 0x418;      // UIStaffContainer::StaffSlots
constexpr int32_t kListItemStringTextOffset = 0x3F4;       // UIListItemString::Text
constexpr int32_t kListItemStringFlagsOffset = 0x414;      // bDisabled/bIsBad/bIsGood/bIsWarning
constexpr uint32_t kListItemDisabled = 0x1;
constexpr uint32_t kListItemBad = 0x2;
constexpr uint32_t kListItemGood = 0x4;
constexpr uint32_t kListItemWarning = 0x8;

constexpr int kShortcutCategoryCount = 5;
constexpr int kMaxRoomActions = 16;
// Every shipped facility adds exactly one; the headroom is for a mod that adds more.
constexpr int kMaxRoomFuncs = 8;
// UIStaffContainer::StaffSlots. The shipped rooms top out at four; a staff-slot mod could add.
constexpr int kMaxStaffSlots = 16;

// Is `obj` an instance of `baseClass`, or of anything derived from it? The room screen is one of
// 22 subclasses (UIFacility_Powercore, UIFacility_Armory, …), so an exact class-name compare
// would miss every one of them — the same trap ScanInstanceRaw documents for XCom3DCursor.
// Walking the instance's own class chain costs a dozen name compares and needs no GObjects pass.
bool IsKindOfRaw(void* obj, const char* baseClass)
{
    if (!obj)
        return false;
    __try {
        auto* cls = reinterpret_cast<UStruct*>(reinterpret_cast<UObject*>(obj)->Class);
        char name[64];
        // Bounded like every other SuperField walk here: a UE3 chain is a dozen deep, and a
        // corrupt link must not spin.
        for (int step = 0; cls && step < 32; step++) {
            if (!ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), name, sizeof(name)))
                return false;
            if (strcmp(name, baseClass) == 0)
                return true;
            cls = reinterpret_cast<UStruct*>(cls->SuperField);
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void* FindRoomScreen()
{
    // Topmost non-null entry only — the same GetCurrentScreen definition ReadAvengerGate uses.
    // A UIFacility further down the stack is one the player has navigated AWAY from (the
    // facility summary and the choose-research screen both push over it), and narrating that
    // would name the wrong room.
    return TopStackScreenRaw([](void* screen, const char*) {
        return IsKindOfRaw(screen, "UIFacility");
    });
}

RoomView ReadRoom(void* screen)
{
    RoomView v;
    if (!screen)
        return v;
    v.facility = screen;

    // The header and the shortcut bar both live on UIAvengerHUD, which sits UNDER the room
    // screen and under the grid. That is not a layering accident to route around — it is where
    // the game itself keeps the room's name and the room's menu.
    void* hud = FindStackScreenRaw("UIAvengerHUD");
    if (hud) {
        void* header = ReadPtrRaw(hud, kAvengerHudFacilityHeaderOffset);
        if (header) {
            v.location = ReadFStringField(header, kScreenHeaderLocationOffset);
            v.room = ReadFStringField(header, kScreenHeaderRoomOffset);
        }
        v.shortcuts = ReadPtrRaw(hud, kAvengerHudShortcutsOffset);
    }

    if (v.shortcuts) {
        v.list = ReadPtrRaw(v.shortcuts, kShortcutsListOffset);
        int cat = ReadI32Raw(v.shortcuts, kShortcutsCurrentCategoryOffset);
        // -1 is a room the shortcut bar does not carry (Living Quarters, the Bar). Legitimate,
        // and the reason the empty case names the room rather than reporting a fault.
        if (cat >= 0 && cat < kShortcutCategoryCount)
            v.category
                = ReadFStringField(v.shortcuts, kShortcutsCategoryLabelsOffset + cat * 0x10);
    }

    void* staff = ReadPtrRaw(screen, kFacilityStaffContainerOffset);
    if (staff) {
        uint32_t flags = ReadU32Raw(staff, kPanelFlagsOffset);
        // The one place a child widget's bIsVisible IS the right test (the CLAUDE.md exception
        // for children): UIFacilityStaffContainer.Refresh calls Hide() deliberately when the
        // facility has no slots, and Show() when at least one is visible. On the fixed rooms it
        // is hidden and still carries a title and a crew count, which are worth speaking.
        v.staffVisible = (flags & kPanelVisible) != 0;
        v.staffTitle = ReadFStringField(staff, kStaffContainerTitleOffset);
        v.staffSkill = ReadFStringField(staff, kStaffContainerSkillOffset);
        void* slots[kMaxStaffSlots] = {};
        v.staffSlots = TakeArrayRaw("Room: staff slots", staff, kStaffContainerSlotsOffset, slots,
            kMaxStaffSlots);
    }

    // The room's OWN button bar. Spawned lazily by AddFacilityButton, so a room with no buttons
    // has a null container rather than an empty one — which is the ordinary case, not a fault.
    void* roomContainer = ReadPtrRaw(screen, kFacilityRoomContainerOffset);
    if (roomContainer) {
        void* funcs[kMaxRoomFuncs] = {};
        int funcCount = TakeArrayRaw("Room: facility buttons", roomContainer,
            kRoomContainerFuncsOffset, funcs, kMaxRoomFuncs);
        for (int i = 0; i < funcCount; i++) {
            if (!funcs[i])
                continue;
            RoomFunc f;
            f.handle = funcs[i];
            f.index = i;
            v.funcs.push_back(f);
        }
    }

    // Only while the game is SHOWING it: ShowUpgradeButton spawns the button once and then
    // Hide()s it on every OnLoseFocus, so the pointer outlives the affordance. A child widget's
    // bIsVisible is the documented exception to the never-gate-on-bIsVisible rule, and this is
    // it — the game toggles this one deliberately.
    void* upgrade = ReadPtrRaw(screen, kFacilityUpgradeButtonOffset);
    if (upgrade && (ReadU32Raw(upgrade, kPanelFlagsOffset) & kPanelVisible)) {
        v.upgrade = upgrade;
        v.upgradeLabel = CleanText(ReadFStringField(upgrade, kButtonTextOffset));
        v.upgradeDisabled = (ReadU32Raw(upgrade, kButtonFlagsOffset) & kButtonDisabled) != 0;
    }

    if (v.list) {
        v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);
        void* rows[kMaxRoomActions] = {};
        int rowCount = TakeListItemsRaw("Room: action rows", v.list, rows, kMaxRoomActions);
        for (int i = 0; i < rowCount; i++) {
            if (!rows[i])
                continue;
            RoomAction a;
            a.label = ReadFStringField(rows[i], kListItemStringTextOffset);
            // DoShowList appends a plain UIPanel as bottom padding, with DisableNavigation and
            // no text. Skipping it by its EMPTINESS rather than by its position keeps every
            // remaining index equal to the game's, which is what SetSelectedIndex and the
            // OnItemClicked delegates are expressed in.
            if (a.label.empty())
                continue;
            a.handle = rows[i];
            a.index = i;
            uint32_t rowFlags = ReadU32Raw(rows[i], kListItemStringFlagsOffset);
            a.disabled = (rowFlags & kListItemDisabled) != 0;
            a.urgent = (rowFlags & kListItemBad) != 0;
            a.warning = (rowFlags & kListItemWarning) != 0;
            v.actions.push_back(std::move(a));
        }
    }
    return v;
}

bool SetRoomActionSelection(const RoomView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivateRoomAction(const RoomView& view, const RoomAction& action)
{
    if (!view.list || action.disabled)
        return false;
    // Select, then accept — and the selection step is load-bearing for the same reason it is on
    // the save rows: UIList.OnUnrealCommand fires OnItemClicked(self, SelectedIndex), ignoring
    // whatever row the caller had in mind. A stale render would otherwise open the wrong screen.
    SetListIndex(view.list, action.index);
    return SendRowCommand(view.list, kCmdEnter);
}

bool ActivateRoomFunc(const RoomView& view, int index)
{
    for (const RoomFunc& f : view.funcs) {
        if (f.index != index)
            continue;
        // ⛔ FXS_BUTTON_A, not Enter. UIFacility_RoomFunc.OnUnrealCommand tests that one
        // constant and falls through to UIPanel's native handler for everything else, so the
        // keyboard constants reach nothing here — which is exactly why these buttons are
        // mouse-or-gamepad in vanilla. Sending the game its own controller command runs its own
        // delegate, with every side effect (sound, screen push, camera) the game expects.
        return SendRowCommand(f.handle, kCmdButtonA);
    }
    return false;
}

// ---- an un-excavated room (UIRoom) — assign the engineer, clear the debris ----
//
// A different class from UIFacility and a different problem. A room with no facility gets
// UIRoom: BUILD slots (Room.BuildSlots, not Facility.StaffSlots), a status panel and an
// excavate button. ⛔ BOTH halves of the player's task dead-end on mouse+keyboard in vanilla:
//
//   * ExcavateButton calls DisableNavigation(), so no keyboard can focus it, and the thing Enter
//     and Space DO reach — UIRoom.OnAccept — is an empty function body. StartExcavation is wired
//     only to FXS_BUTTON_X. Clicking is the only way to start clearing a room.
//   * The staff slots move on FXS_DPAD_LEFT/RIGHT; the ARROW constants are not in that switch.
//   * ExcavationPanel calls DisableNavigation() too, so the status, the time estimate and the
//     loot are unreachable text even for a sighted keyboard player.
//
// So the recipe here is not narration over a working screen — it is what makes the screen
// operable. Native reads STRUCTURE (which widgets exist, which are visible, which are refused);
// X2AccessRoomReader supplies every word, because this screen retains almost none of them: the
// status panel is a pure Flash pipe, UIStaffSlot keeps only StaffSlotRef, and the drop-down's
// rows call AS_UpdateData directly, bypassing UIButton.SetText — so even the retained Text is
// empty on them.

namespace {

constexpr int32_t kRoomStaffContainerOffset = 0x608;  // UIRoom::m_kStaffSlotContainer
constexpr int32_t kRoomExcavateButtonOffset = 0x628;  // UIRoom::ExcavateButton
constexpr int32_t kStaffContainerDropDownOffset = 0x428; // UIStaffContainer::m_kPersonnelDropDown
constexpr int32_t kDropDownListOffset = 0x3E0;        // UIPersonnel_DropDown::m_kList
constexpr int kMaxBuildSlots = 8;

} // namespace

void* FindRoomBuildScreen()
{
    // Topmost non-null entry, the GetCurrentScreen rule again — and UIRoom's own OnUnrealCommand
    // is unguarded, so anything above it has already taken the keys.
    return TopStackScreenRaw([](void*, const char* className) {
        return strcmp(className, "UIRoom") == 0;
    });
}

RoomBuildView ReadRoomBuild(void* screen)
{
    RoomBuildView v;
    if (!screen)
        return v;
    v.screen = screen;

    void* staff = ReadPtrRaw(screen, kRoomStaffContainerOffset);
    if (staff) {
        void* slots[kMaxBuildSlots] = {};
        int n = TakeArrayRaw("RoomBuild: staff slots", staff, kStaffContainerSlotsOffset, slots,
            kMaxBuildSlots);
        for (int i = 0; i < n; i++) {
            if (slots[i])
                v.slots.push_back(slots[i]);
        }

        // The picker is a PANEL on the container, not a screen — it never reaches the screen
        // stack, so its visibility is the only thing that says whether it owns the keys. This is
        // the child-widget case where bIsVisible is meaningful: ShowDropDown/CloseMenu toggle it
        // deliberately, and UIStaffContainer.OnUnrealCommand routes input on exactly this test.
        void* drop = ReadPtrRaw(staff, kStaffContainerDropDownOffset);
        if (drop && (ReadU32Raw(drop, kPanelFlagsOffset) & kPanelVisible)) {
            v.dropDown = drop;
            v.dropList = ReadPtrRaw(drop, kDropDownListOffset);
            if (v.dropList)
                v.dropSelected = ReadI32Raw(v.dropList, kListSelectedIndexOffset);
        }
    }

    void* excavate = ReadPtrRaw(screen, kRoomExcavateButtonOffset);
    if (excavate && (ReadU32Raw(excavate, kPanelFlagsOffset) & kPanelVisible)) {
        v.excavate = excavate;
        v.excavateDisabled = (ReadU32Raw(excavate, kButtonFlagsOffset) & kButtonDisabled) != 0;
    }
    return v;
}

bool OpenStaffSlotPicker(void* slotWidget)
{
    if (!slotWidget)
        return false;
    // The widget's own HandleClick, which is what both the mouse path and UIStaffSlot's Enter
    // reach. It branches on IsLocked() to offer the facility upgrade instead of the picker, so
    // calling it rather than ShowDropDown keeps the locked case behaving as the game intends.
    int64_t zeros[2] = {};
    return CallNamedFunction(slotWidget, "HandleClick", zeros);
}

bool SetStaffPickerSelection(const RoomBuildView& view, int index)
{
    return SetListIndex(view.dropList, index);
}

bool ChooseStaffCandidate(const RoomBuildView& view, int index)
{
    if (!view.dropList)
        return false;
    // Same select-then-Enter shape as the room menu, for the same reason: UIList's accept path
    // fires OnItemClicked(self, SelectedIndex) — here UIPersonnel_DropDown.OnStaffSelected,
    // which indexes m_staff by that number — so the selection is the argument.
    SetListIndex(view.dropList, index);
    return SendRowCommand(view.dropList, kCmdEnter);
}

bool StartRoomExcavation(const RoomBuildView& view)
{
    if (!view.excavate || view.excavateDisabled)
        return false;
    // The button's own Click, i.e. the mouse path, which fires the StartExcavation delegate.
    // ⛔ Do NOT reach for UIRoom.OnAccept: it is an empty function. And do not call
    // StartClearRoom on the room state directly either — StartExcavation also closes the screen,
    // and a room left open over a started project is a state the game never produces.
    return ClickButton(view.excavate);
}

// ---- the popup family (docs/popup-inventory.md §1-3) ----
//
// Every modal notification in the game, behind ONE finder. What lets a dozen unrelated classes
// share it is that they agree on the only two things a reader needs: the popup is the topmost
// stack screen, and its OWN OnUnrealCommand answers FXS_KEY_ENTER / FXS_KEY_ESCAPE at screen
// level (checked class by class against SrcOrig). So activation is the screen's own accept path
// and needs no per-class button offsets — which is why this whole family hand-transcribes NO
// offsets and adds NO static_asserts, the same property the hacking screen has.

namespace {

// Ordered most-specific first only for readability; the match is exact on the class name, so
// order does not matter. UIAlert's two DLC subclasses are listed because the finder compares the
// concrete class name, not the base.
constexpr const char* kPopupClasses[] = {
    // Strategy alerts — one class, 118 variants (words come from Flash, see ReadAlertText)
    "UIAlert",
    "UIAlert_DLC_Day60",
    "UIAlert_DLC_Day90",
    // Bespoke strategy popups
    "UIFactionPopup",
    "UIAbilityPopup",
    "UISoldierBondAlert",
    "UIChosenFragmentRecovered",
    "UIChosenLevelUp",
    "UIUFOAttack",
    "UISitRepInformation",
    "UIAdventOperations",
    // Tactical
    "UIChosenReveal",
    "UICombatLose",
    // Both layers
    "UIProgressDialogue",
    "UIRedScreen",
};

} // namespace

bool IsPopupClass(const char* className)
{
    if (!className)
        return false;
    for (const char* known : kPopupClasses) {
        if (strcmp(className, known) == 0)
            return true;
    }
    return false;
}

PopupView FindPopupScreen()
{
    PopupView v;
    // TOPMOST only, and a non-popup on top is a refusal rather than a reason to keep looking:
    // a popup under another screen has already lost the keys, and answering for it would put
    // our stops in front of a screen the player is actually on. Same rule as UIRoom's finder.
    char name[64] = {};
    v.screen = TopStackScreenRaw([](void*, const char* className) { return IsPopupClass(className); },
        name, static_cast<int>(sizeof(name)));
    if (v.screen)
        v.className = name;
    return v;
}

bool SendScreenCommand(void* screen, int cmd)
{
    // A UIScreen's OnUnrealCommand has the row signature, so the row helper is the whole
    // implementation. Kept as its own name because the INTENT differs: for most of this family
    // the screen's own accept path is the only route there is — UIAlert's buttons carry no
    // retained label, no navigable focus and no reachable delegate of their own.
    return SendRowCommand(screen, cmd);
}

// ---- UIAlert's words, read back out of its Flash panel ----
//
// See the header for why this is the one deliberate exception to "verification only, never the
// speech path". The short version: 106 Build*Alert functions, not one retained string, and a
// popup is read once rather than once per keypress.
//
// The field lists come from decompiling the shipped movie — docs/popup-inventory.md §6 has the
// method and the full table. They are in UpdateData's own assignment order, which is the order a
// sighted player reads the panel top to bottom, so it is also the tab-stop order.

namespace {

// How a field carries its text. The decompile says which per field, so a KNOWN symbol never
// guesses; only the unknown-symbol fallback tries more than one shape.
enum class FlashTextShape : uint8_t {
    Getter, // XComScrollingTextField / XComButton — addProperty('text', …), so `<f>.text` runs it
    TheText, // a plain clip wrapping a TextField named theText — `<f>.theText.text`
};

struct AlertField {
    const char* name;
    FlashTextShape shape;
};

struct AlertPanel {
    const char* symbol;
    const AlertField* fields;
    int fieldCount;
};

#define X2A_FIELDS(name, ...)                                                                      \
    const AlertField name[] = {__VA_ARGS__};                                                       \
    constexpr int name##Count = sizeof(name) / sizeof(name[0])

constexpr FlashTextShape kGet = FlashTextShape::Getter;
constexpr FlashTextShape kHdr = FlashTextShape::TheText;

X2A_FIELDS(kAlienSplash, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet});
X2A_FIELDS(kAssignStaff, {"headerMC", kHdr}, {"nameMC", kGet}, {"bonusMC", kGet},
    {"titleMC", kGet}, {"descriptionMC", kGet});
X2A_FIELDS(kBlackMarket, {"logoTitle", kGet}, {"title", kGet}, {"subTitle", kGet},
    {"description", kGet}, {"footer", kGet}, {"leftText", kGet}, {"rightText", kGet});
X2A_FIELDS(kComplete, {"headerTitle", kHdr}, {"headerSubTitle", kGet}, {"alertTitle", kGet},
    {"alertDescription", kGet});
X2A_FIELDS(kContactMade, {"titleMC", kHdr}, {"nameMC", kGet}, {"descriptionMC", kGet},
    {"incomeLabel", kGet}, {"incomeValue", kGet}, {"rewardLabel", kGet}, {"rewardValue", kGet});
X2A_FIELDS(kCouncilReport, {"councilTitle", kHdr});
X2A_FIELDS(kDarkEvent, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet},
    {"quoteMC", kGet}, {"quoteNameMC", kGet});
X2A_FIELDS(kHelp, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet});
// ⚠ WOTC DROPPED `headerField` from this panel and the base-game movie still has it — checked by
// decompiling both. Kept in the list anyway: it costs one empty read under WOTC and is the right
// answer if this ever runs against the base game's movie.
X2A_FIELDS(kItemAvailable, {"headerTitle", kHdr}, {"headerField", kGet}, {"header", kGet},
    {"headTitle", kGet}, {"headName", kGet});

// ---- WOTC-only panels ----
//
// Recovered by decompressing WOTC's own packages with Oodle (see docs/popup-inventory.md §6).
// The four XPACK_* panels and the *Splash family carry no AS2 class of their own, so their fields
// come from the sprite's placed child-instance names rather than from an UpdateData constant
// pool — which is exactly why ReadAlertField always tries both text shapes.
X2A_FIELDS(kBreakthrough, {"breakthroughTitle", kHdr}, {"descriptionTitle", kGet},
    {"descriptionText", kGet});
X2A_FIELDS(kChosenSplash, {"titleMC", kHdr}, {"chosenType", kGet}, {"chosenName", kGet},
    {"chosenNickname", kGet}, {"descriptionMC", kGet});
X2A_FIELDS(kCouncilMissionSplash, {"splashTitle", kHdr}, {"splashSubtitle", kGet},
    {"splashDescription", kGet}, {"splashGreebleText", kGet});
X2A_FIELDS(kGuerrillaOpsSplash, {"GOPSSplashTitle", kHdr}, {"GOPSSplashHeader", kGet},
    {"GOPSSplashDescription", kGet});
X2A_FIELDS(kNegativeSoldierEvent, {"headerMC", kHdr}, {"rankNameMC", kGet}, {"nameMC", kGet},
    {"classNameMC", kGet}, {"abilityLabelMC", kGet}, {"abilityNameMC", kGet},
    {"descriptionMC", kGet});
X2A_FIELDS(kNewStaffSmall, {"NewStaffHeader", kHdr}, {"NewStaffTitle", kGet},
    {"NewStaffName", kGet}, {"NewStaffBonus", kGet});
X2A_FIELDS(kProvingGroundAvail, {"header", kHdr}, {"headTitle", kGet}, {"headName", kGet});
X2A_FIELDS(kRetaliationSplash, {"headerMC", kHdr}, {"subHeaderMC", kGet}, {"descriptionMC", kGet},
    {"footerMC", kGet});
X2A_FIELDS(kSupplyRaidSplash, {"splashHeader", kHdr}, {"splashTitle", kGet},
    {"splashDescription", kGet}, {"splashGreebleText", kGet});
X2A_FIELDS(kSitRep, {"theTitle", kHdr});
X2A_FIELDS(kCovertOpConfirm, {"titleMC", kHdr}, {"confirmHeader", kGet}, {"missionName", kGet},
    {"rewardLabel", kGet}, {"rewardDescription", kGet});
X2A_FIELDS(kCovertOps, {"titleMC", kHdr}, {"descriptionText", kGet});
X2A_FIELDS(kXpackMissionSplash, {"titleMC", kHdr}, {"descriptionText", kGet});
X2A_FIELDS(kRetribution, {"theRegion", kHdr}, {"theDescription", kGet}, {"bottomLeftText", kGet},
    {"bottomRightText", kGet});
X2A_FIELDS(kMakeContact, {"titleMC", kHdr}, {"nameMC", kGet}, {"costLabelMC", kGet},
    {"subLabelMC", kGet}, {"costValueMC", kGet}, {"costDescriptionMC", kGet},
    {"daysLabelMC", kGet}, {"daysValueMC", kGet}, {"descriptionMC", kGet}, {"hintMC", kGet},
    {"rewardLabelMC", kGet}, {"rewardValueMC", kGet});
X2A_FIELDS(kNewObjective, {"headerTitle", kHdr}, {"headerSubTitle", kGet}, {"alertTitle", kGet},
    {"alertDescription", kGet}, {"alertObjectiveText", kGet});
X2A_FIELDS(kNewRegion, {"titleMC", kHdr}, {"nameMC", kGet}, {"descriptionMC", kGet},
    {"hintMC", kGet});
X2A_FIELDS(kNewStaff, {"NewStaffHeader", kHdr}, {"NewStaffName", kGet}, {"NewStaffBonus", kGet},
    {"NewStaffTitle", kGet});
X2A_FIELDS(kOpenStaff, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet},
    {"wideTipMC", kGet});
X2A_FIELDS(kPOI, {"headerTitle", kHdr}, {"headerSubTitle", kGet}, {"alertDescription", kGet},
    {"POILabel01", kGet}, {"POIValue01", kGet}, {"POILabel02", kGet}, {"POIValue02", kGet});
X2A_FIELDS(kSpecialPOI, {"headerTitle", kHdr}, {"headerSubTitle", kGet},
    {"alertDescription", kGet}, {"POILabel01", kGet}, {"POIValue01", kGet});
X2A_FIELDS(kTrainingComplete, {"headerMC", kHdr}, {"rankNameMC", kGet}, {"nameMC", kGet},
    {"classNameMC", kGet}, {"abilityLabelMC", kGet}, {"abilityNameMC", kGet},
    {"descriptionMC", kGet});
X2A_FIELDS(kWarning, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet});
X2A_FIELDS(kXcomGeneric, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet});

// The union, for a symbol the table does not know. It should now be UNREACHABLE for stock
// content: the table above covers all 30 names UIAlert.GetLibraryID() can return. It stays as the
// guard for a MOD that adds an alert of its own, and every miss is logged so a live run names it.
X2A_FIELDS(kUnionFields, {"headerMC", kHdr}, {"headerTitle", kHdr}, {"titleMC", kHdr},
    {"NewStaffHeader", kHdr}, {"councilTitle", kHdr}, {"headerSubTitle", kGet},
    {"headerField", kGet}, {"header", kGet}, {"logoTitle", kGet}, {"alertTitle", kGet},
    {"title", kGet}, {"subTitle", kGet}, {"nameMC", kGet}, {"headTitle", kGet},
    {"headName", kGet}, {"rankNameMC", kGet}, {"classNameMC", kGet}, {"NewStaffName", kGet},
    {"alertDescription", kGet}, {"descriptionMC", kGet}, {"description", kGet},
    {"alertObjectiveText", kGet}, {"abilityLabelMC", kGet}, {"abilityNameMC", kGet},
    {"bonusMC", kGet}, {"NewStaffBonus", kGet}, {"NewStaffTitle", kGet}, {"quoteMC", kGet},
    {"quoteNameMC", kGet}, {"hintMC", kGet}, {"wideTipMC", kGet}, {"footer", kGet},
    {"leftText", kGet}, {"rightText", kGet}, {"incomeLabel", kGet}, {"incomeValue", kGet},
    {"rewardLabel", kGet}, {"rewardValue", kGet}, {"POILabel01", kGet}, {"POIValue01", kGet},
    {"POILabel02", kGet}, {"POIValue02", kGet},
    // …and the WOTC vocabulary, so a modded panel built from an XPACK one still reads.
    {"theTitle", kHdr}, {"theRegion", kHdr}, {"splashTitle", kGet}, {"splashHeader", kGet},
    {"splashSubtitle", kGet}, {"splashDescription", kGet}, {"splashGreebleText", kGet},
    {"descriptionTitle", kGet}, {"descriptionText", kGet}, {"theDescription", kGet},
    {"breakthroughTitle", kGet}, {"chosenType", kGet}, {"chosenName", kGet},
    {"chosenNickname", kGet}, {"subHeaderMC", kGet}, {"footerMC", kGet}, {"missionName", kGet},
    {"confirmHeader", kGet}, {"rewardDescription", kGet}, {"bottomLeftText", kGet},
    {"bottomRightText", kGet});

#undef X2A_FIELDS

#define X2A_PANEL(sym, arr) {sym, arr, arr##Count}
const AlertPanel kAlertPanels[] = {
    X2A_PANEL("Alert_AlienSplash", kAlienSplash),
    X2A_PANEL("Alert_AssignStaff", kAssignStaff),
    X2A_PANEL("Alert_BlackMarket", kBlackMarket),
    X2A_PANEL("Alert_Complete", kComplete),
    X2A_PANEL("Alert_ContactMade", kContactMade),
    X2A_PANEL("Alert_CouncilReport", kCouncilReport),
    X2A_PANEL("Alert_DarkEventActive", kDarkEvent),
    X2A_PANEL("Alert_Help", kHelp),
    X2A_PANEL("Alert_ItemAvailable", kItemAvailable),
    X2A_PANEL("Alert_MakeContact", kMakeContact),
    X2A_PANEL("Alert_NewObjective", kNewObjective),
    X2A_PANEL("Alert_NewRegion", kNewRegion),
    X2A_PANEL("Alert_NewStaff", kNewStaff),
    X2A_PANEL("Alert_OpenStaff", kOpenStaff),
    X2A_PANEL("Alert_POI", kPOI),
    X2A_PANEL("Alert_ProvingGroundAvailable", kItemAvailable), // GetLibraryID aliases these two
    X2A_PANEL("Alert_SpecialPOI", kSpecialPOI),
    X2A_PANEL("Alert_TrainingComplete", kTrainingComplete),
    X2A_PANEL("Alert_Warning", kWarning),
    X2A_PANEL("Alert_XComGeneric", kXcomGeneric),
    X2A_PANEL("Alert_XcomGeneric", kXcomGeneric), // GetLibraryID's spelling vs the AS2 class's
    // WOTC-only. Every name here is one GetLibraryID() can actually return — the full set of 30
    // was read off UIAlert.uc's own switch, so this table and that switch cover each other.
    X2A_PANEL("Alert_Breakthrough", kBreakthrough),
    X2A_PANEL("Alert_ChosenSplash", kChosenSplash),
    X2A_PANEL("Alert_CouncilMissionSplash", kCouncilMissionSplash),
    X2A_PANEL("Alert_GuerrillaOpsSplash", kGuerrillaOpsSplash),
    X2A_PANEL("Alert_NegativeSoldierEvent", kNegativeSoldierEvent),
    X2A_PANEL("Alert_NewStaffSmall", kNewStaffSmall),
    X2A_PANEL("Alert_ProvingGroundAvailable", kProvingGroundAvail),
    X2A_PANEL("Alert_RetaliationSplash", kRetaliationSplash),
    X2A_PANEL("Alert_SupplyRaidSplash", kSupplyRaidSplash),
    X2A_PANEL("Alert_SitRep", kSitRep),
    X2A_PANEL("XPACK_Alert_CovertOpConfirm", kCovertOpConfirm),
    X2A_PANEL("XPACK_Alert_CovertOps", kCovertOps),
    X2A_PANEL("XPACK_Alert_MissionSplash", kXpackMissionSplash),
    X2A_PANEL("XPACK_Alert_Retribution", kRetribution),
};
#undef X2A_PANEL

const AlertPanel* FindAlertPanel(const std::string& symbol)
{
    for (const AlertPanel& panel : kAlertPanels) {
        if (symbol == panel.symbol)
            return &panel;
    }
    return nullptr;
}

// One field's text. `<f>.text` runs XComScrollingTextField's addProperty getter; `<f>.theText`
// is a raw TextField, whose own `.text` is a plain property. Blank is AMBIGUOUS in GFx — an
// unresolved path answers exactly like an empty field — which is fine here: both mean "nothing
// to say about this field", and the union fallback treats a blank as "not this shape".
// ⚠ The shape is a PREFERENCE, not an assertion, and both are always tried. The decompile shows
// which children a panel assigns but not reliably how each one wraps its text, and several
// symbols are recovered from sprite child-names with no AS2 class at all to consult. Guessing
// wrong would silently drop a line — the failure mode a blind player cannot report, because a
// missing line reads exactly like a screen that has nothing more to say. A second read on an
// empty field is cheap when the whole panel is read once, on attach.
std::string ReadAlertField(void* movie, const std::string& mcPath, const AlertField& field)
{
    const std::string base = mcPath + "." + field.name;
    const bool headerFirst = field.shape == FlashTextShape::TheText;
    const char* first = headerFirst ? ".theText.text" : ".text";
    const char* second = headerFirst ? ".text" : ".theText.text";

    std::wstring w = FlashString(movie, "GetVariableString", base + first);
    if (w.empty())
        w = FlashString(movie, "GetVariableString", base + second);
    if (w.empty())
        return std::string();
    return CleanText(WcharToUtf8(w.c_str(), static_cast<int>(w.size())));
}

// ⛔ THE MESSAGE TEXT DOES NOT COME FROM FLASH, and this is the measured correction to the whole
// read-back design (live 2026-08-22, eAlert_LostAndAbandoned / XPACK_Alert_MissionSplash):
//
//   * `ButtonGroup.Button0.text` READS — XComButton keeps `text` as a plain variable.
//   * `titleMC` and `descriptionText` RESOLVE as clips (`titleMC._width` = 665.75) but carry no
//     text at all: no `.text`, `.htmlText`, `._text`, `.textField`, `.theText`, and no `getText`.
//     XComScrollingTextField exposes its text through an addProperty GETTER, which GFx's
//     GetVariableString cannot run, and the underlying storage is not reachable by path either.
//   * `UIMCController.CommandQueue` is not a way round it: EndOp() flushes and clears it, and it
//     reads back as an empty array immediately after the build.
//
// What DOES answer is the screen itself. Every Build*Alert resolves its words out of UIAlert's
// own `localized` vars, and those are retained — 383 of them — under a naming convention:
// `eAlert_LostAndAbandoned` → `m_strLostAndAbandonedTitle` / `...Label` / `...Body`. Measured
// across the whole enum, 81 of 118 alert names follow it. Native can exploit that where script
// cannot, because reading a property BY NAME is exactly what UE3 reflection does and
// UnrealScript has no equivalent.
//
// Verified live against the screenshot: Title = "ВХОДЯЩЕЕ СООБЩЕНИЕ!",
// Body = "Шеф, нам надо выступить посредниками в делах двух фракций Сопротивления."
namespace {

// Reading order, matching how the panel stacks them top to bottom. `Confirm` is deliberately
// absent — it is a BUTTON label and the Flash button read already has it, so including it here
// would say it twice. `Flare` is the map caption, not shown on the popup, and `Image` is a path.
// Counted across UIAlert.uc's own QueueString calls, which is the only honest test of whether a
// suffix ever reaches the screen: Title 48, Header 24, Body 25, Label 20, Desc/Description 3 each,
// Text 0 (declared but never displayed — so it is not read). `Flare` is queued 7 times but only
// as the MAP caption, never on the popup, so it stays out too.
// ⚠ Known over-read: a given alert uses only some of these, and nothing in the naming convention
// says which. eAlert_LostAndAbandoned displays Title and Body but not Label, so its Label is
// spoken though a sighted player never sees it. That is bounded and one-directional — the
// alternative is dropping a line 20 other alerts really do show — but it is the first thing to
// measure on a live pass.
const char* const kAlertTextSuffixes[] = {
    "Title", "Header", "Label", "Body", "Description", "Desc",
};

// UIAlert::LibraryPanel — the panel every Build*Alert pushes its words through, and therefore the
// cache index the Flash capture keys those words by. Pinned in sdk_layout_checks.cpp.
constexpr int32_t kAlertLibraryPanelOffset = 0x1EE8;

// An operand that is an ASSET REFERENCE rather than something to say.
//
// UIAlert pushes image paths down the very same QueueString stream as its prose —
// BuildGOpsAlert queues title, subtitle, body, m_strGOpsImage, confirm, ignore — so the capture
// sees them and speech must not. The test is deliberately NARROW: a path token carries no
// whitespace at all AND either a URL scheme or a `Package.Asset` dot, with nothing outside the
// ASCII path alphabet. A one-word title ("RETALIATION") has no dot and survives; a localized
// title in any non-Latin script fails the alphabet test and survives. Every drop is logged, so a
// live run enumerates what this rejected instead of it going silently missing — the failure mode
// a blind player cannot report, because a dropped line reads exactly like a screen with nothing
// more to say.
bool LooksLikeAssetPath(const std::string& s)
{
    if (s.empty())
        return false;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            return false; // prose
    }
    if (s.find("://") != std::string::npos)
        return true;
    if (s.find('.') == std::string::npos)
        return false;
    for (unsigned char c : s) {
        const bool pathish = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '/' || c == '-';
        if (!pathish)
            return false;
    }
    return true;
}

// The words UIAlert actually handed Scaleform, in the order it handed them over.
//
// ⛔ THIS IS THE SOURCE OF TRUTH for the alert family, and it replaces two weaker readings. The
// localized-var convention below covers only 81 of 118 alert names and cannot tell which of a
// stem's six suffixes the panel actually DISPLAYS, so it over-reads (eAlert_LostAndAbandoned
// shows Title and Body, and the convention also speaks its Label). The Flash path read cannot
// reach an XComScrollingTextField at all. The capture has neither problem: it is exactly the
// argument list of the Update* call that painted this panel, so it covers every variant, in
// display order, with nothing the panel did not receive.
//
// `buttons` are matched out by EXACT STRING. A confirm/ignore label rides in the same argument
// list as the prose, and the button read already speaks it — without this they would be said
// twice.
std::vector<std::string> ReadAlertCaptured(
    void* screen, const std::vector<std::string>& buttons, const std::string& alertName)
{
    std::vector<std::string> lines;
    void* panel = ReadPtrRaw(screen, kAlertLibraryPanelOffset);
    if (!panel)
        return lines;
    const int32_t index = FlashShadow::PanelCacheIndex(panel);
    if (index < 0)
        return lines;

    FlashShadow::Call call;
    if (!FlashShadow::GetTextCall(index, call))
        return lines;

    for (const FlashShadow::Value& arg : call.args) {
        if (arg.type != FlashShadow::ValueType::String || arg.s.empty())
            continue;
        std::string text = CleanText(arg.s);
        if (text.empty())
            continue;
        if (LooksLikeAssetPath(text)) {
            LOG_INFO("Popup: dropped asset-looking operand '%s' from %s.%s",
                text.c_str(), alertName.c_str(), call.name.c_str());
            continue;
        }
        if (std::find(buttons.begin(), buttons.end(), text) != buttons.end())
            continue; // a button label; the button stop already says it
        if (std::find(lines.begin(), lines.end(), text) != lines.end())
            continue; // several panels push the same string into two fields
        lines.push_back(std::move(text));
    }

    if (!lines.empty()) {
        LOG_INFO("Popup: %s read %zu line(s) from the Flash capture (%s, cache index %d)",
            alertName.c_str(), lines.size(), call.name.c_str(), index);
    }
    return lines;
}

std::vector<std::string> ReadAlertLocalized(void* screen, const std::string& alertName)
{
    std::vector<std::string> lines;
    constexpr const char* kPrefix = "eAlert_";
    if (alertName.compare(0, strlen(kPrefix), kPrefix) != 0)
        return lines;
    const std::string stem = alertName.substr(strlen(kPrefix));
    if (stem.empty())
        return lines;

    for (const char* suffix : kAlertTextSuffixes) {
        const std::string prop = "m_str" + stem + suffix;
        std::string value = UE3Reflect::ReadStringProperty(
            reinterpret_cast<UObject*>(screen), prop.c_str());
        if (value.empty())
            continue;
        value = CleanText(value);
        if (value.empty())
            continue;
        // A suffix that repeats an earlier one adds nothing but length — several alerts set
        // Title and Label to the same string.
        if (std::find(lines.begin(), lines.end(), value) == lines.end())
            lines.push_back(std::move(value));
    }
    return lines;
}

} // namespace

// A read is cached on the triple that decides its content. The alert NAME is in the key because
// one library symbol serves many alerts (Alert_Complete covers seven), and the MCPath because a
// second UIAlert gets a fresh panel — so a changed key is an exact, self-invalidating signal
// that the words are different, the same argument the save rows' pointer-vector key makes.
struct AlertTextCache {
    std::string symbol;
    std::string mcPath;
    std::string alertName;
    AlertView view;
    bool valid = false;
};

AlertTextCache gAlertText;

} // namespace

void ResetAlertTextCache()
{
    gAlertText = AlertTextCache();
}

AlertView ReadAlertText(
    void* screen, const std::string& mcPath, const std::string& symbol, const std::string& alertName)
{
    if (gAlertText.valid && gAlertText.symbol == symbol && gAlertText.mcPath == mcPath
        && gAlertText.alertName == alertName)
        return gAlertText.view;

    AlertView v;
    v.screen = screen;
    v.alertName = alertName;
    v.symbol = symbol;
    if (!screen || mcPath.empty())
        return v;

    void* movie = ReadPtrRaw(screen, kPanelMovieOffset);
    if (!movie)
        return v;

    const AlertPanel* panel = FindAlertPanel(symbol);
    v.knownSymbol = panel != nullptr;
    const AlertField* fields = panel ? panel->fields : kUnionFields;
    const int count = panel ? panel->fieldCount : kUnionFieldsCount;

    // ⛔ THE BUTTONS ARE READ FIRST, and that ordering is load-bearing: their labels ride in the
    // same argument list as the prose, so the capture below needs them in hand to match them out.
    //
    // Almost every panel puts them in a ButtonGroup and Flash names them 0 and 1 (while the
    // UnrealScript vars are Button1/Button2). A hidden button answers blank, which is exactly the
    // "no cancel option" case BuildCompleteAlert produces when strCarryOn is empty.
    // ⚠ Alert_Breakthrough is the exception the decompile turned up: it has NO ButtonGroup and
    // names its two buttons outright, so the confirm/cancel pair is looked for at both places.
    // Order matters — the first hit becomes the Enter action and the second the Escape action.
    const char* const kButtonPaths[] = {
        ".ButtonGroup.Button0",
        ".ButtonGroup.Button1",
        ".breakthroughButton",
        ".abandonButton",
    };
    for (const char* path : kButtonPaths) {
        std::wstring w = FlashString(movie, "GetVariableString", mcPath + path + ".text");
        if (w.empty())
            continue;
        std::string label = CleanText(WcharToUtf8(w.c_str(), static_cast<int>(w.size())));
        if (!label.empty())
            v.buttons.push_back(std::move(label));
    }

    // The capture LEADS: it is the argument list of the Update* call that actually painted this
    // panel, so it covers all 118 variants in display order. The two older readings stay as
    // fallbacks, in the order they are trustworthy — the screen's own localized vars (right for
    // the 81 names that follow the stem convention, over-reading by a suffix or two), then the
    // Flash field sweep (right where a field is a plain TextField rather than a scrolling one).
    v.lines = ReadAlertCaptured(screen, v.buttons, alertName);

    if (v.lines.empty()) {
        LOG_INFO("Popup: nothing captured for '%s' — falling back to the localized vars",
            alertName.c_str());
        v.lines = ReadAlertLocalized(screen, alertName);
    }

    if (v.lines.empty()) {
        if (!panel) {
            // An alert name outside the convention AND a symbol the table does not know. Log
            // both so a live run enumerates the gap rather than hiding it behind a sweep.
            LOG_INFO("Popup: no localized text for '%s' and unknown symbol '%s' — field sweep",
                alertName.c_str(), symbol.c_str());
        } else {
            LOG_INFO("Popup: no localized text for '%s' — falling back to Flash on '%s'",
                alertName.c_str(), symbol.c_str());
        }
        for (int i = 0; i < count; i++) {
            std::string text = ReadAlertField(movie, mcPath, fields[i]);
            if (!text.empty())
                v.lines.push_back(std::move(text));
        }
    }

    gAlertText.symbol = symbol;
    gAlertText.mcPath = mcPath;
    gAlertText.alertName = alertName;
    gAlertText.view = v;
    gAlertText.valid = true;
    return v;
}

// ---- the research report (UIResearchReport) — the capture's second consumer ---------------------
//
// See the header for why the capture is the only route in. What is worth keeping HERE is how much
// less this needs than the alert reader above it: no symbol table, no per-panel field map, no
// asset-path heuristic, no button matcher, and no cache. The function name is unique to this
// screen and the argument order is fixed by the .uc, so the read is eight positions and nothing
// else — which is what the capture was supposed to feel like.
//
// No cache, deliberately: the words are already OUR map (the capture's), so re-reading them is a
// hash lookup and eight string copies per render. ReadAlertText caches because its fallbacks reach
// into Flash with GetVariableString, which UIMCController.uc itself calls "immediate and blocking
// … very expensive". Nothing here does.

namespace {

// AS_UpdateResearchReport's parameter order (UIResearchReport.uc:183-195).
//
// ⛔ READ BY POSITION, and that is the point of StringArgs() keeping non-string operands as empty
// entries: index 4 is an asset path that must never be spoken, and a consumer that dropped it
// instead of skipping it would shift the description into the unlocks and the greeble into the
// description. Same rule as the wire format's repeating fields.
enum ResearchReportArg {
    kReportHeader = 0,
    kReportProject = 1,
    kReportCode = 2,
    kReportDate = 3,
    kReportImage = 4, // TechState.GetImage() — never spoken
    kReportUnlocks = 5,
    kReportDescription = 6,
    kReportGreeble = 7,
};

std::string ReportArg(const std::vector<std::string>& args, size_t index)
{
    return index < args.size() ? CleanText(args[index]) : std::string();
}

// Split on newlines FIRST, then clean each line — see the header. Empty lines go: the description's
// paragraph break is "\n\n", which yields one, and a tech with no unlocks yields nothing at all.
std::vector<std::string> ReportArgLines(const std::vector<std::string>& args, size_t index)
{
    std::vector<std::string> lines;
    if (index >= args.size())
        return lines;
    const std::string& raw = args[index];
    size_t start = 0;
    for (;;) {
        const size_t hit = raw.find('\n', start);
        std::string line = CleanText(raw.substr(start, hit == std::string::npos ? hit : hit - start));
        if (!line.empty())
            lines.push_back(std::move(line));
        if (hit == std::string::npos)
            return lines;
        start = hit + 1;
    }
}

} // namespace

void* FindResearchReportScreen()
{
    return FindStackScreenRaw("UIResearchReport");
}

ResearchReportView ReadResearchReport(void* screen)
{
    ResearchReportView v;
    v.screen = screen;
    if (!screen)
        return v;

    // ⛔ EXACT CLASS, not the class chain, because that is what the screen's own test is:
    // UIScreenStack.GetScreen compares `ScreenClass == Screens[i].Class` (UIScreenStack.uc:613),
    // while the IsA matching everyone assumes lives two functions further down in
    // GetFirstInstanceOf. UIShadowChamberArchives extends UIChooseResearch and is NOT a
    // UIResearchArchives — so a report opened from the SHADOW archives closes with Enter, exactly
    // like the one that opens in the Labs, and its nav help says "Continue" too. FindStackScreenRaw
    // compares class-name strings, which reproduces that semantics for free.
    v.fromArchives = FindStackScreenRaw("UIResearchArchives") != nullptr;

    // The screen pushes through its OWN MC (AS_UpdateResearchReport calls MC.BeginFunctionOp
    // directly), unlike UIAlert, which paints a library panel — so the cache index is the screen's.
    const int32_t index = FlashShadow::PanelCacheIndex(screen);
    if (index < 0)
        return v;

    FlashShadow::Call call;
    if (!FlashShadow::GetCall(index, "UpdateResearchReport", call))
        return v;

    const std::vector<std::string> args = call.StringArgs();
    v.captured = true;
    v.header = ReportArg(args, kReportHeader);
    v.project = ReportArg(args, kReportProject);
    v.code = ReportArg(args, kReportCode);
    v.date = ReportArg(args, kReportDate);
    v.greeble = ReportArg(args, kReportGreeble);
    v.unlockLines = ReportArgLines(args, kReportUnlocks);
    v.descriptionLines = ReportArgLines(args, kReportDescription);

    // ⛔ LOG ON CHANGE, NEVER PER READ. This is called from Build() AND ScreenName(), so twice a
    // poll — about twenty times a second — and the first live run wrote the same line 20+ times
    // for one report. A log that floods is a log nobody can read a defect out of, and the ASI
    // keeps only three sessions.
    //
    // The screen pointer is COMPARED and never dereferenced (the WireCache rule), and the project
    // rides in the key beside it so a second report landing on a reused address still logs.
    static void* sLastScreen = nullptr;
    static std::string sLastProject;
    if (screen != sLastScreen || v.project != sLastProject) {
        sLastScreen = screen;
        sLastProject = v.project;
        LOG_INFO("ResearchReport: '%s' from cache index %d — %zu operand(s), %zu unlock line(s), "
                 "%zu paragraph(s)%s",
            v.project.c_str(), index, args.size(), v.unlockLines.size(), v.descriptionLines.size(),
            v.fromArchives ? ", archives (Escape closes)" : " (Enter closes)");
    }
    return v;
}

// ---- the commodity list (UIInventory and its dozen subclasses) ----
//
// The most reused screen shape in the strategy layer, and the one where the widget tree is
// least help: UIItemCard retains not one of its strings and UIInventory_ListItem.PopulateData
// bypasses SetText, so the row a player is looking at reads as empty text. Words come from
// X2AccessCommodityReader; this reads the three things native should own anyway — which rows
// exist, which one is selected, and how the game tinted each one.
//
// ONE OFFSET. UIInventory::List is the only field transcribed here: the tab strip's labels and
// its selected index are localized strings and an int the companion can read directly, the card
// is asked about through the companion too, and every activation goes through a function call
// rather than a poke. That is deliberate — this screen family is the one most likely to be
// rebuilt by a mod, and a recipe that hangs on a single pinned offset survives that.

namespace {

constexpr int32_t kInventoryListOffset = 0x498; // UIInventory::List (UIList*)

// Engineering's build list and the encyclopedia are the long ones. Bounded like every other
// array walk here, and a truncation is LOGGED rather than silently presented as the whole list
// — a cap that quietly drops the tail reads to the player as "the game has nothing more".
constexpr int kMaxCommodityRows = 256;

} // namespace

void* FindCommodityScreen()
{
    // Topmost non-null entry only, matching GetCurrentScreen — which is what the companion
    // resolves too. A UIInventory further down is one the player has pushed past (the black
    // market's confirmation and the build-item dialog both land on top of it), and reading that
    // would describe a screen nobody is on.
    return TopStackScreenRaw([](void* screen, const char*) {
        return IsKindOfRaw(screen, "UIInventory");
    });
}

CommodityView ReadCommodity(void* screen)
{
    CommodityView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.tabbed = IsKindOfRaw(screen, "UIInventory_BuildItems");
    v.list = ReadPtrRaw(screen, kInventoryListOffset);
    if (!v.list)
        return v;

    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    // The cap-is-loud check that used to be hand-rolled here is now what every array read in
    // this file gets: TakeListItemsRaw logs once per site, and only when the list really held
    // MORE than the cap rather than merely as many.
    void* rows[kMaxCommodityRows] = {};
    int rowCount = TakeListItemsRaw("Commodity: item rows", v.list, rows, kMaxCommodityRows);

    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        // ⛔ The list holds things that are not rows. UIInventory_LootRecovered interleaves
        // UIInventory_HeaderListItem section dividers ("ARTIFACTS RECOVERED"), and those are
        // plain UIPanels — they have no bDisabled/bIsBad bitfield at all, so reading one would
        // report a colour the game never drew. Skipping them by CLASS while keeping i as the
        // index is what keeps every remaining number equal to the game's own, which is what
        // SetSelectedIndex is expressed in and what the companion stamps independently.
        if (!IsKindOfRaw(rows[i], "UIInventory_ListItem"))
            continue;
        CommodityRow r;
        r.handle = rows[i];
        r.index = i;
        // The colour channel, and it is the whole affordability story: RealizeBadState sets
        // bIsBad from CanAffordItem and RealizeDisabledState sets bDisabled from
        // MeetsItemReqs/IsItemPurchased, both refreshed on every PopulateData. Reading the bits
        // the game DREW is what keeps this from becoming a second opinion about the same facts.
        uint32_t flags = ReadU32Raw(rows[i], kListItemStringFlagsOffset);
        r.disabled = (flags & kListItemDisabled) != 0;
        r.bad = (flags & kListItemBad) != 0;
        r.good = (flags & kListItemGood) != 0;
        r.warning = (flags & kListItemWarning) != 0;
        v.rows.push_back(r);
    }
    return v;
}

bool SetCommoditySelection(const CommodityView& view, int index)
{
    // More than a highlight: UIInventory sets List.OnSelectionChanged = SelectedItemChanged in
    // BuildScreen, and that delegate is what REPOPULATES THE CARD. Without this the card stop
    // would describe whichever row the game was left on.
    return SetListIndex(view.list, index);
}

bool ActivateCommodityRow(const CommodityView& view, const CommodityRow& row)
{
    if (!view.screen || !view.list)
        return false;

    // Select first, then hand the Enter to the SCREEN — the top of the game's own routing, not
    // the bottom of it. ⛔ Handing it straight to the ROW looks equivalent and is not: the
    // SCREEN's OnUnrealCommand runs before the navigator gets a say, and two things live in
    // that gap. UIInventory_LootRecovered overrides it to CLOSE on Enter — its rows buy
    // nothing, so a row-targeted Enter would leave a blind player stuck on the post-mission
    // loot screen with no continue key. And UISimpleCommodityScreen uses the same window to
    // re-sync iSelectedItem off the list, which is the number its own purchase branch and its
    // nav help read. Everything else falls through UIScreen.OnUnrealCommand → Navigator →
    // ListContainer → List, and UIList answers Enter by calling the SELECTED row's
    // OnUnrealCommand first (:515) — so the row still gets it, by the game's own path.
    //
    // Selecting first is therefore load-bearing again, the same as on the save rows: from here
    // down, the index is the argument.
    //
    // A disabled row is passed through deliberately rather than refused — these screens
    // disagree about what disabled MEANS (UIInventory_Implants greys an implant it will still
    // let you inspect), so the refusal stays theirs, where it can be right.
    SetListIndex(view.list, row.index);
    return SendRowCommand(view.screen, kCmdEnter);
}

bool SwitchCommodityTab(const CommodityView& view, int tab)
{
    if (!view.screen || !view.tabbed)
        return false;
    // The screen's own SwitchTab, which is exactly what its FXS_ARROW_LEFT/RIGHT handler calls.
    // bForce stays false so switching to the tab already showing is the no-op the game makes it.
    struct {
        int32_t Tab;
        uint32_t bForce;
    } params{tab, 0};
    return CallNamedFunction(view.screen, "SwitchTab", &params);
}

// ---- the personnel roster (UIPersonnel and its ten subclasses) ----
//
// The crew list, wherever the game asks "which person?" — the Living Quarters' roster, the Bar's
// obituaries, the Armory's soldiers, choosing a scientist for a project, staffing a facility, a
// covert action's squad. The subclasses change which array is filled and which tabs exist; the
// layout, the row widgets and the input path all belong to the base class, so ONE OFFSET covers
// every door.
//
// ⛔ The rows retain only a UnitRef. UIPersonnel_ListItem extends UIButton, but UpdateData pushes
// the name, status and location through MC.BeginFunctionOp — bypassing SetText, so the retained
// Text is EMPTY on a row visibly showing a soldier. Words come from X2AccessPersonnelReader,
// which re-derives them from that reference. What native reads is what native should: the list,
// its selection, and the disabled bit the game refuses clicks on.
//
// Unlike the debris room this is not a rescue — UIPersonnel.OnUnrealCommand already walks the
// list on Up/Down, switches tabs on Left/Right, accepts on Enter and backs out on Escape for a
// mouse+keyboard player. It is a reader, plus the one verb vanilla leaves on the mouse: the sort
// columns, whose keyboard branches are all controller constants.

namespace {

constexpr int32_t kPersonnelListOffset = 0x45C; // UIPersonnel::m_kList
constexpr int kMaxPersonnelRows = 256;

} // namespace

void* FindPersonnelScreen()
{
    // Topmost non-null entry only, matching GetCurrentScreen — which is also what the companion
    // resolves. A UIPersonnel further down is one something else has been pushed over (an armory
    // screen, a confirmation), and its rows are not what the player is looking at.
    return TopStackScreenRaw([](void* screen, const char*) {
        return IsKindOfRaw(screen, "UIPersonnel");
    });
}

PersonnelView ReadPersonnel(void* screen)
{
    PersonnelView v;
    if (!screen)
        return v;
    v.screen = screen;

    v.list = ReadPtrRaw(screen, kPersonnelListOffset);
    if (!v.list)
        return v;

    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxPersonnelRows] = {};
    int rowCount = TakeListItemsRaw("Personnel: roster rows", v.list, rows, kMaxPersonnelRows);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        PersonnelRow r;
        r.handle = rows[i];
        r.index = i;
        // UIButton::IsDisabled, which is the exact bit every OnXSelected delegate tests before
        // doing anything — so a disabled row here really is one the game will refuse. The armory
        // sets it on soldiers away on covert actions; the tutorial sets it on everyone but the
        // one soldier the script wants.
        r.disabled = (ReadU32Raw(rows[i], kButtonFlagsOffset) & kButtonDisabled) != 0;
        v.rows.push_back(r);
    }
    return v;
}

bool SetPersonnelSelection(const PersonnelView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivatePersonnelRow(const PersonnelView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    // ⛔ Enter goes to the SCREEN, and the selection has to land first. UIPersonnel.OnAccept
    // calls OnButtonCallback, which reads m_kList.selectedIndex and ignores everything else —
    // the same trap UISaveGame.OnAccept sets, so the same guard. Sending Enter to the ROW would
    // reach UIButton's handler instead and never run the tab's selection delegate at all.
    SetListIndex(view.list, index);
    return SendRowCommand(view.screen, kCmdEnter);
}

// ---- choosing what to build in a room (UIChooseFacility) ----
//
// See the header for why this is not part of the commodity family despite looking exactly like
// one. Native reads the half that IS retained — the row names and their two state bits — and the
// companion re-derives the panel, which retains nothing at all.

namespace {

constexpr int32_t kChooseFacilityListOffset = 0x418; // UIChooseFacility::m_List
constexpr int32_t kListItemAttentionOffset = 0x440;  // UIListItemString::AttentionIcon
constexpr int kMaxFacilityChoices = 64;

} // namespace

void* FindChooseFacilityScreen()
{
    // Stack first, like every other screen-class finder. UIChooseFacility is PUSHED by the build
    // flow, so the stack has the answer without a GObjects pass — and the pass is the cost that
    // was actually being paid, once a second for the whole session in which this screen is not
    // up. The fallback's one-line report is the other half: it is how "this class is not
    // stack-managed" becomes a fact in the log rather than something we assumed.
    static ScreenFinder sFinder{"UIChooseFacility"};
    static bool sReported = false;
    void* screen = PreferStack("UIChooseFacility", sFinder, sReported);
    // PreferStack's two halves do not filter alike: the stack half takes inited/not-removed,
    // the scan half takes ScreenFinder's default IsFocusedRaw. The header promises the CURRENT
    // screen, so the focus test belongs here rather than in whichever half answered.
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

ChooseFacilityView ReadChooseFacility(void* screen)
{
    ChooseFacilityView v;
    if (!screen)
        return v;
    v.screen = screen;

    v.list = ReadPtrRaw(screen, kChooseFacilityListOffset);
    if (!v.list)
        return v;

    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxFacilityChoices] = {};
    int rowCount = TakeListItemsRaw("ChooseFacility: rows", v.list, rows, kMaxFacilityChoices);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        FacilityChoice c;
        c.handle = rows[i];
        c.index = i;
        // Unlike the commodity rows, PopulateList goes through UIListItemString.SetText — so the
        // name really is here and needs no companion.
        c.label = CleanText(ReadFStringField(rows[i], kListItemStringTextOffset));
        c.disabled = (ReadU32Raw(rows[i], kListItemStringFlagsOffset) & kListItemDisabled) != 0;
        // NeedsAttention spawns the icon lazily and then toggles its visibility, so the icon
        // EXISTING is not the same as it being shown — a facility that lost priority keeps a
        // hidden one. Both halves are the game's own.
        void* icon = ReadPtrRaw(rows[i], kListItemAttentionOffset);
        c.priority = icon && (ReadU32Raw(icon, kPanelFlagsOffset) & kPanelVisible) != 0;
        v.choices.push_back(std::move(c));
    }
    return v;
}

bool SetChooseFacilitySelection(const ChooseFacilityView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivateChooseFacility(const ChooseFacilityView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    // ⛔ Enter goes to the SCREEN after the selection lands. OnAccept reads the SCREEN's own
    // SelectedIndex — a field only UpdateSelection writes, and only UIList::SetSelectedIndex
    // calls that — so committing without selecting first would build a different facility from
    // the one just spoken. A disabled row is passed through: OnAccept re-tests CanBuildFacility
    // and plays its own refusal sound, which keeps the refusal where the game can be right about
    // why (supplies, power, or staffing).
    SetListIndex(view.list, index);
    return SendRowCommand(view.screen, kCmdEnter);
}

// ---- covert actions (UICovertActions) ----
//
// See the header for the three-way split and the two ⛔s (destructive selection, mouse-only
// confirm). This half reads the ROWS, which really are retained, plus the four structural facts
// the companion cannot see: which action the panels are describing, whether one is already
// running, whether the game is in its own slot mode, and whether the launch button is on screen.

namespace {

constexpr int32_t kCovertActionRefOffset = 0x580;   // UICovertActions::ActionRef (ObjectID first)
constexpr int32_t kCovertActionsFlagsOffset = 0x5A4; // bActionInProgress .. bTutorialPopupDisplayed
constexpr uint32_t kCovertActionInProgress = 0x01;   // bActionInProgress
constexpr uint32_t kCovertActionSelectingSlots = 0x20; // bIsSelectingSlots
constexpr int32_t kCovertActionsListOffset = 0x5A8;   // UICovertActions::List
constexpr int32_t kCovertActionsLaunchOffset = 0x5B0; // UICovertActions::LaunchButton

// UIListItemString::metadataInt — the action's ObjectID on an action row, and the ONLY field that
// tells one row's action from another's. Zero on the faction headings, which never set it.
constexpr int32_t kListItemStringMetaIntOffset = 0x3D8;

// Four factions' worth of actions plus a heading each, bounded far above what the game builds.
constexpr int kMaxCovertActionRows = 64;

} // namespace

void* FindCovertActionsScreen()
{
    static ScreenFinder sFinder{"UICovertActions"};
    static bool sReported = false;
    void* screen = PreferStack("UICovertActions", sFinder, sReported);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

CovertActionsView ReadCovertActions(void* screen)
{
    CovertActionsView v;
    if (!screen)
        return v;
    v.screen = screen;

    const uint32_t flags = ReadU32Raw(screen, kCovertActionsFlagsOffset);
    v.actionInProgress = (flags & kCovertActionInProgress) != 0;
    v.selectingSlots = (flags & kCovertActionSelectingSlots) != 0;
    v.actionId = ReadI32Raw(screen, kCovertActionRefOffset);

    // ⛔ Only while the game is SHOWING it. UpdateData calls LaunchButton.Hide() outright when an
    // action is in progress or influence is unmet, and UIButton::Click() refuses a hidden button
    // anyway — so a node built over a hidden one would advertise a verb that cannot fire. This is
    // the documented exception to never-gate-on-bIsVisible: a BUTTON's visibility is toggled
    // deliberately and does mean hidden.
    void* launch = ReadPtrRaw(screen, kCovertActionsLaunchOffset);
    if (launch && (ReadU32Raw(launch, kPanelFlagsOffset) & kPanelVisible) != 0) {
        v.launch = launch;
        v.launchDisabled = (ReadU32Raw(launch, kButtonFlagsOffset) & kButtonDisabled) != 0;
    }

    v.list = ReadPtrRaw(screen, kCovertActionsListOffset);
    if (!v.list)
        return v;

    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxCovertActionRows] = {};
    const int rowCount
        = TakeListItemsRaw("CovertActions: rows", v.list, rows, kMaxCovertActionRows);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        // ⛔ The list holds TWO kinds of row. BuildScreen spawns a UICovertOpsFactionListItem
        // heading whenever the faction changes, and that class pushes its words through
        // MC.BeginFunctionOp("populateData") — so it derives from UIListItemString, sits in the
        // same itemContainer, and has an EMPTY Text. It is also DisableNavigation()ed, i.e. the
        // game's own arrows skip it, so ours must too. Matched by CLASS rather than by the empty
        // Text: an empty answer is indistinguishable from a field looked up in the wrong place,
        // and this one has a name to ask for instead.
        char cls[64];
        if (ObjClassNameAnsiRaw(rows[i], cls, sizeof(cls))
            && strcmp(cls, "UICovertOpsFactionListItem") == 0)
            continue;

        CovertActionRow r;
        r.handle = rows[i];
        // The LIST's index, headings included — SetSelectedIndex counts them, so a row's position
        // among the actions is not the number to hand back to the game.
        r.index = i;
        r.actionId = ReadI32Raw(rows[i], kListItemStringMetaIntOffset);
        r.label = CleanText(ReadFStringField(rows[i], kListItemStringTextOffset));
        r.disabled = (ReadU32Raw(rows[i], kListItemStringFlagsOffset) & kListItemDisabled) != 0;
        v.rows.push_back(std::move(r));
    }
    return v;
}

bool SetCovertActionSelection(const CovertActionsView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivateCovertActionLaunch(const CovertActionsView& view)
{
    // No select-then-Enter dance here, and deliberately: OnLaunchClicked reads the screen's own
    // ActionRef, which only SelectedItemChanged writes — and that is already in step, because the
    // recipe's focus write-back is what moved it. Click() re-tests IsDisabled itself.
    if (!view.launch || view.launchDisabled)
        return false;
    return ClickButton(view.launch);
}

// ---- resistance orders (UIStrategyPolicy) ----
//
// See the header for why this screen needs a recipe rather than a reader. Native reads the
// STRUCTURE — which cards are in which slot of which column, which one is picked up, and whether
// the game will accept a change at all; every word comes from X2AccessPolicyReader, because a
// UIStrategyPolicy_Card retains none.

namespace {

constexpr int32_t kPolicyColumnsOffset = 0x4D0; // TArray<UIList*>, always four
constexpr int32_t kPolicyHandOffset = 0x4E0;    // UIStrategyPolicy_DeckList*
constexpr int32_t kPolicyFlagsOffset = 0x4F8;   // bDragging .. m_bLoopHandSelection
constexpr uint32_t kPolicyEnableDragging = 0x04; // bEnableDragging
constexpr uint32_t kPolicyResistanceReport = 0x10; // bResistanceReport
constexpr int32_t kPolicyInspectingOffset = 0x538;  // UIStrategyPolicy_Card*

// UIStrategyPolicy_Card. AssociatedFactionName at 0x3D8 is an FName and is deliberately NOT read:
// the companion has the faction from the card state, and an FName would be an untranslated
// 'Faction_Reapers' at the player.
constexpr int32_t kPolicyCardColumnOffset = 0x3E0; // ColumnIndex
constexpr int32_t kPolicyCardSlotOffset = 0x3E4;   // ColumnSlot
constexpr int32_t kPolicyCardRefOffset = 0x3E8;    // CardRef (ObjectID first)
constexpr int32_t kPolicyCardFlagsOffset = 0x3EC;  // bLocked | bVital
constexpr uint32_t kPolicyCardLocked = 0x01;
constexpr uint32_t kPolicyCardVital = 0x02;

// A column holds at most three slots plus one locked placeholder; the hand is the player's whole
// unplayed deck, which the game does not page on mouse+keyboard (FilterCardsToDisplay returns
// everything unless a controller is active). Both bounded far above what the game builds.
constexpr int kMaxPolicySlots = 16;
constexpr int kMaxPolicyHandCards = 64;
constexpr int kPolicyColumnCount = 4;

PolicyCard ReadPolicyCardRaw(void* handle, void* inspecting, bool inHand, int handIndex)
{
    PolicyCard c;
    c.handle = handle;
    c.cardId = ReadI32Raw(handle, kPolicyCardRefOffset);
    const uint32_t flags = ReadU32Raw(handle, kPolicyCardFlagsOffset);
    c.locked = (flags & kPolicyCardLocked) != 0;
    c.vital = (flags & kPolicyCardVital) != 0;
    c.inspecting = handle == inspecting;
    if (inHand) {
        // A hand card is never given a ColumnIndex — CreateCard leaves it at the default 0, which
        // is the WILD-CARD column's real index. Writing -1 here is what keeps the verbs' two-int
        // address unambiguous; the hand's own list index takes the slot field.
        c.column = -1;
        c.slot = handIndex;
    } else {
        c.column = ReadI32Raw(handle, kPolicyCardColumnOffset);
        c.slot = ReadI32Raw(handle, kPolicyCardSlotOffset);
    }
    return c;
}

} // namespace

void* FindStrategyPolicyScreen()
{
    static ScreenFinder sFinder{"UIStrategyPolicy"};
    static bool sReported = false;
    void* screen = PreferStack("UIStrategyPolicy", sFinder, sReported);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

StrategyPolicyView ReadStrategyPolicy(void* screen)
{
    StrategyPolicyView v;
    if (!screen)
        return v;
    v.screen = screen;

    const uint32_t flags = ReadU32Raw(screen, kPolicyFlagsOffset);
    v.canPlay = (flags & kPolicyEnableDragging) != 0;
    v.resistanceReport = (flags & kPolicyResistanceReport) != 0;

    void* inspecting = ReadPtrRaw(screen, kPolicyInspectingOffset);
    if (inspecting)
        v.inspectingId = ReadI32Raw(inspecting, kPolicyCardRefOffset);

    void* lists[kPolicyColumnCount] = {};
    const int listCount
        = TakeArrayRaw("StrategyPolicy: columns", screen, kPolicyColumnsOffset, lists,
            kPolicyColumnCount);
    for (int i = 0; i < listCount; i++) {
        if (!lists[i])
            continue;
        PolicyColumn col;
        col.list = lists[i];
        col.index = i;
        void* cards[kMaxPolicySlots] = {};
        const int n = TakeListItemsRaw("StrategyPolicy: slots", lists[i], cards, kMaxPolicySlots);
        for (int j = 0; j < n; j++) {
            if (cards[j])
                col.cards.push_back(ReadPolicyCardRaw(cards[j], inspecting, /*inHand*/ false, j));
        }
        v.columns.push_back(std::move(col));
    }

    v.hand = ReadPtrRaw(screen, kPolicyHandOffset);
    if (v.hand) {
        void* cards[kMaxPolicyHandCards] = {};
        const int n
            = TakeListItemsRaw("StrategyPolicy: hand", v.hand, cards, kMaxPolicyHandCards);
        for (int j = 0; j < n; j++) {
            if (cards[j])
                v.handCards.push_back(ReadPolicyCardRaw(cards[j], inspecting, /*inHand*/ true, j));
        }
    }
    return v;
}

// ---- promotion / level-up (UIArmory_Promotion + subclasses) ----

namespace {

constexpr int32_t kArmoryUnitRefOffset = 0x450;      // UIArmory::UnitReference (ObjectID first)

// UIArmory_Promotion (the classic list screen; UIArmory_PromotionPsiOp reuses it unchanged).
constexpr int32_t kPromoClassRowOffset = 0x53C;       // ClassRowItem (UIArmory_PromotionItem*)
constexpr int32_t kPromoSelectedBranchOffset = 0x614; // SelectedAbilityIndex (int)
constexpr int32_t kPromoListOffset = 0x618;           // List (UIList*)

// UIArmory_PromotionItem — one rank row of the classic screen.
constexpr int32_t kPromoItemRankOffset = 0x3D8;     // Rank (int) — an ABILITY TREE index
constexpr int32_t kPromoItemIcon1Offset = 0x404;    // AbilityIcon1 (UIIcon*)
constexpr int32_t kPromoItemIcon2Offset = 0x40C;    // AbilityIcon2 (UIIcon*)
constexpr int32_t kPromoItemSelectedOffset = 0x41C; // SelectedAbility (int)
constexpr int32_t kPromoItemFlagsOffset = 0x420;    // bIsDisabled | bEligibleForPromotion
constexpr uint32_t kPromoItemDisabled = 0x1;
constexpr uint32_t kPromoItemEligible = 0x2;

// UIArmory_PromotionHero and its columns.
constexpr int32_t kPromoHeroColumnsOffset = 0x624; // Columns (TArray<UIArmory_PromotionHeroColumn*>)
constexpr int32_t kPromoHeroSelectedColOffset = 0x6E4; // m_iCurrentlySelectedColumn (int)
constexpr int32_t kPromoColRankOffset = 0x3D8;         // Rank (int)
constexpr int32_t kPromoColIconsOffset = 0x3FC;        // AbilityIcons (TArray<UIIcon*>)
constexpr int32_t kPromoColFlagsOffset = 0x40C;        // bIsDisabled | bEligibleForPurchase
constexpr uint32_t kPromoColDisabled = 0x1;
constexpr uint32_t kPromoColEligible = 0x2;
constexpr int32_t kPromoColPanelIndexOffset = 0x420; // m_iPanelIndex (int)

// UIArmory_PromotionHero spawns exactly seven columns and NUM_ABILITIES_PER_COLUMN is 4; the
// classic screen's list is one row per rank. Both bounded well above what the game draws.
constexpr int kMaxPromotionRows = 32;
constexpr int kMaxPromotionBranches = 4;

// Probe order is the class hierarchy's, most-derived first — FindStackScreenRaw and
// IsInstanceRaw both match the EXACT class name, so a hero screen would never answer to
// "UIArmory_Promotion" and the three can be told apart cleanly.
struct PromotionClass {
    const char* name;
    PromotionKind kind;
};
constexpr PromotionClass kPromotionClasses[] = {
    {"UIArmory_PromotionHero", PromotionKind::Hero},
    {"UIArmory_PromotionPsiOp", PromotionKind::PsiOp},
    {"UIArmory_Promotion", PromotionKind::Classic},
};

ScreenFinder gPromoHeroFinder{"UIArmory_PromotionHero"};
ScreenFinder gPromoPsiFinder{"UIArmory_PromotionPsiOp"};
ScreenFinder gPromoClassicFinder{"UIArmory_Promotion"};
ScreenFinder* const kPromotionFinders[]
    = {&gPromoHeroFinder, &gPromoPsiFinder, &gPromoClassicFinder};
bool gPromoFallbackReported[3] = {};

// Which of the three classes `screen` is, or false when it is none of them.
bool PromotionKindOf(void* screen, PromotionKind* out)
{
    for (const PromotionClass& c : kPromotionClasses) {
        if (ObjectClassNamedRaw(screen, c.name)) {
            *out = c.kind;
            return true;
        }
    }
    return false;
}

// The ability slots of one row, in branch order. `icons` are the row's own UIIcon pointers; a
// HIDDEN one is the game saying the tree has no ability at that branch, which is what makes a
// ragged tree readable — the cell keeps its branch NUMBER while being unlandable.
void ReadPromotionCells(void* const* icons, int count, std::vector<PromotionCell>& out)
{
    for (int i = 0; i < count; i++) {
        PromotionCell cell;
        cell.branch = i;
        cell.icon = icons[i];
        cell.visible = cell.icon && (ReadU32Raw(cell.icon, kPanelFlagsOffset) & kPanelVisible) != 0;
        out.push_back(cell);
    }
}

} // namespace

void* FindPromotionScreen()
{
    for (int i = 0; i < 3; i++) {
        if (void* onStack = FindStackScreenRaw(kPromotionClasses[i].name))
            return onStack;
    }
    // The stack is the authority; this only covers the window in which it is being rebuilt.
    for (int i = 0; i < 3; i++) {
        void* found = kPromotionFinders[i]->Find();
        if (found) {
            if (!gPromoFallbackReported[i]) {
                gPromoFallbackReported[i] = true;
                LOG_INFO("GameUI: %s is not on the screen stack — falling back to an object scan",
                    kPromotionClasses[i].name);
            }
            return found;
        }
    }
    return nullptr;
}

PromotionView ReadPromotion(void* screen)
{
    PromotionView v;
    if (!screen || !PromotionKindOf(screen, &v.kind))
        return v;
    v.screen = screen;
    v.unitId = ReadI32Raw(screen, kArmoryUnitRefOffset);

    if (v.kind == PromotionKind::Hero) {
        void* columns[kMaxPromotionRows] = {};
        int n = ReadPtrArrayRaw(screen, kPromoHeroColumnsOffset, columns, kMaxPromotionRows);
        for (int i = 0; i < n; i++) {
            if (!columns[i])
                continue;
            PromotionRow row;
            row.handle = columns[i];
            row.rank = ReadI32Raw(columns[i], kPromoColRankOffset);
            uint32_t flags = ReadU32Raw(columns[i], kPromoColFlagsOffset);
            row.disabled = (flags & kPromoColDisabled) != 0;
            row.eligible = (flags & kPromoColEligible) != 0;
            row.selectedBranch = ReadI32Raw(columns[i], kPromoColPanelIndexOffset);

            void* icons[kMaxPromotionBranches] = {};
            int iconCount
                = ReadPtrArrayRaw(columns[i], kPromoColIconsOffset, icons, kMaxPromotionBranches);
            ReadPromotionCells(icons, iconCount, row.cells);
            v.rows.push_back(std::move(row));
        }
        // The hero screen navigates itself through its own pair of ints, so they ARE the cursor.
        int col = ReadI32Raw(screen, kPromoHeroSelectedColOffset);
        if (col >= 0 && col < static_cast<int>(v.rows.size())) {
            v.selectedRow = col;
            v.selectedBranch = v.rows[static_cast<size_t>(col)].selectedBranch;
        }
        v.valid = !v.rows.empty();
        return v;
    }

    // Classic / PsiOp. The class row is a SEPARATE widget from the list and it is rank 0 — the
    // list starts at rank 1. Reading it first is what puts the rows in tree order.
    if (void* classRow = ReadPtrRaw(screen, kPromoClassRowOffset)) {
        PromotionRow row;
        row.handle = classRow;
        row.rank = ReadI32Raw(classRow, kPromoItemRankOffset);
        uint32_t flags = ReadU32Raw(classRow, kPromoItemFlagsOffset);
        row.disabled = (flags & kPromoItemDisabled) != 0;
        row.eligible = (flags & kPromoItemEligible) != 0;
        row.selectedBranch = ReadI32Raw(classRow, kPromoItemSelectedOffset);
        void* icons[2] = {ReadPtrRaw(classRow, kPromoItemIcon1Offset),
            ReadPtrRaw(classRow, kPromoItemIcon2Offset)};
        ReadPromotionCells(icons, 2, row.cells);
        v.rows.push_back(std::move(row));
    }

    void* list = ReadPtrRaw(screen, kPromoListOffset);
    void* items[kMaxPromotionRows] = {};
    int n = ReadListItemsRaw(list, items, kMaxPromotionRows);
    for (int i = 0; i < n; i++) {
        if (!items[i])
            continue;
        PromotionRow row;
        row.handle = items[i];
        row.rank = ReadI32Raw(items[i], kPromoItemRankOffset);
        uint32_t flags = ReadU32Raw(items[i], kPromoItemFlagsOffset);
        row.disabled = (flags & kPromoItemDisabled) != 0;
        row.eligible = (flags & kPromoItemEligible) != 0;
        row.selectedBranch = ReadI32Raw(items[i], kPromoItemSelectedOffset);
        void* icons[2] = {ReadPtrRaw(items[i], kPromoItemIcon1Offset),
            ReadPtrRaw(items[i], kPromoItemIcon2Offset)};
        ReadPromotionCells(icons, 2, row.cells);
        v.rows.push_back(std::move(row));
    }

    // The list index is one behind ours, whose slot 0 is the class row. A NEGATIVE index is not
    // "nothing selected" here: UpdateClassRowSelection reads exactly that to mean the class row
    // has focus, which is where InitPromotion parks a rank-1 soldier.
    const bool haveClassRow = !v.rows.empty() && v.rows.front().rank == 0;
    int listIndex = list ? ReadI32Raw(list, kListSelectedIndexOffset) : -1;
    if (listIndex >= 0)
        v.selectedRow = haveClassRow ? listIndex + 1 : listIndex;
    else if (haveClassRow)
        v.selectedRow = 0;
    if (v.selectedRow >= static_cast<int>(v.rows.size()))
        v.selectedRow = -1;
    v.selectedBranch = ReadI32Raw(screen, kPromoSelectedBranchOffset);

    v.valid = !v.rows.empty();
    return v;
}

bool SetPromotionSelection(const PromotionView& view, int row, int branch)
{
    if (!view.valid || row < 0 || row >= static_cast<int>(view.rows.size()))
        return false;
    const PromotionRow& r = view.rows[static_cast<size_t>(row)];

    if (view.kind == PromotionKind::Hero) {
        // The hero screen navigates itself through two plain ints, so writing them IS its model
        // rather than a fight with it, and OnReceiveFocus does the highlight the same way its
        // controller branch does.
        WriteI32Raw(view.screen, kPromoHeroSelectedColOffset, row);
        WriteI32Raw(r.handle, kPromoColPanelIndexOffset, branch);
        CallNamedFunction(r.handle, "OnReceiveFocus", nullptr);
        // PreviewAbility repaints the description panel — the one thing that makes the SCREEN
        // show what the player is hearing. The game reaches it only from a mouse hover or the
        // controller path, which is exactly the already-written-but-unreachable shape this layer
        // exists to call.
        struct {
            int32_t Rank;
            int32_t Branch;
        } params{r.rank, branch};
        CallNamedFunction(view.screen, "PreviewAbility", &params);
        return true;
    }

    // Classic: drive the LIST, which fires OnSelectionChanged = PreviewRow and repaints the
    // description panel for free. The class row is not IN the list, so landing on it means
    // leaving the list where UpdateClassRowSelection expects it.
    void* list = ReadPtrRaw(view.screen, kPromoListOffset);
    const bool haveClassRow = view.rows.front().rank == 0;
    if (!(haveClassRow && row == 0))
        SetListIndex(list, haveClassRow ? row - 1 : row);
    // The row's own within-row cursor, and the screen's copy of it that PreviewRow reads back.
    WriteI32Raw(view.screen, kPromoSelectedBranchOffset, branch);
    struct {
        int32_t AbilityIndex;
    } params{branch};
    CallNamedFunction(r.handle, "SetSelectedAbility", &params);
    return true;
}

bool ActivatePromotionCell(const PromotionView& view, int row, int branch)
{
    if (!view.valid || row < 0 || row >= static_cast<int>(view.rows.size()))
        return false;
    const PromotionRow& r = view.rows[static_cast<size_t>(row)];
    if (branch < 0 || branch >= static_cast<int>(r.cells.size()))
        return false;

    // Selection first. Both row classes read their OWN Rank inside SelectAbility and the classic
    // one hands the screen (Rank, idx) — so a call made while the widgets still pointed at
    // another cell would buy a different ability from the one just spoken.
    SetPromotionSelection(view, row, branch);

    // SelectAbility(int) is declared on BOTH row classes with the same name and signature, and
    // is already the three-way verb the game wrote: owned opens the info popup, purchasable
    // raises the confirm dialog (which the dialog recipe reads), locked plays the refusal sound.
    struct {
        int32_t Idx;
    } params{branch};
    return CallNamedFunction(r.handle, "SelectAbility", &params);
}

// ---- tutorial mission (Tutorial_01) ----

namespace {

// XComTutorialMgr is an Actor, not a screen, so it gets its own finder rather than a slot in the
// shared screen pass. No usability predicate: it has no UIPanel flag dword to test, it exists
// only during the tutorial mission, and there is exactly one per session — so "is it still an
// instance of its class" (which ScreenFinder revalidates on every call) is the whole test.
ScreenFinder gTutorialMgrFinder{"XComTutorialMgr", nullptr};

// An FString field WITHOUT CleanText. World-message text carries <img src='...'> markup that the
// caller has to TRANSLATE rather than drop — CleanText would strip it silently and leave a hole
// exactly where the key name belongs ("Press , to go here").
std::string ReadFStringFieldRawText(const void* obj, int32_t offset)
{
    wchar_t buf[1024];
    int len = CopyFStringRaw(reinterpret_cast<const uint8_t*>(obj) + offset, buf, 1024);
    return len > 0 ? WcharToUtf8(buf, len) : std::string();
}

// A TArray<int32> at `offset`: copies up to maxItems values out. Returns the count, or -1 if the
// array itself could not be read; `clipped` follows ReadPtrArrayRaw's contract.
int ReadIntArrayRaw(const void* obj, int32_t offset, int32_t* out, int maxItems,
    bool* clipped = nullptr)
{
    if (clipped)
        *clipped = false;
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(obj) + offset;
        auto* data = *reinterpret_cast<const int32_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0)
            return 0;
        if (n > maxItems) {
            n = maxItems;
            if (clipped)
                *clipped = true;
        }
        for (int32_t i = 0; i < n; i++)
            out[i] = data[i];
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// The base of element `index` of the m_arrMsgs TArray, bounds-checked against its own count.
const uint8_t* WorldMessageSlotRaw(void* mgr, int index)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(mgr) + kWorldMsgArrayOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || index < 0 || index >= n || n > kMaxWorldMessages)
            return nullptr;
        return data + static_cast<size_t>(index) * kHudMsgStride;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

} // namespace

void* FindTutorialBox()
{
    return FindScreen(kScrTutorialBox);
}

TutorialBoxView ReadTutorialBox(void* screen)
{
    TutorialBoxView v;
    if (!screen)
        return v;
    v.title = CleanText(ReadFStringField(screen, kTutBoxTitleOffset));
    v.body = CleanText(ReadFStringField(screen, kTutBoxDescOffset));
    v.buttonHelp = CleanText(ReadFStringField(screen, kTutBoxButtonHelpOffset));
    return v;
}

void* FindWorldMessageMgr()
{
    // Through the presentation layer, the way the game reaches it (`PRES.GetWorldMessenger()`),
    // rather than by scanning GObjects for the class: a manager from a previous mission stays
    // allocated until GC and would win a "newest instance" scan while being wired to nothing.
    void* hud = FindTacticalHUD();
    if (!hud)
        return nullptr;
    void* movie = ReadPtrRaw(hud, kPanelMovieOffset);
    if (!movie)
        return nullptr;
    void* pres = ReadPtrRaw(movie, kMoviePresOffset);
    if (!pres)
        return nullptr;
    return ReadPtrRaw(pres, kPresWorldMsgMgrOffset);
}

std::vector<WorldMessage> ReadWorldMessages(void* mgr)
{
    std::vector<WorldMessage> out;
    if (!mgr)
        return out;

    // m_arrActiveMessages holds INDICES into m_arrMsgs. Reading the index array rather than
    // walking m_arrMsgs is what distinguishes "showing" from "pooled and waiting": a deactivated
    // message keeps its text in the slot it will be recycled from.
    int32_t active[kMaxWorldMessages];
    bool clipped = false;
    int count = NoteArrayRead("WorldMessages: m_arrActiveMessages",
        ReadIntArrayRaw(mgr, kWorldMsgActiveOffset, active, kMaxWorldMessages, &clipped), clipped,
        kMaxWorldMessages);
    if (count <= 0)
        return out;

    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        const uint8_t* slot = WorldMessageSlotRaw(mgr, active[i]);
        if (!slot)
            continue;
        WorldMessage m;
        m.slot = active[i];
        m.id = ReadFStringFieldRawText(slot, kHudMsgIdOffset);
        m.text = ReadFStringFieldRawText(slot, kHudMsgTextOffset);
        m.type = static_cast<int>(ReadU32Raw(slot, kHudMsgTypeOffset) & 0xFF);
        const uint32_t flags = ReadU32Raw(slot, kHudMsgFlagsOffset);
        m.visible = (flags & kHudMsgVisible) != 0;
        m.important = (flags & kHudMsgImportant) != 0;
        m.followObjectId = static_cast<int>(ReadU32Raw(slot, kHudMsgFollowOffset));
        out.push_back(std::move(m));
    }
    return out;
}

bool HudBladeShowing(void* mgr)
{
    return mgr && ReadPtrRaw(mgr, kWorldMsgBladeOffset) != nullptr;
}

TutorialState ReadTutorialState()
{
    TutorialState s;
    void* mgr = gTutorialMgrFinder.Find();
    if (!mgr)
        return s;
    s.valid = true;
    s.inTutorial = (ReadU32Raw(mgr, kReplayFlagsOffset) & kReplayInTutorial) != 0;
    s.allowingControls = (ReadU32Raw(mgr, kTutFlagsOffset) & kTutAllowingControls) != 0;
    s.historyFrame = static_cast<int>(ReadU32Raw(mgr, kReplayFrameOffset));
    s.nextInputFrame = static_cast<int>(ReadU32Raw(mgr, kTutNextInputFrameOffset));
    s.invalidSubmissions = static_cast<int>(ReadU32Raw(mgr, kTutInvalidCountOffset));
    s.nextAbilityContext = ReadPtrRaw(mgr, kTutNextAbilityCtxOffset);
    return s;
}

std::string CleanText(const std::string& text)
{
    // Strip <...> markup tags, collapse runs of whitespace, trim.
    std::string out;
    out.reserve(text.size());
    bool inTag = false;
    bool pendingSpace = false;
    for (char c : text) {
        if (c == '<') {
            inTag = true;
            continue;
        }
        if (inTag) {
            if (c == '>')
                inTag = false;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        out += c;
    }
    return out;
}

// ---- the geoscape (UIStrategyMap) — the mission tray ------------------------------------------
//
// The globe has no cursor for a mouse+keyboard player: ProcessGeoscapeSelection, and therefore
// UpdateSelection -> SelectMapItemNearestLocation, runs only inside `if( `ISCONTROLLERACTIVE)`.
// What IS live on mouse+keyboard is the mission TRAY along the bottom — the game's own list of
// everything actionable — because InitScreen sets Navigator.HorizontalNavigation and
// UIScreen.OnUnrealCommand pipes unhandled arrows into Navigator.Prev/Next. And because nothing
// recomputes it, whatever those arrows select STICKS: measured live 2026-08-21, one synthetic
// Left moved SelectedIndex 0 -> 1 and SelectedMapItem POI -> Mission, unchanged 12 s later.
//
// So the game hands us a working list cursor that is invisible and silent, and this reader
// supplies the three things the mouse-active guard withholds — brackets, camera, tooltip.

namespace {

// Offsets from the generated SDK's reflection-dump comments, cross-checked live 2026-08-21
// against the running class through /props on an ordinary campaign.
constexpr int32_t kStratMapUiStateOffset = 0x620;      // UIStrategyMap::m_eUIState (byte)
constexpr int32_t kStratMapMissionIconsOffset = 0x624; // ::MissionItemUI.MissionIcons
constexpr int32_t kStratMapSelectedItemOffset = 0x78C; // ::SelectedMapItem
constexpr int32_t kPanelNavigatorOffset = 0x2D8;       // UIPanel::Navigator
constexpr int32_t kNavSelectedIndexOffset = 0x6C;      // UINavigator::SelectedIndex
constexpr int32_t kNavControlsOffset = 0x74;           // ::NavigableControls
constexpr int32_t kIconScanSiteOffset = 0x49C;         // UIStrategyMap_MissionIcon::ScanSite
constexpr int32_t kIconMissionSiteOffset = 0x4A4;      // ::MissionSite
constexpr int32_t kIconMapItemOffset = 0x4AC;          // ::MapItem
constexpr int32_t kIconTooltipOffset = 0x4B4;          // ::ToolTip
constexpr int32_t kIconIdNumOffset = 0x4C0;            // ::idNum
constexpr int32_t kIconImagePathOffset = 0x3DC;        // UIIcon::ImagePath
constexpr int32_t kMapItemPinHeaderOffset = 0x4A0;     // UIStrategyMapItem::MapPin_Header

// The art the game draws in a faction slot whose faction has not been met. This is the game's
// OWN answer to "is this a placeholder", and it is the only reliable one — see ReadStrategyMap.
constexpr char kFactionUnknownArt[] = "Faction_unknown";
constexpr int32_t kTooltipTitleOffset = 0x484;         // UIStrategyMap_MissionIconTooltip::Title
constexpr int32_t kTooltipBodyOffset = 0x494;          // ::Body
constexpr int32_t kMapItemEntityRefOffset = 0x494;     // UIStrategyMapItem::GeoscapeEntityRef

// The HUD's four keyboard-unreachable verbs, and the localized words the screen already owns
// for them. m_strDarkEventsLabel is the one measured live so far ("СОБЫТИЯ", 2026-08-21).
constexpr int32_t kStratMapHudOffset = 0x610;             // UIStrategyMap::StrategyMapHUD
constexpr int32_t kStratMapDarkEventsButtonOffset = 0x654;    // ::DarkEventsButton
constexpr int32_t kStratMapDarkEventsContainerOffset = 0x65C; // ::DarkEventsContainer
constexpr int32_t kStratMapDarkEventsLabelOffset = 0x6AC;     // ::m_strDarkEventsLabel
constexpr int32_t kStratMapToggleResNetOffset = 0x6BC;        // ::m_strToggleResNet
constexpr int32_t kStratMapResOrdersOffset = 0x6CC;           // ::m_srResistanceOrders
constexpr int32_t kStratMapChosenInfoOffset = 0x6DC;          // ::m_srChosenInfo
constexpr int32_t kStratMapFlagsOffset = 0x7C8;               // bCursorAlwaysVisible..
constexpr uint32_t kStratMapResNetForcedOn = 0x08;            // ::m_bResNetForcedOn
constexpr int32_t kHudChosenButtonOffset = 0x474;  // UIStrategyMap_HUD::m_kChosenInfoButton
constexpr int32_t kHudResOrdersButtonOffset = 0x47C; // ::m_kResistanceInfoButton

constexpr int kMaxTrayIcons = 20; // UIStrategyMap.MAX_NUM_STRATEGYICONS
constexpr int kMaxNavControls = 32;

// UIUtilities_Input's own constants. The hover PAIR is deliberately the mouse one rather than a
// D-pad command: that handler is where the game itself keeps the camera fly-to and the tooltip
// refresh. FXS_L_MOUSE_UP is what the Dark Events handler tests for, and what the game's own
// FXS_BUTTON_Y branch passes it.
constexpr int32_t kFxsLMouseUp = 391;
constexpr int32_t kFxsLMouseIn = 392;
constexpr int32_t kFxsLMouseOut = 393;
// ⛔ 400, NOT 391, is a CLICK. UIIcon.OnMouseEvent fires OnClickedDelegate on
// FXS_L_MOUSE_UP_DELAYED and ignores FXS_L_MOUSE_UP entirely (UIIcon.uc:280) — which is a live
// Firaxis bug in UIStrategyMap_MissionIcon.OnUnrealCommand, whose FXS_BUTTON_A branch sends 391
// and therefore does nothing but play a click sound. Getting this constant wrong is silent.
constexpr int32_t kFxsLMouseUpDelayed = 400;

// A child widget's visible bit IS meaningful (unlike a screen's) — the game toggles it
// deliberately, which is exactly how it answers "is this verb on screen right now".
bool WidgetShowing(void* panel)
{
    if (!panel)
        return false;
    uint32_t flags = ReadU32Raw(panel, kPanelFlagsOffset);
    return (flags & kPanelVisible) && !(flags & kPanelRemoved);
}

// UIPanel.OnMouseEvent(int cmd, array<string> args).
//
// The args array is passed by pointer into ProcessEvent's frame (parameters are shallow-memcpy'd)
// and the destroy pass skips everything below ParmsSize, so a zeroed empty TArray we own is both
// safe and never freed by the engine. No handler this is used for reads it.
bool SendIconMouseEvent(void* icon, int32_t cmd)
{
    if (!icon)
        return false;
    struct {
        int32_t Cmd;
        void* ArgsData;
        int32_t ArgsCount;
        int32_t ArgsMax;
    } params{cmd, nullptr, 0, 0};
    return CallNamedFunction(icon, "OnMouseEvent", &params);
}

// UIStrategyMap.SetSelectedMapItem(UIStrategyMapItem Selection, optional bool NavigationSelect).
//
// The one line UIStrategyMap.OnNavigationChanged exists to reach, called directly — the header's
// note on SetTraySelection says why the delegate itself is the wrong door. NavigationSelect=true
// is load-bearing: the false branch ends in Navigator.OnLoseFocus() + a no-argument
// Navigator.SetSelected(), i.e. a DESELECT of the control we just selected.
bool SetSelectedMapItemRaw(void* screen, void* mapItem)
{
    if (!screen || !mapItem)
        return false;
    struct {
        void* Selection;
        uint32_t bNavigationSelect;
    } params{mapItem, 1};
    return CallNamedFunction(screen, "SetSelectedMapItem", &params);
}

} // namespace

GeoscapeGate ReadGeoscapeGate()
{
    GeoscapeGate gate;
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);

    // GetCurrentScreen is the FIRST non-none entry and applies no state-bit filter of its own,
    // so neither do we — the same rule ReadAvengerGate follows, and for the same reason: a gate
    // that disagreed with the game about which screen the arrows reach is the one disagreement
    // that matters.
    bool sawTop = false;
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UIStrategyMap") == 0) {
            gate.mapOnStack = true;
            gate.current = !sawTop;
            if (!sawTop) {
                // Still refuse a map that is on the stack but torn down.
                if (!StackEntryLiveRaw(screens[i]))
                    gate.current = false;
                // Carried out by VALUE, in this same call: the finder below needs exactly the
                // screen this walk already identified, and walking again to re-answer a question
                // just answered is how the two would come to disagree. Nothing caches it across
                // frames, so the never-hold-a-UObject rule is untouched.
                if (gate.current)
                    gate.map = screens[i];
            }
            break;
        }
        if (!sawTop) {
            sawTop = true;
            strncpy_s(gate.blocker, name, _TRUNCATE);
        }
    }
    return gate;
}

void* FindStrategyMapScreen()
{
    // One walk, not two: ReadGeoscapeGate has already found the map and validated it.
    return ReadGeoscapeGate().map;
}

StrategyMapView ReadStrategyMap(void* screen)
{
    StrategyMapView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.uiState = static_cast<int>(ReadU32Raw(screen, kStratMapUiStateOffset) & 0xFF);

    v.navigator = ReadPtrRaw(screen, kPanelNavigatorOffset);

    // The game's own navigable set, which is the ONLY honest source for "is this icon
    // reachable": UpdateMissions calls AddControl exactly for the icons it wants reachable, so
    // membership here is the game's own parity filter rather than a rule of ours.
    // ⚠ It is NavigableControls, not NavigationTargets — the latter is empty on this screen and
    // is what the ant farm's re-arm test looks at, a different list entirely.
    void* navControls[kMaxNavControls] = {};
    int navCount = 0;
    if (v.navigator) {
        v.selected = ReadI32Raw(v.navigator, kNavSelectedIndexOffset);
        navCount = TakeArrayRaw("StrategyMap: NavigableControls", v.navigator, kNavControlsOffset,
            navControls, kMaxNavControls);
    }

    void* icons[kMaxTrayIcons] = {};
    int iconCount = TakeArrayRaw("StrategyMap: MissionIcons", screen, kStratMapMissionIconsOffset,
        icons, kMaxTrayIcons);

    for (int i = 0; i < iconCount; i++) {
        void* icon = icons[i];
        if (!icon)
            continue;
        // A tray slot the game is not showing is not a stop. Unlike a SCREEN's bIsVisible —
        // which the game leaves false on screens it is plainly showing, hence the hard rule
        // against gating readiness on it — a child widget's visible bit is toggled deliberately
        // and does mean hidden. UpdateMissions calls Hide() on every slot it did not bind.
        uint32_t flags = ReadU32Raw(icon, kPanelFlagsOffset);
        if (!(flags & kPanelVisible) || (flags & kPanelRemoved))
            continue;

        TrayIcon t;
        t.handle = icon;
        t.slot = i;
        t.idNum = ReadI32Raw(icon, kIconIdNumOffset);
        t.scanSite = ReadPtrRaw(icon, kIconScanSiteOffset) != nullptr;
        t.mission = ReadPtrRaw(icon, kIconMissionSiteOffset) != nullptr;
        t.mapItem = ReadPtrRaw(icon, kIconMapItemOffset);
        if (t.mapItem) {
            t.entityId = ReadI32Raw(t.mapItem, kMapItemEntityRefOffset);
            // A localized, GENERIC noun for the kind of place the pin marks ("ШТАБ
            // СОПРОТИВЛЕНИЯ"). It is a CPF_Localized class default, so unlike everything else on
            // a pin it is NOT subject to the §4.3 staleness rule — an off-screen pin holds it
            // just as correctly as a framed one, because every instance of the class holds the
            // same value. That is exactly why it is safe to use for a slot with no tooltip.
            t.pinHeader = ReadFStringField(t.mapItem, kMapItemPinHeaderOffset);
        }

        void* tooltip = ReadPtrRaw(icon, kIconTooltipOffset);
        if (tooltip) {
            t.title = ReadFStringField(tooltip, kTooltipTitleOffset);
            t.body = ReadFStringField(tooltip, kTooltipBodyOffset);
        }

        for (int n = 0; n < navCount; n++) {
            if (navControls[n] == icon) {
                t.navIndex = n;
                break;
            }
        }

        // ⛔ THE PLACEHOLDER TEST IS THE ART, and nothing else works.
        //
        // The obvious tests both fail, measured live 2026-08-21 on an unmet faction's slot:
        // ScanSite is SET (to the faction's XComGameState_Haven, so "no site" is false), and the
        // icon IS in NavigableControls (so "not navigable" is false). What is actually true of
        // it is that ToolTip is NULL — SetMissionIconTooltip early-returns on
        // `HavenState != None && !IsResistanceFactionMet()` before it ever spawns one — and that
        // the game draws the Faction_unknown art. The art is the better test of the two: it is
        // the same fact a sighted player is reading off the screen, and it cannot be confused
        // with an icon whose tooltip merely has not been built yet.
        t.placeholder = ReadFStringField(icon, kIconImagePathOffset).find(kFactionUnknownArt)
            != std::string::npos;

        v.icons.push_back(std::move(t));
    }

    // The HUD's four verbs, each present only when the game is drawing it. Order is the screen's
    // own left-to-right reading of the top bar, with the map-mode toggle last because it changes
    // what the globe SHOWS rather than opening anything.
    void* darkContainer = ReadPtrRaw(screen, kStratMapDarkEventsContainerOffset);
    void* darkButton = ReadPtrRaw(screen, kStratMapDarkEventsButtonOffset);
    if (WidgetShowing(darkContainer) && darkButton) {
        HudVerb verb;
        verb.kind = HudVerb::Kind::DarkEvents;
        verb.button = darkButton;
        // On mouse+keyboard AddDarkEventsButton does a plain SetText(m_strDarkEventsLabel), so
        // the button RETAINS the localized word; the screen's own string is the fallback for the
        // controller branch, which wraps it in an icon injection.
        verb.label = ReadFStringField(darkButton, kButtonTextOffset);
        if (verb.label.empty())
            verb.label = ReadFStringField(screen, kStratMapDarkEventsLabelOffset);
        v.verbs.push_back(std::move(verb));
    }

    void* hud = ReadPtrRaw(screen, kStratMapHudOffset);
    if (hud) {
        void* chosenBtn = ReadPtrRaw(hud, kHudChosenButtonOffset);
        if (WidgetShowing(chosenBtn)) {
            HudVerb verb;
            verb.kind = HudVerb::Kind::ChosenInfo;
            verb.button = chosenBtn;
            // InitButton was called with no caption, so the button's own Text is empty — these
            // two read from the SCREEN's localized strings instead.
            verb.label = ReadFStringField(screen, kStratMapChosenInfoOffset);
            v.verbs.push_back(std::move(verb));
        }
        void* ordersBtn = ReadPtrRaw(hud, kHudResOrdersButtonOffset);
        if (WidgetShowing(ordersBtn)) {
            HudVerb verb;
            verb.kind = HudVerb::Kind::ResistanceOrders;
            verb.button = ordersBtn;
            verb.label = ReadFStringField(screen, kStratMapResOrdersOffset);
            v.verbs.push_back(std::move(verb));
        }
    }

    // The resistance-network toggle has no button of its own on mouse+keyboard — UpdateButtonHelp
    // adds it as a nav-help hotlink — so there is no widget to gate on and it is always offered.
    // That matches the screen: the hotlink is drawn whenever the map is.
    {
        HudVerb verb;
        verb.kind = HudVerb::Kind::ResNetToggle;
        verb.label = ReadFStringField(screen, kStratMapToggleResNetOffset);
        verb.on = (ReadU32Raw(screen, kStratMapFlagsOffset) & kStratMapResNetForcedOn) != 0;
        v.verbs.push_back(std::move(verb));
    }
    return v;
}

bool ActivateTraySelection(const StrategyMapView& view, const TrayIcon& icon)
{
    if (!view.screen || !icon.handle || icon.placeholder)
        return false;

    // Best effort, and cosmetic ONLY — nothing below depends on it. The 3D selection cursor, the
    // pin highlight and the nav-help prompts all read SelectedMapItem, so a screen whose
    // activation we are about to trigger should be looking at the thing we are activating.
    if (icon.mapItem && ReadPtrRaw(view.screen, kStratMapSelectedItemOffset) != icon.mapItem)
        SetSelectedMapItemRaw(view.screen, icon.mapItem);

    // The click itself, sent to the ICON. This is the mouse's own path and the whole reason the
    // hazard above is only cosmetic: OnClickedDelegate was bound by SetScanSite/SetMissionSite to
    // THIS icon's own site (`OnClickedDelegate = ScanSite.AttemptSelectionCheckInterruption`), so
    // the target is decided by which icon we hand the event to and by nothing else. Every gate
    // stays the game's — CanInteract, the interruption popup, the mission briefing prompt — and
    // AS_SetLock is a Flash-only call that never touches IsDisabled, so a "locked" pin is refused
    // by CanInteract inside the delegate rather than by the widget, exactly as it is for a mouse.
    return SendIconMouseEvent(icon.handle, kFxsLMouseUpDelayed);
}

bool ActivateHudVerb(const StrategyMapView& view, const HudVerb& verb)
{
    if (!view.screen)
        return false;

    switch (verb.kind) {
    case HudVerb::Kind::ChosenInfo:
    case HudVerb::Kind::ResistanceOrders:
        // UIButton::Click() fires OnClickedDelegate and refuses a disabled or hidden button
        // itself, so the guard the mouse path gets comes along for free. Both handlers also
        // check the screen stack before pushing, which is what stops a double press stacking
        // two copies of the same screen.
        return ClickButton(verb.button);

    case HudVerb::Kind::DarkEvents: {
        // ⛔ NOT Click(): ProcessMouseEvents wired this button's handler as an
        // OnMouseEventDelegate, so it has no OnClickedDelegate and Click() would return false
        // having done nothing at all. This is the game's own keyboard route reproduced exactly,
        // `none` panel included — OnDarkEventsClicked ignores the panel and tests only the
        // command, which must be FXS_L_MOUSE_UP or the body does not run.
        struct {
            void* Panel;
            int32_t Cmd;
        } params{nullptr, kFxsLMouseUp};
        return CallNamedFunction(view.screen, "OnDarkEventsClicked", &params);
    }

    case HudVerb::Kind::ResNetToggle: {
        // Zero parameters, but ProcessEvent still needs a block to point at. The screen's own
        // function owns both halves of the toggle — the forced-on flag and the SetUIState call
        // whose ordering its comment calls load-bearing — so this must never be open-coded.
        char noParams[8] = {};
        return CallNamedFunction(view.screen, "OnResNetClicked", noParams);
    }
    }
    return false;
}

bool SetTraySelection(const StrategyMapView& view, const TrayIcon& icon)
{
    // ⛔ A placeholder must leave the game's navigator alone. Moving it would put
    // SelectedMapItem on an icon other than the one just spoken, and nothing on screen would
    // ever show the disagreement.
    if (!view.navigator || icon.navIndex < 0)
        return false;

    void* controls[kMaxNavControls] = {};
    int count = TakeArrayRaw("StrategyMap: NavigableControls", view.navigator, kNavControlsOffset,
        controls, kMaxNavControls);
    if (icon.navIndex >= count || !controls[icon.navIndex])
        return false;

    // UINavigator.SetSelected(UIPanel optionalSelectedControl). Passing the CONTROL rather than
    // writing SelectedIndex is what gets the game's own bookkeeping — but that is ALL it gets:
    // this mover is the one that does not fire OnSelectedIndexChanged, so the screen's
    // OnNavigationChanged does not run and SelectedMapItem does not follow. See the header.
    struct {
        void* Control;
    } params{controls[icon.navIndex]};
    if (!CallNamedFunction(view.navigator, "SetSelected", &params))
        return false;

    // The half SetSelected leaves undone, and the one that matters: the 3D selection cursor, the
    // pin highlight, the nav-help prompts and — the reason the geoscape has an activation verb at
    // all — what Enter will act on.
    return SetSelectedMapItemRaw(view.screen, icon.mapItem);
}

bool MoveTrayHover(const TrayIcon& icon, bool entering)
{
    if (!icon.handle || icon.placeholder)
        return false;
    return SendIconMouseEvent(icon.handle, entering ? kFxsLMouseIn : kFxsLMouseOut);
}

// ---- dev harness only: raw object access for the /gui and /props dumps -------------------------

#ifdef X2ACCESS_DEVSERVER

// ---- console commands -------------------------------------------------------------------------

std::string ConsoleCommand(const std::string& command)
{
    if (command.empty())
        return "[exec] empty command";

    // WorldInfo.GetWorldInfo() is `native final static` — the one way into the world that needs
    // no object cached across frames, which is exactly the rule about not holding UObject
    // pointers. GetALocalPlayerController is declared on Actor, and WorldInfo is an Actor.
    struct {
        void* ReturnValue;
    } world{};
    if (!CallStaticNative("WorldInfo", "GetWorldInfo", &world) || !world.ReturnValue)
        return "[exec] no WorldInfo (world not up yet?)";

    struct {
        void* ReturnValue;
    } pc{};
    if (!CallNamedFunction(world.ReturnValue, "GetALocalPlayerController", &pc) || !pc.ReturnValue)
        return "[exec] no local PlayerController";

    // Actor.ConsoleCommand(string Command, optional bool bWriteToLog) : string, FUNC_Native.
    // Layout is transcribed from the SDK dump comments and is PACK(4): ReturnValue sits at 0x14,
    // immediately after the bool bitfield with no padding, so the FString pointer is 4-byte
    // aligned. Natural alignment would put it at 0x18 and every call would read garbage.
#pragma pack(push, 4)
    struct ConsoleParams {
        wchar_t* CmdData;  // FString Command    @0x00
        int32_t CmdCount;
        int32_t CmdMax;
        uint32_t bWriteToLog; // bitfield        @0x10
        wchar_t* RetData;  // FString ReturnValue @0x14
        int32_t RetCount;
        int32_t RetMax;
    };
#pragma pack(pop)
    static_assert(sizeof(ConsoleParams) == 0x24, "Actor.ConsoleCommand params must be 0x24 bytes");
    static_assert(offsetof(ConsoleParams, bWriteToLog) == 0x10, "bWriteToLog must sit at 0x10");
    static_assert(offsetof(ConsoleParams, RetData) == 0x14, "ReturnValue FString must sit at 0x14");

    // Safe to hand the engine our own buffer: ProcessEvent (moduleBase+0x143B50, decompiled)
    // shallow-memcpy's the whole parameter block into its alloca'd frame and only ever calls
    // DestroyValue on properties whose Offset is >= ParmsSize — true locals, never parameters.
    // So nothing frees this, and it must merely outlive the call.
    std::wstring wide = Utf8ToWide(command);

    ConsoleParams params{};
    params.CmdData = wide.data();
    params.CmdCount = static_cast<int32_t>(wide.size()) + 1; // FString counts the terminator
    params.CmdMax = params.CmdCount;
    params.bWriteToLog = 1; // also lands in the game's Launch.log, where `just gamelog` finds it

    if (!CallNamedFunction(pc.ReturnValue, "ConsoleCommand", &params))
        return "[exec] ConsoleCommand did not resolve or faulted";

    // The native writes the return FString straight into our block (ProcessEvent passes
    // Parms + ReturnValueOffset as the result pointer), allocated with the ENGINE's allocator.
    // We deliberately leak it: freeing it needs appFree, and a dev-only endpoint leaking a few
    // dozen bytes per call is a far better trade than calling the wrong deallocator.
    std::vector<wchar_t> buf(64 * 1024);
    int len = CopyFStringRaw(&params.RetData, buf.data(), static_cast<int>(buf.size()));
    if (len <= 0)
        return {}; // ran fine, said nothing — most commands are silent
    return WcharToUtf8(buf.data(), len);
}

void* FindStackScreen(const char* className)
{
    if (className && className[0])
        return FindStackScreenRaw(className);

    // No class asked for: the topmost screen the recipes would consider. Index 0 is the top
    // (Push inserts there), but the entry above a live screen can be a corpse the stack has
    // not dropped yet, so skip on the same predicate the finders use rather than taking [0]
    // blind.
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (screens[i] && StackEntryLiveRaw(screens[i]))
            return screens[i];
    }
    return nullptr;
}

int FindInstances(const char* className, void** out, int maxItems)
{
    if (!className || !className[0] || maxItems <= 0)
        return 0;
    UObject* classObj = FindClassRaw(className);
    if (!classObj)
        return 0;

    // Two passes, because matching only the exact class is a trap: asking for XCom3DCursor in a
    // live mission found NOTHING, since the cursor the game spawns is an XCom3DCursorMouse. The
    // script side takes subclasses for granted (`local XCom3DCursor Cursor` binds the subclass
    // fine), so an inspector that does not is wrong in the way that looks like an absent object.
    //
    // Pass 1 collects the class objects that DERIVE from the named one — a few thousand class
    // objects walked, not four hundred thousand instances — so pass 2 stays a pointer compare.
    void* derived[kMaxDerivedClasses] = {};
    int classCount = 0;
    int found = 0;
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return 0;

        derived[classCount++] = classObj;
        char clsName[64];
        for (int32_t i = 0; i < objects->size() && classCount < kMaxDerivedClasses; i++) {
            UObject* obj = objects->at(i);
            if (!obj || obj == classObj)
                continue;
            UClass* meta = obj->Class;
            if (!meta || !ObjNameAnsiRaw(reinterpret_cast<UObject*>(meta), clsName, sizeof(clsName))
                || strcmp(clsName, "Class") != 0)
                continue; // not a class object
            // Bounded: a UE3 class chain is a dozen deep, and a corrupt SuperField must not spin.
            UStruct* super = reinterpret_cast<UStruct*>(obj)->SuperField
                ? reinterpret_cast<UStruct*>(reinterpret_cast<UStruct*>(obj)->SuperField)
                : nullptr;
            for (int step = 0; super && step < 32; step++) {
                if (reinterpret_cast<UObject*>(super) == classObj) {
                    derived[classCount++] = obj;
                    break;
                }
                super = reinterpret_cast<UStruct*>(super->SuperField);
            }
        }

        char name[80];
        for (int32_t i = 0; i < objects->size() && found < maxItems; i++) {
            UObject* obj = objects->at(i);
            if (!obj)
                continue;
            bool isKind = false;
            for (int c = 0; c < classCount; c++) {
                if (reinterpret_cast<UObject*>(obj->Class) == derived[c]) {
                    isKind = true;
                    break;
                }
            }
            if (!isKind)
                continue;
            // The class default object is an instance of its own class and would otherwise be
            // the first hit for every lookup; its name is the one thing that distinguishes it.
            if (ObjNameAnsiRaw(obj, name, sizeof(name)) && strncmp(name, "Default__", 9) == 0)
                continue;
            out[found++] = obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return found;
}

void* FindDefaultObject(const char* className)
{
    if (!className || !className[0])
        return nullptr;
    // A CDO is named Default__<Class>, and that name is exactly what FindInstances excludes it
    // by — so the name IS the lookup here. One pass, oldest first; there is precisely one per
    // class, created at package load.
    std::string expected = std::string("Default__") + className;
    return ScanForObjectNamedRaw(expected.c_str());
}

bool IsLiveObject(void* obj)
{
    if (!obj)
        return false;
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return false;
        for (int32_t i = objects->size() - 1; i >= 0; i--) {
            if (objects->at(i) == reinterpret_cast<UObject*>(obj))
                return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

int ReadChildPanels(void* panel, void** out, int maxItems)
{
    // The cap is the dumper's own buffer, and a /gui listing that silently stops short would
    // send someone hunting for a widget the screen does have.
    return TakeArrayRaw("Dev: ChildPanels", panel, kPanelChildPanelsOffset, out, maxItems);
}

FlashValue FlashCall(void* movie, const char* funcName, const char* path)
{
    FlashValue v;
    if (!funcName || !funcName[0]) {
        v.error = "no function named";
        return v;
    }
    if (!movie) {
        v.error = "no movie to ask (the panel's Movie is null — not inited yet?)";
        return v;
    }

    // Whitelist, because the block below is a LAYOUT and not a guess. The two deliberately
    // excluded neighbours are the ones whose shape differs: GetVariable returns an FASValue
    // struct, and Invoke takes an array of them as a second parameter.
    const std::string fn = funcName;
    if (fn == "GetVariableString" || fn == "ActionScriptString")
        v.kind = FlashValue::Kind::Text;
    else if (fn == "GetVariableNumber" || fn == "ActionScriptFloat")
        v.kind = FlashValue::Kind::Number;
    else if (fn == "ActionScriptInt")
        v.kind = FlashValue::Kind::Int;
    else if (fn == "GetVariableBool")
        v.kind = FlashValue::Kind::Bool;
    else if (fn == "ActionScriptVoid")
        v.kind = FlashValue::Kind::Void;
    else {
        v.error = fn
            + " is not one of the path functions this endpoint knows the return shape of. "
              "Use GetVariableString / GetVariableNumber / GetVariableBool / ActionScriptString "
              "/ ActionScriptInt / ActionScriptFloat / ActionScriptVoid — GetVariable (an "
              "FASValue struct) and Invoke (an array of them) have a different parameter block "
              "and would be misread here";
        return v;
    }

    uint8_t block[64] = {};
    if (!FlashCallRaw(movie, funcName, path ? path : "", block, sizeof(block))) {
        v.error = "GFxMoviePlayer." + fn
            + " could not be resolved or called on that object (is it really a movie?)";
        return v;
    }
    v.called = true;

    switch (v.kind) {
    case FlashValue::Kind::Text: {
        wchar_t buf[4096];
        const int len = CopyFStringRaw(block + kFlashRetOffset, buf, 4096);
        if (len > 0)
            v.text = WcharToUtf8(buf, len);
        break;
    }
    case FlashValue::Kind::Number:
        v.number = *reinterpret_cast<const float*>(block + kFlashRetOffset);
        break;
    case FlashValue::Kind::Int:
        v.number = *reinterpret_cast<const int32_t*>(block + kFlashRetOffset);
        break;
    case FlashValue::Kind::Bool:
        // UBOOL returns arrive as bit 0 of the dword, which is how the generated param struct
        // declares it (`bool ReturnValue : 1`).
        v.number = (*reinterpret_cast<const uint32_t*>(block + kFlashRetOffset) & 1u) ? 1 : 0;
        break;
    case FlashValue::Kind::Void:
        break;
    }
    return v;
}

#endif // X2ACCESS_DEVSERVER

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
