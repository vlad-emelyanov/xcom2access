#include "GraphAnnouncer.hpp"

#include <algorithm>

namespace X2Access::Graph
{

std::function<bool(const ControlType*, const NodeAnnouncement&)> GraphAnnouncer::PartFilter;
std::function<std::string(int, int)> GraphAnnouncer::PositionText;
std::function<std::string(bool)> GraphAnnouncer::ExpandedStateText;
std::function<std::string(int)> GraphAnnouncer::FlyoutHintText;

namespace
{

// The node's path: ancestors outermost-first, then the node itself.
std::vector<const GraphNode*> PathOf(const GraphNode* node)
{
    std::vector<const GraphNode*> path;
    for (const GraphNode* n = node; n; n = n->Parent)
        path.push_back(n);
    std::reverse(path.begin(), path.end());
    return path;
}

bool HasKind(const std::vector<NodeAnnouncement>& anns, const std::string& kind)
{
    if (kind.empty())
        return false;
    for (const NodeAnnouncement& a : anns)
        if (a.Kind == kind)
            return true;
    return false;
}

// Sort key: declared kinds by their order index; everything else after (one shared bucket, with
// stable sort keeping their relative declaration order).
std::size_t OrderIndex(const std::vector<std::string>& order, const std::string& kind)
{
    if (!kind.empty())
        for (std::size_t i = 0; i < order.size(); i++)
            if (order[i] == kind)
                return i;
    return order.size();
}

// A stand-in part handed to the PartFilter so the user's position-kind toggle governs the
// auto-stamped position too.
const NodeAnnouncement& AutoPositionProbe()
{
    static const NodeAnnouncement probe{[]() { return std::string(); }, false, AnnouncementKinds::Position};
    return probe;
}

// The next part "starts as" this label: equal, or its first comma-separated segment is the label
// (a control's readout leads with its label: "Game difficulty, menu button").
bool DuplicatesNext(const std::string& label, const std::string& next)
{
    if (next.rfind(label, 0) != 0)
        return false;
    return next.size() == label.size() || next[label.size()] == ',';
}

void Append(std::string& sb, const std::string& text)
{
    AppendPhrase(sb, text);
}

} // namespace

bool EndsPhrase(std::string_view text)
{
    return !text.empty() && kPhraseTerminators.find(text.back()) != std::string_view::npos;
}

void AppendPhrase(std::string& sb, const std::string& part, std::string_view separator)
{
    if (part.empty())
        return;
    if (!sb.empty())
    {
        // A phrase that already punctuated itself does not take a separator on top — just the
        // space that keeps the two words apart. The set lives in kPhraseTerminators (see the
        // header): spelling it out a second time here is how the forks started.
        sb += EndsPhrase(sb) ? std::string_view(" ") : separator;
    }
    sb += part;
}

std::string GraphAnnouncer::Compose(const GraphNode* from, const GraphNode* to,
    const std::string& transitionLabel)
{
    if (!to)
        return {};

    std::vector<const GraphNode*> toPath = PathOf(to);
    std::vector<const GraphNode*> fromPath = from ? PathOf(from) : std::vector<const GraphNode*>{};

    // Common prefix by identity — levels we were already inside (or ON: descending from a group
    // onto its child keeps the group in the prefix) stay silent.
    std::size_t i = 0;
    while (i < fromPath.size() && i < toPath.size() && fromPath[i]->Id == toPath[i]->Id)
        i++;

    std::vector<std::string> parts;
    if (!transitionLabel.empty())
        parts.push_back(transitionLabel);

    if (i >= toPath.size())
    {
        // Ascended (or same node): announce just the now-innermost focus.
        std::string text = LeafText(to);
        if (!text.empty())
            parts.push_back(std::move(text));
    }
    else
    {
        for (std::size_t j = i; j < toPath.size(); j++)
        {
            std::string text = LeafText(toPath[j]);
            if (text.empty())
                continue;
            // Dedupe: a level whose label just duplicates the next level down (or the control
            // itself — "a 'Game difficulty' section wrapping the 'Game difficulty' control").
            if (j + 1 < toPath.size())
            {
                std::string label = FirstPartText(toPath[j]);
                std::string next = FirstPartText(toPath[j + 1]);
                if (!label.empty() && !next.empty() && DuplicatesNext(label, next))
                    continue;
            }
            parts.push_back(std::move(text));
        }
    }

    if (parts.empty())
        return {};
    std::string sb;
    for (const std::string& part : parts)
        AppendPhrase(sb, part);
    return sb;
}

std::vector<NodeAnnouncement> GraphAnnouncer::EffectiveAnnouncements(const GraphNode* node)
{
    std::vector<NodeAnnouncement> result;
    if (!node)
        return result;
    const NodeVtable& vt = node->Vtable;
    const ControlType* type = vt.Type;

    if (type && type->Common)
        for (const NodeAnnouncement& c : type->Common())
            if (!HasKind(vt.Announcements, c.Kind))
                result.push_back(c);
    for (const NodeAnnouncement& a : vt.Announcements)
        result.push_back(a);

    if (type && !type->Order.empty() && result.size() > 1)
    {
        std::stable_sort(result.begin(), result.end(),
            [type](const NodeAnnouncement& x, const NodeAnnouncement& y)
            { return OrderIndex(type->Order, x.Kind) < OrderIndex(type->Order, y.Kind); });
    }

    if (PartFilter)
        std::erase_if(result, [type](const NodeAnnouncement& a) { return !PartFilter(type, a); });
    return result;
}

std::string GraphAnnouncer::LeafText(const GraphNode* node)
{
    std::vector<NodeAnnouncement> anns = EffectiveAnnouncements(node);
    std::string sb;
    for (const NodeAnnouncement& a : anns)
    {
        std::string t;
        if (a.Text)
            t = a.Text();
        Append(sb, t);
    }
    if (node && node->Expandable && !node->Vtable.SpeaksOwnExpansion && ExpandedStateText)
        Append(sb, ExpandedStateText(node->Expanded));
    if (node && node->FlyoutOwner && FlyoutHintText)
        Append(sb, FlyoutHintText(node->FlyoutCount));

    // The auto-stamped sibling position, unless the node carries its own (an explicit
    // position-kind part, or a composed message). Honors the user's per-kind setting.
    if (node && node->PositionCount > 1 && PositionText && !node->Vtable.SpeaksOwnPosition
        && !HasKind(node->Vtable.Announcements, AnnouncementKinds::Position)
        && (!PartFilter || PartFilter(node->Vtable.Type, AutoPositionProbe())))
    {
        Append(sb, PositionText(node->PositionIndex, node->PositionCount));
    }
    return sb;
}

std::string GraphAnnouncer::FirstPartText(const GraphNode* node)
{
    if (!node || node->Vtable.Announcements.empty())
        return {};
    const NodeAnnouncement& first = node->Vtable.Announcements.front();
    return first.Text ? first.Text() : std::string();
}

} // namespace X2Access::Graph
