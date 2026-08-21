#pragma once

// Graph A11y Kernel — the navigation engine (spec §5). STL-only; see ControlId.hpp for the
// boundary rule.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "GraphTypes.hpp"

namespace X2Access::Graph
{

/// The outcome of a navigation operation, for the caller (navigator) to announce. The kernel
/// never speaks — it returns what happened. From/To point into the engine's current render and
/// are valid only until the next operation on the same KeyGraph.
struct MoveResult
{
    bool Moved = false;            // focus actually changed nodes
    GraphNode* From = nullptr;     // node before the operation (null on first landing)
    GraphNode* To = nullptr;       // node after (== From when at an edge; null when the graph is empty)
    std::string TransitionLabel;   // the crossed edge's spoken line, when it had one
};

/// The navigation engine: a directed graph of controls rebuilt from a render callback on each
/// operation, with focus persisting in an external GraphState. Two invariants carry over from the
/// source lineage:
///
/// DOWN-RIGHT TOTAL ORDER (ComputeOrder): from the start node, go right until stuck, queueing
/// each down — visits a planar UI in reading order. Nodes down-right can't reach (later
/// Tab-stops) are appended in declaration order, keeping the order total.
///
/// FOCUS RECOVERY ON REBUILD (Reconcile): if the focused control vanished, land on the nearest
/// survivor rather than jumping to the start — following the backing object that moved (tier 1)
/// or the logical control whose backing object was rebuilt (tier 2) first.
class KeyGraph
{
public:
    using RenderCallback = std::function<std::unique_ptr<GraphRender>()>;

    /// `state` is non-owning and must outlive the KeyGraph (the screen manager owns per-screen
    /// cursor state).
    KeyGraph(RenderCallback renderCallback, GraphState* state);

    GraphState& State() { return *_state; }

    /// The most recently built render, or null if not yet rendered / empty.
    GraphRender* Current() { return _current.get(); }

    /// The focused node in the current render, or null.
    GraphNode* CurrentNode();

    /// Rebuild the render and reconcile focus into it. False when the callback produced nothing
    /// (the caller should treat the graph as closed/empty).
    bool Rerender();

    /// Move focus from the cached GraphState::CurKey to a valid control in `render`, then
    /// recompute the traversal order.
    static void Reconcile(GraphRender& render, GraphState& state);

    /// The down-right total order: go right until stuck (recording each node), queue every down
    /// for a later pass, repeat — then append any node the walk never reached (e.g. later
    /// Tab-stops, which have no cross-stop edges) in declaration order, so the order is total.
    static std::vector<ControlId> ComputeOrder(const GraphRender& render);

    // ---- navigation operations ----

    /// One step in `dir`. Not moved (at an edge / empty) → To == From.
    MoveResult Move(GraphDir dir);

    /// As far as possible in `dir` (Home/End within a row or column).
    MoveResult MoveToEdge(GraphDir dir);

    /// Cycle to the next/previous Tab-stop (declaration order), landing on the stop's remembered
    /// position (else its selected member, else its first node). `wrap` continues past the ends;
    /// without it, at the last/first stop the result is not-moved (the caller may blur instead).
    MoveResult MoveStop(int dir, bool wrap);

    /// Jump to the next/previous region within the current stop (declaration order), landing on
    /// the region's first node.
    MoveResult MoveRegion(int dir);

    /// Jump to the next/previous control whose ControlType::Key — or one of its Aliases — is
    /// `typeKey`: a screen reader's single-letter navigation (K for links, H for headings, G for
    /// graphics), driven by the navigator's quick-nav keys.
    ///
    /// Traversal ORDER, and deliberately NOT limited to the current stop: the reader's mental model
    /// here is one document, so K from a screen's chrome should find the first link in its body
    /// rather than nothing. Stops still matter for Tab; they are not a wall for role jumps.
    ///
    /// ⛔ IT DOES NOT WRAP. Running out of links is a refusal, and the layer's refusals are
    /// SILENT — so it returns not-moved and the navigator says nothing, rather than quietly
    /// teleporting the cursor back to the top where the player did not ask to be.
    ///
    /// With no focus yet, a forward jump starts at the top of the order and a backward one at the
    /// bottom, so the first press lands somewhere sensible either way.
    MoveResult MoveToType(int dir, const std::string& typeKey);

    /// Move focus to a specific control (a node just revealed, a screen's chosen landing). False
    /// when it isn't in the render.
    bool Focus(const ControlId& id);

    /// Tier-1 focus sync from the game: if a node's backing object is `reference`, move focus
    /// there. True if focus changed nodes.
    bool FocusByReference(const void* reference);

    /// Where focus lands when entering a stop with no active cursor: the remembered position,
    /// else the SELECTED member (a radio/tab/list item currently checked — a boon on long lists),
    /// else the stop's first node.
    GraphNode* StopLanding(const std::string& stopKey);

    /// The first node in a stop that reads as SELECTED — carries a non-empty selected-kind
    /// announcement part — or null.
    static GraphNode* SelectedNodeInStop(const GraphRender& render, const std::string& stopKey);

    // ---- tree operations (Right/Left semantics for expandable groups) ----

    /// What a tree side-step did (the caller composes the speech).
    enum class TreeMove
    {
        None,       // not applicable here (not in a tree / nothing to do) — caller decides consume/bubble
        Expanded,   // the focused group expanded (focus unchanged; speak its new state)
        Collapsed,  // the focused group collapsed (focus unchanged; speak its new state)
        EmptyGroup, // expanding found no children — auto-recollapsed (speak "no details")
        Descended,  // moved to the group's first child (announce as a move)
        Ascended,   // moved to the nearest focusable ancestor (announce as a move)
        Leaf,       // Right on a non-group inside a tree — consumed, nothing to descend into
    };

    struct TreeResult
    {
        TreeMove Kind = TreeMove::None;
        MoveResult Move; // valid for Descended/Ascended
    };

    /// Is this node part of an expandable structure (itself a group, or under one)? The navigator
    /// uses this to decide whether Left/Right get tree semantics.
    static bool InTree(const GraphNode* node);

    /// Is this node an item INSIDE a flyout column (GraphBuilder::BeginFlyout)? False for the
    /// owner itself, which is an ordinary strip item that happens to own one. Flyout movement is
    /// all plain edges, so this exists only for the operations that reason about a node's LINE:
    /// Home/End (siblings within the column) and MoveToEdge (never leave the line).
    static bool InFlyout(const GraphNode* node);

    /// Right on a group: expand (auto-recollapse when it turns out empty), or descend into an
    /// expanded one. Right elsewhere in a tree: Leaf (consume).
    TreeResult TreeRight();

    /// Left on an expanded group: collapse. Left elsewhere in a tree: ascend to the nearest
    /// focusable ancestor.
    TreeResult TreeLeft();

    /// Home/End inside a tree: the first/last node sharing the focused node's parent (its
    /// siblings at the current depth).
    MoveResult MoveToSiblingEdge(bool first);

    /// Enter the focused node's FLYOUT column at its first item — the context-menu gesture. Works
    /// for either FlyoutEntry: a Down-entry owner is reachable both ways. Moved=false when the
    /// focused node owns no column, so the caller can fall through to its own handling.
    MoveResult EnterFlyout();

    /// Leave the flyout column the focus is inside, returning to its owner. The same thing Left
    /// does, as a named operation so the context-menu key can toggle. Moved=false when the focus
    /// is not in a column.
    MoveResult LeaveFlyout();

    // ---- behavior invokers (the caller announces fallbacks / state) ----

    /// Run the focused control's primary activation. False = it has none.
    bool Activate();

    /// Run the focused control's secondary activation. False = it has none.
    bool Secondary();

    /// Run the focused control's shift-modified activation. False = it has none.
    bool ActivateShift();

    /// Run the focused control's ctrl-modified activation. False = it has none.
    bool ActivateCtrl();

    /// Run the focused control's HELD activation. False = it has none.
    bool ActivateHold();

    /// Does the focused control declare a held activation? The navigator asks BEFORE acting on a
    /// press, to decide whether the tap must be deferred until the gesture resolves. Pure query
    /// over the current render — deliberately does NOT rebuild, because the caller has just
    /// rendered and a second build here would run every screen closure twice per keystroke.
    bool HasHoldActivation();

    /// Run the focused control's tooltip behavior. False = it has none.
    bool Tooltip();

    /// If the focused control adjusts horizontally (a slider), adjust and return true; false =
    /// the caller should navigate instead.
    bool TryAdjust(int sign, bool large);

    /// From `start`, walk its row's Left/Right edges to the cell at `prefCol` — or the nearest
    /// cell BELOW it in a sparse row. Returns `start` unchanged when either side is not tabular
    /// (Column < 0) or the column already matches. Horizontal edges never leave a row, so the walk
    /// cannot escape it. Public for the conformance tests.
    static ControlId SlideToColumn(const GraphRender& render, const ControlId& start, int prefCol);

private:
    static GraphNode* StopLandingIn(const GraphRender& render, const GraphState& state, const std::string& stopKey);
    static void RememberStop(const GraphRender& render, GraphState& state, const ControlId& key);
    static void RememberColumn(const GraphRender& render, GraphState& state, const ControlId& key);
    static void RememberLandGroup(const GraphRender& render, GraphState& state, const ControlId& key);
    static ControlId GroupSurvivorAt(const GraphRender& render, const GraphState& state, int index);
    void SetCurrent(GraphNode* node);
    void SetExpanded(GraphNode* group, bool expanded);
    GraphNode* FirstChildOf(const GraphNode* group);
    std::vector<std::string> StopOrder() const;

    RenderCallback _renderCallback;
    GraphState* _state;
    std::unique_ptr<GraphRender> _current;
};

} // namespace X2Access::Graph
