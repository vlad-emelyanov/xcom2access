// Mod-provided screens' recipes — Mod Config Menu first (docs/mod-config-menu-recon.md). The
// family exists because these screens are not the game's: the reader (game_ui_mods.cpp) reaches
// them by walking and by name rather than by pinned offset, and LWOTC's replacements will join
// it. The recipe itself is ordinary — a settings screen is a settings screen — and it is modelled
// line for line on BuildOptions in recipe_shell.cpp so the two read alike.

#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "game_ui.h"
#include "game_ui_mods.h"
#include "log.h"
#include "tts.h"
#include "vocab.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace MenuNavigator {
namespace Recipes {
namespace {

// A structural key that stays stable across rebuilds and cannot collide: keyed on the LABEL, as
// the settings recipe keys its rows (so a remembered position survives a re-render), with the
// list index appended only when a page carries two settings with the same words — a real case
// on a mod that labels every row of a group identically.
std::string UniqueKey(std::set<std::string>& used, const std::string& prefix,
    const std::string& label, int index)
{
    std::string key = prefix + label;
    if (!used.insert(key).second) {
        key += "#" + std::to_string(index);
        used.insert(key);
    }
    return key;
}

// One setting row as a node — BuildOptions' switch, with two differences: the slider speaks the
// row's OWN value text (the bar's percent is what the game draws for its own sliders; MCM draws
// a number beside the bar, and that number is what a sighted player reads — and when MCM hides
// both, nothing is drawn and nothing is spoken), and focus write-back goes through the page's
// list rather than the recipe's default.
void AddSettingRow(GraphBuilder& b, const GameUI::ModConfigItem& item, int selectedRow,
    const std::string& key)
{
    const GameUI::OptionRow& row = item.row;
    ControlId id = ControlId::Referenced(row.handle, key);
    gFocus[id.StructuralKey] = FocusTarget{row.index, nullptr, false};

    NodeVtable vt;
    vt.Announcements.emplace_back(NodeAnnouncement::Static(row.label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;

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
        // Never the bar's percent: MCM_Slider.SetEditable(false) hides the value display AND the
        // bar, so an empty sliderValue means a sighted player sees the label alone, and the
        // percent left in the hidden widget is a fact the game draws nowhere (the aid rule). The
        // row still announces Disabled below.
        valueText = item.sliderValue;
        // Disabled rows declare no adjust: the kernel's silent refusal, not a command the game
        // would drop followed by a false state re-speak.
        if (!row.disabled)
            vt.OnAdjust = [handle = row.handle](int sign, bool) {
                GameUI::SendRowCommand(
                    handle, sign > 0 ? GameUI::kCmdArrowRight : GameUI::kCmdArrowLeft);
            };
        break;
    case GameUI::OptionWidget::Spinner:
        vt.Type = &SpinnerType();
        valueText = row.value;
        if (!row.disabled)
            vt.OnAdjust = [handle = row.handle](int sign, bool) {
                GameUI::SendRowCommand(
                    handle, sign > 0 ? GameUI::kCmdArrowRight : GameUI::kCmdArrowLeft);
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
        valueText = row.value; // the embedded button's own label (MCM_Button's ButtonLabel)
        vt.OnActivate = [handle = row.handle]() {
            GameUI::SendRowCommand(handle, GameUI::kCmdEnter);
        };
        break;
    case GameUI::OptionWidget::ColorChip:
        vt.Type = &TextType();
        valueText = Vocab::ColourName(row.value);
        vt.OnActivate = [handle = row.handle]() {
            GameUI::SendRowCommand(handle, GameUI::kCmdEnter);
        };
        break;
    case GameUI::OptionWidget::Value:
    case GameUI::OptionWidget::Description:
        // MCM_Label: UpdateDataDescription, a line of text with a tooltip and no control.
        vt.Type = &TextType();
        valueText = row.value;
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
        // The state alone (SetEditable(false) → SetDisabled); the why is whatever tooltip the
        // mod gave the row, on Space.
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
    }
    // Space: the setting's tooltip — the manager's copy when the page has been shown twice, the
    // row's retained DisplayTooltip before that (see ReadModConfigRowTooltip for why both).
    vt.OnTooltip = [handle = row.handle]() {
        std::string tip = GameUI::ReadModConfigRowTooltip(handle);
        if (!tip.empty())
            TTS::Speak(tip);
    };
    vt.Announcements.emplace_back(NodeAnnouncement(
        [sel = (row.index == selectedRow)]() {
            return sel ? std::string(Vocab::Selected) : std::string();
        },
        false, AnnouncementKinds::Selected));

    b.AddItem(std::move(id), std::move(vt));
}

// The screen as a graph, three Tab-stops: the tab list (one per client mod; Enter shows its
// page), the page (its groups as contexts around their rows), and the bottom buttons.
std::unique_ptr<GraphRender> BuildModConfig()
{
    GameUI::ModConfigView view = GameUI::ReadModConfig(gScreen);
    if (view.tabs.empty() && view.buttons.empty())
        return nullptr; // the skeleton is not built yet; the next render will see it

    GraphBuilder b;
    std::set<std::string> used;

    // The tab list is VERTICAL (a UIList down the left edge), so its nodes stack rather than
    // sharing a row: Up/Down walk it, as they do on screen.
    b.BeginStop("tabs");
    std::string checkedTabKey;
    std::string firstTabKey;
    for (const GameUI::OptionRow& tab : view.tabs) {
        const std::string key = UniqueKey(used, "mcm/tab/", tab.label, tab.index);
        if (firstTabKey.empty())
            firstTabKey = key;
        if (tab.checked && checkedTabKey.empty())
            checkedTabKey = key;
        ControlId id = ControlId::Referenced(tab.handle, key);
        FocusTarget focus;
        focus.custom = [i = tab.index]() { GameUI::SetModConfigTabIndex(gScreen, i); };
        gFocus[id.StructuralKey] = std::move(focus);

        NodeVtable vt;
        vt.Type = &TabType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(tab.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The row is a checkbox the mod uses as a radio: checked == this page is the one showing.
        // Spoken as the SELECTED part, not as checked/unchecked — it is a tab.
        vt.Announcements.emplace_back(NodeAnnouncement(
            [sel = tab.checked]() { return sel ? std::string(Vocab::Selected) : std::string(); },
            false, AnnouncementKinds::Selected));
        // Enter: the row's own OnUnrealCommand → Checkbox.OnUnrealCommand → the changed callback
        // → TabClickedHandler → ChoosePanelByPageID. A custom-page tab unchecks itself there and
        // opens a screen of the mod's own instead; that screen is that mod's to read.
        vt.OnActivate = [handle = tab.handle]() {
            GameUI::SendRowCommand(handle, GameUI::kCmdEnter);
        };
        vt.StateText = [sel = tab.checked]() {
            return sel ? std::string(Vocab::Selected) : std::string();
        };
        b.AddItem(std::move(id), std::move(vt));
    }

    b.BeginStop("page");
    std::string firstRowKey;
    std::string selectedRowKey;
    if (!view.pageShown) {
        // SelectedPageID starts at -1 and the right side is EMPTY until a tab is clicked. A
        // sighted player sees a blank panel; this says what the blank means and gives the stop a
        // node to stand on, so Tab still cycles three stops.
        b.AddLabel(ControlId::Structural("mcm/nopage"),
            []() { return std::string(Vocab::ModConfigNoPage); });
    } else {
        // The page title (the mod's name, or MCM's own "Mod Settings") is the outer context;
        // each group heading opens a context of its own around its rows. Contexts are pure
        // structure — announced on entry, never a stop for the arrows — which is what a heading
        // drawn on a separator line is to a sighted player. Every heading entry is a group
        // BOUNDARY and closes the open group; only one with words opens a new one, so an
        // unlabelled group's rows read under the page title, never under the previous heading.
        b.PushContext(view.pageTitle.empty() ? Vocab::ScreenModConfigFallback : view.pageTitle);
        bool inGroup = false;
        for (const GameUI::ModConfigItem& item : view.items) {
            if (item.heading) {
                if (inGroup)
                    b.PopContext();
                inGroup = !item.text.empty();
                if (inGroup)
                    b.PushContext(item.text);
                continue;
            }
            const std::string key
                = UniqueKey(used, "mcm/row/", item.row.label, item.row.index);
            if (firstRowKey.empty())
                firstRowKey = key;
            if (item.row.index == view.selectedRow && selectedRowKey.empty())
                selectedRowKey = key;
            AddSettingRow(b, item, view.selectedRow, key);
        }
        if (inGroup)
            b.PopContext();
        b.PopContext();
    }

    // Save and Exit, Cancel, and the page's Reset when the mod enabled it: their own stop, as on
    // the settings screen, horizontal — Tab reaches them, arrows stay in the list they were in.
    b.BeginStop("buttons");
    b.StartRow();
    for (const GameUI::MenuItem& btn : view.buttons) {
        const std::string key = UniqueKey(used, "mcm/btn/", btn.label, -1);
        ControlId id = ControlId::Referenced(btn.handle, key);
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
            [focused = btn.focused]() {
                return focused ? std::string(Vocab::Selected) : std::string();
            },
            false, AnnouncementKinds::Selected));
        vt.OnActivate = [handle = btn.handle]() { GameUI::ClickButton(handle); };
        vt.OnTooltip = PanelTooltipAction(btn.handle);
        b.AddItem(std::move(id), std::move(vt));
    }
    b.EndRow();

    // First landing: with a page showing, its selected row, else its first row (the player
    // clicked a tab to get here, so the page is what they came for); with none, the checked tab
    // if the mod re-checked one, else the first tab — the click that makes a page appear.
    std::string start;
    if (view.pageShown)
        start = !selectedRowKey.empty() ? selectedRowKey : firstRowKey;
    if (start.empty())
        start = !checkedTabKey.empty() ? checkedTabKey : firstTabKey;
    if (!start.empty())
        b.SetStart(ControlId::Structural(start));

    return b.Build();
}

std::string ModConfigName()
{
    std::string title;
    std::string subtitle;
    GameUI::ReadModConfigHeading(gScreen, &title, &subtitle);
    // Title, then the subtitle joined by the kernel's phrase rule — never a bare ", ".
    std::string name = JoinPhrases(title, subtitle);
    return name.empty() ? Vocab::ScreenModConfigFallback : name;
}

} // namespace

// ---- the recipes this file defines (probe ORDER is kRecipes in menu_navigator.cpp) ----

// Pushed OVER the settings screen by the button MCM injects there, so it must resolve before the
// settings recipe; it raises nothing above itself but a custom page's own screen, which is that
// mod's to read. Rows select through the page's list; the tabs carry their own write-back.
const Recipe kModConfigRecipe{"mod config", ProbeFocused<GameUI::FindModConfigScreen>,
    BuildModConfig, ModConfigName, GameUI::SetModConfigRowIndex};

} // namespace Recipes
} // namespace MenuNavigator
