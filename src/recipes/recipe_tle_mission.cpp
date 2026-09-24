// The Tactical Legacy Pack's MISSION-SIDE recipes: the scoring dialog before the first turn, the
// upgrade picker and the squad review between ladder missions, the medal, medal-progress and end
// screens after them, the challenge result, and the challenge event notice — every screen the
// pack pushes over UITacticalHUD that a player ACTS on. docs/tle-mission-recon.md for the census
// and the two screens that deliberately get no recipe (the score HUDs), and docs/surfaces/
// tactical.md for the rules these inherit from the tactical family.
//
// Every one of these is a modal over a mission the tactical gate already reads as `blocked` (an
// unknown screen above the HUD), which is what detaches the ambient HUD layer and lets these probe
// — the same arrangement the mission bookends and the loot receipt rely on. None has a UIList, so
// none writes focus back through SetListIndex; the upgrade picker writes it back through the
// screen's own SelectColumn instead.
//
// Words: labels off the instance (game_ui_tle_mission.cpp), figures from X2AccessLadderReader,
// the same split the hub uses. The wire caches are keyed on the screen pointer: every one of these
// screens is built once at OnInit and never repopulated, except the squad review, which pages —
// its key carries the page.

#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "game_ui.h"
#include "game_ui_tle_mission.h"
#include "log.h"
#include "tts.h"
#include "vocab.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MenuNavigator {
namespace Recipes {
namespace {

constexpr const char* kLadderReaderClass = "X2AccessLadderReader";

// ---- the pieces every screen here is assembled from ----

// A read-only line. Returns the id so a caller can hand it to SetStart; invalid when the text
// was empty, because an unlabelled node is a ghost the player can land on and hear nothing from.
ControlId AddLine(GraphBuilder& b, const std::string& id, const std::string& text)
{
    if (text.empty())
        return ControlId();
    ControlId node = ControlId::Structural(id);
    b.AddLabel(node, [text]() { return text; });
    return node;
}

// One label/value row as a node. The label carries the game's own trailing ':' where it has one
// and the two are joined by the announcer's phrase rule, never by us. A row whose label is
// missing degrades to the value alone; a row with neither adds nothing and answers invalid.
ControlId AddStat(GraphBuilder& b, const std::string& id, const std::string& label,
    const std::string& value)
{
    if (label.empty() && value.empty())
        return ControlId();
    NodeVtable vt;
    vt.Type = &TextType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(label.empty() ? value : label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (!label.empty() && !value.empty()) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    }
    ControlId node = ControlId::Structural(id);
    b.AddItem(node, std::move(vt));
    return node;
}

// A button node whose activation is a zero-parameter verb on the screen — the shape of every
// Continue in this family (the nav help's own button, whose label lives in the movie; Enter and
// Space reach the same verb through the screen's OnUnrealCommand).
void AddVerbButton(GraphBuilder& b, const std::string& id, const std::string& label,
    const char* verb)
{
    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    vt.OnActivate = [verb]() { GameUI::InvokeScreenVerb(gScreen, verb); };
    b.AddItem(ControlId::Structural(id), std::move(vt));
}

// A button node over a real UIButton the game retains a pointer to: the game's own Click(),
// which runs the delegate only while the button is enabled and visible — so a greyed button is
// spoken "unavailable" and its Enter is refused by the game itself.
void AddClickButton(GraphBuilder& b, const std::string& key, const GameUI::MenuItem& btn,
    const std::string& fallbackLabel)
{
    if (!btn.handle || (btn.label.empty() && fallbackLabel.empty()))
        return; // an unlabelled button is a ghost node — the empty-answer trap, refused here
    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(
        NodeAnnouncement::Static(btn.label.empty() ? fallbackLabel : btn.label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (btn.disabled) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
    } else {
        vt.OnActivate = [handle = btn.handle]() { GameUI::ClickButton(handle); };
    }
    vt.OnTooltip = PanelTooltipAction(btn.handle);
    b.AddItem(ControlId::Referenced(btn.handle, key), std::move(vt));
}

// The title block four ladder screens share (the companion's `header` record): MISSION COMPLETE,
// the ladder's name, then "MISSION: r/size" — the screen's own SetScreenTitle operands, joined by
// the phrase rule.
std::string TitleBlock(const std::vector<WireRecord>& records)
{
    const WireRecord* header = FindRecord(records, "header");
    if (!header)
        return std::string();
    std::string name = RecField(*header, "title");
    AppendPhrase(name, RecField(*header, "name"));
    std::string mission = RecField(*header, "label");
    AppendPhrase(mission, RecField(*header, "count"), " ");
    AppendPhrase(name, mission);
    return name;
}

// ---- the scoring dialogs (UILadderModeScoringDialog, UIChallengeModeScoringDialog) ----
//
// The modal before the first turn: a table of scoring categories with their maximum points, the
// total, two lines of explanation and START MISSION / START CHALLENGE. TWO STOPS: the table as
// label/value rows (one fact per row, so this is a list rather than a sheet — there is one
// column of numbers), then the button. The game's Enter reaches nothing here (the dialog handles
// no keys; the button is mouse-only in vanilla), so the button node is the whole way out.

WireCache gScoringWords(kLadderReaderClass, "DescribeScoring");

std::unique_ptr<GraphRender> BuildScoringDialog()
{
    const GameUI::ScoringDialogView view = GameUI::ReadScoringDialog(gScreen);
    const std::vector<WireRecord>& words = gScoringWords.Get({gScreen});

    GraphBuilder b;
    b.BeginStop("scoring");
    b.PushContext("", "", /*positions*/ false);
    ControlId start;
    int n = 0;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "row")
            continue;
        // Keyed on the label, never the loop index: a category the manager scores zero for is
        // not drawn and must not renumber the ones after it.
        ControlId node = AddStat(b, "tlescore/row/" + RecField(rec, "label"),
            RecField(rec, "label"), RecField(rec, "points"));
        if (node.IsValid() && !start.IsValid())
            start = node;
        n++;
    }
    if (const WireRecord* total = FindRecord(words, "total")) {
        ControlId node = AddStat(b, "tlescore/total", RecField(*total, "label"),
            RecField(*total, "value"));
        if (node.IsValid() && !start.IsValid())
            start = node;
    }
    AddLine(b, "tlescore/desc1", view.description1);
    AddLine(b, "tlescore/desc2", view.description2);
    if (n == 0 && view.description1.empty()) {
        // Recognised the dialog and got no words — said out loud, because the player still has
        // to press the button to start the mission.
        AddLine(b, "tlescore/empty", Vocab::SummaryNoText);
    }
    b.PopContext();
    if (start.IsValid())
        b.SetStart(start);

    b.BeginStop("actions");
    AddClickButton(b, "tlescore/continue", view.continueButton, Vocab::SummaryContinueFallback());
    return b.Build();
}

std::string ScoringDialogName()
{
    const GameUI::ScoringDialogView view = GameUI::ReadScoringDialog(gScreen);
    return view.header.empty() ? std::string(Vocab::ScreenScoringFallback) : view.header;
}

// ---- the ladder upgrade picker (UILadderUpgradeScreen) ----
//
// Two cards, one stop, drawn as a row: each card is its title, its paragraph, the per-class
// gain/replace lines and the EQUIP UPGRADE word, and Enter is the card's own button (its click
// delegate IS the choice — ChooseUpgrade0/1). The game highlights one column and its Left/Right
// is SelectColumn, so focus write-back goes through that verb and the highlighted card carries
// the Selected part. The paragraph goes on Space rather than into the readout: it is the card's
// side text, and the lines are the facts a choice turns on.

WireCache gUpgradeWords(kLadderReaderClass, "DescribeUpgrade");

const WireRecord* FindCard(const std::vector<WireRecord>& records, int index)
{
    const std::string want = std::to_string(index);
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == "card" && RecField(rec, "index") == want)
            return &rec;
    }
    return nullptr;
}

std::unique_ptr<GraphRender> BuildLadderUpgrade()
{
    const GameUI::LadderUpgradeView view = GameUI::ReadLadderUpgrade(gScreen);
    if (!view.buttons[0].handle && !view.buttons[1].handle)
        return nullptr; // still spawning
    const std::vector<WireRecord>& words = gUpgradeWords.Get({gScreen});

    GraphBuilder b;
    b.BeginStop("upgrades");
    b.PushContext(view.chooseLabel, "", /*positions*/ true);
    b.StartRow();
    for (int i = 0; i < 2; i++) {
        const GameUI::MenuItem& btn = view.buttons[i];
        if (!btn.handle)
            continue;
        const WireRecord* card = FindCard(words, i);

        ControlId id = ControlId::Referenced(btn.handle, "tleupgrade/" + std::to_string(i));
        gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false,
            [i]() { GameUI::SelectLadderUpgradeColumn(gScreen, i); }};

        NodeVtable vt;
        vt.Type = &ButtonType();
        std::string title = card ? RecField(*card, "title") : std::string();
        if (title.empty())
            title = view.chooseLabel.empty() ? std::string(Vocab::ScreenLadderUpgradeFallback)
                                             : view.chooseLabel;
        vt.Announcements.emplace_back(NodeAnnouncement::Static(title));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;

        // The gain / replace lines, then the button's own word — the order the card draws them.
        std::string value;
        if (card) {
            for (const std::string& line : RecFieldAll(*card, "line"))
                AppendPhrase(value, line);
        }
        AppendPhrase(value, view.equipLabel);
        if (!value.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        vt.Announcements.emplace_back(NodeAnnouncement(
            [sel = (i == view.selectedColumn)]() {
                return sel ? std::string(Vocab::Selected) : std::string();
            },
            false, AnnouncementKinds::Selected));
        vt.OnActivate = [handle = btn.handle]() { GameUI::ClickButton(handle); };
        const std::string desc = card ? RecField(*card, "desc") : std::string();
        if (!desc.empty())
            vt.OnTooltip = [desc]() { TTS::Speak(desc); };
        b.AddItem(std::move(id), std::move(vt));
    }
    b.EndRow();
    b.PopContext();
    return b.Build();
}

std::string LadderUpgradeName()
{
    const GameUI::LadderUpgradeView view = GameUI::ReadLadderUpgrade(gScreen);
    std::string name = TitleBlock(gUpgradeWords.Get({gScreen}));
    AppendPhrase(name, view.chooseLabel);
    return name.empty() ? std::string(Vocab::ScreenLadderUpgradeFallback) : name;
}

// ---- the ladder squad review (UILadderSoldierInfo) ----
//
// Three soldiers a page. ONE STOP for the page's soldiers — each soldier a context (name, class,
// rank, and the header the game colours: RANK UP / NEW SOLDIER / KIA) holding the info button,
// then the NEW ABILITIES and NEW ITEMS the screen lists under its two headings — and an ACTIONS
// stop for paging and NEXT MISSION. The page is the game's own: Prev / Next call the buttons'
// delegates, exactly what the game's own Left/Right do, and the wire cache is keyed on the page
// so a turned page re-asks. Mirrors the screen: a KIA soldier lists nothing.

WireCache gSoldierWords(kLadderReaderClass, "DescribeSoldierPage");

std::unique_ptr<GraphRender> BuildLadderSoldiers()
{
    const GameUI::LadderSoldierInfoView view = GameUI::ReadLadderSoldierInfo(gScreen);
    const std::vector<WireRecord>& words = gSoldierWords.Get({gScreen}, view.startIndex);

    GraphBuilder b;
    b.BeginStop("soldiers");
    ControlId start;
    int shown = 0;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "soldier")
            continue;
        const std::string slot = RecField(rec, "slot");
        const int slotIndex = atoi(slot.c_str());
        const std::string prefix = "tlesquad/" + std::to_string(view.startIndex) + "/" + slot;

        // The soldier's head: what SetSoldierImage and SetSoldierHeader draw, as one context.
        std::string head = RecField(rec, "name");
        AppendPhrase(head, RecField(rec, "rank"));
        AppendPhrase(head, RecField(rec, "class"));
        AppendPhrase(head, RecField(rec, "header"));
        b.PushContext(head, "", /*positions*/ false);
        shown++;

        int nodesInSoldier = 0;

        // The info button, where the game draws it — only on a living soldier's portrait.
        if (slotIndex >= 0 && slotIndex < 3 && view.infoButtons[slotIndex]) {
            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(
                view.helpSoldierInfo.empty() ? std::string(Vocab::SoldierInfoFallback())
                                             : view.helpSoldierInfo));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            vt.OnActivate = [slotIndex]() { GameUI::OpenLadderSoldierStats(gScreen, slotIndex); };
            ControlId node = ControlId::Referenced(view.infoButtons[slotIndex], prefix + "/info");
            if (!start.IsValid())
                start = node;
            b.AddItem(std::move(node), std::move(vt));
            nodesInSoldier++;
        }

        // The two columns, each under the screen's own heading — a heading only when the column
        // has something in it, the way a KIA soldier's empty columns draw their KIA header alone.
        int k = 0;
        bool opened = false;
        for (const WireRecord& ab : words) {
            if (RecField(ab, "kind") != "ability" || RecField(ab, "slot") != slot)
                continue;
            if (!opened) {
                b.PushContext(view.newAbilities, "", /*positions*/ true);
                opened = true;
            }
            ControlId node = AddLine(
                b, prefix + "/ability/" + std::to_string(k++) + "/" + RecField(ab, "name"),
                RecField(ab, "name"));
            if (node.IsValid() && !start.IsValid())
                start = node;
            if (node.IsValid())
                nodesInSoldier++;
        }
        if (opened)
            b.PopContext();

        k = 0;
        opened = false;
        for (const WireRecord& it : words) {
            if (RecField(it, "kind") != "item" || RecField(it, "slot") != slot)
                continue;
            if (!opened) {
                b.PushContext(view.newItems, "", /*positions*/ true);
                opened = true;
            }
            ControlId node = AddLine(
                b, prefix + "/item/" + std::to_string(k++) + "/" + RecField(it, "name"),
                RecField(it, "name"));
            if (node.IsValid() && !start.IsValid())
                start = node;
            if (node.IsValid())
                nodesInSoldier++;
        }
        if (opened)
            b.PopContext();

        // A head with nothing under it — a KIA soldier draws its header and two empty columns —
        // still needs a node to land on, or the context and the soldier would be unreachable.
        if (nodesInSoldier == 0) {
            // The header word alone (KIA), not the whole head — the context already said that.
            std::string alone = RecField(rec, "header");
            if (alone.empty())
                alone = head;
            ControlId node = AddLine(b, prefix + "/head", alone);
            if (node.IsValid() && !start.IsValid())
                start = node;
        }
        b.PopContext();
    }
    if (shown == 0) {
        // Recognised the screen before its page was described (the companion missing, or the
        // arrays not yet built): the player still has to leave it, so say so.
        AddLine(b, "tlesquad/empty", Vocab::SummaryNoText);
    }

    b.BeginStop("actions");
    b.StartRow();
    AddClickButton(b, "tlesquad/prev", view.prevButton, view.helpPrevPage);
    AddClickButton(b, "tlesquad/next", view.nextButton, view.helpNextPage);
    AddVerbButton(b, "tlesquad/nextmission",
        view.nextMission.empty() ? std::string(Vocab::SummaryContinueFallback()) : view.nextMission,
        "NextMissionButtonClicked");
    b.EndRow();

    if (start.IsValid())
        b.SetStart(start);
    return b.Build();
}

std::string LadderSoldiersName()
{
    const GameUI::LadderSoldierInfoView view = GameUI::ReadLadderSoldierInfo(gScreen);
    std::string name = TitleBlock(gSoldierWords.Get({gScreen}, view.startIndex));
    return name.empty() ? std::string(Vocab::ScreenLadderSquadFallback) : name;
}

// ---- the medal screen (UITLELadderMedalScreen) ----
//
// The finished ladder's medal: the MEDAL ACHIEVED line (filled by the screen itself), the
// congratulation, TOTAL SCORE and MEDAL SCORE, then the rewards block when there is one — the next
// ladder unlocked and the cosmetic content, greyed when a previous run had already earned them.
// Read-only lines and Continue.

WireCache gMedalWords(kLadderReaderClass, "DescribeMedal");

std::unique_ptr<GraphRender> BuildLadderMedal()
{
    const GameUI::LadderMedalView view = GameUI::ReadLadderMedal(gScreen);
    const std::vector<WireRecord>& words = gMedalWords.Get({gScreen});
    const WireRecord* medal = FindRecord(words, "medal");
    const WireRecord* rewards = FindRecord(words, "rewards");

    GraphBuilder b;
    b.BeginStop("medal");
    b.PushContext("", "", /*positions*/ false);
    ControlId start = AddLine(b, "tlemedal/unlocked", view.medalUnlocked);
    ControlId node = AddLine(b, "tlemedal/victory", view.victory);
    if (!start.IsValid())
        start = node;
    if (medal) {
        node = AddStat(b, "tlemedal/score", view.scoreLabel, RecField(*medal, "score"));
        if (!start.IsValid())
            start = node;
        node = AddStat(b, "tlemedal/threshold", view.medalScoreLabel, RecField(*medal, "threshold"));
        if (!start.IsValid())
            start = node;
    }
    if (rewards) {
        b.PushContext(RecField(*rewards, "label"), "", /*positions*/ false);
        std::string next = RecField(*rewards, "nextlabel");
        AppendPhrase(next, RecField(*rewards, "nextname"));
        std::string unlock = RecField(*rewards, "unlock");
        AppendPhrase(unlock, RecField(*rewards, "unlockdesc"));
        // Drawn greyed when the profile had these already: the game's way of saying "not new".
        const bool greyed = !RecFieldAll(*rewards, "greyed").empty();
        if (greyed) {
            AppendPhrase(next, Vocab::RewardAlreadyUnlocked);
            AppendPhrase(unlock, Vocab::RewardAlreadyUnlocked);
        }
        AddLine(b, "tlemedal/next", next);
        AddLine(b, "tlemedal/unlock", unlock);
        b.PopContext();
    }
    if (!start.IsValid())
        start = AddLine(b, "tlemedal/empty", Vocab::SummaryNoText);
    b.PopContext();
    if (start.IsValid())
        b.SetStart(start);

    b.BeginStop("actions");
    AddVerbButton(b, "tlemedal/continue", Vocab::SummaryContinueFallback(), "ContinueButtonClicked");
    return b.Build();
}

std::string LadderMedalName()
{
    const std::string name = TitleBlock(gMedalWords.Get({gScreen}));
    return name.empty() ? std::string(Vocab::ScreenLadderMedalFallback) : name;
}

// ---- the medal progress screen (UITLELadderMedalProgressScreen) ----
//
// Between missions of a narrative ladder: the timeline (the count says where on it), the three
// medal markers with their thresholds, and the meter — score over the gold threshold — as a
// percentage, because the game draws it as a bar with no figure. Read-only lines and Continue.

WireCache gProgressWords(kLadderReaderClass, "DescribeMedalProgress");

std::unique_ptr<GraphRender> BuildLadderMedalProgress()
{
    const std::vector<WireRecord>& words = gProgressWords.Get({gScreen});
    const WireRecord* progress = FindRecord(words, "progress");
    const WireRecord* meter = FindRecord(words, "meter");

    GraphBuilder b;
    b.BeginStop("progress");
    b.PushContext("", "", /*positions*/ false);
    ControlId start;
    if (progress) {
        // "MISSION PROGRESS:" then the count the header already carries, so the node stands on
        // its own after the player has arrowed away from the screen name.
        std::string count = RecField(*progress, "rung");
        AppendPhrase(count, RecField(*progress, "size"), "/");
        start = AddStat(b, "tleprogress/mission", RecField(*progress, "label"), count);
    }
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "marker")
            continue;
        std::string value = RecField(rec, "threshold");
        if (!RecFieldAll(rec, "reached").empty())
            AppendPhrase(value, Vocab::MedalEarned);
        ControlId node = AddStat(
            b, "tleprogress/marker/" + RecField(rec, "index"), RecField(rec, "label"), value);
        if (!start.IsValid())
            start = node;
    }
    if (meter) {
        std::string value = RecField(*meter, "score");
        const std::string pct = RecField(*meter, "pct");
        if (!pct.empty())
            AppendPhrase(value, Vocab::Percent(atoi(pct.c_str())));
        ControlId node = AddStat(b, "tleprogress/meter",
            progress ? RecField(*progress, "medallabel") : std::string(), value);
        if (!start.IsValid())
            start = node;
    }
    if (!start.IsValid())
        start = AddLine(b, "tleprogress/empty", Vocab::SummaryNoText);
    b.PopContext();
    if (start.IsValid())
        b.SetStart(start);

    b.BeginStop("actions");
    AddVerbButton(
        b, "tleprogress/continue", Vocab::SummaryContinueFallback(), "ContinueButtonClicked");
    return b.Build();
}

std::string LadderMedalProgressName()
{
    const std::string name = TitleBlock(gProgressWords.Get({gScreen}));
    return name.empty() ? std::string(Vocab::ScreenMedalProgressFallback) : name;
}

// ---- the end screen (UITLELadderEndScreen) ----
//
// The whole run's six statistics — captions off the instance, values off the analytics state —
// and the congratulation. Read-only lines and Continue, which also shuttles back to the menu.

WireCache gEndWords(kLadderReaderClass, "DescribeEndStats");

std::unique_ptr<GraphRender> BuildLadderEnd()
{
    const GameUI::LadderEndView view = GameUI::ReadLadderEnd(gScreen);
    const std::vector<WireRecord>& words = gEndWords.Get({gScreen});

    GraphBuilder b;
    b.BeginStop("stats");
    b.PushContext("", "", /*positions*/ false);
    ControlId start;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "stat")
            continue;
        const int i = atoi(RecField(rec, "index").c_str());
        if (i < 0 || i >= 6)
            continue;
        // Keyed on the caption: an index with no caption in this language draws nothing.
        ControlId node = AddStat(
            b, "tleend/stat/" + view.statLabels[i], view.statLabels[i], RecField(rec, "value"));
        if (!start.IsValid())
            start = node;
    }
    ControlId desc = AddLine(b, "tleend/description", view.description);
    if (!start.IsValid())
        start = desc;
    if (!start.IsValid())
        start = AddLine(b, "tleend/empty", Vocab::SummaryNoText);
    b.PopContext();
    if (start.IsValid())
        b.SetStart(start);

    b.BeginStop("actions");
    AddVerbButton(b, "tleend/continue", Vocab::SummaryContinueFallback(), "ContinueButtonClicked");
    return b.Build();
}

std::string LadderEndName()
{
    const std::string name = TitleBlock(gEndWords.Get({gScreen}));
    return name.empty() ? std::string(Vocab::ScreenLadderEndFallback) : name;
}

// ---- the challenge result (UIChallengePostScreen) ----
//
// The header (or the failure header), the player's name and the time, TOTAL SCORE, the maximum,
// the description; then Continue and — for a networked challenge — LEADERBOARD, which opens
// UIChallengeLeaderboards (no recipe of its own; a separate entry). Enter and Space are Continue
// through the screen's own keys, so the button node is the one that matters for parity.

WireCache gPostWords(kLadderReaderClass, "DescribeChallengePost");

std::unique_ptr<GraphRender> BuildChallengePost()
{
    const GameUI::ChallengePostView view = GameUI::ReadChallengePost(gScreen);
    const std::vector<WireRecord>& words = gPostWords.Get({gScreen});
    const WireRecord* result = FindRecord(words, "result");

    GraphBuilder b;
    b.BeginStop("result");
    b.PushContext("", "", /*positions*/ false);
    ControlId start;
    if (result) {
        std::string who = RecField(*result, "gamertag");
        AppendPhrase(who, RecField(*result, "time"));
        start = AddLine(b, "tlepost/who", who);
        ControlId node = AddStat(b, "tlepost/score", view.scoreLabel, RecField(*result, "score"));
        if (!start.IsValid())
            start = node;
        node = AddStat(
            b, "tlepost/max", RecField(*result, "maxlabel"), RecField(*result, "max"));
        if (!start.IsValid())
            start = node;
    }
    ControlId desc = AddLine(b, "tlepost/description", view.description);
    if (!start.IsValid())
        start = desc;
    if (!start.IsValid())
        start = AddLine(b, "tlepost/empty", Vocab::SummaryNoText);
    b.PopContext();
    if (start.IsValid())
        b.SetStart(start);

    b.BeginStop("actions");
    b.StartRow();
    AddClickButton(b, "tlepost/continue", view.continueButton, Vocab::SummaryContinueFallback());
    if (view.leaderboardButton) {
        GameUI::MenuItem leaderboard;
        leaderboard.handle = view.leaderboardButton;
        leaderboard.label = view.leaderboardLabel;
        AddClickButton(b, "tlepost/leaderboard", leaderboard, view.leaderboardLabel);
    }
    b.EndRow();
    return b.Build();
}

std::string ChallengePostName()
{
    const GameUI::ChallengePostView view = GameUI::ReadChallengePost(gScreen);
    return view.header.empty() ? std::string(Vocab::ScreenChallengePostFallback) : view.header;
}

// ---- the challenge event notice (UIChallengeModeEventNotify) ----
//
// A networked challenge's small window: one text block and three buttons, all retained, no
// companion. Escape, Enter and Space all close it in the game; Previous / Next page through the
// pending events and are greyed by the game when there is nothing to page to.

std::unique_ptr<GraphRender> BuildChallengeEvent()
{
    const GameUI::ChallengeEventView view = GameUI::ReadChallengeEvent(gScreen);

    GraphBuilder b;
    b.BeginStop("event");
    b.PushContext("", "", /*positions*/ false);
    ControlId start = AddLine(b, "tleevent/body", view.body);
    if (!start.IsValid())
        start = AddLine(b, "tleevent/empty", Vocab::SummaryNoText);
    b.PopContext();
    if (start.IsValid())
        b.SetStart(start);

    b.BeginStop("actions");
    b.StartRow();
    AddClickButton(b, "tleevent/previous", view.previousButton, std::string());
    AddClickButton(b, "tleevent/next", view.nextButton, std::string());
    AddClickButton(b, "tleevent/close", view.closeButton, Vocab::PopupDismiss);
    b.EndRow();
    return b.Build();
}

std::string ChallengeEventName()
{
    const GameUI::ChallengeEventView view = GameUI::ReadChallengeEvent(gScreen);
    return view.title.empty() ? std::string(Vocab::ScreenChallengeEventFallback) : view.title;
}

} // namespace

// ---- the score HUD's stop for the tactical HUD graph ----
//
// The two score HUDs (UIChallengeModeHUD, UILadderModeHUD) get no recipe — docs/tle-mission-
// recon.md §1 — but what they draw is on screen for the whole mission: the running score, and in a
// challenge the countdown. This adds them to the ambient HUD graph as one trailing stop, one node
// each, so Tab reaches them the way it reaches the objectives. Uncached: both change under a held
// pointer set. Answers false and adds nothing when neither HUD is up, which is every mission that
// is not a ladder or a challenge. Called from BuildTacticalHud (recipe_tactical.cpp).
bool AddTLEHudStop(GraphBuilder& b)
{
    const std::vector<WireRecord> words = FetchCompanionRecords(kLadderReaderClass, "DescribeHudScore");
    const WireRecord* score = FindRecord(words, "score");
    if (!score)
        return false;
    b.BeginStop("score");
    b.PushContext(RecField(*score, "label"), "", /*positions*/ false);
    AddStat(b, "thud/tle/score", RecField(*score, "label"), RecField(*score, "value"));
    if (const WireRecord* timer = FindRecord(words, "timer"))
        AddStat(b, "thud/tle/timer", RecField(*timer, "label"), RecField(*timer, "value"));
    b.PopContext();
    return true;
}

// ---- the recipes this file defines ----
//
// ⛔ THE PROBE ORDER IS NOT HERE — it is the kRecipes literal in menu_navigator.cpp. Each
// definition keeps the sentence that argues where in that order it belongs.

// ABOVE the ambient tactical recipe, all of them: pushed over UITacticalHUD while the map is
// loaded, read by the gate as `blocked`, which detaches the HUD layer and lets these probe. Below
// every modal a mission can still raise (the dialog, the pause menu — both listed earlier).
//
// Among themselves they are SEQUENTIAL and never stacked: the scoring dialog pops before the first
// turn; the post-mission chain runs upgrade → squad review → medal progress (or medal) → end, each
// popped before the next is pushed (X2TacticalGameRuleset's bWaitingForMissionSummary loop); the
// challenge result stands alone. So their relative order is free, and they are grouped by
// meaning: the dialog that opens a mission first, then the ladder's post-mission chain in the
// order the player meets it, then the two challenge screens.
const Recipe kScoringDialogRecipe{"mission scoring", ProbeFocused<GameUI::FindScoringDialog>,
    BuildScoringDialog, ScoringDialogName};

const Recipe kLadderUpgradeRecipe{"ladder upgrade",
    ProbeFocused<GameUI::FindLadderUpgradeScreen>, BuildLadderUpgrade, LadderUpgradeName};

// The squad review pushes UITLEUnitStats over itself (the info buttons) — that recipe, the shell
// family's, must be listed ABOVE this one.
const Recipe kLadderSoldiersRecipe{"ladder squad",
    ProbeFocused<GameUI::FindLadderSoldierInfoScreen>, BuildLadderSoldiers, LadderSoldiersName};

const Recipe kLadderMedalProgressRecipe{"medal progress",
    ProbeFocused<GameUI::FindLadderMedalProgressScreen>, BuildLadderMedalProgress,
    LadderMedalProgressName};

const Recipe kLadderMedalRecipe{"ladder medal", ProbeFocused<GameUI::FindLadderMedalScreen>,
    BuildLadderMedal, LadderMedalName};

const Recipe kLadderEndRecipe{"ladder end", ProbeFocused<GameUI::FindLadderEndScreen>,
    BuildLadderEnd, LadderEndName};

// The challenge result pushes UIChallengeLeaderboards over itself (no recipe yet); the event
// notice is pushed over the HUD mid-mission and over nothing else of ours.
const Recipe kChallengePostRecipe{"challenge result",
    ProbeFocused<GameUI::FindChallengePostScreen>, BuildChallengePost, ChallengePostName};

const Recipe kChallengeEventRecipe{"challenge event",
    ProbeFocused<GameUI::FindChallengeEventScreen>, BuildChallengeEvent, ChallengeEventName};

} // namespace Recipes
} // namespace MenuNavigator
