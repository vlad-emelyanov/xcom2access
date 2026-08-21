#include "menu_navigator.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "tts.h"
#include "ue3_reflection.h"
#include "vocab.h"
#include "log.h"

#include "Graph/GraphAnnouncer.hpp"
#include "Graph/GraphBuilder.hpp"
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

void Detach()
{
    const char* name = gActive ? gActive->logName : "?";
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
constexpr const char* kWireRecordSep = "~|~";
constexpr const char* kSquadFieldSep = "~:~";

// One record of a companion's wire format, as ordered key/value pairs — ordered because some
// keys REPEAT (squad select's `util`, once per slot) and the order is the row order. Shared by
// every recipe whose words come from a companion CDO.
using WireRecord = std::vector<std::pair<std::string, std::string>>;

std::string RecField(const WireRecord& rec, const char* key)
{
    for (const auto& kv : rec) {
        if (kv.first == key)
            return kv.second;
    }
    return std::string();
}

std::vector<std::string> RecFieldAll(const WireRecord& rec, const char* key)
{
    std::vector<std::string> out;
    for (const auto& kv : rec) {
        if (kv.first == key)
            out.push_back(kv.second);
    }
    return out;
}

std::vector<std::string> SplitOn(const std::string& text, const char* sep)
{
    std::vector<std::string> parts;
    const size_t sepLen = strlen(sep);
    size_t start = 0;
    for (;;) {
        const size_t hit = text.find(sep, start);
        if (hit == std::string::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, hit - start));
        start = hit + sepLen;
    }
}

// ONE outbound call per render, which is one per keypress — the same budget the tile cursor
// spends. Used wherever the widget tree cannot answer: squad select retains no words at all, and
// the loadout retains its rows but not the four things it computes and throws away.
std::vector<WireRecord> FetchCompanionRecords(const char* className, const char* funcName)
{
    std::vector<WireRecord> records;
    void* cdo = GameUI::FindCompanionCDO(className);
    if (!cdo)
        return records; // FindCompanionCDO logs the miss (companion mod not loaded)

    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, funcName, zeros)) {
        LOG_WARN("MenuNavigator: %s.%s failed", className, funcName);
        return records;
    }
    const std::string raw
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (raw.empty())
        return records;

    for (const std::string& recText : SplitOn(raw, kWireRecordSep)) {
        WireRecord rec;
        for (const std::string& field : SplitOn(recText, kSquadFieldSep)) {
            const size_t eq = field.find('=');
            if (eq == std::string::npos || eq == 0)
                continue;
            // CleanText here rather than in script: one implementation of "strip the markup the
            // game colours its text with", shared with every other screen.
            rec.emplace_back(field.substr(0, eq), GameUI::CleanText(field.substr(eq + 1)));
        }
        if (!rec.empty())
            records.push_back(std::move(rec));
    }
    return records;
}

// Join the parts a card names, dropping the ones the game is not showing. The companion sends
// identity as four SEPARATE fields for exactly this: joining them there meant a value that
// CleanText later reduced to nothing still left its comma behind, and every rookie read
// "Новобранец, Sokolova,," live 2026-08-21.
std::string JoinParts(const WireRecord& rec, std::initializer_list<const char*> keys)
{
    std::string line;
    for (const char* key : keys) {
        const std::string part = RecField(rec, key);
        if (part.empty())
            continue;
        if (!line.empty())
            line += ", ";
        line += part;
    }
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
    case GameUI::SquadButton::WeaponPrime: {
        const std::string label = RecField(card, "weaponlabel");
        const std::string name = RecField(card, "weapon");
        if (name.empty())
            return label;
        return label.empty() ? name : label + ", " + name;
    }
    case GameUI::SquadButton::WeaponHeavy: {
        const std::string label = RecField(card, "heavylabel");
        const std::string name = RecField(card, "heavy");
        return label.empty() ? name : label + ", " + name;
    }
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
            const std::string text = label.empty() ? value : label + ", " + value;
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
        return row.slotLabel + ", " + Vocab::LoadoutEmptySlot;
    }
    return row.slotLabel.empty() ? row.title : row.slotLabel + ", " + row.title;
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
        const std::string delta = RecField(rec, "delta");
        if (!delta.empty())
            value += (value.empty() ? "" : ", ") + delta;
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
        const std::string label = view.makeAvailable + ", "
            + LoadoutRowLabel(view.rows[static_cast<size_t>(view.selectedRow)]);
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

    void* cdo = GameUI::FindCompanionCDO(kSaveReaderClass);
    if (!cdo)
        return sWords; // FindCompanionCDO logs the miss (companion mod not loaded)

    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, "DescribeSaves", zeros)) {
        LOG_WARN("MenuNavigator: save/load — DescribeSaves failed");
        return sWords;
    }
    const std::string raw
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (raw.empty())
        return sWords;

    for (const std::string& recText : SplitOn(raw, kWireRecordSep)) {
        int index = -1;
        SaveWords words;
        for (const std::string& field : SplitOn(recText, kSquadFieldSep)) {
            const size_t eq = field.find('=');
            if (eq == std::string::npos || eq == 0)
                continue;
            const std::string key2 = field.substr(0, eq);
            // CleanText here rather than in script: one implementation of "strip the markup the
            // game colours its text with", shared with every other screen.
            const std::string value = GameUI::CleanText(field.substr(eq + 1));
            if (key2 == "index")
                index = atoi(value.c_str());
            else if (key2 == "desc")
                words.desc = value;
            else if (key2 == "name")
                words.name = value;
            else if (key2 == "mission")
                words.mission = value;
            else if (key2 == "time")
                words.time = value;
            else if (key2 == "new")
                words.isNew = true;
        }
        if (index >= 0)
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
    std::string line;
    auto append = [&line](const std::string& part) {
        if (part.empty())
            return;
        if (!line.empty())
            line += ", ";
        line += part;
    };

    if (!words) {
        // No record for this row: the companion is missing, or the row carries no header at
        // all. The widget's own label is still a true statement about what is there.
        return slot.acceptLabel;
    }
    if (words->isNew)
        append(slot.acceptLabel);
    append(words->desc);
    append(words->name);
    append(words->mission);
    append(words->time);
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

    b.BeginStop("actions");

    if (view.actions.empty()) {
        // Not a fault. Living Quarters and the Bar/Memorial carry no shortcut category at all
        // (CurrentCategory reads -1), so the game shows no submenu either — and on a room that
        // does have one, this is also the state while the list is still being rebuilt. Naming
        // the way out matters more here than anywhere else: with no nodes to move between, a
        // player has nothing to feel their way with.
        b.AddLabel(ControlId::Structural("room/empty"), []() {
            return std::string(Vocab::RoomNoActions) + ". " + Vocab::RoomLeaveHint;
        });
        return b.Build();
    }

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
        vt.StateText = [selected]() {
            return selected ? std::string(Vocab::Selected) : std::string();
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

        // The right-hand quantity column, spoken only where the row draws one.
        const std::string qty = RecField(rec, "qty");
        if (!qty.empty())
            label += ", " + qty + " " + Vocab::CommodityOwned;

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

// Above the pause menu and the shell it is opened from, below any dialog it raises.
const Recipe kSaveLoadRecipe{"save/load", ProbeSaveLoad, BuildSaveLoad, SaveLoadName};

// Probe order is topmost-screen-first: anything that can overlay squad select — a dialog, the
// pause menu — has to resolve before it, or the layer would attach to the screen underneath.
const Recipe* const kRecipes[] = {&kDialogRecipe, &kNarrativeRecipe, &kIronManRecipe,
    &kSoundtrackRecipe, &kOptionsRecipe, &kDifficultyRecipe, &kSaveLoadRecipe, &kPauseMenuRecipe,
    &kArmoryLoadoutRecipe, &kSquadSelectRecipe, &kCommodityRecipe, &kRoomBuildRecipe,
    &kRoomRecipe, &kMainMenuRecipe};

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
    RegisterClaim({VK_HOME, ModNone, attached, onDown([]() { OnEdgeJump(GraphDir::Up); }),
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
    RegisterClaim({VK_SPACE, ModNone, attached,
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
