#include "menu_navigator.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "tts.h"
#include "vocab.h"
#include "log.h"

#include "Graph/GraphAnnouncer.hpp"
#include "Graph/GraphBuilder.hpp"
#include "Graph/KeyGraph.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

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
const Recipe* const kRecipes[] = {&kDialogRecipe, &kNarrativeRecipe, &kIronManRecipe,
    &kSoundtrackRecipe, &kOptionsRecipe, &kDifficultyRecipe, &kPauseMenuRecipe, &kMainMenuRecipe};

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
