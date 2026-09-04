// The SHELL family's readers: main menu, settings, the new-game flow, the pause menu, the modal
// dialog, save/load and the text-input dialog. Split out of game_ui.cpp along the recipe-family
// axis; the raw primitives it is built on stay in game_ui.cpp behind game_ui_internal.h.

#include "game_ui_shell.h"
#include "game_ui_internal.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <string>

// Hand-rolled ProcessEvent parameter blocks below must match the engine's pack(4) layout.
#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GameUI {

namespace {

// Engine field offsets, from the generated SDK's reflection-dump comments (see game_ui.cpp's
// provenance note; every one is pinned in sdk_layout_checks.cpp). Core set validated live
// (ReadProcessMemory against a running WOTC shell, 2026-08-18): the flags dword read 0x279/0xA7B
// with expected bits; MAINMENU held 6 buttons; Text read the localized "НОВАЯ ИГРА". The
// shell-extras set (ticker, My2K, tooltip, attention mask) comes from the same dump; every read
// through it is SEH-guarded and gated on the target's panel-visible bit.
constexpr int32_t kShellMainMenuOffset = 0x4CC; // UIShell::MAINMENU (TArray<UIButton*>)
constexpr int32_t kShellTickerOffset = 0x4F4;   // UIShell::TickerText (UIScrollingText*)
constexpr int32_t kShellMy2KOffset = 0x5C8;     // UIFinalShell::m_my2KButton (UIButton*)
constexpr int32_t kShellLiveClient = 0x608;     // UIFinalShell::FiraxisLiveClient

// UIOptionsPCScreen (the settings screen), same reflection-dump provenance. The tab-name strings
// are NOT in tab-index order in the class: the enum is Audio(0), Video(1), Graphics(2),
// Gameplay(3), Interface(4) while the strings declare Video, Graphics, Audio, Gameplay,
// Interface — kOptsTabNameOffsets below is in ENUM order.
constexpr int32_t kOptsTitleOffset = 0x42C;       // m_strTitle (FString)
constexpr int32_t kOptsTabNameOffsets[5] = {0x4DC, 0x4BC, 0x4CC, 0x4EC, 0x4FC};
constexpr int32_t kOptsListOffset = 0x140C;       // List (UIList*)
constexpr int32_t kOptsMechaItemsOffset = 0x1414; // m_arrMechaItems (TArray<UIMechaListItem*>)
constexpr int32_t kOptsCurrentTabOffset = 0x1424; // m_iCurrentTab (int)
constexpr int32_t kOptsGPUButtonOffset = 0x1434;  // GPUAutoDetectButton (UIButton*)
constexpr int32_t kOptsSaveButtonOffset = 0x1444; // SaveAndExitButton (UIButton*)
constexpr int32_t kOptsExitButtonOffset = 0x144C; // ExitWitoutChangesButton (UIButton*)
constexpr int32_t kOptsCreditsButtonOffset = 0x1454; // CreditsButton (UIButton*)

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

// ---- settings screen (UIOptionsPCScreen) ----

void* FindOptionsScreen()
{
    static ScreenFinder sFinder{"UIOptionsPCScreen"};
    static bool sReported = false;
    return PreferStack("UIOptionsPCScreen", sFinder, sReported);
}

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

bool SetOptionsListIndex(void* screen, int index)
{
    return SetListIndex(ReadPtrRaw(screen, kOptsListOffset), index);
}

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

// ---- the character pool (UICharacterPool) and its pool files — docs/customize-recon.md §3 ----

namespace {

// UICharacterPool, from the dump; pinned in sdk_layout_checks.cpp.
constexpr int32_t kPoolListOffset = 0x420;         // List (UIList*)
constexpr int32_t kPoolTitleHeaderOffset = 0x428;  // TitleHeader (UIX2PanelHeader*)
// Create, Import, Export, Delete, SelectAll, DeselectAll — spawned only when the mouse is active,
// which on PC it is; each stays null otherwise.
constexpr int32_t kPoolButtonOffsets[6] = {0x430, 0x438, 0x440, 0x448, 0x450, 0x458};
constexpr int32_t kPoolEditLabelOffset = 0x4D4;    // m_strEditCharacter (FString)
constexpr int32_t kPoolNoCharactersOffset = 0x554; // m_strNoCharacters (FString)
constexpr int32_t kPoolOptionsListOffset = 0x5F8;  // OptionsList (UIList*) — the usage dropdown

// UICharacterPool_ListPools.
constexpr int32_t kPoolFilesListOffset = 0x420;        // List
constexpr int32_t kPoolFilesTitleHeaderOffset = 0x430; // TitleHeader
constexpr int32_t kPoolFilesFlagsOffset = 0x468;       // bIsExporting / bHasSelectedImportLocation
constexpr uint32_t kPoolFilesExporting = 0x01;
constexpr uint32_t kPoolFilesInFile = 0x02;

// UIX2PanelHeader retains what SetText was handed.
constexpr int32_t kPanelHeaderTitleOffset = 0x3D8; // Title (FString)
constexpr int32_t kPanelHeaderLabelOffset = 0x3E8; // Label (FString)

void ReadPanelHeader(void* screen, int32_t offset, std::string* title, std::string* subtitle)
{
    void* header = ReadPtrRaw(screen, offset);
    if (!header)
        return;
    *title = ReadFStringField(header, kPanelHeaderTitleOffset);
    *subtitle = ReadFStringField(header, kPanelHeaderLabelOffset);
}

} // namespace

void* FindCharacterPoolScreen()
{
    static ScreenFinder sFinder{"UICharacterPool"};
    static bool sReported = false;
    void* screen = PreferStack("UICharacterPool", sFinder, sReported);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

CharacterPoolView ReadCharacterPool(void* screen)
{
    CharacterPoolView v;
    if (!screen)
        return v;
    v.screen = screen;
    ReadPanelHeader(screen, kPoolTitleHeaderOffset, &v.title, &v.subtitle);
    v.editLabel = ReadFStringField(screen, kPoolEditLabelOffset);
    v.emptyLabel = ReadFStringField(screen, kPoolNoCharactersOffset);
    v.list = ReadPtrRaw(screen, kPoolListOffset);
    if (v.list) {
        v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);
        // Sized from the list: a pool holds as many soldiers as the player has made.
        v.rows = ReadListRowsAll("CharacterPool: rows", v.list);
    }
    for (int32_t offset : kPoolButtonOffsets) {
        MenuItem btn = ReadButtonField(screen, offset);
        if (btn.handle)
            v.buttons.push_back(std::move(btn));
    }
    if (void* options = ReadPtrRaw(screen, kPoolOptionsListOffset)) {
        std::vector<OptionRow> rows = ReadListRows(options);
        if (!rows.empty())
            v.usage = rows.front();
    }
    return v;
}

bool SetCharacterPoolSelection(const CharacterPoolView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ToggleCharacterPoolRow(const CharacterPoolView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    SetListIndex(view.list, index);
    return InvokeScreenVerb(view.screen, "SimulateMouseClickOnCheckbox");
}

bool EditCharacterPoolRow(const CharacterPoolView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    // EditSoldier reads List.GetSelectedItem() — the accept-handler-reads-its-own-selection
    // rule, so the selection lands first.
    SetListIndex(view.list, index);
    return InvokeScreenVerb(view.screen, "EditSoldier");
}

void* FindPoolFilesScreen()
{
    static ScreenFinder sFinder{"UICharacterPool_ListPools"};
    static bool sReported = false;
    void* screen = PreferStack("UICharacterPool_ListPools", sFinder, sReported);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

PoolFilesView ReadPoolFiles(void* screen)
{
    PoolFilesView v;
    if (!screen)
        return v;
    v.screen = screen;
    ReadPanelHeader(screen, kPoolFilesTitleHeaderOffset, &v.title, &v.subtitle);
    const uint32_t flags = ReadU32Raw(screen, kPoolFilesFlagsOffset);
    v.exporting = (flags & kPoolFilesExporting) != 0;
    v.inFile = (flags & kPoolFilesInFile) != 0;
    v.list = ReadPtrRaw(screen, kPoolFilesListOffset);
    if (v.list) {
        v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);
        // Sized from the list: inside a file the rows are that file's characters.
        v.rows = ReadListRowsAll("PoolFiles: rows", v.list);
    }
    return v;
}

bool SetPoolFilesSelection(const PoolFilesView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivatePoolFilesRow(const PoolFilesView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    SetListIndex(view.list, index);
    // OnClickLocal(UIList _list, int iItemIndex): the list's own OnItemClicked target, and what
    // OnAccept's controller branch calls with the selection. Padded to the engine's 4-byte pack.
    struct {
        void* List;
        int32_t Index;
        int32_t pad;
    } params{view.list, index, 0};
    return CallNamedFunction(view.screen, "OnClickLocal", &params);
}

bool DeletePoolFilesRow(const PoolFilesView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    SetListIndex(view.list, index);
    return SendScreenCommand(view.screen, kCmdButtonY);
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
