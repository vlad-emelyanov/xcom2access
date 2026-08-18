// Conformance tests: the FLYOUT primitive — a menu-bar strip whose items may own a transient
// drop-down column. Covers builder wiring, engine movement at every boundary, and the announcer's
// owner hint. Design: docs/design-flyout-primitive.md.
//
// The invariant under test throughout: a flyout is NOT a tree. It has no persistent state, its
// owner stays an ordinary activatable strip item, and none of the tree code paths may fire.

#include <stdexcept>
#include <string>
#include <vector>

#include "Graph/GraphAnnouncer.hpp"
#include "Graph/GraphBuilder.hpp"
#include "Graph/KeyGraph.hpp"
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

// A menu-bar hub in miniature: a five-item strip where two items own columns.
//   cyber   inventory[backpack, crafting]   map   journal[shards, tarot, gallery]
std::unique_ptr<GraphRender> BuildHub()
{
    GraphBuilder b;
    b.StartRow("strip")
        .AddItem(Id("cyber"), Vt("Cyberware"))
        .AddItem(Id("inventory"), Vt("Inventory"))
        .AddItem(Id("map"), Vt("Map"))
        .AddItem(Id("journal"), Vt("Journal"))
        .EndRow();
    b.BeginFlyout(Id("inventory"))
        .AddItem(Id("backpack"), Vt("Backpack"))
        .AddItem(Id("crafting"), Vt("Crafting"))
        .EndFlyout();
    b.BeginFlyout(Id("journal"))
        .AddItem(Id("shards"), Vt("Shards"))
        .AddItem(Id("tarot"), Vt("Tarot"))
        .AddItem(Id("gallery"), Vt("Gallery"))
        .EndFlyout();
    return b.Build();
}

KeyGraph Hub(GraphState& state)
{
    return KeyGraph([]() { return BuildHub(); }, &state);
}

// Resets a pluggable announcer hook when the test scope exits.
struct Cleanup
{
    std::function<void()> Fn;
    ~Cleanup() { Fn(); }
};

} // namespace

// ---- builder wiring ----

TEST(FlyoutOwnerIsStampedWithItsColumnSize)
{
    auto render = BuildHub();

    const GraphNode* inventory = render->NodeAt(Id("inventory"));
    CHECK(inventory->FlyoutOwner);
    CHECK_EQ(2, inventory->FlyoutCount);

    const GraphNode* journal = render->NodeAt(Id("journal"));
    CHECK(journal->FlyoutOwner);
    CHECK_EQ(3, journal->FlyoutCount);

    // A strip item with no column is untouched.
    const GraphNode* map = render->NodeAt(Id("map"));
    CHECK_FALSE(map->FlyoutOwner);
    CHECK_EQ(0, map->FlyoutCount);
}

TEST(FlyoutItemsTakeTheOwnerAsParent)
{
    auto render = BuildHub();
    const GraphNode* owner = render->NodeAt(Id("inventory"));

    CHECK_EQ(owner, render->NodeAt(Id("backpack"))->Parent);
    CHECK_EQ(owner, render->NodeAt(Id("crafting"))->Parent);
}

TEST(OwnerDownEntersTheColumn)
{
    auto render = BuildHub();

    CHECK_EQ(Id("backpack"), render->NodeAt(Id("inventory"))->GetTransition(GraphDir::Down)->Destination);
    CHECK_EQ(Id("shards"), render->NodeAt(Id("journal"))->GetTransition(GraphDir::Down)->Destination);
}

TEST(StripItemsWithoutAColumnGainNoDown)
{
    // The failure this guards: row-to-row wiring, or a stitch, handing EVERY strip cell a Down
    // into the first column — the whole strip falling into Inventory's drop-down.
    auto render = BuildHub();

    CHECK_FALSE(render->NodeAt(Id("cyber"))->HasTransition(GraphDir::Down));
    CHECK_FALSE(render->NodeAt(Id("map"))->HasTransition(GraphDir::Down));
}

TEST(StripKeepsItsHorizontalWiringAcrossFlyouts)
{
    auto render = BuildHub();

    CHECK_EQ(Id("inventory"), render->NodeAt(Id("cyber"))->GetTransition(GraphDir::Right)->Destination);
    CHECK_EQ(Id("map"), render->NodeAt(Id("inventory"))->GetTransition(GraphDir::Right)->Destination);
    CHECK_EQ(Id("inventory"), render->NodeAt(Id("map"))->GetTransition(GraphDir::Left)->Destination);
}

TEST(ColumnWiresVerticallyAndLeftEscapesToTheOwner)
{
    auto render = BuildHub();

    CHECK_EQ(Id("inventory"), render->NodeAt(Id("backpack"))->GetTransition(GraphDir::Up)->Destination);
    CHECK_EQ(Id("crafting"), render->NodeAt(Id("backpack"))->GetTransition(GraphDir::Down)->Destination);
    CHECK_EQ(Id("backpack"), render->NodeAt(Id("crafting"))->GetTransition(GraphDir::Up)->Destination);
    CHECK_FALSE(render->NodeAt(Id("crafting"))->HasTransition(GraphDir::Down));

    // Left leaves the column from anywhere in it; Right is a dead end.
    CHECK_EQ(Id("inventory"), render->NodeAt(Id("backpack"))->GetTransition(GraphDir::Left)->Destination);
    CHECK_EQ(Id("inventory"), render->NodeAt(Id("crafting"))->GetTransition(GraphDir::Left)->Destination);
    CHECK_FALSE(render->NodeAt(Id("backpack"))->HasTransition(GraphDir::Right));
    CHECK_FALSE(render->NodeAt(Id("crafting"))->HasTransition(GraphDir::Right));
}

TEST(ColumnItemsArePositionStampedWithinTheirColumn)
{
    auto render = BuildHub();

    CHECK_EQ(1, render->NodeAt(Id("backpack"))->PositionIndex);
    CHECK_EQ(2, render->NodeAt(Id("backpack"))->PositionCount);
    CHECK_EQ(2, render->NodeAt(Id("crafting"))->PositionIndex);
    CHECK_EQ(2, render->NodeAt(Id("crafting"))->PositionCount);

    // A neighbouring column counts separately, and the strip counts as its own row.
    CHECK_EQ(3, render->NodeAt(Id("gallery"))->PositionIndex);
    CHECK_EQ(3, render->NodeAt(Id("gallery"))->PositionCount);
    CHECK_EQ(4, render->NodeAt(Id("cyber"))->PositionCount);
}

TEST(EmptyFlyoutLeavesTheOwnerAPlainItem)
{
    // Screens filter their children (hidden, unavailable) and must not have to pre-count them.
    auto render = GraphBuilder()
                      .StartRow().AddItem(Id("a"), Vt("A")).AddItem(Id("b"), Vt("B")).EndRow()
                      .BeginFlyout(Id("b"))
                      .EndFlyout()
                      .Build();

    CHECK_FALSE(render->NodeAt(Id("b"))->FlyoutOwner);
    CHECK_EQ(0, render->NodeAt(Id("b"))->FlyoutCount);
    CHECK_FALSE(render->NodeAt(Id("b"))->HasTransition(GraphDir::Down));
}

TEST(FlyoutDoesNotBreakARowDeclaredAfterIt)
{
    // The flyout is skipped by the row segmenter rather than closing the stop's open segment, so
    // the two strips still chain to each other.
    auto render = GraphBuilder()
                      .StartRow().AddItem(Id("a"), Vt("A")).EndRow()
                      .BeginFlyout(Id("a")).AddItem(Id("a1"), Vt("A1")).EndFlyout()
                      .StartRow().AddItem(Id("b"), Vt("B")).EndRow()
                      .Build();

    // 'a' Down belongs to its column; 'b' still reaches back up to the row above it.
    CHECK_EQ(Id("a1"), render->NodeAt(Id("a"))->GetTransition(GraphDir::Down)->Destination);
    CHECK_EQ(Id("a"), render->NodeAt(Id("b"))->GetTransition(GraphDir::Up)->Destination);
}

TEST(FlyoutInsideACollapsedGroupIsSwallowed)
{
    // The owner is never declared under a collapsed group, so the block must no-op rather than
    // throw: a screen that works while its group is expanded must not kill its own render when
    // the user collapses it.
    GraphBuilder b;
    b.AddItem(Id("top"), Vt("Top"));
    b.BeginGroup(Id("g"), Vt("G"), /*expanded*/ false);
    b.AddItem(Id("owner"), Vt("Owner"));
    b.BeginFlyout(Id("owner")).AddItem(Id("child"), Vt("Child")).EndFlyout();
    b.EndGroup();
    auto render = b.Build();

    CHECK_EQ(std::size_t{2}, render->Order.size()); // just "top" and the group header
    CHECK(render->NodeAt(Id("owner")) == nullptr);
    CHECK(render->NodeAt(Id("child")) == nullptr);
}

TEST(FlyoutInsideAnExpandedGroupWiresNormally)
{
    GraphBuilder b;
    b.BeginGroup(Id("g"), Vt("G"), /*expanded*/ true);
    b.AddItem(Id("owner"), Vt("Owner"));
    b.BeginFlyout(Id("owner")).AddItem(Id("child"), Vt("Child")).EndFlyout();
    b.EndGroup();
    auto render = b.Build();

    const GraphNode* owner = render->NodeAt(Id("owner"));
    CHECK(owner->FlyoutOwner);
    CHECK_EQ(Id("child"), owner->GetTransition(GraphDir::Down)->Destination);
    // The column's parent is the OWNER, not the enclosing group.
    CHECK_EQ(owner, render->NodeAt(Id("child"))->Parent);
    CHECK_EQ(Id("owner"), render->NodeAt(Id("child"))->GetTransition(GraphDir::Left)->Destination);
}

TEST(FlyoutMisuseThrows)
{
    CHECK_THROWS(std::logic_error,
        GraphBuilder().StartRow().AddItem(Id("a"), Vt("A")).BeginFlyout(Id("a")));
    CHECK_THROWS(std::invalid_argument, GraphBuilder().BeginFlyout(Id("nope")));
    CHECK_THROWS(std::logic_error,
        GraphBuilder().AddNode(Id("raw"), Vt("Raw")).BeginFlyout(Id("raw")));
    CHECK_THROWS(std::logic_error, GraphBuilder().AddItem(Id("a"), Vt("A")).EndFlyout());
    CHECK_THROWS(std::logic_error,
        GraphBuilder().AddItem(Id("a"), Vt("A")).BeginFlyout(Id("a")).Build());

    {
        GraphBuilder b;
        b.AddItem(Id("a"), Vt("A")).AddItem(Id("b"), Vt("B"));
        b.BeginFlyout(Id("a")).AddItem(Id("a1"), Vt("A1")).EndFlyout();
        CHECK_THROWS(std::logic_error, b.BeginFlyout(Id("a"))); // duplicate column
    }
    {
        GraphBuilder b;
        b.AddItem(Id("a"), Vt("A"));
        b.BeginFlyout(Id("a"));
        CHECK_THROWS(std::logic_error, b.BeginFlyout(Id("a"))); // nesting
        CHECK_THROWS(std::logic_error, b.StartRow());
        CHECK_THROWS(std::logic_error, b.AddNode(Id("raw"), Vt("Raw")));
        CHECK_THROWS(std::logic_error, b.BeginGroup(Id("g"), Vt("G")));
    }
}

// ---- engine movement ----

TEST(DownEntersAndUpLeavesTheColumn)
{
    GraphState state;
    KeyGraph g = Hub(state);
    g.Focus(Id("inventory"));

    MoveResult down = g.Move(GraphDir::Down);
    CHECK(down.Moved);
    CHECK_EQ(Id("backpack"), down.To->Id);

    MoveResult deeper = g.Move(GraphDir::Down);
    CHECK(deeper.Moved);
    CHECK_EQ(Id("crafting"), deeper.To->Id);

    CHECK_EQ(Id("backpack"), g.Move(GraphDir::Up).To->Id);
    CHECK_EQ(Id("inventory"), g.Move(GraphDir::Up).To->Id);
}

TEST(LeftFromAnywhereInTheColumnReturnsToTheOwner)
{
    GraphState state;
    KeyGraph g = Hub(state);
    g.Focus(Id("gallery")); // last item of a three-deep column

    MoveResult left = g.Move(GraphDir::Left);
    CHECK(left.Moved);
    CHECK_EQ(Id("journal"), left.To->Id);
}

TEST(StripTraversalSkipsColumnsEntirely)
{
    GraphState state;
    KeyGraph g = Hub(state);
    g.Focus(Id("cyber"));

    CHECK_EQ(Id("inventory"), g.Move(GraphDir::Right).To->Id);
    CHECK_EQ(Id("map"), g.Move(GraphDir::Right).To->Id);
    CHECK_EQ(Id("journal"), g.Move(GraphDir::Right).To->Id);
    CHECK_FALSE(g.Move(GraphDir::Right).Moved);
}

TEST(SiblingEdgeStaysWithinTheColumn)
{
    GraphState state;
    KeyGraph g = Hub(state);
    g.Focus(Id("tarot"));

    CHECK_EQ(Id("shards"), g.MoveToSiblingEdge(/*first*/ true).To->Id);
    CHECK_EQ(Id("gallery"), g.MoveToSiblingEdge(/*first*/ false).To->Id);
}

TEST(MoveToEdgeNeverCrossesAFlyoutBoundary)
{
    GraphState state;
    KeyGraph g = Hub(state);

    // End on an owner must not fall down into its column — the strip is its line.
    g.Focus(Id("inventory"));
    CHECK_FALSE(g.MoveToEdge(GraphDir::Down).Moved);

    // From inside a column the walk stops at the column's head instead of climbing out to the
    // owner: it goes as far as the line goes, and no further.
    g.Focus(Id("crafting"));
    MoveResult up = g.MoveToEdge(GraphDir::Up);
    CHECK(up.Moved);
    CHECK_EQ(Id("backpack"), up.To->Id);
}

TEST(FlyoutsAreNotTrees)
{
    auto render = BuildHub();

    for (const char* key : {"cyber", "inventory", "map", "journal", "backpack", "gallery"})
    {
        const GraphNode* n = render->NodeAt(Id(key));
        CHECK_FALSE(KeyGraph::InTree(n));
        CHECK_FALSE(n->Expandable);
    }

    CHECK(KeyGraph::InFlyout(render->NodeAt(Id("backpack"))));
    CHECK_FALSE(KeyGraph::InFlyout(render->NodeAt(Id("inventory")))); // owns one, isn't in one
    CHECK_FALSE(KeyGraph::InFlyout(render->NodeAt(Id("map"))));
}

TEST(ColumnItemSurvivesARebuild)
{
    // Flyouts hold no state, so a rebuild must land back on the same item rather than the start.
    GraphState state;
    KeyGraph g = Hub(state);
    g.Focus(Id("tarot"));

    CHECK(g.Rerender());
    CHECK_EQ(Id("tarot"), g.CurrentNode()->Id);
}

TEST(ColumnItemsAreReachableWithoutVisitingTheOwner)
{
    // Type-ahead and focus restore address column items directly: they are ordinary members of
    // the render, not children gated behind an expansion.
    GraphState state;
    KeyGraph g = Hub(state);
    g.Focus(Id("cyber"));

    CHECK(g.Focus(Id("crafting")));
    CHECK_EQ(Id("crafting"), g.CurrentNode()->Id);
}

// ---- announcer ----

TEST(OwnerAnnouncesItsColumnBeforeItsPosition)
{
    Cleanup restore{[]
        {
            GraphAnnouncer::FlyoutHintText = nullptr;
            GraphAnnouncer::PositionText = nullptr;
        }};
    GraphAnnouncer::FlyoutHintText = [](int count)
    { return "submenu, " + std::to_string(count) + " items"; };
    GraphAnnouncer::PositionText = [](int index, int count)
    { return std::to_string(index) + " of " + std::to_string(count); };

    auto render = BuildHub();

    CHECK_EQ(std::string("Inventory, submenu, 2 items, 2 of 4"),
        GraphAnnouncer::LeafText(render->NodeAt(Id("inventory"))));
    CHECK_EQ(std::string("Map, 3 of 4"), GraphAnnouncer::LeafText(render->NodeAt(Id("map"))));
}

TEST(OwnersSayNothingExtraWithoutTheHook)
{
    Cleanup restore{[] { GraphAnnouncer::PositionText = nullptr; }};
    GraphAnnouncer::FlyoutHintText = nullptr;

    auto render = BuildHub();
    CHECK_EQ(std::string("Inventory"), GraphAnnouncer::LeafText(render->NodeAt(Id("inventory"))));
}

TEST(DescendingIntoAColumnAnnouncesOnlyTheChild)
{
    Cleanup restore{[] { GraphAnnouncer::PositionText = nullptr; }};
    GraphAnnouncer::FlyoutHintText = nullptr;

    auto render = BuildHub();
    const GraphNode* owner = render->NodeAt(Id("inventory"));
    const GraphNode* child = render->NodeAt(Id("backpack"));

    // The owner is both the from-node and on the child's chain, so the shared prefix swallows it.
    CHECK_EQ(std::string("Backpack"), GraphAnnouncer::Compose(owner, child));
}

TEST(ArrivingAtAColumnItemFromElsewhereAnnouncesTheOwnerFirst)
{
    Cleanup restore{[] { GraphAnnouncer::PositionText = nullptr; }};
    GraphAnnouncer::FlyoutHintText = nullptr;

    auto render = BuildHub();
    const GraphNode* elsewhere = render->NodeAt(Id("cyber"));
    const GraphNode* child = render->NodeAt(Id("backpack"));

    CHECK_EQ(std::string("Inventory, Backpack"), GraphAnnouncer::Compose(elsewhere, child));
}

// ---- key-entry columns (FlyoutEntry::KeyOnly) ----
//
// The per-item action menu: an owner inside a VERTICAL list, where Down must go on meaning "next
// item". Everything about the column is identical to a Down-entry one — only the way in differs.
// Design: docs/design-inventory-ui.md §2.

namespace
{

// A backpack in miniature: three stacked items, the middle one owning an action column.
//   guillotine
//   unity        [equip, drop]
//   maxdoc
std::unique_ptr<GraphRender> BuildList()
{
    GraphBuilder b;
    b.AddItem(Id("guillotine"), Vt("Guillotine"))
        .AddItem(Id("unity"), Vt("Unity"))
        .AddItem(Id("maxdoc"), Vt("Maxdoc"));
    b.BeginFlyout(Id("unity"), GraphBuilder::FlyoutEntry::KeyOnly)
        .AddItem(Id("equip"), Vt("Equip"))
        .AddItem(Id("drop"), Vt("Drop"))
        .EndFlyout();
    return b.Build();
}

KeyGraph List(GraphState& state)
{
    return KeyGraph([]() { return BuildList(); }, &state);
}

} // namespace

TEST(KeyOnlyColumnLeavesTheOwnersDownAlone)
{
    auto render = BuildList();
    const GraphNode* owner = render->NodeAt(Id("unity"));

    CHECK(owner->FlyoutOwner);
    CHECK(owner->FlyoutKeyOnly);
    CHECK_EQ(2, owner->FlyoutCount);
    // The whole point: Down still walks the list, it does not fall into the column.
    CHECK_EQ(Id("maxdoc"), owner->GetTransition(GraphDir::Down)->Destination);
}

TEST(DownEntryRemainsTheDefault)
{
    auto render = BuildHub();
    const GraphNode* owner = render->NodeAt(Id("inventory"));

    CHECK(owner->FlyoutOwner);
    CHECK_FALSE(owner->FlyoutKeyOnly);
    CHECK_EQ(Id("backpack"), owner->GetTransition(GraphDir::Down)->Destination);
}

TEST(KeyOnlyColumnWiresItsInternalEdgesIdentically)
{
    auto render = BuildList();
    const GraphNode* owner = render->NodeAt(Id("unity"));
    const GraphNode* equip = render->NodeAt(Id("equip"));
    const GraphNode* drop = render->NodeAt(Id("drop"));

    CHECK_EQ(owner, equip->Parent);
    CHECK_EQ(Id("unity"), equip->GetTransition(GraphDir::Up)->Destination);
    CHECK_EQ(Id("unity"), equip->GetTransition(GraphDir::Left)->Destination);
    CHECK_EQ(Id("drop"), equip->GetTransition(GraphDir::Down)->Destination);
    CHECK_EQ(Id("unity"), drop->GetTransition(GraphDir::Left)->Destination);
    CHECK_FALSE(drop->HasTransition(GraphDir::Down));
}

TEST(EnterFlyoutDescendsIntoTheColumnHead)
{
    GraphState state;
    KeyGraph g = List(state);
    g.Focus(Id("unity"));

    MoveResult r = g.EnterFlyout();
    CHECK(r.Moved);
    CHECK_EQ(Id("equip"), r.To->Id);
    CHECK_EQ(Id("unity"), r.From->Id);
}

TEST(EnterFlyoutAlsoWorksOnADownEntryOwner)
{
    // Both gestures reach a Down column: the key is an addition, never a restriction.
    GraphState state;
    KeyGraph g = Hub(state);
    g.Focus(Id("inventory"));

    MoveResult r = g.EnterFlyout();
    CHECK(r.Moved);
    CHECK_EQ(Id("backpack"), r.To->Id);
}

TEST(EnterFlyoutDoesNothingWithoutAColumn)
{
    GraphState state;
    KeyGraph g = List(state);
    g.Focus(Id("guillotine"));

    MoveResult r = g.EnterFlyout();
    CHECK_FALSE(r.Moved);
    CHECK_EQ(Id("guillotine"), g.CurrentNode()->Id);
}

TEST(LeaveFlyoutReturnsToTheOwner)
{
    GraphState state;
    KeyGraph g = List(state);
    g.Focus(Id("drop"));

    MoveResult r = g.LeaveFlyout();
    CHECK(r.Moved);
    CHECK_EQ(Id("unity"), r.To->Id);
}

TEST(LeaveFlyoutDoesNothingOutsideAColumn)
{
    GraphState state;
    KeyGraph g = List(state);
    g.Focus(Id("unity")); // the OWNER is not inside its own column

    MoveResult r = g.LeaveFlyout();
    CHECK_FALSE(r.Moved);
    CHECK_EQ(Id("unity"), g.CurrentNode()->Id);
}

TEST(KeyOnlyOwnerStillGetsTheSubmenuHint)
{
    Cleanup restore{[]
        {
            GraphAnnouncer::FlyoutHintText = nullptr;
            GraphAnnouncer::PositionText = nullptr;
        }};
    GraphAnnouncer::FlyoutHintText = [](int count)
    { return "submenu, " + std::to_string(count) + " items"; };
    GraphAnnouncer::PositionText = nullptr;

    auto render = BuildList();
    CHECK_EQ(std::string("Unity, submenu, 2 items"),
        GraphAnnouncer::LeafText(render->NodeAt(Id("unity"))));
}

TEST(FlyoutOwnerCarryingASecondaryActionThrows)
{
    // Both are the right-click equivalent and share one key: declaring both would silently
    // shadow the secondary action, so the builder refuses rather than picking a winner.
    GraphBuilder b;
    NodeVtable vt = Vt("Unity");
    vt.OnSecondary = [] {};
    b.AddItem(Id("unity"), std::move(vt));
    b.BeginFlyout(Id("unity"), GraphBuilder::FlyoutEntry::KeyOnly)
        .AddItem(Id("equip"), Vt("Equip"))
        .EndFlyout();
    CHECK_THROWS(std::logic_error, b.Build());
}
