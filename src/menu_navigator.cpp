#include "menu_navigator.h"
#include "game_ui.h"
#include "input_arbiter.h"
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

namespace {

using namespace X2Access::Graph;
using namespace X2Access::Wire;

// Host-local announcement kind for the game's "!" badge (custom kinds are part of the kernel
// contract: they order wherever the type's Order places them and key user settings like the
// well-known ones).
constexpr const char* kKindAttention = "attention";

// ---- the control-type registry (spec §3.3: one registry, key collisions are an error) ----

const ControlType& ButtonType()
{
    static const ControlType type{
        /*Key*/ Vocab::RoleButton,
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
        /*Key*/ Vocab::RoleTicker,
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
ControlType MakeValueType(const char* roleWord)
{
    return ControlType{
        /*Key*/ roleWord,
        /*Order*/ {AnnouncementKinds::Label, AnnouncementKinds::Role, AnnouncementKinds::Value,
            AnnouncementKinds::Enabled, AnnouncementKinds::Reason, AnnouncementKinds::Selected},
        /*Common*/ [roleWord]() {
            return std::vector<NodeAnnouncement>{
                NodeAnnouncement([roleWord]() { return std::string(roleWord); }, false,
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

// ---- screen recipes + attach state ----

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

bool gAttached = false;
const Recipe* gActive = nullptr;   // the attached recipe
const Recipe* gPending = nullptr;  // the recipe currently settling toward attach
void* gScreen = nullptr;
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

GraphState gState;
std::unique_ptr<KeyGraph> gGraph;

// Focus write-back targets, refreshed on every render (handles are only valid immediately after
// the read that produced them — never carried across frames).
std::unordered_map<std::string, FocusTarget> gFocus;

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
                TTS::Speak(text, /*interrupt*/ false);
        }
    }
}

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
            TTS::SpeakInterrupt(tip);
    };
}

// Speak a focus change and update the differ memory so the differ stays silent about it
// (spec A4: announce exactly once, no matter what caused the change).
void SpeakMove(const GraphNode* from, const GraphNode* to, const std::string& transition,
    bool interrupt)
{
    if (!to)
        return;
    std::string line = GraphAnnouncer::Compose(from, to, transition);
    if (!line.empty())
        TTS::Speak(line, interrupt);
    gLastSpoken = to->Id;
    RefreshLiveBaseline(to);
}

// Defined with the geoscape recipe below. Detaching has to hand the tray icon we hovered its
// matching MOUSE_OUT — the pair is symmetric (IN saves the camera location and sets
// bOverrideZoomHide, OUT restores and clears both), so leaving without it strands the pin in its
// hover state and leaves the camera parked on whatever site the player was last reading.
void ClearGeoscapeHover();

void Detach()
{
    const char* name = gActive ? gActive->logName : "?";
    ClearGeoscapeHover();
    // The alert text cache is keyed on symbol+path+name, which is self-invalidating for a
    // REPLACED alert — but a second alert of the same kind, raised after the first closed,
    // reuses all three, so without this it would be answered from the first one's reading.
    GameUI::ResetAlertTextCache();
    gAttached = false;
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

void Attach()
{
    gState = GraphState{};
    gLastSpoken = {};
    gLiveBase.clear();
    gGraph = std::make_unique<KeyGraph>([]() { return gActive->Build(); }, &gState);
    if (!gGraph->Rerender()) {
        gGraph.reset();
        gAttached = false;
        return; // screen not actually readable yet — stay detached, poll again
    }
    gAttached = true;

    // Screen name first (interrupting — the user just landed here), then the landing readout
    // queued behind it (§9 standardized first-focus).
    TTS::SpeakInterrupt(gActive->ScreenName());
    GraphNode* landing = gGraph->CurrentNode();
    if (landing) {
        std::string line = GraphAnnouncer::ComposeFull(landing);
        if (!line.empty())
            TTS::Speak(line, /*interrupt*/ false);
        gLastSpoken = landing->Id;
        RefreshLiveBaseline(landing);
        WriteBackFocus(landing);
    }
    if (gDetachMs) {
        LOG_INFO("MenuNavigator: attached to %s (%zu focus targets) — %llu ms after detach",
            gActive->logName, gFocus.size(),
            static_cast<unsigned long long>(GetTickCount64() - gDetachMs));
        gDetachMs = 0;
    } else {
        LOG_INFO(
            "MenuNavigator: attached to %s (%zu focus targets)", gActive->logName, gFocus.size());
    }
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
        SpeakMove(from, node, "", /*interrupt*/ false);
        WriteBackFocus(node);
        return;
    }
    if (node->Vtable.StateText) {
        std::string line = node->Vtable.StateText();
        if (!line.empty())
            TTS::Speak(line, /*interrupt*/ true);
    }
    RefreshLiveBaseline(node);
}

// ---- the main menu recipe (UIFinalShell) ----

std::unique_ptr<GraphRender> BuildMainMenu()
{
    gFocus.clear();
    std::vector<GameUI::MenuItem> items = GameUI::ReadMainMenu(gScreen);
    if (items.empty())
        return nullptr; // treated as "closed" — focus state survives for the next good render

    GraphBuilder b;
    for (const GameUI::MenuItem& item : items) {
        // Stable content-id key (labels are unique on the shell); the button pointer rides
        // along as the tier-1 recovery token.
        ControlId id = ControlId::Referenced(item.handle, "shell/" + item.label);
        gFocus[id.StructuralKey] = FocusTarget{-1, item.handle, false};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(item.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (item.disabled) {
            // The state alone — a sighted player sees the graying without hovering. The
            // why-reason lives in the hover tooltip and speaks only on Space (OnTooltip).
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        // The game's "!" badge (new challenge live, unplayed Legacy content).
        if (item.attention) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Attention));
            vt.Announcements.back().Kind = kKindAttention;
        }
        // Semantic selected part (spec §3.2): the game's own focused button is where landings
        // and reconciliation resolve to.
        vt.Announcements.emplace_back(NodeAnnouncement(
            [focused = item.focused]() { return focused ? std::string(Vocab::Selected) : std::string(); },
            false, AnnouncementKinds::Selected));
        vt.OnActivate = [handle = item.handle]() { GameUI::ClickButton(handle); };
        vt.OnTooltip = PanelTooltipAction(item.handle);

        b.AddItem(std::move(id), std::move(vt));
    }

    // The rest of what a sighted player sees on this screen, in on-screen order after the menu
    // row: the My2K account button (bottom right), then the MOTD news ticker (bottom edge).
    // Neither joins gFocus: focus write-back would push the game's Navigator outside the menu
    // container (activation captures the handle directly, so Enter still works on My2K).
    GameUI::ShellExtras extras = GameUI::ReadShellExtras(gScreen);
    if (extras.my2kHandle) {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::My2KAccount));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        const char* status = nullptr;
        switch (extras.my2kStatus) {
        case GameUI::My2KStatus::Offline: status = Vocab::My2KOffline; break;
        case GameUI::My2KStatus::NotLinked: status = Vocab::My2KNotLinked; break;
        case GameUI::My2KStatus::Linked: status = Vocab::My2KLinked; break;
        case GameUI::My2KStatus::Unknown: break; // status query failed — label alone
        }
        if (status) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(status));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        vt.OnActivate = [handle = extras.my2kHandle]() { GameUI::ClickButton(handle); };
        vt.OnTooltip = PanelTooltipAction(extras.my2kHandle);
        b.AddItem(ControlId::Referenced(extras.my2kHandle, "shell/my2k"), std::move(vt));
    }
    if (extras.tickerHandle) {
        NodeVtable vt;
        vt.Type = &TickerType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(extras.tickerText));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        b.AddItem(ControlId::Referenced(extras.tickerHandle, "shell/ticker"), std::move(vt));
    }

    return b.Build();
}

void* ProbeMainMenu()
{
    void* shell = GameUI::FindMainMenuShell();
    return (shell && GameUI::IsScreenFocused(shell)) ? shell : nullptr;
}

std::string MainMenuName()
{
    return Vocab::ScreenMainMenu;
}

// ---- the settings recipe (UIOptionsPCScreen) ----

// The screen as a graph, three Tab-stops: the tab strip (Left/Right walk it, Enter selects),
// the current tab's rows (vertical), and the bottom buttons (horizontal — Save & Exit,
// Discard, Credits, GPU auto-detect). Tab cycles the stops; arrows never leave one.
std::unique_ptr<GraphRender> BuildOptions()
{
    gFocus.clear();
    GameUI::OptionsView view = GameUI::ReadOptions(gScreen);
    if (view.rows.empty() && view.buttons.empty())
        return nullptr;

    GraphBuilder b;

    b.BeginStop("tabs");
    b.StartRow();
    for (int i = 0; i < GameUI::kOptionsTabCount; i++) {
        std::string name
            = view.tabNames[i].empty() ? Vocab::TabFallback(i) : view.tabNames[i];
        ControlId id = ControlId::Structural("opts/tab/" + std::to_string(i));
        gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, true};

        NodeVtable vt;
        vt.Type = &TabType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(std::move(name)));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement(
            [sel = (i == view.currentTab)]() { return sel ? std::string(Vocab::Selected) : std::string(); },
            false, AnnouncementKinds::Selected));
        vt.OnActivate = [i]() { GameUI::SetOptionsTab(gScreen, i); };
        // Post-activation feedback: the fresh render's closure answers for the new state.
        vt.StateText = [sel = (i == view.currentTab)]() {
            return sel ? std::string(Vocab::Selected) : std::string();
        };
        b.AddItem(std::move(id), std::move(vt));
    }
    b.EndRow();

    b.BeginStop("content");
    std::string tabName = (view.currentTab >= 0 && view.currentTab < GameUI::kOptionsTabCount
                              && !view.tabNames[view.currentTab].empty())
        ? view.tabNames[view.currentTab]
        : Vocab::TabFallback(view.currentTab);
    b.PushContext(tabName);
    for (const GameUI::OptionRow& row : view.rows) {
        ControlId id = ControlId::Referenced(row.handle, "opts/row/" + row.label);
        gFocus[id.StructuralKey] = FocusTarget{row.index, nullptr, false};

        NodeVtable vt;
        vt.Announcements.emplace_back(NodeAnnouncement::Static(row.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;

        // The row's current value: a LIVE part, so game-driven changes (a preset applying, GPU
        // auto-detect finishing) re-speak just the value while the row stays focused. StateText
        // carries the same text for the synchronous adjust/toggle feedback path.
        std::string valueText;
        switch (row.widget) {
        case GameUI::OptionWidget::Checkbox:
            vt.Type = &CheckboxType();
            valueText = row.checked ? Vocab::Checked : Vocab::Unchecked;
            vt.OnActivate = [handle = row.handle]() {
                GameUI::SendRowCommand(handle, GameUI::kCmdEnter);
            };
            break;
        case GameUI::OptionWidget::Slider:
            vt.Type = &SliderType();
            valueText = Vocab::Percent(row.percent);
            // Disabled rows declare no adjust at all: the kernel's silent-refusal path, rather
            // than a command the game would refuse followed by a false state re-speak.
            if (!row.disabled)
                vt.OnAdjust = [handle = row.handle](int sign, bool) {
                    GameUI::SendRowCommand(handle,
                        sign > 0 ? GameUI::kCmdArrowRight : GameUI::kCmdArrowLeft);
                };
            break;
        case GameUI::OptionWidget::Spinner:
            vt.Type = &SpinnerType();
            valueText = row.value;
            if (!row.disabled)
                vt.OnAdjust = [handle = row.handle](int sign, bool) {
                    GameUI::SendRowCommand(handle,
                        sign > 0 ? GameUI::kCmdArrowRight : GameUI::kCmdArrowLeft);
                };
            break;
        case GameUI::OptionWidget::Dropdown:
            vt.Type = &DropdownType();
            valueText = row.value;
            if (!row.disabled)
                vt.OnAdjust = [handle = row.handle](int sign, bool) {
                    GameUI::AdjustDropdown(handle, sign);
                };
            break;
        case GameUI::OptionWidget::Button:
            vt.Type = &ButtonType();
            valueText = row.value; // the embedded button's own label ("Bindings")
            vt.OnActivate = [handle = row.handle]() {
                GameUI::SendRowCommand(handle, GameUI::kCmdEnter);
            };
            break;
        case GameUI::OptionWidget::Value:
        case GameUI::OptionWidget::Description:
            vt.Type = &TextType();
            valueText = row.value;
            // A description row may still carry the screen's click delegate; the row itself
            // ignores Enter when it has none.
            vt.OnActivate = [handle = row.handle]() {
                GameUI::SendRowCommand(handle, GameUI::kCmdEnter);
            };
            break;
        }
        if (!valueText.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(valueText));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
            vt.Announcements.back().Live = true;
            vt.StateText = [valueText]() { return valueText; };
        }
        if (row.disabled) {
            // The state alone; the why-reason is the row's hover tooltip (SetDisabled swaps
            // it in), so Space speaks it — same gating a sighted player gets.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        // Space: the row's hover tooltip (description, or why-disabled reason). Rows carry it
        // on their BG child panel, hence the row-specific reader.
        vt.OnTooltip = [handle = row.handle]() {
            std::string tip = GameUI::ReadRowTooltip(handle);
            if (!tip.empty())
                TTS::SpeakInterrupt(tip);
        };
        vt.Announcements.emplace_back(NodeAnnouncement(
            [sel = (row.index == view.selectedRow)]() {
                return sel ? std::string(Vocab::Selected) : std::string();
            },
            false, AnnouncementKinds::Selected));

        b.AddItem(std::move(id), std::move(vt));
    }
    b.PopContext();

    // Bottom buttons are their own stop: Tab reaches them, arrows stay inside the row list.
    b.BeginStop("buttons");
    b.StartRow();
    for (const GameUI::MenuItem& btn : view.buttons) {
        ControlId id = ControlId::Referenced(btn.handle, "opts/btn/" + btn.label);
        gFocus[id.StructuralKey] = FocusTarget{-1, btn.handle, true};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(btn.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (btn.disabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        vt.Announcements.emplace_back(NodeAnnouncement(
            [focused = btn.focused]() { return focused ? std::string(Vocab::Selected) : std::string(); },
            false, AnnouncementKinds::Selected));
        vt.OnActivate = [handle = btn.handle]() { GameUI::ClickButton(handle); };
        vt.OnTooltip = PanelTooltipAction(btn.handle);
        b.AddItem(std::move(id), std::move(vt));
    }
    b.EndRow();

    // First landing: the game's own selected row, else the first setting, else the first button
    // (never the tab strip — the player came here to change settings).
    ControlId start;
    for (const GameUI::OptionRow& row : view.rows) {
        if (start.IsValid() && row.index != view.selectedRow)
            continue;
        start = ControlId::Structural("opts/row/" + row.label);
        if (row.index == view.selectedRow)
            break;
    }
    if (!start.IsValid() && !view.buttons.empty())
        start = ControlId::Structural("opts/btn/" + view.buttons.front().label);
    if (start.IsValid())
        b.SetStart(std::move(start));

    return b.Build();
}

void* ProbeOptions()
{
    void* screen = GameUI::FindOptionsScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string OptionsName()
{
    std::string title = GameUI::ReadOptions(gScreen).title;
    return title.empty() ? Vocab::ScreenOptionsFallback : title;
}

// ---- the new-game recipes (UIShellDifficulty → UIChooseIronMan → UIShellNarrativeContent) ----

// A campaign-setup row as a node. These screens use only three of the mecha widget kinds —
// checkbox, description and value — and every row activates the same way, so one helper
// covers them all; the settings rows keep their own builder for sliders/spinners/dropdowns.
void AddSetupRow(GraphBuilder& b, const GameUI::OptionRow& row, const std::string& key,
    FocusTarget focus, bool selected, std::function<void()> tooltip)
{
    ControlId id = ControlId::Referenced(row.handle, key);
    gFocus[id.StructuralKey] = focus;

    NodeVtable vt;
    vt.Announcements.emplace_back(NodeAnnouncement::Static(row.label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;

    // A checkbox reads its state; anything else here is a line that does something when
    // pressed, which is a button however the game classifies its widget internally.
    std::string valueText;
    if (row.widget == GameUI::OptionWidget::Checkbox) {
        vt.Type = &CheckboxType();
        valueText = row.checked ? Vocab::Checked : Vocab::Unchecked;
    } else {
        vt.Type = &ButtonType();
        valueText = row.value;
    }
    if (!valueText.empty()) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(valueText));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
        // LIVE: the game changes these under us — picking a difficulty unchecks the other
        // three, and Legend forces the tutorial off — and the watch reports that while the row
        // stays focused. StateText answers the synchronous post-activation feedback.
        vt.Announcements.back().Live = true;
        vt.StateText = [valueText]() { return valueText; };
    }
    if (row.disabled) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
    }
    vt.Announcements.emplace_back(NodeAnnouncement(
        [selected]() { return selected ? std::string(Vocab::Selected) : std::string(); }, false,
        AnnouncementKinds::Selected));

    vt.OnActivate = [handle = row.handle]() { GameUI::SendRowCommand(handle, GameUI::kCmdEnter); };
    vt.OnTooltip = std::move(tooltip);

    b.AddItem(std::move(id), std::move(vt));
}

// A large button (the launch buttons, the Ironman choices): activation runs the game's own
// click handler. Their panels are not always navigable, so focus write-back is opt-in.
void AddSetupButton(
    GraphBuilder& b, const GameUI::MenuItem& btn, const std::string& key, bool writeBackFocus)
{
    ControlId id = ControlId::Referenced(btn.handle, key);
    if (writeBackFocus)
        gFocus[id.StructuralKey] = FocusTarget{-1, btn.handle, true};

    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(btn.label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (btn.disabled) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
    }
    vt.Announcements.emplace_back(NodeAnnouncement(
        [focused = btn.focused]() { return focused ? std::string(Vocab::Selected) : std::string(); },
        false, AnnouncementKinds::Selected));
    vt.OnActivate = [handle = btn.handle]() { GameUI::ClickButton(handle); };
    vt.OnTooltip = PanelTooltipAction(btn.handle);

    b.AddItem(std::move(id), std::move(vt));
}

// Speak a fixed description on Space. Used where the text a sighted player reads in the
// screen's side panel is not a real tooltip and so cannot go through ReadRowTooltip.
std::function<void()> TextAction(std::string text)
{
    if (text.empty())
        return nullptr; // no handler → the kernel refuses silently
    return [text = std::move(text)]() { TTS::SpeakInterrupt(text); };
}

// A row's own hover tooltip on Space (difficulty descriptions, why-disabled reasons).
std::function<void()> RowTooltipAction(void* handle)
{
    return [handle]() {
        std::string tip = GameUI::ReadRowTooltip(handle);
        if (!tip.empty())
            TTS::SpeakInterrupt(tip);
    };
}

// The difficulty screen. Two Tab-stops mirroring the game's two panels — the difficulty
// choices and the campaign options — plus the start button. While the Second Wave panel is
// open it REPLACES all three: the game clears the screen navigator and re-enables only that
// panel, so those are the only controls a sighted player can reach either.
std::unique_ptr<GraphRender> BuildDifficulty()
{
    gFocus.clear();
    GameUI::DifficultyView view = GameUI::ReadDifficulty(gScreen);

    GraphBuilder b;

    if (view.secondWaveOpen) {
        if (view.secondWave.empty())
            return nullptr;
        b.BeginStop("secondwave");
        b.PushContext(
            view.secondWaveTitle.empty() ? Vocab::GroupCampaignOptions : view.secondWaveTitle);
        for (const GameUI::OptionRow& row : view.secondWave) {
            // These rows' checkboxes are read-only: the screen's click delegate is the only
            // path that toggles them AND keeps the rival faction-HQ options exclusive.
            ControlId id = ControlId::Referenced(row.handle, "sw/" + row.label);
            gFocus[id.StructuralKey] = FocusTarget{row.index, nullptr, false};

            NodeVtable vt;
            vt.Type = &CheckboxType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(row.label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            std::string valueText = row.checked ? Vocab::Checked : Vocab::Unchecked;
            vt.Announcements.emplace_back(NodeAnnouncement::Static(valueText));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
            vt.Announcements.back().Live = true;
            vt.StateText = [valueText]() { return valueText; };
            if (row.disabled) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
                vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            }
            vt.Announcements.emplace_back(NodeAnnouncement(
                [sel = (row.index == view.secondWaveSelected)]() {
                    return sel ? std::string(Vocab::Selected) : std::string();
                },
                false, AnnouncementKinds::Selected));
            vt.OnActivate
                = [index = row.index]() { GameUI::ToggleSecondWaveOption(gScreen, index); };
            // The option's explanation lives in a localized array on the screen, not on the
            // row — it is the side-panel text a sighted player reads. Space speaks it.
            vt.OnTooltip = TextAction(GameUI::GetSecondWaveTooltip(gScreen, row.index));
            b.AddItem(std::move(id), std::move(vt));
        }
        b.PopContext();
        return b.Build();
    }

    if (view.difficulties.empty() && view.options.empty())
        return nullptr;

    // BuildMenu spawns the four rows and only then ticks the default difficulty, so for a
    // frame or two all four read unchecked. Attaching in that window lands on row 1 instead of
    // the selected difficulty and never corrects itself — the differ only re-speaks when the
    // focused IDENTITY changes, and it does not. Exactly one row checked is the readiness
    // test; until then this reads as "not built yet" and the navigator polls again.
    bool anyChecked = false;
    for (const GameUI::OptionRow& row : view.difficulties)
        anyChecked = anyChecked || row.checked;
    if (!view.difficulties.empty() && !anyChecked)
        return nullptr;

    b.BeginStop("difficulty");
    b.PushContext(Vocab::GroupDifficulty);
    for (const GameUI::OptionRow& row : view.difficulties) {
        // These four are a RADIO GROUP, so the semantic selection is the CHECKED one, not the
        // one the game's cursor happens to sit on (row 1 on arrival). That distinction is what
        // lands first focus on the difficulty actually chosen: the kernel resolves a landing to
        // the selected member of the stop, which is a stronger mechanism than SetStart and
        // overrides it. Everything else on this screen keeps focus as its selection.
        AddSetupRow(b, row, "diff/" + row.label, FocusTarget{-1, row.handle, false}, row.checked,
            RowTooltipAction(row.handle));
    }
    b.PopContext();

    b.BeginStop("options");
    b.PushContext(Vocab::GroupCampaignOptions);
    for (const GameUI::OptionRow& row : view.options) {
        AddSetupRow(b, row, "opt/" + row.label, FocusTarget{-1, row.handle, false}, row.focused,
            RowTooltipAction(row.handle));
    }
    b.PopContext();

    // The start button gets no focus write-back: the game calls DisableNavigation() on it, so
    // driving the navigator there would push focus somewhere the game refuses to keep it.
    if (view.start.handle) {
        b.BeginStop("start");
        AddSetupButton(b, view.start, "diff/start", /*writeBackFocus*/ false);
    }

    return b.Build();
}

void* ProbeDifficulty()
{
    void* screen = GameUI::FindDifficultyScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string DifficultyName()
{
    std::string title = GameUI::ReadDifficulty(gScreen).title;
    return title.empty() ? Vocab::ScreenDifficultyFallback : title;
}

// The Ironman choice: a warning and two buttons, nothing else on the screen.
std::unique_ptr<GraphRender> BuildIronMan()
{
    gFocus.clear();
    GameUI::IronManView view = GameUI::ReadIronMan(gScreen);
    if (view.buttons.empty())
        return nullptr;

    GraphBuilder b;
    b.BeginStop("buttons");
    b.StartRow();
    for (const GameUI::MenuItem& btn : view.buttons)
        AddSetupButton(b, btn, "iron/" + btn.label, /*writeBackFocus*/ true);
    b.EndRow();
    return b.Build();
}

void* ProbeIronMan()
{
    void* screen = GameUI::FindIronManScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

// The warning IS the screen — a sighted player reads it without hovering anything — so it is
// spoken on arrival rather than hidden behind the tooltip key.
std::string IronManName()
{
    GameUI::IronManView view = GameUI::ReadIronMan(gScreen);
    std::string name = view.title.empty() ? Vocab::ScreenIronManFallback : view.title;
    if (!view.body.empty())
        name += ". " + view.body;
    return name;
}

// The optional-narrative screen: the expansion-narrative toggle and the DLC list in one stop
// (they read as one column on screen), then the button that actually starts the campaign.
std::unique_ptr<GraphRender> BuildNarrative()
{
    gFocus.clear();
    GameUI::NarrativeView view = GameUI::ReadNarrative(gScreen);
    if (!view.xpack.handle && view.rows.empty())
        return nullptr;

    GraphBuilder b;
    b.BeginStop("content");
    if (view.xpack.handle) {
        // Not a list row: deselect the list so the game's own selection follows ours out of it.
        AddSetupRow(b, view.xpack, "narr/xpack", FocusTarget{-1, view.xpack.handle, true},
            view.xpack.focused, TextAction(view.xpackTooltip));
    }
    for (std::size_t i = 0; i < view.rows.size(); i++) {
        const GameUI::OptionRow& row = view.rows[i];
        // The row's summary shows in the side panel as focus moves; Space speaks it, the same
        // gating every other screen's descriptions get.
        AddSetupRow(b, row, "narr/" + row.label, FocusTarget{row.index, nullptr, false},
            row.index == view.selectedRow,
            TextAction(i < view.summaries.size() ? view.summaries[i] : std::string()));
    }

    if (view.start.handle) {
        b.BeginStop("start");
        AddSetupButton(b, view.start, "narr/start", /*writeBackFocus*/ false);
    }
    return b.Build();
}

void* ProbeNarrative()
{
    void* screen = GameUI::FindNarrativeScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string NarrativeName()
{
    std::string title = GameUI::ReadNarrative(gScreen).title;
    return title.empty() ? Vocab::ScreenNarrativeFallback : title;
}

// The soundtrack picker the difficulty screen's soundtrack row opens (Tactical Legacy Pack
// only). Same shape as the narrative screen: a choice list, then the confirm button.
std::unique_ptr<GraphRender> BuildSoundtrack()
{
    gFocus.clear();
    GameUI::SoundtrackView view = GameUI::ReadSoundtrack(gScreen);
    if (view.rows.empty())
        return nullptr;

    GraphBuilder b;
    b.BeginStop("content");
    for (const GameUI::OptionRow& row : view.rows) {
        AddSetupRow(b, row, "snd/" + row.label, FocusTarget{row.index, nullptr, false},
            row.index == view.selectedRow, RowTooltipAction(row.handle));
    }
    if (view.start.handle) {
        b.BeginStop("start");
        AddSetupButton(b, view.start, "snd/start", /*writeBackFocus*/ false);
    }
    return b.Build();
}

void* ProbeSoundtrack()
{
    void* screen = GameUI::FindSoundtrackScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

// Ours rather than the game's: this screen's localized title does not exist in the Community
// Highlander's rebuild of XComGame (see the UIShellSoundtrackPicker note in game_ui.cpp).
std::string SoundtrackName()
{
    return Vocab::ScreenSoundtrackFallback;
}

// ---- the in-game pause menu recipe (UIPauseMenu) ----

// Escape over a running campaign, in the tactical layer and on the Avenger alike. One vertical
// list of plain text rows, and WHICH rows depends on where the game is (save and restart come
// and go with the mission, difficulty and Second Wave only outside the shell) — so nothing here
// is positional; the list is read as it stands and activation goes back through its index.
std::unique_ptr<GraphRender> BuildPauseMenu()
{
    gFocus.clear();
    GameUI::PauseMenuView view = GameUI::ReadPauseMenu(gScreen);
    if (view.options.empty())
        return nullptr;

    GraphBuilder b;
    b.BeginStop("options");
    for (size_t i = 0; i < view.options.size(); i++) {
        const GameUI::MenuItem& opt = view.options[i];
        const int index = static_cast<int>(i);

        ControlId id = ControlId::Referenced(opt.handle, "pause/" + opt.label);
        gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(opt.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (opt.disabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        vt.Announcements.emplace_back(NodeAnnouncement(
            [selected = opt.focused]() {
                return selected ? std::string(Vocab::Selected) : std::string();
            },
            false, AnnouncementKinds::Selected));
        // Read gScreen at activation rather than capturing it: the handler runs from the input
        // drain, a frame or more after this render, and the screen may have been rebuilt.
        vt.OnActivate = [index]() { GameUI::ActivatePauseMenuItem(gScreen, index); };
        b.AddItem(std::move(id), std::move(vt));
    }
    return b.Build();
}

void* ProbePauseMenu()
{
    void* screen = GameUI::FindPauseMenu();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string PauseMenuName()
{
    std::string title = GameUI::ReadPauseMenu(gScreen).title;
    return title.empty() ? Vocab::ScreenPauseMenuFallback : title;
}

// ---- the modal dialog recipe (UIDialogueBox) ----

// The confirmations the setup screens raise (turning the tutorial off, choosing Ironman,
// disabling the expansion narrative). The box is a PERMANENT screen that never takes stack
// focus, so it is probed on its queue instead — and probed FIRST, because the screen it covers
// still reports itself focused underneath.
std::unique_ptr<GraphRender> BuildDialog()
{
    gFocus.clear(); // Flash-driven: no panels to drive the game's own focus onto

    GameUI::DialogView view = GameUI::ReadDialog(gScreen);

    // Identity has to carry the dialog's text: one dialog can be replaced by another between
    // two polls (a callback raising the next), and without that the differ would see the same
    // node and stay silent about a question the player has not heard.
    const std::string key = view.title + "\n" + view.body + "\n";

    GraphBuilder b;
    b.BeginStop("buttons");
    // The question itself is a context, not the screen name: a REPLACED dialog gets no fresh
    // attach to speak a screen name with, and the landing readout carries only the node's own
    // path — so the text has to live in that path to be heard at all.
    std::string question = view.title;
    if (!view.body.empty())
        question += question.empty() ? view.body : ". " + view.body;
    if (!question.empty())
        b.PushContext(question, "", /*positions*/ false);
    b.StartRow();

    // The game gates its own Enter/Escape on these labels being non-empty; a dialog with
    // neither cannot be answered at all, so it becomes a read-only line carrying its text.
    if (view.accept.empty() && view.cancel.empty()) {
        std::string text = view.body.empty() ? view.title : view.body;
        if (text.empty())
            return nullptr;
        b.AddLabel(ControlId::Structural(key + "notice"), [text]() { return text; });
        b.EndRow();
        return b.Build();
    }

    struct Choice {
        const std::string& label;
        const char* suffix;
        bool (*Resolve)(void*);
    };
    const Choice choices[2] = {
        {view.accept, "accept", GameUI::DialogAccept},
        {view.cancel, "cancel", GameUI::DialogCancel},
    };
    for (const Choice& choice : choices) {
        if (choice.label.empty())
            continue;
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(choice.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = [resolve = choice.Resolve]() { resolve(gScreen); };
        b.AddItem(ControlId::Structural(key + choice.suffix), std::move(vt));
    }
    b.EndRow();
    return b.Build();
}

void* ProbeDialog()
{
    // The finder only hands back a box that is actually showing something, so presence is the
    // whole readiness test here — there is no focus bit to consult.
    return GameUI::FindDialogScreen();
}

// Just the word: the dialog's own title and body ride in the graph as a context, so naming the
// screen only has to announce that a modal question has appeared.
std::string DialogName()
{
    return Vocab::ScreenDialog;
}

// ---- the recipe roster (probe order: topmost screens first — settings stacks on the shell) ----

const Recipe kDialogRecipe{"dialog", ProbeDialog, BuildDialog, DialogName};
const Recipe kNarrativeRecipe{
    "narrative content", ProbeNarrative, BuildNarrative, NarrativeName, GameUI::SetNarrativeIndex};
const Recipe kIronManRecipe{"ironman", ProbeIronMan, BuildIronMan, IronManName};
const Recipe kSoundtrackRecipe{
    "soundtrack", ProbeSoundtrack, BuildSoundtrack, SoundtrackName, GameUI::SetSoundtrackIndex};
const Recipe kOptionsRecipe{
    "settings", ProbeOptions, BuildOptions, OptionsName, GameUI::SetOptionsListIndex};
// The Second Wave list is the only UIList on the difficulty screen; the rest of its controls
// hang off plain panels and take the SetSelectedNavigation path instead.
const Recipe kDifficultyRecipe{
    "difficulty", ProbeDifficulty, BuildDifficulty, DifficultyName, GameUI::SetSecondWaveIndex};
// Below the setup screens it can open (difficulty, Second Wave, settings) and above nothing —
// it is the bottom of the in-game stack the way the shell is the bottom of the menu stack.
const Recipe kPauseMenuRecipe{
    "pause menu", ProbePauseMenu, BuildPauseMenu, PauseMenuName, GameUI::SetPauseMenuIndex};
const Recipe kMainMenuRecipe{"main menu", ProbeMainMenu, BuildMainMenu, MainMenuName};

// The dialog leads: it never takes stack focus, so the screen it covers still probes ready.
// ---- squad select recipe (UISquadSelect) ----
//
// The last gate before every mission. Unlike the shell screens this one already HAS working
// keyboard navigation (UIList_SquadEditor handles the arrows, Tab and Enter with no controller
// gate — measured live), so a recipe has to earn its place. It does, twice over: the controls a
// sighted player uses most — launch, and the strip/build/boost verbs — are not on vanilla's
// focus path AT ALL (UINavigationHelp dispatches them from OnMouseEvent alone, and LaunchButton
// is bIsNavigable false), and the mission header no focus ever visits. As graph nodes they cost
// nothing; as a narrate-only reader they would have cost one claimed hotkey each.
//
// Attaching also takes vanilla's unbounded ShiftButtonFocus skip loop out of the picture, and
// lets DISABLED controls be announced rather than skipped — a sighted player still sees the
// greyed ИЗМЕНИТЬ. See docs/squad-select-recon.md §4 and §7.

constexpr const char* kSquadReaderClass = "X2AccessSquadReader";

// The record grammar, the parse and the field readers live in src/wire_record.hpp — STL-only, so
// `tests/` can compile them without a game and pin the empty-value rule the format turns on.
// Nothing here is squad-specific despite where the section sits; every wire-backed recipe uses it.

// ONE outbound call per render, which is one per keypress — the same budget the tile cursor
// spends. Used wherever the widget tree cannot answer: squad select retains no words at all, and
// the loadout retains its rows but not the four things it computes and throws away.
std::vector<WireRecord> FetchCompanionRecords(const char* className, const char* funcName)
{
    void* cdo = GameUI::FindCompanionCDO(className);
    if (!cdo)
        return {}; // FindCompanionCDO logs the miss (companion mod not loaded)

    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, funcName, zeros)) {
        LOG_WARN("MenuNavigator: %s.%s failed", className, funcName);
        return {};
    }
    const std::string raw
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    // CleanText is handed in rather than called inside the parser: one implementation of "strip
    // the markup the game colours its text with", shared with every other screen, and keeping it
    // out is what lets the parser stay engine-free.
    return ParseRecords(raw, &GameUI::CleanText);
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

// A card's spoken context line — what the announcer reads when focus ENTERS the card, and stays
// quiet about while moving within it.
//
// ⛔ The four screen-wide labels (edit / dismiss / promote / select-unit) live on the MISSION
// record, not the card: they are class defaults, identical for every card, and sending them six
// times over would be six copies of the same string per keypress. Looking them up on the card is
// what silently dropped the Edit, Dismiss and Promote nodes entirely — SquadControlLabel returned
// "" and the build skips an unlabelled node, so a whole live run of the screen offered a weapon
// and its utility slots and nothing else.
std::string SquadCardContext(const WireRecord& card, const WireRecord& mission, int visualIndex)
{
    const std::string locked = RecField(card, "locked");
    if (!locked.empty())
        return locked;
    if (!RecField(card, "empty").empty())
        return RecField(mission, "selectunit");

    // Reading order is the card's own top-to-bottom order, with the two icon-only markers last:
    // they qualify the soldier rather than identify them.
    std::string line = JoinParts(card, {"rank", "name", "nick", "class", "hp", "will", "psi",
                                           "faction", "bond"});
    if (line.empty())
        line = Vocab::SoldierFallback + (" " + std::to_string(visualIndex + 1));
    return line;
}

// The spoken label for one card control. The words are the companion's; which controls EXIST is
// native's read plus the companion's flags, and the two are joined here.
std::string SquadControlLabel(const WireRecord& card, const WireRecord& mission,
    const GameUI::SquadControl& control)
{
    switch (control.button) {
    case GameUI::SquadButton::Promote:
        return RecField(mission, "promotelabel");
    case GameUI::SquadButton::Edit:
        return RecField(mission, "edit");
    case GameUI::SquadButton::Dismiss:
        return RecField(mission, "dismiss");
    case GameUI::SquadButton::WeaponPrime:
        return JoinPhrases(RecField(card, "weaponlabel"), RecField(card, "weapon"));
    case GameUI::SquadButton::WeaponHeavy:
        return JoinPhrases(RecField(card, "heavylabel"), RecField(card, "heavy"));
    case GameUI::SquadButton::Utility: {
        // Positional: `util` is emitted once per widget INCLUDING the empty ones, so this index
        // and the companion's are the same count. When even that is silent the slot is locked,
        // and its own tooltip — the reason the game gives — is added as a Reason part below.
        const std::vector<std::string> slots = RecFieldAll(card, "util");
        const int index = control.utilityIndex;
        if (index >= 0 && index < static_cast<int>(slots.size()) && !slots[static_cast<size_t>(index)].empty())
            return slots[static_cast<size_t>(index)];
        return control.tooltip.empty() ? std::string(Vocab::UtilitySlotFallback) : control.tooltip;
    }
    case GameUI::SquadButton::Bond:
        // The icon's own tooltip IS its name here ("SOLDIER BOND" / the bondmate's), which keeps
        // this node localized without a word of ours; the card's `bond` line carries the state.
        return control.tooltip;
    default:
        return std::string();
    }
}

// Where Left/Right land when leaving card `from` in direction `step`: the same control ROW in the
// nearest card that has one, clamped to its last control. Cards are ragged — a Reaper has one
// utility slot where a Grenadier has three, and only some soldiers can be promoted — so a plain
// index would fall off the end of the short ones.
ControlId SquadNeighbour(const std::vector<std::vector<ControlId>>& columns, size_t from,
    int step, size_t row)
{
    for (int c = static_cast<int>(from) + step; c >= 0 && c < static_cast<int>(columns.size());
        c += step) {
        const std::vector<ControlId>& col = columns[static_cast<size_t>(c)];
        if (col.empty())
            continue;
        return col[row < col.size() ? row : col.size() - 1];
    }
    return columns[from][row]; // no card that way: a self-edge, which KeyGraph::Move reads as a wall
}

std::unique_ptr<GraphRender> BuildSquadSelect()
{
    gFocus.clear();

    GameUI::SquadSelectView view = GameUI::ReadSquadSelect(gScreen);
    // ⛔ Before the walk-up matinee fires PreM_LineupUI the screen is hidden AND deaf —
    // OnUnrealCommand returns early on !bReceivedWalkupEvent — and the cards are still being
    // filled. Refusing to build here is what keeps the layer from narrating a screen the player
    // cannot see yet (recon §5).
    if (!view.walkupDone || view.slots.empty())
        return nullptr;

    const std::vector<WireRecord> words = FetchCompanionRecords(kSquadReaderClass, "DescribeSquad");
    const WireRecord* mission = nullptr;
    std::vector<const WireRecord*> cards;
    for (const WireRecord& rec : words) {
        const std::string kind = RecField(rec, "kind");
        if (kind == "mission" && !mission)
            mission = &rec;
        else if (kind == "card")
            cards.push_back(&rec);
    }
    static const WireRecord kEmptyRecord;
    const WireRecord* missionRec = mission ? mission : &kEmptyRecord;

    GraphBuilder b(&gState.Expanded);

    // ---- the mission header: read by nobody today, because no focus goes there ----
    if (mission) {
        b.BeginStop("mission");
        b.SetRegion("mission");
        // Label and value both come from the game. Ours used to be English constants, which read
        // "objective, Уничтожить передатчик" — a mixed-language line three words from the game's
        // own word for it, the same defect the tutorial vocabulary table exists to prevent.
        struct HeaderField {
            const char* key;
            const char* labelKey;
        };
        static constexpr HeaderField kHeader[] = {
            {"op", nullptr},
            {"squadsize", "squadlabel"},
            {"objective", "objlabel"},
            {"difficulty", "difflabel"},
            {"rewards", "rewlabel"},
            {"sitrep", nullptr},
            {"sitrepdesc", nullptr},
        };
        for (const HeaderField& h : kHeader) {
            const std::string value = RecField(*mission, h.key);
            if (value.empty())
                continue;
            const std::string label = h.labelKey ? RecField(*mission, h.labelKey) : std::string();
            // The game's own captions — "ЗАДАЧИ:" and friends already carry their colon, which
            // is exactly the shape AppendPhrase exists for.
            const std::string text = JoinPhrases(label, value);
            b.AddLabel(ControlId::Structural(std::string("squad/mission/") + h.key),
                [text]() { return text; });
        }
    }

    // ---- the cards, in the screen's own visual order ----
    //
    // ARROW MODEL, and it is the screen's own geometry rather than a convention of ours: the
    // cards sit SIDE BY SIDE and each stacks its controls vertically, so Up/Down walks one
    // soldier's card and Left/Right steps to the soldier beside them at the same height. Every
    // edge below is wired EXPLICITLY (raw edges are applied after the menu pass, so they win):
    // the builder's automatic single-item chain would have run Down straight out of one card and
    // into the next, which reads as one long list and loses the column the screen actually draws.
    // A direction with nowhere to go points at ITSELF — KeyGraph::Move treats dest == node as a
    // wall and stays put, which is how a card's top and bottom stop without wrapping.
    b.BeginStop("squad");
    b.SetRegion("squad");
    std::vector<std::vector<ControlId>> columns(view.slots.size());
    for (size_t i = 0; i < view.slots.size(); i++) {
        const GameUI::SquadSlot& slot = view.slots[i];
        const WireRecord& card
            = i < cards.size() ? *cards[i] : kEmptyRecord;
        const int visual = static_cast<int>(i);

        // A pure-structure parent: never navigable, announced when focus enters the card from
        // outside and silent while moving within it. This is where the soldier is named, so the
        // controls under it can be as short as "primary weapon, Shotgun" — and with Left/Right
        // crossing cards, it is what makes each step across say who you landed on.
        b.PushContext(SquadCardContext(card, *missionRec, visual));

        // An empty or locked card holds no controls a sighted player can use either; its one
        // node IS the card, and activating it opens the soldier picker.
        if (slot.controls.empty()) {
            ControlId id = ControlId::Referenced(slot.handle, "squad/card/" + std::to_string(i));
            columns[i].push_back(id);
            gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [visual]() {
                GameUI::SquadControl none;
                GameUI::SetSquadSelectFocus(gScreen, visual, none);
            }};
            NodeVtable vt;
            vt.Type = &ButtonType();
            const std::string label = SquadCardContext(card, *missionRec, visual);
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            if (!slot.locked) {
                vt.OnActivate = [visual]() {
                    GameUI::SquadSelectView v = GameUI::ReadSquadSelect(gScreen);
                    if (visual < static_cast<int>(v.slots.size())) {
                        GameUI::SquadControl pick;
                        pick.button = GameUI::SquadButton::Edit; // the empty card's own click
                        GameUI::ActivateSquadControl(gScreen, v.slots[visual], pick);
                    }
                };
            }
            b.AddItem(std::move(id), std::move(vt));
            b.PopContext();
            continue;
        }

        // Promote, the heavy weapon and the bond icon are emitted HERE rather than by native:
        // their widgets are hidden (or inert) when the soldier has none, and only the unit state
        // knows which — so the companion's flags decide, and the order stays the card's own
        // top-to-bottom order (ESquadSelectButton's declaration order).
        std::vector<GameUI::SquadControl> controls;
        if (!RecField(card, "promote").empty()) {
            GameUI::SquadControl promote;
            promote.handle = slot.handle;
            promote.button = GameUI::SquadButton::Promote;
            controls.push_back(promote);
        }
        for (const GameUI::SquadControl& c : slot.controls) {
            controls.push_back(c);
            // The heavy weapon sits between the primary and the utility row on the card.
            if (c.button == GameUI::SquadButton::WeaponPrime && !RecField(card, "heavy").empty()) {
                GameUI::SquadControl heavy;
                heavy.handle = slot.handle;
                heavy.button = GameUI::SquadButton::WeaponHeavy;
                controls.push_back(heavy);
            }
        }
        if (!RecField(card, "canbond").empty() && slot.bondIcon) {
            GameUI::SquadControl bond;
            bond.handle = slot.handle;
            bond.button = GameUI::SquadButton::Bond;
            bond.tooltip = GameUI::ReadPanelTooltip(slot.bondIcon);
            controls.push_back(bond);
        }

        for (size_t c = 0; c < controls.size(); c++) {
            const GameUI::SquadControl& control = controls[c];
            const std::string label = SquadControlLabel(card, *missionRec, control);
            if (label.empty())
                continue;

            ControlId id = ControlId::Structural(
                "squad/card/" + std::to_string(i) + "/"
                + std::to_string(static_cast<int>(control.button)) + "/"
                + std::to_string(control.utilityIndex));
            columns[i].push_back(id);
            gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [visual, control]() {
                GameUI::SetSquadSelectFocus(gScreen, visual, control);
            }};

            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            if (control.button == GameUI::SquadButton::Bond) {
                // The icon's tooltip named the control; the card's own line says what state it
                // is in ("bond level 1, bondmate on the squad"), which is what the icon draws.
                const std::string state = RecField(card, "bond");
                if (!state.empty()) {
                    vt.Announcements.emplace_back(NodeAnnouncement::Static(state));
                    vt.Announcements.back().Kind = AnnouncementKinds::Value;
                }
            }
            if (control.disabled) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
                vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
                // Why the game refuses, in the game's own words — the padlock's hover text. The
                // label already IS the tooltip when nothing else named the slot, so this only
                // speaks when it adds something.
                if (!control.tooltip.empty() && control.tooltip != label) {
                    vt.Announcements.emplace_back(NodeAnnouncement::Static(control.tooltip));
                    vt.Announcements.back().Kind = AnnouncementKinds::Reason;
                }
            }
            if (!control.disabled) {
                // Re-read the screen at activation: the handler runs from the input drain, a
                // frame or more after this render, and the cards are rebuilt on every change.
                vt.OnActivate = [visual, control]() {
                    GameUI::SquadSelectView v = GameUI::ReadSquadSelect(gScreen);
                    if (visual < static_cast<int>(v.slots.size()))
                        GameUI::ActivateSquadControl(gScreen, v.slots[visual], control);
                };
            }
            b.AddItem(std::move(id), std::move(vt));
        }

        // The tired warning belongs to the CARD, not to any one control: CreateFlyoverTooltip
        // sets bHasTooltip only at eMentalState_Tired, so this line exists exactly when a mouse
        // player has one to hover. It rides as a read-only tail node so it can be reached without
        // the header repeating it on every step within the card.
        if (!slot.tooltip.empty()) {
            const std::string warn = slot.tooltip;
            ControlId id = ControlId::Structural("squad/card/" + std::to_string(i) + "/tired");
            columns[i].push_back(id);
            b.AddLabel(id, [warn]() { return warn; });
        }
        b.PopContext();
    }

    // Up/Down inside one card, Left/Right across the strip — see the note above the loop.
    for (size_t c = 0; c < columns.size(); c++) {
        for (size_t r = 0; r < columns[c].size(); r++) {
            const ControlId& here = columns[c][r];
            b.Connect(here, GraphDir::Up, columns[c][r > 0 ? r - 1 : r]);
            b.Connect(here, GraphDir::Down, columns[c][r + 1 < columns[c].size() ? r + 1 : r]);
            b.Connect(here, GraphDir::Left, SquadNeighbour(columns, c, -1, r));
            b.Connect(here, GraphDir::Right, SquadNeighbour(columns, c, 1, r));
        }
    }

    // ---- the actions a mouse player clicks and a keyboard player cannot reach ----
    b.BeginStop("actions");
    b.SetRegion("actions");
    if (view.launch) {
        ControlId id = ControlId::Referenced(view.launch, "squad/launch");
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(view.launchTitle));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (view.launchDisabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            // The game's own refusal text — "not enough soldiers", or whatever the mission's
            // CanLaunchMission complains about — read from the tooltip a mouse player hovers.
            const std::string reason = GameUI::ReadPanelTooltip(view.launch);
            if (!reason.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(reason));
                vt.Announcements.back().Kind = AnnouncementKinds::Reason;
            }
        } else {
            // Through the button's own Click(), so OnLaunchMission runs with every side effect
            // the game expects (hidden soldiers added, psi training paused, bLaunched set, the
            // departure cinematic started).
            vt.OnActivate = []() {
                GameUI::SquadSelectView v = GameUI::ReadSquadSelect(gScreen);
                if (v.launch)
                    GameUI::ClickButton(v.launch);
            };
        }
        b.AddItem(std::move(id), std::move(vt));
    }
    for (const GameUI::SquadVerb& verb : view.verbs) {
        ControlId id = ControlId::Structural(std::string("squad/verb/") + verb.handler);
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(verb.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        const char* handler = verb.handler;
        vt.OnActivate = [handler]() { GameUI::InvokeScreenVerb(gScreen, handler); };
        b.AddItem(std::move(id), std::move(vt));
    }

    return b.Build();
}

void* ProbeSquadSelect()
{
    void* screen = GameUI::FindSquadSelect();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string SquadSelectName()
{
    // The operation's own name is what a sighted player reads at the top left; the fallback is
    // only for the window between attach and the companion answering.
    for (const WireRecord& rec : FetchCompanionRecords(kSquadReaderClass, "DescribeSquad")) {
        if (RecField(rec, "kind") != "mission")
            continue;
        const std::string op = RecField(rec, "op");
        if (!op.empty())
            return op;
        break;
    }
    return Vocab::ScreenSquadSelectFallback;
}

const Recipe kSquadSelectRecipe{
    "squad select", ProbeSquadSelect, BuildSquadSelect, SquadSelectName};

// ---- armory loadout recipe (UIArmory_Loadout) ----
//
// The screen squad select hands the player to, and until now the only one in that flow with no
// reader at all: a live session equipped a medikit here with FIVE Escapes and two Enters and zero
// feedback of any kind (2026-08-21), which is what made a correct squad-select readout look like
// a stale one.
//
// Unlike squad select this is mostly a READER, for the same reason the ant farm is: every
// UIArmory_LoadoutItem RETAINS its words. What the companion adds is only what the screen
// computes and discards — the localized count sentence, the refusal behind a greyed row, the
// hover tooltip's stat block, and what equipping a candidate would change.
//
// ⛔ Only ONE list is navigable at a time. ChangeActiveList calls DisableNavigation() on the
// other, so the active list is the focusable stop and the equipped list becomes a READ-ONLY
// mirror while the locker is open — which is exactly what stays on screen, since "openList" only
// slides it aside. Writing a selection into a list the game has switched off is how the two
// models drift apart, so the mirror gets no focus write-back and no activation.
constexpr const char* kLoadoutReaderClass = "X2AccessLoadoutReader";

// An equipped row reads "SLOT, item"; a locker row has no slot label and reads as the item alone.
// An empty slot says so rather than leaving the slot name to stand on its own, which would sound
// identical to a filled slot whose item we failed to resolve.
std::string LoadoutRowLabel(const GameUI::LoadoutRow& row)
{
    if (row.title.empty()) {
        if (row.slotLabel.empty())
            return std::string();
        return JoinPhrases(row.slotLabel, Vocab::LoadoutEmptySlot);
    }
    return JoinPhrases(row.slotLabel, row.title);
}

std::unique_ptr<GraphRender> BuildArmoryLoadout()
{
    gFocus.clear();

    GameUI::LoadoutView view = GameUI::ReadArmoryLoadout(gScreen);
    if (!view.valid)
        return nullptr; // InitArmory has not run PopulateData yet — treated as "closed"

    const std::vector<WireRecord> words
        = FetchCompanionRecords(kLoadoutReaderClass, "DescribeLoadout");
    static const WireRecord kEmptyRecord;
    const WireRecord* head = &kEmptyRecord;
    std::vector<const WireRecord*> eqWords, lkWords;
    for (const WireRecord& rec : words) {
        const std::string kind = RecField(rec, "kind");
        if (kind == "header" && head == &kEmptyRecord)
            head = &rec;
        else if (kind == "row")
            (RecField(rec, "list") == "lk" ? lkWords : eqWords).push_back(&rec);
    }
    const bool lockerActive = view.active == GameUI::LoadoutList::Locker;
    const std::vector<const WireRecord*>& activeWords = lockerActive ? lkWords : eqWords;

    GraphBuilder b;

    // ---- who this is: three lines no focus visits on mouse+keyboard ----
    b.BeginStop("soldier");
    b.SetRegion("soldier");
    for (const char* key : {"who", "status", "stats"}) {
        const std::string text = RecField(*head, key);
        if (text.empty())
            continue;
        b.AddLabel(ControlId::Structural(std::string("loadout/soldier/") + key),
            [text]() { return text; });
    }

    // ---- the list the game is actually driving ----
    b.BeginStop("items");
    b.SetRegion("items");
    const std::string listTitle = lockerActive ? view.lockerTitle : view.inventoryTitle;
    if (!listTitle.empty())
        b.PushContext(listTitle);
    for (size_t i = 0; i < view.rows.size(); i++) {
        const GameUI::LoadoutRow& row = view.rows[i];
        const WireRecord& rec = i < activeWords.size() ? *activeWords[i] : kEmptyRecord;
        const std::string label = LoadoutRowLabel(row);
        if (label.empty() || !row.handle)
            continue;

        ControlId id = ControlId::Referenced(row.handle, "loadout/row/" + std::to_string(i));
        const int index = static_cast<int>(i);
        gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false,
            [index]() { GameUI::SetLoadoutSelection(gScreen, index); }};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (row.disabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            // The refusal in the game's own words. `disabledText` is declared on the row and
            // never assigned — SetDisabled queues the reason straight into Flash — so this is
            // the companion asking the screen the same question it asked itself.
            const std::string reason = RecField(rec, "reason");
            if (!reason.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(reason));
                vt.Announcements.back().Kind = AnnouncementKinds::Reason;
            }
        }
        // How many the inventory holds, and — for a locker row — what equipping it would change.
        // The delta rides the ROW rather than the soldier header because our focus write-back
        // lands AFTER the render that announces, so a header-sourced one would always be a row
        // behind.
        std::string value = row.infinite ? std::string() : RecField(rec, "count");
        AppendPhrase(value, RecField(rec, "delta"));
        if (!value.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        // Space: the hover tooltip a mouse player gets and a keyboard player never can —
        // DelayedShowTooltip is called only under `ISCONTROLLERACTIVE.
        const std::string detail = RecField(rec, "detail");
        if (!detail.empty())
            vt.OnTooltip = [detail]() { TTS::Speak(detail, /*interrupt*/ true); };
        if (!row.disabled) {
            // Selection FIRST, then OnAccept — it reads ActiveList.SelectedIndex and ignores
            // everything else, so a one-frame disagreement equips the wrong item.
            vt.OnActivate = [index]() {
                if (GameUI::SetLoadoutSelection(gScreen, index))
                    GameUI::ActivateLoadoutSelection(gScreen);
            };
        }
        if (index == view.selectedRow)
            b.SetStart(id);
        b.AddItem(std::move(id), std::move(vt));
    }
    if (!listTitle.empty())
        b.PopContext();

    // ---- what the soldier is still wearing, while the locker is open ----
    if (lockerActive && !view.equipped.empty()) {
        b.BeginStop("equipped");
        b.SetRegion("equipped");
        if (!view.inventoryTitle.empty())
            b.PushContext(view.inventoryTitle);
        for (size_t i = 0; i < view.equipped.size(); i++) {
            const std::string label = LoadoutRowLabel(view.equipped[i]);
            if (label.empty())
                continue;
            b.AddLabel(ControlId::Structural("loadout/eq/" + std::to_string(i)),
                [label]() { return label; });
        }
        if (!view.inventoryTitle.empty())
            b.PopContext();
    }

    // ---- the verbs a keyboard cannot reach ----
    //
    // All four are nav-help entries: UpdateNavHelp registers them with mouse handlers, and
    // OnUnrealCommand maps them to CONTROLLER trigger codes (FXS_BUTTON_LTRIGGER / RTRIGGER / R3,
    // and FXS_BUTTON_X for the clear). None of those is a key a mouse+keyboard player has.
    b.BeginStop("actions");
    b.SetRegion("actions");
    if (!lockerActive && view.selectedRow >= 0
        && view.selectedRow < static_cast<int>(view.rows.size())
        && view.rows[static_cast<size_t>(view.selectedRow)].canBeCleared
        && !view.makeAvailable.empty()) {
        // Keyed to the SELECTION rather than carried on the row, exactly as
        // UpdateNavHelp_LoadoutItem shows it: the nav help offers "make available" only while the
        // selected equipped item bCanBeCleared, and it acts on that item.
        const std::string label = JoinPhrases(view.makeAvailable,
            LoadoutRowLabel(view.rows[static_cast<size_t>(view.selectedRow)]));
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = []() {
            GameUI::LoadoutView v = GameUI::ReadArmoryLoadout(gScreen);
            if (v.valid && v.selectedRow >= 0
                && v.selectedRow < static_cast<int>(v.rows.size()))
                GameUI::ClearLoadoutRow(v.rows[static_cast<size_t>(v.selectedRow)]);
        };
        b.AddItem(ControlId::Structural("loadout/clear"), std::move(vt));
    }

    struct StripVerb {
        const char* labelKey;
        const char* handler;
        bool spent;
        const std::string* tip;
        const std::string* tipSpent;
    };
    const StripVerb kStrip[] = {
        {"stripitems", "OnStripItems", view.itemsStripped, &view.tipStripItems,
            &view.tipStripItemsOff},
        {"stripweapons", "OnStripWeapons", view.weaponsStripped, &view.tipStripWeapons,
            &view.tipStripWeaponsOff},
        {"stripgear", "OnStripGear", view.gearStripped, &view.tipStripGear, &view.tipStripGearOff},
    };
    for (const StripVerb& verb : kStrip) {
        const std::string label = RecField(*head, verb.labelKey);
        if (label.empty())
            continue;
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (verb.spent) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        const std::string tip = verb.spent ? *verb.tipSpent : *verb.tip;
        if (!tip.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(tip));
            vt.Announcements.back().Kind = AnnouncementKinds::Reason;
        }
        if (!verb.spent) {
            const char* handler = verb.handler;
            vt.OnActivate = [handler]() { GameUI::InvokeScreenVerb(gScreen, handler); };
        }
        b.AddItem(ControlId::Structural(std::string("loadout/verb/") + verb.handler),
            std::move(vt));
    }

    return b.Build();
}

void* ProbeArmoryLoadout()
{
    void* screen = GameUI::FindArmoryLoadout();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string ArmoryLoadoutName()
{
    // Whose loadout, which is the question two screens of arrowing makes urgent; the panel
    // heading is the fallback for the window between attach and the companion answering.
    for (const WireRecord& rec : FetchCompanionRecords(kLoadoutReaderClass, "DescribeLoadout")) {
        if (RecField(rec, "kind") != "header")
            continue;
        const std::string who = RecField(rec, "who");
        if (!who.empty())
            return who;
        break;
    }
    GameUI::LoadoutView view = GameUI::ReadArmoryLoadout(gScreen);
    return view.inventoryTitle.empty() ? std::string(Vocab::ScreenLoadoutFallback)
                                       : view.inventoryTitle;
}

const Recipe kArmoryLoadoutRecipe{
    "armory loadout", ProbeArmoryLoadout, BuildArmoryLoadout, ArmoryLoadoutName};


// ---- save / load recipe (UILoadGame, UISaveGame) ----
//
// The shell's first screen whose rows hold no words: UISaveLoadGameListItem.UpdateData pushes
// the date, the name and the mission into Flash and retains only the struct it built them from.
// So this is the squad-select split again — native reads which rows exist and which is selected,
// X2AccessSaveReader re-derives the text — and the recipe itself is deliberately the thinnest
// one in the file, because unlike the ant farm nothing here is un-ungated: the game's own
// OnUnrealCommand already walks the list with the arrows on mouse+keyboard, and its
// RawInputListener already deletes on Delete/Backspace. See docs/save-load-recon.md §2.
//
// ONE NODE PER ROW, and that is a decision rather than a shortcut. The row shows two buttons,
// but a second column would replace the one number a blind player cannot get any other way —
// "save 4 of 12" — with "1 of 2", because the builder stamps positions WITHIN a row that holds
// more than one item. The delete verb stays on the game's own key (which this layer must NOT
// claim: every other Delete claim in the project stands down while this screen is up) and is
// named once on attach, in the row's own localized word.

constexpr const char* kSaveReaderClass = "X2AccessSaveReader";

// One row's words, as the companion re-derived them from the row's own save header.
// Read out NAME FIRST and the wall-clock timestamp LAST. The row draws them the other way round
// (UpdateData puts the timestamp at the head of its first line), and that is right for a glance
// but wrong for speech: a list of autosaves differs in the description, not in the minute it was
// written, so the timestamp is the part you sit through before reaching the part you wanted.
struct SaveWords {
    std::string desc;    // the player/auto save description — "АВТОСОХРАНЕНИЕ: Кампания 6, ..."
    std::string name;    // the game-type line
    std::string mission; // its detail
    std::string time;    // the formatted wall-clock timestamp
    bool isNew = false;  // the save screen's row 0
};

// Keyed by the row's OWN Index field, which is what the screen's SetSelection talks in — never
// by array position, so a null row in the middle of the list cannot shift the words along.
using SaveWordMap = std::unordered_map<int, SaveWords>;

// The companion call, cached on the ROW POINTERS. The navigator re-renders about ten times a
// second and this walk is not free (the save screen's new-save row re-runs FillInHeaderForSave,
// which iterates the history), but a save list changes ONLY through BuildMenu(), which respawns
// every row panel — so an unchanged pointer vector is an exact, self-invalidating cache key.
const SaveWordMap& FetchSaveWords(const std::vector<GameUI::SaveSlot>& slots)
{
    static std::vector<void*> sKey;
    static SaveWordMap sWords;

    std::vector<void*> key;
    key.reserve(slots.size() + 1);
    key.push_back(gScreen);
    for (const GameUI::SaveSlot& slot : slots)
        key.push_back(slot.handle);
    if (key == sKey)
        return sWords;
    sKey = key;
    sWords.clear();

    // The same wire payload every other recipe reads, decoded by the same parser — this screen
    // only differs in what it does with the records afterwards (index them, rather than keep the
    // order). A second hand-written copy of the parse is how the two would drift.
    for (const WireRecord& rec : FetchCompanionRecords(kSaveReaderClass, "DescribeSaves")) {
        const std::string indexText = RecField(rec, "index");
        if (indexText.empty())
            continue;
        const int index = atoi(indexText.c_str());
        if (index < 0)
            continue;
        SaveWords words;
        words.desc = RecField(rec, "desc");
        words.name = RecField(rec, "name");
        words.mission = RecField(rec, "mission");
        words.time = RecField(rec, "time");
        // Presence is the flag — the producer sends `new` only on the save screen's row 0, and
        // whatever it carries as a value is not read.
        words.isNew = !RecFieldAll(rec, "new").empty();
        sWords[index] = std::move(words);
    }
    LOG_DEBUG("MenuNavigator: save/load — described %zu of %zu rows", sWords.size(), slots.size());
    return sWords;
}

// What the row reads out as. The button's own verb leads ONLY on the new-save row, where it is
// the thing that makes that row different from every other one ("НОВОЕ СОХРАНЕНИЕ"); on the
// rest it is the same word twelve times over and the save's own description is the content.
std::string SaveSlotLabel(const GameUI::SaveSlot& slot, const SaveWords* words)
{
    if (!words) {
        // No record for this row: the companion is missing, or the row carries no header at
        // all. The widget's own label is still a true statement about what is there.
        return slot.acceptLabel;
    }
    // ⛔ AppendPhrase, not a bare comma: `desc` is the engine's own save description, split out
    // of Header.Description, and the error-report branch ends it in a full stop.
    std::string line;
    if (words->isNew)
        AppendPhrase(line, slot.acceptLabel);
    AppendPhrase(line, words->desc);
    AppendPhrase(line, words->name);
    AppendPhrase(line, words->mission);
    AppendPhrase(line, words->time);
    return line.empty() ? slot.acceptLabel : line;
}

std::unique_ptr<GraphRender> BuildSaveLoad()
{
    gFocus.clear();

    GameUI::SaveLoadView view = GameUI::ReadSaveLoad(gScreen);

    GraphBuilder b;
    b.BeginStop("saves");

    // While a save or load is in flight the screen drops every key, and on a slow load the
    // silence is the thing players report. One node saying so is the honest readout — and the
    // differ speaks it, because the focused identity has changed.
    if (view.busy) {
        b.AddLabel(ControlId::Structural("saveload/busy"),
            []() { return std::string(Vocab::SaveLoadBusy); });
        return b.Build();
    }

    if (view.slots.empty()) {
        // A list that is genuinely empty (no saves yet) is what a sighted player sees too.
        b.AddLabel(ControlId::Structural("saveload/empty"),
            []() { return std::string(Vocab::SaveLoadEmpty); });
        return b.Build();
    }

    const SaveWordMap& words = FetchSaveWords(view.slots);

    for (const GameUI::SaveSlot& slot : view.slots) {
        if (!slot.handle)
            continue;
        auto found = words.find(slot.index);
        const std::string label
            = SaveSlotLabel(slot, found == words.end() ? nullptr : &found->second);
        if (label.empty())
            continue;

        ControlId id = ControlId::Referenced(slot.handle, "saveload/" + std::to_string(slot.index));
        const int index = slot.index;
        gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false,
            [index]() { GameUI::SetSaveLoadSelection(gScreen, index); }};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (slot.acceptDisabled || !slot.accept) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        } else {
            // Re-read the screen at activation: the handler runs from the input drain, a frame
            // or more after this render, and a delete rebuilds every row. Matching on the row's
            // own Index is what makes that safe — an array position would have shifted.
            vt.OnActivate = [index]() {
                GameUI::SaveLoadView live = GameUI::ReadSaveLoad(gScreen);
                for (const GameUI::SaveSlot& s : live.slots) {
                    if (s.index == index) {
                        GameUI::ActivateSaveSlot(gScreen, s);
                        return;
                    }
                }
            };
        }
        b.AddItem(std::move(id), std::move(vt));
    }
    return b.Build();
}

void* ProbeSaveLoad()
{
    void* screen = GameUI::FindSaveLoadScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string SaveLoadName()
{
    GameUI::SaveLoadView view = GameUI::ReadSaveLoad(gScreen);
    std::string name = view.title;
    if (name.empty())
        name = view.saving ? Vocab::ScreenSaveFallback : Vocab::ScreenLoadFallback;

    // The delete verb, named ONCE per screen entry, in the row's own localized word. A sighted
    // player reads it off the highlighted row's second button; this layer does not put that
    // button in the graph (see the section note), so this is where the affordance lives. The
    // game's own nav help offers nothing but Back on mouse+keyboard.
    for (const GameUI::SaveSlot& slot : view.slots) {
        if (slot.remove && !slot.deleteLabel.empty()) {
            name += ". " + slot.deleteLabel + ": " + Vocab::KeyBackspace;
            break;
        }
    }
    return name;
}

// ---- inside a room on the Avenger (UIFacility + 22 subclasses) ----
//
// A RECIPE rather than a narrate-only reader, and the contrast with the ant farm next door is
// the reason why. The ant farm keeps the game's own navigation because taking it would destroy
// information we cannot rebuild: UIFacilityGrid_FacilityOverlay sets the room NAME inside
// `if( bIsFocused )`, so a focus model of ours would leave every room nameless (measured live
// 2026-08-21 — of nineteen overlays, exactly one carried a Label, the focused one), and the
// camera swoop and highlight box come from the same navigator. None of that is true in here.
// Driving the submenu ourselves costs nothing and buys the three things only a recipe gives:
// a stop on the crew line, which the game draws but makes unfocusable; a stable reading order;
// and focus tracking on a screen where the game's own is invisible on mouse+keyboard
// (UIStaffSlot.OnReceiveFocus sends its highlight op only under `ISCONTROLLERACTIVE).
//
// ⛔ The nodes come from TWO screens. The room screen is on top; its menu belongs to
// UIAvengerHUD underneath. That is the game's own arrangement, not a layering problem to route
// around — see the RoomView comment in game_ui.h.

constexpr const char* kFacilityReaderClass = "X2AccessFacilityReader";

// Cached on the button pointers, and this one is not merely an optimisation: the frame differ
// rebuilds the graph on every poll, so an uncached fetch would make an outbound ProcessEvent ten
// times a second for a set of labels that CANNOT change — AddFacilityButton runs once, in
// InitScreen, and nothing ever relabels a UIFacility_RoomFunc. A changed pointer list means a
// different room, which is the only thing that can change the answer.
struct FacilityWords {
    std::vector<WireRecord> records;
    std::vector<void*> key;
};

FacilityWords gFacilityWords;

const std::vector<WireRecord>& FetchFacilityWords(const GameUI::RoomView& view)
{
    std::vector<void*> key;
    key.reserve(view.funcs.size() + 1);
    for (const GameUI::RoomFunc& f : view.funcs)
        key.push_back(f.handle);
    key.push_back(view.facility);

    if (key != gFacilityWords.key) {
        gFacilityWords.records = FetchCompanionRecords(kFacilityReaderClass, "DescribeFacility");
        gFacilityWords.key = std::move(key);
    }
    return gFacilityWords.records;
}

// The room's own bottom-bar buttons, and the upgrade button beside them. Split out of BuildRoom
// because it is the half that needs the companion: a UIFacility_RoomFunc retains no text at all.
void AddRoomFunctions(GraphBuilder& b, const GameUI::RoomView& view)
{
    if (view.funcs.empty() && !view.upgrade)
        return;

    b.BeginStop("room");

    if (!view.funcs.empty()) {
        const std::vector<WireRecord>& words = FetchFacilityWords(view);

        for (const GameUI::RoomFunc& f : view.funcs) {
            std::string label;
            for (const WireRecord& rec : words) {
                if (RecField(rec, "kind") == "func"
                    && atoi(RecField(rec, "index").c_str()) == f.index) {
                    label = RecField(rec, "label");
                    break;
                }
            }
            // Only a facility no shipped class covers reaches this — a mod's second button. Say
            // something placeable rather than nothing: an unnamed node the player can still
            // press beats a node that is not there.
            if (label.empty())
                label = Vocab::RoomFunctionFallback;

            ControlId id = ControlId::Referenced(f.handle, "room/func/" + std::to_string(f.index));
            const int index = f.index;
            // Focus the button itself. The shortcut list underneath keeps its own selection —
            // deliberately not cleared, because it belongs to a different screen and the game
            // leaves it alone here too.
            gFocus[id.StructuralKey] = FocusTarget{-1, f.handle, false, nullptr};

            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            vt.OnActivate = [index]() {
                GameUI::ActivateRoomFunc(GameUI::ReadRoom(gScreen), index);
            };
            b.AddItem(std::move(id), std::move(vt));
        }
    }

    if (view.upgrade) {
        ControlId id = ControlId::Referenced(view.upgrade, "room/upgrade");
        void* handle = view.upgrade;
        gFocus[id.StructuralKey] = FocusTarget{-1, handle, false, nullptr};

        NodeVtable vt;
        vt.Type = &ButtonType();
        // UIButton::Text is retained here — ShowUpgradeButton goes through SetText — so unlike
        // the bar buttons this one needs no companion. It already reads "УЛУЧШИТЬ" or
        // "ПРОСМОТРЕТЬ УЛУЧШЕНИЯ" depending on which of the three states the facility is in,
        // which is the whole distinction a sighted player gets from it.
        vt.Announcements.emplace_back(NodeAnnouncement::Static(view.upgradeLabel));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (view.upgradeDisabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        } else {
            vt.OnActivate = [handle]() { GameUI::ClickButton(handle); };
        }
        b.AddItem(std::move(id), std::move(vt));
    }
}

std::unique_ptr<GraphRender> BuildRoom()
{
    gFocus.clear();

    GameUI::RoomView view = GameUI::ReadRoom(gScreen);

    GraphBuilder b;

    // The crew line first, and as its own stop: "SCIENTISTS: 0" is drawn in the room and is
    // reachable by no keyboard in the game. It is also the one number that decides whether the
    // room's verbs will do anything — a lab with no scientists researches nothing — so it reads
    // before the actions rather than after them.
    if (!view.staffTitle.empty() && !view.staffSkill.empty()) {
        b.BeginStop("crew");
        const std::string crew = view.staffTitle + Vocab::RoomCrewSeparator + view.staffSkill;
        b.AddLabel(ControlId::Structural("room/crew"), [crew]() { return crew; });
    }

    // The room's OWN controls before the HUD's submenu, because that is the order the game
    // itself reads them in: UIFacility.OnUnrealCommand offers the keys to the room screen first
    // and only lets them fall through to UIAvengerShortcuts when the room handles nothing. A
    // room with a bottom bar therefore consumes the arrows here, and a player arriving in the
    // Bar should land on the button the Bar actually has.
    AddRoomFunctions(b, view);

    if (view.actions.empty()) {
        if (view.funcs.empty() && !view.upgrade) {
            // Now genuinely nothing, and still not a fault: this is also the state while the
            // submenu is being rebuilt. ⛔ It used to be reached by the Bar and the Living
            // Quarters as well, whose shortcut category really is -1 — but those rooms DO have a
            // bottom-bar button, so "nothing to do here" was a lie about a screen with a visible
            // control on it (reported live 2026-08-21). The empty case has to mean empty.
            b.BeginStop("actions");
            b.AddLabel(ControlId::Structural("room/empty"), []() {
                return std::string(Vocab::RoomNoActions) + ". " + Vocab::RoomLeaveHint;
            });
        }
        return b.Build();
    }

    b.BeginStop("actions");

    for (const GameUI::RoomAction& action : view.actions) {
        ControlId id
            = ControlId::Referenced(action.handle, "room/" + std::to_string(action.index));
        const int index = action.index;
        gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false, nullptr};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(action.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;

        if (action.disabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        } else {
            // Re-read at activation rather than closing over the row pointer: the handler runs
            // from the input drain, a frame or more after this render, and DoShowList calls
            // List.ClearItems() and respawns every row whenever the category changes. Matching
            // on the index — which is the game's own, padding skipped but not renumbered — is
            // what makes that safe.
            vt.OnActivate = [index]() {
                GameUI::RoomView live = GameUI::ReadRoom(gScreen);
                for (const GameUI::RoomAction& a : live.actions) {
                    if (a.index == index) {
                        GameUI::ActivateRoomAction(live, a);
                        return;
                    }
                }
            };
        }

        // The game colours a row red or amber off Urgency, which is a whole channel of meaning
        // speech loses by default. Reported as the "!" badge kind, so it orders with the ant
        // farm's attention word rather than inventing a second vocabulary for the same idea.
        if (action.urgent || action.warning) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(
                action.urgent ? Vocab::RoomUrgent : Vocab::RoomWarning));
            vt.Announcements.back().Kind = kKindAttention;
        }

        b.AddItem(std::move(id), std::move(vt));
    }
    return b.Build();
}

void* ProbeRoom()
{
    void* screen = GameUI::FindRoomScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string RoomName()
{
    GameUI::RoomView view = GameUI::ReadRoom(gScreen);
    // RoomField is what the game itself prints in the header, and it is the ONLY name worth
    // using: the room templates' own DataNames are systematically wrong here — the template
    // called PowerCore is the Research lab, Hangar is the Armory and Storage is Engineering.
    // Same trap as the ant farm's, one layer up. Fall back to the category label, which is the
    // same room named by the shortcut bar, before ever reaching a word of ours.
    std::string name = view.room;
    if (name.empty())
        name = view.category;
    if (name.empty())
        name = Vocab::ScreenRoomFallback;
    return name;
}

// P11 write-back for the submenu. Driving the game's own UIList.SetSelectedIndex is what keeps
// its highlight, its scroll position and its OnSelectionChanged delegates in step with our
// focus — verified live 2026-08-21 to do all three.
bool SetRoomListIndex(void* screen, int index)
{
    GameUI::RoomView view = GameUI::ReadRoom(screen);
    return GameUI::SetRoomActionSelection(view, index);
}

// Above the ant farm it is entered from, below anything the room's own verbs push over it
// (choose-research, the facility summary, a dialog) — which is also why ProbeRoom insists the
// facility screen is TOPMOST rather than merely present.
const Recipe kRoomRecipe{"avenger room", ProbeRoom, BuildRoom, RoomName, SetRoomListIndex};

// ---- an un-excavated room (UIRoom): assign an engineer, clear the debris ----
//
// The strongest case in the strategy layer for a recipe rather than narration, because vanilla
// leaves this screen not merely unspoken but UNOPERABLE from the keyboard: ExcavateButton and
// ExcavationPanel both call DisableNavigation(), the staff slots answer only to controller
// D-pad constants, and UIRoom.OnAccept — the thing Enter reaches — is an empty function body.
// See the RoomBuildView comment in game_ui.h for the full list.
//
// TWO SHAPES, one recipe. While the personnel picker is open the game routes every key into it
// (UIStaffContainer.OnUnrealCommand tests m_kPersonnelDropDown.bIsVisible before anything else),
// so the graph becomes candidate-only rather than showing the room's stops behind a modal the
// player is already inside. That mirrors the game's own input routing instead of inventing a
// parallel one, which is the same discipline the tactical gate follows.

constexpr const char* kRoomReaderClass = "X2AccessRoomReader";

// Words for the room screen, keyed by the widget pointers they describe. Same self-invalidating
// cache as the save rows: the slot widgets are respawned by RealizeStaffSlots and the picker's
// rows by UpdateList, so a changed pointer set is an exact cache key — and the outbound call is
// the cost here, not the payload.
struct RoomWords {
    std::vector<WireRecord> records;
    std::vector<void*> key;
    bool dropOpen = false;
};

RoomWords gRoomWords;

const std::vector<WireRecord>& FetchRoomWords(const GameUI::RoomBuildView& view)
{
    std::vector<void*> key = view.slots;
    key.push_back(view.excavate);
    key.push_back(view.dropDown);
    const bool dropOpen = view.dropDown != nullptr;

    // Re-ask whenever the widget set changes OR the picker opens/closes. ⛔ Also re-ask every
    // time the picker is OPEN and nothing else moved: assigning a staffer rewrites the slot's
    // occupant and the status panel's whole state machine ("engineer required" → "ready") without
    // respawning a single widget, so a pointer-only cache key would speak the pre-assignment
    // room back at the player.
    if (key != gRoomWords.key || dropOpen != gRoomWords.dropOpen || dropOpen) {
        gRoomWords.records = FetchCompanionRecords(kRoomReaderClass, "DescribeRoom");
        gRoomWords.key = std::move(key);
        gRoomWords.dropOpen = dropOpen;
    }
    return gRoomWords.records;
}

std::unique_ptr<GraphRender> BuildRoomBuild()
{
    gFocus.clear();

    GameUI::RoomBuildView view = GameUI::ReadRoomBuild(gScreen);
    const std::vector<WireRecord>& records = FetchRoomWords(view);

    GraphBuilder b;

    // --- shape 2: the picker owns the keys ---
    if (view.dropDown) {
        b.BeginStop("staff");
        int shown = 0;
        for (const WireRecord& rec : records) {
            if (RecField(rec, "kind") != "candidate")
                continue;
            const int index = atoi(RecField(rec, "index").c_str());
            std::string label = RecField(rec, "name");
            const std::string status = RecField(rec, "status");
            if (label.empty())
                continue;
            AppendPhrase(label, status);

            ControlId id = ControlId::Structural("roompick/" + std::to_string(index));
            gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false, nullptr};

            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            vt.OnActivate = [index]() {
                GameUI::RoomBuildView live = GameUI::ReadRoomBuild(gScreen);
                GameUI::ChooseStaffCandidate(live, index);
            };
            b.AddItem(std::move(id), std::move(vt));
            shown++;
        }
        if (shown == 0) {
            // The picker opens even with nothing to put in it — IsUnitAvailableForThisSlot is
            // what greys the SLOT, not what closes the list. Saying so beats an empty graph,
            // which would read as the layer having failed.
            b.AddLabel(ControlId::Structural("roompick/empty"),
                []() { return std::string(Vocab::RoomNoStaff); });
        }
        return b.Build();
    }

    // --- shape 1: the room itself ---
    for (const WireRecord& rec : records) {
        const std::string kind = RecField(rec, "kind");

        if (kind == "status") {
            // The status panel, which no keyboard in the game can focus. Header, state, time and
            // reward as one stop: they are one paragraph on screen and a sighted player takes
            // them in together, so splitting them into four stops would be worse than the game.
            b.BeginStop("status");
            std::string line;
            for (const char* field : {"title", "time", "rewardLabel", "reward"})
                AppendPhrase(line, RecField(rec, field));
            if (!line.empty()) {
                b.AddLabel(ControlId::Structural("room/status"), [line]() { return line; });
            }
            continue;
        }

        if (kind == "slot") {
            b.BeginStop("slot");
            const int index = atoi(RecField(rec, "index").c_str());
            std::string label = RecField(rec, "label");
            AppendPhrase(label, RecField(rec, "bonus"));

            ControlId id = ControlId::Structural("room/slot/" + std::to_string(index));

            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;

            // "No one is available" is NOT the same as "you cannot press this": the game greys
            // the slot but still opens its picker, and the picker is where a filled slot is
            // emptied. So this is spoken as a Reason and the node stays activatable.
            if (RecField(rec, "available") == "0" && RecField(rec, "filled") == "0") {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::RoomNoStaff));
                vt.Announcements.back().Kind = AnnouncementKinds::Reason;
            }

            const size_t slotPos = static_cast<size_t>(index);
            vt.OnActivate = [slotPos]() {
                GameUI::RoomBuildView live = GameUI::ReadRoomBuild(gScreen);
                if (slotPos < live.slots.size())
                    GameUI::OpenStaffSlotPicker(live.slots[slotPos]);
            };
            b.AddItem(std::move(id), std::move(vt));
            continue;
        }

        if (kind == "excavate") {
            b.BeginStop("excavate");
            const std::string label = RecField(rec, "label");
            const bool enabled = RecField(rec, "enabled") == "1";

            ControlId id = ControlId::Structural("room/excavate");
            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;

            if (!enabled) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
                vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
                const std::string reason = RecField(rec, "reason");
                if (!reason.empty()) {
                    vt.Announcements.emplace_back(NodeAnnouncement::Static(reason));
                    vt.Announcements.back().Kind = AnnouncementKinds::Reason;
                }
            } else {
                vt.OnActivate = []() {
                    GameUI::RoomBuildView live = GameUI::ReadRoomBuild(gScreen);
                    GameUI::StartRoomExcavation(live);
                };
            }
            b.AddItem(std::move(id), std::move(vt));
        }
    }
    return b.Build();
}

void* ProbeRoomBuild()
{
    void* screen = GameUI::FindRoomBuildScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string RoomBuildName()
{
    GameUI::RoomBuildView view = GameUI::ReadRoomBuild(gScreen);

    // ⛔ NOT UIAvengerHUD.FacilityHeader. UIRoom never writes that header — only
    // UIFacility.RealizeFacility does — so it still holds the previous screen's title, and live
    // 2026-08-21 entering a debris room announced "РЕЗУЛЬТАТЫ ЗАДАНИЯ" (MISSION RESULTS) left
    // over from the post-mission screen. The companion names the room off the room STATE, using
    // the grid overlay's own three branches, so both surfaces call it the same thing.
    std::string name;
    for (const WireRecord& rec : FetchRoomWords(view)) {
        if (RecField(rec, "kind") == "status") {
            name = RecField(rec, "room");
            break;
        }
    }
    if (name.empty())
        name = Vocab::ScreenRoomFallback;
    if (view.dropDown)
        AppendPhrase(name, Vocab::RoomPickStaff);
    return name;
}

// P11 write-back, and it only means anything while the picker is open — the room's own stops are
// panels and buttons the game never selects, which is exactly why they needed a recipe.
bool SetRoomBuildListIndex(void* screen, int index)
{
    GameUI::RoomBuildView view = GameUI::ReadRoomBuild(screen);
    return GameUI::SetStaffPickerSelection(view, index);
}

const Recipe kRoomBuildRecipe{
    "avenger debris room", ProbeRoomBuild, BuildRoomBuild, RoomBuildName, SetRoomBuildListIndex};

// ---- the commodity list (UIInventory and its dozen subclasses) ----
//
// The widest single recipe in the project: choose research, choose project, choose a psi
// ability, view objectives, the three archives, build items, storage, implants, the XCOM
// database, the black market's buy side, resistance goods and the officer school are all the
// same screen — a UIList of UIInventory_ListItems with a UIItemCard beside it.
//
// Unlike the debris room this screen is already OPERABLE from the keyboard: the list navigates,
// and Enter on a row buys or starts the thing. What it is not is READABLE, and it fails twice
// over. UIInventory_ListItem.PopulateData bypasses UIListItemString.SetText, so the row is
// showing a name it does not retain; and the CARD — cost, duration, requirements, the
// description, everything that decides which tech to pick — retains nothing at all AND is in no
// navigator, so no keyboard in the game has ever been able to reach it.
//
// THREE STOPS, in the screen's own top-to-bottom order: the category strip (Build Items only),
// the list, then the card. The card is a separate stop rather than part of each row on purpose:
// a research list is fifteen entries long and a card is a paragraph, so folding one into the
// other would make every arrow press a paragraph. Within the card, one node per LINE — cost,
// time, requirements, description — because speech is serial and "what does it cost" should not
// require sitting through the write-up.

constexpr const char* kCommodityReaderClass = "X2AccessCommodityReader";

// Same self-invalidating cache as the save rows and the debris room, with one addition: the
// SELECTION is part of the key. The card is the selection's shadow — UIInventory sets
// List.OnSelectionChanged = SelectedItemChanged in BuildScreen, and that delegate repopulates
// the card on every move — so a pointer-only key would describe the row the player just left.
struct CommodityWords {
    std::vector<WireRecord> records;
    std::vector<void*> key;
    int selected = -2;
};

CommodityWords gCommodityWords;

const std::vector<WireRecord>& FetchCommodityWords(const GameUI::CommodityView& view)
{
    std::vector<void*> key;
    key.reserve(view.rows.size() + 1);
    for (const GameUI::CommodityRow& row : view.rows)
        key.push_back(row.handle);
    // Switching a Build Items tab is covered by the row pointers alone: SwitchTab calls
    // PopulateData, which runs List.ClearItems() and spawns a fresh panel per item, so a
    // category change replaces every pointer in this key. The screen pointer goes in too, so a
    // second screen of the family can never inherit a warm cache from the one it replaced.
    key.push_back(view.screen);

    if (key != gCommodityWords.key || view.selected != gCommodityWords.selected) {
        gCommodityWords.records
            = FetchCompanionRecords(kCommodityReaderClass, "DescribeCommodity");
        gCommodityWords.key = std::move(key);
        gCommodityWords.selected = view.selected;
    }
    return gCommodityWords.records;
}

const WireRecord* FindRecord(const std::vector<WireRecord>& records, const char* kind)
{
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == kind)
            return &rec;
    }
    return nullptr;
}

// The category strip, and it is the clearest small case of "reachable but unspeakable" in the
// strategy layer. Left/Right already switch these tabs for a sighted keyboard player
// (UIInventory_BuildItems.OnUnrealCommand handles the arrow constants at screen level), but
// CreateTabButton calls DisableNavigation() on every button so none can be focused, and the new
// category's NAME goes into Flash through UIInventory.SetCategory, which retains nothing. So a
// blind player could switch tabs all day and never learn which one they were on.
void BuildCommodityTabs(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    bool any = false;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "tab")
            continue;
        const std::string label = RecField(rec, "label");
        if (label.empty())
            continue;
        if (!any) {
            b.BeginStop("tabs");
            b.StartRow();
            any = true;
        }
        const int index = atoi(RecField(rec, "index").c_str());
        const bool selected = RecField(rec, "selected") == "1";
        const bool disabled = RecField(rec, "disabled") == "1";

        ControlId id = ControlId::Structural("commodity/tab/" + std::to_string(index));
        // Deliberately NOT in gFocus. The game's own focus lives on the list and the card is
        // fed from it; pushing focus at a tab button that called DisableNavigation() would ask
        // the navigator for something it refuses to keep.
        NodeVtable vt;
        vt.Type = &TabType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement(
            [selected]() { return selected ? std::string(Vocab::Selected) : std::string(); },
            false, AnnouncementKinds::Selected));
        if (disabled) {
            // An empty category. The game greys the button and SwitchTab still switches to it,
            // showing an empty list — so this is a Reason, not a refusal.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        vt.OnActivate = [index]() {
            GameUI::SwitchCommodityTab(GameUI::ReadCommodity(gScreen), index);
        };
        // Post-activation feedback, read LIVE rather than from this render's closure. A captured
        // flag can only ever describe the state before the press — which spoke a bare "selected"
        // when the player pressed the tab they were already on, and nothing at all when they
        // actually switched. SwitchTab respawns every row, so the cache key has already changed
        // by the time this runs and the re-ask is the fresh answer.
        vt.StateText = [index]() {
            const std::vector<WireRecord>& fresh
                = FetchCommodityWords(GameUI::ReadCommodity(gScreen));
            for (const WireRecord& rec : fresh) {
                if (RecField(rec, "kind") != "tab"
                    || atoi(RecField(rec, "index").c_str()) != index)
                    continue;
                std::string line = RecField(rec, "label");
                if (RecField(rec, "selected") == "1")
                    AppendPhrase(line, Vocab::Selected);
                return line;
            }
            return std::string();
        };
        b.AddItem(std::move(id), std::move(vt));
    }
    if (any)
        b.EndRow();
}

// One line of the card: the game's own caption joined to its value. AppendPhrase does the join,
// so a caption the game already ends in a colon — "СТОИМОСТЬ:" is that shape — does not also
// collect a comma.
std::string ComposeCardLine(const WireRecord& card, const char* labelKey, const char* valueKey)
{
    const std::string value = RecField(card, valueKey);
    if (value.empty() || !labelKey)
        return value;
    std::string line = RecField(card, labelKey);
    if (line.empty())
        return value;
    AppendPhrase(line, value);
    return line;
}

// The card, one node per line the card itself draws.
void BuildCommodityCard(GraphBuilder& b, const WireRecord& card)
{
    struct Line {
        const char* label;
        const char* value;
    };
    // Order is the card's own: the title heads it, the cost block sits under the image, and the
    // description is last because it is the longest and the least decisive.
    const Line lines[] = {
        {nullptr, "title"},
        {"costLabel", "cost"},
        {"timeLabel", "time"},
        {nullptr, "req"},
        {nullptr, "desc"},
    };

    bool any = false;
    for (const Line& line : lines) {
        if (RecField(card, line.value).empty())
            continue; // the card is not drawing this line at all
        if (!any) {
            b.BeginStop("details");
            any = true;
        }

        // Read at ANNOUNCE time, not at build time. The card is repopulated by the game the
        // instant our write-back moves the list selection, whereas this graph is up to one poll
        // old — so a Tab pressed hard on the heels of an arrow would otherwise describe the row
        // the player just left. The build-time text stays as the fallback, because a node that
        // has already earned a stop must never go silent.
        std::string fallback = ComposeCardLine(card, line.label, line.value);
        b.AddLabel(ControlId::Structural(std::string("commodity/card/") + line.value),
            [labelKey = std::string(line.label ? line.label : ""),
                valueKey = std::string(line.value), fallback]() {
                const std::vector<WireRecord>& fresh
                    = FetchCommodityWords(GameUI::ReadCommodity(gScreen));
                const WireRecord* live = FindRecord(fresh, "card");
                if (!live)
                    return fallback;
                std::string text = ComposeCardLine(
                    *live, labelKey.empty() ? nullptr : labelKey.c_str(), valueKey.c_str());
                return text.empty() ? fallback : text;
            });
    }
}

std::unique_ptr<GraphRender> BuildCommodity()
{
    gFocus.clear();

    GameUI::CommodityView view = GameUI::ReadCommodity(gScreen);
    const std::vector<WireRecord>& records = FetchCommodityWords(view);

    GraphBuilder b;

    BuildCommodityTabs(b, records);

    b.BeginStop("items");
    int shown = 0;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "row")
            continue;
        const int index = atoi(RecField(rec, "index").c_str());
        std::string label = RecField(rec, "name");
        if (label.empty())
            continue;

        // The right-hand quantity column, spoken only where the row draws one. `label` is the
        // game's own item name, so the join is AppendPhrase's — never a bare comma.
        const std::string qty = RecField(rec, "qty");
        if (!qty.empty())
            AppendPhrase(label, qty + " " + Vocab::CommodityOwned);

        // Native's flags and the companion's words are matched on the GAME's list index, which
        // both halves stamp independently — so a row either half skipped cannot silently shift
        // every entry after it.
        const GameUI::CommodityRow* state = nullptr;
        for (const GameUI::CommodityRow& r : view.rows) {
            if (r.index == index) {
                state = &r;
                break;
            }
        }

        ControlId id = ControlId::Structural("commodity/row/" + std::to_string(index));
        gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(std::move(label)));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;

        if (state) {
            // The row's colour, which is the whole affordability story and is lost to speech by
            // default. bDisabled is "requirements unmet, or already bought"; bIsBad is "you
            // cannot afford it" — and the two are independent, so an unaffordable AND
            // unavailable row says both, exactly as it is drawn.
            if (state->disabled) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
                vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            }
            if (state->bad) {
                vt.Announcements.emplace_back(
                    NodeAnnouncement::Static(Vocab::CommodityUnaffordable));
                vt.Announcements.back().Kind = AnnouncementKinds::Reason;
            }
            if (state->warning) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::RoomWarning));
                vt.Announcements.back().Kind = kKindAttention;
            }
        }

        // Activation is offered even on a disabled row, unlike the room menu's. These screens
        // disagree about what disabled means — UIInventory_Implants greys an implant it will
        // still let you look at — so the refusal stays the game's, where it can be right.
        vt.OnActivate = [index]() {
            GameUI::CommodityView live = GameUI::ReadCommodity(gScreen);
            for (const GameUI::CommodityRow& r : live.rows) {
                if (r.index == index) {
                    GameUI::ActivateCommodityRow(live, r);
                    return;
                }
            }
        };
        b.AddItem(std::move(id), std::move(vt));
        shown++;
    }

    if (shown == 0) {
        // A legitimate state, not a fault: an archive with nothing in it yet, a build list with
        // no unlocked items, an empty category. The screen says so in its own subtitle (
        // PopulateData swaps m_strEmptyListTitle in), which the recipe's name already spoke —
        // so this node's job is only to give the player something to stand on and the way out.
        b.AddLabel(ControlId::Structural("commodity/empty"), []() {
            return std::string(Vocab::CommodityEmpty) + ". " + Vocab::CommodityLeaveHint;
        });
    }

    if (const WireRecord* card = FindRecord(records, "card"))
        BuildCommodityCard(b, *card);

    return b.Build();
}

void* ProbeCommodity()
{
    void* screen = GameUI::FindCommodityScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string CommodityName()
{
    // The header the screen itself is drawing. UIX2PanelHeader retains both strings under a
    // dirty check right before the Flash op, so this is what is on screen — including the
    // EMPTY-LIST subtitle, which PopulateData swaps in when the list came back with nothing.
    const std::vector<WireRecord>& records = FetchCommodityWords(GameUI::ReadCommodity(gScreen));
    std::string name;
    if (const WireRecord* header = FindRecord(records, "header")) {
        name = RecField(*header, "title");
        AppendPhrase(name, RecField(*header, "subtitle"));
    }
    if (name.empty())
        name = Vocab::ScreenCommodityFallback;
    return name;
}

// P11 write-back. Only the row nodes carry a listIndex — the tab strip and the card
// deliberately stay out of gFocus, so this is never called for them.
bool SetCommodityListIndex(void* screen, int index)
{
    return GameUI::SetCommoditySelection(GameUI::ReadCommodity(screen), index);
}

// Below anything its own rows push (the build-item confirmation, the black market's dialog) and
// below the pause menu, but above the room screen it is opened from — the Research lab's
// UIFacility is still on the stack underneath and probes ready.
const Recipe kCommodityRecipe{
    "commodity list", ProbeCommodity, BuildCommodity, CommodityName, SetCommodityListIndex};

// ---- the personnel roster (UIPersonnel and its ten subclasses) ----
//
// One recipe for every door the game opens when it asks "which person?" — the Living Quarters'
// roster, the Bar's obituaries, the Armory's soldiers, staffing a facility, choosing a scientist,
// a covert action's squad. The subclasses swap which array is filled and which tabs exist; the
// layout, the rows and the input path all belong to UIPersonnel.
//
// This screen is already OPERABLE from a keyboard — Up/Down walk the list, Left/Right switch
// tabs, Enter chooses, Escape backs out — so, like save/load, the recipe is a READER first. What
// it adds is the three things the keyboard cannot get to:
//
//   * The ROWS' words. UIPersonnel_ListItem extends UIButton and then bypasses SetText, so the
//     retained Text is empty on a row visibly showing a soldier — the companion re-derives every
//     column from the UnitRef the row DID keep.
//   * The TAB STRIP's voice. Left/Right already switch tabs; CreateTabButton calls
//     DisableNavigation() and SwitchTab announces the change only into Flash, so a blind player
//     could switch all day and never learn which tab they were on.
//   * SORTING, which on mouse+keyboard is CLICK-ONLY: the keyboard branches for it are
//     FXS_BUTTON_X and FXS_DPAD_LEFT/RIGHT, all three controller constants. On a roster of twenty
//     soldiers the sort column IS the reading order, which makes it the one verb here worth
//     re-providing rather than merely narrating.
//
// STOPS in the screen's own top-to-bottom order — tabs, sort header, list — but focus STARTS on
// the list, because that is where the game itself puts it (UpdateList ends with
// Navigator.SetSelected(m_kList)). Reading order and landing spot are different questions, and
// answering them separately is what keeps a player from Tabbing twice to reach the thing they
// opened the screen for.

constexpr const char* kPersonnelReaderClass = "X2AccessPersonnelReader";

// Same self-invalidating cache as the commodity list, and for the same two reasons: the row
// pointers change wholesale whenever the list is rebuilt (UpdateList calls ClearItems and
// PopulateListInstantly respawns every item, which is what a tab switch and every sort do), and
// the screen pointer keeps a second roster from inheriting the first one's words.
//
// ⛔ The SELECTION is deliberately NOT part of the key, unlike the commodity list's. Nothing on
// this screen is the selection's shadow — there is no card — so re-asking on every arrow press
// would walk the whole roster through XCOMHISTORY once per keystroke for no new words.
struct PersonnelWords {
    std::vector<WireRecord> records;
    std::vector<void*> key;
};

PersonnelWords gPersonnelWords;

const std::vector<WireRecord>& FetchPersonnelWords(const GameUI::PersonnelView& view)
{
    std::vector<void*> key;
    key.reserve(view.rows.size() + 1);
    for (const GameUI::PersonnelRow& row : view.rows)
        key.push_back(row.handle);
    key.push_back(view.screen);

    if (key != gPersonnelWords.key) {
        gPersonnelWords.records
            = FetchCompanionRecords(kPersonnelReaderClass, "DescribePersonnel");
        gPersonnelWords.key = std::move(key);
    }
    return gPersonnelWords.records;
}

// Drop the cache so the next read re-asks. Called after a tab switch or a sort, both of which
// respawn the rows anyway — but the pointers can land on the same freed addresses, and a key
// that happens to match would keep the previous tab's words on screen.
void InvalidatePersonnelWords()
{
    gPersonnelWords.key.clear();
}

// One outbound call with a single int, for the two verbs the companion owns. Both resolve the
// screen themselves from the stack, exactly as DescribePersonnel does, so there is nothing to
// pass but the index.
bool CallPersonnelVerb(const char* funcName, int value)
{
    void* cdo = GameUI::FindCompanionCDO(kPersonnelReaderClass);
    if (!cdo)
        return false;
    struct {
        int32_t Value;
    } params{value};
    if (!GameUI::CallScriptFunction(cdo, funcName, &params)) {
        LOG_WARN("MenuNavigator: %s.%s failed", kPersonnelReaderClass, funcName);
        return false;
    }
    InvalidatePersonnelWords();
    return true;
}

void BuildPersonnelTabs(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    bool any = false;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "tab")
            continue;
        const std::string label = RecField(rec, "label");
        if (label.empty())
            continue;
        const int tab = atoi(RecField(rec, "tab").c_str());
        const bool selected = RecField(rec, "selected") == "1";

        if (!any) {
            b.BeginStop("tabs");
            b.StartRow("personnel/tabs");
            any = true;
        }

        // Not in gFocus: every tab button called DisableNavigation(), so asking the navigator to
        // hold one would be asking for something the game refuses to keep.
        ControlId id = ControlId::Structural("personnel/tab/" + std::to_string(tab));
        NodeVtable vt;
        vt.Type = &TabType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement(
            [selected]() { return selected ? std::string(Vocab::Selected) : std::string(); },
            false, AnnouncementKinds::Selected));
        vt.OnActivate = [tab]() { CallPersonnelVerb("SwitchPersonnelTab", tab); };
        // Read LIVE, for the reason the commodity strip earned it: a flag captured at build time
        // can only describe the state before the press.
        vt.StateText = [tab]() {
            const std::vector<WireRecord>& fresh
                = FetchPersonnelWords(GameUI::ReadPersonnel(gScreen));
            for (const WireRecord& rec : fresh) {
                if (RecField(rec, "kind") != "tab" || atoi(RecField(rec, "tab").c_str()) != tab)
                    continue;
                std::string line = RecField(rec, "label");
                if (RecField(rec, "selected") == "1")
                    AppendPhrase(line, Vocab::Selected);
                return line;
            }
            return std::string();
        };
        b.AddItem(std::move(id), std::move(vt));
    }
    if (any)
        b.EndRow();
}

void BuildPersonnelSort(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    bool any = false;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "sort")
            continue;
        const std::string label = RecField(rec, "label");
        if (label.empty())
            continue;
        const int index = atoi(RecField(rec, "index").c_str());
        const bool selected = RecField(rec, "selected") == "1";
        const bool descending = RecField(rec, "desc") == "1";

        if (!any) {
            b.BeginStop("sort");
            b.StartRow("personnel/sort");
            any = true;
        }

        ControlId id = ControlId::Structural("personnel/sort/" + std::to_string(index));
        NodeVtable vt;
        vt.Type = &ButtonType();
        std::string line = std::string(Vocab::PersonnelSortBy) + " " + label;
        if (selected)
            AppendPhrase(line, descending ? Vocab::SortDescending : Vocab::SortAscending);
        vt.Announcements.emplace_back(NodeAnnouncement::Static(line));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The game's own mouse handler, which flips the direction when the column is already the
        // sort column and re-sorts the list either way. Re-read afterwards rather than closing
        // over the flag: pressing this changes exactly the thing the node reports.
        vt.OnActivate = [index]() { CallPersonnelVerb("ClickPersonnelSort", index); };
        vt.StateText = [index]() {
            const std::vector<WireRecord>& fresh
                = FetchPersonnelWords(GameUI::ReadPersonnel(gScreen));
            for (const WireRecord& rec : fresh) {
                if (RecField(rec, "kind") != "sort"
                    || atoi(RecField(rec, "index").c_str()) != index)
                    continue;
                std::string text
                    = std::string(Vocab::PersonnelSortBy) + " " + RecField(rec, "label");
                if (RecField(rec, "selected") == "1") {
                    AppendPhrase(text,
                        RecField(rec, "desc") == "1" ? Vocab::SortDescending
                                                     : Vocab::SortAscending);
                }
                return text;
            }
            return std::string();
        };
        b.AddItem(std::move(id), std::move(vt));
    }
    if (any)
        b.EndRow();
}

// The roster is a TABLE, so it is emitted as one — GraphSheet, not a list of flattened
// sentences. Column 0 is the person's NAME and carries every other cell as extra announcement
// parts, so walking DOWN reads the whole row exactly as a flat list would; stepping RIGHT lands
// on one fact at a time, each announced with the column heading the game draws over it, and
// Up/Down then reads that ONE column down the roster. On twenty soldiers, "who is wounded" stops
// being twenty paragraphs and becomes twenty words.

SheetConfig PersonnelSheetConfig()
{
    return SheetConfig{Vocab::TableBlank, Vocab::RoleTable, &TextType()};
}

std::unique_ptr<GraphRender> BuildPersonnel()
{
    gFocus.clear();

    GameUI::PersonnelView view = GameUI::ReadPersonnel(gScreen);
    const std::vector<WireRecord>& records = FetchPersonnelWords(view);

    GraphBuilder b;

    BuildPersonnelTabs(b, records);
    BuildPersonnelSort(b, records);

    b.BeginStop("list");

    if (view.rows.empty()) {
        // The game's own empty line — "НЕТ ПОГИБШИХ СОЛДАТ" in the memorial, and one per tab
        // elsewhere. It exists on screen (PopulateListInstantly pushes it into Flash, and the
        // Bar spawns a UIText for its own), so this is parity, not a placeholder.
        std::string text;
        if (const WireRecord* empty = FindRecord(records, "empty"))
            text = RecField(*empty, "text");
        if (text.empty())
            text = Vocab::PersonnelEmpty;
        text += ". ";
        text += Vocab::CommodityLeaveHint;
        b.AddLabel(ControlId::Structural("personnel/empty"), [text]() { return text; });
        return b.Build();
    }

    std::vector<std::string> columns;
    if (const WireRecord* cols = FindRecord(records, "cols"))
        columns = RecFieldAll(*cols, "col");

    GraphSheet sheet(b, "personnel/", PersonnelSheetConfig());
    // No region label: the screen is already named by RecipeName, and a second title here would
    // repeat it on every entry.
    sheet.Region("", columns);

    ControlId start;
    bool haveStart = false;
    const int rowCount = static_cast<int>(view.rows.size());
    int position = 0;

    for (const GameUI::PersonnelRow& row : view.rows) {
        const WireRecord* rec = nullptr;
        for (const WireRecord& candidate : records) {
            if (RecField(candidate, "kind") == "row"
                && atoi(RecField(candidate, "index").c_str()) == row.index) {
                rec = &candidate;
                break;
            }
        }

        std::string name = rec ? RecField(*rec, "name") : std::string();
        if (name.empty())
            name = Vocab::SoldierFallback;
        std::vector<std::string> cells
            = rec ? RecFieldAll(*rec, "cell") : std::vector<std::string>();
        const bool promote = rec && !RecField(*rec, "promote").empty();

        const int index = row.index;
        position++;

        NodeVtable primary;
        primary.Type = &ButtonType();
        primary.Announcements.emplace_back(NodeAnnouncement::Static(name));
        primary.Announcements.back().Kind = AnnouncementKinds::Label;
        // The row's other facts, carried on the PRIMARY as extra parts — this is what keeps the
        // flat reading and the tabular one the same graph. Value-kind so they land where a
        // control's value lands rather than after the position count, and stable_sort holds
        // their declaration order, so they read left to right as the row is drawn.
        for (const std::string& cell : cells) {
            if (cell.empty())
                continue;
            primary.Announcements.emplace_back(NodeAnnouncement::Static(cell));
            primary.Announcements.back().Kind = AnnouncementKinds::Value;
        }

        if (promote) {
            // The row draws a badge for a soldier ready to be promoted — a whole affordance
            // carried by an icon, which speech otherwise loses entirely.
            primary.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::PersonnelPromote));
            primary.Announcements.back().Kind = kKindAttention;
        }

        if (row.disabled) {
            // Real, and the game enforces it: every OnXSelected delegate opens by testing
            // IsDisabled and doing nothing. The armory sets it on soldiers away on covert
            // actions; the tutorial sets it on everyone but the one soldier the script wants.
            primary.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            primary.Announcements.back().Kind = AnnouncementKinds::Enabled;
        } else {
            // Re-read at activation: the handler runs from the input drain, a frame or more
            // after this render, and any sort or tab change respawns every row. The index is
            // the game's own, which is what makes matching on it safe.
            primary.OnActivate = [index]() {
                GameUI::ActivatePersonnelRow(GameUI::ReadPersonnel(gScreen), index);
            };
        }

        // A sheet is raw mode underneath, so the builder stamps no automatic positions — and on
        // a roster "4 of 20" is one of the facts a blind player most wants, so it is added
        // explicitly. Position-kind, so the user's per-kind filter still governs it.
        primary.Announcements.emplace_back(
            NodeAnnouncement::Static(Vocab::Position(position, rowCount)));
        primary.Announcements.back().Kind = AnnouncementKinds::Position;

        std::vector<std::function<std::string()>> producers;
        producers.reserve(cells.size());
        for (const std::string& cell : cells) {
            // An empty cell is left NULL rather than blank: a sparse column is not landable, so
            // arrowing across a scientist's row does not stop on the rank they do not have —
            // while the columns AFTER it keep their numbers, which is what lets Up/Down match
            // columns across rows of different shapes.
            if (cell.empty())
                producers.push_back(nullptr);
            else
                producers.push_back([cell]() { return cell; });
        }

        sheet.Row(std::move(primary), row.handle, std::move(producers));

        // The primary's own key, rebuilt the way GraphSheet builds it, so the focus write-back
        // finds it. Every CELL inherits the row's list index too: selecting is what Enter reads
        // back, and a player acting from column 3 must act on the row they are in.
        for (int col = 0; col <= static_cast<int>(cells.size()); col++) {
            std::string key = GraphSheet::RowKey("personnel/", row.handle, col);
            gFocus[key] = FocusTarget{index, nullptr, false, nullptr};
            if (col == 0 && !haveStart && (index == view.selected || position == 1)) {
                start = ControlId::Referenced(row.handle, key);
                haveStart = index == view.selected;
            }
        }
    }
    sheet.Finish();

    // Land where the game itself lands. UpdateList ends with Navigator.SetSelected(m_kList), so
    // the roster — not the tab strip above it — is what a sighted player is already on.
    if (start.IsValid())
        b.SetStart(std::move(start));

    return b.Build();
}

void* ProbePersonnel()
{
    void* screen = GameUI::FindPersonnelScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string PersonnelName()
{
    // UIPersonnel.Title is retained under a dirty check — but SetScreenHeader is called ONLY by
    // RefreshTitle, and only when the screen needs a single tab. With a strip up there is no
    // header at all, and the tab names are the heading a sighted player reads instead, so the
    // selected tab's label stands in for it.
    const std::vector<WireRecord>& records = FetchPersonnelWords(GameUI::ReadPersonnel(gScreen));
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == "tab" && RecField(rec, "selected") == "1")
            return RecField(rec, "label");
    }
    if (const WireRecord* header = FindRecord(records, "header")) {
        const std::string title = RecField(*header, "title");
        if (!title.empty())
            return title;
    }
    return Vocab::ScreenPersonnelFallback;
}

bool SetPersonnelListIndex(void* screen, int index)
{
    return GameUI::SetPersonnelSelection(GameUI::ReadPersonnel(screen), index);
}

// Above every screen it is opened from — the room, the armory, squad select, the debris room's
// build slots — and below any dialog it raises.
const Recipe kPersonnelRecipe{
    "personnel roster", ProbePersonnel, BuildPersonnel, PersonnelName, SetPersonnelListIndex};

// ---- choosing what to build in a room (UIChooseFacility) ----
//
// The screen build mode's Enter opens, and the only way a facility ever gets onto the Avenger. It
// LOOKS like a commodity screen — a list beside a panel of cost, power, time and description —
// but it `extends UIScreen` rather than UIInventory, so the thirteen-screen recipe never saw it
// and it was SILENT (reported live 2026-08-21: "I selected an empty room and the mod went
// silent").
//
// Two stops, in the screen's own left-to-right order: the list, then the panel with one node per
// LINE. Split for the same reason the commodity card is: the description is a paragraph and
// "what does it cost" should not require sitting through it.
//
// ⛔ Native reads the ROWS and the companion reads the PANEL, because the two halves fail in
// opposite directions — PopulateList goes through UIListItemString.SetText (names retained,
// bDisabled retained, attention icon spawned) while UpdateSelection pushes every number into
// Flash and keeps none.

constexpr const char* kBuildFacilityReaderClass = "X2AccessBuildFacilityReader";

// Keyed on the rows AND the selection, like the commodity list's and for the same reason: the
// panel is the selection's shadow (UIList::OnSelectionChanged = UpdateSelection repopulates it on
// every move), so a pointer-only key would describe the facility the player just left.
struct BuildFacilityWords {
    std::vector<WireRecord> records;
    std::vector<void*> key;
    int selected = -2;
};

BuildFacilityWords gBuildFacilityWords;

const std::vector<WireRecord>& FetchBuildFacilityWords(const GameUI::ChooseFacilityView& view)
{
    std::vector<void*> key;
    key.reserve(view.choices.size() + 1);
    for (const GameUI::FacilityChoice& choice : view.choices)
        key.push_back(choice.handle);
    key.push_back(view.screen);

    if (key != gBuildFacilityWords.key || view.selected != gBuildFacilityWords.selected) {
        gBuildFacilityWords.records
            = FetchCompanionRecords(kBuildFacilityReaderClass, "DescribeFacilityChoice");
        gBuildFacilityWords.key = std::move(key);
        gBuildFacilityWords.selected = view.selected;
    }
    return gBuildFacilityWords.records;
}

void BuildFacilityCard(GraphBuilder& b, const WireRecord& card)
{
    struct Line {
        const char* label;
        const char* value;
    };
    // The panel's own order: what it is, what it costs, what it draws, what else it needs, how
    // long, what it costs every month, why you cannot have it, and finally what it does.
    const Line lines[] = {
        {nullptr, "title"},
        {nullptr, "cost"},
        {nullptr, "power"},
        {nullptr, "reqs"},
        {"timeLabel", "time"},
        {nullptr, "upkeep"},
        {nullptr, "warning"},
        {nullptr, "desc"},
    };

    bool any = false;
    for (const Line& line : lines) {
        if (RecField(card, line.value).empty())
            continue; // the panel is not drawing this line
        if (!any) {
            b.BeginStop("details");
            any = true;
        }

        // Read at ANNOUNCE time with the build-time text as fallback: UpdateSelection repopulates
        // the panel the instant our write-back moves the selection, while this graph is up to one
        // poll old — so a Tab pressed hard on the heels of an arrow would describe the facility
        // just left. Same guard the commodity card earned.
        std::string fallback = ComposeCardLine(card, line.label, line.value);
        b.AddLabel(ControlId::Structural(std::string("buildfacility/card/") + line.value),
            [labelKey = std::string(line.label ? line.label : ""),
                valueKey = std::string(line.value), fallback]() {
                const std::vector<WireRecord>& fresh
                    = FetchBuildFacilityWords(GameUI::ReadChooseFacility(gScreen));
                const WireRecord* live = FindRecord(fresh, "card");
                if (!live)
                    return fallback;
                std::string text = ComposeCardLine(
                    *live, labelKey.empty() ? nullptr : labelKey.c_str(), valueKey.c_str());
                return text.empty() ? fallback : text;
            });
    }
}

std::unique_ptr<GraphRender> BuildChooseFacility()
{
    gFocus.clear();

    GameUI::ChooseFacilityView view = GameUI::ReadChooseFacility(gScreen);
    const std::vector<WireRecord>& records = FetchBuildFacilityWords(view);

    GraphBuilder b;
    b.BeginStop("list");
    b.SetRegion("list");

    if (view.choices.empty()) {
        b.AddLabel(ControlId::Structural("buildfacility/empty"), []() {
            return std::string(Vocab::CommodityEmpty) + ". " + Vocab::CommodityLeaveHint;
        });
        return b.Build();
    }

    for (const GameUI::FacilityChoice& choice : view.choices) {
        ControlId id = ControlId::Referenced(
            choice.handle, "buildfacility/" + std::to_string(choice.index));
        const int index = choice.index;
        gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false, nullptr};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(choice.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;

        if (choice.priority) {
            // The game spawns an attention icon for a bPriority facility and sorts it to the
            // front — a nudge carried entirely by a glyph and a position.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Attention));
            vt.Announcements.back().Kind = kKindAttention;
        }
        if (choice.disabled) {
            // CanBuildFacility said no. The REASON is on the card (insufficient power, or the
            // cost line the player cannot meet), which is why this is a flag and not a sentence.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        // Passed through even when disabled: OnAccept re-tests and plays its own refusal, which
        // keeps the refusal where the game can be right about why.
        vt.OnActivate = [index]() {
            GameUI::ActivateChooseFacility(GameUI::ReadChooseFacility(gScreen), index);
        };
        b.AddItem(std::move(id), std::move(vt));
    }

    if (const WireRecord* card = FindRecord(records, "card"))
        BuildFacilityCard(b, *card);

    return b.Build();
}

void* ProbeChooseFacility()
{
    void* screen = GameUI::FindChooseFacilityScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string ChooseFacilityName()
{
    const std::vector<WireRecord>& records
        = FetchBuildFacilityWords(GameUI::ReadChooseFacility(gScreen));
    std::string name;
    if (const WireRecord* header = FindRecord(records, "header")) {
        name = RecField(*header, "title");
        AppendPhrase(name, RecField(*header, "subtitle"));
    }
    if (name.empty())
        name = Vocab::ScreenChooseFacilityFallback;
    return name;
}

bool SetChooseFacilityListIndex(void* screen, int index)
{
    return GameUI::SetChooseFacilitySelection(GameUI::ReadChooseFacility(screen), index);
}

// Above the facility grid and UIBuildFacilities it is opened from, below any dialog it raises.
const Recipe kChooseFacilityRecipe{"choose facility", ProbeChooseFacility, BuildChooseFacility,
    ChooseFacilityName, SetChooseFacilityListIndex};

// Above the pause menu and the shell it is opened from, below any dialog it raises.
const Recipe kSaveLoadRecipe{"save/load", ProbeSaveLoad, BuildSaveLoad, SaveLoadName};

// ---- the geoscape's mission tray (UIStrategyMap) ----
//
// The globe itself is unreachable from the keyboard — pin selection is per-frame and
// ISCONTROLLERACTIVE-gated, so it is not the ant farm's one-call ungate — but the TRAY along the
// bottom is the game's own list of everything actionable, and its navigator already answers the
// arrows on mouse+keyboard. What the mouse-active guard withholds is only the visible half:
// UIStrategyMap_MissionIcon.OnReceiveFocus early-returns, so there are no selection brackets, no
// camera fly-to and no tooltip refresh. This recipe supplies those three and the voice.
//
// Claiming the arrows is deliberate and costs the player nothing they cannot get back: bare
// arrows pan the globe, Ctrl+arrow still does (the leniency rule), and panning actively degrades
// our own data — an off-screen pin stops updating (UpdateVisuals gates on bOnScreen), so sharing
// the axis would leave the reader describing widgets holding class defaults.

// The icon our hover currently sits on, so the next move can hand the one being LEFT its
// matching MOUSE_OUT. The pair is symmetric by design — IN saves the camera location and sets
// bOverrideZoomHide, OUT restores and clears both — so an unmatched IN strands the pin in its
// hover state and overwrites the saved location with a site the player never asked to return to.
//
// Stored as a raw pointer and always re-matched against a freshly read view before use: the
// twenty icons are spawned once and reused, so the pointers are stable across UpdateMissions,
// but a screen teardown collects them and a stale one must never be called into.
void* gGeoHovered = nullptr;

void ClearGeoscapeHover()
{
    if (!gGeoHovered)
        return;
    void* screen = GameUI::FindStrategyMapScreen();
    if (screen) {
        GameUI::StrategyMapView live = GameUI::ReadStrategyMap(screen);
        for (const GameUI::TrayIcon& icon : live.icons) {
            if (icon.handle == gGeoHovered) {
                GameUI::MoveTrayHover(icon, /*entering*/ false);
                break;
            }
        }
    }
    gGeoHovered = nullptr;
}

// eSMS_Flight — the Avenger is in transit and the game itself takes NO input on this screen:
// UIStrategyMap.OnUnrealCommand opens with `if(m_eUIstate == eSMS_Flight) return true;`, above
// even its release check. Attaching there would claim the arrows for a tray whose icons cannot
// be acted on and whose pins are moving under the camera, so the layer stands down for exactly
// as long as the game does. It is also the state the pins are least readable in — UpdateVisuals
// skips UpdateFromGeoscapeEntity entirely while bFlightMode.
constexpr int kStrategyMapFlight = 2;

void* ProbeGeoscape()
{
    void* screen = GameUI::FindStrategyMapScreen();
    if (!screen || !GameUI::IsScreenFocused(screen))
        return nullptr;
    if (GameUI::ReadStrategyMap(screen).uiState == kStrategyMapFlight)
        return nullptr;
    return screen;
}

std::string GeoscapeName()
{
    return Vocab::ScreenGeoscapeFallback;
}

std::unique_ptr<GraphRender> BuildGeoscape()
{
    gFocus.clear();

    GameUI::StrategyMapView view = GameUI::ReadStrategyMap(gScreen);

    GraphBuilder b;
    b.BeginStop("tray");

    if (view.icons.empty()) {
        // Legitimate, not a fault: with no missions live, no scan site available and no faction
        // met, UpdateMissions binds nothing and hides all twenty slots. Naming the way out
        // matters most here, as it does in an empty room — with no nodes to move between, a
        // player has nothing to feel their way with.
        b.AddLabel(ControlId::Structural("geoscape/empty"), []() {
            return std::string(Vocab::GeoscapeTrayEmpty) + ". " + Vocab::GeoscapeLeaveHint;
        });
        return b.Build();
    }

    // A horizontal row, because that is what the tray IS on screen — Left/Right step it, which
    // is also the direction the game's own navigator moves in (HorizontalNavigation is set in
    // InitScreen).
    b.StartRow("tray");

    for (const GameUI::TrayIcon& icon : view.icons) {
        // Keyed by the pin's ObjectID where there is one: the tray is rebuilt whenever the map
        // changes and a slot index is not stable across that, while an ObjectID is stable over
        // the whole campaign history. Placeholders have no entity, so they key by slot.
        ControlId id = ControlId::Referenced(icon.handle,
            "geoscape/" + std::string(icon.entityId ? "e" : "s")
                + std::to_string(icon.entityId ? icon.entityId : icon.slot));

        NodeVtable vt;
        vt.Type = &ButtonType();

        if (icon.placeholder) {
            // The slot of a faction not yet met. The game gives it no tooltip at all, so a
            // sighted player hovering it learns nothing either — the grey Faction_unknown art is
            // the whole message. Parity is therefore "one of the faction slots, and you have not
            // met them", which is exactly what the PIN's generic localized noun plus our clause
            // says. ⛔ Never name WHICH faction: the icon deliberately does not, and the tray
            // slot order (Reaper, Skirmisher, Templar) would leak it.
            const std::string label
                = icon.pinHeader.empty() ? std::string(Vocab::GeoscapeUnmetFaction)
                                         : icon.pinHeader;
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            if (!icon.pinHeader.empty()) {
                vt.Announcements.emplace_back(
                    NodeAnnouncement::Static(Vocab::GeoscapeUnmetFaction));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }
        } else {
            // The game's own hover tooltip, verbatim: Title is the site, Body the detail
            // ("НЕПОВРЕЖДЕННЫЕ СТРОЕНИЯ: АФРИКА" / "Новобранцы: 6 дн. осталось"). Read at
            // ANNOUNCE time rather than closed over, because our own MOUSE_IN refreshes them —
            // it re-fetches the scan site from the history first, so a days-remaining count
            // that has ticked since UpdateMissions last ran is current by the time we speak.
            void* handle = icon.handle;
            // Never silence. A tooltip is spawned lazily by SetMissionIconTooltip, so an icon
            // bound this frame can legitimately have none yet — and a stop that says only
            // "button, 3 of 5" tells a player nothing at all. The pin's own generic localized
            // noun ("RESISTANCE HQ", "POINT OF INTEREST") is the honest last word for that.
            const std::string titleFallback = icon.title.empty() ? icon.pinHeader : icon.title;
            vt.Announcements.emplace_back(
                [handle, titleFallback]() {
                    GameUI::StrategyMapView live = GameUI::ReadStrategyMap(gScreen);
                    for (const GameUI::TrayIcon& t : live.icons)
                        if (t.handle == handle)
                            return t.title.empty() ? titleFallback : t.title;
                    return titleFallback;
                },
                /*live*/ false, AnnouncementKinds::Label);

            const std::string bodyFallback = icon.body;
            if (!bodyFallback.empty()) {
                vt.Announcements.emplace_back(
                    [handle, bodyFallback]() {
                        GameUI::StrategyMapView live = GameUI::ReadStrategyMap(gScreen);
                        for (const GameUI::TrayIcon& t : live.icons)
                            if (t.handle == handle)
                                return t.body.empty() ? bodyFallback : t.body;
                        return bodyFallback;
                    },
                    /*live*/ false, AnnouncementKinds::Value);
            }

            // Send the Avenger there — or make contact, or build the outpost, or start the
            // scan, whichever the game's own priority picks for this pin. Vanilla reaches none
            // of it from the keyboard: FXS_BUTTON_A is the only entry, so this is the layer
            // supplying a route the game already built and never wired to a key.
            //
            // ⚠ It acts on the game's SelectedMapItem, which our focus write-back has already
            // moved onto this icon — so it is re-read here only to confirm the screen is still
            // there, not to identify the target.
            vt.OnActivate = []() {
                GameUI::StrategyMapView live = GameUI::ReadStrategyMap(gScreen);
                GameUI::ActivateTraySelection(live);
            };
        }

        // Focus write-back, two INDEPENDENT halves. Conflating them was the first version's bug.
        //
        //  1. Move the game's own navigator, so SelectedMapItem follows what was just spoken.
        //     This applies to placeholders TOO — measured live, an unmet faction's slot IS in
        //     NavigableControls — and SetTraySelection no-ops on the navIndex < 0 case by itself.
        //  2. Hand the icon the mouse hover, for the camera fly-to and the tooltip refresh. NOT
        //     for a placeholder: OnMouseEvent early-returns on an unmet faction before reaching
        //     any of that, so the game would not move the camera either, and pretending to would
        //     be worse than parity rather than better.
        void* handle = icon.handle;
        gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [handle]() {
            GameUI::StrategyMapView live = GameUI::ReadStrategyMap(gScreen);
            for (const GameUI::TrayIcon& t : live.icons) {
                if (t.handle != handle)
                    continue;
                GameUI::SetTraySelection(live, t.navIndex);
                if (t.placeholder) {
                    ClearGeoscapeHover();
                    return;
                }
                if (gGeoHovered != handle)
                    ClearGeoscapeHover();
                if (GameUI::MoveTrayHover(t, /*entering*/ true))
                    gGeoHovered = handle;
                return;
            }
        }};

        b.AddItem(std::move(id), std::move(vt));
    }

    b.EndRow();

    // ---- stop 2: the HUD's four keyboard-unreachable verbs ----
    //
    // PARITY, which is what earns them a stop at all: Dark Events, Chosen Info and Resistance
    // Orders are drawn on screen and clickable with a mouse, and vanilla reaches none of them
    // from the keyboard (AddDarkEventsButton ends in DisableNavigation() on both the container
    // and the button; the two HUD buttons are children of StrategyMapHUD, which UpdateMissions'
    // Navigator.Clear() leaves out of the screen's navigator entirely). The ResNet toggle is a
    // nav-help hotlink with a controller-only key. So this is the ant-farm shape again — the
    // game has the verb and only the mouse can reach it.
    //
    // ⛔ And it is what justifies TAKING Tab on this screen. The world ring and the ';' status
    // block are AIDS — they enumerate regions and continents no sighted player can read off the
    // globe — so by the stop-list parity rule they stay on their own keys and out of the Tab
    // cycle. Without this stop the tray would be the only one and Tab would have nothing to do.
    if (!view.verbs.empty()) {
        b.BeginStop("hud");
        for (const GameUI::HudVerb& verb : view.verbs) {
            const auto kind = verb.kind;
            ControlId id = ControlId::Structural(
                "geoscape/verb/" + std::to_string(static_cast<int>(kind)));

            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(verb.label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;

            // The map-mode toggle is the one verb that HAS a state, and a sighted player reads
            // it off the globe itself changing — so speech has to say it outright.
            if (kind == GameUI::HudVerb::Kind::ResNetToggle) {
                const bool on = verb.on;
                vt.Announcements.emplace_back(NodeAnnouncement::Static(
                    on ? Vocab::GeoscapeResNetOn : Vocab::GeoscapeResNetOff));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }

            // Re-read at activation rather than closing over the verb: the handler runs from the
            // input drain a frame or more after this render, and the two HUD buttons are
            // Show()/Hide()n on every RefreshData — so the pointer captured here may describe a
            // verb the game has since taken off screen.
            vt.OnActivate = [kind]() {
                GameUI::StrategyMapView live = GameUI::ReadStrategyMap(gScreen);
                for (const GameUI::HudVerb& v : live.verbs) {
                    if (v.kind == kind) {
                        GameUI::ActivateHudVerb(live, v);
                        return;
                    }
                }
            };

            b.AddItem(std::move(id), std::move(vt));
        }
    }

    return b.Build();
}

// Above the ant farm and the Avenger HUD it sits over, below anything it pushes (a mission
// briefing, the pause menu, a dialog) — which is what ProbeGeoscape's "must be the current
// screen" test enforces, mirroring UIStrategyMap's own input routing.
const Recipe kGeoscapeRecipe{"geoscape tray", ProbeGeoscape, BuildGeoscape, GeoscapeName};

// Probe order is topmost-screen-first: anything that can overlay squad select — a dialog, the
// pause menu — has to resolve before it, or the layer would attach to the screen underneath.
// ---- the popup recipe (docs/popup-inventory.md §1-3) ----
//
// One recipe for every modal notification in the game: the strategy alert family (UIAlert, 118
// variants), the bespoke one-screen popups (faction, bond, ability, Chosen fragment/level-up/
// reveal, UFO attack), and the blocking progress dialog. They look nothing alike and they share
// exactly what a reader needs: the popup is the topmost stack screen, and its own
// OnUnrealCommand answers Enter and Escape at SCREEN level.
//
// TWO STOPS. The MESSAGE, one node per line — which is the point of this whole recipe: a bond
// alert or a Chosen reveal is several paragraphs, and speech is serial, so a player has to be
// able to step through it with Tab rather than sit through all of it to reach the part they
// wanted. Then the BUTTONS. Nothing goes in gFocus: every one of these screens either disables
// navigation on its buttons or spawns them with an empty label, so pushing the game's own focus
// at them would ask the navigator for something it refuses to keep.
//
// ⛔ Activation is the SCREEN's own OnUnrealCommand, never a button's Click. UIAlert's Button1/
// Button2 are spawned with InitButton(…, "", …) and get their labels only through Flash, so
// there is no delegate to reach that the screen's own accept path does not already run — and for
// several alert names OnUnrealCommand branches BEFORE dispatching (eAlert_Contact refuses when
// the player cannot afford contact, eAlert_TrainingComplete routes Enter to cancel rather than
// confirm). Sending the key keeps every one of those branches; poking a delegate loses them all.

constexpr const char* kPopupReaderClass = "X2AccessPopupReader";

std::unique_ptr<GraphRender> BuildPopup()
{
    gFocus.clear(); // Flash-driven throughout: no panel the game will accept focus on

    const std::vector<WireRecord> words = FetchCompanionRecords(kPopupReaderClass, "DescribePopup");
    if (words.empty())
        return nullptr;

    const WireRecord* header = FindRecord(words, "popup");
    const std::string screenClass = header ? RecField(*header, "class") : std::string();

    // Message lines: the companion's own, plus — for UIAlert — the ones native reads back out of
    // the Flash panel, because that screen retains none (see GameUI::ReadAlertText).
    std::vector<std::string> lines;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "line")
            lines.push_back(RecField(rec, "text"));
    }

    std::vector<std::string> alertButtons;
    if (const WireRecord* alert = FindRecord(words, "alert")) {
        const GameUI::AlertView view = GameUI::ReadAlertText(gScreen, RecField(*alert, "mcpath"),
            RecField(*alert, "symbol"), RecField(*alert, "name"));
        // The Flash reading leads: for every alert but the objective one the companion has
        // nothing, and where it DOES have something (ObjectiveUIHeaderText/BodyText) the panel
        // is showing the same words, so appending would say each of them twice.
        if (!view.lines.empty())
            lines = view.lines;
        alertButtons = view.buttons;
    }

    // Identity carries the text, for the dialog recipe's reason: one popup can be replaced by
    // another between two polls (a callback raising the next alert in the queue), and without
    // the words in the key the differ would see the same node and stay silent about a message
    // the player has not heard.
    std::string key = screenClass + "\n";
    for (const std::string& line : lines)
        key += line + "\n";

    GraphBuilder b;
    b.BeginStop("message");

    // The screen's own title as a context rather than a node: a REPLACED popup gets no fresh
    // attach to speak a screen name with, so the heading has to ride in the node path to be
    // heard at all.
    const std::string title = header ? RecField(*header, "title") : std::string();
    if (!title.empty())
        b.PushContext(title, "", /*positions*/ false);

    // ⛔ NO StartRow here, and that is the point: a control added outside an open row becomes its
    // OWN single-item row, so the message reads DOWN with Up/Down instead of across with
    // Left/Right. A message is prose stacked vertically on screen, not a strip of side-by-side
    // controls, and walking it sideways contradicts both the layout and the reading order.
    // It also drops the "N of M" stamp, which GraphBuilder only applies within a row holding more
    // than one item — right here, because "СОЛДАТ ПОЛУЧИЛ НОВОЕ ЗВАНИЕ, 1 of 6" reads as a menu
    // position on something that is a sentence.
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& line = lines[i];
        b.AddLabel(ControlId::Structural(key + "line/" + std::to_string(i)),
            [line]() { return line; });
    }
    if (lines.empty()) {
        // Recognised, but nothing readable came back. Say so rather than attaching an empty
        // graph: silence here is indistinguishable from "no popup", and the player still has to
        // press something to get out of it.
        b.AddLabel(ControlId::Structural(key + "line/none"),
            []() { return std::string(Vocab::PopupNoText); });
    }

    // The buttons. Their labels come from the companion when it has them and from Flash for an
    // alert; when neither does, the accept/cancel pair is still offered under the game's own
    // generic wording, because a modal a blind player cannot dismiss is worse than one that
    // reads badly.
    struct Choice {
        std::string label;
        int cmd;
    };
    std::vector<Choice> choices;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "button")
            continue;
        const std::string label = RecField(rec, "label");
        if (!label.empty())
            choices.push_back({label, atoi(RecField(rec, "cmd").c_str())});
    }
    // Flash order is Button0 = confirm, Button1 = cancel — the same order UIAlert's own
    // OnUnrealCommand maps Enter and Escape onto.
    for (size_t i = 0; i < alertButtons.size(); i++)
        choices.push_back({alertButtons[i], i == 0 ? GameUI::kCmdEnter : GameUI::kCmdEscape});
    if (choices.empty()) {
        choices.push_back({std::string(Vocab::PopupContinue), GameUI::kCmdEnter});
        choices.push_back({std::string(Vocab::PopupDismiss), GameUI::kCmdEscape});
    }

    b.BeginStop("buttons");
    b.StartRow();
    for (size_t i = 0; i < choices.size(); i++) {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(choices[i].label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // Read gScreen at activation rather than capturing it: the handler runs from the input
        // drain, a frame or more after this render, and the popup may already have been replaced.
        vt.OnActivate = [cmd = choices[i].cmd]() { GameUI::SendScreenCommand(gScreen, cmd); };
        b.AddItem(ControlId::Structural(key + "button/" + std::to_string(i)), std::move(vt));
    }
    b.EndRow();
    return b.Build();
}

void* ProbePopup()
{
    const GameUI::PopupView view = GameUI::FindPopupScreen();
    // Focus is the readiness test the shell screens use; the finder has already refused anything
    // that is not topmost, inited and un-removed.
    return (view.screen && GameUI::IsScreenFocused(view.screen)) ? view.screen : nullptr;
}

// Just the word. The popup's own title and its message ride in the graph — as a context and as
// the message stop — so naming the screen only has to announce that a modal has appeared.
std::string PopupName()
{
    return Vocab::ScreenPopup;
}

const Recipe kPopupRecipe{"popup", ProbePopup, BuildPopup, PopupName};

const Recipe* const kRecipes[] = {&kDialogRecipe, &kPopupRecipe, &kNarrativeRecipe, &kIronManRecipe,
    &kSoundtrackRecipe, &kOptionsRecipe, &kDifficultyRecipe, &kSaveLoadRecipe, &kPauseMenuRecipe,
    &kPersonnelRecipe, &kChooseFacilityRecipe, &kArmoryLoadoutRecipe, &kSquadSelectRecipe,
    &kCommodityRecipe,
    &kRoomBuildRecipe, &kRoomRecipe, &kGeoscapeRecipe, &kMainMenuRecipe};

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
        SpeakMove(r.From, r.To, r.TransitionLabel, /*interrupt*/ true);
    }
    // Not moved (edge) — consumed silently; the layer's refusals are silent. The claim still
    // swallowed the chord, so the game's own navigation never runs in parallel (spec A1).
}

void OnEdgeJump(GraphDir dir)
{
    if (!gGraph)
        return;
    MoveResult r = gGraph->MoveToEdge(dir);
    if (r.Moved) {
        WriteBackFocus(r.To);
        GameUI::PlayNavSound();
        SpeakMove(r.From, r.To, "", /*interrupt*/ true);
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
        SpeakMove(r.From, r.To, r.TransitionLabel, /*interrupt*/ true);
    }
}

void OnActivate()
{
    if (!gGraph)
        return;
    if (gGraph->Activate())
        SpeakStateAfterChange();
    // No handler → consumed silently (spec §3.4).
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
        SpeakMove(nullptr, node, "", /*interrupt*/ true);
    } else {
        // Focus held (a tab node, or a row both tabs share) — name the new tab explicitly.
        std::string name = GameUI::GetOptionsTabName(gScreen, now);
        TTS::SpeakInterrupt(name.empty() ? Vocab::TabFallback(now) : name);
        if (node)
            RefreshLiveBaseline(node); // new tab under the same id — don't let the watch re-announce
    }
}

} // namespace

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

    auto attached = []() { return gAttached; };
    auto optionsAttached = []() { return gAttached && gActive == &kOptionsRecipe; };

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
    auto attachedNotGeoscape = []() { return gAttached && gActive != &kGeoscapeRecipe; };
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
        // Being on top of the stack is not the same as being reachable. Starting a campaign pops
        // the narrative screen and leaves the difficulty screen underneath inited, focused and
        // topmost for the whole twelve seconds the loading movie plays — so the layer happily
        // announced a screen that, to everyone else, had already gone. The stack's own gates say
        // otherwise, and they are the same ones the game consults before opening its pause menu.
        GameUI::StackState stack = GameUI::ReadStackState();
        bool movie = GameUI::MoviePlaying();
        bool gated = movie || (stack.valid && !stack.Interactive());
        if (gated != gGated) {
            gGated = gated;
            LOG_INFO("MenuNavigator: UI %s (movie %d, input blocked %d, screens hidden %d, "
                     "cinematic %d)",
                gated ? "gated — nothing is navigable" : "ungated", movie, stack.inputBlocked,
                stack.screensHidden, stack.cinematic);
        }
        if (gated) {
            if (gAttached)
                Detach();
            return;
        }

        if (!gAttached) {
            const Recipe* found = nullptr;
            void* screen = nullptr;
            for (const Recipe* recipe : kRecipes) {
                screen = recipe->Probe();
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
                    TTS::SpeakInterrupt(Vocab::Loading);
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
            SpeakMove(from, node, "", /*interrupt*/ false); // differ landings queue (spec A7)
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

const char* AttachedRecipeName()
{
    return (gAttached && gActive) ? gActive->logName : nullptr;
}

} // namespace MenuNavigator
