// The half of the recipe context that has a definition of its own: the companion-wire cache
// every wire-backed recipe reads its words through. Everything else recipe_context.h declares
// is defined in menu_navigator.cpp, next to the attach state it belongs to.
//
// ⚠ This TU deliberately touches no engine layout. UObject arrives as an incomplete type from
// ue3_reflection.h and is only ever cast TO, so there is no GameDefines.hpp here and therefore
// no `#pragma pack(4)` region to get wrong — the ODR hazard CLAUDE.md records under "Anything
// in GameDefines.hpp must be included inside #pragma pack(4)" cannot arise in this file.
#include "recipes/recipe_context.h"

#include "log.h"
#include "script_call.h"
#include "ue3_reflection.h"
#include "vocab.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace MenuNavigator {
namespace Recipes {

// ONE outbound call per render, which is one per keypress — the same budget the tile cursor
// spends. Used wherever the widget tree cannot answer: squad select retains no words at all, and
// the loadout retains its rows but not the four things it computes and throws away.
std::vector<WireRecord> FetchCompanionRecords(const char* className, const char* funcName)
{
    std::string raw;
    if (!GameUI::CallCompanion(className, funcName, nullptr, &raw))
        return {}; // the shared helper logs both a missing companion and a failed call
    // CleanText is handed in rather than called inside the parser: one implementation of "strip
    // the markup the game colours its text with", shared with every other screen, and keeping it
    // out is what lets the parser stay engine-free.
    return ParseRecords(raw, &GameUI::CleanText);
}

void InvalidateWireCaches()
{
    WireCache::InvalidateAll();
}

std::string NameFromHeader(const std::vector<WireRecord>& records, const std::string& fallback)
{
    std::string name;
    if (const WireRecord* header = FindRecord(records, "header")) {
        name = RecField(*header, "title");
        AppendPhrase(name, RecField(*header, "subtitle"));
    }
    if (name.empty())
        name = fallback;
    return name;
}

bool CallCompanionVerb(WireCache& cache, const char* className, const char* funcName, int value)
{
    struct {
        int32_t Value;
    } params{value};
    if (!GameUI::CallCompanion(className, funcName, &params, nullptr))
        return false;
    // The verb respawned the rows; a fresh panel landing on a freed address must not let a
    // matching pointer key keep the previous words on screen.
    cache.Invalidate();
    return true;
}

// Join the parts a card names, dropping the ones the game is not showing. The companion sends
// identity as four SEPARATE fields for exactly this: joining them there meant a value that
// CleanText later reduced to nothing still left its comma behind, and every rookie read
// "Новобранец, Sokolova,," live 2026-08-21. The join itself is AppendPhrase's, not ours — these
// are the game's own strings and it ends plenty of them in '.' or ':' (⛔ never a bare ", ").
std::string JoinParts(const WireRecord& rec, std::initializer_list<const char*> keys)
{
    std::string line;
    for (const char* key : keys)
        AppendPhrase(line, RecField(rec, key));
    return line;
}

// The same join for two parts held in hand rather than looked up — the label-then-value shape
// these recipes compose all over ("ОСНОВНОЕ ОРУЖИЕ" + the weapon, a mission caption + its text).
// Either half may be absent, and neither leaves a separator behind when it is.
std::string JoinPhrases(const std::string& first, const std::string& second)
{
    std::string line = first;
    AppendPhrase(line, second);
    return line;
}

const WireRecord* FindRecord(const std::vector<WireRecord>& records, const char* kind)
{
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == kind)
            return &rec;
    }
    return nullptr;
}

// ---- the resource strip ----
//
// See the header for why this is trailing, why it is conditional, and why it is shared. The read
// itself is GameUI::ReadResourceStrip's; this is only the shape it takes as nodes.
//
// One node per resource rather than one node for the strip: nine resources in a single sentence
// is the all-items dump the project's speech rule forbids, and arrowing across them is how a
// player checks the one figure they came for. The hotkey is the other half of that split — it
// says the whole strip at once, which is what a key pressed FROM ANOTHER STOP has to do.
bool AddResourceStop(GraphBuilder& b)
{
    const GameUI::ResourceStripView strip = GameUI::ReadResourceStrip();
    if (!strip.showing || strip.entries.empty())
        return false;

    b.BeginStop("resources");
    b.SetRegion("resources");
    // A CONTEXT rather than a leading label node: the strip needs a name — Tabbing into it and
    // hearing "INCOME" with no warning that the stop changed is how a player loses their place —
    // but naming it with a node would put a step in front of every resource on every visit. A
    // context is announced on entry from outside and is never navigable, which is exactly the
    // difference. The word is ours; the game labels the band only by drawing it in one place.
    b.PushContext(Vocab::Resources);

    for (size_t i = 0; i < strip.entries.size(); i++) {
        const GameUI::ResourceEntry& entry = strip.entries[i];

        NodeVtable vt;
        vt.Type = &TextType();

        // ⛔ The name and the value stay SEPARATE parts rather than a string we join here. Both
        // are the game's own captured words and the game ends plenty of its strings in its own
        // punctuation — so the join belongs to GraphAnnouncer, whose join sites route through
        // AppendPhrase. Composing them by hand would be the bare-", " bug in a new place.
        vt.Announcements.emplace_back(NodeAnnouncement::Static(entry.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement::Static(entry.value));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;

        // The colour, which is the only reason the strip is worth more than a number. Same two
        // kinds the commodity list already uses for the same two tints, so the ordering and the
        // per-kind user settings stay consistent across surfaces.
        if (entry.state == GameUI::ResourceState::Critical) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::ResourceCritical));
            vt.Announcements.back().Kind = AnnouncementKinds::Reason;
        } else if (entry.state == GameUI::ResourceState::Warning) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::ResourceLow));
            vt.Announcements.back().Kind = kKindAttention;
        }

        // ⛔ NO OnActivate and no focus write-back. `UIX2ResourceHeader` sets
        // `bIsNavigable = false` and spawns no child widgets — there is nothing to press and
        // nowhere for the game's own focus to go, so offering either would be inventing a control
        // a sighted player does not have.
        b.AddItem(ControlId::Structural("resources/" + std::to_string(i)), std::move(vt));
    }
    // ⛔ Pop what we pushed. Build() does not check the parent stack, so an unpopped context is
    // silent here and lands on whoever adds a call AFTER this one — which is a live risk, since
    // this is the last line of six builders and will be the last line of more.
    b.PopContext();
    return true;
}

} // namespace Recipes
} // namespace MenuNavigator
