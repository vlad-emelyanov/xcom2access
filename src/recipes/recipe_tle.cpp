// Tactical Legacy Pack recipes, the shell half: the Legacy Operations menu, its difficulty page
// and the soldier loadout popup — docs/surfaces/shell.md ("Tactical Legacy Pack"). The hub they
// hang off is in recipe_shell.cpp beside the main menu; the skirmish, challenge and in-mission
// screens have their own TUs in this family.

#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "game_ui.h"
#include "game_ui_tle.h"
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

constexpr const char* kTLEReaderClass = "X2AccessTLEReader";

// Speak a fixed text on Space — the description a sighted player reads off the panel.
std::function<void()> SpeakAction(std::string text)
{
    if (text.empty())
        return nullptr; // no handler → the kernel refuses silently
    return [text = std::move(text)]() { TTS::Speak(text); };
}

// A row's own hover tooltip on Space (the difficulty descriptions).
std::function<void()> RowTipAction(void* handle)
{
    return [handle]() {
        std::string tip = GameUI::ReadRowTooltip(handle);
        if (!tip.empty())
            TTS::Speak(tip);
    };
}

// The record of `kind` whose `index=` field is `index`, or null.
const WireRecord* FindIndexed(const std::vector<WireRecord>& records, const char* kind, int index)
{
    const std::string want = std::to_string(index);
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == kind && RecField(rec, "index") == want)
            return &rec;
    }
    return nullptr;
}

// A read-only line: "<label> <value>", the label being the game's own caption. Nothing is added
// when the value is missing — the game draws nothing there either.
void AddTextLine(GraphBuilder& b, const std::string& key, const std::string& label,
    const std::string& value, std::function<void()> tooltip = nullptr)
{
    if (value.empty())
        return;
    NodeVtable vt;
    vt.Type = &TextType();
    if (!label.empty()) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    } else {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
    }
    vt.OnTooltip = std::move(tooltip);
    b.AddItem(ControlId::Structural(key), std::move(vt));
}

// ---- the Legacy Operations menu (UITLE_LadderModeMenu) ----
//
// FOUR STOPS. The ladders: both lists as one column under their two headers, the way the screen
// draws them, each row a button (Enter is the screen's own OnLadderClicked: load the save, or
// open the difficulty page). The details: the right-hand panel for the selected ladder, one
// read-only line per fact, under the ladder's name as context. The squad: one node per soldier
// the panel draws, Enter opening the loadout popup the mouse-only info button opens. The actions:
// CONTINUE or BEGIN, and ABANDON / DELETE where the game draws them — mouse-only Flash buttons in
// vanilla, re-provided through the screen's own popup functions (each raises its confirm dialog).

// Keyed on every row of both lists (the save-list callback respawns them all) plus the screen;
// the selection rides in `extra`, because the panel is the selection's shadow.
WireCache gLadderWords(kTLEReaderClass, "DescribeLadderMenu");

long long LadderSelectionKey(const GameUI::LadderMenuView& view)
{
    return static_cast<long long>(view.clickedIndex)
        + (view.clickedList && view.clickedList == view.randomList ? 0x10000LL : 0LL);
}

const std::vector<WireRecord>& FetchLadderWords(const GameUI::LadderMenuView& view)
{
    std::vector<void*> key;
    key.reserve(view.narrative.size() + view.random.size() + 1);
    for (const GameUI::LadderRow& row : view.narrative)
        key.push_back(row.handle);
    for (const GameUI::LadderRow& row : view.random)
        key.push_back(row.handle);
    key.push_back(view.screen);
    return gLadderWords.Get(std::move(key), LadderSelectionKey(view));
}

// The name a row reads as: the companion's for a narrative ladder, the retained Text for a
// procedural one, a numbered fallback when neither answered.
std::string LadderRowName(const GameUI::LadderRow& row, const WireRecord* rec, bool random)
{
    std::string name = random ? row.label : (rec ? RecField(*rec, "name") : std::string());
    if (name.empty())
        name = Vocab::LadderRowFallback(row.index + 1);
    return name;
}

void AddLadderRow(GraphBuilder& b, const GameUI::LadderMenuView& view, const GameUI::LadderRow& row,
    bool random, const WireRecord* rec)
{
    const int index = row.index;
    ControlId id = ControlId::Referenced(row.handle,
        std::string(random ? "ladder/r/" : "ladder/n/") + std::to_string(index));
    // Two lists, so the write-back is the screen's own select-and-refresh rather than a single
    // list index. Re-read at write-back time: it runs a frame or more after this render.
    gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [random, index]() {
        GameUI::SelectLadderRow(GameUI::ReadLadderMenu(gScreen), random, index);
    }};

    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(LadderRowName(row, rec, random)));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (rec) {
        // IN PROGRESS / COMPLETE, then the row's medal icons as the game's own medal word.
        std::string value = RecField(*rec, "status");
        const std::string medal = RecField(*rec, "medal");
        if (!medal.empty()) {
            std::string medals = view.medalsLabel;
            AppendPhrase(medals, medal, " ");
            AppendPhrase(value, medals);
        }
        if (!value.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
    }
    if (row.disabled) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
    }
    const bool selected = view.clickedIndex == index
        && view.clickedList == (random ? view.randomList : view.narrativeList);
    vt.Announcements.emplace_back(NodeAnnouncement(
        [selected]() { return selected ? std::string(Vocab::Selected) : std::string(); }, false,
        AnnouncementKinds::Selected));
    vt.OnActivate = [random, index]() {
        GameUI::ActivateLadderRow(GameUI::ReadLadderMenu(gScreen), random, index);
        gLadderWords.Invalidate();
    };
    b.AddItem(std::move(id), std::move(vt));
}

std::unique_ptr<GraphRender> BuildLadderMenu()
{
    const GameUI::LadderMenuView view = GameUI::ReadLadderMenu(gScreen);
    // Both lists are filled by the save-list callback, a frame or more after the push; until
    // then the screen is not built and the navigator polls again.
    if (view.narrative.empty() && view.random.empty())
        return nullptr;

    const std::vector<WireRecord>& records = FetchLadderWords(view);

    GraphBuilder b;

    // ---- the ladders ----
    b.BeginStop("ladders");
    ControlId start;
    if (!view.narrative.empty()) {
        b.PushContext(view.narrativeHeader);
        for (const GameUI::LadderRow& row : view.narrative) {
            if (view.clickedList == view.narrativeList && view.clickedIndex == row.index)
                start = ControlId::Structural("ladder/n/" + std::to_string(row.index));
            AddLadderRow(b, view, row, false, FindIndexed(records, "row", row.index));
        }
        b.PopContext();
    }
    if (!view.random.empty()) {
        b.PushContext(view.randomHeader);
        for (const GameUI::LadderRow& row : view.random) {
            if (view.clickedList == view.randomList && view.clickedIndex == row.index)
                start = ControlId::Structural("ladder/r/" + std::to_string(row.index));
            AddLadderRow(b, view, row, true, nullptr);
        }
        b.PopContext();
    }

    // ---- the details panel ----
    const WireRecord* panel = FindRecord(records, "panel");
    if (panel) {
        // The panel's subject, as its context: the selected row's own name.
        std::string subject;
        const bool random = view.clickedList == view.randomList;
        const std::vector<GameUI::LadderRow>& rows = random ? view.random : view.narrative;
        if (view.clickedIndex >= 0 && view.clickedIndex < static_cast<int>(rows.size()))
            subject = LadderRowName(rows[view.clickedIndex],
                random ? nullptr : FindIndexed(records, "row", view.clickedIndex), random);
        // The panel's identity carries the subject, so a selection change re-speaks the lines a
        // player is parked on rather than leaving a stale readout under a new name.
        const std::string key = "ladder/panel/" + RecField(*panel, "index") + "/";

        b.BeginStop("details");
        if (!subject.empty())
            b.PushContext(subject, "", /*positions*/ false);
        AddTextLine(b, key + "lock", RecField(*panel, "locked"), RecField(*panel, "lockedtext"));
        AddTextLine(b, key + "objective", RecField(*panel, "objectivelabel"),
            RecField(*panel, "objective"));
        AddTextLine(b, key + "progress", RecField(*panel, "progresslabel"),
            RecField(*panel, "progress"));
        AddTextLine(b, key + "difficulty", RecField(*panel, "difficultylabel"),
            RecField(*panel, "difficulty"));
        AddTextLine(b, key + "score", RecField(*panel, "scorelabel"), RecField(*panel, "score"));
        // The medal meter: each threshold with its word, and whether the score has reached it.
        {
            const std::vector<std::string> medals = RecFieldAll(*panel, "medal");
            const std::vector<std::string> won = RecFieldAll(*panel, "medalwon");
            std::string line;
            for (size_t i = 0; i < medals.size(); i++) {
                std::string part = medals[i];
                if (i < won.size() && !won[i].empty())
                    AppendPhrase(part, Vocab::MedalEarned, " ");
                AppendPhrase(line, part);
            }
            AddTextLine(b, key + "medals", view.medalsLabel, line);
        }
        AddTextLine(b, key + "narrative", "", RecField(*panel, "narrative"));
        if (!subject.empty())
            b.PopContext();

        // ---- the squad ----
        bool anySoldier = false;
        for (const WireRecord& rec : records) {
            if (RecField(rec, "kind") != "soldier")
                continue;
            if (!anySoldier) {
                b.BeginStop("squad");
                anySoldier = true;
            }
            const int index = atoi(RecField(rec, "index").c_str());
            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(RecField(rec, "name")));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            const std::string value = JoinParts(rec, {"rank", "class"});
            if (!value.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }
            // The info button for this slot — the loadout popup, which the game offers only to
            // the mouse (and to a gamepad's Y, for slot 0 alone).
            void* button = nullptr;
            for (const auto& entry : view.soldierButtons)
                if (entry.first == index)
                    button = entry.second;
            if (button) {
                vt.OnActivate = [index]() {
                    const GameUI::LadderMenuView fresh = GameUI::ReadLadderMenu(gScreen);
                    for (const auto& entry : fresh.soldierButtons)
                        if (entry.first == index)
                            GameUI::OpenLadderSoldierInfo(gScreen, entry.second);
                };
            }
            b.AddItem(ControlId::Structural(key + "soldier/" + std::to_string(index)), std::move(vt));
        }

        // ---- the actions ----
        const bool hasSave = !RecField(*panel, "hassave").empty();
        const bool deletable = !RecField(*panel, "deletable").empty();
        b.BeginStop("actions");
        b.StartRow();
        {
            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(
                NodeAnnouncement::Static(hasSave ? view.continueLabel : view.beginLabel));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            vt.OnActivate = [random, index = view.clickedIndex]() {
                GameUI::ActivateLadderRow(GameUI::ReadLadderMenu(gScreen), random, index);
                gLadderWords.Invalidate();
            };
            b.AddItem(ControlId::Structural(key + "continue"), std::move(vt));
        }
        if (hasSave) {
            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(view.abandonLabel));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            vt.OnActivate = []() {
                GameUI::LadderAbandon(gScreen);
                gLadderWords.Invalidate();
            };
            b.AddItem(ControlId::Structural(key + "abandon"), std::move(vt));
        }
        if (deletable) {
            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(view.deleteLabel));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            vt.OnActivate = []() {
                GameUI::LadderDelete(gScreen);
                gLadderWords.Invalidate();
            };
            b.AddItem(ControlId::Structural(key + "delete"), std::move(vt));
        }
        b.EndRow();
    }

    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string LadderMenuName()
{
    const std::string title = GameUI::ReadLadderMenu(gScreen).title;
    return title.empty() ? std::string(Vocab::ScreenLadderMenuFallback) : title;
}

// ---- the ladder difficulty page (UITLE_LadderDifficulty) ----
//
// The campaign difficulty page's shape: the four difficulty checkboxes are a RADIO GROUP (the
// checked one is the selection, which is what the landing resolves to), the bottom rows are the
// narrative-VO toggle and the read-only Ironman notice, and LAUNCH starts the ladder. Each row's
// description is its hover tooltip, on Space.

WireCache gLadderDiffWords(kTLEReaderClass, "DescribeLadderDifficulty");

void AddLadderSetupRow(GraphBuilder& b, const GameUI::OptionRow& row, const std::string& key,
    bool selected)
{
    ControlId id = ControlId::Referenced(row.handle, key);
    gFocus[id.StructuralKey] = FocusTarget{-1, row.handle, false};

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
        // LIVE: picking a difficulty unchecks the other three under us.
        vt.Announcements.back().Live = true;
        vt.StateText = [valueText]() { return valueText; };
    }
    if (row.disabled) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        // SetDisabled(true, reason) parks the why as the row's tooltip: "no changing history".
        const std::string reason = GameUI::ReadRowTooltip(row.handle);
        if (!reason.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(reason));
            vt.Announcements.back().Kind = AnnouncementKinds::Reason;
        }
    }
    vt.Announcements.emplace_back(NodeAnnouncement(
        [selected]() { return selected ? std::string(Vocab::Selected) : std::string(); }, false,
        AnnouncementKinds::Selected));
    vt.OnActivate = [handle = row.handle]() { GameUI::SendRowCommand(handle, GameUI::kCmdEnter); };
    vt.OnTooltip = RowTipAction(row.handle);
    b.AddItem(std::move(id), std::move(vt));
}

std::unique_ptr<GraphRender> BuildLadderDifficulty()
{
    const GameUI::LadderDifficultyView view = GameUI::ReadLadderDifficulty(gScreen);
    if (view.difficulties.empty())
        return nullptr;
    // BuildMenu ticks the default after spawning the rows; exactly one checked is the
    // readiness test, as on the campaign page.
    bool anyChecked = false;
    for (const GameUI::OptionRow& row : view.difficulties)
        anyChecked = anyChecked || row.checked;
    if (!anyChecked)
        return nullptr;

    GraphBuilder b;
    b.BeginStop("difficulty");
    b.PushContext(Vocab::GroupDifficulty);
    for (const GameUI::OptionRow& row : view.difficulties)
        AddLadderSetupRow(b, row, "ldiff/" + row.label, row.checked);
    b.PopContext();

    if (!view.options.empty()) {
        b.BeginStop("options");
        for (const GameUI::OptionRow& row : view.options)
            AddLadderSetupRow(b, row, "lopt/" + row.label, row.focused);
    }

    if (view.start.handle) {
        b.BeginStop("start");
        ControlId id = ControlId::Referenced(view.start.handle, "ldiff/start");
        gFocus[id.StructuralKey] = FocusTarget{-1, view.start.handle, false};
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(view.start.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement(
            [focused = view.start.focused]() {
                return focused ? std::string(Vocab::Selected) : std::string();
            },
            false, AnnouncementKinds::Selected));
        vt.OnActivate = [handle = view.start.handle]() { GameUI::ClickButton(handle); };
        vt.OnTooltip = PanelTooltipAction(view.start.handle);
        b.AddItem(std::move(id), std::move(vt));
    }
    return b.Build();
}

std::string LadderDifficultyName()
{
    const GameUI::LadderDifficultyView view = GameUI::ReadLadderDifficulty(gScreen);
    const std::vector<WireRecord>& records
        = gLadderDiffWords.Get({gScreen}, view.selectedDifficulty);
    return NameFromHeader(records, Vocab::ScreenLadderDifficultyFallback);
}

// ---- the soldier loadout popup (UITLEUnitStats) ----
//
// One soldier: a header node whose Left/Right ARE the game's Prev/Next soldier (the screen
// consumes every key, so the arrows reach it as an adjust on this node rather than as claims it
// would swallow), then the stats, the gear, and the abilities with their descriptions on Space.
// The soldier index is part of every identity, so stepping re-speaks the node under focus.

WireCache gUnitStatsWords(kTLEReaderClass, "DescribeUnitStats");

std::unique_ptr<GraphRender> BuildUnitStats()
{
    const GameUI::UnitStatsView view = GameUI::ReadUnitStats(gScreen);
    if (!view.screen || view.count <= 0)
        return nullptr;
    const std::vector<WireRecord>& records = gUnitStatsWords.Get({gScreen}, view.index);
    const WireRecord* header = FindRecord(records, "header");
    if (!header)
        return nullptr; // the companion is the only source of words here
    const std::string key = "units/" + std::to_string(view.index) + "/";

    GraphBuilder b;

    b.BeginStop("soldier");
    {
        NodeVtable vt;
        vt.Type = &SpinnerType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(RecField(*header, "name")));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        const std::string value = JoinParts(*header, {"rank", "class"});
        if (!value.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        if (view.count > 1) {
            vt.OnAdjust = [](int sign, bool) {
                GameUI::UnitStatsStep(gScreen, sign);
                gUnitStatsWords.Invalidate();
            };
        }
        b.AddItem(ControlId::Structural(key + "soldier"), std::move(vt));
    }

    bool inStats = false, inGear = false, inAbilities = false;
    int statIndex = 0, gearIndex = 0, abilityIndex = 0;
    for (const WireRecord& rec : records) {
        const std::string kind = RecField(rec, "kind");
        if (kind == "stat") {
            if (!inStats) {
                b.BeginStop("stats");
                inStats = true;
            }
            AddTextLine(b, key + "stat/" + std::to_string(statIndex++), RecField(rec, "label"),
                RecField(rec, "value"));
        } else if (kind == "gear" || kind == "pcs") {
            if (!inGear) {
                b.BeginStop("gear");
                inGear = true;
            }
            // Positional: an empty pocket is still a slot the screen draws, said as empty.
            const std::vector<std::string> items = RecFieldAll(rec, "item");
            std::string item = items.empty() ? std::string() : items.front();
            if (kind == "pcs" && item.empty())
                continue; // the PCS slot shows nothing when none is fitted
            if (item.empty())
                item = Vocab::EmptySlot;
            AddTextLine(b, key + "gear/" + std::to_string(gearIndex++),
                kind == "pcs" ? std::string(Vocab::PCSLabel) : RecField(rec, "label"), item);
        } else if (kind == "ability") {
            if (!inAbilities) {
                b.BeginStop("abilities");
                b.PushContext(RecField(*header, "subtitle"));
                inAbilities = true;
            }
            AddTextLine(b, key + "ability/" + std::to_string(abilityIndex++), "",
                RecField(rec, "name"), SpeakAction(RecField(rec, "desc")));
        }
    }
    if (inAbilities)
        b.PopContext();
    return b.Build();
}

std::string UnitStatsName()
{
    const std::string title = GameUI::ReadUnitStats(gScreen).title;
    return title.empty() ? std::string(Vocab::ScreenUnitStatsFallback) : title;
}

} // namespace

// ---- the recipes this file defines (probe ORDER is kRecipes in menu_navigator.cpp) ----

// The loadout popup is pushed over the Legacy Operations menu (and over the skirmish menu); it
// pushes nothing itself.
const Recipe kUnitStatsRecipe{
    "soldier loadout", ProbeFocused<GameUI::FindUnitStatsScreen>, BuildUnitStats, UnitStatsName};
// The difficulty page over the Legacy Operations menu; it raises nothing above itself.
const Recipe kLadderDifficultyRecipe{"ladder difficulty",
    ProbeFocused<GameUI::FindLadderDifficultyScreen>, BuildLadderDifficulty, LadderDifficultyName};
// The Legacy Operations menu over the hub, under the two above and the confirm dialogs.
const Recipe kLadderMenuRecipe{"legacy operations", ProbeFocused<GameUI::FindLadderMenuScreen>,
    BuildLadderMenu, LadderMenuName};

} // namespace Recipes
} // namespace MenuNavigator
