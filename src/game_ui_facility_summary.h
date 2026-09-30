#pragma once

// Game-UI access, the Avenger Report: UIFacilitySummary, the Commander's Quarters' "Avenger status"
// table — every facility on the ship in one sortable list, the screen recipe_facility_summary.cpp
// reads. Implemented in game_ui_facility_summary.cpp. Recon: docs/avenger-view-recon.md §4 (the
// paragraph on UIFacilitySummary); rules: docs/surfaces/avenger.md, "Avenger Report".
//
// THE ROWS RETAIN NO TEXT — the house style. UIFacilitySummary_ListItem.UpdateData computes seven
// strings and hands them to Flash through one `setData` push (name + upkeep, staff slot 1, staff
// slot 2, power, construction date, status, queue); the widget keeps only FacilityRef and the
// template name. So the words come from the Flash capture of that push, which is exactly what a
// sighted player reads, and nothing is re-derived in the companion.
//
// The five sort headers DO retain their words: each UIFacilitySummary_HeaderButton carries the
// whole localized m_strButtonLabels array and pushes the entry for its own SortType.

#include "game_ui_core.h"

namespace GameUI {

/// One facility row (UIFacilitySummary_ListItem), its cells as the row pushed them.
struct FacilitySummaryRow {
    void* handle = nullptr;  // UIFacilitySummary_ListItem
    int index = 0;           // the list's own item index — the SetSelectedIndex target
    int objectId = 0;        // FacilityRef.ObjectID: the facility itself, stable across a sort
                             // (a sort ClearItems() and respawns every row, so neither `index`
                             // nor `handle` survives one — this does)
    bool commandersQuarters = false; // m_FacilityName == 'CommandersQuarters': the game refuses
                                     // Enter on this row (the player is already in that room)
    bool captured = false;   // the row's setData push was found in the capture
    std::string name;        // arg 0 — the display name, the upkeep suffix split off
    int upkeep = -1;         // arg 0's "(§N)" suffix — the monthly upkeep in supplies; -1 when
                             // the row draws none (no upkeep, or still under construction)
    std::string staff1;      // arg 1 — staff slot 1's display string, empty with no slot
    std::string staff2;      // arg 2 — staff slot 2's
    std::string power;       // arg 3 — the facility's power output, signed
    std::string built;       // arg 4 — construction date, or "core facility", or empty while building
    std::string status;      // arg 5 — GetStatusMessage
    std::string queue;       // arg 6 — GetQueueMessage
};

inline constexpr int kFacilitySortColumns = 5; // EFacilitySortType: name, staff, power, date, status

struct FacilitySortHeader {
    void* handle = nullptr;  // UIFacilitySummary_HeaderButton
    int sortType = -1;       // its EFacilitySortType
    std::string label;       // m_strButtonLabels[SortType] — the word drawn over the column
};

struct FacilitySummaryView {
    void* screen = nullptr;
    void* list = nullptr;                 // m_kList
    std::string title;                    // m_strTitle — "Avenger status"
    int sortType = 0;                     // m_eSortType (EFacilitySortType)
    bool sortFlipped = false;             // m_bFlipSort
    std::vector<FacilitySortHeader> headers; // the header strip, in the game's own child order
    std::string columns[kFacilitySortColumns]; // header labels indexed by sort type
    std::vector<FacilitySummaryRow> rows; // list order, which is the current SORT order
    int selected = -1;                    // m_kList.SelectedIndex — -1 on mouse+keyboard until set
};

/// The focused UIFacilitySummary, or null. Game thread only.
void* FindFacilitySummaryScreen();

/// Read the table. Rows whose push has not reached the capture yet come back with `captured`
/// false; the recipe treats that as "not ready". Game thread only.
FacilitySummaryView ReadFacilitySummary(void* screen);

/// P11 write-back: m_kList.SetSelectedIndex. Game thread only.
bool SetFacilitySummaryIndex(void* screen, int index);

/// Go to the facility on row `index`: select it, then hand the ROW its own Enter
/// (UIFacilitySummary_ListItem.OnUnrealCommand → OnButtonClicked → SelectFacility). Game thread only.
bool ActivateFacilitySummaryRow(void* screen, int index);

/// Click a sort header the way the mouse does (its OnMouseEvent with FXS_L_MOUSE_UP): selects that
/// column, or flips the direction when it already was the sort column, then rebuilds the list.
/// `sortType` names the header. Game thread only.
bool ClickFacilitySortHeader(void* screen, int sortType);

} // namespace GameUI
