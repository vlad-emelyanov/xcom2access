#include "KeyGraph.hpp"

#include <algorithm>

namespace X2Access::Graph
{

namespace
{

int IndexOf(const std::vector<ControlId>& order, const ControlId& key)
{
    for (std::size_t i = 0; i < order.size(); i++)
        if (order[i] == key)
            return static_cast<int>(i);
    return -1;
}

int IndexOf(const std::vector<std::string>& keys, const std::string& key)
{
    for (std::size_t i = 0; i < keys.size(); i++)
        if (keys[i] == key)
            return static_cast<int>(i);
    return -1;
}

} // namespace

KeyGraph::KeyGraph(RenderCallback renderCallback, GraphState* state)
    : _renderCallback(std::move(renderCallback))
    , _state(state)
{
}

GraphNode* KeyGraph::CurrentNode()
{
    return _current ? _current->NodeAt(_state->CurKey) : nullptr;
}

bool KeyGraph::Rerender()
{
    _current = _renderCallback();
    if (!_current || _current->Nodes.empty())
    {
        _current.reset();
        return false;
    }
    Reconcile(*_current, *_state);
    return true;
}

void KeyGraph::Reconcile(GraphRender& render, GraphState& state)
{
    // Honor a pending suggested move first, if its target still exists (consumed either way).
    if (state.NextSuggestedMove.IsValid())
    {
        if (GraphNode* target = render.NodeAt(state.NextSuggestedMove))
            state.CurKey = target->Id;
        state.NextSuggestedMove = {};
    }

    ControlId old = state.CurKey;
    ControlId resolved;

    if (old.IsValid())
    {
        // Tier 1: the same backing object, even if its structural key changed (it moved).
        //
        // A reference is meant to name ONE node (ControlId.hpp), but a screen that derives several
        // nodes from a single object would otherwise pin the cursor to whichever of them this scan
        // reached first — an arbitrary pick out of an unordered_map, and the same pick every time,
        // so every move bounces back to it and the region cannot be walked at all. Preferring the
        // candidate that still carries the old structural key costs one comparison, leaves the
        // unique-reference case exactly as it was, and makes the duplicated one merely useless
        // rather than actively hostile.
        if (old.Reference)
        {
            for (const auto& kv : render.Nodes)
            {
                if (!kv.second->Id.ReferenceMatches(old.Reference))
                    continue;
                if (kv.second->Id == old)
                {
                    resolved = kv.second->Id; // same object AND same key — nothing moved
                    break;
                }
                if (!resolved.IsValid())
                    resolved = kv.second->Id; // it may have moved; keep looking for an exact hit
            }
        }

        // Tier 2: the same structural key, even if the backing object was rebuilt.
        if (!resolved.IsValid())
            if (GraphNode* structural = render.NodeAt(old))
                resolved = structural->Id;

        // Tier 3 — the GENERAL row-vanish rule: land on the nearest survivor OF THE SAME LANDING
        // GROUP (this list's rows), next-first then previous — never a header, a lead line or
        // another panel. Only nodes stamped with a LandGroup take part; the vanished node's group
        // was remembered at focus time.
        if (!resolved.IsValid() && !state.KeyOrder.empty() && !state.LastLandGroup.empty())
        {
            int oldIndex = IndexOf(state.KeyOrder, old);
            if (oldIndex >= 0)
            {
                const int count = static_cast<int>(state.KeyOrder.size());
                for (int off = 1; off < count && !resolved.IsValid(); off++)
                {
                    resolved = GroupSurvivorAt(render, state, oldIndex + off); // next row first
                    if (!resolved.IsValid())
                        resolved = GroupSurvivorAt(render, state, oldIndex - off);
                }
                if (resolved.IsValid())
                    resolved = SlideToColumn(render, resolved, state.LastColumn);
            }
        }

        // Fallback: nearest survivor walking the previous order backward. The order interleaves a
        // table's row CELLS, so when a whole row vanished the walk lands on the previous row's LAST
        // cell — a different column. Slide along that row to the column focus was on, so acting on
        // rows in sequence keeps your place in the table.
        if (!resolved.IsValid() && !state.KeyOrder.empty())
        {
            int oldIndex = IndexOf(state.KeyOrder, old);
            if (oldIndex >= 0)
                for (int i = oldIndex; i >= 0; i--)
                    if (GraphNode* survivor = render.NodeAt(state.KeyOrder[i]))
                    {
                        resolved = SlideToColumn(render, survivor->Id, state.LastColumn);
                        break;
                    }
        }
    }

    // Nothing matched (or first render): the start node — but prefer the SELECTED member of its
    // stop (initial focus lands on the checked radio/tab, not the top of a long list).
    if (!resolved.IsValid())
    {
        GraphNode* startNode = render.NodeAt(render.StartKey);
        GraphNode* sel = startNode ? SelectedNodeInStop(render, startNode->StopKey) : nullptr;
        resolved = sel ? sel->Id : (startNode ? startNode->Id : render.StartKey);
    }

    state.CurKey = resolved;
    RememberStop(render, state, resolved);
    RememberColumn(render, state, resolved);
    RememberLandGroup(render, state, resolved);
    state.KeyOrder = ComputeOrder(render);
}

ControlId KeyGraph::GroupSurvivorAt(const GraphRender& render, const GraphState& state, int index)
{
    if (index < 0 || index >= static_cast<int>(state.KeyOrder.size()))
        return {};
    GraphNode* n = render.NodeAt(state.KeyOrder[index]);
    if (!n)
        return {};
    return n->Vtable.LandGroup == state.LastLandGroup ? n->Id : ControlId{};
}

void KeyGraph::RememberColumn(const GraphRender& render, GraphState& state, const ControlId& key)
{
    GraphNode* node = render.NodeAt(key);
    // Only a TABULAR node updates it. Leaving the last column alone when focus passes over an
    // untabbed node (a section header between two tables) is what lets the column survive the
    // trip: forgetting it there would drop the player back at column 0 on the next row.
    if (node && node->Vtable.Column >= 0)
        state.LastColumn = node->Vtable.Column;
}

void KeyGraph::RememberLandGroup(const GraphRender& render, GraphState& state, const ControlId& key)
{
    GraphNode* node = render.NodeAt(key);
    state.LastLandGroup = node ? node->Vtable.LandGroup : std::string();
}

ControlId KeyGraph::SlideToColumn(const GraphRender& render, const ControlId& start, int prefCol)
{
    GraphNode* node = render.NodeAt(start);
    if (!node || prefCol < 0)
        return start;
    int col = node->Vtable.Column;
    if (col < 0 || col == prefCol)
        return start;

    const GraphDir dir = col < prefCol ? GraphDir::Right : GraphDir::Left;
    GraphNode* cur = node;
    while (cur->Vtable.Column != prefCol)
    {
        const Transition* t = cur->GetTransition(dir);
        if (!t)
            break;
        GraphNode* next = render.NodeAt(t->Destination);
        if (!next || next->Vtable.Column < 0)
            break;
        if (dir == GraphDir::Right && next->Vtable.Column > prefCol)
            break; // sparse row: stop at the last cell below the wanted column
        if (dir == GraphDir::Left && next->Vtable.Column < prefCol)
        {
            cur = next; // overshot: the nearest cell below it is the answer
            break;
        }
        cur = next;
    }
    return cur->Id;
}

std::vector<ControlId> KeyGraph::ComputeOrder(const GraphRender& render)
{
    std::vector<ControlId> order;
    std::unordered_set<ControlId> seen;
    std::vector<ControlId> downFringe{render.StartKey};

    std::size_t i = 0;
    while (i < downFringe.size())
    {
        ControlId k = downFringe[i];
        while (!seen.count(k))
        {
            seen.insert(k);
            order.push_back(k);

            GraphNode* n = render.NodeAt(k);
            if (!n)
                break;

            if (const Transition* d = n->GetTransition(GraphDir::Down))
                downFringe.push_back(d->Destination);
            const Transition* t = n->GetTransition(GraphDir::Right);
            if (!t)
                break;
            k = t->Destination;
        }
        i++;
    }

    for (GraphNode* node : render.Order)
        if (seen.insert(node->Id).second)
            order.push_back(node->Id);

    return order;
}

void KeyGraph::RememberStop(const GraphRender& render, GraphState& state, const ControlId& key)
{
    GraphNode* node = render.NodeAt(key);
    if (node && !node->StopKey.empty())
        state.StopMemory[node->StopKey] = key;
}

void KeyGraph::SetCurrent(GraphNode* node)
{
    _state->CurKey = node->Id;
    if (!node->StopKey.empty())
        _state->StopMemory[node->StopKey] = node->Id;
    if (node->Vtable.Column >= 0)
        _state->LastColumn = node->Vtable.Column;
    _state->LastLandGroup = node->Vtable.LandGroup;
}

// ---- navigation operations ----

MoveResult KeyGraph::Move(GraphDir dir)
{
    MoveResult result{};
    if (!Rerender())
        return result;

    GraphNode* node = CurrentNode();
    result.From = node;
    result.To = node;
    if (!node)
        return result;

    const Transition* t = node->GetTransition(dir);
    GraphNode* dest = t ? _current->NodeAt(t->Destination) : nullptr;
    if (!dest || dest == node)
        return result;

    SetCurrent(dest);
    result.To = dest;
    result.Moved = true;
    result.TransitionLabel = t->Label;
    return result;
}

MoveResult KeyGraph::MoveToEdge(GraphDir dir)
{
    MoveResult result{};
    if (!Rerender())
        return result;

    GraphNode* node = CurrentNode();
    result.From = node;
    result.To = node;
    if (!node)
        return result;

    // Home/End must never leave your LINE. Without this, End on a flyout owner would follow the
    // column's Down edge and land inside the drop-down.
    const bool startInFlyout = InFlyout(node);
    const GraphNode* startParent = node->Parent;

    GraphNode* cur = node;
    while (true)
    {
        const Transition* t = cur->GetTransition(dir);
        if (!t)
            break;
        GraphNode* next = _current->NodeAt(t->Destination);
        if (!next || next == cur)
            break;
        if (InFlyout(next) != startInFlyout || (startInFlyout && next->Parent != startParent))
            break; // crossing a flyout boundary
        cur = next;
    }

    if (cur != node)
    {
        SetCurrent(cur);
        result.To = cur;
        result.Moved = true;
    }
    return result;
}

MoveResult KeyGraph::MoveStop(int dir, bool wrap)
{
    MoveResult result{};
    if (!Rerender())
        return result;

    GraphNode* node = CurrentNode();
    result.From = node;
    result.To = node;
    if (!node)
        return result;

    std::vector<std::string> stops = StopOrder();
    if (stops.size() <= 1)
        return result;

    int count = static_cast<int>(stops.size());
    int idx = IndexOf(stops, node->StopKey);
    if (idx < 0)
        return result;
    int ni = idx + dir;
    if (wrap)
        ni = ((ni % count) + count) % count;
    if (ni < 0 || ni >= count || ni == idx)
        return result;

    GraphNode* dest = StopLanding(stops[ni]);
    if (!dest)
        return result;

    SetCurrent(dest);
    result.To = dest;
    result.Moved = true;
    return result;
}

MoveResult KeyGraph::MoveRegion(int dir)
{
    MoveResult result{};
    if (!Rerender())
        return result;

    GraphNode* node = CurrentNode();
    result.From = node;
    result.To = node;
    if (!node || node->RegionKey.empty())
        return result;

    std::vector<std::string> regions;
    for (GraphNode* n : _current->Order)
        if (n->StopKey == node->StopKey && !n->RegionKey.empty()
            && IndexOf(regions, n->RegionKey) < 0)
            regions.push_back(n->RegionKey);

    int idx = IndexOf(regions, node->RegionKey);
    int ni = idx + dir;
    if (idx < 0 || ni < 0 || ni >= static_cast<int>(regions.size()))
        return result;

    for (GraphNode* n : _current->Order)
        if (n->StopKey == node->StopKey && n->RegionKey == regions[ni])
        {
            SetCurrent(n);
            result.To = n;
            result.Moved = true;
            return result;
        }
    return result;
}

MoveResult KeyGraph::MoveToType(int dir, const std::string& typeKey)
{
    MoveResult result{};
    if (!Rerender())
        return result;

    GraphNode* node = CurrentNode();
    result.From = node;
    result.To = node;
    if (typeKey.empty() || dir == 0 || _current->Order.empty())
        return result;

    const int count = static_cast<int>(_current->Order.size());

    // Where the search starts. A focus that is no longer in the order (or none at all) is not an
    // error here — it just means "start at the end the search is coming from".
    int at = -1;
    for (int i = 0; i < count; i++)
        if (_current->Order[i] == node)
        {
            at = i;
            break;
        }
    int i = at < 0 ? (dir > 0 ? 0 : count - 1) : at + dir;

    for (; i >= 0 && i < count; i += dir)
    {
        GraphNode* n = _current->Order[i];
        // A type answers to its own Key and to any alias it declares, so one control can be found
        // by two letters without pretending to be two types (ControlType::Aliases).
        const ControlType* type = n ? n->Vtable.Type : nullptr;
        if (type
            && (type->Key == typeKey
                || std::find(type->Aliases.begin(), type->Aliases.end(), typeKey)
                    != type->Aliases.end()))
        {
            SetCurrent(n);
            result.To = n;
            result.Moved = true;
            return result;
        }
    }
    return result;
}

bool KeyGraph::Focus(const ControlId& id)
{
    if (!id.IsValid() || !Rerender())
        return false;
    GraphNode* node = _current->NodeAt(id);
    if (!node)
        return false;
    SetCurrent(node);
    return true;
}

bool KeyGraph::FocusByReference(const void* reference)
{
    // Every operation acts on current reality (spec §5) — a game-driven sync arrives precisely
    // when the game just changed something, so the previous render is the one render guaranteed
    // to be stale.
    if (!reference || !Rerender())
        return false;
    for (const auto& kv : _current->Nodes)
        if (kv.second->Id.ReferenceMatches(reference))
        {
            bool changed = !_state->CurKey.IsValid() || _state->CurKey != kv.second->Id;
            SetCurrent(kv.second);
            return changed;
        }
    return false;
}

std::vector<std::string> KeyGraph::StopOrder() const
{
    std::vector<std::string> stops;
    for (GraphNode* n : _current->Order)
        if (!n->StopKey.empty() && IndexOf(stops, n->StopKey) < 0)
            stops.push_back(n->StopKey);
    return stops;
}

GraphNode* KeyGraph::StopLanding(const std::string& stopKey)
{
    return StopLandingIn(*_current, *_state, stopKey);
}

GraphNode* KeyGraph::StopLandingIn(const GraphRender& render, const GraphState& state, const std::string& stopKey)
{
    auto remembered = state.StopMemory.find(stopKey);
    if (remembered != state.StopMemory.end())
    {
        GraphNode* node = render.NodeAt(remembered->second);
        if (node && node->StopKey == stopKey)
            return node;
    }
    if (GraphNode* selected = SelectedNodeInStop(render, stopKey))
        return selected;
    for (GraphNode* n : render.Order)
        if (n->StopKey == stopKey)
            return n;
    return nullptr;
}

GraphNode* KeyGraph::SelectedNodeInStop(const GraphRender& render, const std::string& stopKey)
{
    for (GraphNode* n : render.Order)
    {
        if (n->StopKey != stopKey)
            continue;
        for (const NodeAnnouncement& a : n->Vtable.Announcements)
            if (a.Kind == AnnouncementKinds::Selected)
            {
                std::string t;
                try
                {
                    if (a.Text)
                        t = a.Text();
                }
                catch (...)
                {
                }
                if (!t.empty())
                    return n;
            }
    }
    return nullptr;
}

// ---- tree operations ----

bool KeyGraph::InTree(const GraphNode* node)
{
    for (const GraphNode* n = node; n; n = n->Parent)
        if (n->Expandable)
            return true;
    return false;
}

bool KeyGraph::InFlyout(const GraphNode* node)
{
    return node && node->Parent && node->Parent->FlyoutOwner;
}

KeyGraph::TreeResult KeyGraph::TreeRight()
{
    TreeResult result{};
    if (!Rerender())
        return result;
    GraphNode* node = CurrentNode();
    if (!node)
        return result;

    if (node->Expandable && !node->Expanded)
    {
        // The re-render below invalidates `node` — keep only its id across the boundary.
        ControlId headerId = node->Id;
        SetExpanded(node, true);
        if (!Rerender())
            return result;
        GraphNode* header = _current->NodeAt(headerId);
        if (!header)
            return result;
        if (!FirstChildOf(header))
        {
            // A lazy drill-in that resolved to nothing: don't leave a silent empty-expanded node.
            SetExpanded(header, false);
            Rerender();
            result.Kind = TreeMove::EmptyGroup;
            return result;
        }
        result.Kind = TreeMove::Expanded;
        return result;
    }

    if (node->Expandable && node->Expanded)
    {
        GraphNode* child = FirstChildOf(node);
        if (!child)
        {
            result.Kind = TreeMove::Leaf;
            return result;
        }
        result.Move.From = node;
        SetCurrent(child);
        result.Move.To = child;
        result.Move.Moved = true;
        result.Kind = TreeMove::Descended;
        return result;
    }

    result.Kind = InTree(node) ? TreeMove::Leaf : TreeMove::None;
    return result;
}

KeyGraph::TreeResult KeyGraph::TreeLeft()
{
    TreeResult result{};
    if (!Rerender())
        return result;
    GraphNode* node = CurrentNode();
    if (!node)
        return result;

    if (node->Expandable && node->Expanded)
    {
        SetExpanded(node, false);
        Rerender(); // focus stays on the header by identity
        result.Kind = TreeMove::Collapsed;
        return result;
    }

    for (GraphNode* p = node->Parent; p; p = p->Parent)
    {
        if (!p->Focusable || !_current->Nodes.count(p->Id))
            continue;
        result.Move.From = node;
        GraphNode* target = _current->NodeAt(p->Id);
        SetCurrent(target);
        result.Move.To = target;
        result.Move.Moved = true;
        result.Kind = TreeMove::Ascended;
        return result;
    }

    result.Kind = InTree(node) ? TreeMove::Leaf : TreeMove::None;
    return result;
}

MoveResult KeyGraph::EnterFlyout()
{
    MoveResult result{};
    if (!Rerender())
        return result;
    GraphNode* node = CurrentNode();
    result.From = node;
    result.To = node;
    if (!node || !node->FlyoutOwner)
        return result;

    // The column's items carry the owner as their Parent, so the first child in declaration order
    // IS the column head — the same lookup a tree descend uses, on a structure that is not a tree.
    GraphNode* head = FirstChildOf(node);
    if (!head)
        return result; // FlyoutOwner is only stamped on non-empty columns; belt and braces
    SetCurrent(head);
    result.To = head;
    result.Moved = true;
    return result;
}

MoveResult KeyGraph::LeaveFlyout()
{
    MoveResult result{};
    if (!Rerender())
        return result;
    GraphNode* node = CurrentNode();
    result.From = node;
    result.To = node;
    if (!InFlyout(node))
        return result;

    GraphNode* owner = node->Parent;
    if (!owner)
        return result;
    SetCurrent(owner);
    result.To = owner;
    result.Moved = true;
    return result;
}

MoveResult KeyGraph::MoveToSiblingEdge(bool first)
{
    MoveResult result{};
    if (!Rerender())
        return result;
    GraphNode* node = CurrentNode();
    result.From = node;
    result.To = node;
    if (!node)
        return result;

    GraphNode* target = nullptr;
    for (GraphNode* n : _current->Order)
    {
        // Siblings share (parent, stop) — parent alone is not enough: root-level nodes all carry
        // the null parent, so Home/End on a top-level node would otherwise scan across every
        // Tab-stop, and arrows never cross a stop (spec §4.3, §5.3).
        if (n->Parent != node->Parent || n->StopKey != node->StopKey)
            continue;
        if (first)
        {
            target = n;
            break;
        }
        target = n; // last match wins
    }
    if (!target || target == node)
        return result;
    SetCurrent(target);
    result.To = target;
    result.Moved = true;
    return result;
}

// Change a group's expansion: through its vtable override when declared (a host driving retained
// state of its own), else the persistent set.
void KeyGraph::SetExpanded(GraphNode* group, bool expanded)
{
    if (expanded && group->Vtable.OnExpand)
    {
        group->Vtable.OnExpand();
        return;
    }
    if (!expanded && group->Vtable.OnCollapse)
    {
        group->Vtable.OnCollapse();
        return;
    }
    if (expanded)
        _state->Expanded.insert(group->Id);
    else
        _state->Expanded.erase(group->Id);
}

GraphNode* KeyGraph::FirstChildOf(const GraphNode* group)
{
    for (GraphNode* n : _current->Order)
        if (n->Parent == group)
            return n;
    return nullptr;
}

// ---- behavior invokers ----

bool KeyGraph::Activate()
{
    if (!Rerender())
        return false;
    GraphNode* node = CurrentNode();
    if (!node || !node->Vtable.OnActivate)
        return false;
    node->Vtable.OnActivate();
    return true;
}

bool KeyGraph::Secondary()
{
    if (!Rerender())
        return false;
    GraphNode* node = CurrentNode();
    if (!node || !node->Vtable.OnSecondary)
        return false;
    node->Vtable.OnSecondary();
    return true;
}

bool KeyGraph::ActivateShift()
{
    if (!Rerender())
        return false;
    GraphNode* node = CurrentNode();
    if (!node || !node->Vtable.OnActivateShift)
        return false;
    node->Vtable.OnActivateShift();
    return true;
}

bool KeyGraph::ActivateCtrl()
{
    if (!Rerender())
        return false;
    GraphNode* node = CurrentNode();
    if (!node || !node->Vtable.OnActivateCtrl)
        return false;
    node->Vtable.OnActivateCtrl();
    return true;
}

bool KeyGraph::ActivateHold()
{
    if (!Rerender())
        return false;
    GraphNode* node = CurrentNode();
    if (!node || !node->Vtable.OnActivateHold)
        return false;
    node->Vtable.OnActivateHold();
    return true;
}

bool KeyGraph::HasHoldActivation()
{
    GraphNode* node = CurrentNode();
    return node && node->Vtable.OnActivateHold;
}

bool KeyGraph::Tooltip()
{
    if (!Rerender())
        return false;
    GraphNode* node = CurrentNode();
    if (!node || !node->Vtable.OnTooltip)
        return false;
    node->Vtable.OnTooltip();
    return true;
}

bool KeyGraph::TryAdjust(int sign, bool large)
{
    if (!Rerender())
        return false;
    GraphNode* node = CurrentNode();
    if (!node || !node->Vtable.OnAdjust)
        return false;
    node->Vtable.OnAdjust(sign, large);
    return true;
}

} // namespace X2Access::Graph
