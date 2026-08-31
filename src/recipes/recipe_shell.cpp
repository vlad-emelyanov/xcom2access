// Shell recipes: the main menu, settings, the new-campaign flow, the in-game pause menu, the
// modal dialog, and save / load — docs/surfaces/shell.md. One TU per surface family, which is
// the axis these recipes were already grouped along inside menu_navigator.cpp; the split makes
// that grouping structural instead of a matter of where a banner happened to sit.

// Shared by every recipe TU: the recipe contract, the attach state a builder reads, the
// control-type registry and the companion-wire cache. See src/recipes/recipe_context.h.
#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "game_ui.h"
#include "log.h"
#include "tts.h"
#include "vocab.h"

#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MenuNavigator {
namespace Recipes {
namespace {

// ---- the main menu recipe (UIFinalShell) ----

std::unique_ptr<GraphRender> BuildMainMenu()
{
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
    GameUI::PauseMenuView view = GameUI::ReadPauseMenu(gScreen);
    if (view.options.empty())
        return nullptr;

    GraphBuilder b;
    b.BeginStop("options");
    for (size_t i = 0; i < view.options.size(); i++) {
        const GameUI::MenuItem& opt = view.options[i];
        // The row's LIST index, not the options position — the two drift apart past a skipped
        // unlabeled row, and selection and activation both address the list.
        const int index = opt.index;

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
        vt.OnTooltip = ListItemTooltipAction(opt.handle);
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
    // Nothing joins gFocus here — Flash-driven: no panels to drive the game's own focus onto.
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

// The companion call, cached on the ROW POINTERS like every other recipe's — the navigator
// re-renders about ten times a second and this walk is not free (the save screen's new-save row
// re-runs FillInHeaderForSave, which iterates the history). What this screen does differently is
// only what it does with the records AFTERWARDS: index them by the row's own `index=` rather than
// keep the emission order. So the fetch is a WireCache like the others and the index pass runs
// exactly when the cache actually refetched, which is what Generation() answers.
WireCache gSaveWords(kSaveReaderClass, "DescribeSaves");

const SaveWordMap& FetchSaveWords(const std::vector<GameUI::SaveSlot>& slots)
{
    static SaveWordMap sWords;
    static unsigned sGeneration = 0;

    std::vector<void*> key;
    key.reserve(slots.size() + 1);
    key.push_back(gScreen);
    for (const GameUI::SaveSlot& slot : slots)
        key.push_back(slot.handle);

    const std::vector<WireRecord>& records = gSaveWords.Get(std::move(key));
    if (gSaveWords.Generation() == sGeneration)
        return sWords;
    sGeneration = gSaveWords.Generation();
    sWords.clear();

    for (const WireRecord& rec : records) {
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

} // namespace

// ---- the recipes this file defines ----
//
// ⛔ THE PROBE ORDER IS NOT HERE. It is the `kRecipes` array literal in menu_navigator.cpp,
// the one place that lists every recipe in a single readable sequence next to the loop that
// consumes it. Adding a recipe means adding it there as well as declaring it in recipes.h; a
// recipe declared and never listed is a screen that is silently never probed. Each definition
// below keeps the sentence that argues where in that order it belongs.

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

// Above the pause menu and the shell it is opened from, below any dialog it raises.
const Recipe kSaveLoadRecipe{"save/load", ProbeSaveLoad, BuildSaveLoad, SaveLoadName};

} // namespace Recipes
} // namespace MenuNavigator
