#include "menu_navigator.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "recipes/recipe_context.h"
#include "recipes/recipes.h"
#include "tactical_hud.h"
#include "avenger.h"
#include "tts.h"
#include "ue3_reflection.h"
#include "vocab.h"
#include "wire_record.hpp"
#include "log.h"

#include "Graph/GraphAnnouncer.hpp"
#include "Graph/GraphBuilder.hpp"
#include "Graph/GraphSheet.hpp"
#include "Graph/KeyGraph.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MenuNavigator {

// ---- what the recipe TUs share with this one ----
//
// The eighteen screen recipes moved out to src/recipes/, one TU per surface family. What is
// left in this file is the state machine — probe, settle, attach, differ, detach — plus the
// handful of things a recipe reaches back for. Those live in the Recipes namespace so that the
// include graph states which they are: recipe_context.h declares exactly that set and no more.
// This file goes on naming them unqualified.
using namespace X2Access::Graph;
using namespace X2Access::Wire;
using namespace Recipes;

namespace Recipes {

// ---- the control-type registry (spec §3.3: one registry, key collisions are an error) ----

const ControlType& ButtonType()
{
    static const ControlType type{
        /*Key*/ Vocab::RoleButton.key,
        /*Order*/ {AnnouncementKinds::Label, AnnouncementKinds::Role, AnnouncementKinds::Enabled,
            AnnouncementKinds::Reason, kKindAttention, AnnouncementKinds::Value,
            AnnouncementKinds::Selected},
        /*Common*/ []() {
            return std::vector<NodeAnnouncement>{
                NodeAnnouncement(
                    []() { return std::string(Vocab::RoleButton); }, false, AnnouncementKinds::Role),
            };
        },
    };
    return type;
}

// The MOTD news line: pure information, no activation — its label IS the content.
const ControlType& TickerType()
{
    static const ControlType type{
        /*Key*/ Vocab::RoleTicker.key,
        /*Order*/ {AnnouncementKinds::Label, AnnouncementKinds::Role},
        /*Common*/ []() {
            return std::vector<NodeAnnouncement>{
                NodeAnnouncement(
                    []() { return std::string(Vocab::RoleTicker); }, false, AnnouncementKinds::Role),
            };
        },
    };
    return type;
}

// A value-carrying settings control ("checkbox", "slider", …): the value reads right after the
// role, before any unavailable/reason status. One factory — the types differ only in role word.
ControlType MakeValueType(const Vocab::Word& roleWord)
{
    return ControlType{
        /*Key*/ roleWord.key,
        /*Order*/ {AnnouncementKinds::Label, AnnouncementKinds::Role, AnnouncementKinds::Value,
            AnnouncementKinds::Enabled, AnnouncementKinds::Reason, AnnouncementKinds::Selected},
        /*Common*/ [role = &roleWord]() {
            return std::vector<NodeAnnouncement>{
                NodeAnnouncement([role]() { return std::string(*role); }, false,
                    AnnouncementKinds::Role),
            };
        },
    };
}

const ControlType& TabType()
{
    static const ControlType type = MakeValueType(Vocab::RoleTab);
    return type;
}

const ControlType& CheckboxType()
{
    static const ControlType type = MakeValueType(Vocab::RoleCheckbox);
    return type;
}

const ControlType& SliderType()
{
    static const ControlType type = MakeValueType(Vocab::RoleSlider);
    return type;
}

const ControlType& SpinnerType()
{
    static const ControlType type = MakeValueType(Vocab::RoleSpinner);
    return type;
}

const ControlType& DropdownType()
{
    static const ControlType type = MakeValueType(Vocab::RoleDropdown);
    return type;
}

// A plain informational row (settings description lines): no role word, label then value.
const ControlType& TextType()
{
    static const ControlType type{
        /*Key*/ "text",
        /*Order*/ {AnnouncementKinds::Label, AnnouncementKinds::Value, AnnouncementKinds::Enabled,
            AnnouncementKinds::Reason, AnnouncementKinds::Selected},
        /*Common*/ nullptr,
    };
    return type;
}

// A tactical-bar ability icon. No role word — the stop's own heading already says what these
// are, and thirty "button"s a mission would be pure noise. Value (cooldown / charges / hotkey)
// reads right after the name, availability after that.
const ControlType& AbilityType()
{
    static const ControlType type{
        /*Key*/ "ability",
        /*Order*/ {AnnouncementKinds::Label, AnnouncementKinds::Value, AnnouncementKinds::Enabled,
            AnnouncementKinds::Reason},
        /*Common*/ nullptr,
    };
    return type;
}

// ---- the attach state a recipe builder reads ----
//
// Three of this file's globals, and only three, are part of the recipe contract: the screen a
// Build() reads, the expanded-group set a GraphBuilder needs, and the focus write-back map a
// builder fills in as it goes. Everything else in the attach state below stays private to the
// state machine, which is why it is declared in the anonymous namespace and not the header.
void* gScreen = nullptr;

GraphState gState;

// Focus write-back targets, refreshed on every render (handles are only valid immediately after
// the read that produced them — never carried across frames).
std::unordered_map<std::string, FocusTarget> gFocus;

// The Space action for a control backed by a game panel: speak its hover tooltip. What a
// sighted player only learns on mouse-over (descriptions, why-disabled reasons) is announced
// ONLY here, never in the focus readout — the same information gating they get. The action
// owns the speech, per the kernel's tooltip contract; no tooltip → the action never speaks
// (and an absent action refuses silently upstream).
std::function<void()> PanelTooltipAction(void* handle)
{
    return [handle]() {
        std::string tip = GameUI::ReadPanelTooltip(handle);
        if (!tip.empty())
            TTS::Speak(tip);
    };
}

// The same for a UIListItemString row, whose tooltip hangs off a DIFFERENT child (ButtonBG) —
// see ReadListItemTooltip. Wired on every list-row recipe so Space means one thing everywhere a
// list is on screen, and stays silent where the game registered nothing; SetDisabled(bDisabled,
// Reason) is the engine's standard why-greyed channel, so a row that grows a reason starts
// speaking it with no further change here.
std::function<void()> ListItemTooltipAction(void* handle)
{
    return [handle]() {
        std::string tip = GameUI::ReadListItemTooltip(handle);
        if (!tip.empty())
            TTS::Speak(tip);
    };
}

} // namespace Recipes

namespace {

bool gAttached = false;
// The claims' other half: attached AND focused. Every menu recipe is focused for its whole
// attach, so for them this bit shadows gAttached exactly. An AMBIENT recipe (the tactical HUD)
// attaches with it FALSE — the gameplay layer keeps the keyboard — and Tab / Escape toggle it.
// While false, the navigator holds no claims, runs no differ and speaks nothing: attached means
// only "this recipe would win the probe", which is what lets the graph keep its position and
// its wire caches across a whole mission without ever contending with the tile cursor.
bool gFocused = false;
const Recipe* gActive = nullptr;   // the attached recipe
const Recipe* gPending = nullptr;  // the recipe currently settling toward attach
int gSettle = 0;             // consecutive ready polls before attach (§9 native settle debounce)
uint64_t gLastPollMs = 0;

// Detach→attach stopwatch. A screen change the player drives (Escape, a button) is silent until
// the next screen attaches, and that gap is the thing players actually report, so it is measured
// rather than guessed at: Attach logs the elapsed time and, while the gap is open, the poll logs
// what each candidate screen looks like so a long one says WHICH screen was not ready.
uint64_t gDetachMs = 0;
uint64_t gGapLogMs = 0;
uint64_t gLoadSaidMs = 0; // last "Loading" cue, 0 while none has been spoken this gap
bool gGated = false;      // the game had the UI gated as of the last poll
bool gCinematic = false;  // bCinematicMode as of the last poll — logged because it decides
                          // attachment per SCREEN rather than for the whole layer

std::unique_ptr<KeyGraph> gGraph;

// The frame differ's memory (spec A4/§7.3): the identity last spoken.
ControlId gLastSpoken;

// The live-part watch (spec §3.2 "LIVE part"): the focused node's live announcement parts as
// last spoken, keyed by kind (or declaration index for kindless parts). When a rebuilt render
// resolves a different text for a watched part, the navigator speaks just that part.
std::unordered_map<std::string, std::string> gLiveBase;

constexpr int kSettlePolls = 2;
constexpr uint64_t kPollIntervalMs = 100;

// ---- shared speech/focus plumbing ----

void WriteBackFocus(const GraphNode* node)
{
    if (!node)
        return;
    auto it = gFocus.find(node->Id.StructuralKey);
    if (it == gFocus.end())
        return;
    const FocusTarget& target = it->second;
    if (target.custom) {
        target.custom();
        return;
    }
    if (target.listIndex >= 0) {
        if (gActive->SetListIndex)
            gActive->SetListIndex(gScreen, target.listIndex);
        return;
    }
    if (target.clearList && gActive->SetListIndex)
        gActive->SetListIndex(gScreen, -1);
    if (target.button && !GameUI::FocusButton(target.button)) {
        static int sWarned = 0;
        if (sWarned++ < 3)
            LOG_WARN("MenuNavigator: focus write-back failed for \"%s\"",
                     node->Id.StructuralKey.c_str());
    }
}

void RefreshLiveBaseline(const GraphNode* node)
{
    gLiveBase.clear();
    if (!node)
        return;
    std::vector<NodeAnnouncement> parts = GraphAnnouncer::EffectiveAnnouncements(node);
    for (std::size_t i = 0; i < parts.size(); i++) {
        if (!parts[i].Live || !parts[i].Text)
            continue;
        std::string key = parts[i].Kind.empty() ? ("#" + std::to_string(i)) : parts[i].Kind;
        gLiveBase[key] = parts[i].Text();
    }
}

// The focused node's live parts against the baseline: a changed part speaks alone (queued —
// game-driven changes are async, spec A7) and updates the baseline.
void WatchLiveParts(const GraphNode* node)
{
    if (!node)
        return;
    std::vector<NodeAnnouncement> parts = GraphAnnouncer::EffectiveAnnouncements(node);
    for (std::size_t i = 0; i < parts.size(); i++) {
        if (!parts[i].Live || !parts[i].Text)
            continue;
        std::string key = parts[i].Kind.empty() ? ("#" + std::to_string(i)) : parts[i].Kind;
        std::string text = parts[i].Text();
        auto it = gLiveBase.find(key);
        if (it == gLiveBase.end()) {
            gLiveBase.emplace(std::move(key), std::move(text));
            continue;
        }
        if (it->second != text) {
            it->second = text;
            if (!text.empty())
                TTS::Speak(text);
        }
    }
}

// Speak a focus change and update the differ memory so the differ stays silent about it
// (spec A4: announce exactly once, no matter what caused the change).
void SpeakMove(const GraphNode* from, const GraphNode* to, const std::string& transition)
{
    if (!to)
        return;
    std::string line = GraphAnnouncer::Compose(from, to, transition);
    if (!line.empty())
        TTS::Speak(line);
    gLastSpoken = to->Id;
    RefreshLiveBaseline(to);
}

void Detach()
{
    const char* name = gActive ? gActive->logName : "?";
    ClearGeoscapeHover();
    // The alert text cache is keyed on symbol+path+name, which is self-invalidating for a
    // REPLACED alert — but a second alert of the same kind, raised after the first closed,
    // reuses all three, so without this it would be answered from the first one's reading.
    GameUI::ResetAlertTextCache();
    // Same argument one layer up: the companion-word caches are keyed on widget POINTERS, and
    // the screen those pointers named is exactly what has just gone away. UE3 reuses freed
    // addresses, so a warm cache is one recycled pointer set away from speaking the previous
    // screen's words at a player who cannot see that they are wrong. Costs at most one outbound
    // call per screen entry, which the first render was going to make anyway.
    InvalidateWireCaches();
    gAttached = false;
    gFocused = false;
    gActive = nullptr;
    gPending = nullptr;
    gGraph.reset();
    gFocus.clear();
    gLiveBase.clear();
    gLastSpoken = {};
    gSettle = 0;
    gDetachMs = GetTickCount64();
    gGapLogMs = 0;
    gLoadSaidMs = 0;
    // The screen we were on is gone, so every finder's cache is suspect exactly now. Without
    // this, the next screen is found only when the finder's own second happens to elapse.
    GameUI::HintScreenChange();
    LOG_INFO("MenuNavigator: detached from %s", name);
}

// Screen name first (the user just landed here), then the landing readout queued behind it
// (§9 standardized first-focus). Shared by a menu recipe's attach and an
// ambient recipe's Tab focus entry, which are the same moment to the player.
void AnnounceEntry()
{
    TTS::Speak(gActive->ScreenName());
    GraphNode* landing = gGraph->CurrentNode();
    if (landing) {
        std::string line = GraphAnnouncer::ComposeFull(landing);
        if (!line.empty())
            TTS::Speak(line);
        gLastSpoken = landing->Id;
        RefreshLiveBaseline(landing);
        WriteBackFocus(landing);
    }
}

void Attach()
{
    gState = GraphState{};
    gLastSpoken = {};
    gLiveBase.clear();
    // ⛔ The focus map is reset HERE, not by each builder. gFocus holds raw widget pointers that
    // are only valid for the render that produced them, and Build() is reached from far more
    // places than this file — KeyGraph::Move, Activate, MoveStop and a dozen others all call
    // Rerender() themselves. Every builder used to open with its own gFocus.clear(), which made
    // the invariant something eighteen functions had to remember; the one that forgot would leak
    // stale FocusTarget::button pointers into the next render's write-back. This lambda is the
    // single entry into Build(), so it is the only place the reset cannot be forgotten.
    gGraph = std::make_unique<KeyGraph>([]() {
        gFocus.clear();
        return gActive->Build();
    }, &gState);
    if (!gGraph->Rerender()) {
        gGraph.reset();
        gAttached = false;
        return; // screen not actually readable yet — stay detached, poll again
    }
    gAttached = true;
    gFocused = !gActive->ambient;

    // An ambient attach is SILENT and unfocused: it happens the moment a mission's HUD settles,
    // mid-gameplay, and announcing a screen the player did not navigate to would be noise. The
    // player hears it when they ask for it (Tab → AnnounceEntry).
    if (gFocused)
        AnnounceEntry();

    // ONE announcement per session about the companion, and this is its moment: the first
    // main-menu attach is after DLC enumeration (so the package is loaded or never will be)
    // and the first time the player is listening to a screen. The greeting at hooks-init is
    // too early — mods enumerate a second after it — and a per-surface "unavailable" only comes
    // on a pressed key, deep in a mission, long after the player concluded the mod was broken.
    // Queued behind the menu's own announcement, never in place of it.
    static bool sCompanionChecked = false;
    if (!sCompanionChecked && gActive == &kMainMenuRecipe) {
        sCompanionChecked = true;
        if (!GameUI::CompanionPackageLoaded()) {
            LOG_WARN("MenuNavigator: companion package not loaded at the first main-menu attach");
            const std::string warning = GameUI::LocalizeString(
                "X2AccessVocab", "CompanionNotLoaded", "X2AccessCompanion");
            if (!warning.empty())
                TTS::Speak(warning);
            else
                LOG_ERROR("MenuNavigator: localized companion warning unavailable");
        }
    }

    if (gDetachMs) {
        LOG_INFO("MenuNavigator: attached to %s%s (%zu focus targets) — %llu ms after detach",
            gActive->logName, gFocused ? "" : " (ambient, unfocused)", gFocus.size(),
            static_cast<unsigned long long>(GetTickCount64() - gDetachMs));
        gDetachMs = 0;
    } else {
        LOG_INFO("MenuNavigator: attached to %s%s (%zu focus targets)", gActive->logName,
            gFocused ? "" : " (ambient, unfocused)", gFocus.size());
    }
}

// ---- the ambient recipe's focus toggle (Tab / Escape / targeting yield) ----

// Tab into the HUD graph. The rebuild comes first because the graph was last rendered at
// attach (or the previous focus), possibly minutes of gameplay ago — every widget pointer and
// every word in it is suspect until Rerender() replaces them.
void EnterAmbientFocus()
{
    if (!gGraph || !gGraph->Rerender()) {
        // The screen emptied under the attach (should have detached already; the next tick
        // will). A pressed mode key must not be silent about refusing.
        TTS::Speak(Vocab::SurfaceUnavailable(gActive->ScreenName().c_str()));
        return;
    }
    gFocused = true;
    AnnounceEntry();
    LOG_INFO("MenuNavigator: %s focused (HUD mode)", gActive->logName);
}

// Back to the gameplay layer. Silent on a YIELD (the game raised its own UI over the keys; its
// narration speaks next) — spoken on a player exit, because a mode change they asked for must
// answer.
void ExitAmbientFocus(bool silent)
{
    if (!gFocused)
        return;
    gFocused = false;
    // The recipe's own word for what the arrows drive again — two ambient layers exist now, and
    // "Map" said on the Avenger would name the wrong one.
    if (!silent)
        TTS::Speak((gActive && gActive->ambientExitName)
            ? std::string(*gActive->ambientExitName) : std::string(Vocab::TacticalWorldMode));
    LOG_INFO("MenuNavigator: %s unfocused (%s)", gActive ? gActive->logName : "?",
        silent ? "yielded to the game" : "player exit");
}

// The Tab handler. Claimed whenever a mission or the ant farm is active — broader than
// "attached" — so that the handler decides, and a refusal is spoken: a blind player cannot
// otherwise tell "refused" from "broken". The claim's predicate already excludes a focused
// layer (there Tab is the stop cycle), so this only ever ENTERS; Escape leaves.
void OnLayerEnter()
{
    if (!gAttached || !gActive->ambient) {
        // Nothing attached to name, so name the layer the player is standing in: the claim is
        // live in exactly two places, and which one is a flag read away.
        TTS::Speak(Vocab::SurfaceUnavailable(
            Avenger::IsGridActive() ? Vocab::ScreenAvengerHud : Vocab::ScreenTacticalHud));
        return;
    }
    if (gFocused)
        return;
    // Entering a focus the game would take straight back (the shot HUD is raised — the yield
    // test already says so) would announce a landing and then strip its keys 100 ms later.
    // Refuse up front instead, in the same words as any other unavailable surface.
    if (gActive->YieldFocus && gActive->YieldFocus()) {
        TTS::Speak(Vocab::SurfaceUnavailable(gActive->ScreenName().c_str()));
        return;
    }
    EnterAmbientFocus();
}

// After an adjust/activate that may have changed state: revalidate the screen (the action may
// have closed it), rebuild, and give the synchronous feedback — the fresh StateText when focus
// held still, or a normal landing when the action moved focus.
void SpeakStateAfterChange()
{
    if (!gActive || gActive->Probe() != gScreen) {
        Detach();
        return;
    }
    if (!gGraph->Rerender()) {
        Detach();
        return;
    }
    GraphNode* node = gGraph->CurrentNode();
    if (!node)
        return;
    if (!(node->Id == gLastSpoken)) {
        GraphNode* from = gGraph->Current()->NodeAt(gLastSpoken);
        SpeakMove(from, node, "");
        WriteBackFocus(node);
        return;
    }
    if (node->Vtable.StateText) {
        std::string line = node->Vtable.StateText();
        if (!line.empty())
            TTS::Speak(line);
    }
    RefreshLiveBaseline(node);
}

// ---- the recipe roster: PROBE ORDER, topmost screen first ----
//
// ⛔ This array literal IS the rule. OnTick below walks it as a first-match linear scan, so the
// order decides which recipe wins on a stacked screen, and it is written out in one place for
// exactly that reason. Do NOT replace it with a registry, a vector the recipe TUs push into or
// a linker-section trick now that the recipes live in four files: cross-TU static
// initialization order is unspecified, so a self-registering roster would order itself by LINK
// order and attach to the wrong screen non-deterministically. The screen it would get wrong
// first is a modal — which is a player answering a dialog blind.
//
// What the order is earning, top to bottom:
//  · The dialog leads: it never takes stack focus, so the screen it covers still probes ready.
//  · Anything that can overlay squad select — a dialog, the pause menu — has to resolve before
//    it, or the layer would attach to the screen underneath.
//  · Settings stacks on the shell, so the shell is last.
// Each recipe's own definition in src/recipes/ carries the rest of its ordering argument.
const Recipe* const kRecipes[] = {&kDialogRecipe, &kPopupRecipe, &kEndGameRecipe,
    &kMemorialDetailsRecipe, &kResearchReportRecipe,
    // The rewards recap beside the research deck it follows: the last modal of the post-mission
    // chain, sequential with the deck and never stacked with it.
    &kResearchUnlockedRecipe, &kRewardsRecapRecipe,
    &kNarrativeRecipe, &kIronManRecipe,
    // Mod Config Menu: pushed OVER the settings screen by the button it injects there, so
    // topmost-first puts it ahead of settings (the soundtrack picker on the next line never
    // shares a stack with it, so sitting ahead of that too costs nothing).
    &kModConfigRecipe,
    &kSoundtrackRecipe, &kOptionsRecipe, &kDifficultyRecipe, &kSaveLoadRecipe, &kPauseMenuRecipe,
    // The recruit list: pushed over the Armory room, raising only its own dialog (listed first).
    &kRecruitsRecipe,
    // The upgrade picker beside the facility picker: both are pushed over the room or the grid
    // they were opened from and raise only dialogs above themselves.
    &kPersonnelRecipe, &kChooseFacilityRecipe, &kChooseUpgradeRecipe, &kPromotionRecipe,
    // The bond confirmation is pushed OVER the bond screen (and over the bond alert and the
    // Training Center's slot dialog, both listed above); the bond screen is pushed over squad
    // select, after action and the armory, all listed below. Neither raises anything but dialogs
    // and the photobooth.
    &kBondConfirmRecipe, &kBondScreenRecipe,
    // The strip-equipment menu: pushed over the loadout and over squad select (both below), and
    // raising only its confirm dialogs above itself.
    &kManageEquipmentRecipe,
    &kArmoryLoadoutRecipe,
    // Weapon upgrades (and the pattern picker pushed over them): opened from the hub below, and
    // pushing only the rename dialog and confirm dialogs, both listed earlier.
    &kWeaponUpgradeRecipe,
    // Soldier customization, one recipe for the family: the category menu is pushed over the hub
    // (below) or the character pool (near the end), each category screen over the menu and the
    // picker over that — the probe takes the TOPMOST of the family, so their relative order needs
    // no entry here. It raises the name-edit dialog (no recipe; the observer) and dialogs.
    &kCustomizeRecipe,
    // The armory hub: under every screen it opens (all listed above), over the Armory room.
    &kArmoryMenuRecipe,
    // Below BOTH screens it opens: a slot asking for a soldier pushes UIPersonnel_CovertAction,
    // and the EDIT verb pushes UIArmory_Loadout. Above the Resistance Ring it was pushed over.
    &kCovertActionsRecipe,
    // The action's RESULT screen: over the geoscape, under the reward popups its Continue raises
    // (the popup recipe, listed first), never stacked with the picker.
    &kCovertReportRecipe,
    // Its own modal: raised OVER the Resistance Report and over the Ring, and it pushes only
    // dialogs and alerts (both above it here), so nothing it opens competes with it.
    &kStrategyPolicyRecipe,
    &kSquadSelectRecipe,
    // The post-mission walk-up. Everything it pushes — the promotion screens, dialogs, popups,
    // the pause menu — resolves above it; squad select is its pre-mission twin and the two are
    // never on the stack at once, so their relative order is free.
    &kAfterActionRecipe,
    // The trading post: pushed over the black market's entry menu (listed below), and never on
    // the stack beside the Buy side — the menu opens one or the other — so its place against
    // the commodity recipe is free. Only dialogs stack over it.
    &kBlackMarketSellRecipe,
    &kCommodityRecipe,
    // The black market's entry menu: UNDER both of its own screens — the trading post and the Buy
    // side, both above — and over the geoscape the POI opened it from.
    &kBlackMarketRecipe,
    // The mission blade: below every modal that pushes over it (dialogs, alerts, the sitrep
    // detail popup — all listed above), above the geoscape tray it covers.
    &kMissionBladeRecipe,
    // The Skyranger-arrived prompt: the blade's sequel, pushed over the geoscape when the flight
    // ends and never on the stack with the blade (the blade closed when the squad launched). It
    // raises nothing but the recall narrative, so only the modals listed first can cover it.
    &kSkyrangerArrivesRecipe,
    // The Chosen dossier: pushed over the geoscape (its HUD button and the map's own handler)
    // and over an alert's Chosen panel — the popup recipe's topmost-only probe stands down for
    // it there — and it raises nothing above itself.
    &kChosenInfoRecipe,
    // The monthly report and its Chosen page: sequential, never stacked (the report's own
    // CloseScreen pushes the Chosen page), both over the geoscape, and the Chosen page raises
    // only the level-up popups — resolved by the popup recipe above.
    &kChosenEventsRecipe, &kResistanceReportRecipe,
    &kRoomBuildRecipe, &kRoomRecipe, &kGeoscapeRecipe,
    // The mission bookends, ABOVE the ambient tactical recipe they are pushed over. Sequential
    // with each other (the Chosen screen pops itself before pushing the summary), so their
    // relative order is free; below every modal a mission can still raise, all listed earlier.
    &kChosenSummaryRecipe, &kMissionSummaryRecipe,
    // The loot receipt and the soldier sheet: the same band, for the same reason — pushed over the
    // HUD, raising only the pause menu (listed earlier), and never on the stack with each other or
    // with the bookends.
    &kLootRecipe, &kCharInfoRecipe,
    // The Tactical Legacy Pack's mission-side modals: the same band, for the same reason — each is
    // pushed over the HUD and popped before the next (docs/tle-mission-recon.md §1). The squad
    // review pushes the soldier loadout popup over itself, so that recipe leads the band; it is
    // also the popup the Legacy Operations menu opens, and the shell band below needs no second
    // entry for it (a probe answers by class, wherever it is listed).
    &kUnitStatsRecipe,
    &kScoringDialogRecipe, &kLadderUpgradeRecipe, &kLadderSoldiersRecipe,
    &kLadderMedalProgressRecipe, &kLadderMedalRecipe, &kLadderEndRecipe,
    &kChallengePostRecipe, &kChallengeEventRecipe,
    // The ambient Avenger recipe — the ant farm's Tab layer. Below every screen that stacks over
    // the grid (dialogs, popups, the pickers, the rooms, the geoscape — all of which make the
    // grid not-current, which is its probe's whole test), and it can never contend with the
    // tactical recipe: no stack holds a UIFacilityGrid and a UITacticalHUD at once.
    &kAvengerHudRecipe,
    // The ambient tactical recipe. Below every overlay a mission can raise (dialog, popup,
    // pause menu — all resolve above), and it can never contend with the strategy screens:
    // its probe requires a tactical gate no strategy stack can satisfy.
    &kTacticalHudRecipe,
    // The character pool's import / export picker is pushed over the pool, and the pool over the
    // shell; both raise only dialogs, the text-entry dialog and the customization family (all
    // listed above).
    &kPoolFilesRecipe, &kCharacterPoolRecipe,
    // The Tactical Legacy Pack's Legacy Operations branch, topmost first: the difficulty page
    // is pushed over the Legacy Operations menu (and pushes nothing but dialogs, listed first);
    // the menu is pushed over the hub; the hub over the shell. The soldier loadout popup the
    // menu opens is listed in the mission band above.
    &kLadderDifficultyRecipe, &kLadderMenuRecipe,
    // The Challenge Archives: pushed over the hub, raising only the LOADING progress dialog
    // above itself (the popup recipe's, listed first); Enter runs the map command directly.
    &kChallengeMenuRecipe,
    // Skirmish Mode: one screen for all its states, pushed over the hub, raising only its
    // three confirm dialogs above itself; Enter on its base menu opens the map directly.
    &kSkirmishRecipe,
    &kTLEHubRecipe, &kMainMenuRecipe};
static_assert(sizeof(kRecipes) / sizeof(kRecipes[0]) == Recipes::kRecipeCount,
    "a recipe was declared in recipes.h and not listed above — it would never be probed");

// ---- key handlers (game thread, via the arbiter's drain) ----

void OnArrow(GraphDir dir)
{
    if (!gGraph)
        return;
    // Spec §7.2 evaluation order: a focused adjustable control adjusts on Left/Right before
    // any navigation (settings sliders, spinners, dropdowns).
    if (dir == GraphDir::Left || dir == GraphDir::Right) {
        if (gGraph->TryAdjust(dir == GraphDir::Right ? 1 : -1, /*large*/ false)) {
            SpeakStateAfterChange();
            return;
        }
    }
    MoveResult r = gGraph->Move(dir);
    if (r.Moved) {
        WriteBackFocus(r.To);
        // The native scroll cue lives only in the game's own key handler — the path our
        // claims swallow — so successful moves plumb it explicitly (write-back is silent).
        GameUI::PlayNavSound();
        SpeakMove(r.From, r.To, r.TransitionLabel);
    }
    // Not moved (edge) — consumed silently; the layer's refusals are silent. The claim still
    // swallowed the chord, so the game's own navigation never runs in parallel (spec A1).
}

// The kernel's adjust verb on keys of its own. Left/Right already adjust a focused slider or
// spinner (OnArrow tries that first), but inside a GraphSheet the arrows are column travel and
// cannot be both — so an adjustable ROW (the trading post's sell quantity) needs a chord that
// means nothing else. Silent on a node with no OnAdjust: the layer's refusals are silent.
void OnAdjustKey(int sign)
{
    if (!gGraph)
        return;
    if (gGraph->TryAdjust(sign, /*large*/ false))
        SpeakStateAfterChange();
}

void OnEdgeJump(GraphDir dir)
{
    if (!gGraph)
        return;
    MoveResult r = gGraph->MoveToEdge(dir);
    if (r.Moved) {
        WriteBackFocus(r.To);
        GameUI::PlayNavSound();
        SpeakMove(r.From, r.To, "");
    }
}

void OnStop(int dir)
{
    if (!gGraph)
        return;
    MoveResult r = gGraph->MoveStop(dir, /*wrap*/ true);
    if (r.Moved) {
        WriteBackFocus(r.To);
        GameUI::PlayNavSound();
        SpeakMove(r.From, r.To, r.TransitionLabel);
    }
}

void OnTooltipKey()
{
    if (!gGraph)
        return;
    gGraph->Tooltip(); // the node's action owns the speech; no handler → silent refusal
}

// The game's own tab-cycle shortcut on the settings screen (PageUp/PageDown), kept because it
// beats Tab+arrows for a five-tab strip. SetSelectedTab wraps out-of-range itself.
void OnTabPage(int dir)
{
    if (!gGraph || gActive != &kOptionsRecipe)
        return;
    GameUI::SetOptionsTab(gScreen, GameUI::GetOptionsTab(gScreen) + dir);
    int now = GameUI::GetOptionsTab(gScreen);
    // The row list was just rebuilt; reconcile now rather than on the next differ tick.
    if (!gGraph->Rerender()) {
        Detach();
        return;
    }
    GraphNode* node = gGraph->CurrentNode();
    if (node && !(node->Id == gLastSpoken)) {
        // The rebuild took the focused row with it: the full-path landing already reads the
        // new tab (the row's context parent), so one utterance covers both.
        WriteBackFocus(node);
        SpeakMove(nullptr, node, "");
    } else {
        // Focus held (a tab node, or a row both tabs share) — name the new tab explicitly.
        std::string name = GameUI::GetOptionsTabName(gScreen, now);
        TTS::Speak(name.empty() ? Vocab::TabFallback(now) : name);
        if (node)
            RefreshLiveBaseline(node); // new tab under the same id — don't let the watch re-announce
    }
}

} // namespace

// The one key handler the recipes themselves name: every activatable node's action is this
// function, so it belongs to the Recipes namespace rather than to this file's anonymous one.
// Defined here, after the state machine it drives; declared in recipe_context.h.
namespace Recipes {

void OnActivate()
{
    if (!gGraph)
        return;
    if (gGraph->Activate())
        SpeakStateAfterChange();
    // No handler → consumed silently (spec §3.4).
}

} // namespace Recipes

void Init()
{
    using namespace InputArbiter;

    // Announcer host hooks — installed once (spec §6.3).
    GraphAnnouncer::PositionText = [](int index, int count) { return Vocab::Position(index, count); };

    // Speak the selected part only on TABS: which tab is open is real state, distinct from
    // where focus stands. Everywhere else focus write-back keeps the game's selection in sync,
    // so the word would just echo every landing. The muted nodes still declare the part: a
    // non-empty selected part is what stop landings and reconciliation resolve to (Tab returns
    // to the current tab / the game-selected row).
    GraphAnnouncer::PartFilter = [](const ControlType* type, const NodeAnnouncement& part) {
        if (part.Kind != AnnouncementKinds::Selected)
            return true;
        return type == &TabType();
    };

    // ⛔ Attached is NOT the claims' gate any more — FOCUSED is. For every menu recipe the two
    // are the same bit; the ambient tactical recipe spends most of a mission attached with the
    // claims off, which is the whole point of the split (the tile cursor keeps the arrows).
    auto attached = []() { return gAttached && gFocused; };
    auto optionsAttached = []() { return gAttached && gFocused && gActive == &kOptionsRecipe; };

    // ⛔ THE GEOSCAPE KEEPS SPACE, and Space alone.
    //
    // UIStrategyMap.OnUnrealCommand maps BOTH Enter and Space to ToggleScan(), which is the only
    // keyboard route to scanning there is. Taking BOTH would have removed scanning outright — so
    // one of the pair stays the game's, permanently, and Space is the one with nothing else to
    // do. (Its usual job here, reading the focused control's tooltip, is a courtesy; the tray's
    // words are already in the focus readout.)
    //
    // ✅ ENTER IS OURS, because the tray now has something for it that the game does not: a
    // keyboard route to AttemptSelectionCheckInterruption — send the Avenger there, make contact,
    // build the outpost. Vanilla reaches that ONLY through FXS_BUTTON_A, the controller accept
    // button, which no key on a mouse+keyboard setup produces. Leaving Enter to ToggleScan made
    // it a dead key on every tray stop where the Avenger is not already parked, which is most of
    // them. Space still scans, so nothing was traded away to get it.
    //
    // ✅ TAB IS OURS, earned by the HUD stop — Dark Events, Chosen Info, Resistance Orders and
    // the ResNet toggle, all drawn on screen and reachable by no key in the game. ⛔ NOT by the
    // world ring or the ';' status block: those are AIDS, enumerating regions and continents no
    // sighted player can read off the globe, so by the stop-list parity rule they live on
    // PageUp/PageDown and ';' and stay OUT of the Tab cycle. And Tab's own case label falls
    // through into FXS_KEY_ESCAPE's body inside `if(`notdefined(FINAL_RELEASE)), a debug
    // convenience, so Escape already carries the action it looked like it owned.
    //
    // The general test, which the Enter and Tab rulings each failed once before landing: a key is
    // takeable when the ACTION behind it survives losing it AND the layer has parity-legitimate
    // work for it. Space fails the first — ToggleScan has no other key once Enter is spent.
    auto attachedNotGeoscape
        = []() { return gAttached && gFocused && gActive != &kGeoscapeRecipe; };
    auto onDown = [](void (*fn)()) {
        return [fn](const KeyEvent& e) {
            if (e.down)
                fn();
        };
    };

    RegisterClaim({VK_UP, ModNone, attached,
        [](const KeyEvent& e) { if (e.down) OnArrow(GraphDir::Up); }, "menu: up"});
    RegisterClaim({VK_DOWN, ModNone, attached,
        [](const KeyEvent& e) { if (e.down) OnArrow(GraphDir::Down); }, "menu: down"});
    // Left/Right are claimed too — not because a vertical menu uses them, but because the
    // layer owns the whole navigation model while attached (spec A1): unclaimed, they reach
    // the game's own keyboard nav and its selection diverges from ours.
    RegisterClaim({VK_LEFT, ModNone, attached,
        [](const KeyEvent& e) { if (e.down) OnArrow(GraphDir::Left); }, "menu: left"});
    RegisterClaim({VK_RIGHT, ModNone, attached,
        [](const KeyEvent& e) { if (e.down) OnArrow(GraphDir::Right); }, "menu: right"});
    // ⛔ HOME GOES TO THE GEOSCAPE RING, not to the edge jump. The ring's sort origin is the only
    // cursor the globe has for a mouse+keyboard player — pin selection by proximity is per-frame
    // and `ISCONTROLLERACTIVE-gated — so Home there means "measure everything from what I just
    // heard" (see geo_scanner.cpp). Jump-to-first on a five-icon strip is a convenience; a place
    // to stand on a world map is not. End keeps the edge jump on every screen, this one included.
    RegisterClaim({VK_HOME, ModNone, attachedNotGeoscape, onDown([]() { OnEdgeJump(GraphDir::Up); }),
        "menu: home"});
    RegisterClaim({VK_END, ModNone, attached, onDown([]() { OnEdgeJump(GraphDir::Down); }),
        "menu: end"});
    RegisterClaim({VK_TAB, ModNone, attached, onDown([]() { OnStop(1); }), "menu: next stop"});
    RegisterClaim({VK_TAB, ModShift, attached, onDown([]() { OnStop(-1); }), "menu: previous stop"});
    RegisterClaim({VK_RETURN, ModNone, attached,
        [](const KeyEvent& e) { if (e.down && !e.repeat) OnActivate(); }, "menu: activate"});
    // Space reads the focused control's tooltip — the hover description sighted players get.
    // Claimed either way, so it can't reach the game's own handler and double-act on the
    // focus our write-back keeps in sync. No tooltip → silent (the layer's refusals are
    // silent).
    RegisterClaim({VK_SPACE, ModNone, attachedNotGeoscape,
        [](const KeyEvent& e) { if (e.down && !e.repeat) OnTooltipKey(); }, "menu: tooltip"});
    RegisterClaim({VK_PRIOR, ModNone, optionsAttached, onDown([]() { OnTabPage(-1); }),
        "settings: previous tab"});
    RegisterClaim({VK_NEXT, ModNone, optionsAttached, onDown([]() { OnTabPage(1); }),
        "settings: next tab"});
    // Plus and minus adjust the focused control — the kernel's TryAdjust on keys of its own.
    // Left/Right already do this for a slider or a spinner, but inside a GraphSheet the arrows
    // are column travel and cannot be both, and the trading post's sell quantity is a table row
    // that adjusts. The main-row keys and the numpad both, plus Shift on '=' because that is
    // where '+' lives. Budget (docs/input-map.md): '=' and '-' carry no binding in any input
    // section; numpad Add and Subtract are SpeedUpGame / SlowDownGame in the TACTICAL block
    // only, which a menu recipe focused mid-mission swallows for exactly as long as it holds the
    // keyboard — the same deal every claim here makes — and leniency leaves both at Ctrl+numpad.
    RegisterClaim({VK_OEM_PLUS, ModNone, attached, onDown([]() { OnAdjustKey(1); }),
        "menu: adjust up (+)"});
    RegisterClaim({VK_OEM_PLUS, ModShift, attached, onDown([]() { OnAdjustKey(1); }),
        "menu: adjust up (Shift+=)"});
    RegisterClaim({VK_ADD, ModNone, attached, onDown([]() { OnAdjustKey(1); }),
        "menu: adjust up (numpad +)"});
    RegisterClaim({VK_OEM_MINUS, ModNone, attached, onDown([]() { OnAdjustKey(-1); }),
        "menu: adjust down (-)"});
    RegisterClaim({VK_SUBTRACT, ModNone, attached, onDown([]() { OnAdjustKey(-1); }),
        "menu: adjust down (numpad -)"});

    // ---- the ambient layers' own key ----
    //
    // Bare Tab enters the HUD graph — on a mission and on the Avenger's ant farm alike. Tab is
    // DEAD in the game under X2Access: the companion's Config/XComInput.ini moves the game's
    // next-unit command off Tab onto E (and previous-unit off LeftShift onto Q) in both input
    // sections, so the vanilla forwarder no longer exists and claiming the key displaces
    // nothing. Claimed on MISSION ACTIVE / GRID ACTIVE rather than on attached, so that a press
    // while the layer cannot be entered is REFUSED aloud — a blind player cannot otherwise tell
    // "refused" from "broken" — and only while the layer is NOT already focused: focused, Tab is
    // the stop cycle (the `attached` claim above), and the two predicates are disjoint by
    // construction. Escape is the way back out. IsMissionActive and IsGridActive are plain
    // flags published from a tick, which is what a predicate evaluated inside the window
    // procedure is allowed to read.
    auto layerUnfocused = []() {
        return (TacticalHUD::IsMissionActive() || Avenger::IsGridActive())
            && !(gAttached && gFocused);
    };
    // No auto-repeat: a held key re-entering the layer would re-announce the landing.
    RegisterClaim({VK_TAB, ModNone, layerUnfocused,
        [](const KeyEvent& e) { if (e.down && !e.repeat) OnLayerEnter(); },
        "HUD layer: enter (tactical and Avenger)"});

    // Escape backs out of the HUD graph to the gameplay layer. Only while an ambient recipe is
    // focused — on every menu recipe Escape stays the game's own close key, unclaimed.
    auto ambientFocused = []() { return gAttached && gFocused && gActive->ambient; };
    RegisterClaim({VK_ESCAPE, ModNone, ambientFocused,
        [](const KeyEvent& e) { if (e.down && !e.repeat) ExitAmbientFocus(/*silent*/ false); },
        "HUD layer: leave"});

    LOG_INFO("MenuNavigator: initialized (%zu screen recipes)",
        sizeof(kRecipes) / sizeof(kRecipes[0]));
}

void OnTick()
{
    // The Tick event fires for every actor; throttle real work to the poll cadence. Key
    // handlers run from the arbiter's drain regardless — this pump only owns attach/detach
    // and the differ (spec A2 cadence carve-out: ~100 ms idle rebuild).
    uint64_t now = GetTickCount64();
    if (now - gLastPollMs < kPollIntervalMs)
        return;
    gLastPollMs = now;

    try {
        Vocab::LoadFromGame();
        Vocab::AnnounceStartup();
        // Being on top of the stack is not the same as being reachable. Starting a campaign pops
        // the narrative screen and leaves the difficulty screen underneath inited, focused and
        // topmost for the whole twelve seconds the loading movie plays — so the layer happily
        // announced a screen that, to everyone else, had already gone. The stack's own gates say
        // otherwise, and they are the same ones the game consults before opening its pause menu.
        GameUI::StackState stack = GameUI::ReadStackState();
        bool movie = GameUI::MoviePlaying();
        bool gated = movie || (stack.valid && stack.Blocked());
        // ⛔ Cinematic mode is NOT one of those gates, and treating it as one cost a player three
        // minutes of silence on the Chosen reveal. HideUIForCinematics() hides only the screens
        // that do NOT declare bShowDuringCinematic; the ones that do stay inited, focused, on
        // top and taking input — which is how the pause menu opens during a cutscene at all. So
        // the refusal moves down to the SCREEN, one bit the game itself consults.
        bool cinematic = stack.valid && stack.cinematic;
        auto hiddenByCinematic = [&](void* screen) {
            return cinematic && !GameUI::ScreenShowsDuringCinematic(screen);
        };
        if (gated != gGated) {
            gGated = gated;
            LOG_INFO("MenuNavigator: UI %s (movie %d, input blocked %d, screens hidden %d, "
                     "cinematic %d)",
                gated ? "gated — nothing is navigable" : "ungated", movie, stack.inputBlocked,
                stack.screensHidden, stack.cinematic);
        }
        if (cinematic != gCinematic) {
            gCinematic = cinematic;
            LOG_INFO("MenuNavigator: cinematic mode %s — screens declaring bShowDuringCinematic "
                     "stay attachable",
                cinematic ? "on" : "off");
        }
        if (gated) {
            if (gAttached)
                Detach();
            return;
        }
        // A cutscene that started UNDER an attached recipe takes its screen away exactly as a
        // hard gate would.
        if (gAttached && hiddenByCinematic(gScreen)) {
            Detach();
            return;
        }

        if (!gAttached) {
            const Recipe* found = nullptr;
            void* screen = nullptr;
            for (const Recipe* recipe : kRecipes) {
                screen = recipe->Probe();
                // A screen a cutscene has hidden still reads inited and focused (cinematic mode
                // clears bIsVisible and nothing else), so every probe would happily find its
                // corpse. Keep looking: the screen the cutscene LEFT up may be further down the
                // roster than the one it took away.
                if (screen && hiddenByCinematic(screen))
                    screen = nullptr;
                if (screen) {
                    found = recipe;
                    break;
                }
            }
            if (!found) {
                gPending = nullptr;
                gSettle = 0;
                // Only interesting while a gap is open (the player just left a screen and is
                // waiting). Twice a second is enough to see how the state bits move, and the
                // 15 s cutoff keeps an idle loading screen from writing forever.
                // Every half second at first, then every five — a long gap is the interesting
                // one, so it must not be the one that goes unlogged.
                uint64_t idle = gDetachMs ? now - gDetachMs : 0;
                uint64_t every = idle < 15000 ? 500 : 5000;
                if (gDetachMs && idle < 180000 && now - gGapLogMs >= every) {
                    gGapLogMs = now;
                    void* shell = GameUI::FindMainMenuShell();
                    LOG_DEBUG("MenuNavigator: %llu ms with nothing to attach — stack valid %d "
                              "gated %d, UIFinalShell %p/0x%X, %s",
                        static_cast<unsigned long long>(idle), stack.valid, gated, shell,
                        GameUI::ScreenStateBits(shell),
                        GameUI::DescribeTrackedScreens().c_str());
                }
                // A world change leaves nothing to read for ten seconds or more, and to a player
                // who cannot see the loading art that silence is indistinguishable from a crash
                // — it is what the "it hangs on the menu" reports were really about. Keyed off
                // the game's own overlay so it can never fire during ordinary play, where having
                // no screen attached is simply normal.
                if (idle >= 1500 && (!gLoadSaidMs || now - gLoadSaidMs >= 10000)
                    && GameUI::LoadScreenShowing()) {
                    gLoadSaidMs = now;
                    TTS::Speak(Vocab::Loading);
                }
                return;
            }
            if (found != gPending || screen != gScreen) {
                gPending = found;
                gScreen = screen;
                gSettle = 1; // §9 native settle: don't build against a half-constructed screen
                return;
            }
            if (++gSettle < kSettlePolls)
                return;
            gActive = found;
            Attach();
            return;
        }

        if (gActive->Probe() != gScreen) {
            Detach();
            return;
        }

        // The ambient recipe's two extra states. Focused: the yield test hands input back the
        // moment the game raises its own UI over the keys (targeting). Unfocused: the layer is
        // DORMANT — no rebuilds, no differ, no live watch — because ten widget-tree reads a
        // second for a graph nobody is in would be pure cost, and every pointer in the stale
        // render is refreshed by the Rerender() that focus entry starts with.
        if (gActive->ambient) {
            if (gFocused && gActive->YieldFocus && gActive->YieldFocus())
                ExitAmbientFocus(/*silent*/ true);
            if (!gFocused)
                return;
        }

        // The frame differ (spec §7.3): rebuild, reconcile, and speak only if the focused
        // identity differs from the identity last spoken (game-driven changes, content shifts).
        // On a held identity, the live-part watch reports value changes instead.
        if (!gGraph->Rerender()) {
            Detach(); // screen emptied under us
            return;
        }
        GraphNode* node = gGraph->CurrentNode();
        if (node && !(node->Id == gLastSpoken)) {
            GraphNode* from = gGraph->Current()->NodeAt(gLastSpoken);
            SpeakMove(from, node, ""); // differ landings (spec A7)
            WriteBackFocus(node);
        } else {
            WatchLiveParts(node);
        }
    } catch (const std::exception& e) {
        // Fault isolation (spec §7.9): a throw must not mute the layer or repeat every frame.
        LOG_ERROR("MenuNavigator: tick exception: %s", e.what());
        Detach();
    } catch (...) {
        LOG_ERROR("MenuNavigator: tick exception (unknown)");
        Detach();
    }
}

bool IsAttached()
{
    return gAttached;
}

bool OwnsInput()
{
    return gAttached && gFocused;
}

bool MenuAttached()
{
    return gAttached && !gActive->ambient;
}

const char* AttachedRecipeName()
{
    return (gAttached && gActive) ? gActive->logName : nullptr;
}

} // namespace MenuNavigator
