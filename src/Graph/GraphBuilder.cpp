#include "GraphBuilder.hpp"

#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace X2Access::Graph
{

GraphBuilder::GraphBuilder(const ControlIdSet* expansion)
    : _expansion(expansion)
{
}

// ---- stops / regions ----

GraphBuilder& GraphBuilder::BeginStop(std::string key)
{
    if (_currentRow)
        throw std::logic_error("Cannot begin a stop inside an open row");
    _stopKey = key.empty() ? AutoStopKey(_stopAuto) : std::move(key);
    _stopAuto++;
    _regionKey.clear(); // regions are per-stop
    return *this;
}

GraphBuilder& GraphBuilder::SetRegion(std::string key)
{
    _regionKey = std::move(key);
    return *this;
}

// ---- the parent stack: contexts + groups ----

GraphBuilder& GraphBuilder::PushContext(const std::string& label, const std::string& role, bool positions)
{
    GraphNode* parent = CurrentParent();
    auto node = std::make_unique<GraphNode>();
    // Stable synthetic identity (label-pathed) so cross-render chain diffs match up.
    node->Id = ControlId::Structural(
        "ctx:" + (parent ? parent->Id.StructuralKey : std::string()) + "/" + label);
    node->Vtable.Announcements.push_back(NodeAnnouncement::Static(label));
    if (!role.empty())
        node->Vtable.Announcements.push_back(NodeAnnouncement::Static(role));
    node->Parent = parent;
    node->Focusable = false;
    node->SuppressChildPositions = !positions;

    GraphNode* raw = node.get();
    _pool.push_back(std::move(node));
    _parents.push_back({raw, Suppressed()});
    return *this;
}

GraphBuilder& GraphBuilder::PopContext()
{
    if (_parents.empty())
        throw std::logic_error("No context/group to pop");
    _parents.pop_back();
    return *this;
}

GraphBuilder& GraphBuilder::BeginGroup(ControlId id, NodeVtable vtable, std::optional<bool> expanded,
    bool defaultExpanded)
{
    if (!id.IsValid())
        throw std::invalid_argument("BeginGroup requires a valid id");
    if (_currentRow)
        throw std::logic_error("Cannot begin a group inside an open row");
    if (_currentFlyout >= 0)
        throw std::logic_error("Cannot begin a group inside a flyout");
    bool isExpanded = expanded.has_value()
        ? *expanded
        : (_expansion ? _expansion->count(id) > 0 : defaultExpanded);

    GraphNode* header = nullptr;
    if (!Suppressed())
    {
        header = MakeNode(std::move(id), std::move(vtable));
        header->Expandable = true;
        header->Expanded = isExpanded;
        Row* row = NewRow();
        row->StopKey = _stopKey;
        row->Items.push_back(header);
        _rows.push_back(row);
        _rowOf[header] = row;
    }
    _parents.push_back({
        // Suppressed subtree: keep chaining from the outer parent so the stack stays coherent.
        header ? header : CurrentParent(),
        Suppressed() || !isExpanded,
    });
    return *this;
}

// ---- flyouts ----

GraphBuilder& GraphBuilder::BeginFlyout(ControlId owner, FlyoutEntry entry)
{
    if (_currentRow)
        throw std::logic_error("Cannot begin a flyout inside an open row");
    if (_currentFlyout >= 0)
        throw std::logic_error("Flyouts cannot nest");

    if (Suppressed())
    {
        // Under a collapsed group nothing was declared, so there is no owner to hang a column on.
        // Swallow the whole block exactly as AddItem does, rather than throwing — otherwise a
        // screen that works while its group is expanded kills its own render when the user
        // collapses it.
        _flyouts.push_back(Flyout{nullptr, {}, entry});
        _currentFlyout = static_cast<int>(_flyouts.size()) - 1;
        _parents.push_back({CurrentParent(), true});
        return *this;
    }

    GraphNode* node = FindDeclared(owner);
    if (!node)
        throw std::invalid_argument("BeginFlyout owner must already be declared: " + owner.ToString());
    if (!_rowOf.count(node))
        throw std::logic_error("Flyout owner must be a menu item: " + owner.ToString());
    for (const Flyout& f : _flyouts)
        if (f.Owner == node)
            throw std::logic_error("Duplicate flyout for owner: " + owner.ToString());

    _flyouts.push_back(Flyout{node, {}, entry});
    _currentFlyout = static_cast<int>(_flyouts.size()) - 1;
    // The owner IS the column's parent level: the announcer's path diff then swallows it on a
    // descend (owner is both the from-node and on the child's chain), exactly as for a group.
    _parents.push_back({node, Suppressed()});
    return *this;
}

GraphBuilder& GraphBuilder::EndFlyout()
{
    if (_currentFlyout < 0)
        throw std::logic_error("No flyout to end");
    _currentFlyout = -1;
    return PopContext();
}

GraphNode* GraphBuilder::FindDeclared(const ControlId& id) const
{
    for (GraphNode* n : _declared)
        if (n->Id == id)
            return n;
    return nullptr;
}

bool GraphBuilder::InFlyoutRow(const GraphNode* node) const
{
    auto it = _rowOf.find(const_cast<GraphNode*>(node));
    return it != _rowOf.end() && it->second->Flyout;
}

bool GraphBuilder::IsExpanded(const ControlId& id) const
{
    return _expansion && id.IsValid() && _expansion->count(id) > 0;
}

GraphBuilder& GraphBuilder::SetStart(ControlId id)
{
    _start = std::move(id);
    return *this;
}

// ---- menu mode ----

GraphBuilder& GraphBuilder::StartRow(std::string rowKey)
{
    if (_currentRow)
        throw std::logic_error("Cannot start a row while another is open");
    // A flyout column is vertical by construction — its items are single-item rows.
    if (_currentFlyout >= 0)
        throw std::logic_error("Cannot start a row inside a flyout");
    Row* row = NewRow();
    row->Key = std::move(rowKey);
    row->StopKey = _stopKey;
    _currentRow = row;
    return *this;
}

GraphBuilder& GraphBuilder::EndRow()
{
    if (!_currentRow)
        throw std::logic_error("No row to end");
    if (_currentRow->Items.empty() && !Suppressed())
        throw std::logic_error("Row cannot be empty");
    if (!_currentRow->Items.empty())
        _rows.push_back(_currentRow);
    _currentRow = nullptr;
    return *this;
}

GraphBuilder& GraphBuilder::AddItem(ControlId id, NodeVtable vtable)
{
    if (Suppressed())
        return *this;
    GraphNode* node = MakeNode(std::move(id), std::move(vtable));
    if (_currentRow)
    {
        _currentRow->Items.push_back(node);
        _rowOf[node] = _currentRow;
    }
    else
    {
        Row* row = NewRow();
        row->StopKey = _stopKey;
        row->Flyout = _currentFlyout >= 0;
        row->Items.push_back(node);
        _rows.push_back(row);
        _rowOf[node] = row;
    }
    // Flyout columns stay single-item rows (StartRow is refused inside one), so this is the only
    // path that can reach a column. Membership is recorded for WireFlyoutEdges; the row stays in
    // _rows so StampPositions still stamps "n of m" within the column.
    if (_currentFlyout >= 0)
        _flyouts[static_cast<std::size_t>(_currentFlyout)].Items.push_back(node);
    return *this;
}

GraphBuilder& GraphBuilder::AddLabel(ControlId id, std::function<std::string()> label)
{
    NodeVtable vtable;
    vtable.Announcements.emplace_back(std::move(label));
    return AddItem(std::move(id), std::move(vtable));
}

// ---- raw mode ----

GraphBuilder& GraphBuilder::AddNode(ControlId id, NodeVtable vtable)
{
    if (_currentFlyout >= 0)
        throw std::logic_error("Cannot add a raw node inside a flyout");
    if (Suppressed())
        return *this;
    _rawNodes.push_back(MakeNode(std::move(id), std::move(vtable)));
    return *this;
}

GraphBuilder& GraphBuilder::Connect(ControlId from, GraphDir dir, ControlId to, std::string label)
{
    if (!from.IsValid() || !to.IsValid())
        throw std::invalid_argument("Connect requires valid ids");
    _rawEdges.push_back({std::move(from), dir, std::move(to), std::move(label)});
    return *this;
}

GraphNode* GraphBuilder::MakeNode(ControlId id, NodeVtable vtable)
{
    if (!id.IsValid())
        throw std::invalid_argument("Control id must be valid");
    if (vtable.Announcements.empty())
        throw std::invalid_argument("A control must have at least one announcement");
    if (!_ids.insert(id).second)
        throw std::logic_error("Duplicate control id: " + id.ToString());

    auto node = std::make_unique<GraphNode>();
    node->Id = std::move(id);
    node->Vtable = std::move(vtable);
    node->Parent = CurrentParent();
    node->StopKey = _stopKey;
    node->RegionKey = _regionKey;

    GraphNode* raw = node.get();
    _pool.push_back(std::move(node));
    _declared.push_back(raw);
    return raw;
}

GraphBuilder::Row* GraphBuilder::NewRow()
{
    _rowPool.push_back(std::make_unique<Row>());
    return _rowPool.back().get();
}

// ---- build ----

std::unique_ptr<GraphRender> GraphBuilder::Build()
{
    if (_currentRow)
        throw std::logic_error("Unclosed row - call EndRow()");
    if (_currentFlyout >= 0)
        throw std::logic_error("Unclosed flyout - call EndFlyout()");
    if (_built)
        throw std::logic_error("Build() may only be called once");
    if (_rawNodes.empty() && _rows.empty())
        return nullptr;
    _built = true;

    auto render = std::make_unique<GraphRender>();
    for (GraphNode* node : _declared)
    {
        render->Nodes.emplace(node->Id, node);
        render->Order.push_back(node);
    }

    WireMenuEdges();
    // After the row passes (a Down-entry column overrides its owner's row-to-row Down; a KeyOnly
    // one deliberately leaves it standing) but before raw edges, the explicit escape hatch.
    WireFlyoutEdges();
    for (const RawEdge& e : _rawEdges)
        if (render->Nodes.count(e.From) && render->Nodes.count(e.To))
            render->Nodes[e.From]->SetTransition(e.Dir, Transition{e.To, e.Label});
    StitchModeBoundaries();

    render->StartKey = (_start.IsValid() && render->Nodes.count(_start))
        ? _start
        : render->Order[0]->Id;
    StampPositions();

    render->Pool = std::move(_pool);
    return render;
}

// Where a stop mixes MENU rows with RAW content (search/sort/filter controls above a sheet), the
// two wiring systems don't see each other: menu auto-wiring connects only menu rows, and the raw
// content's explicit edges stop at its own borders — leaving a vertical gap arrows can't cross.
// Stitch it: at each menu→raw boundary (declaration order, same stop), the menu row's cells gain
// Down edges into the first raw node still missing an Up edge, and that node gains the Up back;
// at raw→menu boundaries the reverse. Only MISSING edges are filled — the raw content's own
// wiring is never overridden.
void GraphBuilder::StitchModeBoundaries()
{
    std::unordered_map<std::string, std::vector<GraphNode*>> byStop;
    std::vector<std::string> stops;
    for (GraphNode* n : _declared)
    {
        // A flyout column is never a menu↔raw seam, and it must not shadow the real one: a column
        // declared between a menu row and a raw block would otherwise sit at the boundary and
        // absorb the stitch.
        if (InFlyoutRow(n))
            continue;
        auto [it, inserted] = byStop.try_emplace(n->StopKey);
        if (inserted)
            stops.push_back(n->StopKey);
        it->second.push_back(n);
    }

    for (const std::string& stop : stops)
    {
        auto& nodes = byStop[stop];
        for (std::size_t i = 1; i < nodes.size(); i++)
        {
            GraphNode* prev = nodes[i - 1];
            GraphNode* cur = nodes[i];
            bool prevMenu = _rowOf.count(prev) > 0;
            bool curMenu = _rowOf.count(cur) > 0;
            if (prevMenu == curMenu)
                continue; // same mode — its own wiring covers it

            if (prevMenu) // menu row above raw content: row cells ↓ first raw node without an Up
            {
                if (cur->HasTransition(GraphDir::Up))
                    continue;
                Row* row = _rowOf[prev];
                for (GraphNode* cell : row->Items)
                    if (!cell->HasTransition(GraphDir::Down))
                        cell->SetTransition(GraphDir::Down, Transition{cur->Id});
                cur->SetTransition(GraphDir::Up, Transition{row->Items[0]->Id});
            }
            else // raw content above a menu row: last raw node without a Down ↕ the row
            {
                Row* row = _rowOf[cur];
                // The raw side's bottom = the latest raw node (walking back) missing a Down.
                GraphNode* bottom = nullptr;
                for (int j = static_cast<int>(i) - 1; j >= 0 && _rowOf.count(nodes[j]) == 0; j--)
                    if (!nodes[j]->HasTransition(GraphDir::Down))
                    {
                        bottom = nodes[j];
                        break;
                    }
                if (!bottom)
                    continue;
                bottom->SetTransition(GraphDir::Down, Transition{row->Items[0]->Id});
                for (GraphNode* cell : row->Items)
                    if (!cell->HasTransition(GraphDir::Up))
                        cell->SetTransition(GraphDir::Up, Transition{bottom->Id});
            }
        }
    }
}

// Auto-stamp "n of m" positions: a multi-item row's members are positioned within their ROW (a
// bar); single-item-row nodes among the siblings sharing their (parent, stop, segment) — the
// vertical list/tree level arrows actually traverse. Raw/grid nodes get none. Announced only when
// m > 1.
void GraphBuilder::StampPositions()
{
    // Sibling runs must break exactly where the vertical chain breaks (WireMenuEdges): raw
    // content interleaved in a stop severs the chain, so counting across it would announce
    // "n of m" over nodes arrows cannot traverse between (spec §4.6). A multi-item row does not
    // break a run — the chain passes through it. Flyout columns never break one either: they sit
    // outside the strip's chain and group by their owner-parent below.
    std::unordered_map<const GraphNode*, int> segmentOf;
    std::unordered_map<std::string, int> stopSegment; // stop → its currently-open segment ordinal
    for (GraphNode* node : _declared)
    {
        auto rowIt = _rowOf.find(node);
        if (rowIt != _rowOf.end() && rowIt->second->Flyout)
        {
            segmentOf[node] = 0; // a column's items all share (owner, stop) — one run regardless
            continue;
        }
        if (rowIt != _rowOf.end())
            segmentOf[node] = stopSegment[node->StopKey];
        else
            stopSegment[node->StopKey]++; // raw node: close this stop's open run
    }

    std::vector<std::vector<GraphNode*>> groups;
    std::map<std::tuple<GraphNode*, std::string, int>, std::size_t> groupIndex;
    for (Row* row : _rows)
    {
        if (row->Items.size() > 1)
        {
            Stamp(row->Items);
            continue;
        }
        GraphNode* node = row->Items[0];
        if (node->Parent && node->Parent->SuppressChildPositions)
            continue;
        auto key = std::make_tuple(node->Parent, node->StopKey, segmentOf[node]);
        auto it = groupIndex.find(key);
        std::size_t idx;
        if (it == groupIndex.end())
        {
            groups.emplace_back();
            idx = groups.size() - 1;
            groupIndex.emplace(std::move(key), idx);
        }
        else
        {
            idx = it->second;
        }
        groups[idx].push_back(node);
    }
    for (auto& siblings : groups)
        Stamp(siblings);
}

void GraphBuilder::Stamp(std::vector<GraphNode*>& siblings)
{
    if (siblings.size() < 2)
        return;
    for (std::size_t i = 0; i < siblings.size(); i++)
    {
        siblings[i]->PositionIndex = static_cast<int>(i) + 1;
        siblings[i]->PositionCount = static_cast<int>(siblings.size());
    }
}

// Left/right within a row; up/down between consecutive rows OF THE SAME STOP (arrows never cross
// a Tab-stop). Shared non-empty row keys preserve the column; otherwise vertical lands on the
// first item.
void GraphBuilder::WireMenuEdges()
{
    // Segment rows in DECLARATION order: within a stop, consecutive menu rows chain vertically
    // only when no raw node was declared between them. Interleaved raw content BREAKS the chain —
    // StitchModeBoundaries wires the seams. Without the break, menu edges would skip straight
    // over the raw block; the stitcher (which only fills missing edges) would find the gap
    // already bridged, leaving the block an unreachable island.
    std::vector<std::vector<Row*>> byStop;
    std::unordered_map<std::string, std::size_t> openSegment; // stop → its currently-open segment
    for (GraphNode* node : _declared)
    {
        auto rowIt = _rowOf.find(node);
        // Flyout columns wire themselves. Skipping rather than closing the stop's open segment
        // keeps a menu row declared AFTER a flyout chained to the one before it.
        if (rowIt != _rowOf.end() && rowIt->second->Flyout)
            continue;
        if (rowIt != _rowOf.end())
        {
            Row* row = rowIt->second;
            auto segIt = openSegment.find(node->StopKey);
            std::size_t segIdx;
            if (segIt == openSegment.end())
            {
                byStop.emplace_back();
                segIdx = byStop.size() - 1;
                openSegment.emplace(node->StopKey, segIdx);
            }
            else
            {
                segIdx = segIt->second;
            }
            auto& seg = byStop[segIdx];
            if (seg.empty() || seg.back() != row)
                seg.push_back(row);
        }
        else
        {
            openSegment.erase(node->StopKey); // raw node: close this stop's segment
        }
    }

    for (auto& rows : byStop)
    {
        for (std::size_t r = 0; r < rows.size(); r++)
        {
            Row* row = rows[r];
            for (std::size_t pos = 0; pos < row->Items.size(); pos++)
            {
                GraphNode* node = row->Items[pos];
                if (r > 0)
                    node->SetTransition(GraphDir::Up, Transition{VerticalTarget(*row, *rows[r - 1], pos)});
                if (r < rows.size() - 1)
                    node->SetTransition(GraphDir::Down, Transition{VerticalTarget(*row, *rows[r + 1], pos)});
                if (pos > 0)
                    node->SetTransition(GraphDir::Left, Transition{row->Items[pos - 1]->Id});
                if (pos < row->Items.size() - 1)
                    node->SetTransition(GraphDir::Right, Transition{row->Items[pos + 1]->Id});
            }
        }
    }
}

// The menu-bar drop-down: the owner's column is entered by Down or by the navigator's
// context-menu key (FlyoutEntry), Up walks back out through the column's head, and Left leaves
// the column from anywhere in it. The column is NOT in the strip's row-to-row chain, so the strip
// items beside the owner are unaffected — which is the whole difference from a tree, where the
// children live in the parent's own vertical walk.
//
// An empty column leaves the owner a plain item (FlyoutOwner stays false): screens filter their
// children and must not have to pre-count them.
void GraphBuilder::WireFlyoutEdges()
{
    for (const Flyout& f : _flyouts)
    {
        if (!f.Owner || f.Items.empty()) // null owner = declared under a collapsed group
            continue;

        // Both are the right-click equivalent and share the navigator's one key, so a node
        // carrying both would silently never run its secondary action. Fail loudly at build.
        if (f.Owner->Vtable.OnSecondary)
            throw std::logic_error(
                "Flyout owner must not also declare OnSecondary: " + f.Owner->Id.ToString());

        f.Owner->FlyoutOwner = true;
        f.Owner->FlyoutCount = static_cast<int>(f.Items.size());
        f.Owner->FlyoutKeyOnly = f.Entry == FlyoutEntry::KeyOnly;
        // Down-entry takes the owner's Down: on a horizontal strip nothing else wants it. KeyOnly
        // leaves it alone, because a vertical list needs Down to go on meaning "next item".
        if (f.Entry == FlyoutEntry::Down)
            f.Owner->SetTransition(GraphDir::Down, Transition{f.Items.front()->Id});

        for (std::size_t i = 0; i < f.Items.size(); i++)
        {
            GraphNode* item = f.Items[i];
            item->SetTransition(GraphDir::Left, Transition{f.Owner->Id});
            item->SetTransition(
                GraphDir::Up, Transition{i == 0 ? f.Owner->Id : f.Items[i - 1]->Id});
            if (i + 1 < f.Items.size())
                item->SetTransition(GraphDir::Down, Transition{f.Items[i + 1]->Id});
        }
    }
}

// Where vertical navigation from position pos lands in the adjacent row: the same position when
// the rows share a non-empty key (column nav) and it exists there, else the first item.
ControlId GraphBuilder::VerticalTarget(const Row& from, const Row& to, std::size_t pos)
{
    if (!from.Key.empty() && from.Key == to.Key && pos < to.Items.size())
        return to.Items[pos]->Id;
    return to.Items[0]->Id;
}

} // namespace X2Access::Graph
