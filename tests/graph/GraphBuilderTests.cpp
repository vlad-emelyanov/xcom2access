// Conformance tests: builder wiring (menu rows, raw edges, stops, contexts, groups, stitching,
// position stamping). Ported from RTAccess tests/GraphBuilderTests.cs.

#include <stdexcept>

#include "Graph/GraphBuilder.hpp"
#include "TestHarness.hpp"

using namespace X2Access::Graph;

namespace
{

NodeVtable Vt(const std::string& label)
{
    NodeVtable vt;
    vt.Announcements = {NodeAnnouncement::Static(label)};
    return vt;
}

ControlId Id(const std::string& key)
{
    return ControlId::Structural(key);
}

} // namespace

TEST(SingleItemsFormVerticalMenu)
{
    auto render = GraphBuilder()
                      .AddItem(Id("a"), Vt("A"))
                      .AddItem(Id("b"), Vt("B"))
                      .AddItem(Id("c"), Vt("C"))
                      .Build();

    CHECK_EQ(Id("a"), render->StartKey);
    CHECK_EQ(Id("b"), render->NodeAt(Id("a"))->GetTransition(GraphDir::Down)->Destination);
    CHECK_EQ(Id("a"), render->NodeAt(Id("b"))->GetTransition(GraphDir::Up)->Destination);
    CHECK_EQ(Id("c"), render->NodeAt(Id("b"))->GetTransition(GraphDir::Down)->Destination);
    CHECK_FALSE(render->NodeAt(Id("a"))->HasTransition(GraphDir::Up));
    CHECK_FALSE(render->NodeAt(Id("a"))->HasTransition(GraphDir::Left));
    CHECK_FALSE(render->NodeAt(Id("a"))->HasTransition(GraphDir::Right));
}

TEST(RowsWireHorizontally)
{
    auto render = GraphBuilder()
                      .StartRow().AddItem(Id("a"), Vt("A")).AddItem(Id("b"), Vt("B")).EndRow()
                      .Build();

    CHECK_EQ(Id("b"), render->NodeAt(Id("a"))->GetTransition(GraphDir::Right)->Destination);
    CHECK_EQ(Id("a"), render->NodeAt(Id("b"))->GetTransition(GraphDir::Left)->Destination);
}

TEST(SharedRowKeysPreserveColumn)
{
    auto render = GraphBuilder()
                      .StartRow("grid").AddItem(Id("a1"), Vt("A1")).AddItem(Id("a2"), Vt("A2")).EndRow()
                      .StartRow("grid").AddItem(Id("b1"), Vt("B1")).AddItem(Id("b2"), Vt("B2")).EndRow()
                      .Build();

    CHECK_EQ(Id("b2"), render->NodeAt(Id("a2"))->GetTransition(GraphDir::Down)->Destination);
    CHECK_EQ(Id("a2"), render->NodeAt(Id("b2"))->GetTransition(GraphDir::Up)->Destination);
}

TEST(UnkeyedRowsLandOnFirstItem)
{
    auto render = GraphBuilder()
                      .StartRow().AddItem(Id("a1"), Vt("A1")).AddItem(Id("a2"), Vt("A2")).EndRow()
                      .StartRow().AddItem(Id("b1"), Vt("B1")).AddItem(Id("b2"), Vt("B2")).EndRow()
                      .Build();

    CHECK_EQ(Id("b1"), render->NodeAt(Id("a2"))->GetTransition(GraphDir::Down)->Destination);
}

TEST(RaggedKeyedRowFallsToFirstItem)
{
    auto render = GraphBuilder()
                      .StartRow("grid")
                      .AddItem(Id("a1"), Vt("A1")).AddItem(Id("a2"), Vt("A2")).AddItem(Id("a3"), Vt("A3"))
                      .EndRow()
                      .StartRow("grid").AddItem(Id("b1"), Vt("B1")).EndRow()
                      .Build();

    // Column 3 doesn't exist below → first item.
    CHECK_EQ(Id("b1"), render->NodeAt(Id("a3"))->GetTransition(GraphDir::Down)->Destination);
}

TEST(ArrowsNeverCrossStops)
{
    auto render = GraphBuilder()
                      .AddItem(Id("a"), Vt("A"))
                      .BeginStop()
                      .AddItem(Id("b"), Vt("B"))
                      .Build();

    CHECK_FALSE(render->NodeAt(Id("a"))->HasTransition(GraphDir::Down));
    CHECK_FALSE(render->NodeAt(Id("b"))->HasTransition(GraphDir::Up));
    CHECK_NE(render->NodeAt(Id("a"))->StopKey, render->NodeAt(Id("b"))->StopKey);
}

TEST(ContextBuildsNonFocusableParentChain)
{
    auto render = GraphBuilder()
                      .PushContext("Settings", "list")
                      .AddItem(Id("a"), Vt("A"))
                      .PushContext("Advanced")
                      .AddItem(Id("b"), Vt("B"))
                      .PopContext()
                      .AddItem(Id("c"), Vt("C"))
                      .Build();

    GraphNode* a = render->NodeAt(Id("a"));
    GraphNode* b = render->NodeAt(Id("b"));
    GraphNode* c = render->NodeAt(Id("c"));
    CHECK(a->Parent != nullptr);
    CHECK_FALSE(a->Parent->Focusable);
    CHECK(a->Parent->Parent == nullptr);
    CHECK(a->Parent == b->Parent->Parent); // Advanced nests under Settings
    CHECK(a->Parent == c->Parent);         // c popped back out to Settings
    CHECK(render->NodeAt(a->Parent->Id) == nullptr); // context nodes are never navigable
}

TEST(GroupsEmitHeadersAndSuppressCollapsedSubtrees)
{
    ControlIdSet expansion;
    auto build = [&expansion]()
    {
        return GraphBuilder(&expansion)
            .BeginGroup(Id("combat"), Vt("Combat"))
                .AddItem(Id("pause"), Vt("Auto pause"))
                .BeginGroup(Id("nested"), Vt("Nested"))
                    .AddItem(Id("deep"), Vt("Deep"))
                .EndGroup()
            .EndGroup()
            .AddItem(Id("after"), Vt("After"))
            .Build();
    };

    auto collapsed = build();
    CHECK(collapsed->NodeAt(Id("combat")) != nullptr);
    CHECK(collapsed->NodeAt(Id("combat"))->Expandable);
    CHECK_FALSE(collapsed->NodeAt(Id("combat"))->Expanded);
    CHECK(collapsed->NodeAt(Id("pause")) == nullptr); // collapsed → children swallowed
    CHECK(collapsed->NodeAt(Id("nested")) == nullptr);
    CHECK(collapsed->NodeAt(Id("after")) != nullptr);

    expansion.insert(Id("combat"));
    auto expanded = build();
    CHECK(expanded->NodeAt(Id("pause")) != nullptr);
    CHECK(expanded->NodeAt(Id("combat")) == expanded->NodeAt(Id("pause"))->Parent);
    CHECK(expanded->NodeAt(Id("nested")) != nullptr); // nested header visible…
    CHECK(expanded->NodeAt(Id("deep")) == nullptr);   // …but its own subtree still collapsed

    expansion.insert(Id("nested"));
    auto deep = build();
    CHECK(deep->NodeAt(Id("deep")) != nullptr);
    CHECK(deep->NodeAt(Id("nested")) == deep->NodeAt(Id("deep"))->Parent);
}

TEST(PositionsAutoStampBySiblingGroup)
{
    ControlIdSet expansion;
    expansion.insert(Id("g"));
    auto render = GraphBuilder(&expansion)
                      .AddItem(Id("a"), Vt("A"))       // top level: a, g = 2 siblings
                      .BeginGroup(Id("g"), Vt("G"))
                          .AddItem(Id("c1"), Vt("C1")) // group level: 3 siblings
                          .AddItem(Id("c2"), Vt("C2"))
                          .AddItem(Id("c3"), Vt("C3"))
                      .EndGroup()
                      .BeginStop()
                      .AddItem(Id("lone"), Vt("Lone")) // single sibling → no position
                      .BeginStop()
                      .StartRow().AddItem(Id("r1"), Vt("R1")).AddItem(Id("r2"), Vt("R2")).EndRow() // row members
                      .Build();

    CHECK_EQ(1, render->NodeAt(Id("a"))->PositionIndex);
    CHECK_EQ(2, render->NodeAt(Id("a"))->PositionCount);
    CHECK_EQ(2, render->NodeAt(Id("g"))->PositionIndex);
    CHECK_EQ(2, render->NodeAt(Id("c2"))->PositionIndex);
    CHECK_EQ(3, render->NodeAt(Id("c2"))->PositionCount);
    CHECK_EQ(0, render->NodeAt(Id("lone"))->PositionCount);
    CHECK_EQ(1, render->NodeAt(Id("r1"))->PositionIndex);
    CHECK_EQ(2, render->NodeAt(Id("r2"))->PositionIndex);
    CHECK_EQ(2, render->NodeAt(Id("r2"))->PositionCount);
}

TEST(RegionsAreStamped)
{
    auto render = GraphBuilder()
                      .SetRegion("filters").AddItem(Id("a"), Vt("A"))
                      .SetRegion("items").AddItem(Id("b"), Vt("B"))
                      .Build();

    CHECK_EQ("filters", render->NodeAt(Id("a"))->RegionKey);
    CHECK_EQ("items", render->NodeAt(Id("b"))->RegionKey);
}

TEST(MixedModesKeepDeclarationOrder)
{
    // A screen declaring list → raw grid → button must keep that Tab-stop order (raw nodes must
    // NOT be appended after all menu rows).
    auto render = GraphBuilder()
                      .AddItem(Id("list1"), Vt("L1"))
                      .BeginStop()
                      .AddNode(Id("cell1"), Vt("C1"))
                      .BeginStop()
                      .AddItem(Id("button"), Vt("B"))
                      .Build();

    CHECK_EQ(Id("list1"), render->Order[0]->Id);
    CHECK_EQ(Id("cell1"), render->Order[1]->Id);
    CHECK_EQ(Id("button"), render->Order[2]->Id);
}

TEST(MixedStopStitchesMenuToRawVertically)
{
    // A stop with menu controls above raw sheet content (search/sort/filters over an item table)
    // must be arrow-traversable across the mode boundary.
    auto render = GraphBuilder()
                      .AddItem(Id("search"), Vt("Search"))
                      .StartRow().AddItem(Id("f1"), Vt("F1")).AddItem(Id("f2"), Vt("F2")).EndRow()
                      .AddNode(Id("r0"), Vt("Row0"))
                      .AddNode(Id("r1"), Vt("Row1"))
                      .Connect(Id("r0"), GraphDir::Down, Id("r1"))
                      .Connect(Id("r1"), GraphDir::Up, Id("r0"))
                      .Build();

    // Filter cells drop into the sheet's first row; the sheet's top links back up.
    CHECK_EQ(Id("r0"), render->NodeAt(Id("f1"))->GetTransition(GraphDir::Down)->Destination);
    CHECK_EQ(Id("r0"), render->NodeAt(Id("f2"))->GetTransition(GraphDir::Down)->Destination);
    CHECK_EQ(Id("f1"), render->NodeAt(Id("r0"))->GetTransition(GraphDir::Up)->Destination);
    // The sheet's own wiring is untouched.
    CHECK_EQ(Id("r1"), render->NodeAt(Id("r0"))->GetTransition(GraphDir::Down)->Destination);
}

TEST(RawModeWiresExplicitEdges)
{
    auto render = GraphBuilder()
                      .AddNode(Id("a"), Vt("A"))
                      .AddNode(Id("b"), Vt("B"))
                      .Connect(Id("a"), GraphDir::Right, Id("b"), "crossing the aisle")
                      .Connect(Id("a"), GraphDir::Down, Id("ghost")) // undeclared → dropped
                      .SetStart(Id("b"))
                      .Build();

    CHECK_EQ(Id("b"), render->StartKey);
    CHECK_EQ("crossing the aisle", render->NodeAt(Id("a"))->GetTransition(GraphDir::Right)->Label);
    CHECK_FALSE(render->NodeAt(Id("a"))->HasTransition(GraphDir::Down));
}

TEST(GuardsRejectMisuse)
{
    CHECK(GraphBuilder().Build() == nullptr); // empty = closed

    GraphBuilder dup;
    dup.AddItem(Id("a"), Vt("A"));
    CHECK_THROWS(std::logic_error, dup.AddItem(Id("a"), Vt("A2")));

    CHECK_THROWS(std::invalid_argument, GraphBuilder().AddItem(Id("x"), NodeVtable{}));
}

TEST(MenuRowsAndRawNodesMix)
{
    // A screen mixing an auto-wired list with a computed-topology grid: raw edges may reference
    // menu nodes.
    auto render = GraphBuilder()
                      .AddItem(Id("list1"), Vt("List1"))
                      .AddNode(Id("cell1"), Vt("Cell1"))
                      .AddNode(Id("cell2"), Vt("Cell2"))
                      .Connect(Id("cell1"), GraphDir::Right, Id("cell2"))
                      .Connect(Id("cell1"), GraphDir::Up, Id("list1"))
                      .Build();

    CHECK_EQ(std::size_t{3}, render->Order.size());
    CHECK_EQ(Id("cell2"), render->NodeAt(Id("cell1"))->GetTransition(GraphDir::Right)->Destination);
    CHECK_EQ(Id("list1"), render->NodeAt(Id("cell1"))->GetTransition(GraphDir::Up)->Destination);
}

TEST(PositionRunsBreakAtRawContent)
{
    // Raw content interleaved in a stop breaks the vertical chain (WireMenuEdges never wires menu
    // rows past it), so "n of m" must not count across the break either — otherwise the numbers
    // span nodes arrows cannot traverse between (spec §4.6; Appendix B defect 3).
    auto render = GraphBuilder()
                      .AddItem(Id("a"), Vt("A"))
                      .AddNode(Id("raw"), Vt("Raw"))
                      .AddItem(Id("b"), Vt("B"))
                      .AddItem(Id("c"), Vt("C"))
                      .Build();

    CHECK_EQ(0, render->NodeAt(Id("a"))->PositionCount); // a run of one — no position
    CHECK_EQ(0, render->NodeAt(Id("raw"))->PositionCount); // raw nodes are never stamped
    CHECK_EQ(1, render->NodeAt(Id("b"))->PositionIndex);
    CHECK_EQ(2, render->NodeAt(Id("b"))->PositionCount);
    CHECK_EQ(2, render->NodeAt(Id("c"))->PositionIndex);
    CHECK_EQ(2, render->NodeAt(Id("c"))->PositionCount);
}
