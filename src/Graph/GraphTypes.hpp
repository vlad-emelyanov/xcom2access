#pragma once

// Graph A11y Kernel — data model (spec §3). STL-only; see ControlId.hpp for the boundary rule.

#include <any>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ControlId.hpp"

namespace X2Access::Graph
{

/// The four navigable directions between graph nodes (explicit edges). Tab-stop cycling and
/// region jumps are OPERATIONS over node metadata (GraphNode::StopKey / RegionKey), not edges —
/// they carry per-stop remembered positions, which a static edge can't express.
enum class GraphDir : std::size_t
{
    Up = 0,
    Right = 1,
    Down = 2,
    Left = 3,
};

inline constexpr std::size_t kGraphDirCount = 4;

/// The well-known announcement-part kinds. A part's kind is its identity for control-type
/// ordering, node-over-type overriding, and the user's per-kind announcement settings.
namespace AnnouncementKinds
{
inline constexpr const char* Label = "label";
inline constexpr const char* Role = "role";
inline constexpr const char* Value = "value";

/// ⚠️ NOT JUST A WORD IN THE READOUT. A node carrying a non-empty Selected part is also the
/// LANDING NODE its stop is entered on (KeyGraph::StopLandingIn, after stop memory). Use it only
/// where the stop genuinely has a current item — a list's open row, a filter row's active button.
/// Marking a merely-notable node (the journal's tracked objective did) silently reroutes Tab past
/// every node declared before it, and the ones it skipped become unreachable.
inline constexpr const char* Selected = "selected";
inline constexpr const char* Enabled = "enabled";
inline constexpr const char* Tooltip = "tooltip";
inline constexpr const char* Position = "position";

/// A why-unavailable reason line, ordered immediately after the label. Distinct from Enabled
/// (the leading status word) so the two can sit apart in the readout.
inline constexpr const char* Reason = "reason";
} // namespace AnnouncementKinds

/// One part of a control's spoken focus readout ("Hold position" / "toggle" / "on"), resolved
/// live at speak time. A LIVE part is additionally watched while its node is focused: when its
/// resolved text changes (an async toggle settling, a value the game flips), the navigator speaks
/// just that part immediately — state feedback without re-reading the whole control, and without
/// per-element watcher machinery.
struct NodeAnnouncement
{
    /// The part's text, resolved live. Empty at speak time = the part stays silent.
    std::function<std::string()> Text;

    /// Watch this part while the node is focused and speak it when its value changes.
    bool Live = false;

    /// The part's kind (AnnouncementKinds), or empty for a custom one-off part. Kinds drive the
    /// control type's speak order, let a node's part override the type's common part of the same
    /// kind, and key the user's per-kind announcement settings.
    std::string Kind;

    NodeAnnouncement() = default;

    NodeAnnouncement(std::function<std::string()> text, bool live = false, std::string kind = "")
        : Text(std::move(text))
        , Live(live)
        , Kind(std::move(kind))
    {
    }

    static NodeAnnouncement Static(std::string text)
    {
        return NodeAnnouncement([t = std::move(text)]() { return t; });
    }
};

/// A CONTROL TYPE — "button", "toggle", "slider" — as a registry value rather than a class
/// hierarchy. A type owns the speak ORDER of its announcement kinds and the parts COMMON to every
/// control of the type (the localized role word); nodes contribute their specific parts,
/// overriding a common part of the same kind. The user's per-type announcement settings key off
/// Key. Instances live in a host-side registry and outlive renders; nodes hold non-owning
/// pointers.
struct ControlType
{
    /// Stable settings/registry key ("button", "toggle", "slider").
    std::string Key;

    /// The announcement kinds in speak order; parts with unknown/absent kinds append after, in
    /// declaration order.
    std::vector<std::string> Order;

    /// The parts every control of this type shares (the role word), resolved per compose.
    std::function<std::vector<NodeAnnouncement>()> Common;

    /// Extra role keys a quick-nav jump also stops on (`KeyGraph::MoveToType`), for a control that
    /// honestly answers to more than one letter. A web page's linked image is the case that forced
    /// it: it is a graphic AND a link, and a reader expects both G and K to find it — while a node
    /// carries exactly one Type, so one of the two letters would otherwise have to lose.
    ///
    /// ⛔ REACHABILITY ONLY. `Key` remains the single identity for the role word, the speak order
    /// and the user's per-type announcement settings; an alias adds a way to ARRIVE at the control
    /// and never a second identity — otherwise two types could claim the same settings row.
    std::vector<std::string> Aliases;
};

/// The behaviors of a control, as data. Announcements is required (its parts compose the spoken
/// focus readout; the FIRST part is the control's label for search/dedupe purposes); the rest are
/// optional — an empty slot means the control doesn't have that behavior and the navigator speaks
/// its "nothing there" feedback instead.
struct NodeVtable
{
    /// Required, at least one part. The control's spoken focus readout. Parts marked Live re-speak
    /// on change while focused. When Type is set, the type's common parts merge in and the type's
    /// kind order applies; otherwise parts speak in declaration order.
    std::vector<NodeAnnouncement> Announcements;

    /// The control's type (registry value) — supplies the role word, the speak order, and the
    /// per-type announcement settings identity. Null = an untyped one-off. Non-owning.
    const ControlType* Type = nullptr;

    /// Optional. Primary activation — the left-click equivalent (Enter).
    std::function<void()> OnActivate;

    /// Optional. Secondary activation — the right-click equivalent.
    std::function<void()> OnSecondary;

    /// Optional. Shift-modified activation (Shift+Enter) — the shift-drag equivalent.
    std::function<void()> OnActivateShift;

    /// Optional. Ctrl-modified activation (Ctrl+Enter) — the ctrl-drag equivalent.
    std::function<void()> OnActivateCtrl;

    /// Optional. HELD activation — Enter kept down past the navigator's hold threshold. This is
    /// the game's own hold-to-confirm gesture, for controls that carry a second, weightier verb
    /// (a skill grid's "spend the point" behind its "inspect this upgrade").
    ///
    /// ⚠️ Declaring it makes the plain Enter on THIS node fire on RELEASE instead of on press —
    /// tap and hold cannot be told apart until one of them completes. Nodes that leave it empty
    /// keep instant activation, so the latency is paid only where it buys a second gesture.
    std::function<void()> OnActivateHold;

    /// Optional. Read / open the control's tooltip (Space, F1). The action owns the whole
    /// behavior, so the kernel stays game-agnostic.
    std::function<void()> OnTooltip;

    /// Optional. Horizontal value adjust (a slider): sign is -1 (decrease) / +1 (increase), large
    /// requests a coarse step. When set, Left/Right do NOT navigate.
    std::function<void(int sign, bool large)> OnAdjust;

    /// Optional. The control's state line, spoken IMMEDIATELY after an activation/adjust that
    /// changes state — the synchronous feedback path for rapid key repeats.
    /// Asynchronous/game-driven changes ride the Live announcement watch instead.
    std::function<std::string()> StateText;

    /// Optional. The text type-ahead matches against; empty = the first announcement part (the
    /// label).
    std::function<std::string()> SearchText;

    /// If true, type-ahead never matches this control.
    bool ExcludeFromSearch = false;

    // Sounds are host-typed but stored OPAQUE (std::any) — this file is STL-only by design; the
    // navigator casts at its chokepoints. Empty = the host's default for that slot.

    /// The game's themed hover sound for a focus MOVE onto this control. Played by the navigator
    /// exactly on the keypress-driven move paths.
    std::any HoverSound;

    /// The game's themed click sound for activation. Takes precedence over ActivateSound.
    std::any ClickSound;

    /// The activation sound; empty (and no ClickSound) = the control's own action plays its
    /// sound, so the navigator adds nothing.
    std::any ActivateSound;

    /// Optional (expandable groups): override HOW expansion state changes. When empty the engine
    /// mutates the persistent expansion set (GraphState::Expanded).
    std::function<void()> OnExpand;
    std::function<void()> OnCollapse;

    /// Set when this group's own announcements already include its expanded/collapsed state, so
    /// the announcer doesn't append it again.
    bool SpeaksOwnExpansion = false;

    /// Set when this node's announcements already include its list position, so the announcer
    /// doesn't append the auto-stamped one.
    bool SpeaksOwnPosition = false;

    /// The node's LANDING GROUP — the general row-vanish rule. When the focused node disappears,
    /// reconcile lands on the nearest survivor OF THE SAME GROUP (next row first, then previous),
    /// never on a header, a lead line or another panel. Only nodes stamped with a group take part;
    /// the vanished node's group is remembered at focus time. Empty = not a group member.
    std::string LandGroup;

    /// The node's LOGICAL COLUMN in a tabular row (0 = the row's primary), or -1 when not tabular.
    /// Stamped by GraphSheet; the engine uses it to PRESERVE the column when focus jumps
    /// NON-directionally — the reconcile fallback after a row vanishes — by sliding along the
    /// destination row's Left/Right edges to the same column. Ordinary arrow moves never consult
    /// it: they already went where the player pointed.
    int Column = -1;
};

/// A directed edge to another node, with an optional spoken transition line (a "lane change" —
/// e.g. crossing into a new column band).
struct Transition
{
    ControlId Destination;
    std::string Label; // spoken only while crossing this edge; empty = silent edge
};

/// A control: identity, behaviors, directional transitions, and structural metadata (its parent
/// chain, tab-stop and region membership, expandability). Owned by its GraphRender's pool, so
/// Parent pointers stay valid for the render's lifetime.
struct GraphNode
{
    ControlId Id;
    NodeVtable Vtable;

    /// Directional edges, indexed by GraphDir. Prefer the accessors below.
    std::array<std::optional<Transition>, kGraphDirCount> Transitions{};

    /// The node's structural parent within THIS render, or null at screen level. The parent chain
    /// IS the presentation hierarchy: the announcer prefix-diffs old/new chains by identity, so
    /// entering a group reads its levels outermost-first and descending from a group onto its own
    /// child re-announces nothing. A parent may be non-focusable pure structure (a labeled panel —
    /// Focusable false, never in Nodes/Order) or a real control (a tree group header).
    GraphNode* Parent = nullptr;

    /// False for a pure-structure parent node (a labeled panel): it exists only on Parent chains
    /// for announcements — never navigable, never in Nodes/Order.
    bool Focusable = true;

    /// This node is a group that can expand/collapse (a tree section header). The engine's tree
    /// operations (expand/collapse/descend/ascend) key off this.
    bool Expandable = false;

    /// An Expandable group's state AT THIS RENDER (stamped by the builder from the persistent
    /// expansion set, or the explicit value the declarer passed).
    bool Expanded = false;

    /// This node owns a FLYOUT column — a menu-bar drop-down (GraphBuilder::BeginFlyout). Down
    /// enters the column's first item, and the column's items carry this node as their Parent.
    /// Unlike Expandable, a flyout has NO persistent state: its items are declared, and present,
    /// in every render, so the owner stays an ordinary activatable control rather than becoming a
    /// container you open. A column item is one whose `Parent->FlyoutOwner` is set.
    bool FlyoutOwner = false;

    /// The number of items in this node's flyout column (0 when it owns none). Stamped by the
    /// builder so the announcer can say "submenu, 4 items" without walking the render.
    int FlyoutCount = 0;

    /// This owner's column is entered by the navigator's CONTEXT-MENU KEY only — Down keeps its
    /// ordinary row-to-row meaning. Set for an owner living in a VERTICAL list, where Down is
    /// already "next item"; a horizontal strip (the hub) has Down free and leaves this false.
    /// Meaningless unless FlyoutOwner.
    bool FlyoutKeyOnly = false;

    /// The Tab-stop this node belongs to (empty = none). Nodes sharing a StopKey form one stop;
    /// Tab cycles stops in first-appearance order, landing on the stop's remembered position.
    std::string StopKey;

    /// The region (within a stop) this node belongs to, or empty. Ctrl+arrows jump between
    /// regions in first-appearance order.
    std::string RegionKey;

    /// Auto-stamped sibling position (1-based) and count, from the builder — "3 of 10" among the
    /// siblings arrows actually reach. 0 = none (single sibling, raw/grid nodes).
    int PositionIndex = 0;
    int PositionCount = 0;

    /// On a parent (context/group) node: its direct children get NO auto position — for log-like
    /// streams where "37 of 200" is noise.
    bool SuppressChildPositions = false;

    bool HasTransition(GraphDir dir) const { return Transitions[static_cast<std::size_t>(dir)].has_value(); }

    const Transition* GetTransition(GraphDir dir) const
    {
        const auto& t = Transitions[static_cast<std::size_t>(dir)];
        return t ? &*t : nullptr;
    }

    void SetTransition(GraphDir dir, Transition t)
    {
        Transitions[static_cast<std::size_t>(dir)] = std::move(t);
    }
};

/// One built snapshot of a graph: the nodes (keyed by structural identity), their order of
/// declaration, and where focus starts when there is no prior position. Rebuilt per operation and
/// thrown away — live state belongs in the node callbacks, not here. Pool owns every node the
/// build created (including non-focusable context parents that are never in Nodes/Order), keeping
/// Parent pointers valid.
struct GraphRender
{
    ControlId StartKey;
    std::unordered_map<ControlId, GraphNode*> Nodes;

    /// Declaration order — drives stop/region cycling and type-ahead scan order.
    std::vector<GraphNode*> Order;

    std::vector<std::unique_ptr<GraphNode>> Pool;

    GraphNode* NodeAt(const ControlId& key) const
    {
        if (!key.IsValid())
            return nullptr;
        auto it = Nodes.find(key);
        return it != Nodes.end() ? it->second : nullptr;
    }
};

using ControlIdSet = std::unordered_set<ControlId>;

/// The persistent cursor for a graph — the only thing that survives between renders. Holds where
/// focus is, the last computed traversal order (for closest-survivor recovery), per-stop
/// remembered positions (so Tab returns to where you were in a stop), and a one-shot move
/// request.
struct GraphState
{
    /// The focused control's id (carries its Reference for tier-1 recovery). Invalid until the
    /// first render.
    ControlId CurKey;

    /// The down-right total order from the previous render. Empty on first render.
    std::vector<ControlId> KeyOrder;

    /// If valid, focus jumps here on the next render when present (consumed either way).
    ControlId NextSuggestedMove;

    /// Remembered position per Tab-stop: where Tab lands when cycling back into a stop.
    std::unordered_map<std::string, ControlId> StopMemory;

    /// The logical column focus last sat on in a tabular row (NodeVtable::Column), or -1. The
    /// reconcile fallbacks slide to this column when the focused row vanished, so acting on rows
    /// in sequence keeps your place in the table rather than dumping you back at column 0.
    int LastColumn = -1;

    /// The focused node's NodeVtable::LandGroup (empty when it had none) — the row-vanish
    /// fallback prefers survivors of this group.
    std::string LastLandGroup;

    /// The expanded groups (by id). The builder consults this for groups declared without an
    /// explicit state; the engine's expand/collapse operations mutate it. Screens hold NO
    /// expansion state of their own.
    ControlIdSet Expanded;

    /// This cursor with every ControlId::Reference dropped, for a host that keeps a graph's
    /// position across a GAP in rendering (its screen covered by another, then uncovered). A
    /// reference is only meaningful to the render that produced it; across a gap its object may
    /// be gone and its address reused, so tier-1 reconciliation could match a stranger. With the
    /// references gone, reconciliation resolves by structural key (tier 2) and the fallbacks.
    GraphState WithoutReferences() const
    {
        auto strip = [](ControlId id) {
            id.Reference = nullptr;
            return id;
        };
        GraphState out;
        out.CurKey = strip(CurKey);
        out.NextSuggestedMove = strip(NextSuggestedMove);
        out.KeyOrder.reserve(KeyOrder.size());
        for (const ControlId& id : KeyOrder)
            out.KeyOrder.push_back(strip(id));
        for (const auto& kv : StopMemory)
            out.StopMemory.emplace(kv.first, strip(kv.second));
        out.LastColumn = LastColumn;
        out.LastLandGroup = LastLandGroup;
        for (const ControlId& id : Expanded)
            out.Expanded.insert(strip(id));
        return out;
    }
};

} // namespace X2Access::Graph
