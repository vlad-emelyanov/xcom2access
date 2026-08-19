#include "game_ui.h"
#include "log.h"

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
constexpr int32_t kMechaBGOffset = 0x40C;         // UIMechaListItem::BG (UIPanel*)

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

// ---- new game flow: UIShellSoundtrackPicker ----
constexpr int32_t kSndListOffset = 0x410;           // m_List (UIList*)
constexpr int32_t kSndStartButtonOffset = 0x418;    // m_StartButton (UILargeButton*)

// ---- tactical HUD (UITacticalHUD) ----
constexpr int32_t kTacHudFlagsOffset = 0x410;       // flag dword holding m_isMenuRaised
constexpr uint32_t kTacHudMenuRaised = 0x02;        // m_isMenuRaised (shot HUD up)
constexpr int32_t kSndTitleOffset = 0x430;          // m_chooseSoundtrack (FString)

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

// ---- UIListItemString (the pause menu's rows — a plain text row, not a UIMechaListItem) ----
constexpr int32_t kStringItemTextOffset = 0x3F4;    // Text (FString)
constexpr int32_t kStringItemFlagsOffset = 0x414;   // bDisabled bit 0 (bIsBad/bIsGood/... follow)
constexpr uint32_t kStringItemDisabled = 0x0001;

// ---- UIScreenStack: the game's own list of what is on screen ----
constexpr int32_t kStackFlagsOffset = 0x60;    // UIScreenStack flag dword
constexpr uint32_t kStackInputBlocked = 0x1;   // IsInputBlocked — "ignore input, system is gated"
constexpr uint32_t kStackScreensVisible = 0x2; // ScreensVisible — the whole UI is showing
constexpr uint32_t kStackCinematic = 0x8;      // bCinematicMode — screens hidden for cinematics
constexpr int32_t kStackScreensOffset = 0x6C;  // UIScreenStack::Screens (TArray<UIScreen*>)
constexpr int kMaxStackScreens = 32;

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
int ReadOptionRowPtrsRaw(void* screen, void** out, int maxItems)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(screen) + kOptsMechaItemsOffset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0)
            return 0;
        if (n > maxItems)
            n = maxItems;
        for (int32_t i = 0; i < n; i++)
            out[i] = data[i];
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Any TArray<T*> at `offset`: copies up to maxItems pointers out, indices preserved. Returns
// the count copied, or -1 if the array itself could not be read.
int ReadPtrArrayRaw(const void* obj, int32_t offset, void** out, int maxItems)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(obj) + offset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0)
            return 0;
        if (n > maxItems)
            n = maxItems;
        for (int32_t i = 0; i < n; i++)
            out[i] = data[i];
        return n;
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
int ReadListItemsRaw(void* list, void** out, int maxItems)
{
    if (!list)
        return 0;
    void* container = ReadPtrRaw(list, kListItemContainerOffset);
    if (!container)
        return 0;
    return ReadPtrArrayRaw(container, kPanelChildPanelsOffset, out, maxItems);
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

// The topmost live screen of `className` on the stack, or null if the stack does not hold one.
// Matching is by class NAME, so no UClass has to be resolved first — the stack is at most a few
// dozen entries, which makes the name compares cheaper than the class lookup would be.
void* FindStackScreenRaw(const char* className)
{
    void* stack = gStackFinder.Find();
    if (!stack)
        return nullptr;

    void* screens[kMaxStackScreens] = {};
    int count = ReadPtrArrayRaw(stack, kStackScreensOffset, screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        uint32_t flags = ReadU32Raw(screens[i], kPanelFlagsOffset);
        if (!(flags & kPanelInited) || (flags & kPanelRemoved))
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
    case kLineCheckbox: {
        void* checkbox = ReadPtrRaw(item, kMechaCheckboxOffset);
        out->checked = checkbox && (ReadU32Raw(checkbox, kCheckboxFlagsOffset) & kCheckboxChecked);
        break;
    }
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

// Every visible row of a UIList, indices preserved.
std::vector<OptionRow> ReadListRows(void* list)
{
    std::vector<OptionRow> rows;
    void* items[64] = {};
    int count = ReadListItemsRaw(list, items, 64);
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

    void* rows[64] = {};
    int rowCount = ReadOptionRowPtrsRaw(screen, rows, 64);
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

std::string ReadPanelTooltip(void* panel)
{
    if (!panel || !(ReadU32Raw(panel, kPanelFlagsOffset) & kPanelHasTooltip))
        return {};
    int32_t id = ReadI32Raw(panel, kPanelTooltipIdOffset);

    void* movie = ReadPtrRaw(panel, kPanelMovieOffset);
    void* pres = movie ? ReadPtrRaw(movie, kMoviePresOffset) : nullptr;
    void* mgr = pres ? ReadPtrRaw(pres, kPresTooltipMgrOffset) : nullptr;
    if (!mgr)
        return {};

    // SetTooltipText path: a data element in the manager's shared TextTooltip.
    if (const uint8_t* el = FindTextTooltipDataRaw(mgr, id)) {
        std::string body = ReadFStringField(el, kTTDataBodyOffset);
        std::string title = ReadFStringField(el, kTTDataTitleOffset);
        if (title.empty())
            return body;
        if (body.empty())
            return title;
        return title + ". " + body;
    }

    // Preformed path: a real UITextTooltip object in the Tooltips array. Only text tooltips
    // carry sTitle/sBody at these offsets; other subclasses (ability tooltips) would read
    // garbage, so the class check gates the read.
    void* tip = FindTooltipByIdRaw(mgr, id);
    if (!tip || !ObjectClassNamedRaw(tip, "UITextTooltip"))
        return {};

    std::string body = ReadFStringField(tip, kTextTooltipBodyOffset);
    std::string title = ReadFStringField(tip, kTextTooltipTitleOffset);
    if (title.empty())
        return body;
    if (body.empty())
        return title;
    return title + ". " + body;
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

bool (*const kTrackedUsable[kTrackedScreenCount])(void*) = {
    IsFocusedRaw, IsFocusedRaw, IsFocusedRaw, IsFocusedRaw, DialogIsShowingRaw, IsFocusedRaw,
    IsShownRaw, IsFocusedRaw,
};

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

bool ToggleSecondWaveOption(void* screen, int index)
{
    void* list = ReadPtrRaw(screen, kDiffSWListOffset);
    if (!list || index < 0)
        return false;
    // The screen's own delegate target, called exactly as UIList's mouse path calls it:
    // toggles the row's checkbox AND clears the two rival faction-HQ options.
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
    void* infos[32] = {};
    int infoCount = ReadPtrArrayRaw(screen, kNarrDLCInfosOffset, infos, 32);
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
    v.title = CleanText(ReadFStringField(screen, kSndTitleOffset));
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
    void* items[64] = {};
    int count = ReadListItemsRaw(list, items, 64);
    for (int i = 0; i < count; i++) {
        MenuItem item;
        item.handle = items[i];
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
    return SetListIndex(ReadPtrRaw(screen, kPauseListOffset), index);
}

bool ActivatePauseMenuItem(void* screen, int index)
{
    void* list = ReadPtrRaw(screen, kPauseListOffset);
    if (!list)
        return false;
    // OnChildClicked is the screen's own click target: it sets m_iCurrentSelection (which its
    // switch reads, so poking the list alone would activate the WRONG entry), plays the menu
    // sound, and runs the confirmation dialogs. The parameter block matches its signature
    // (UIList*, int) padded to the engine's 4-byte pack.
    struct {
        void* ContainerList;
        int32_t ItemIndex;
        int32_t pad;
    } params{list, index, 0};
    return CallNamedFunction(screen, "OnChildClicked", &params);
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

// ---- tactical HUD (UITacticalHUD) — tile reader support ----

void* FindTacticalHUD()
{
    return FindScreen(kScrTacticalHUD);
}

bool TacticalHUDMenuRaised(void* screen)
{
    return screen && (ReadU32Raw(screen, kTacHudFlagsOffset) & kTacHudMenuRaised) != 0;
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
    void* stack = gStackFinder.Find();
    if (!stack)
        return gate;

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
        if (strcmp(name, "UITacticalHUD") == 0) {
            gate.hudOnStack = true;
            gate.menuRaised
                = (ReadU32Raw(screens[i], kTacHudFlagsOffset) & kTacHudMenuRaised) != 0;
            return gate;
        }
        if (!IsTacticalPassive(name)) {
            // Whatever this is owns the surface — the cursor stands down under it whether or
            // not a HUD sits further below.
            gate.blocked = true;
            strncpy_s(gate.blocker, name, _TRUNCATE);
            return gate;
        }
    }
    return gate; // no tactical HUD on the stack: not in tactical
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

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
