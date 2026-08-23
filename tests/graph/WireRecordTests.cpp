// Conformance tests for the companion wire grammar (src/wire_record.hpp).
//
// The first two cases are the ⛔ hard rule this module was given its own translation unit to make
// testable: an empty VALUE is KEPT, so a repeating field stays positional. CLAUDE.md records four
// of squad select's five first-run defects as that one bug in different clothes — a dropped empty
// shifting every later value — and until the parse moved out of menu_navigator.cpp (which includes
// windows.h and so cannot be compiled here) nothing pinned it.

#include <string>
#include <vector>

#include "TestHarness.hpp"
#include "wire_record.hpp"

using namespace X2Access::Wire;

namespace
{

// A FieldCleaner is a plain function pointer, so this stands in for GameUI::CleanText.
std::string Shout(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
    {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

} // namespace

// ⛔ THE rule. Three utility slots, the middle one empty: the empty one must survive as a value in
// its own right, because the recipe reads these BY POSITION against the slots on screen.
TEST(WireRecord_EmptyValueIsKeptAndStaysPositional)
{
    const std::vector<WireRecord> recs =
        ParseRecords("kind=card~:~util=a~:~util=~:~util=c");

    CHECK_EQ(std::size_t{1}, recs.size());

    const std::vector<std::string> expected{"a", "", "c"};
    CHECK_EQ(expected, RecFieldAll(recs[0], "util"));
}

// A field with no '=' or with an empty KEY is malformed and dropped — and dropping it must not
// shift anything, which is the other half of the rule above.
TEST(WireRecord_MalformedFieldIsSkippedAndShiftsNothing)
{
    const std::vector<WireRecord> recs =
        ParseRecords("a=1~:~=orphan~:~noequals~:~b=2");

    CHECK_EQ(std::size_t{1}, recs.size());
    CHECK_EQ(std::size_t{2}, recs[0].size());
    CHECK_EQ(std::string("1"), RecField(recs[0], "a"));
    CHECK_EQ(std::string("2"), RecField(recs[0], "b"));
}

// A record with no valid field at all is dropped whole, so an empty payload and a payload of bare
// separators both answer "the game is not showing this" the same way.
TEST(WireRecord_RecordWithNoValidFieldsIsDropped)
{
    CHECK_EQ(std::size_t{0}, ParseRecords("").size());
    CHECK_EQ(std::size_t{0}, ParseRecords("~|~~|~").size());

    // ...but a malformed record between two good ones costs only itself.
    const std::vector<WireRecord> recs = ParseRecords("a=1~|~junk~|~b=2");
    CHECK_EQ(std::size_t{2}, recs.size());
    CHECK_EQ(std::string("1"), RecField(recs[0], "a"));
    CHECK_EQ(std::string("2"), RecField(recs[1], "b"));
}

// SplitOn never collapses adjacent separators and always yields at least one part. That is what
// makes an empty run between two separators a part in its own right.
TEST(WireRecord_SplitOnPreservesEmptyRuns)
{
    const std::vector<std::string> one = SplitOn("", "~:~");
    CHECK_EQ(std::size_t{1}, one.size());
    CHECK_EQ(std::string(""), one[0]);

    const std::vector<std::string> expected{"a", "", "b"};
    CHECK_EQ(expected, SplitOn("a~:~~:~b", "~:~"));

    // Leading and trailing separators each produce an empty part too.
    const std::vector<std::string> edges{"", "a", ""};
    CHECK_EQ(edges, SplitOn("~:~a~:~", "~:~"));
}

// RecField answers the FIRST value; RecFieldAll answers all of them in emission order. A key the
// record does not carry reads as empty — indistinguishable from an empty value, which is exactly
// why positional data must never be read through RecField.
TEST(WireRecord_RecFieldTakesTheFirstAndRecFieldAllTakesAllInOrder)
{
    const std::vector<WireRecord> recs = ParseRecords("k=1~:~k=2~:~k=3");
    CHECK_EQ(std::size_t{1}, recs.size());

    CHECK_EQ(std::string("1"), RecField(recs[0], "k"));

    const std::vector<std::string> expected{"1", "2", "3"};
    CHECK_EQ(expected, RecFieldAll(recs[0], "k"));

    CHECK_EQ(std::string(""), RecField(recs[0], "absent"));
    CHECK_EQ(std::size_t{0}, RecFieldAll(recs[0], "absent").size());
}

// ⚠ The format has no escaping, and this pins the consequence rather than leaving it to be
// rediscovered: a value that itself contains a separator splits the record, and the tail becomes a
// field with no '=' — which the malformed rule then silently drops. No producer emits one today
// and nothing here can detect it if one starts. A future escaping change should have to EDIT this
// test, deliberately, rather than surprise someone.
TEST(WireRecord_ValueContainingASeparatorSplitsTheRecord)
{
    const std::vector<WireRecord> recs = ParseRecords("a=x~:~y");

    CHECK_EQ(std::size_t{1}, recs.size());
    CHECK_EQ(std::size_t{1}, recs[0].size());
    CHECK_EQ(std::string("x"), RecField(recs[0], "a"));
}

// The cleaner is applied to VALUES only. Running it over keys would break every lookup the moment
// CleanText touched a character a key uses.
TEST(WireRecord_CleanerAppliesToValuesNeverToKeys)
{
    const std::vector<WireRecord> recs = ParseRecords("name=aim~:~rank=rookie", &Shout);

    CHECK_EQ(std::size_t{1}, recs.size());
    CHECK_EQ(std::string("name"), recs[0][0].first);
    CHECK_EQ(std::string("AIM"), recs[0][0].second);
    CHECK_EQ(std::string("rank"), recs[0][1].first);
    CHECK_EQ(std::string("ROOKIE"), recs[0][1].second);

    // No cleaner is the tests' own default and must leave the value byte-for-byte.
    CHECK_EQ(std::string("aim"), RecField(ParseRecords("name=aim")[0], "name"));
}
