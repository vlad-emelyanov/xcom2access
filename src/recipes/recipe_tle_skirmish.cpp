// The Tactical Legacy Pack's SKIRMISH MODE menu (UITLE_SkirmishModeMenu) — docs/surfaces/shell.md,
// recon in docs/tle-skirmish-recon.md. One TU for one screen: it is a twenty-one-state machine
// over a single list and a right-hand panel, and its family (the shell) already fills a file.
//
// THREE STOPS, mirroring the three things a sighted player looks at:
//   options — the list the arrows walk. Its rows are mecha items (label, optional value, or a
//             checkbox on the abilities page) and every one activates through its own Enter,
//             which is the game's OnClick* delegate for that row. Named by the screen's subtitle
//             for the current state ("EDIT SQUAD", "EDIT PRIMARY WEAPON"), so a state change —
//             every Enter is one — is heard as a new context under a new row.
//   details — what the panel on the right is showing: the mission (op name, opponent squad,
//             objective, the six soldier slots), the generic squad blurb, one soldier (rank,
//             name, class; the stat strip as a SHEET, because the game draws it as values under
//             headings; the gear list), the selected ability's text, or the opponent pod. Read
//             from the companion, which re-derives it from the state objects the screen itself
//             cached (X2AccessSkirmishReader).
//   start   — the START GAME button, a UILargeButton the game's own Down arrow reaches from the
//             bottom of the list.
//
// The panel FOLLOWS THE SELECTION on three pages (squad, enemies, abilities — the list's
// OnSetSelectedIndex is the screen's hook), so focus write-back goes through the list's own
// SetSelectedIndex and the companion is re-asked whenever the selection or the state changes.

#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "game_ui.h"
#include "game_ui_tle_skirmish.h"
#include "log.h"
#include "tts.h"
#include "vocab.h"

#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MenuNavigator {
namespace Recipes {
namespace {

constexpr const char* kSkirmishReaderClass = "X2AccessSkirmishReader";

// Keyed on the row pointers (every state change respawns them) plus the screen; `extra` carries
// the state and the selection, because the panel follows the selection on three pages and the
// rows do not change when it does.
WireCache gSkirmishWords(kSkirmishReaderClass, "DescribeSkirmish");

long long SkirmishExtra(const GameUI::SkirmishView& view)
{
    return (static_cast<long long>(view.state) << 16) | static_cast<long long>(view.selected + 1);
}

// Speak a fixed description on Space; null when there is nothing, so the kernel refuses silently.
std::function<void()> SpeakText(std::string text)
{
    if (text.empty())
        return nullptr;
    return [text = std::move(text)]() { TTS::Speak(text); };
}

// A read-only fact in the details stop: a label part, a value part, optionally a longer text on
// Space. Nothing joins gFocus — the panel is Flash-only, there is no game control to select.
void AddFact(GraphBuilder& b, const std::string& key, const std::string& label,
    const std::string& value, std::string tooltip = std::string())
{
    if (label.empty() && value.empty())
        return;
    NodeVtable vt;
    vt.Type = &TextType();
    if (!label.empty()) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
    }
    if (!value.empty()) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    }
    vt.OnTooltip = SpeakText(std::move(tooltip));
    b.AddItem(ControlId::Structural(key), std::move(vt));
}

// One list row as a node. Checkbox rows are the abilities page (Enter toggles through the row's
// own checkbox, which is OnAbilityCheckboxChanged); everything else is a line that does
// something when pressed — a button however the widget is classed — carrying its value ("Edit
// Unit: Jane Kelly", "Edit Difficulty: Commander") as the value part.
void AddSkirmishRow(GraphBuilder& b, const GameUI::OptionRow& row, bool selected,
    std::function<void()> tooltip)
{
    ControlId id = ControlId::Referenced(row.handle, "skirmish/row/" + std::to_string(row.index));
    gFocus[id.StructuralKey] = FocusTarget{row.index, nullptr, false};

    NodeVtable vt;
    vt.Announcements.emplace_back(NodeAnnouncement::Static(row.label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;

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
        // LIVE: an ability toggle re-checks the row under the same focus; StateText answers the
        // synchronous post-activation feedback.
        vt.Announcements.back().Live = true;
        vt.StateText = [valueText]() { return valueText; };
    }
    if (row.disabled) {
        // The abilities page greys the rank-0 abilities and any whose prerequisites are unmet
        // (SetDisabled on the row); the row's own Enter then does nothing.
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

// The stat strip is drawn as values under headings ("HEALTH 6, MOBILITY 12, ..." stacked to the
// right), so it is a one-row sheet: the soldier line is the primary, each stat a column named by
// its heading. Reaching a stat says its heading; reading down is moot on one row, but the shape
// is the one the game draws.
SheetConfig SkirmishSheetConfig()
{
    return SheetConfig{Vocab::TableBlank, Vocab::RoleTable, &TextType()};
}

void AddSoldierPanel(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    const WireRecord* unit = FindRecord(records, "unit");
    if (!unit)
        return;

    // Rank, name, class — the three lines SetSoldierData pushes (the rank as its icon; its name
    // is what the icon means).
    std::string who = JoinParts(*unit, {"rank", "name", "class"});

    std::vector<std::string> labels;
    std::vector<std::string> values;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "stat")
            continue;
        // Emitted as a pair, and kept as a pair: a stat with no heading is a bare figure.
        const std::string label = RecField(rec, "label");
        if (label.empty())
            continue;
        labels.push_back(label);
        values.push_back(RecField(rec, "value"));
    }

    GraphSheet sheet(b, "skirmish/soldier/", SkirmishSheetConfig());
    sheet.Region("", labels);
    NodeVtable primary;
    primary.Type = &TextType();
    primary.Announcements.emplace_back(NodeAnnouncement::Static(who));
    primary.Announcements.back().Kind = AnnouncementKinds::Label;
    // The row's other facts on the PRIMARY too, so the flat reading carries the whole strip.
    for (std::size_t i = 0; i < labels.size(); i++) {
        std::string part = labels[i];
        AppendPhrase(part, values[i], " ");
        primary.Announcements.emplace_back(NodeAnnouncement::Static(part));
        primary.Announcements.back().Kind = AnnouncementKinds::Value;
    }
    std::vector<std::function<std::string()>> producers;
    producers.reserve(values.size());
    for (const std::string& value : values)
        producers.push_back([value]() { return value; });
    sheet.Row(std::move(primary), unit, std::move(producers));
    sheet.Finish();

    // The gear list under the strip: armor, primary, secondary, the utility items, the grenade
    // pocket, the heavy weapon, the PCS — each a heading and an item name, in SetSoldierGear's
    // order. Emitted with its label so an empty slot the game draws nothing for is skipped
    // rather than shifting the ones after it.
    int gear = 0;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "gear")
            continue;
        AddFact(b, "skirmish/gear/" + std::to_string(gear++), RecField(rec, "label"),
            RecField(rec, "item"));
    }
}

void AddMissionPanel(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    const WireRecord* mission = FindRecord(records, "mission");
    if (mission) {
        AddFact(b, "skirmish/mission", std::string(), RecField(*mission, "opname"));
        // The opponent's description is the paragraph under its name; Space, as elsewhere.
        AddFact(b, "skirmish/enemy", RecField(*mission, "enemylabel"), RecField(*mission, "enemy"),
            RecField(*mission, "enemydesc"));
        AddFact(b, "skirmish/objective", RecField(*mission, "objlabel"),
            RecField(*mission, "objective"));
    }
    // The six soldier slots: name, class, rank — the slots past the squad are drawn empty and
    // the companion sends nothing for them.
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "member")
            continue;
        AddFact(b, "skirmish/member/" + RecField(rec, "index"), RecField(rec, "name"),
            JoinParts(rec, {"rank", "class"}));
    }
}

void AddSquadInfoPanel(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    const WireRecord* info = FindRecord(records, "squadinfo");
    if (!info)
        return;
    AddFact(b, "skirmish/squadinfo", RecField(*info, "label"), RecField(*info, "desc"));
    // The six class names drawn as icons with captions, then the faction crests with theirs.
    std::string classes;
    for (const std::string& name : RecFieldAll(*info, "class"))
        AppendPhrase(classes, name);
    AddFact(b, "skirmish/classes", std::string(), classes);
    std::string factions;
    for (const std::string& name : RecFieldAll(*info, "faction"))
        AppendPhrase(factions, name);
    AddFact(b, "skirmish/factioninfo", RecField(*info, "factionlabel"),
        RecField(*info, "factiondesc"));
    AddFact(b, "skirmish/factions", std::string(), factions);
}

void AddAbilityPanel(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    const WireRecord* ability = FindRecord(records, "ability");
    if (!ability)
        return;
    // The selected ability's name and its whole description: the description IS the panel, so
    // it is a value here and not hidden behind Space.
    AddFact(b, "skirmish/ability", RecField(*ability, "name"), RecField(*ability, "desc"));
}

void AddPodPanel(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    const WireRecord* pod = FindRecord(records, "pod");
    if (!pod)
        return;
    AddFact(b, "skirmish/pod", RecField(*pod, "name"), std::string(), RecField(*pod, "desc"));
    // The portraits: one caption per enemy type the pod can hold, in the game's grouping.
    std::string enemies;
    for (const std::string& name : RecFieldAll(*pod, "enemy"))
        AppendPhrase(enemies, name);
    AddFact(b, "skirmish/podenemies", std::string(), enemies);
}

// The ability text for one row, for Space — the companion mirrors UpdateAbilityInfo per row so
// a player need not select a row to hear what it does.
std::string AbilityTooltipFor(const std::vector<WireRecord>& records, int index)
{
    const std::string want = std::to_string(index);
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == "abil" && RecField(rec, "index") == want)
            return JoinParts(rec, {"name", "desc"});
    }
    return std::string();
}

std::unique_ptr<GraphRender> BuildSkirmish()
{
    const GameUI::SkirmishView view = GameUI::ReadSkirmish(gScreen);
    if (!view.list)
        return nullptr; // InitScreen has not spawned the list yet
    if (view.rows.empty() && !view.startButton)
        return nullptr; // mid-rebuild: HideListItems has run and the new rows are not up

    const std::vector<WireRecord>& records
        = FetchWordsKeyedOnRows(gSkirmishWords, gScreen, view.rows, SkirmishExtra(view));

    GraphBuilder b;

    // ---- the list ----
    b.BeginStop("options");
    const bool abilities = view.state == static_cast<int>(GameUI::SkirmishState::SoldierAbilities);
    if (!view.subtitle.empty())
        b.PushContext(view.subtitle);
    ControlId start;
    bool haveStart = false;
    for (const GameUI::OptionRow& row : view.rows) {
        std::function<void()> tooltip;
        if (abilities) {
            tooltip = SpeakText(AbilityTooltipFor(records, row.index));
        } else {
            // A mecha row carries its hover tooltip on its BG child, hence the row reader.
            tooltip = [handle = row.handle]() {
                std::string tip = GameUI::ReadRowTooltip(handle);
                if (!tip.empty())
                    TTS::Speak(tip);
            };
        }
        const bool selected = row.index == view.selected;
        AddSkirmishRow(b, row, selected, std::move(tooltip));
        if (!haveStart && (selected || !start.IsValid())) {
            start = ControlId::Structural("skirmish/row/" + std::to_string(row.index));
            haveStart = selected;
        }
    }
    if (!view.subtitle.empty())
        b.PopContext();

    // ---- the panel ----
    b.BeginStop("details");
    using S = GameUI::SkirmishState;
    switch (static_cast<S>(view.state)) {
    case S::Base:
    case S::MapData:
    case S::Map:
    case S::Mission:
    case S::Biome:
    case S::Difficulty:
        AddMissionPanel(b, records);
        break;
    case S::Squad:
        // The companion sends the squad blurb on the ADD NEW UNIT row (and with nothing
        // selected) and a soldier otherwise — the screen's own RefreshSquadDetailsPanel test.
        AddSquadInfoPanel(b, records);
        AddSoldierPanel(b, records);
        break;
    case S::SoldierAbilities:
        AddAbilityPanel(b, records);
        break;
    case S::Enemies:
        AddPodPanel(b, records);
        break;
    default:
        // Soldier data, equipment, and every picker under them keep the soldier panel up.
        AddSoldierPanel(b, records);
        break;
    }

    // ---- the button ----
    if (view.startButton) {
        b.BeginStop("start");
        ControlId id = ControlId::Referenced(view.startButton, "skirmish/start");
        // Navigable in the game (LeftColumn.Navigator holds it after the list), so the write-back
        // moves the game's highlight onto it and deselects the list, as the game's Down does.
        gFocus[id.StructuralKey] = FocusTarget{-1, view.startButton, true};
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(
            view.startLabel.empty() ? std::string(Vocab::SkirmishStartFallback()) : view.startLabel));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement(
            [focused = view.startFocused]() {
                return focused ? std::string(Vocab::Selected) : std::string();
            },
            false, AnnouncementKinds::Selected));
        vt.OnActivate = [handle = view.startButton]() { GameUI::ClickButton(handle); };
        vt.OnTooltip = PanelTooltipAction(view.startButton);
        b.AddItem(std::move(id), std::move(vt));
    }

    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string SkirmishName()
{
    const GameUI::SkirmishView view = GameUI::ReadSkirmish(gScreen);
    std::string name = view.title.empty() ? std::string(Vocab::ScreenSkirmishFallback()) : view.title;
    // The base menu's subtitle is empty; every other state names itself and is heard as the
    // list's context, so the attach line carries only what the base shows.
    AppendPhrase(name, view.subtitle);
    return name;
}

} // namespace

// Pushed over the Tactical Legacy Pack hub (the shell file's kTLEHubRecipe), raising only dialogs
// above itself — the leave / remove-soldier / needs-a-human confirmations, all the dialog recipe's.
// Listed anywhere above the hub and below the dialog.
const Recipe kSkirmishRecipe{"skirmish", ProbeFocused<GameUI::FindSkirmishScreen>, BuildSkirmish,
    SkirmishName, GameUI::SetSkirmishListIndex};

} // namespace Recipes
} // namespace MenuNavigator
