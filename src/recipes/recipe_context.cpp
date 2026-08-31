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

} // namespace Recipes
} // namespace MenuNavigator
