// The TACTICAL LEGACY PACK family's readers (shell half): the Legacy Operations menu, its
// difficulty page and the soldier loadout popup. The hub stays in game_ui_shell.cpp beside the
// main menu it hangs off. Raw primitives come from game_ui.cpp behind game_ui_internal.h.

#include "game_ui_tle.h"
#include "game_ui_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

// Hand-rolled ProcessEvent parameter blocks below must match the engine's pack(4) layout.
#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GameUI {

namespace {

// Engine field offsets, from the generated SDK's reflection-dump comments; every one is pinned
// in sdk_layout_checks.cpp. The ladder menu's set was also read LIVE through `just dev-props`
// on 2026-09-17 (List 0x420, NarrativeList 0x428, ClickedList 0x430, clickedIndex 0x448, the
// strings from 0x4B0) and matched the dump.

// ---- UITLE_LadderModeMenu ----
constexpr int32_t kLadderListOffset = 0x420;            // List (procedural ladders)
constexpr int32_t kLadderNarrativeListOffset = 0x428;   // NarrativeList
constexpr int32_t kLadderClickedListOffset = 0x430;     // ClickedList
constexpr int32_t kLadderClickedIndexOffset = 0x448;    // clickedIndex
constexpr int32_t kLadderSoldierButtonsOffset = 0x498;  // LadderSoldierInfoButtons (TArray<UIButton*>)
constexpr int32_t kLadderTitleOffset = 0x4B0;           // m_ScreenTitle
constexpr int32_t kLadderAbandonOffset = 0x4C0;         // m_strAbandonLadder
constexpr int32_t kLadderDeleteOffset = 0x4D0;          // m_strDeleteLadder
constexpr int32_t kLadderBeginOffset = 0x4E0;           // m_strBeginLadder
constexpr int32_t kLadderContinueOffset = 0x4F0;        // m_strContinueLadder
constexpr int32_t kLadderMedalsOffset = 0x530;          // m_Medals
constexpr int32_t kLadderRandomHeaderOffset = 0x5A0;    // m_RandomLadderHeader
constexpr int32_t kLadderNarrativeHeaderOffset = 0x5C0; // m_NarrativeLaddersHeader

// UITLENarrativeLadder_ListItem
constexpr int32_t kNarrativeRowFlagsOffset = 0x3E0;     // bDisabled : 1
constexpr uint32_t kNarrativeRowDisabled = 0x0001;

// UIListItemString (the procedural rows) — the same pair the pause menu reads.
constexpr int32_t kStringRowTextOffset = kListItemStringTextOffset;   // 0x3F4
constexpr int32_t kStringRowFlagsOffset = kListItemStringFlagsOffset; // 0x414
constexpr uint32_t kStringRowDisabled = 0x0001;

// UIUtilities_Input.FXS_L_MOUSE_UP — what OnClickSoldierInfo tests its command against.
constexpr int32_t kCmdLeftMouseUp = 391;

// ---- UITLE_LadderDifficulty ----
constexpr int32_t kLDiffRowOffsets[4] = {0x420, 0x428, 0x430, 0x438}; // Rookie..Legend mecha items
constexpr int32_t kLDiffOptionOffsets[2] = {0x448, 0x450};            // narrative VO, Ironman
constexpr int32_t kLDiffStartButtonOffset = 0x468;                    // m_StartButton
constexpr int32_t kLDiffSelectedOffset = 0x470;                       // m_iSelectedDifficulty

// ---- UITLEUnitStats ----
constexpr int32_t kUnitStatsArrayOffset = 0x438;   // m_UnitArray (TArray<XComGameState_Unit*>)
constexpr int32_t kUnitStatsIndexOffset = 0x450;   // m_CurrentIndex
constexpr int32_t kUnitStatsTitleOffset = 0x454;   // m_ScreenTitle

// The rows of one list, in list order. Narrative rows carry only their disabled bit (the name
// is on the state object the row retains — the companion's half); string rows carry their Text.
void ReadLadderList(const char* what, void* list, bool stringRows, std::vector<LadderRow>* out)
{
    if (!list)
        return;
    // Sized from the list: the procedural side grows with every ladder the player generates.
    // GetItem(i) IS ItemContainer.ChildPanels[i]; sized from the array, never capped.
    std::vector<void*> items = ReadPtrArrayAllRaw(what, ReadPtrRaw(list, kListItemContainerOffset),
        kPanelChildPanelsOffset);
    for (size_t i = 0; i < items.size(); i++) {
        void* row = items[i];
        if (!row)
            continue;
        LadderRow r;
        r.handle = row;
        r.index = static_cast<int>(i);
        if (stringRows) {
            r.label = CleanText(ReadFStringField(row, kStringRowTextOffset));
            r.disabled = (ReadU32Raw(row, kStringRowFlagsOffset) & kStringRowDisabled) != 0;
        } else {
            r.disabled = (ReadU32Raw(row, kNarrativeRowFlagsOffset) & kNarrativeRowDisabled) != 0;
        }
        out->push_back(std::move(r));
    }
}

} // namespace

// ---- the Legacy Operations menu (UITLE_LadderModeMenu) ----

void* FindLadderMenuScreen()
{
    static ScreenFinder sFinder{"UITLE_LadderModeMenu"};
    static bool sReported = false;
    void* screen = PreferStack("UITLE_LadderModeMenu", sFinder, sReported, ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

LadderMenuView ReadLadderMenu(void* screen)
{
    LadderMenuView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.title = CleanText(ReadFStringField(screen, kLadderTitleOffset));
    v.narrativeHeader = CleanText(ReadFStringField(screen, kLadderNarrativeHeaderOffset));
    v.randomHeader = CleanText(ReadFStringField(screen, kLadderRandomHeaderOffset));
    v.beginLabel = CleanText(ReadFStringField(screen, kLadderBeginOffset));
    v.continueLabel = CleanText(ReadFStringField(screen, kLadderContinueOffset));
    v.abandonLabel = CleanText(ReadFStringField(screen, kLadderAbandonOffset));
    v.deleteLabel = CleanText(ReadFStringField(screen, kLadderDeleteOffset));
    v.medalsLabel = CleanText(ReadFStringField(screen, kLadderMedalsOffset));

    v.narrativeList = ReadPtrRaw(screen, kLadderNarrativeListOffset);
    v.randomList = ReadPtrRaw(screen, kLadderListOffset);
    ReadLadderList("LadderMenu: narrative rows", v.narrativeList, false, &v.narrative);
    ReadLadderList("LadderMenu: procedural rows", v.randomList, true, &v.random);
    v.clickedList = ReadPtrRaw(screen, kLadderClickedListOffset);
    v.clickedIndex = ReadI32Raw(screen, kLadderClickedIndexOffset);

    std::vector<void*> buttons
        = ReadPtrArrayAllRaw("LadderMenu: soldier buttons", screen, kLadderSoldierButtonsOffset);
    for (size_t i = 0; i < buttons.size(); i++) {
        void* btn = buttons[i];
        if (!btn)
            continue;
        // UpdateData Show()s one button per squad member and Hide()s the rest: here the
        // visible bit is the game's own statement of how many soldiers the panel draws.
        if (!(ReadU32Raw(btn, kPanelFlagsOffset) & kPanelVisible))
            continue;
        v.soldierButtons.emplace_back(static_cast<int>(i), btn);
    }
    return v;
}

namespace {

// OnSelectedChange(UIList ContainerList, int ItemIndex) / OnLadderClicked(same): the two
// entry points the screen's own arrow and Enter handlers call with the selected list and index.
// Padded to the engine's 4-byte pack; the trailing locals are the engine's, not parameters.
struct LadderListIndexParams {
    void* List;
    int32_t Index;
    int32_t pad;
};

bool CallLadderListFunction(void* screen, const char* func, void* list, int index)
{
    if (!screen || !list)
        return false;
    LadderListIndexParams params{list, index, 0};
    return CallNamedFunction(screen, func, &params);
}

} // namespace

bool SelectLadderRow(const LadderMenuView& view, bool random, int index)
{
    void* list = random ? view.randomList : view.narrativeList;
    void* other = random ? view.narrativeList : view.randomList;
    const std::vector<LadderRow>& rows = random ? view.random : view.narrative;
    if (!view.screen || !list || index < 0 || index >= static_cast<int>(rows.size()))
        return false;
    // The game's own path: the row selects itself up the navigator chain (row → list → left
    // column → screen), which is what a hover does; the other list is left with no selection so
    // the two highlights never both show; then the handler its arrow keys call.
    SetListIndex(other, -1);
    if (!FocusButton(rows[index].handle))
        SetListIndex(list, index);
    return CallLadderListFunction(view.screen, "OnSelectedChange", list, index);
}

bool ActivateLadderRow(const LadderMenuView& view, bool random, int index)
{
    void* list = random ? view.randomList : view.narrativeList;
    if (!SelectLadderRow(view, random, index))
        return false;
    return CallLadderListFunction(view.screen, "OnLadderClicked", list, index);
}

bool LadderAbandon(void* screen)
{
    if (!screen)
        return false;
    uint8_t params[16] = {}; // no parameters (the dialog struct is a local, not a parameter)
    return CallNamedFunction(screen, "AbandonLadderPopup", params);
}

bool LadderDelete(void* screen)
{
    if (!screen)
        return false;
    uint8_t params[16] = {};
    return CallNamedFunction(screen, "DeleteLadderPopup", params);
}

bool OpenLadderSoldierInfo(void* screen, void* button)
{
    if (!screen || !button)
        return false;
    // OnClickSoldierInfo(UIPanel Panel, int Cmd): resolves the slot by comparing Panel against
    // its six buttons and opens the popup only on FXS_L_MOUSE_UP.
    LadderListIndexParams params{button, kCmdLeftMouseUp, 0};
    return CallNamedFunction(screen, "OnClickSoldierInfo", &params);
}

// ---- the ladder difficulty page (UITLE_LadderDifficulty) ----

void* FindLadderDifficultyScreen()
{
    static ScreenFinder sFinder{"UITLE_LadderDifficulty"};
    static bool sReported = false;
    void* screen = PreferStack("UITLE_LadderDifficulty", sFinder, sReported, ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

LadderDifficultyView ReadLadderDifficulty(void* screen)
{
    LadderDifficultyView v;
    if (!screen)
        return v;
    v.selectedDifficulty = ReadI32Raw(screen, kLDiffSelectedOffset);
    for (int i = 0; i < 4; i++) {
        OptionRow row;
        if (MakeRow(ReadPtrRaw(screen, kLDiffRowOffsets[i]), i, &row))
            v.difficulties.push_back(std::move(row));
    }
    // Both hang off m_BottomPanel, not a list: index -1, SetSelectedNavigation write-back. The
    // narrative-VO row is Remove()d on a procedural ladder; MakeRow reads an empty label off the
    // corpse and refuses it, which is the right answer.
    for (int32_t offset : kLDiffOptionOffsets) {
        OptionRow row;
        if (MakeRow(ReadPtrRaw(screen, offset), -1, &row))
            v.options.push_back(std::move(row));
    }
    v.start = ReadButtonField(screen, kLDiffStartButtonOffset);
    return v;
}

// ---- the soldier loadout popup (UITLEUnitStats) ----

void* FindUnitStatsScreen()
{
    static ScreenFinder sFinder{"UITLEUnitStats"};
    static bool sReported = false;
    void* screen = PreferStack("UITLEUnitStats", sFinder, sReported, ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

UnitStatsView ReadUnitStats(void* screen)
{
    UnitStatsView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.title = CleanText(ReadFStringField(screen, kUnitStatsTitleOffset));
    v.index = ReadI32Raw(screen, kUnitStatsIndexOffset);
    v.count = static_cast<int>(
        ReadPtrArrayAllRaw("UnitStats: units", screen, kUnitStatsArrayOffset).size());
    return v;
}

bool UnitStatsStep(void* screen, int sign)
{
    if (!screen || sign == 0)
        return false;
    uint8_t params[16] = {};
    return CallNamedFunction(screen, sign > 0 ? "NextSoldier" : "PrevSoldier", params);
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
