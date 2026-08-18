// Conformance tests: engine operations (moves, reconciliation tiers, stop/region cycling, tree
// semantics, behavior invokers). Ported from RTAccess tests/KeyGraphTests.cs.

#include <memory>
#include <string>
#include <utility>
#include <vector>

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

KeyGraph Menu(GraphState& state, std::vector<std::string> items)
{
    return KeyGraph(
        [items = std::move(items)]()
        {
            GraphBuilder b;
            for (const auto& i : items)
                b.AddItem(Id(i), Vt(i));
            return b.Build();
        },
        &state);
}

/// The role registry the type-jump tests navigate by. Types are non-owning (NodeVtable::Type), so
/// they must outlive every render — function-local statics, one per role, keyed the way a screen's
/// own ControlType constants are.
/// `aliases` is honoured on FIRST use of a key and ignored after — the registry is a cache, and a
/// screen's ControlType constants are likewise defined exactly once.
const ControlType& RoleType(const std::string& key, std::vector<std::string> aliases = {})
{
    static std::vector<std::unique_ptr<ControlType>> types;
    for (const auto& t : types)
        if (t->Key == key)
            return *t;
    types.push_back(
        std::unique_ptr<ControlType>(new ControlType{key, {}, nullptr, std::move(aliases)}));
    return *types.back();
}

NodeVtable Typed(const std::string& label, const std::string& typeKey)
{
    NodeVtable vt = Vt(label);
    if (!typeKey.empty())
        vt.Type = &RoleType(typeKey);
    return vt;
}

/// A flat list of `{key, roleKey}` — an empty role means an untyped node, the thing a jump skips.
KeyGraph Roles(GraphState& state, std::vector<std::pair<std::string, std::string>> items)
{
    return KeyGraph(
        [items = std::move(items)]()
        {
            GraphBuilder b;
            for (const auto& i : items)
                b.AddItem(Id(i.first), Typed(i.first, i.second));
            return b.Build();
        },
        &state);
}

NodeVtable Radio(const std::string& label, bool selected)
{
    NodeVtable vt;
    vt.Announcements = {
        NodeAnnouncement::Static(label),
        NodeAnnouncement([selected]() { return selected ? std::string("selected") : std::string(); },
            false, AnnouncementKinds::Selected),
    };
    return vt;
}

} // namespace

TEST(FirstRenderLandsOnStart)
{
    GraphState state;
    KeyGraph g = Menu(state, {"a", "b"});
    CHECK(g.Rerender());
    CHECK_EQ(Id("a"), state.CurKey);
}

TEST(MoveStepsAndStopsAtEdges)
{
    GraphState state;
    KeyGraph g = Menu(state, {"a", "b"});

    MoveResult r = g.Move(GraphDir::Down);
    CHECK(r.Moved);
    CHECK_EQ(Id("b"), r.To->Id);

    r = g.Move(GraphDir::Down); // at the end
    CHECK_FALSE(r.Moved);
    CHECK_EQ(Id("b"), r.To->Id);
    CHECK(r.From == r.To);
}

TEST(MoveToEdgeGoesAllTheWay)
{
    GraphState state;
    KeyGraph g = Menu(state, {"a", "b", "c", "d"});
    MoveResult r = g.MoveToEdge(GraphDir::Down);
    CHECK(r.Moved);
    CHECK_EQ(Id("d"), r.To->Id);
}

TEST(TransitionLabelIsReported)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            return GraphBuilder()
                .AddNode(Id("a"), Vt("A"))
                .AddNode(Id("b"), Vt("B"))
                .Connect(Id("a"), GraphDir::Right, Id("b"), "lane change")
                .Build();
        },
        &state);

    MoveResult r = g.Move(GraphDir::Right);
    CHECK(r.Moved);
    CHECK_EQ("lane change", r.TransitionLabel);
}

TEST(ReconcileTier2FollowsStructuralKeyAcrossRebuilds)
{
    GraphState state;
    int generation = 0;
    KeyGraph g(
        [&generation]()
        {
            generation++; // fresh vtables/nodes each render — only the structural keys repeat
            return GraphBuilder()
                .AddItem(Id("a"), Vt("A" + std::to_string(generation)))
                .AddItem(Id("b"), Vt("B" + std::to_string(generation)))
                .Build();
        },
        &state);

    g.Move(GraphDir::Down);
    CHECK_EQ(Id("b"), state.CurKey);
    CHECK(g.Rerender()); // a whole new render
    CHECK_EQ(Id("b"), state.CurKey);
}

TEST(ReconcileTier1FollowsAMovedObject)
{
    GraphState state;
    int backing = 0;
    const void* thing = &backing; // the backing domain object
    int slot = 1;                 // its structural position, which will change

    KeyGraph g(
        [&]()
        {
            return GraphBuilder()
                .AddItem(ControlId::Structural("header"), Vt("Header"))
                .AddItem(ControlId::Referenced(thing, "slot" + std::to_string(slot)), Vt("Thing"))
                .Build();
        },
        &state);

    g.Move(GraphDir::Down); // focus the thing (at slot1)
    CHECK_EQ("slot1", state.CurKey.StructuralKey);

    slot = 2; // the object moves to a different slot
    CHECK(g.Rerender());
    CHECK_EQ("slot2", state.CurKey.StructuralKey); // followed the reference, not the old key
}

// Several nodes deriving from ONE object is a contract violation (a reference names one node), but
// it is an easy one to commit — a details pane whose every line comes from the same quest. It used
// to make tier 1 resolve to an arbitrary member of the group out of an unordered_map, identically
// on every rebuild, so the cursor snapped back there after each move and the pane could not be
// walked. Tier 1 now keeps the node that still carries the old key.
TEST(ReconcileKeepsItsNodeWhenSiblingsShareAReference)
{
    GraphState state;
    int backing = 0;
    const void* shared = &backing; // ONE object behind every line

    KeyGraph g(
        [&]()
        {
            GraphBuilder b;
            for (int i = 0; i < 6; i++)
                b.AddItem(ControlId::Referenced(shared, "line" + std::to_string(i)),
                    Vt("Line " + std::to_string(i)));
            return b.Build();
        },
        &state);

    g.Move(GraphDir::Down);
    g.Move(GraphDir::Down);
    CHECK_EQ("line2", state.CurKey.StructuralKey);

    CHECK(g.Rerender()); // the rebuild must not drag the cursor to another line
    CHECK_EQ("line2", state.CurKey.StructuralKey);

    g.Move(GraphDir::Down);
    CHECK(g.Rerender());
    CHECK_EQ("line3", state.CurKey.StructuralKey); // and moves still stick
}

TEST(ReconcileFallsBackToNearestSurvivor)
{
    GraphState state;
    std::vector<std::string> items{"a", "b", "c", "d"};
    KeyGraph g(
        [&items]()
        {
            GraphBuilder b;
            for (const auto& i : items)
                b.AddItem(Id(i), Vt(i));
            return b.Build();
        },
        &state);

    g.MoveToEdge(GraphDir::Down); // on "d"
    std::erase(items, "d");
    std::erase(items, "c");
    CHECK(g.Rerender());
    CHECK_EQ(Id("b"), state.CurKey); // nearest earlier survivor
}

TEST(SuggestedMoveIsHonoredAndConsumed)
{
    GraphState state;
    KeyGraph g = Menu(state, {"a", "b", "c"});
    state.NextSuggestedMove = Id("c");
    CHECK(g.Rerender());
    CHECK_EQ(Id("c"), state.CurKey);
    CHECK_FALSE(state.NextSuggestedMove.IsValid());
}

TEST(ComputeOrderCoversAllStops)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            return GraphBuilder()
                .AddItem(Id("a"), Vt("A"))
                .BeginStop().AddItem(Id("b"), Vt("B"))
                .BeginStop().AddItem(Id("c"), Vt("C"))
                .Build();
        },
        &state);

    CHECK(g.Rerender());
    CHECK_EQ(std::size_t{3}, state.KeyOrder.size()); // later stops appended despite no cross-stop edges
}

TEST(StopCyclingRemembersPositionPerStop)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            return GraphBuilder()
                .AddItem(Id("a1"), Vt("A1")).AddItem(Id("a2"), Vt("A2"))
                .BeginStop()
                .AddItem(Id("b1"), Vt("B1")).AddItem(Id("b2"), Vt("B2"))
                .Build();
        },
        &state);

    g.Move(GraphDir::Down); // a2 (remembered for stop 1)
    MoveResult r = g.MoveStop(+1, /*wrap*/ false);
    CHECK(r.Moved);
    CHECK_EQ(Id("b1"), r.To->Id);
    g.Move(GraphDir::Down); // b2

    r = g.MoveStop(-1, /*wrap*/ false);
    CHECK_EQ(Id("a2"), r.To->Id); // remembered, not the stop's first node

    r = g.MoveStop(-1, /*wrap*/ false); // at the first stop, no wrap
    CHECK_FALSE(r.Moved);

    r = g.MoveStop(-1, /*wrap*/ true); // wraps to the last stop's memory
    CHECK(r.Moved);
    CHECK_EQ(Id("b2"), r.To->Id);
}

TEST(RegionJumpsWithinStop)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            return GraphBuilder()
                .SetRegion("filters").AddItem(Id("f1"), Vt("F1"))
                .SetRegion("items").AddItem(Id("i1"), Vt("I1")).AddItem(Id("i2"), Vt("I2"))
                .SetRegion("footer").AddItem(Id("z1"), Vt("Z1"))
                .Build();
        },
        &state);

    MoveResult r = g.MoveRegion(+1);
    CHECK_EQ(Id("i1"), r.To->Id);
    r = g.MoveRegion(+1);
    CHECK_EQ(Id("z1"), r.To->Id);
    r = g.MoveRegion(+1); // at the last region
    CHECK_FALSE(r.Moved);
    r = g.MoveRegion(-1);
    CHECK_EQ(Id("i1"), r.To->Id);
}

TEST(TypeJumpVisitsOnlyThatRole)
{
    GraphState state;
    KeyGraph g = Roles(state, {{"t1", ""}, {"k1", "link"}, {"t2", ""}, {"k2", "link"}});

    MoveResult r = g.MoveToType(+1, "link");
    CHECK(r.Moved);
    CHECK_EQ(Id("k1"), r.To->Id);

    r = g.MoveToType(+1, "link");
    CHECK_EQ(Id("k2"), r.To->Id);

    r = g.MoveToType(-1, "link");
    CHECK_EQ(Id("k1"), r.To->Id);
}

// ⛔ The no-wrap contract. Wrapping would silently teleport the cursor to the top of the page, and
// the alternative to a wrap is not a spoken refusal but SILENCE (house rule 5) — so not-moved with
// To == From is the whole answer the navigator gets, and it must survive a refactor.
TEST(TypeJumpStopsAtTheEndsInsteadOfWrapping)
{
    GraphState state;
    KeyGraph g = Roles(state, {{"k1", "link"}, {"t1", ""}, {"k2", "link"}});

    CHECK_EQ(Id("k2"), g.MoveToType(+1, "link").To->Id);

    MoveResult r = g.MoveToType(+1, "link"); // past the last one
    CHECK_FALSE(r.Moved);
    CHECK_EQ(Id("k2"), r.To->Id);
    CHECK(r.From == r.To);

    CHECK_EQ(Id("k1"), g.MoveToType(-1, "link").To->Id);
    CHECK_FALSE(g.MoveToType(-1, "link").Moved); // ...and stays put at the first
}

// A role nothing declares is the state EVERY quick-nav key starts life in: claimed from the game so
// a stray letter cannot open a menu behind the screen, and inert until a build pass stamps the type.
TEST(TypeJumpForAnUnusedRoleDoesNothing)
{
    GraphState state;
    KeyGraph g = Roles(state, {{"t1", ""}, {"k1", "link"}});

    MoveResult r = g.MoveToType(+1, "heading");
    CHECK_FALSE(r.Moved);
    CHECK_EQ(Id("t1"), r.To->Id);

    CHECK_FALSE(g.MoveToType(+1, "").Moved); // an empty role never matches an untyped node either
}

// Crossing stops is the POINT: the reader's model is one document, so K from a screen's chrome has
// to find the first link in its body. Tab keeps the stops; a role jump ignores them.
TEST(TypeJumpCrossesStopBoundaries)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            GraphBuilder b;
            b.BeginStop().AddItem(Id("chrome"), Vt("Chrome"));
            b.BeginStop().AddItem(Id("k1"), Typed("Link", "link"));
            return b.Build();
        },
        &state);

    CHECK(g.Rerender());
    CHECK_EQ(Id("chrome"), state.CurKey);

    MoveResult r = g.MoveToType(+1, "link");
    CHECK(r.Moved);
    CHECK_EQ(Id("k1"), r.To->Id);
}

// A web page's linked image is a graphic AND a link, and a node carries one Type — so the type
// answers to both letters. Without this, one of G and K silently loses every image link on a page.
TEST(TypeJumpMatchesAnAliasAsWellAsTheKey)
{
    GraphState state;
    KeyGraph g(
        [&]()
        {
            GraphBuilder b;
            b.AddItem(Id("t1"), Vt("Text"));
            b.AddItem(Id("g1"), [&]
                {
                    NodeVtable vt = Vt("Picture");
                    vt.Type = &RoleType("graphic", {"link"});
                    return vt;
                }());
            b.AddItem(Id("k1"), Typed("Link", "link"));
            return b.Build();
        },
        &state);

    CHECK(g.Rerender());

    // G finds it by its own key, and stops there — the plain link is not a graphic.
    MoveResult byKey = g.MoveToType(+1, "graphic");
    CHECK(byKey.Moved);
    CHECK_EQ(Id("g1"), byKey.To->Id);
    CHECK_FALSE(g.MoveToType(+1, "graphic").Moved);

    // K finds it by the alias, BEFORE reaching the plain link that follows it.
    CHECK(g.Focus(Id("t1")));
    MoveResult byAlias = g.MoveToType(+1, "link");
    CHECK(byAlias.Moved);
    CHECK_EQ(Id("g1"), byAlias.To->Id);
    CHECK_EQ(Id("k1"), g.MoveToType(+1, "link").To->Id);
}

TEST(BehaviorInvokersReportAbsence)
{
    GraphState state;
    bool clicked = false, adjusted = false;
    KeyGraph g(
        [&]()
        {
            NodeVtable vt;
            vt.Announcements = {NodeAnnouncement::Static("A")};
            vt.OnActivate = [&clicked]() { clicked = true; };
            vt.OnAdjust = [&adjusted](int sign, bool) { adjusted = sign > 0; };
            return GraphBuilder().AddItem(Id("a"), std::move(vt)).Build();
        },
        &state);

    CHECK(g.Activate());
    CHECK(clicked);
    CHECK(g.TryAdjust(+1, false));
    CHECK(adjusted);
    CHECK_FALSE(g.Secondary());
    CHECK_FALSE(g.Tooltip());
}

TEST(InitialFocusLandsOnSelectedMember)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            return GraphBuilder()
                .AddItem(Id("a"), Radio("A", false))
                .AddItem(Id("b"), Radio("B", false))
                .AddItem(Id("c"), Radio("C", true)) // the checked radio, deep in the list
                .AddItem(Id("d"), Radio("D", false))
                .Build();
        },
        &state);

    CHECK(g.Rerender());
    CHECK_EQ(Id("c"), state.CurKey); // not the first node
}

TEST(TabIntoStopLandsOnSelectedMemberWhenNoMemory)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            return GraphBuilder()
                .AddItem(Id("a1"), Vt("A1"))
                .BeginStop()
                .AddItem(Id("b1"), Radio("B1", false))
                .AddItem(Id("b2"), Radio("B2", true)) // selected in the second stop
                .Build();
        },
        &state);

    MoveResult r = g.MoveStop(+1, /*wrap*/ false);
    CHECK(r.Moved);
    CHECK_EQ(Id("b2"), r.To->Id); // landed on the checked one, not b1

    // But remembered position wins on return.
    g.Move(GraphDir::Up);          // b1
    g.MoveStop(-1, /*wrap*/ false); // to stop 1
    r = g.MoveStop(+1, /*wrap*/ false);
    CHECK_EQ(Id("b1"), r.To->Id); // memory beats selection
}

TEST(RawBlockBetweenMenuRowsStaysReachable)
{
    // Menu row, raw block (its own internal wiring), menu row — all one stop. Menu wiring must
    // BREAK at the raw block (declaration order), and the stitcher wires the seams; otherwise the
    // menu edges skip over the block and it becomes an unreachable island.
    GraphState state;
    KeyGraph g(
        []()
        {
            return GraphBuilder()
                .AddItem(Id("above"), Vt("Above"))
                .AddNode(Id("raw1"), Vt("Raw 1"))
                .AddNode(Id("raw2"), Vt("Raw 2"))
                .Connect(Id("raw1"), GraphDir::Down, Id("raw2"))
                .Connect(Id("raw2"), GraphDir::Up, Id("raw1"))
                .AddItem(Id("below"), Vt("Below"))
                .Build();
        },
        &state);

    CHECK(g.Rerender());
    CHECK_EQ(Id("above"), state.CurKey);
    g.Move(GraphDir::Down);
    CHECK_EQ(Id("raw1"), state.CurKey); // into the block, not over it
    g.Move(GraphDir::Down);
    CHECK_EQ(Id("raw2"), state.CurKey);
    g.Move(GraphDir::Down);
    CHECK_EQ(Id("below"), state.CurKey); // out the bottom
    g.Move(GraphDir::Up);
    CHECK_EQ(Id("raw2"), state.CurKey); // and back in
}

TEST(TreeOpsExpandCollapseDescendAscend)
{
    GraphState state;
    KeyGraph g(
        [&state]()
        {
            return GraphBuilder(&state.Expanded)
                .BeginGroup(Id("combat"), Vt("Combat"))
                    .AddItem(Id("pause"), Vt("Auto pause"))
                    .AddItem(Id("delay"), Vt("Delay"))
                .EndGroup()
                .Build();
        },
        &state);

    CHECK(g.Rerender()); // focus lands on the collapsed header
    CHECK_EQ(Id("combat"), state.CurKey);
    CHECK(KeyGraph::InTree(g.CurrentNode()));

    KeyGraph::TreeResult r = g.TreeRight(); // collapsed → expand
    CHECK(KeyGraph::TreeMove::Expanded == r.Kind);
    CHECK_EQ(Id("combat"), state.CurKey); // focus stays on the header
    CHECK(g.CurrentNode()->Expanded);
    CHECK(g.Current()->NodeAt(Id("pause")) != nullptr);

    r = g.TreeRight(); // expanded → descend to first child
    CHECK(KeyGraph::TreeMove::Descended == r.Kind);
    CHECK_EQ(Id("pause"), state.CurKey);

    r = g.TreeRight(); // a leaf inside the tree — consume
    CHECK(KeyGraph::TreeMove::Leaf == r.Kind);

    r = g.TreeLeft(); // child → ascend to the header
    CHECK(KeyGraph::TreeMove::Ascended == r.Kind);
    CHECK_EQ(Id("combat"), state.CurKey);

    r = g.TreeLeft(); // expanded header → collapse
    CHECK(KeyGraph::TreeMove::Collapsed == r.Kind);
    CHECK_EQ(Id("combat"), state.CurKey);
    CHECK(g.Current()->NodeAt(Id("pause")) == nullptr);
}

TEST(EmptyGroupRecollapsesOnExpand)
{
    GraphState state;
    KeyGraph g(
        [&state]()
        {
            return GraphBuilder(&state.Expanded)
                .BeginGroup(Id("dud"), Vt("Dud")) // no children declared
                .EndGroup()
                .Build();
        },
        &state);

    CHECK(g.Rerender());
    KeyGraph::TreeResult r = g.TreeRight();
    CHECK(KeyGraph::TreeMove::EmptyGroup == r.Kind);
    CHECK_FALSE(g.CurrentNode()->Expanded); // auto-recollapsed
    CHECK_EQ(std::size_t{0}, state.Expanded.count(Id("dud")));
}

TEST(CollapseWhileInsideLandsOnNearestSurvivor)
{
    GraphState state;
    state.Expanded.insert(Id("combat"));
    KeyGraph g(
        [&state]()
        {
            return GraphBuilder(&state.Expanded)
                .BeginGroup(Id("combat"), Vt("Combat"))
                    .AddItem(Id("pause"), Vt("Auto pause"))
                .EndGroup()
                .Build();
        },
        &state);

    g.Rerender();
    g.Focus(Id("pause"));
    state.Expanded.erase(Id("combat")); // collapsed externally while focus was inside
    CHECK(g.Rerender());
    CHECK_EQ(Id("combat"), state.CurKey); // nearest survivor = the header
}

TEST(SiblingEdgeJumpStaysAtDepth)
{
    GraphState state;
    state.Expanded.insert(Id("g"));
    KeyGraph g(
        [&state]()
        {
            return GraphBuilder(&state.Expanded)
                .AddItem(Id("top"), Vt("Top"))
                .BeginGroup(Id("g"), Vt("Group"))
                    .AddItem(Id("c1"), Vt("C1"))
                    .AddItem(Id("c2"), Vt("C2"))
                    .AddItem(Id("c3"), Vt("C3"))
                .EndGroup()
                .Build();
        },
        &state);

    g.Rerender();
    g.Focus(Id("c2"));
    MoveResult r = g.MoveToSiblingEdge(/*first*/ false);
    CHECK(r.Moved);
    CHECK_EQ(Id("c3"), state.CurKey); // last SIBLING, not the last visible row overall

    r = g.MoveToSiblingEdge(/*first*/ true);
    CHECK_EQ(Id("c1"), state.CurKey);
}

TEST(FocusAndFocusByReferenceWork)
{
    GraphState state;
    int dummy = 0;
    const void* backing = &dummy;
    KeyGraph g(
        [backing]()
        {
            return GraphBuilder()
                .AddItem(Id("a"), Vt("A"))
                .AddItem(ControlId::Referenced(backing, "b"), Vt("B"))
                .Build();
        },
        &state);

    CHECK(g.Rerender());
    CHECK(g.FocusByReference(backing));
    CHECK_EQ("b", state.CurKey.StructuralKey);
    CHECK_FALSE(g.FocusByReference(backing)); // already there — not a change

    CHECK(g.Focus(Id("a")));
    CHECK_EQ(Id("a"), state.CurKey);
    CHECK_FALSE(g.Focus(Id("nope")));
}

// ---- held activation (the tap/hold split the navigator drives) ----

TEST(ActivateHoldRunsTheHeldActionAndNotTheTap)
{
    GraphState state;
    bool tapped = false, held = false;
    KeyGraph g(
        [&]()
        {
            NodeVtable vt;
            vt.Announcements = {NodeAnnouncement::Static("A")};
            vt.OnActivate = [&tapped]() { tapped = true; };
            vt.OnActivateHold = [&held]() { held = true; };
            return GraphBuilder().AddItem(Id("a"), std::move(vt)).Build();
        },
        &state);

    CHECK(g.ActivateHold());
    CHECK(held);
    CHECK_FALSE(tapped); // the two gestures are separate verbs, never both
}

TEST(ActivateHoldReportsAbsence)
{
    GraphState state;
    KeyGraph g = Menu(state, {"a"});
    CHECK(g.Rerender());
    CHECK_FALSE(g.ActivateHold());
}

TEST(HasHoldActivationDistinguishesNodes)
{
    GraphState state;
    KeyGraph g(
        []()
        {
            NodeVtable holdable;
            holdable.Announcements = {NodeAnnouncement::Static("A")};
            holdable.OnActivateHold = []() {};
            return GraphBuilder()
                .AddItem(Id("a"), std::move(holdable))
                .AddItem(Id("b"), Vt("B"))
                .Build();
        },
        &state);

    CHECK(g.Rerender());
    CHECK(g.HasHoldActivation()); // focus starts on "a"
    g.Focus(Id("b"));
    CHECK_FALSE(g.HasHoldActivation());
}

TEST(HasHoldActivationDoesNotRebuild)
{
    // Contract, not an optimisation: the navigator asks this on every Enter PRESS, right after it
    // has already rendered. A rebuild here would run every screen closure twice per keystroke.
    GraphState state;
    int builds = 0;
    KeyGraph g(
        [&builds]()
        {
            ++builds;
            return GraphBuilder().AddItem(Id("a"), Vt("A")).Build();
        },
        &state);

    CHECK(g.Rerender());
    CHECK_EQ(1, builds);
    CHECK_FALSE(g.HasHoldActivation());
    CHECK_EQ(1, builds);
}

TEST(HasHoldActivationIsFalseBeforeAnyRender)
{
    GraphState state;
    KeyGraph g = Menu(state, {"a"});
    CHECK_FALSE(g.HasHoldActivation()); // no render yet — no focused node to ask
}

// ---- Draft 2 defect regressions (spec Appendix B) ----

TEST(SiblingEdgeJumpStaysInStop)
{
    // Root-level nodes all share the null parent: without the (parent, stop) filter, End on a
    // top-level node would scan clear across every Tab-stop — and arrows never cross a stop
    // (spec §4.3, §5.3; Appendix B defect 1).
    GraphState state;
    KeyGraph g(
        []()
        {
            return GraphBuilder()
                .AddItem(Id("a1"), Vt("A1"))
                .AddItem(Id("a2"), Vt("A2"))
                .BeginStop()
                .AddItem(Id("b1"), Vt("B1"))
                .AddItem(Id("b2"), Vt("B2"))
                .Build();
        },
        &state);

    g.Rerender();
    g.Focus(Id("a1"));
    MoveResult r = g.MoveToSiblingEdge(/*first*/ false);
    CHECK(r.Moved);
    CHECK_EQ(Id("a2"), state.CurKey); // the stop's own last root node — never b2 in the next stop
}

TEST(FocusByReferenceRerendersFirst)
{
    // Every operation acts on current reality (spec §5): the tier-1 sync must re-render rather
    // than read the previous render (Appendix B defect 2). A game-driven sync arrives precisely
    // when the game just changed something — here the referenced node exists only in the SECOND
    // render, so a stale read cannot find it.
    GraphState state;
    int dummy = 0;
    const void* backing = &dummy;
    int builds = 0;
    KeyGraph g(
        [&builds, backing]()
        {
            ++builds;
            GraphBuilder b;
            b.AddItem(Id("a"), Vt("A"));
            if (builds > 1)
                b.AddItem(ControlId::Referenced(backing, "late"), Vt("Late"));
            return b.Build();
        },
        &state);

    CHECK(g.Rerender());
    CHECK_EQ(1, builds);
    CHECK(g.FocusByReference(backing));
    CHECK_EQ(2, builds);
    CHECK_EQ("late", state.CurKey.StructuralKey);
}
