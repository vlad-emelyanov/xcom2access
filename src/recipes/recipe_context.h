#pragma once

// What a screen recipe is, and everything it is allowed to reach back into the navigator for.
//
// The eighteen recipes live in src/recipes/recipe_{shell,strategy,avenger}.cpp — one TU per
// surface family, the same axis docs/surfaces/*.md uses. They shared one 4168-line file with
// the attach state machine, and the coupling was invisible because everything was in one
// anonymous namespace: a recipe could reach any global in the file and nothing said which of
// them were contract and which were the state machine's private business. This header is that
// statement. It declares the contract — the Recipe struct, the three pieces of attach state a
// builder reads, the control-type registry, the two navigator entry points a node's action
// needs, and the companion-wire cache — and nothing else. Everything it declares is defined
// either in menu_navigator.cpp (the attach state and the registry) or in recipe_context.cpp
// (the wire cache); a recipe TU defines only its own recipes.
//
// ⛔ PROBE ORDER IS NOT DECLARED HERE. It is the kRecipes array literal in menu_navigator.cpp.
// See the comment there before adding a recipe.

#include "game_ui.h"
#include "wire_record.hpp"

#include "Graph/GraphAnnouncer.hpp"
#include "Graph/GraphBuilder.hpp"
#include "Graph/GraphSheet.hpp"
#include "Graph/KeyGraph.hpp"

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MenuNavigator {
namespace Recipes {

// The two directives the recipe bodies were written under, kept so the move could copy them
// unchanged. They are scoped to this namespace rather than dropped into a TU at large, and the
// only files that include this header are the navigator and its recipe TUs.
using namespace X2Access::Graph;
using namespace X2Access::Wire;

// Host-local announcement kind for the game's "!" badge (custom kinds are part of the kernel
// contract: they order wherever the type's Order places them and key user settings like the
// well-known ones).
constexpr const char* kKindAttention = "attention";

// ---- the recipe contract ----

// A screen recipe: how to find the screen, build its GraphRender, and name it on attach. The
// navigator hosts exactly one attached recipe at a time; probes run in kRecipes order.
struct Recipe {
    const char* logName;
    void* (*Probe)();                             // ready (focused) instance, or null
    std::unique_ptr<GraphRender> (*Build)();      // reads gScreen
    std::string (*ScreenName)();                  // spoken (interrupting) on attach
    // The screen's UIList row selector, for FocusTarget::listIndex write-back; null on screens
    // whose controls all hang off plain panels (the shell, the Ironman choice, dialogs).
    bool (*SetListIndex)(void* screen, int index) = nullptr;
};

// Where the game's focus should follow ours (P11 write-back), per node.
struct FocusTarget {
    int listIndex = -1;      // >= 0: drive UIList.SetSelectedIndex to this row
    void* button = nullptr;  // drive UIPanel.SetSelectedNavigation onto this control
    bool clearList = false;  // deselect the row list first (leaving it for buttons/tabs)
    // Screens whose focus is more than "a row in a list": squad select has to place a CARD and
    // then a control within it, through the two calls the game's own controller path makes.
    // When set, it is the whole write-back and the fields above are ignored.
    std::function<void()> custom;
};

// ---- the attach state a builder reads (defined in menu_navigator.cpp) ----

// The screen the attached recipe was probed onto — what every Build() reads.
extern void* gScreen;

// The kernel's expanded-group set, for a builder that constructs its own GraphBuilder.
extern GraphState gState;

// Focus write-back targets (P11), filled in by the builder as it goes. ⛔ Reset by Attach(),
// never by a builder: these are raw widget pointers valid only for the render that produced
// them, and Build() is reached from a dozen places that would each have had to remember.
extern std::unordered_map<std::string, FocusTarget> gFocus;

// ---- the control-type registry (defined in menu_navigator.cpp) ----
//
// Spec §3.3: one registry, key collisions are an error. Declared here so a recipe TU can name
// a type without owning one; the definitions stay together in one file so the collision rule
// is checkable by reading a single block.
const ControlType& ButtonType();
const ControlType& TickerType();
const ControlType& TabType();
const ControlType& CheckboxType();
const ControlType& SliderType();
const ControlType& SpinnerType();
const ControlType& DropdownType();
const ControlType& TextType();

// ---- what a node's action reaches back for (defined in menu_navigator.cpp) ----

// The activate handler every activatable node names as its action.
void OnActivate();

// The Space action for a control backed by a game panel: speak its hover tooltip.
std::function<void()> PanelTooltipAction(void* handle);

// ---- what the navigator calls back into a recipe TU for ----

// Defined with the geoscape recipe (recipe_strategy.cpp). Detaching has to hand the tray icon
// we hovered its matching MOUSE_OUT — the pair is symmetric (IN saves the camera location and
// sets bOverrideZoomHide, OUT restores and clears both), so leaving without it strands the pin
// in its hover state and leaves the camera parked on whatever site the player was last reading.
void ClearGeoscapeHover();

// ---- the companion wire cache (defined in recipe_context.cpp) ----

// The record grammar, the parse and the field readers live in src/wire_record.hpp — STL-only, so
// `tests/` can compile them without a game and pin the empty-value rule the format turns on.
// What follows is the engine-facing half: the call, the cache and the joins. It used to sit in
// the middle of the squad-select section, which is the only reason it ever looked squad-specific;
// every wire-backed recipe in all three families uses it.

// ONE outbound call per render, which is one per keypress — the same budget the tile cursor
// spends. Used wherever the widget tree cannot answer.
std::vector<WireRecord> FetchCompanionRecords(const char* className, const char* funcName);

// One recipe's companion words, held between renders and keyed on the widget pointers they
// describe. Five recipes need this and had five hand-written copies of it, which is how they came
// to disagree about the hazard below.
//
// Why cache at all: the navigator re-renders about ten times a second, so an uncached fetch is an
// outbound ProcessEvent per poll for words that usually cannot have changed. The game respawns a
// screen's rows whenever their content changes (ClearItems + a fresh panel per item), so a changed
// pointer set is an exact, self-invalidating key for almost everything.
//
// ⛔ Almost. The key is COMPARED and never dereferenced, so the never-cache-a-UObject-pointer rule
// is satisfied by construction — but a key that MATCHES is not proof the screen is the same one.
// UE3 reuses freed addresses, so an in-place repopulation can land the new widgets on the old
// ones' addresses and produce a matching key over stale words. That is not hypothetical: it is
// what `InvalidatePersonnelWords` was written for, and the reasoning is a property of the engine
// rather than of the personnel screen. So the rule is now uniform — every verb this layer fires
// that repopulates a screen calls `Invalidate()`, and `Detach()` calls `InvalidateWireCaches()`.
//
// `extra` is a second discriminator for a payload that also depends on something that is not a
// pointer (the commodity list's selected row — the card is the selection's shadow); `alwaysStale`
// is the debris room's deliberate unconditional re-ask while its picker is open.
class WireCache {
public:
    WireCache(const char* className, const char* funcName) : mClass(className), mFunc(funcName)
    {
        All().push_back(this);
    }

    const std::vector<WireRecord>& Get(std::vector<void*> key, long long extra = 0,
        bool alwaysStale = false)
    {
        if (!mWarm || alwaysStale || extra != mExtra || key != mKey) {
            mRecords = FetchCompanionRecords(mClass, mFunc);
            mKey = std::move(key);
            mExtra = extra;
            mWarm = true;
            mGeneration++;
        }
        return mRecords;
    }

    /// Bumped on every actual re-ask, so a caller that DERIVES something from the records (the
    /// save screen indexes them) can tell "the cache answered" from "the cache refetched"
    /// without keeping a second copy of the key.
    unsigned Generation() const { return mGeneration; }

    /// Drop the cache so the next Get re-asks, whatever the key says.
    void Invalidate() { mWarm = false; }

    static void InvalidateAll()
    {
        for (WireCache* cache : All())
            cache->Invalidate();
    }

private:
    // Function-local so the registry is alive before the first cache's constructor runs, whatever
    // order the file-scope caches are built in. That mattered when they all sat in one file; it
    // matters more now that they are spread over three recipe TUs, because cross-TU static
    // initialization order is unspecified and a namespace-scope registry could be constructed
    // after the caches that register into it. `All` is inline, so there is exactly one `caches`
    // across the whole binary and InvalidateAll really does reach every cache.
    static std::vector<WireCache*>& All()
    {
        static std::vector<WireCache*> caches;
        return caches;
    }

    const char* mClass;
    const char* mFunc;
    std::vector<WireRecord> mRecords;
    std::vector<void*> mKey;
    long long mExtra = 0;
    unsigned mGeneration = 0;
    bool mWarm = false;
};

// Drops every recipe's cached companion words. Called by Detach(); see the WireCache comment
// above for why a pointer key alone cannot be trusted across a screen change.
void InvalidateWireCaches();

// Join the parts a card names, dropping the ones the game is not showing — through
// AppendPhrase, never a bare ", ". See the definitions for what each one is for.
std::string JoinParts(const WireRecord& rec, std::initializer_list<const char*> keys);
std::string JoinPhrases(const std::string& first, const std::string& second);

// The first record of a given `kind` in a payload, or null.
const WireRecord* FindRecord(const std::vector<WireRecord>& records, const char* kind);

} // namespace Recipes
} // namespace MenuNavigator
