// Game-UI access, the Tactical Legacy Pack's Challenge Archives menu — see the header. Explicit
// offsets from the generated SDK's dump comments, each pinned in sdk_layout_checks.cpp.

#include "game_ui_tle_challenge.h"
#include "game_ui_internal.h"
#include "log.h"

#include <windows.h>

#include <string>

#ifdef _MSC_VER
#pragma pack(push, 4)
#endif

namespace GameUI {

namespace {

// ---- UITLE_ChallengeModeMenu ----
constexpr int32_t kChalListOffset = 0x420;         // List (UIList*)
constexpr int32_t kChalTitleOffset = 0x44C;        // m_ScreenTitle (FString)
constexpr int32_t kChalButtonLabelsOffset = 0x500; // m_strButtonLabels[5] (FString, 0x10 stride)
constexpr int32_t kChalFlagsOffset = 0x550;        // flag dword: m_bFlipSort is bit 0
constexpr uint32_t kChalFlipSort = 0x01;
constexpr int32_t kChalSortTypeOffset = 0x554;     // m_eSortType (byte enum)

// ---- UITLEChallenge_ListItem::Data (FChallengeListItemData at 0x3D8) ----
constexpr int32_t kChalRowDataOffset = 0x3D8;
constexpr int32_t kChalDataIndexOffset = kChalRowDataOffset + 0x00;      // ChallengeListDataIndex
constexpr int32_t kChalDataMissionOffset = kChalRowDataOffset + 0x04;    // MissionName
constexpr int32_t kChalDataObjectiveOffset = kChalRowDataOffset + 0x14;  // MissionObjective
constexpr int32_t kChalDataMapOffset = kChalRowDataOffset + 0x24;        // MapName
constexpr int32_t kChalDataSquadOffset = kChalRowDataOffset + 0x44;      // Squad
constexpr int32_t kChalDataEnemiesOffset = kChalRowDataOffset + 0x54;    // Enemies

} // namespace

void* FindChallengeMenuScreen()
{
    static ScreenFinder sFinder{"UITLE_ChallengeModeMenu"};
    static bool sReported = false;
    void* screen = PreferStack("UITLE_ChallengeModeMenu", sFinder, sReported, ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

ChallengeMenuView ReadChallengeMenu(void* screen)
{
    ChallengeMenuView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.title = CleanText(ReadFStringField(screen, kChalTitleOffset));
    for (int i = 0; i < kChallengeSortColumns; i++)
        v.columns[i] = CleanText(ReadFStringField(screen, kChalButtonLabelsOffset + 0x10 * i));
    v.sortFlipped = (ReadU32Raw(screen, kChalFlagsOffset) & kChalFlipSort) != 0;
    v.sortType = ReadU8Raw(screen, kChalSortTypeOffset);

    // List is spawned in OnInit and its rows only once the LoadingItems state has read every
    // history file (BuildListItems); until then this is an empty view and the recipe waits.
    v.list = ReadPtrRaw(screen, kChalListOffset);
    if (!v.list)
        return v;
    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    // ⛔ Sized from the list, never a fixed cap: the archive is 107 rows, past kMaxListRows, and
    // a dropped tail would read to a blind player as "the archive ends here".
    void* container = ReadPtrRaw(v.list, kListItemContainerOffset);
    if (!container)
        return v;
    std::vector<void*> items
        = ReadPtrArrayAllRaw("ChallengeMenu: rows", container, kPanelChildPanelsOffset);
    v.rows.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
        if (!items[i])
            continue;
        ChallengeRow r;
        r.handle = items[i];
        r.index = static_cast<int>(i);
        r.dataIndex = ReadI32Raw(items[i], kChalDataIndexOffset);
        r.mission = CleanText(ReadFStringField(items[i], kChalDataMissionOffset));
        r.objective = CleanText(ReadFStringField(items[i], kChalDataObjectiveOffset));
        r.map = CleanText(ReadFStringField(items[i], kChalDataMapOffset));
        r.squad = CleanText(ReadFStringField(items[i], kChalDataSquadOffset));
        r.enemies = CleanText(ReadFStringField(items[i], kChalDataEnemiesOffset));
        // A row spawned but not yet Refresh()ed carries an empty Data; RefreshData follows
        // BuildListItems in the same LoadFinished, so this is a one-frame window at most.
        if (r.mission.empty() && r.objective.empty() && r.map.empty())
            continue;
        v.rows.push_back(std::move(r));
    }
    return v;
}

bool SetChallengeMenuIndex(void* screen, int index)
{
    void* list = screen ? ReadPtrRaw(screen, kChalListOffset) : nullptr;
    return list && SetListIndex(list, index);
}

bool ActivateChallengeRow(void* screen, int index)
{
    void* list = screen ? ReadPtrRaw(screen, kChalListOffset) : nullptr;
    if (!list)
        return false;
    // ⛔ Selection first, then the call: OnChallengeClicked takes the index it is handed, but the
    // details panel and the game's own highlight follow SelectedIndex, and the two must agree
    // at the moment the map loads.
    SetListIndex(list, index);
    // OnChallengeClicked(UIList ContainerList, int ListItemIndex) — the screen's Enter branch,
    // verbatim. Padded to the engine's 4-byte pack; pinned against the generated _Params.
    struct {
        void* List;
        int32_t Index;
        int32_t pad;
    } params{list, index, 0};
    return CallNamedFunction(screen, "OnChallengeClicked", &params);
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
