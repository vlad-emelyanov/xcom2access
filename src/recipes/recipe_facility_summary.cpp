// The Avenger Report (UIFacilitySummary) — the Commander's Quarters' "Avenger status" screen: every
// facility on the ship in one sortable table (docs/surfaces/avenger.md, "Avenger Report"; the
// header comment in game_ui_facility_summary.h).
//
// THE SCREEN IS A TABLE. Five sort headers drawn once across the top (NAME, STAFF, POWER, BUILT,
// STATUS) and one row per facility under them. So the list is a GraphSheet — column 0 is the
// facility name (with its upkeep, as drawn) and carries the row's other cells as value parts, so
// walking down reads each whole row; stepping right lands on one cell under its heading, and
// Up/Down then reads that ONE column down the ship — "which rooms are idle" is the status column.
//
// TWO STOPS in the screen's top-to-bottom order, plus the resource strip when the game draws one:
// the sort headers (Enter clicks one, the mouse's own path — they are mouse-only in vanilla, the
// D-pad column switch being controller-only), then the table. Focus STARTS on the table.
//
// The words are the Flash capture of each row's setData push: native only, no companion. Staff
// slots 1 and 2 share the STAFF column and status + queue share the STATUS column, as drawn.

#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "game_ui_facility_summary.h"
#include "log.h"
#include "vocab.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MenuNavigator {
namespace Recipes {
namespace {

// The direction word for a header that is the sort column. Every comparator on the screen has the
// same shape — `A < B` answers 1 (keep) unflipped — so unflipped is ascending for all five.
std::string SortHeaderLine(const GameUI::FacilitySummaryView& view, int sortType)
{
    std::string line
        = std::string(Vocab::PersonnelSortBy) + " " + view.columns[sortType];
    if (view.sortType == sortType)
        AppendPhrase(line, view.sortFlipped ? Vocab::SortDescending : Vocab::SortAscending);
    return line;
}

// ---- the sort headers ----

void BuildFacilitySort(GraphBuilder& b, const GameUI::FacilitySummaryView& view)
{
    bool any = false;
    for (const GameUI::FacilitySortHeader& header : view.headers) {
        if (header.label.empty())
            continue;
        if (!any) {
            any = true;
            b.BeginStop("sort");
            b.StartRow("facilities/sort");
        }
        const int sortType = header.sortType;
        const bool selected = view.sortType == sortType;

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(SortHeaderLine(view, sortType)));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement(
            [selected]() { return selected ? std::string(Vocab::Selected) : std::string(); },
            false, AnnouncementKinds::Selected));
        // The header's own mouse-up: selects the column, or flips the direction when it already
        // was the sort column, then UpdateData respawns every row in the new order.
        vt.OnActivate = [sortType]() { GameUI::ClickFacilitySortHeader(gScreen, sortType); };
        // The post-activation feedback: the header's state read fresh off the screen.
        vt.StateText = [sortType]() {
            return SortHeaderLine(GameUI::ReadFacilitySummary(gScreen), sortType);
        };
        b.AddItem(ControlId::Structural("facilities/sort/" + std::to_string(sortType)),
            std::move(vt));
    }
    if (any)
        b.EndRow();
}

// ---- the table ----

std::unique_ptr<GraphRender> BuildFacilitySummary()
{
    const GameUI::FacilitySummaryView view = GameUI::ReadFacilitySummary(gScreen);
    if (view.rows.empty())
        return nullptr;

    // Not ready until every row's push has reached the capture: UpdateData respawns all rows in
    // one call, so their pushes land in the same flush and a partial table is at most a frame
    // wide. A row that NEVER captures would keep the screen silent, so that is logged — once per
    // screen instance and row count, never per poll.
    size_t missing = 0;
    for (const GameUI::FacilitySummaryRow& row : view.rows)
        if (!row.captured)
            missing++;
    if (missing) {
        static const void* sLastScreen = nullptr;
        static size_t sLastRows = 0;
        if (gScreen != sLastScreen || view.rows.size() != sLastRows) {
            sLastScreen = gScreen;
            sLastRows = view.rows.size();
            LOG_INFO("FacilitySummary: %zu of %zu row(s) have no captured setData yet; waiting",
                missing, view.rows.size());
        }
        return nullptr;
    }

    GraphBuilder b;

    // The headers first, as drawn; SetStart below still lands attach on the table.
    BuildFacilitySort(b, view);

    b.BeginStop("list");

    // Column headings are the sort headers' own labels, in the order the columns are drawn — the
    // primary (name) has none, its own readout identifies it.
    std::vector<std::string> columns;
    for (int i = 1; i < GameUI::kFacilitySortColumns; i++)
        columns.push_back(view.columns[i]);

    GraphSheet sheet(b, "facilities/", SheetConfig{Vocab::TableBlank, Vocab::RoleTable, &TextType()});
    // No region label: the screen is already named on attach.
    sheet.Region("", columns);

    ControlId start;
    bool haveStart = false;
    const int rowCount = static_cast<int>(view.rows.size());
    int position = 0;

    for (const GameUI::FacilitySummaryRow& row : view.rows) {
        const int index = row.index;
        position++;

        // Cells as drawn: the two staff lines share one column, and so do status and queue.
        const std::string cells[4] = {JoinPhrases(row.staff1, row.staff2), row.power, row.built,
            JoinPhrases(row.status, row.queue)};

        NodeVtable primary;
        primary.Type = &ButtonType();
        primary.Announcements.emplace_back(NodeAnnouncement::Static(row.name));
        primary.Announcements.back().Kind = AnnouncementKinds::Label;
        // The "(§105)" the name cell draws, said as what it is: the game's own upkeep label, then
        // the figure as a counted supplies noun — a bare "105" means nothing without the glyph.
        if (row.upkeep >= 0) {
            std::string upkeep = Vocab::UpkeepCostLabel();
            AppendPhrase(upkeep,
                Vocab::Format(Vocab::CountForm(row.upkeep, Vocab::SuppliesCountOne,
                                  Vocab::SuppliesCountFew, Vocab::SuppliesCountMany),
                    std::to_string(row.upkeep)));
            primary.Announcements.emplace_back(NodeAnnouncement::Static(upkeep));
            primary.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        // The row's other cells on the PRIMARY as value parts, left to right as drawn: this is
        // what keeps the flat reading and the tabular one the same graph.
        for (const std::string& cell : cells) {
            if (cell.empty())
                continue;
            primary.Announcements.emplace_back(NodeAnnouncement::Static(cell));
            primary.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        // Enter goes to the room, through the row's own handler — which refuses the Commander's
        // Quarters, the room this screen was opened from; the game drops its Select nav help on
        // that row, so it gets no activation here either.
        if (!row.commandersQuarters)
            primary.OnActivate = [index]() { GameUI::ActivateFacilitySummaryRow(gScreen, index); };
        // A sheet is raw mode underneath, so the builder stamps no automatic positions.
        primary.Announcements.emplace_back(
            NodeAnnouncement::Static(Vocab::Position(position, rowCount)));
        primary.Announcements.back().Kind = AnnouncementKinds::Position;

        std::vector<std::function<std::string()>> producers;
        producers.reserve(4);
        for (const std::string& cell : cells) {
            // An empty cell stays NULL: unlandable, so arrowing across a row does not stop on a
            // fact it does not have, while the columns after it keep their numbers.
            if (cell.empty())
                producers.push_back(nullptr);
            else
                producers.push_back([cell]() { return cell; });
        }

        // ⛔ The row identity is the FACILITY (FacilityRef.ObjectID), not the widget or the index:
        // a sort respawns every row in a new order, and a handle- or index-keyed row would
        // silently rebadge "the facility under my cursor" as another after every sort.
        const void* rowRef = reinterpret_cast<const void*>(
            static_cast<uintptr_t>(row.objectId > 0 ? row.objectId : 0));
        sheet.Row(std::move(primary), rowRef, std::move(producers));

        // Focus write-back on every cell of the row: selecting is what the game's nav help and
        // its own Enter read back, and a player acting from column 3 acts on their row.
        for (int col = 0; col < GameUI::kFacilitySortColumns; col++) {
            std::string key = GraphSheet::RowKey("facilities/", rowRef, col);
            gFocus[key] = FocusTarget{index, nullptr, false, nullptr};
            if (col == 0 && !haveStart && (index == view.selected || position == 1)) {
                start = ControlId::Referenced(rowRef, key);
                haveStart = index == view.selected;
            }
        }
    }
    sheet.Finish();

    AddResourceStop(b);

    // Land on the table: the game's selected row when it has one (a controller, or a return from
    // a room), else the first row — on mouse+keyboard the list starts with nothing selected.
    if (start.IsValid())
        b.SetStart(std::move(start));

    return b.Build();
}

std::string FacilitySummaryName()
{
    const GameUI::FacilitySummaryView view = GameUI::ReadFacilitySummary(gScreen);
    return view.title.empty() ? Vocab::ScreenFacilitySummaryFallback() : view.title;
}

} // namespace

// ---- the recipe this file defines ----
//
// ⛔ THE PROBE ORDER IS NOT HERE — it is the kRecipes literal in menu_navigator.cpp. The report is
// pushed over the Commander's Quarters (the avenger room recipe) and raises only dialogs above
// itself; Enter on a row leaves it for the chosen room. So it goes ABOVE the room recipe and
// below every modal.
const Recipe kFacilitySummaryRecipe{"avenger report",
    ProbeFocused<GameUI::FindFacilitySummaryScreen>, BuildFacilitySummary, FacilitySummaryName,
    GameUI::SetFacilitySummaryIndex};

} // namespace Recipes
} // namespace MenuNavigator
