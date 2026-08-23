// Conformance tests: announcer path-diffing, dedupe, control-type ordering, leaf readouts.
// Ported from RTAccess tests/GraphAnnouncerTests.cs.

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include "Graph/GraphAnnouncer.hpp"
#include "TestHarness.hpp"

using namespace X2Access::Graph;

namespace
{

GraphNode MakeNode(const std::string& label, GraphNode* parent = nullptr)
{
    GraphNode n;
    n.Id = ControlId::Structural(label);
    n.Vtable.Announcements = {NodeAnnouncement::Static(label)};
    n.Parent = parent;
    return n;
}

GraphNode MakeContext(const std::string& label, const std::string& role = "", GraphNode* parent = nullptr)
{
    GraphNode n;
    n.Id = ControlId::Structural("ctx:" + label);
    n.Vtable.Announcements = {NodeAnnouncement::Static(label)};
    if (!role.empty())
        n.Vtable.Announcements.push_back(NodeAnnouncement::Static(role));
    n.Parent = parent;
    n.Focusable = false;
    return n;
}

// Resets a pluggable announcer hook when a test scope exits (the C# tests' try/finally).
struct Cleanup
{
    std::function<void()> Fn;
    ~Cleanup() { Fn(); }
};

const ControlType TestButton{
    "button",
    {AnnouncementKinds::Label, AnnouncementKinds::Role, AnnouncementKinds::Value,
        AnnouncementKinds::Enabled, AnnouncementKinds::Position},
    []()
    {
        return std::vector<NodeAnnouncement>{
            NodeAnnouncement([]() { return std::string("button"); }, false, AnnouncementKinds::Role)};
    },
};

GraphNode TypedNode(const ControlType* type, std::vector<NodeAnnouncement> parts)
{
    GraphNode n;
    n.Id = ControlId::Structural("typed");
    n.Vtable.Type = type;
    n.Vtable.Announcements = std::move(parts);
    return n;
}

} // namespace

TEST(EntryFromNothingReadsFullChain)
{
    GraphNode options = MakeContext("Options");
    GraphNode list = MakeContext("Difficulty settings", "list", &options);
    GraphNode node = MakeNode("Normal, radio button, selected", &list);

    CHECK_EQ("Options, Difficulty settings, list, Normal, radio button, selected",
        GraphAnnouncer::ComposeFull(&node));
}

TEST(SiblingMoveReadsLeafOnly)
{
    GraphNode list = MakeContext("Difficulty settings", "list");
    GraphNode from = MakeNode("Easy", &list);
    GraphNode to = MakeNode("Hard", &list);

    CHECK_EQ("Hard", GraphAnnouncer::Compose(&from, &to));
}

TEST(EnteringNestedContextReadsEnteredLevels)
{
    GraphNode outer = MakeContext("Options");
    GraphNode from = MakeNode("Back", &outer);
    GraphNode list = MakeContext("Difficulty settings", "list", &outer);
    GraphNode to = MakeNode("Normal", &list);

    CHECK_EQ("Difficulty settings, list, Normal", GraphAnnouncer::Compose(&from, &to));
}

TEST(AscendReadsLeafOnly)
{
    GraphNode outer = MakeContext("Options");
    GraphNode list = MakeContext("Difficulty settings", "list", &outer);
    GraphNode from = MakeNode("Normal", &list);
    GraphNode to = MakeNode("Back", &outer);

    CHECK_EQ("Back", GraphAnnouncer::Compose(&from, &to));
}

TEST(DescendingFromGroupOntoItsChildReadsChildOnly)
{
    // The group is ON the child's chain AND is the from-node: the prefix swallows it (stepping
    // into your own group never re-announces the group).
    GraphNode group = MakeNode("Combat");
    group.Expandable = true;
    group.Expanded = true;
    GraphNode child = MakeNode("Auto pause on combat start, toggle, on", &group);

    CHECK_EQ("Auto pause on combat start, toggle, on", GraphAnnouncer::Compose(&group, &child));
}

TEST(EnteringAGroupFromOutsideReadsTheGroup)
{
    GraphNode group = MakeNode("Combat");
    group.Expandable = true;
    group.Expanded = true;
    GraphNode child = MakeNode("Auto pause on combat start, toggle, on", &group);
    GraphNode elsewhere = MakeNode("Tabs");

    CHECK_EQ("Combat, Auto pause on combat start, toggle, on",
        GraphAnnouncer::Compose(&elsewhere, &child));
}

TEST(ExpandedStateWordAppendsToGroups)
{
    GraphNode group = MakeNode("Combat");
    group.Expandable = true;
    group.Expanded = false;

    Cleanup cleanup{[]() { GraphAnnouncer::ExpandedStateText = nullptr; }};
    GraphAnnouncer::ExpandedStateText = [](bool e) { return std::string(e ? "expanded" : "collapsed"); };

    CHECK_EQ("Combat, collapsed", GraphAnnouncer::ComposeFull(&group));
    group.Expanded = true;
    CHECK_EQ("Combat, expanded", GraphAnnouncer::ComposeFull(&group));
    group.Vtable.SpeaksOwnExpansion = true; // nodes may carry their own state word
    CHECK_EQ("Combat", GraphAnnouncer::ComposeFull(&group));
}

TEST(DuplicateContainerLabelIsSkipped)
{
    // A "Game difficulty" section wrapping the "Game difficulty" control: the section stays
    // silent.
    GraphNode section = MakeContext("Game difficulty");
    GraphNode to = MakeNode("Game difficulty, menu button", &section);
    CHECK_EQ("Game difficulty, menu button", GraphAnnouncer::ComposeFull(&to));

    // But a control that merely STARTS with different text keeps its container.
    GraphNode section2 = MakeContext("Game difficulty");
    GraphNode other = MakeNode("Game difficulty presets, menu button", &section2);
    CHECK_EQ("Game difficulty, Game difficulty presets, menu button",
        GraphAnnouncer::ComposeFull(&other));
}

TEST(ControlTypeSuppliesRoleAndOrdering)
{
    // Parts declared out of order — the type's kind order sorts them; the common role merges in.
    GraphNode node = TypedNode(&TestButton,
        {
            NodeAnnouncement([]() { return std::string("on"); }, false, AnnouncementKinds::Value),
            NodeAnnouncement([]() { return std::string("Hold position"); }, false, AnnouncementKinds::Label),
        });

    CHECK_EQ("Hold position, button, on", GraphAnnouncer::ComposeFull(&node));
}

TEST(NodePartOverridesCommonOfSameKind)
{
    GraphNode node = TypedNode(&TestButton,
        {
            NodeAnnouncement([]() { return std::string("Continue"); }, false, AnnouncementKinds::Label),
            NodeAnnouncement([]() { return std::string("menu button"); }, false, AnnouncementKinds::Role),
        });

    CHECK_EQ("Continue, menu button", GraphAnnouncer::ComposeFull(&node));
}

TEST(KindlessPartsKeepDeclarationOrderAfterKnownKinds)
{
    GraphNode node = TypedNode(&TestButton,
        {
            NodeAnnouncement([]() { return std::string("custom one"); }),
            NodeAnnouncement([]() { return std::string("Continue"); }, false, AnnouncementKinds::Label),
            NodeAnnouncement([]() { return std::string("custom two"); }),
        });

    CHECK_EQ("Continue, button, custom one, custom two", GraphAnnouncer::ComposeFull(&node));
}

TEST(PartFilterDropsParts)
{
    GraphNode node = TypedNode(&TestButton,
        {
            NodeAnnouncement([]() { return std::string("Continue"); }, false, AnnouncementKinds::Label),
        });

    Cleanup cleanup{[]() { GraphAnnouncer::PartFilter = nullptr; }};
    GraphAnnouncer::PartFilter = [](const ControlType*, const NodeAnnouncement& part)
    { return part.Kind != AnnouncementKinds::Role; };

    CHECK_EQ("Continue", GraphAnnouncer::ComposeFull(&node));
}

TEST(LeafTextJoinsAnnouncementParts)
{
    GraphNode node;
    node.Id = ControlId::Structural("x");
    node.Vtable.Announcements = {
        NodeAnnouncement::Static("Hold position"),
        NodeAnnouncement::Static("toggle"),
        NodeAnnouncement([]() { return std::string("on"); }, /*live*/ true),
        NodeAnnouncement([]() { return std::string(); }), // empty at speak time — silent
    };
    CHECK_EQ("Hold position, toggle, on", GraphAnnouncer::ComposeFull(&node));
}

TEST(TransitionLabelLeads)
{
    GraphNode from = MakeNode("A");
    GraphNode to = MakeNode("B");
    CHECK_EQ("next column, B", GraphAnnouncer::Compose(&from, &to, "next column"));
}

TEST(ContextChangeAtSameDepthReadsNewLevel)
{
    GraphNode ctx1 = MakeContext("Level 1 spells", "table");
    GraphNode from = MakeNode("Fireball", &ctx1);
    GraphNode ctx2 = MakeContext("Level 2 spells", "table");
    GraphNode to = MakeNode("Haste", &ctx2);

    CHECK_EQ("Level 2 spells, table, Haste", GraphAnnouncer::Compose(&from, &to));
}

// A phrase that punctuates itself must not also take a comma. Every one of these strings is a
// real line measured on 2026-08-21 in a Russian install: the game ends plenty of its own strings
// with '.', '!' or '?', and blind joining voiced "5 дн..", "ТРЕБУЕТСЯ ИНЖЕНЕР!." and
// "Перезаписать эту сохраненную игру?," at the player — 14 in a single Avenger session.
TEST(SelfPunctuatedPhraseTakesNoComma)
{
    std::string sb;
    AppendPhrase(sb, "5 \xD0\xB4\xD0\xBD.");
    AppendPhrase(sb, "\xD0\x9D\xD0\x90\xD0\x93\xD0\xA0\xD0\x90\xD0\x94\xD0\x90:");
    AppendPhrase(sb, "x39");
    CHECK_EQ("5 \xD0\xB4\xD0\xBD. \xD0\x9D\xD0\x90\xD0\x93\xD0\xA0\xD0\x90\xD0\x94\xD0\x90: x39",
        sb);
}

TEST(OrdinaryPhrasesStillTakeCommas)
{
    std::string sb;
    AppendPhrase(sb, "Fireball");
    AppendPhrase(sb, "button");
    AppendPhrase(sb, "selected");
    CHECK_EQ("Fireball, button, selected", sb);
}

TEST(AppendPhraseSkipsEmptyAndDoesNotLeadWithSeparator)
{
    std::string sb;
    AppendPhrase(sb, "");
    AppendPhrase(sb, "only");
    AppendPhrase(sb, "");
    CHECK_EQ("only", sb);
}

// The exclamation and question forms, which is where a screen reader's stumble is most audible.
TEST(TerminalPunctuationVariants)
{
    std::string sb;
    AppendPhrase(sb, "\xD0\x98\xD0\x9D\xD0\x96\xD0\x95\xD0\x9D\xD0\x95\xD0\xA0!");
    AppendPhrase(sb, "ok?");
    AppendPhrase(sb, "end");
    CHECK_EQ("\xD0\x98\xD0\x9D\xD0\x96\xD0\x95\xD0\x9D\xD0\x95\xD0\xA0! ok? end", sb);
}

// ---- the terminator set itself ----
//
// The rule was re-derived by hand across both layers with THREE different terminator sets, so the
// set is now a single named constant and these tests are what makes it a contract rather than a
// comment. Five characters in; everything else out, the trailing comma included — a part ending
// in ',' is a part mid-list, and eating our separator there would run two facts together.

TEST(EndsPhraseAcceptsExactlyTheFiveTerminators)
{
    CHECK_EQ(std::string(".!?:;"), std::string(kPhraseTerminators));
    for (char c : std::string(".!?:;"))
        CHECK(EndsPhrase(std::string("x") + c));
}

TEST(EndsPhraseRejectsEverythingElse)
{
    // ',' is the load-bearing rejection; the rest are the ordinary shapes a readout ends in.
    for (const char* text : {"x,", "x", "60", "x)", "x-", "x ", "x\"", "x\xE2\x80\xA6"})
        CHECK_FALSE(EndsPhrase(text));
    CHECK_FALSE(EndsPhrase(""));
}

// The separator is the ONE knob, and a self-punctuated tail takes a plain space in every mode —
// which is precisely why a ". " caller cannot just append a period of its own. This is the shape
// the Skyranger briefing and the tutorial modal join in: whole sentences the GAME wrote, most of
// which already end in a full stop and some of which (a title) never do.
TEST(SentenceSeparatorSuppliesTheMissingFullStop)
{
    std::string sb;
    AppendPhrase(sb, "Operation Gatecrasher", ". ");
    AppendPhrase(sb, "Secure the transmitter.", ". ");
    AppendPhrase(sb, "Press Enter to close.", ". ");
    CHECK_EQ("Operation Gatecrasher. Secure the transmitter. Press Enter to close.", sb);
}

TEST(SeparatorDefaultsToCommaAndNeverLeadsTheLine)
{
    std::string sb;
    AppendPhrase(sb, "only", ". ");
    CHECK_EQ("only", sb);
    AppendPhrase(sb, "", ". ");
    CHECK_EQ("only", sb);
}

// ⚠ The test is on the ACCUMULATOR's tail, never the incoming part. A part that punctuates
// itself still takes the ordinary separator, because the question the joiner answers is whether
// the text ALREADY on the line wants a comma after it.
TEST(SelfPunctuationOfTheIncomingPartIsNotConsulted)
{
    std::string sb;
    AppendPhrase(sb, "Fireball");
    AppendPhrase(sb, "Ready.");
    AppendPhrase(sb, "next");
    CHECK_EQ("Fireball, Ready. next", sb);
}
