// Game-UI access, the Avenger Report (UIFacilitySummary) — see the header. Explicit offsets from
// the generated SDK's dump comments, each pinned in sdk_layout_checks.cpp.

#include "game_ui_facility_summary.h"
#include "game_ui_internal.h"
#include "flash_shadow.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _MSC_VER
#pragma pack(push, 4)
#endif

namespace GameUI {

namespace {

// ---- UIFacilitySummary ----
constexpr int32_t kFsFlagsOffset = 0x410;       // flag dword: m_bFlipSort is bit 0
constexpr uint32_t kFsFlipSort = 0x01;
constexpr int32_t kFsSortTypeOffset = 0x414;    // m_eSortType (byte enum)
constexpr int32_t kFsHeaderOffset = 0x418;      // m_kHeader (UIPanel*) — parent of the sort buttons
constexpr int32_t kFsListOffset = 0x428;        // m_kList (UIList*)
constexpr int32_t kFsTitleOffset = 0x45C;       // m_strTitle (FString)

// ---- UIFacilitySummary_HeaderButton ----
constexpr int32_t kFsHeaderSortTypeOffset = 0x4CC; // SortType (byte enum)
constexpr int32_t kFsHeaderLabelsOffset = 0x4D8;   // m_strButtonLabels[5] (FString, 0x10 stride)

// ---- UIFacilitySummary_ListItem ----
constexpr int32_t kFsRowFacilityRefOffset = 0x3D8; // FacilityRef.ObjectID (int)
constexpr int32_t kFsRowTemplateNameOffset = 0x3DC; // m_FacilityName (FName)

// UIUtilities_Input.FXS_L_MOUSE_UP — what the header button's OnMouseEvent tests for.
constexpr int32_t kFxsLMouseUp = 391;

// setData's parameter order (UIFacilitySummary_ListItem.uc:108-116). Read by POSITION, the
// capture's standing rule.
enum FacilityRowArg {
    kArgName = 0,
    kArgStaff1 = 1,
    kArgStaff2 = 2,
    kArgPower = 3,
    kArgBuilt = 4,
    kArgStatus = 5,
    kArgQueue = 6,
};

// Arg 0 is `DisplayName @ "(" $ m_strCreditsPrefix $ UpkeepCost $ ")"` (UpdateData:36-42), so the
// upkeep is a trailing parenthesis holding a short non-digit prefix and then digits. Split it off
// so the recipe can say what the number IS; a name that does not end that way is left whole.
void SplitUpkeep(std::string& name, int& upkeep)
{
    upkeep = -1;
    if (name.size() < 3 || name.back() != ')')
        return;
    const size_t open = name.rfind('(');
    if (open == std::string::npos)
        return;
    const size_t close = name.size() - 1;
    size_t digits = close;
    while (digits > open + 1 && name[digits - 1] >= '0' && name[digits - 1] <= '9')
        digits--;
    // Digits must be present, and the prefix before them short (the "§" glyph, 2 bytes of UTF-8)
    // and free of spaces, so a name that merely ends in a parenthesis is never cut.
    const size_t prefixLen = digits - (open + 1);
    if (digits == close || prefixLen > 8)
        return;
    for (size_t i = open + 1; i < digits; i++)
        if (name[i] == ' ')
            return;
    upkeep = atoi(name.c_str() + digits);
    size_t end = open;
    while (end > 0 && name[end - 1] == ' ')
        end--;
    name.resize(end);
}

std::vector<void*> ReadHeaderButtons(void* screen)
{
    std::vector<void*> out;
    void* header = ReadPtrRaw(screen, kFsHeaderOffset);
    if (!header)
        return out;
    for (void* child :
        ReadPtrArrayAllRaw("FacilitySummary: headers", header, kPanelChildPanelsOffset)) {
        if (child && IsKindOfRaw(child, "UIFacilitySummary_HeaderButton"))
            out.push_back(child);
    }
    return out;
}

} // namespace

void* FindFacilitySummaryScreen()
{
    static ScreenFinder sFinder{"UIFacilitySummary"};
    static bool sReported = false;
    void* screen = PreferStack("UIFacilitySummary", sFinder, sReported, ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

FacilitySummaryView ReadFacilitySummary(void* screen)
{
    FacilitySummaryView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.title = CleanText(ReadFStringField(screen, kFsTitleOffset));
    v.sortFlipped = (ReadU32Raw(screen, kFsFlagsOffset) & kFsFlipSort) != 0;
    v.sortType = ReadU8Raw(screen, kFsSortTypeOffset);

    // The header strip. Each button pushes m_strButtonLabels[SortType] through setLabel, and keeps
    // the whole localized array, so the word is read off the widget; the capture of that same push
    // is the fallback for a CDO whose array came back empty (the two-classes-one-populated trap).
    for (void* button : ReadHeaderButtons(screen)) {
        FacilitySortHeader h;
        h.handle = button;
        h.sortType = ReadU8Raw(button, kFsHeaderSortTypeOffset);
        if (h.sortType < 0 || h.sortType >= kFacilitySortColumns)
            continue;
        h.label = CleanText(ReadFStringField(button, kFsHeaderLabelsOffset + 0x10 * h.sortType));
        if (h.label.empty()) {
            FlashShadow::Call call;
            const int32_t index = FlashShadow::PanelCacheIndex(button);
            if (index >= 0 && FlashShadow::GetCall(index, "setLabel", call))
                h.label = FlashArg(call.StringArgs(), 0);
        }
        v.columns[h.sortType] = h.label;
        v.headers.push_back(std::move(h));
    }

    v.list = ReadPtrRaw(screen, kFsListOffset);
    if (!v.list)
        return v;
    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    // ⛔ Sized from the list: every facility the Avenger has ever built is a row, and nothing in
    // the game bounds that below kMaxListRows once a mod adds facilities.
    void* container = ReadPtrRaw(v.list, kListItemContainerOffset);
    if (!container)
        return v;
    std::vector<void*> items
        = ReadPtrArrayAllRaw("FacilitySummary: rows", container, kPanelChildPanelsOffset);
    v.rows.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
        if (!items[i])
            continue;
        FacilitySummaryRow r;
        r.handle = items[i];
        r.index = static_cast<int>(i);
        r.objectId = ReadI32Raw(items[i], kFsRowFacilityRefOffset);
        char templateName[64] = {};
        if (FNameAtAnsiRaw(static_cast<const uint8_t*>(items[i]) + kFsRowTemplateNameOffset,
                templateName, sizeof(templateName)))
            r.commandersQuarters = strcmp(templateName, "CommandersQuarters") == 0;

        // Each row paints itself with its OWN MC (SetData calls mc.BeginFunctionOp), so the cache
        // index is the row's. The rows are respawned by every UpdateData (a sort, a return from a
        // room), and the fresh rows' push lands on the next UI flush — until then they read as
        // not captured.
        const int32_t index = FlashShadow::PanelCacheIndex(items[i]);
        FlashShadow::Call call;
        if (index >= 0 && FlashShadow::GetCall(index, "setData", call)) {
            const std::vector<std::string> args = call.StringArgs();
            r.captured = true;
            r.name = FlashArg(args, kArgName);
            SplitUpkeep(r.name, r.upkeep);
            r.staff1 = FlashArg(args, kArgStaff1);
            r.staff2 = FlashArg(args, kArgStaff2);
            r.power = FlashArg(args, kArgPower);
            r.built = FlashArg(args, kArgBuilt);
            r.status = FlashArg(args, kArgStatus);
            r.queue = FlashArg(args, kArgQueue);
        }
        v.rows.push_back(std::move(r));
    }
    return v;
}

bool SetFacilitySummaryIndex(void* screen, int index)
{
    void* list = screen ? ReadPtrRaw(screen, kFsListOffset) : nullptr;
    return list && SetListIndex(list, index);
}

bool ActivateFacilitySummaryRow(void* screen, int index)
{
    void* list = screen ? ReadPtrRaw(screen, kFsListOffset) : nullptr;
    if (!list)
        return false;
    void* container = ReadPtrRaw(list, kListItemContainerOffset);
    if (!container)
        return false;
    std::vector<void*> items
        = ReadPtrArrayAllRaw("FacilitySummary: rows", container, kPanelChildPanelsOffset);
    if (index < 0 || index >= static_cast<int>(items.size()) || !items[index])
        return false;
    // ⛔ Selection first: the screen's nav help and its own Enter both read GetSelectedItem().
    SetListIndex(list, index);
    // ⛔ Enter goes to the ROW, not the screen or the list. The screen's OnUnrealCommand hands
    // Enter to the selected row and then falls through to super, whose navigator reaches the list,
    // whose own Enter branch calls the row AGAIN and then OnItemClicked — three SelectFacility
    // calls for one keypress, each opening the room. The row's own handler is the first of those
    // and the only one that tests for the Commander's Quarters.
    return SendRowCommand(items[index], kCmdEnter);
}

bool ClickFacilitySortHeader(void* screen, int sortType)
{
    if (!screen)
        return false;
    for (void* button : ReadHeaderButtons(screen)) {
        if (ReadU8Raw(button, kFsHeaderSortTypeOffset) != sortType)
            continue;
        // UIFacilitySummary_HeaderButton.OnMouseEvent(int Cmd, array<string> args) — the mouse's
        // own path, the only one the keyboard has (the D-pad column switch is controller-only and
        // moves the arrow without re-sorting). A zeroed empty TArray we own: the frame copy is
        // shallow and the destroy pass skips parameters, and the handler never reads it. Pinned
        // against the generated _Params in sdk_layout_checks.cpp.
        struct {
            int32_t Cmd;
            void* ArgsData;
            int32_t ArgsCount;
            int32_t ArgsMax;
        } params{kFxsLMouseUp, nullptr, 0, 0};
        return CallNamedFunction(button, "OnMouseEvent", &params);
    }
    return false;
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
