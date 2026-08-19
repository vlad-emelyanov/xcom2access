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
        GameUI::SetOptionsListIndex(gScreen, target.listIndex);
        return;
    }
    if (target.clearList)
        GameUI::SetOptionsListIndex(gScreen, -1);
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
    LOG_INFO("MenuNavigator: attached to %s (%zu focus targets)", gActive->logName, gFocus.size());
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

// ---- the recipe roster (probe order: topmost screens first — settings stacks on the shell) ----

const Recipe kOptionsRecipe{"settings", ProbeOptions, BuildOptions, OptionsName};
const Recipe kMainMenuRecipe{"main menu", ProbeMainMenu, BuildMainMenu, MainMenuName};
const Recipe* const kRecipes[] = {&kOptionsRecipe, &kMainMenuRecipe};

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

} // namespace MenuNavigator
