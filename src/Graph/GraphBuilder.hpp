#pragma once

// Graph A11y Kernel — the builder (spec §4). STL-only; see ControlId.hpp for the boundary rule.

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "GraphTypes.hpp"

namespace X2Access::Graph
{

/// Builds a GraphRender. Two construction styles, freely mixable in one build:
///
/// MENU MODE — rows of controls, wired automatically: Left/Right within a row, Up/Down between
/// consecutive rows (two rows sharing a non-empty row key get column navigation — Up/Down
/// preserves the position instead of snapping to the first item). Items added outside an explicit
/// row become single-item rows (a plain vertical menu).
///
/// RAW MODE — AddNode + Connect for arbitrary topologies.
///
/// Orthogonal to both: BeginStop groups nodes into Tab-stops (arrows never cross a stop; Tab
/// cycles them), SetRegion tags nodes with a region for Ctrl+arrow jumps, and the PARENT STACK
/// builds the presentation hierarchy: PushContext pushes a non-focusable structural level
/// ("Difficulty settings, list" — announced when focus enters from outside), while BeginGroup
/// pushes a focusable, EXPANDABLE group header (a tree section) whose children only emit while
/// it's expanded — expansion state lives in the persistent set the builder is constructed with
/// (GraphState::Expanded), so screens hold no tree state of their own. Nesting recurses; a
/// collapsed ancestor suppresses everything beneath it.
///
/// Builders are single-use: Build() transfers node ownership into the render.
class GraphBuilder
{
public:
    /// `expansion` is the persistent expanded-group set (usually &GraphState::Expanded), or null
    /// when every group passes an explicit state. Non-owning; must outlive the builder.
    explicit GraphBuilder(const ControlIdSet* expansion = nullptr);

    GraphBuilder(const GraphBuilder&) = delete;
    GraphBuilder& operator=(const GraphBuilder&) = delete;

    // ---- stops / regions ----

    /// Start a new Tab-stop; nodes added from here belong to it. `key` must be stable across
    /// rebuilds (it keys the stop's remembered position); empty auto-assigns by index, which is
    /// stable when the screen builds its stops in a fixed order.
    GraphBuilder& BeginStop(std::string key = "");

    /// Tag nodes added from here with a region (Ctrl+arrow jump target) within the current stop;
    /// empty clears. Region keys must be stable across rebuilds.
    GraphBuilder& SetRegion(std::string key);

    // ---- the parent stack: contexts + groups ----

    /// Push one NON-FOCUSABLE level of presentation hierarchy ("Difficulty settings", "list")
    /// onto nodes added from here — pure structure: never navigable, announced when focus enters
    /// from outside. Close with PopContext().
    GraphBuilder& PushContext(const std::string& label, const std::string& role = "", bool positions = true);

    GraphBuilder& PopContext();

    /// Push a FOCUSABLE, expandable group header (a tree section): the header emits as a
    /// navigable node here, and the children declared before EndGroup() emit only while the group
    /// is expanded (a collapsed ancestor suppresses the whole subtree — recursion just works).
    /// Expansion state: `expanded` when given, else the persistent expansion set the builder was
    /// constructed with, else `defaultExpanded`. The engine's tree operations (Right/Left)
    /// expand/collapse via the vtable's OnExpand/OnCollapse overrides when set, else by mutating
    /// the persistent set.
    GraphBuilder& BeginGroup(ControlId id, NodeVtable vtable, std::optional<bool> expanded = std::nullopt,
        bool defaultExpanded = false);

    GraphBuilder& EndGroup() { return PopContext(); }

    // ---- flyouts (the menu-bar drop-down) ----

    /// How a column is ENTERED from its owner. Everything else about a flyout is identical either
    /// way — this only decides which gesture descends into it.
    enum class FlyoutEntry
    {
        /// Down from the owner enters the column, replacing its row-to-row Down. The menu-bar
        /// default: a HORIZONTAL strip has nothing below it, so Down is free.
        Down,

        /// Only the navigator's context-menu key enters the column; the owner keeps its ordinary
        /// Down. For an owner inside a VERTICAL list, where Down must go on meaning "next item" —
        /// a per-item action menu, which is a side channel and not a step in the list.
        KeyOnly,
    };

    /// Open a FLYOUT column hanging off `owner` — the menu-bar pattern, NOT a tree. `owner` must
    /// already have been added as a menu item. The items declared until EndFlyout() form the
    /// column: they take `owner` as their Parent but are NOT part of the strip's row-to-row
    /// vertical chain. `entry` decides how the column is entered (Down, or the context-menu key);
    /// either way Up from its first item returns to the owner, and Left anywhere in the column
    /// leaves it.
    ///
    /// A flyout holds NO state — declare it unconditionally, every render. That is the whole
    /// point of it over BeginGroup: the owner stays a destination you can activate rather than a
    /// container you expand, which is what menu bars (and hub strips) actually do.
    ///
    /// An EMPTY column is legal and silent: the owner simply stays a plain item. Screens filter
    /// their children (visibility, availability) and must not have to pre-count them.
    ///
    /// The owner must NOT also declare `NodeVtable::OnSecondary`: both are the right-click
    /// equivalent and share the navigator's one key, so declaring both would silently shadow the
    /// secondary action. Build() throws on it.
    GraphBuilder& BeginFlyout(ControlId owner, FlyoutEntry entry = FlyoutEntry::Down);

    GraphBuilder& EndFlyout();

    /// Whether a group id is expanded in the persistent set — for screens that must avoid even
    /// BUILDING a collapsed group's children (a lazy hierarchy). Groups with an explicit
    /// expanded argument manage their own state instead.
    bool IsExpanded(const ControlId& id) const;

    /// Focus starts here when the graph has no prior position (defaults to the first node).
    GraphBuilder& SetStart(ControlId id);

    // ---- menu mode ----

    /// Open a horizontal row. Rows sharing a non-empty `rowKey` with the row above/below get
    /// column-preserving vertical navigation.
    GraphBuilder& StartRow(std::string rowKey = "");

    GraphBuilder& EndRow();

    /// Add a control — into the open row, or as its own single-item row. A no-op inside a
    /// collapsed group's subtree.
    GraphBuilder& AddItem(ControlId id, NodeVtable vtable);

    /// Add a read-only line (label only; no actions).
    GraphBuilder& AddLabel(ControlId id, std::function<std::string()> label);

    // ---- raw mode ----

    /// Add a node with no automatic wiring (raw mode; wire with Connect). A no-op inside a
    /// collapsed group's subtree.
    GraphBuilder& AddNode(ControlId id, NodeVtable vtable);

    /// Directed edge from → to, with an optional spoken transition line ("lane change"). Edges
    /// to/from undeclared nodes are dropped at build.
    GraphBuilder& Connect(ControlId from, GraphDir dir, ControlId to, std::string label = "");

    // ---- build ----

    /// Finalize into a render, or nullptr when nothing was declared (treat as "closed"). Menu
    /// rows and raw nodes/edges may coexist in one build: rows wire themselves; raw edges may
    /// reference any node.
    std::unique_ptr<GraphRender> Build();

private:
    struct Row
    {
        std::vector<GraphNode*> Items;
        std::string Key;     // empty = unkeyed (no column preservation)
        std::string StopKey;
        bool Flyout = false; // a flyout column's item: wired by WireFlyoutEdges, not the row passes
    };

    // One owner and its declared column. Empty columns are dropped at wire time.
    struct Flyout
    {
        GraphNode* Owner = nullptr;
        std::vector<GraphNode*> Items;
        FlyoutEntry Entry = FlyoutEntry::Down;
    };

    struct RawEdge
    {
        ControlId From;
        GraphDir Dir;
        ControlId To;
        std::string Label;
    };

    // The parent stack: structural levels (PushContext) and group headers (BeginGroup). A frame
    // whose group is collapsed suppresses every declaration beneath it (the stack stays balanced
    // regardless).
    struct ParentFrame
    {
        GraphNode* Node;  // the parent node (non-focusable context, or the group header)
        bool Suppressed;  // this frame's subtree is swallowed (collapsed, or under a collapsed ancestor)
    };

    GraphNode* CurrentParent() const { return _parents.empty() ? nullptr : _parents.back().Node; }
    bool Suppressed() const { return !_parents.empty() && _parents.back().Suppressed; }

    static std::string AutoStopKey(int index) { return "stop#" + std::to_string(index); }

    GraphNode* MakeNode(ControlId id, NodeVtable vtable);
    Row* NewRow();
    GraphNode* FindDeclared(const ControlId& id) const;
    bool InFlyoutRow(const GraphNode* node) const;
    void WireMenuEdges();
    void WireFlyoutEdges();
    void StitchModeBoundaries();
    void StampPositions();
    static void Stamp(std::vector<GraphNode*>& siblings);
    static ControlId VerticalTarget(const Row& from, const Row& to, std::size_t pos);

    const ControlIdSet* _expansion; // persistent expanded-group set (null = all explicit)

    // Node ownership until Build() transfers it to the render (includes context parents that are
    // never navigable — Parent chains must outlive the builder).
    std::vector<std::unique_ptr<GraphNode>> _pool;
    std::vector<std::unique_ptr<Row>> _rowPool;

    // Menu mode.
    std::vector<Row*> _rows;
    Row* _currentRow = nullptr;

    // Flyouts, in declaration order. _currentFlyout indexes into it (-1 = none open); an index
    // rather than a pointer because pushing to the vector may reallocate.
    std::vector<Flyout> _flyouts;
    int _currentFlyout = -1;

    // Raw mode.
    std::vector<GraphNode*> _rawNodes;
    std::vector<RawEdge> _rawEdges;

    // Every node in DECLARATION order regardless of mode — the render's node order (and so the
    // Tab-stop cycle) must interleave menu rows and raw nodes as the screen declared them.
    std::vector<GraphNode*> _declared;

    // The menu row each menu-mode node belongs to (absent for raw nodes) — for stitching the
    // vertical gap where a stop mixes menu rows with raw content.
    std::unordered_map<GraphNode*, Row*> _rowOf;

    std::unordered_set<ControlId> _ids;
    ControlId _start;

    // Stop / region / parent state applied to nodes as they are added.
    std::string _stopKey = AutoStopKey(0);
    int _stopAuto = 1;
    std::string _regionKey;

    std::vector<ParentFrame> _parents;
    bool _built = false;
};

} // namespace X2Access::Graph
