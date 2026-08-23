// Conformance tests for the TABLE emitter (GraphSheet) and the kernel support it needs —
// NodeVtable::Column, KeyGraph::SlideToColumn and the LandGroup row-vanish rule.
//
// The three things a table has to do that a flat list does not, one test group each:
//   * name the column you stepped into (Left/Right edge labels),
//   * name the row you dropped into when you are off column 0 (Up/Down edge labels),
//   * KEEP your column when focus is moved for you rather than by you.

#include <memory>
#include <string>
#include <vector>

#include "Graph/GraphBuilder.hpp"
#include "Graph/GraphSheet.hpp"
#include "Graph/KeyGraph.hpp"
#include "TestHarness.hpp"

using namespace X2Access::Graph;

namespace
{

const ControlType& CellType()
{
    static const ControlType type{"text", {AnnouncementKinds::Label}, nullptr};
    return type;
}

SheetConfig Config()
{
    return SheetConfig{/*Blank*/ "blank", /*TableRole*/ "table", /*CellType*/ &CellType()};
}

NodeVtable Primary(const std::string& label)
{
    NodeVtable vt;
    vt.Announcements = {NodeAnnouncement::Static(label)};
    return vt;
}

std::function<std::string()> Cell(std::string text)
{
    return [t = std::move(text)]() { return t; };
}

// Three soldiers, three columns. `rows` are the row identities so a test can drop one.
struct Roster
{
    std::vector<std::string> Names{"Kelly", "Bose", "Osei"};
};

// Distinct non-null row references. Their VALUES never matter — only that they are stable and
// unequal, which is what the sheet keys rows on.
const void* RowRef(int i)
{
    static const char kAnchors[8] = {};
    return &kAnchors[i];
}

std::unique_ptr<GraphRender> BuildRoster(const Roster& roster)
{
    GraphBuilder b;
    b.BeginStop("list");
    GraphSheet sheet(b, "roster/", Config());
    sheet.Region("Soldiers", {"rank", "class", "status"});
    for (std::size_t i = 0; i < roster.Names.size(); i++)
        sheet.Row(Primary(roster.Names[i]), RowRef(static_cast<int>(i)),
            {Cell("Squaddie"), Cell("Ranger"), Cell("Active")});
    sheet.Finish();
    return b.Build();
}

std::string LabelOf(const GraphNode* n)
{
    if (!n || n->Vtable.Announcements.empty())
        return {};
    return n->Vtable.Announcements.front().Text();
}

} // namespace

// ---- column headers ----

TEST(Sheet_RightIntoMetadataCellAnnouncesItsColumnHeader)
{
    Roster roster;
    GraphState state;
    KeyGraph g([&roster]() { return BuildRoster(roster); }, &state);

    CHECK(g.Rerender());
    CHECK_EQ(std::string("Kelly"), LabelOf(g.CurrentNode()));

    MoveResult r = g.Move(GraphDir::Right);
    CHECK(r.Moved);
    // The whole column-header mechanism: the edge INTO a cell carries the destination column's
    // heading, so a bare value never reaches the player without the word that gives it meaning.
    CHECK_EQ(std::string("rank"), r.TransitionLabel);
    CHECK_EQ(std::string("Squaddie"), LabelOf(r.To));

    r = g.Move(GraphDir::Right);
    CHECK_EQ(std::string("class"), r.TransitionLabel);
    CHECK_EQ(std::string("Ranger"), LabelOf(r.To));
}

// ---- row verbs reach every cell ----

TEST(Sheet_CellsInheritTheRowsTooltip)
{
    // A tooltip describes the ROW, not the cell you happen to be standing in, so asking for it
    // from column 2 must answer rather than making the player walk back to column 0 first —
    // the same rule Enter and Backspace already follow.
    int calls = 0;
    auto build = [&calls]() {
        GraphBuilder b;
        b.BeginStop("list");
        GraphSheet sheet(b, "roster/", Config());
        sheet.Region("Soldiers", {"rank", "class"});
        NodeVtable primary = Primary("Kelly");
        primary.OnTooltip = [&calls]() { calls++; };
        sheet.Row(std::move(primary), RowRef(0), {Cell("Squaddie"), Cell("Ranger")});
        sheet.Finish();
        return b.Build();
    };

    GraphState state;
    KeyGraph g(build, &state);
    CHECK(g.Rerender());

    CHECK(g.Tooltip()); // on the primary
    CHECK_EQ(1, calls);

    CHECK(g.Move(GraphDir::Right).Moved);
    CHECK(g.Move(GraphDir::Right).Moved);
    CHECK_EQ(std::string("Ranger"), LabelOf(g.CurrentNode()));
    CHECK(g.Tooltip()); // and from the far cell of the same row
    CHECK_EQ(2, calls);
}

TEST(Sheet_CellKeepsItsOwnTooltipOverTheRows)
{
    // Inheritance only fills a gap: a cell that declares its own tooltip keeps it.
    int rowCalls = 0;
    int cellCalls = 0;
    auto build = [&rowCalls, &cellCalls]() {
        GraphBuilder b;
        b.BeginStop("list");
        GraphSheet sheet(b, "roster/", Config());
        sheet.Region("Soldiers", {"rank"});
        NodeVtable primary = Primary("Kelly");
        primary.OnTooltip = [&rowCalls]() { rowCalls++; };
        NodeVtable cell;
        cell.Type = &CellType();
        cell.Announcements = {NodeAnnouncement::Static("Squaddie")};
        cell.OnTooltip = [&cellCalls]() { cellCalls++; };
        sheet.RowAt(std::move(primary), RowRef(0), {{1, std::move(cell)}});
        sheet.Finish();
        return b.Build();
    };

    GraphState state;
    KeyGraph g(build, &state);
    CHECK(g.Rerender());
    CHECK(g.Move(GraphDir::Right).Moved);
    CHECK(g.Tooltip());
    CHECK_EQ(0, rowCalls);
    CHECK_EQ(1, cellCalls);
}

TEST(Sheet_LeftBackOntoThePrimaryIsUnlabelled)
{
    Roster roster;
    GraphState state;
    KeyGraph g([&roster]() { return BuildRoster(roster); }, &state);
    CHECK(g.Rerender());

    g.Move(GraphDir::Right);
    MoveResult r = g.Move(GraphDir::Left);
    CHECK(r.Moved);
    // Column 0 needs no heading: its own readout identifies the row.
    CHECK_EQ(std::string(""), r.TransitionLabel);
    CHECK_EQ(std::string("Kelly"), LabelOf(r.To));
}

// ---- row names on vertical moves ----

TEST(Sheet_DownWithinAColumnNamesTheRowLandedIn)
{
    Roster roster;
    GraphState state;
    KeyGraph g([&roster]() { return BuildRoster(roster); }, &state);
    CHECK(g.Rerender());

    g.Move(GraphDir::Right); // into "rank"
    MoveResult r = g.Move(GraphDir::Down);
    CHECK(r.Moved);
    // Off column 0 the value alone says nothing about WHICH soldier, so the edge names the row.
    CHECK_EQ(std::string("Bose"), r.TransitionLabel);
    CHECK_EQ(1, r.To->Vtable.Column);
}

TEST(Sheet_DownAlongColumnZeroIsUnlabelled)
{
    Roster roster;
    GraphState state;
    KeyGraph g([&roster]() { return BuildRoster(roster); }, &state);
    CHECK(g.Rerender());

    MoveResult r = g.Move(GraphDir::Down);
    CHECK(r.Moved);
    // The primary's own parts read the row — a label here would say the name twice.
    CHECK_EQ(std::string(""), r.TransitionLabel);
    CHECK_EQ(std::string("Bose"), LabelOf(r.To));
}

TEST(Sheet_VerticalStaysInItsColumn)
{
    Roster roster;
    GraphState state;
    KeyGraph g([&roster]() { return BuildRoster(roster); }, &state);
    CHECK(g.Rerender());

    g.Move(GraphDir::Right);
    g.Move(GraphDir::Right); // column 2, "class"
    CHECK_EQ(2, g.CurrentNode()->Vtable.Column);
    g.Move(GraphDir::Down);
    // Reading a column DOWN is the single most useful thing a table offers; snapping back to the
    // first cell would make it impossible.
    CHECK_EQ(2, g.CurrentNode()->Vtable.Column);
    g.Move(GraphDir::Down);
    CHECK_EQ(2, g.CurrentNode()->Vtable.Column);
    CHECK_EQ(std::string("Ranger"), LabelOf(g.CurrentNode()));
}

// ---- sparse rows ----

TEST(Sheet_SparseRowKeepsColumnNumbersAndFallsToThePrimary)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            GraphBuilder b;
            b.BeginStop("list");
            GraphSheet sheet(b, "s/", Config());
            sheet.Region("Crew", {"rank", "class"});
            sheet.Row(Primary("Kelly"), RowRef(0), {Cell("Squaddie"), Cell("Ranger")});
            // A scientist has no rank and no class: BOTH producers null, so the row is one cell.
            sheet.Row(Primary("Tygan"), RowRef(1), {nullptr, nullptr});
            sheet.Row(Primary("Osei"), RowRef(2), {Cell("Sergeant"), Cell("Sharpshooter")});
            sheet.Finish();
            return b.Build();
        },
        &state);
    CHECK(g.Rerender());

    g.Move(GraphDir::Right);
    g.Move(GraphDir::Right); // Kelly, column 2
    MoveResult r = g.Move(GraphDir::Down);
    CHECK(r.Moved);
    // A ragged row must never dead-end the walk: with no cell at column 2, the edge falls to that
    // row's primary rather than to nothing.
    CHECK_EQ(0, r.To->Vtable.Column);
    CHECK_EQ(std::string("Tygan"), LabelOf(r.To));
    // ...and unlabelled, because a landing on column 0 reads the whole row anyway.
    CHECK_EQ(std::string(""), r.TransitionLabel);
}

TEST(Sheet_SkippedCellDoesNotRenumberTheOnesAfterIt)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            GraphBuilder b;
            b.BeginStop("list");
            GraphSheet sheet(b, "s/", Config());
            sheet.Region("Crew", {"rank", "class", "status"});
            sheet.Row(Primary("Tygan"), RowRef(0), {nullptr, nullptr, Cell("Active")});
            sheet.Finish();
            return b.Build();
        },
        &state);
    CHECK(g.Rerender());

    MoveResult r = g.Move(GraphDir::Right);
    CHECK(r.Moved);
    // The one emitted cell is still logical column 3 — which is what lets vertical navigation
    // match columns across rows of different shapes — and still takes the third heading.
    CHECK_EQ(3, r.To->Vtable.Column);
    CHECK_EQ(std::string("status"), r.TransitionLabel);
}

// ---- blank cells ----

TEST(Sheet_EmptyCellSpeaksTheBlankWord)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            GraphBuilder b;
            b.BeginStop("list");
            GraphSheet sheet(b, "s/", Config());
            sheet.Region("Crew", {"location"});
            sheet.Row(Primary("Kelly"), RowRef(0), {Cell("   ")});
            sheet.Finish();
            return b.Build();
        },
        &state);
    CHECK(g.Rerender());

    MoveResult r = g.Move(GraphDir::Right);
    // Silence would be indistinguishable from a cell that failed to resolve, or from no cell.
    CHECK_EQ(std::string("blank"), LabelOf(r.To));
}

// ---- keeping the column when focus is moved FOR you ----

TEST(Sheet_ColumnSurvivesTheRowVanishing)
{
    Roster roster;
    GraphState state;
    KeyGraph g([&roster]() { return BuildRoster(roster); }, &state);
    CHECK(g.Rerender());

    g.Move(GraphDir::Down); // Bose
    g.Move(GraphDir::Right);
    g.Move(GraphDir::Right); // Bose, column 2
    CHECK_EQ(2, g.CurrentNode()->Vtable.Column);
    CHECK_EQ(2, state.LastColumn);

    // Bose is dismissed between renders. The order interleaves a table's CELLS, so the plain
    // backward walk lands on the previous row's LAST cell — a different column. The slide is what
    // puts focus back where the player was working.
    roster.Names = {"Kelly", "Osei"};
    CHECK(g.Rerender());
    CHECK_EQ(2, g.CurrentNode()->Vtable.Column);
}

TEST(Sheet_RowVanishPrefersASurvivingRowOverALeadLine)
{
    std::vector<std::string> names{"Kelly", "Bose"};
    GraphState state;
    KeyGraph g(
        [&names]()
        {
            GraphBuilder b;
            b.BeginStop("list");
            GraphSheet sheet(b, "roster/", Config());
            sheet.Region("Soldiers", {"rank"});
            // A lead line ahead of the data: it carries no row reference, so it stays OUT of the
            // landing group and the row-vanish rule can never drop the player onto it.
            sheet.Line(Primary("Barracks"));
            for (std::size_t i = 0; i < names.size(); i++)
                sheet.Row(Primary(names[i]), RowRef(static_cast<int>(i)), {Cell("Squaddie")});
            sheet.Finish();
            return b.Build();
        },
        &state);
    CHECK(g.Rerender());

    g.Move(GraphDir::Down); // Kelly
    CHECK_EQ(std::string("Kelly"), LabelOf(g.CurrentNode()));
    CHECK_EQ(std::string("roster/"), state.LastLandGroup);

    names = {"Bose"}; // Kelly is dismissed; the lead line sits immediately before her
    CHECK(g.Rerender());
    CHECK_EQ(std::string("Bose"), LabelOf(g.CurrentNode()));
}

TEST(Sheet_NonTabularGraphIsUnaffectedByTheSlide)
{
    // A plain menu has Column -1 everywhere; the slide must return the node it was handed.
    GraphState state;
    std::vector<std::string> items{"one", "two", "three"};
    KeyGraph g(
        [&items]()
        {
            GraphBuilder b;
            for (const std::string& i : items)
            {
                NodeVtable vt;
                vt.Announcements = {NodeAnnouncement::Static(i)};
                b.AddItem(ControlId::Structural(i), std::move(vt));
            }
            return b.Build();
        },
        &state);
    CHECK(g.Rerender());
    g.Move(GraphDir::Down); // "two"
    CHECK_EQ(-1, state.LastColumn);

    items = {"one", "three"};
    CHECK(g.Rerender());
    CHECK_EQ(std::string("one"), LabelOf(g.CurrentNode()));
}
