// The Tactical Legacy Pack's Challenge Archives menu (UITLE_ChallengeModeMenu) — the offline
// archive of every past daily challenge, opened from the hub's third card. One screen is the whole
// branch: Enter on a row loads the challenge's history and runs its map command straight away
// (docs/tle-challenge-recon.md, and the header comment in game_ui_tle_challenge.h).
//
// THE SCREEN IS A TABLE. Five sort headers drawn once across the top (MISSION, OBJECTIVE, MAP,
// SQUAD, ENEMIES), 107 rows of five cells under them, and a details panel on the right that
// repeats the selected row's cells under the same headings plus three profile figures. So the
// list is a GraphSheet — column 0 is the operation name and carries the row's other four cells as
// value parts (walking down reads the whole row), stepping right lands on one cell under its
// heading, and Up/Down then reads that ONE column down the archive — which is how "which
// challenges are on a slum map" stops being 107 paragraphs.
//
// THREE STOPS in the screen's own top-to-bottom order: the sort headers (Enter sorts, the same
// click a mouse makes, through the companion because the buttons are unnamed children the widget
// reader cannot reach), the archive, and the three profile figures the details panel draws under
// the selected row. Focus STARTS on the archive, where LoadFinished puts the game's own selection.
// The details panel's per-row half is deliberately NOT a fourth stop: it is the focused row's
// cells again, and the sheet already reads them.

#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "game_ui_tle_challenge.h"
#include "log.h"
#include "tts.h"
#include "vocab.h"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MenuNavigator {
namespace Recipes {
namespace {

constexpr const char* kChallengeReaderClass = "X2AccessChallengeReader";

// The companion's half: the five sort headers' state (they are UIFlipSortButtons the screen
// spawns and never stores, so only GetChildrenOfType reaches them) and the three profile
// figures. Keyed on the row handles plus the screen, with the SORT STATE as the extra
// discriminator: a sort Refresh()es the rows IN PLACE (UpdateList, no respawn), so the pointer
// key alone would keep the previous header's arrow after a re-sort.
WireCache gChallengeWords(kChallengeReaderClass, "DescribeChallengeMenu");

long long SortStateKey(const GameUI::ChallengeMenuView& view)
{
    return (static_cast<long long>(view.sortType) << 8) | (view.sortFlipped ? 1 : 0);
}

const std::vector<WireRecord>& FetchChallengeWords(const GameUI::ChallengeMenuView& view)
{
    return FetchWordsKeyedOnRows(gChallengeWords, view.screen, view.rows, SortStateKey(view));
}

// ---- the sort headers ----

// One node per header, in the game's own child order (mission, objective, map, squad, enemies).
// The header's state is an ARROW on the active column, up or down — two facts a sighted player
// reads at a glance and speech has to say: which column sorts, and in which direction.
void BuildChallengeSort(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    bool any = false;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "sort")
            continue;
        const std::string indexText = RecField(rec, "index");
        if (indexText.empty())
            continue;
        const int index = atoi(indexText.c_str());
        if (!any) {
            any = true;
            b.BeginStop("sort");
            b.StartRow("challenge/sort");
        }
        ControlId id = ControlId::Structural("challenge/sort/" + std::to_string(index));

        NodeVtable vt;
        vt.Type = &ButtonType();
        std::string line = std::string(Vocab::PersonnelSortBy) + " " + RecField(rec, "label");
        const bool selected = !RecFieldAll(rec, "selected").empty();
        const bool descending = !RecFieldAll(rec, "desc").empty();
        if (selected)
            AppendPhrase(line, descending ? Vocab::SortDescending : Vocab::SortAscending);
        vt.Announcements.emplace_back(NodeAnnouncement::Static(line));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement(
            [selected]() { return selected ? std::string(Vocab::Selected) : std::string(); },
            false, AnnouncementKinds::Selected));
        // The button's own mouse-up: it deselects its siblings, FLIPS the direction when the
        // column was already the sort column, and calls RefreshData. The verb helper drops the
        // wire cache afterwards; the sort-state key would have caught it too.
        vt.OnActivate = [index]() {
            CallCompanionVerb(gChallengeWords, kChallengeReaderClass, "ClickChallengeSort", index);
        };
        // The post-activation feedback: a fresh render's record for this header.
        vt.StateText = [index]() {
            const GameUI::ChallengeMenuView view = GameUI::ReadChallengeMenu(gScreen);
            for (const WireRecord& rec : FetchChallengeWords(view)) {
                if (RecField(rec, "kind") != "sort"
                    || atoi(RecField(rec, "index").c_str()) != index)
                    continue;
                std::string text = std::string(Vocab::PersonnelSortBy) + " " + RecField(rec, "label");
                if (!RecFieldAll(rec, "selected").empty())
                    AppendPhrase(text,
                        RecFieldAll(rec, "desc").empty() ? Vocab::SortAscending
                                                          : Vocab::SortDescending);
                return text;
            }
            return std::string();
        };
        b.AddItem(std::move(id), std::move(vt));
    }
    if (any)
        b.EndRow();
}

// ---- the archive ----

SheetConfig ChallengeSheetConfig()
{
    return SheetConfig{Vocab::TableBlank, Vocab::RoleTable, &TextType()};
}

std::unique_ptr<GraphRender> BuildChallengeMenu()
{
    const GameUI::ChallengeMenuView view = GameUI::ReadChallengeMenu(gScreen);
    // No rows yet: the LoadingItems state is still reading history files behind the LOADING
    // CHALLENGES progress dialog (the popup recipe's, listed before this one). Not ready.
    if (view.rows.empty())
        return nullptr;

    const std::vector<WireRecord>& records = FetchChallengeWords(view);

    GraphBuilder b;

    BuildChallengeSort(b, records);

    b.BeginStop("list");

    // Column headings are the sort headers' own labels, in the order the columns are drawn —
    // the primary (mission) has none, its own readout identifies it.
    std::vector<std::string> columns;
    for (int i = 1; i < GameUI::kChallengeSortColumns; i++)
        columns.push_back(view.columns[i]);

    GraphSheet sheet(b, "challenge/", ChallengeSheetConfig());
    // No region label: the screen is already named on attach.
    sheet.Region("", columns);

    ControlId start;
    bool haveStart = false;
    const int rowCount = static_cast<int>(view.rows.size());
    int position = 0;

    for (const GameUI::ChallengeRow& row : view.rows) {
        const int index = row.index;
        position++;

        NodeVtable primary;
        primary.Type = &ButtonType();
        primary.Announcements.emplace_back(NodeAnnouncement::Static(row.mission));
        primary.Announcements.back().Kind = AnnouncementKinds::Label;
        // The row's other cells on the PRIMARY as value parts, left to right as drawn: this is
        // what keeps the flat reading and the tabular one the same graph.
        const std::string cells[4] = {row.objective, row.map, row.squad, row.enemies};
        for (const std::string& cell : cells) {
            if (cell.empty())
                continue;
            primary.Announcements.emplace_back(NodeAnnouncement::Static(cell));
            primary.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        // Re-read at activation: the handler runs from the input drain, a frame or more after
        // this render, and a sort rewrites every row's Data in place. The list index is what
        // the game's own Enter branch passes, so it is what this passes.
        primary.OnActivate = [index]() { GameUI::ActivateChallengeRow(gScreen, index); };
        // A sheet is raw mode underneath, so the builder stamps no automatic positions — and
        // "40 of 107" is one of the facts a blind player most wants in an archive this long.
        primary.Announcements.emplace_back(
            NodeAnnouncement::Static(Vocab::Position(position, rowCount)));
        primary.Announcements.back().Kind = AnnouncementKinds::Position;

        std::vector<std::function<std::string()>> producers;
        producers.reserve(4);
        for (const std::string& cell : cells) {
            // An empty cell stays NULL: unlandable, so arrowing across a row does not stop on
            // a fact it does not have, while the columns after it keep their numbers.
            if (cell.empty())
                producers.push_back(nullptr);
            else
                producers.push_back([cell]() { return cell; });
        }

        // ⛔ The row identity is the challenge's FILE INDEX, not the widget: a sort Refresh()es
        // the same widgets with different challenges, so a handle-keyed row would silently
        // rebadge "the challenge under my cursor" as a different one after every sort. The
        // file index is what OnChallengeClicked resolves the row to, and it is per challenge.
        const void* rowRef = reinterpret_cast<const void*>(
            static_cast<uintptr_t>(row.dataIndex >= 0 ? row.dataIndex + 1 : 0));
        sheet.Row(std::move(primary), rowRef, std::move(producers));

        // Focus write-back on every cell of the row: selecting is what the details panel and
        // the game's own Enter read back, and a player acting from column 3 acts on their row.
        for (int col = 0; col <= 4; col++) {
            std::string key = GraphSheet::RowKey("challenge/", rowRef, col);
            gFocus[key] = FocusTarget{index, nullptr, false, nullptr};
            if (col == 0 && !haveStart && (index == view.selected || position == 1)) {
                start = ControlId::Referenced(rowRef, key);
                haveStart = index == view.selected;
            }
        }
    }
    sheet.Finish();

    // ---- the profile figures the details panel draws under every row ----
    //
    // Victories, high score, challenges completed — the same three the hub card shows, drawn here
    // again beside whichever row is selected. Screen-wide, so one stop rather than a part on
    // each of 107 rows.
    bool anyStat = false;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "stat")
            continue;
        const std::string label = RecField(rec, "label");
        const std::string value = RecField(rec, "value");
        if (label.empty() || value.empty())
            continue;
        if (!anyStat) {
            anyStat = true;
            b.BeginStop("progress");
        }
        NodeVtable vt;
        vt.Type = &TextType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(value + " " + label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        b.AddItem(ControlId::Structural("challenge/stat/" + label), std::move(vt));
    }

    // Land where the game lands: LoadFinished selects the list's first row, and a return from
    // a mission (the screen is re-pushed fresh, but the list keeps the last SelectedIndex
    // through UpdateList) lands on the row the player launched.
    if (start.IsValid())
        b.SetStart(std::move(start));

    return b.Build();
}

std::string ChallengeMenuName()
{
    const GameUI::ChallengeMenuView view = GameUI::ReadChallengeMenu(gScreen);
    return view.title.empty() ? std::string(Vocab::ScreenChallengeArchiveFallback) : view.title;
}

} // namespace

// ---- the recipe this file defines ----
//
// ⛔ THE PROBE ORDER IS NOT HERE — it is the kRecipes literal in menu_navigator.cpp. The archive
// is pushed over the Tactical Legacy Pack hub (kTLEHubRecipe, itself over the shell) and raises
// only the LOADING CHALLENGES progress dialog above itself, which the popup recipe (listed first)
// takes; so it goes ABOVE the hub and below every modal.
const Recipe kChallengeMenuRecipe{"challenge archive", ProbeFocused<GameUI::FindChallengeMenuScreen>,
    BuildChallengeMenu, ChallengeMenuName, GameUI::SetChallengeMenuIndex};

} // namespace Recipes
} // namespace MenuNavigator
