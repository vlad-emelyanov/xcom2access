#pragma once

// Graph A11y Kernel — the announcer (spec §6). STL-only; see ControlId.hpp for the boundary rule.

#include <functional>
#include <string>
#include <vector>

#include "GraphTypes.hpp"

namespace X2Access::Graph
{

/// Composes the spoken line for a focus change by diffing the old and new focus PATHS — each
/// node's ancestor chain (GraphNode::Parent) plus the node itself, compared by identity. Newly-
/// entered levels read outermost-first, then the landing control: "Difficulty settings, list,
/// Normal, radio button, selected", recursing as deep as the hierarchy goes. Sibling moves share
/// the whole prefix and read just the control; ascends likewise; and descending from a group onto
/// its own child re-announces nothing but the child — the group is on the child's chain AND is
/// the from-node, so the prefix swallows it.
/// Join one phrase onto a spoken line with the right separator, and the reason it is not just
/// ``sb + ", "``: the GAME's own strings routinely end a sentence themselves. Joining blindly
/// produced "5 дн..", "ТРЕБУЕТСЯ ИНЖЕНЕР!." and "Перезаписать эту сохраненную игру?," — 14 of
/// them in one measured Avenger session — and a screen reader voices that stumble. A part that
/// already ends in terminal or clause punctuation gets a plain space; everything else gets the
/// comma. Shared so the announcer and the recipes that pre-join their own labels cannot drift
/// apart about it.
void AppendPhrase(std::string& sb, const std::string& part);

class GraphAnnouncer
{
public:
    /// The line for landing on `to` having come from `from` (null = from nothing: the full path
    /// reads). `transitionLabel` is the crossed edge's spoken line, when it had one. Empty when
    /// there is nothing to say.
    static std::string Compose(const GraphNode* from, const GraphNode* to,
        const std::string& transitionLabel = "");

    /// The full readout for a landing with no prior focus (screen entry, focus restore).
    static std::string ComposeFull(const GraphNode* to) { return Compose(nullptr, to); }

    /// A node's EFFECTIVE announcement parts: the control type's common parts (the role word)
    /// merged with the node's own — a node part overrides a common part of the same kind — sorted
    /// by the type's kind order (unknown/kindless parts append in declaration order), then
    /// filtered by the user's settings. This is the single list readouts and the live watch
    /// operate on.
    static std::vector<NodeAnnouncement> EffectiveAnnouncements(const GraphNode* node);

    /// A node's own readout: its effective announcement parts, resolved live, non-empty ones
    /// joined — plus, for an expandable group, its expanded/collapsed state word, plus the
    /// auto-stamped "n of m" position. The first part is the control's label, so path dedupe's
    /// prefix check applies. Empty when nothing resolved.
    static std::string LeafText(const GraphNode* node);

    /// The first announcement part's text (the label) — for dedupe and search fallbacks.
    static std::string FirstPartText(const GraphNode* node);

    /// Pluggable per-part filter — installed by the host to consult the user's announcement
    /// settings (per control type + per kind); empty (tests, boot) = everything speaks. Returning
    /// false drops the part from readouts AND from the live watch.
    static std::function<bool(const ControlType*, const NodeAnnouncement&)> PartFilter;

    /// Pluggable "n of m" wording (localized by the host); empty = no auto positions.
    static std::function<std::string(int index, int count)> PositionText;

    /// Pluggable expanded/collapsed wording for group headers (localized by the host); empty =
    /// groups don't speak their state.
    static std::function<std::string(bool expanded)> ExpandedStateText;

    /// Pluggable "submenu, N items" wording for flyout owners (localized by the host); empty =
    /// owners don't advertise their column. Unlike a group, a flyout has no open/closed state to
    /// report — only that there is something below.
    static std::function<std::string(int count)> FlyoutHintText;
};

} // namespace X2Access::Graph
