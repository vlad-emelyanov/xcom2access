// Strategy-layer recipes: squad select, the armory loadout, the geoscape's mission tray and the
// popup family — docs/surfaces/strategy.md.

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

// A card's words, matched on its POSITION in m_kSlotList — the one index both halves walk in the
// same order, native through TakeListItemsRaw and the companion through GetItem(i).
//
// ⛔ NOT on SlotIndex, which is what this did until 2026-08-23, and not because push order is
// safe: it is because SlotIndex IS NOT UNIQUE. UpdateData returns at `if(bDisabled) return;`
// one line BEFORE it assigns SlotIndex, so every locked "increase squad size" card keeps the
// default 0. Measured live, six cards read 0,1,0,2,3,0 — the two locked cards collided with the
// genuine squad-slot-0 soldier, who took the FIRST match and spent the whole screen announcing
// herself as "увеличьте размер отряда в партизанской школе". The lesson is narrower than "never
// by push order" and outranks it: a field both halves read off the same object is only a KEY if
// the game guarantees it is distinct, and this one is not even always written.
//
// The original concern still holds and is what makes the LIST POSITION the right answer rather
// than a running counter: the companion skips a list entry that is not a UISquadSelect_ListItem
// (`if (Card == none) continue;`) while native walks every item, so a skip must leave a HOLE —
// caught by the warning at the call site — never shift every later card onto the wrong soldier.
// On this screen the wrong soldier owns a DISMISS button.
const WireRecord* FindCardByPosition(const std::vector<const WireRecord*>& cards, int listIndex)
{
    if (listIndex < 0)
        return nullptr;
    for (const WireRecord* rec : cards) {
        const std::string text = RecField(*rec, "pos");
        if (!text.empty() && atoi(text.c_str()) == listIndex)
            return rec;
    }
    return nullptr;
}

std::unique_ptr<GraphRender> BuildSquadSelect()
{
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
        const WireRecord* cardRec = FindCardByPosition(cards, static_cast<int>(i));
        if (!cardRec && !cards.empty()) {
            // Both halves walk the same list in the same order, so a miss means the companion
            // skipped that entry — the hole the position key exists to leave visible. Say so once
            // instead of quietly reading the card as an unnamed soldier.
            static int sWarned = 0;
            if (sWarned++ < 3)
                LOG_WARN("MenuNavigator: squad select — no card record at position %zu of %zu"
                    " (slot %d)", i, cards.size(), slot.slotIndex);
        }
        const WireRecord& card = cardRec ? *cardRec : kEmptyRecord;
        const int visual = static_cast<int>(i);

        // A pure-structure parent: never navigable, announced when focus enters the card from
        // outside and silent while moving within it. This is where the soldier is named, so the
        // controls under it can be as short as "primary weapon, Shotgun" — and with Left/Right
        // crossing cards, it is what makes each step across say who you landed on.
        b.PushContext(SquadCardContext(card, *missionRec, visual));

        // An empty or locked card holds no controls a sighted player can use either; its one
        // node IS the card, and activating it opens the soldier picker.
        //
        // ⛔ Emptiness is the COMPANION's answer (the unit ref behind the card), never
        // `slot.controls.empty()`. An unmanned card keeps every widget it was built with — live
        // 2026-08-23 the "ВЫБРАТЬ БОЙЦА" card still owned a two-item UtilitySlots list under a
        // hidden parent — so native reports controls for it and this branch never fired. The card
        // was built as an ordinary one whose first node is EDIT, and Edit on an empty slot falls
        // straight through `if( XComHQ.Squad[...].ObjectID > 0 )` and does nothing: the player
        // heard "ВЫБРАТЬ БОЙЦА, ИЗМЕНИТЬ" and could not put a soldier on the mission at all.
        // A widget tree cannot answer "is anyone standing here"; only the unit ref can.
        const bool cardEmpty = !RecField(card, "empty").empty();
        if (slot.controls.empty() || cardEmpty) {
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
                        // OnClickedSelectUnitButton — the empty card's own click, and the only
                        // handler that opens UIPersonnel_SquadSelect. Edit sat here and was a
                        // no-op on an unmanned slot; see the note above the branch.
                        pick.button = GameUI::SquadButton::SelectUnit;
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

// ---- after action recipe (UIAfterAction) ----
//
// The post-mission squad walk-up — squad select's sibling in layout and in division of labor
// (docs/after-action-recon.md §6): soldier panels side by side whose words all went into Flash,
// a mission block no focus ever visits, and the screen's main verb (Continue) reachable in
// vanilla only as Escape's side effect or a mouse click on a nav-help button. This is also the
// screen where promotions are SPENT, so the promote node is the whole reason a blind player
// keeps the levels a mission earned. Native reads which card, m_bCanPromote / m_bKIA and the
// two buttons; the companion (X2AccessAfterActionReader) re-derives every word from the
// UnitReference each row retains.

constexpr const char* kAfterActionReaderClass = "X2AccessAfterActionReader";

// A panel's spoken context line — read when focus ENTERS the card, quiet while moving within
// it. Reading order is the panel's own top-to-bottom order: the colored status header (with
// days out), who this is, the two label+value pairs the panel draws, the bars, the markers.
std::string AfterActionCardContext(const WireRecord& card, const WireRecord& mission,
    int visualIndex)
{
    std::string line = JoinParts(card, {"status", "days", "rank", "name", "nick", "class"});
    // ЗАДАНИЙ 4 / УБИТО 5 — the labels are class defaults, identical for every panel, so they
    // ride on the MISSION record (strategy.md's label rule) and join their values here.
    const std::string missions = RecField(card, "missions");
    if (!missions.empty())
        line = JoinPhrases(line, JoinPhrases(RecField(mission, "missionslabel"), missions));
    const std::string kills = RecField(card, "kills");
    if (!kills.empty())
        line = JoinPhrases(line, JoinPhrases(RecField(mission, "killslabel"), kills));
    line = JoinPhrases(line, JoinParts(card, {"hp", "will", "psi", "cohesion", "bond"}));
    if (line.empty())
        line = Vocab::SoldierFallback + (" " + std::to_string(visualIndex + 1));
    return line;
}

std::unique_ptr<GraphRender> BuildAfterAction()
{
    GameUI::AfterActionView view = GameUI::ReadAfterAction(gScreen);
    // ⛔ Before PostM_ShowSoldierHUD the panels are hidden for the fly-in matinee and the screen
    // swallows every key. Refusing to build keeps the layer from narrating a screen the player
    // cannot see yet — squad select's walk-up gate, same shape (recon §5).
    if (!view.hudShown || view.slots.empty())
        return nullptr;

    const std::vector<WireRecord> words
        = FetchCompanionRecords(kAfterActionReaderClass, "DescribeAfterAction");
    const WireRecord* mission = FindRecord(words, "mission");
    std::vector<const WireRecord*> cards;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "card")
            cards.push_back(&rec);
    }
    static const WireRecord kEmptyRecord;
    const WireRecord* missionRec = mission ? mission : &kEmptyRecord;

    GraphBuilder b(&gState.Expanded);

    // ---- the mission block: the top-left words no focus visits ----
    if (mission) {
        b.BeginStop("mission");
        b.SetRegion("mission");
        const std::string op = RecField(*mission, "op");
        if (!op.empty())
            b.AddLabel(ControlId::Structural("afteraction/mission/op"), [op]() { return op; });
        const std::string objective
            = JoinPhrases(RecField(*mission, "objlabel"), RecField(*mission, "objective"));
        if (!objective.empty()) {
            b.AddLabel(ControlId::Structural("afteraction/mission/objective"),
                [objective]() { return objective; });
        }
    }

    // ---- the soldier panels, in the list's own order (== visual left-to-right) ----
    //
    // Same arrow model as squad select, for the same geometric reason: panels sit SIDE BY SIDE
    // and stack their (at most two) controls vertically, so Up/Down walks one soldier's panel
    // and Left/Right steps to the soldier beside them, clamped to ragged columns, with explicit
    // self-edge walls at the ends.
    b.BeginStop("squad");
    b.SetRegion("squad");
    std::vector<std::vector<ControlId>> columns(view.slots.size());
    for (size_t i = 0; i < view.slots.size(); i++) {
        const GameUI::AfterActionSlot& slot = view.slots[i];
        const WireRecord* cardRec = FindCardByPosition(cards, static_cast<int>(i));
        if (!cardRec && !cards.empty()) {
            // Both halves walk the same list in the same order, so a miss means the companion
            // skipped this entry — the hole the position key exists to leave visible.
            static int sWarned = 0;
            if (sWarned++ < 3)
                LOG_WARN("MenuNavigator: after action — no card record at position %zu of %zu",
                    i, cards.size());
        }
        const WireRecord& card = cardRec ? *cardRec : kEmptyRecord;
        const int visual = static_cast<int>(i);

        // The soldier is named by the pure-structure parent; the controls under it stay short.
        b.PushContext(AfterActionCardContext(card, *missionRec, visual));

        // Promote and Memorialize are the SAME widget with the delegate swapped; m_bKIA decides
        // which verb it means. Both flags are the screen's own cached answers, read natively.
        if (slot.promoteButton && (slot.canPromote || slot.kia)) {
            const GameUI::AfterActionControl control = slot.kia
                ? GameUI::AfterActionControl::Memorial
                : GameUI::AfterActionControl::Promote;
            const std::string label
                = RecField(*missionRec, slot.kia ? "memorial" : "promotelabel");
            if (!label.empty()) {
                ControlId id = ControlId::Structural(
                    "afteraction/card/" + std::to_string(i) + "/promote");
                columns[i].push_back(id);
                gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [visual, control]() {
                    GameUI::SetAfterActionFocus(gScreen, visual, control);
                }};
                NodeVtable vt;
                vt.Type = &ButtonType();
                vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
                vt.Announcements.back().Kind = AnnouncementKinds::Label;
                vt.OnActivate = [visual, control]() {
                    GameUI::AfterActionView v = GameUI::ReadAfterAction(gScreen);
                    if (visual < static_cast<int>(v.slots.size()))
                        GameUI::ActivateAfterActionControl(v.slots[visual], control);
                };
                b.AddItem(std::move(id), std::move(vt));
            }
        }

        // The bond icon exists on the companion's say-so — UpdateData parks it at
        // SetBondLevel(-1) for a class that cannot bond and for KIA, and only the unit state
        // knows which. The icon's own tooltip names it (the bondmate's, or the screen's bond
        // title); the card's `bond` line carries the state, unlit state silent.
        if (!RecField(card, "canbond").empty() && slot.bondIcon) {
            const std::string label = GameUI::ReadPanelTooltip(slot.bondIcon);
            if (!label.empty()) {
                ControlId id = ControlId::Structural(
                    "afteraction/card/" + std::to_string(i) + "/bond");
                columns[i].push_back(id);
                gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [visual]() {
                    GameUI::SetAfterActionFocus(gScreen, visual,
                        GameUI::AfterActionControl::Bond);
                }};
                NodeVtable vt;
                vt.Type = &ButtonType();
                vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
                vt.Announcements.back().Kind = AnnouncementKinds::Label;
                const std::string state = RecField(card, "bond");
                if (!state.empty()) {
                    vt.Announcements.emplace_back(NodeAnnouncement::Static(state));
                    vt.Announcements.back().Kind = AnnouncementKinds::Value;
                }
                vt.OnActivate = [visual]() {
                    GameUI::AfterActionView v = GameUI::ReadAfterAction(gScreen);
                    if (visual < static_cast<int>(v.slots.size()))
                        GameUI::ActivateAfterActionControl(v.slots[visual],
                            GameUI::AfterActionControl::Bond);
                };
                b.AddItem(std::move(id), std::move(vt));
            }
        }

        // A panel with no controls at all (cannot promote, cannot bond, not KIA) still gets one
        // read-only node, or the soldier would be unreachable by arrow — a sighted player sees
        // that panel like any other.
        if (columns[i].empty()) {
            ControlId id = ControlId::Referenced(slot.handle,
                "afteraction/card/" + std::to_string(i));
            columns[i].push_back(id);
            gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [visual]() {
                GameUI::SetAfterActionFocus(gScreen, visual,
                    GameUI::AfterActionControl::Promote);
            }};
            const std::string label = AfterActionCardContext(card, *missionRec, visual);
            b.AddLabel(std::move(id), [label]() { return label; });
        }
        b.PopContext();
    }

    // Up/Down inside one panel, Left/Right across the strip — SquadNeighbour clamps to the
    // neighbour's last control and self-edges are walls, exactly as on squad select.
    for (size_t c = 0; c < columns.size(); c++) {
        for (size_t r = 0; r < columns[c].size(); r++) {
            const ControlId& here = columns[c][r];
            b.Connect(here, GraphDir::Up, columns[c][r > 0 ? r - 1 : r]);
            b.Connect(here, GraphDir::Down, columns[c][r + 1 < columns[c].size() ? r + 1 : r]);
            b.Connect(here, GraphDir::Left, SquadNeighbour(columns, c, -1, r));
            b.Connect(here, GraphDir::Right, SquadNeighbour(columns, c, 1, r));
        }
    }

    // ---- Continue: vanilla offers it as Escape's side effect and a mouse-only button ----
    b.BeginStop("actions");
    b.SetRegion("actions");
    {
        std::string label = RecField(*missionRec, "continue");
        if (label.empty())
            label = Vocab::AfterActionContinueFallback;
        ControlId id = ControlId::Structural("afteraction/continue");
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The screen's own zero-param handler — the same one Escape and the nav-help button
        // reach — so the After Action state, the exit cinematic and the loot screen all follow.
        // The hudShown gate above stands in for the input handler's own flag check, which
        // OnContinue itself does not repeat.
        vt.OnActivate = []() {
            if (gScreen)
                GameUI::InvokeScreenVerb(gScreen, "OnContinue");
        };
        b.AddItem(std::move(id), std::move(vt));
    }

    return b.Build();
}

std::string AfterActionName()
{
    // The header a sighted player reads top-left ("РЕЗУЛЬТАТЫ ЗАДАНИЯ") plus the operation's
    // own name; the fallback covers only the window between attach and the companion answering.
    for (const WireRecord& rec
        : FetchCompanionRecords(kAfterActionReaderClass, "DescribeAfterAction")) {
        if (RecField(rec, "kind") != "mission")
            continue;
        const std::string name
            = JoinPhrases(RecField(rec, "header"), RecField(rec, "op"));
        if (!name.empty())
            return name;
        break;
    }
    return Vocab::ScreenAfterActionFallback;
}

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

// ---- the armory hub (UIArmory_MainMenu) — docs/armory-recon.md §1 ----
//
// The menu between the Armory room and everything a soldier can have done to them. Vanilla's
// keyboard already walks the list and Enters (UIArmory.OnUnrealCommand → OnAccept); what it lacks
// is the words — the header, the description under the list, a greyed row's reason and the whole
// ability panel are Flash pushes — and two verbs: the soldier cycle, which vanilla puts on Tab and
// LeftShift (both the layer's), and the abilities/traits toggle, a nav-help button with no key.
//
// THREE STOPS in the screen's own order: the soldier (a spinner while the game offers the cycle,
// exactly as the bond screen's), the menu rows (label, the "!" badge, the description as a value
// part, a greyed row's reason), and the ability or trait summary with its toggle. Rows are keyed
// on their MCName — the Highlander's identity for its eight, and a mod's for any it inserts.

constexpr const char* kArmoryReaderClass = "X2AccessArmoryReader";

// Keyed on the rows and discriminated on the soldier: PopulateData respawns every row on each
// focus return and on each cycle, and a fresh set landing on the old addresses would otherwise
// keep the previous soldier's header.
WireCache gArmoryWords(kArmoryReaderClass, "DescribeArmoryMenu");

const std::vector<WireRecord>& FetchArmoryWords(const GameUI::ArmoryMenuView& view)
{
    return FetchWordsKeyedOnRows(gArmoryWords, view.screen, view.rows, view.unitId);
}

const WireRecord* FindArmoryRow(const std::vector<WireRecord>& words, const std::string& key)
{
    if (key.empty())
        return nullptr;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "row" && RecField(rec, "key") == key)
            return &rec;
    }
    return nullptr;
}

std::string ArmorySoldierLine(const std::vector<WireRecord>& words)
{
    const WireRecord* header = FindRecord(words, "header");
    return header ? RecField(*header, "who") : std::string();
}

std::unique_ptr<GraphRender> BuildArmoryMenu()
{
    const GameUI::ArmoryMenuView view = GameUI::ReadArmoryMenu(gScreen);
    const std::vector<WireRecord>& words = FetchArmoryWords(view);
    const WireRecord* header = FindRecord(words, "header");
    const bool canCycle = header && RecField(*header, "cycle") == "1";

    GraphBuilder b;

    // ---- who this is: the header no focus visits on mouse+keyboard ----
    b.BeginStop("soldier");
    b.PushContext("", "", /*positions*/ false);
    {
        std::string line = ArmorySoldierLine(words);
        if (line.empty())
            line = Vocab::SoldierFallback;
        NodeVtable vt;
        vt.Type = canCycle ? &SpinnerType() : &TextType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(line));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (canCycle) {
            // NextSoldier/PrevSoldier through the companion, behind the screen's own gate; the
            // rows respawn, so the verb helper's invalidation is what makes the re-read fresh.
            vt.OnAdjust = [](int sign, bool) {
                CallCompanionVerb(gArmoryWords, kArmoryReaderClass, "CycleArmorySoldier",
                    sign > 0 ? 1 : -1);
            };
            vt.StateText = []() {
                return ArmorySoldierLine(FetchArmoryWords(GameUI::ReadArmoryMenu(gScreen)));
            };
        }
        b.AddItem(ControlId::Structural("armory/soldier"), std::move(vt));
    }
    if (header) {
        for (const char* key : {"status", "stats", "bond", "intel", "ap"}) {
            const std::string text = RecField(*header, key);
            if (text.empty())
                continue;
            b.AddLabel(ControlId::Structural(std::string("armory/header/") + key),
                [text]() { return text; });
        }
    }
    b.PopContext();

    // ---- the menu ----
    b.BeginStop("menu");
    ControlId start;
    bool haveStart = false;
    for (const GameUI::ArmoryMenuRow& row : view.rows) {
        const WireRecord* rec = FindArmoryRow(words, row.key);
        std::string label = row.label.empty() ? std::string(Vocab::ArmoryRowFallback) : row.label;
        const int index = row.index;

        // Keyed on the MCName: PopulateData respawns the rows on every focus return, so the
        // pointer changes while the verb does not, and a mod may reorder the list.
        ControlId id = ControlId::Referenced(
            row.handle, "armory/row/" + (row.key.empty() ? std::to_string(index) : row.key));
        gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false, nullptr};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (row.attention) {
            // The "!" badge — a PCS slot to fill, an upgrade to apply, a bond available.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Attention));
            vt.Announcements.back().Kind = kKindAttention;
        }
        // The description panel under the list, which OnSelectionChanged repaints for the
        // selected row; one line, so it rides the row rather than earning a stop.
        const std::string desc = rec ? RecField(*rec, "desc") : std::string();
        if (!desc.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(desc));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        if (row.disabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            // The tooltip SetDisabled parked for a mouse player, re-derived by the companion.
            const std::string reason = rec ? RecField(*rec, "reason") : std::string();
            if (!reason.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(reason));
                vt.Announcements.back().Kind = AnnouncementKinds::Reason;
            }
        }
        // Offered even when greyed: OnAccept refuses a disabled row with the game's own sound,
        // where a missing node would refuse it with nothing.
        vt.OnActivate = [index]() {
            GameUI::ActivateArmoryMenuRow(GameUI::ReadArmoryMenu(gScreen), index);
        };
        vt.OnTooltip = ListItemTooltipAction(row.handle);

        if (!haveStart && (index == view.selected || !start.IsValid())) {
            start = id;
            haveStart = index == view.selected;
        }
        b.AddItem(std::move(id), std::move(vt));
    }

    // ---- the ability / trait summary, and its toggle ----
    if (const WireRecord* summary = FindRecord(words, "summary")) {
        b.BeginStop("abilities");
        const std::string title = RecField(*summary, "title");
        if (!title.empty())
            b.PushContext(title, "", /*positions*/ true);
        for (const WireRecord& rec : words) {
            if (RecField(rec, "kind") != "ability")
                continue;
            const std::string name = RecField(rec, "name");
            if (name.empty())
                continue;
            NodeVtable vt;
            vt.Type = &TextType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(name));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            const std::string desc = RecField(rec, "desc");
            if (!desc.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(desc));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }
            b.AddItem(ControlId::Structural("armory/ability/" + RecField(rec, "index") + "/" + name),
                std::move(vt));
        }
        if (!title.empty())
            b.PopContext();

        // The nav-help toggle (FXS_BUTTON_X on a gamepad, no key at all otherwise), offered
        // exactly when the game offers it — a soldier with a trait to switch to.
        const std::string toggle = RecField(*summary, "toggle");
        if (!toggle.empty()) {
            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(toggle));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            vt.OnActivate = []() {
                CallCompanionVerb(gArmoryWords, kArmoryReaderClass, "ToggleArmoryTraits", 0);
            };
            b.AddItem(ControlId::Structural("armory/toggle"), std::move(vt));
        }
    }

    AddResourceStop(b);
    // Land on the menu, where the game itself puts focus (List.Navigator.SelectFirstAvailable).
    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string ArmoryMenuName()
{
    // Whose armory, as the loadout names itself — the question two screens of arrowing makes
    // urgent.
    const std::string who = ArmorySoldierLine(FetchArmoryWords(GameUI::ReadArmoryMenu(gScreen)));
    return who.empty() ? std::string(Vocab::ScreenArmoryFallback) : who;
}

bool SetArmoryMenuListIndex(void* screen, int index)
{
    return GameUI::SetArmoryMenuSelection(GameUI::ReadArmoryMenu(screen), index);
}

// ---- weapon upgrades (UIArmory_WeaponUpgrade) and the pattern picker — docs/armory-recon.md §2 ----
//
// The loadout's shape over a weapon: ONE of two lists is live (ActiveList), the other a mirror.
// The stops, in the screen's own order: the soldier and the weapon (a spinner while the game
// offers the soldier cycle), the SLOTS list — or, while the picker is open, its read-only mirror
// followed by the UPGRADES list — then the previewed stat block with the description box on top,
// then the customize rows. Enter on a slot opens the picker (the game's own OnAccept); Escape
// backs out of it (the game's own OnCancel, unclaimed); Enter on an upgrade raises the confirm.
//
// The stat block reads at ANNOUNCE time, the commodity card's arrangement: the game repaints it
// on every selection change (PreviewUpgrade) and our write-back is what moves the selection.

constexpr const char* kWeaponUpgradeReaderClass = "X2AccessWeaponUpgradeReader";

// Keyed on both lists' rows, discriminated on the weapon and the SELECTION of the active list —
// the preview is that selection's shadow — with the pane folded into the discriminator so a
// switch that happened to keep every pointer still re-asks.
WireCache gWeaponUpgradeWords(kWeaponUpgradeReaderClass, "DescribeWeaponUpgrade");

const std::vector<WireRecord>& FetchWeaponUpgradeWords(const GameUI::WeaponUpgradeView& view)
{
    std::vector<void*> key;
    key.reserve(view.slots.size() + view.upgrades.size() + view.options.size() + 1);
    for (const GameUI::WeaponUpgradeRow& row : view.slots)
        key.push_back(row.handle);
    for (const GameUI::WeaponUpgradeRow& row : view.upgrades)
        key.push_back(row.handle);
    for (const GameUI::MenuItem& row : view.options)
        key.push_back(row.handle);
    key.push_back(view.screen);
    const long long selected = view.upgradesActive ? view.upgradesSelected : view.slotsSelected;
    const long long extra = (static_cast<long long>(view.weaponId) << 20)
        + (view.upgradesActive ? 1 << 16 : 0) + (selected + 1);
    return gWeaponUpgradeWords.Get(std::move(key), extra);
}

const WireRecord* FindUpgradeRowRec(
    const std::vector<WireRecord>& words, const char* list, int index)
{
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "row" && RecField(rec, "list") == list
            && atoi(RecField(rec, "index").c_str()) == index)
            return &rec;
    }
    return nullptr;
}

std::string WeaponUpgradeHeaderLine(const std::vector<WireRecord>& words, const char* key)
{
    const WireRecord* header = FindRecord(words, "header");
    return header ? RecField(*header, key) : std::string();
}

// One upgrade-list or slot row as a node: title, the effect line, the count sentence, the greyed
// state with its reason.
NodeVtable UpgradeRowVtable(const GameUI::WeaponUpgradeRow& row, const WireRecord* rec, bool live)
{
    NodeVtable vt;
    vt.Type = live ? &ButtonType() : &TextType();
    std::string label = row.title.empty() ? std::string(Vocab::SellItemFallback) : row.title;
    vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (!row.subtitle.empty()) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(row.subtitle));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    }
    const std::string count = rec ? RecField(*rec, "count") : std::string();
    if (!count.empty()) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(count));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    }
    if (row.disabled) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        const std::string reason = rec ? RecField(*rec, "reason") : std::string();
        if (!reason.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(reason));
            vt.Announcements.back().Kind = AnnouncementKinds::Reason;
        }
    }
    return vt;
}

std::unique_ptr<GraphRender> BuildWeaponPattern(const GameUI::WeaponUpgradeView& view)
{
    const std::vector<WireRecord>& words = FetchWeaponUpgradeWords(view);
    const WireRecord* trait = FindRecord(words, "trait");

    GraphBuilder b;
    b.BeginStop("options");
    const std::string title = trait ? RecField(*trait, "title") : std::string();
    if (!title.empty())
        b.PushContext(title, "", /*positions*/ true);
    ControlId start;
    for (const GameUI::MenuItem& option : view.options) {
        const int index = option.index;
        ControlId id
            = ControlId::Referenced(option.handle, "pattern/row/" + std::to_string(index));
        gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false,
            [index]() { GameUI::SelectWeaponPattern(GameUI::ReadWeaponUpgrade(gScreen), index); }};
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(
            option.label.empty() ? std::string(Vocab::SellItemFallback) : option.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The pattern the weapon wears now — where the game lands, and what Escape reverts to.
        if (index == view.startingIndex) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Selected));
            vt.Announcements.back().Kind = AnnouncementKinds::Selected;
        }
        vt.OnActivate = [index]() {
            GameUI::ActivateWeaponPattern(GameUI::ReadWeaponUpgrade(gScreen), index);
        };
        if (index == view.optionsSelected || (!start.IsValid() && index == view.startingIndex))
            start = id;
        b.AddItem(std::move(id), std::move(vt));
    }
    if (!title.empty())
        b.PopContext();
    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::unique_ptr<GraphRender> BuildWeaponUpgrade()
{
    const GameUI::WeaponUpgradeView view = GameUI::ReadWeaponUpgrade(gScreen);
    if (view.trait)
        return BuildWeaponPattern(view);

    const std::vector<WireRecord>& words = FetchWeaponUpgradeWords(view);
    const WireRecord* header = FindRecord(words, "header");
    const bool canCycle = header && RecField(*header, "cycle") == "1";

    GraphBuilder b;

    // ---- the soldier and the weapon ----
    b.BeginStop("weapon");
    b.PushContext("", "", /*positions*/ false);
    {
        std::string who = WeaponUpgradeHeaderLine(words, "who");
        if (who.empty())
            who = Vocab::SoldierFallback;
        NodeVtable vt;
        vt.Type = canCycle ? &SpinnerType() : &TextType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(who));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (canCycle) {
            vt.OnAdjust = [](int sign, bool) {
                CallCompanionVerb(gWeaponUpgradeWords, kWeaponUpgradeReaderClass,
                    "CycleWeaponUpgradeSoldier", sign > 0 ? 1 : -1);
            };
            vt.StateText = []() {
                return WeaponUpgradeHeaderLine(
                    FetchWeaponUpgradeWords(GameUI::ReadWeaponUpgrade(gScreen)), "who");
            };
        }
        b.AddItem(ControlId::Structural("wupgrade/soldier"), std::move(vt));
    }
    for (const char* key : {"weapon", "equipped", "slots"}) {
        const std::string text = WeaponUpgradeHeaderLine(words, key);
        if (!text.empty())
            b.AddLabel(ControlId::Structural(std::string("wupgrade/header/") + key),
                [text]() { return text; });
    }
    b.PopContext();

    ControlId start;

    // ---- the slots: live, or the mirror while the picker is open ----
    b.BeginStop("slots");
    {
        std::string title = WeaponUpgradeHeaderLine(words, "lefttitle");
        if (view.upgradesActive)
            title = JoinPhrases(title, Vocab::WeaponSlotsMirror);
        if (!title.empty())
            b.PushContext(title, "", /*positions*/ true);
        for (const GameUI::WeaponUpgradeRow& row : view.slots) {
            const WireRecord* rec = FindUpgradeRowRec(words, "slots", row.index);
            const int index = row.index;
            ControlId id = ControlId::Referenced(row.handle, "wupgrade/slot/" + std::to_string(index));
            NodeVtable vt = UpgradeRowVtable(row, rec, /*live*/ !view.upgradesActive);
            if (view.upgradesActive) {
                // The slot the picker is open for: the game freezes it (bIsLocked) and every
                // upgrade row previews against it.
                if (row.locked) {
                    vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Selected));
                    vt.Announcements.back().Kind = AnnouncementKinds::Selected;
                }
            } else {
                gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [index]() {
                    GameUI::SelectWeaponUpgradeRow(GameUI::ReadWeaponUpgrade(gScreen),
                        GameUI::WeaponUpgradeList::Slots, index);
                }};
                vt.OnActivate = [index]() {
                    GameUI::ActivateWeaponUpgradeRow(GameUI::ReadWeaponUpgrade(gScreen),
                        GameUI::WeaponUpgradeList::Slots, index);
                };
                if (!start.IsValid() && index == view.slotsSelected)
                    start = id;
            }
            b.AddItem(std::move(id), std::move(vt));
        }
        if (!title.empty())
            b.PopContext();
    }

    // ---- the upgrade picker, only while the game shows it ----
    if (view.upgradesActive) {
        b.BeginStop("upgrades");
        const std::string title = WeaponUpgradeHeaderLine(words, "righttitle");
        if (!title.empty())
            b.PushContext(title, "", /*positions*/ true);
        ControlId pickerStart;
        for (const GameUI::WeaponUpgradeRow& row : view.upgrades) {
            const WireRecord* rec = FindUpgradeRowRec(words, "upgrades", row.index);
            const int index = row.index;
            ControlId id
                = ControlId::Referenced(row.handle, "wupgrade/upgrade/" + std::to_string(index));
            gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [index]() {
                GameUI::SelectWeaponUpgradeRow(GameUI::ReadWeaponUpgrade(gScreen),
                    GameUI::WeaponUpgradeList::Upgrades, index);
            }};
            NodeVtable vt = UpgradeRowVtable(row, rec, /*live*/ true);
            // Offered even when greyed: OnItemClicked refuses with the game's own sound.
            vt.OnActivate = [index]() {
                GameUI::ActivateWeaponUpgradeRow(GameUI::ReadWeaponUpgrade(gScreen),
                    GameUI::WeaponUpgradeList::Upgrades, index);
            };
            if (index == view.upgradesSelected || !pickerStart.IsValid())
                pickerStart = id;
            b.AddItem(std::move(id), std::move(vt));
        }
        if (view.upgrades.empty()) {
            // The game's own line — m_strNoUpgradeAvailable is the title above — so the node
            // only gives the player something to stand on and the way back.
            b.AddLabel(ControlId::Structural("wupgrade/upgrades/empty"),
                []() { return std::string(Vocab::CommodityLeaveHint); });
        }
        if (!title.empty())
            b.PopContext();
        // Land in the picker: the game itself moved its navigator there.
        if (pickerStart.IsValid())
            start = pickerStart;
    }

    // ---- the description box and the stat block, previewed for the row under the cursor ----
    {
        bool any = false;
        auto ensure = [&]() {
            if (any)
                return;
            b.BeginStop("stats");
            b.PushContext(Vocab::StopWeaponStats, "", /*positions*/ false);
            any = true;
        };
        // Live readers: the words for THIS render's selection are the fallback, and a fresh
        // fetch answers for wherever the write-back has moved the selection since.
        auto liveField = [](const char* kind, const char* key, int index, std::string fallback) {
            return [kind, key, index, fallback]() {
                const std::vector<WireRecord>& fresh
                    = FetchWeaponUpgradeWords(GameUI::ReadWeaponUpgrade(gScreen));
                for (const WireRecord& rec : fresh) {
                    if (RecField(rec, "kind") != kind)
                        continue;
                    if (index >= 0 && atoi(RecField(rec, "index").c_str()) != index)
                        continue;
                    const std::string text = RecField(rec, key);
                    return text.empty() ? fallback : text;
                }
                return fallback;
            };
        };
        if (const WireRecord* preview = FindRecord(words, "preview")) {
            ensure();
            b.AddLabel(ControlId::Structural("wupgrade/preview/name"),
                liveField("preview", "name", -1, RecField(*preview, "name")));
            b.AddLabel(ControlId::Structural("wupgrade/preview/desc"),
                liveField("preview", "desc", -1, RecField(*preview, "desc")));
        }
        for (const WireRecord& rec : words) {
            if (RecField(rec, "kind") != "stat")
                continue;
            ensure();
            const int index = atoi(RecField(rec, "index").c_str());
            // Label and value as one line — the label already ends in its own colon where the
            // game gives it one, and AppendPhrase answers that with a plain space.
            std::string fallback = JoinPhrases(RecField(rec, "label"), RecField(rec, "value"));
            b.AddLabel(ControlId::Structural("wupgrade/stat/" + std::to_string(index)),
                [index, fallback]() {
                    const std::vector<WireRecord>& fresh
                        = FetchWeaponUpgradeWords(GameUI::ReadWeaponUpgrade(gScreen));
                    for (const WireRecord& live : fresh) {
                        if (RecField(live, "kind") != "stat"
                            || atoi(RecField(live, "index").c_str()) != index)
                            continue;
                        const std::string text
                            = JoinPhrases(RecField(live, "label"), RecField(live, "value"));
                        return text.empty() ? fallback : text;
                    }
                    return fallback;
                });
        }
        if (any)
            b.PopContext();
    }

    // ---- the customize rows: name, colour, pattern ----
    if (!view.customize.empty()) {
        b.BeginStop("customize");
        const std::string title = WeaponUpgradeHeaderLine(words, "customtitle");
        if (!title.empty())
            b.PushContext(title, "", /*positions*/ true);
        for (const GameUI::OptionRow& row : view.customize) {
            const int index = row.index;
            ControlId id
                = ControlId::Referenced(row.handle, "wupgrade/customize/" + std::to_string(index));
            gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false, [index]() {
                GameUI::SelectWeaponCustomizeRow(GameUI::ReadWeaponUpgrade(gScreen), index);
            }};
            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(
                row.label.empty() ? Vocab::CommandFallback(index) : row.label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            // The pattern row's value is its current pattern's name; the colour row is a swatch
            // with no words at all, named by its hue (Vocab::ColourName — an aid) and silent
            // while the weapon wears the default, which the game draws as no swatch.
            const std::string value = row.widget == GameUI::OptionWidget::ColorChip
                ? Vocab::ColourName(row.value)
                : row.value;
            if (!value.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }
            if (row.disabled) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
                vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            }
            vt.OnActivate = [index]() {
                GameUI::ActivateWeaponCustomizeRow(GameUI::ReadWeaponUpgrade(gScreen), index);
            };
            vt.OnTooltip = PanelTooltipAction(row.handle);
            b.AddItem(std::move(id), std::move(vt));
        }
        if (!title.empty())
            b.PopContext();
    }

    AddResourceStop(b);
    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string WeaponUpgradeName()
{
    const GameUI::WeaponUpgradeView view = GameUI::ReadWeaponUpgrade(gScreen);
    const std::vector<WireRecord>& words = FetchWeaponUpgradeWords(view);
    if (view.trait) {
        const WireRecord* trait = FindRecord(words, "trait");
        const std::string title = trait ? RecField(*trait, "title") : std::string();
        return title.empty() ? std::string(Vocab::ScreenWeaponPatternFallback) : title;
    }
    // The weapon, then whose — the two facts a player wants confirmed on arrival.
    std::string name = WeaponUpgradeHeaderLine(words, "weapon");
    AppendPhrase(name, WeaponUpgradeHeaderLine(words, "who"));
    return name.empty() ? std::string(Vocab::ScreenWeaponUpgradeFallback) : name;
}

// ---- soldier customization (the UICustomize family) — docs/customize-recon.md §2 ----
//
// One recipe for the whole family, because the family is one layout: a soldier header over a
// list of mecha rows, on the category menu, on every category screen, and on the option picker
// pushed over any of them. The rows retain their words (label, current choice, checkbox state,
// button label, a swatch's colour), so the companion supplies only the header line, the screen's
// localized title and the info screen's biography box.
//
// THREE STOPS in the screen's own order: the soldier (a spinner while the game offers the cycle,
// which vanilla puts on Tab and LeftShift — both the layer's, and only inside the armory), the
// rows under the screen's title (the picker's rows under the label of the row that opened it,
// since the picker keeps no title of its own), and on the info screen the biography. While the
// COLOUR SELECTOR is up the whole graph is its grid instead: one row per grid row, so Up/Down
// keep the column, each chip named by its hue (an aid — the game draws a swatch and a number).
//
// Enter is the screen's own; Escape stays the game's (the picker reverts, the selector reverts,
// the screen closes). The picker's PREVIEW button — the voice picker's, on every row — is the
// row's secondary action, so a voice can be heard without being chosen.

constexpr const char* kCustomizeReaderClass = "X2AccessCustomizeReader";

// Keyed on the rows, the screen and the UNIT pointer, discriminated on the unit id: the family
// REUSES its rows across UpdateData (GetListItem spawns only past ItemCount), so a soldier cycle
// changes neither the row set nor the screen — the unit is what changes, and a character-pool
// unit has no ObjectID to discriminate on, hence the pointer. A name or biography edit goes
// through the text-entry dialog, whose push detaches this recipe and drops the cache on the way.
WireCache gCustomizeWords(kCustomizeReaderClass, "DescribeCustomize");

const std::vector<WireRecord>& FetchCustomizeWords(const GameUI::CustomizeView& view)
{
    std::vector<void*> key;
    key.reserve(view.rows.size() + 2);
    for (const GameUI::OptionRow& row : view.rows)
        key.push_back(row.handle);
    key.push_back(view.unit);
    key.push_back(view.screen);
    return gCustomizeWords.Get(std::move(key), view.unitId);
}

std::string CustomizeHeaderField(const std::vector<WireRecord>& words, const char* key)
{
    const WireRecord* header = FindRecord(words, "header");
    return header ? RecField(*header, key) : std::string();
}

// The soldier stop every screen of the family opens with — the armory hub's shape: the header
// line, a spinner while the game offers the cycle, through the companion behind the screen's own
// gate (the rows are reused, so the verb helper's invalidation is what makes the re-read fresh).
void AddCustomizeSoldierStop(GraphBuilder& b, const std::vector<WireRecord>& words, bool canCycle)
{
    b.BeginStop("soldier");
    b.PushContext("", "", /*positions*/ false);
    std::string who = CustomizeHeaderField(words, "who");
    if (who.empty())
        who = Vocab::SoldierFallback;
    NodeVtable vt;
    vt.Type = canCycle ? &SpinnerType() : &TextType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(who));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (canCycle) {
        vt.OnAdjust = [](int sign, bool) {
            CallCompanionVerb(
                gCustomizeWords, kCustomizeReaderClass, "CycleCustomizeSoldier", sign > 0 ? 1 : -1);
        };
        vt.StateText = []() {
            return CustomizeHeaderField(FetchCustomizeWords(GameUI::ReadCustomize(gScreen)), "who");
        };
    }
    b.AddItem(ControlId::Structural("customize/soldier"), std::move(vt));
    b.PopContext();
}

// One row of a category screen as a node: the label, the current choice as a value (a swatch by
// its hue, a checkbox by its state), a greyed row's state with the reason the game parked in its
// tooltip. Every non-checkbox row opens something when pressed, which makes it a button.
NodeVtable CustomizeRowVtable(const GameUI::OptionRow& row)
{
    NodeVtable vt;
    vt.Announcements.emplace_back(NodeAnnouncement::Static(row.label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;

    std::string valueText;
    switch (row.widget) {
    case GameUI::OptionWidget::Checkbox:
        vt.Type = &CheckboxType();
        valueText = row.checked ? Vocab::Checked : Vocab::Unchecked;
        break;
    case GameUI::OptionWidget::ColorChip:
        vt.Type = &ButtonType();
        valueText = Vocab::ColourName(row.value);
        break;
    default:
        vt.Type = &ButtonType();
        valueText = row.value;
        break;
    }
    if (!valueText.empty()) {
        // LIVE: a colour accepted or a class changed repaints the row under the cursor, and the
        // fresh render's closure answers the synchronous feedback path.
        vt.Announcements.emplace_back(NodeAnnouncement::Static(valueText));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
        vt.Announcements.back().Live = true;
        vt.StateText = [valueText]() { return valueText; };
    }
    if (row.disabled) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        // SetDisabled(disabled, Reason) parks the why on the row's BG tooltip; read here so the
        // reason rides the row, as the armory hub's does, rather than waiting for Space.
        const std::string reason = GameUI::ReadRowTooltip(row.handle);
        if (!reason.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(reason));
            vt.Announcements.back().Kind = AnnouncementKinds::Reason;
        }
    }
    vt.OnTooltip = [handle = row.handle]() {
        std::string tip = GameUI::ReadRowTooltip(handle);
        if (!tip.empty())
            TTS::SpeakInterrupt(tip);
    };
    return vt;
}

// One option of the picker: the option's name, the PREVIEW button's label where the row carries
// one, and `selected` on the choice the soldier wears now — where the game lands, and what
// Escape reverts to (the pattern picker's rule).
NodeVtable CustomizeOptionVtable(const GameUI::OptionRow& row, const GameUI::CustomizeView& view)
{
    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(row.label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (row.widget == GameUI::OptionWidget::Button && !row.value.empty()) {
        // Advertised as well as bound: an unadvertised verb is one nobody presses.
        vt.Announcements.emplace_back(NodeAnnouncement::Static(row.value));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    }
    if (row.index == view.startingIndex) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Selected));
        vt.Announcements.back().Kind = AnnouncementKinds::Selected;
    }
    if (row.disabled) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
    }
    return vt;
}

std::unique_ptr<GraphRender> BuildColorSelector(const GameUI::CustomizeView& view)
{
    GraphBuilder b;
    b.BeginStop("colours");
    // The list is hidden under the grid but keeps its selection: the row that opened the grid
    // names it (HAIR COLOR, MAIN COLOR…), as it does for a sighted player who just pressed it.
    std::string title;
    for (const GameUI::OptionRow& row : view.rows) {
        if (row.index == view.selected) {
            title = row.label;
            break;
        }
    }
    if (title.empty())
        title = Vocab::CustomizeColours;
    b.PushContext(title, "", /*positions*/ false);

    ControlId start;
    int currentRow = -1;
    bool rowOpen = false;
    for (const GameUI::ColorChip& chip : view.chips) {
        if (chip.row != currentRow) {
            if (rowOpen)
                b.EndRow();
            // One key for every grid row: column-preserving Up/Down across the whole grid.
            b.StartRow("chips");
            currentRow = chip.row;
            rowOpen = true;
        }
        const int index = chip.index;
        ControlId id = ControlId::Referenced(chip.handle, "customize/chip/" + std::to_string(index));
        gFocus[id.StructuralKey] = FocusTarget{-1, nullptr, false,
            [index]() { GameUI::SelectColorChip(GameUI::ReadCustomize(gScreen), index); }};

        NodeVtable vt;
        vt.Type = &ButtonType();
        std::string name = Vocab::ColourName(chip.hex);
        if (name.empty())
            name = Vocab::ColourUnknown;
        vt.Announcements.emplace_back(NodeAnnouncement::Static(std::move(name)));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The number the chip draws in its corner — the game's own identity for it.
        vt.Announcements.emplace_back(NodeAnnouncement::Static(std::to_string(index)));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
        if (index == view.initialColor) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Selected));
            vt.Announcements.back().Kind = AnnouncementKinds::Selected;
        }
        vt.OnActivate
            = [index]() { GameUI::ActivateColorChip(GameUI::ReadCustomize(gScreen), index); };

        // Land where the game's navigator is (the chip it highlighted on opening is the colour
        // worn now), else on that colour.
        if (chip.focused || (!start.IsValid() && index == view.initialColor))
            start = id;
        b.AddItem(std::move(id), std::move(vt));
    }
    if (rowOpen)
        b.EndRow();
    b.PopContext();
    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::unique_ptr<GraphRender> BuildCustomize()
{
    const GameUI::CustomizeView view = GameUI::ReadCustomize(gScreen);
    if (view.colorSelector)
        return BuildColorSelector(view);

    const std::vector<WireRecord>& words = FetchCustomizeWords(view);
    const bool canCycle = CustomizeHeaderField(words, "cycle") == "1";

    GraphBuilder b;
    AddCustomizeSoldierStop(b, words, canCycle);

    // ---- the rows ----
    b.BeginStop("options");
    std::string title = view.trait ? view.openerLabel : CustomizeHeaderField(words, "title");
    if (title.empty())
        title = view.trait ? Vocab::ScreenCustomizeOptionsFallback : Vocab::ScreenCustomizeFallback;
    b.PushContext(title, "", /*positions*/ true);
    ControlId start;
    bool haveStart = false;
    for (const GameUI::OptionRow& row : view.rows) {
        const int index = row.index;
        ControlId id = ControlId::Referenced(row.handle, "customize/row/" + std::to_string(index));
        gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false};

        NodeVtable vt = view.trait ? CustomizeOptionVtable(row, view) : CustomizeRowVtable(row);
        // Offered even when greyed: the row's own Click refuses a disabled row silently, where
        // a missing node would refuse it with nothing either — and a checkbox row's box is
        // toggled through the screen, which is the one path its read-only box answers.
        vt.OnActivate
            = [index]() { GameUI::ActivateCustomizeRow(GameUI::ReadCustomize(gScreen), index); };
        if (view.trait && row.widget == GameUI::OptionWidget::Button) {
            // The PREVIEW button: hear the voice without choosing it. Vanilla reaches it by
            // mouse only — the screen's own Enter picks and closes before the row sees it.
            vt.OnSecondary
                = [index]() { GameUI::PressCustomizeRow(GameUI::ReadCustomize(gScreen), index); };
        }

        // Land on the game's own selection — on the picker that is the choice worn now.
        if (!haveStart && (index == view.selected || !start.IsValid())) {
            start = id;
            haveStart = index == view.selected;
        }
        b.AddItem(std::move(id), std::move(vt));
    }
    b.PopContext();

    // ---- the biography box, on the info screen ----
    if (const WireRecord* bio = FindRecord(words, "bio")) {
        b.BeginStop("biography");
        // Label and text as one line: the box is a caption over a paragraph, and an empty
        // paragraph still leaves the caption — which is what the screen draws.
        const std::string line = JoinPhrases(RecField(*bio, "label"), RecField(*bio, "text"));
        if (!line.empty())
            b.AddLabel(ControlId::Structural("customize/bio"), [line]() { return line; });
    }

    AddResourceStop(b);
    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string CustomizeName()
{
    const GameUI::CustomizeView view = GameUI::ReadCustomize(gScreen);
    if (view.trait) {
        // The picker keeps no title: the row that opened it is what a sighted player just read.
        return view.openerLabel.empty() ? std::string(Vocab::ScreenCustomizeOptionsFallback)
                                        : view.openerLabel;
    }
    const std::vector<WireRecord>& words = FetchCustomizeWords(view);
    std::string name = CustomizeHeaderField(words, "title");
    if (name.empty())
        name = Vocab::ScreenCustomizeFallback;
    // Then whose — the fact a soldier cycle two screens up makes worth confirming.
    AppendPhrase(name, CustomizeHeaderField(words, "who"));
    return name;
}

bool SetCustomizeListIndex(void* screen, int index)
{
    return GameUI::SetCustomizeSelection(GameUI::ReadCustomize(screen), index);
}

// ---- manage equipment (UIManageEquipmentMenu) — docs/customize-recon.md §1 ----
//
// The pause menu's shape over three rows: strip items, strip gear, strip weapons. Each Enter
// raises the game's own confirmation (the dialog recipe) and closes this menu behind it. On
// mouse+keyboard the menu itself is reached only through a nav-help button, which the loadout
// and squad-select recipes expose as their own verbs.
std::unique_ptr<GraphRender> BuildManageEquipment()
{
    const GameUI::ManageEquipmentView view = GameUI::ReadManageEquipment(gScreen);
    if (view.options.empty())
        return nullptr; // OnInit has not built the rows yet

    GraphBuilder b;
    b.BeginStop("options");
    for (const GameUI::MenuItem& opt : view.options) {
        const int index = opt.index;
        ControlId id = ControlId::Referenced(opt.handle, "manage/" + std::to_string(index));
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
        vt.OnActivate = [index]() {
            GameUI::ActivateManageEquipmentRow(GameUI::ReadManageEquipment(gScreen), index);
        };
        vt.OnTooltip = ListItemTooltipAction(opt.handle);
        b.AddItem(std::move(id), std::move(vt));
    }
    return b.Build();
}

std::string ManageEquipmentName()
{
    const std::string title = GameUI::ReadManageEquipment(gScreen).title;
    return title.empty() ? std::string(Vocab::ScreenManageEquipmentFallback) : title;
}

bool SetManageEquipmentListIndex(void* screen, int index)
{
    return GameUI::SetManageEquipmentSelection(GameUI::ReadManageEquipment(screen), index);
}

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

// ---- stop 2: the region pins' two buttons (docs/surfaces/strategy.md, "Region actions") ----
//
// MAKE CONTACT and the captionless radio-tower button, which between them are the whole
// resistance economy and which vanilla wires to no key at all — see X2AccessGeoScanner's own
// header for the four dead ends that leave them mouse-and-controller-only.
//
// ⛔ This is PARITY, not an aid, and that is the whole reason it is a Tab stop next to the tray
// while the world ring stays on PageUp/PageDown. Both buttons are DRAWN, on screen, right now, on
// a pin a sighted player can click. The ring enumerates seventeen regions nobody can read off the
// globe; this enumerates the two or three the globe is currently offering a verb on.
constexpr const char* kGeoScannerClass = "X2AccessGeoScanner";

// Verb codes, matching X2AccessGeoScanner's VERB_CONTACT / VERB_RELAY.
constexpr int kGeoVerbContact = 0;
constexpr int kGeoVerbRelay = 1;

// ⛔ CACHED ON THE SCREEN, which needs an argument because this payload is game STATE and not
// widgets — a pointer key cannot see a region change resistance level. It works because every
// transition that changes this set pops a MODAL: contact completing (eAlert_ContactMade), a relay
// completing (eAlert_OutpostBuilt), a region unlocking (UnlockPopup), the ResistanceRadio tech
// finishing, and our own verb (which opens the confirm alert). A modal pushes a screen, the
// navigator detaches, and Detach() calls InvalidateWireCaches() — so the set is re-asked exactly
// when it can have moved. The tray handles ride along in the key as a free second signal.
//
// ⚠ The one case neither covers is a console command; not worth a per-render outbound call.
WireCache gGeoActions(kGeoScannerClass, "DescribeRegionActions");

// Fire one of the two verbs. The companion re-checks the gate by (region, verb) before acting —
// the geoscape clock keeps running under this stop, so the region can have changed level between
// the render that spoke a line and the Enter that acts on it.
void CallGeoAction(int regionId, int verb)
{
    struct {
        int32_t RegionID;
        int32_t Verb;
    } params{regionId, verb};
    if (!GameUI::CallCompanion(kGeoScannerClass, "ActivateRegionAction", &params, nullptr))
        return;
    // The alert this opens detaches us and invalidates every cache on the way out; do it here
    // too, for the path where the verb was refused and no alert appeared.
    gGeoActions.Invalidate();
}

void BuildRegionActions(GraphBuilder& b, const GameUI::StrategyMapView& view)
{
    std::vector<void*> key;
    key.reserve(view.icons.size() + 1);
    for (const GameUI::TrayIcon& icon : view.icons)
        key.push_back(icon.handle);
    key.push_back(view.screen);

    bool any = false;
    for (const WireRecord& rec : gGeoActions.Get(std::move(key))) {
        if (RecField(rec, "kind") != "action")
            continue;
        const std::string region = RecField(rec, "region");
        const int regionId = atoi(RecField(rec, "id").c_str());
        // ⛔ An unnamed or unidentified record is DROPPED rather than given an anonymous stop —
        // and this is the one place that is safe to do here, because these fields are keyed and
        // never positional (CLAUDE.md's empty-field rule). A node reading "button, 2 of 3" with
        // no region would offer to spend supplies somewhere the player cannot name.
        if (region.empty() || regionId == 0)
            continue;
        const int verb = RecField(rec, "verb") == "relay" ? kGeoVerbRelay : kGeoVerbContact;

        if (!any) {
            // Its own stop, and a context label because the buttons have no group name of their
            // own: on screen their grouping is spatial (each sits on its region's pin), which is
            // exactly the channel this layer cannot use.
            b.BeginStop("actions");
            b.PushContext(Vocab::GeoscapeRegionActions);
            any = true;
        }

        // Keyed by ObjectID, stable over the whole campaign — there is no widget here to key on,
        // and the verb is part of the identity because a region can offer a different one after
        // the state moves.
        ControlId id = ControlId::Structural(
            "geoscape/action/" + std::to_string(verb) + "/" + std::to_string(regionId));

        NodeVtable vt;
        vt.Type = &ButtonType();

        // Region first, verb second: the region is what tells the entries apart, and on a stop
        // where every line is one of two verbs the place is the news.
        vt.Announcements.emplace_back(NodeAnnouncement::Static(region));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;

        auto value = [&vt](std::string text) {
            if (text.empty())
                return;
            vt.Announcements.emplace_back(NodeAnnouncement::Static(std::move(text)));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        };

        value(RecField(rec, "label"));
        // The "+N supplies" figure the pin draws beside the button as HoverInfo — the whole
        // reason for the verb, and the number a sighted player compares regions on.
        value(RecField(rec, "income"));
        // Cost and time as the alert labels them ("Cost", "Time"), joined here rather than in the
        // companion so a label the game left empty leaves no separator behind.
        value(JoinPhrases(RecField(rec, "costlabel"), RecField(rec, "cost")));
        value(JoinPhrases(RecField(rec, "timelabel"), RecField(rec, "time")));
        // Both refusals are still ACTIVATABLE, exactly as the pin's button is: the game shows the
        // button whatever you can afford and explains itself inside the alert. Saying so here
        // just means the player learns it before spending a keypress.
        if (RecField(rec, "afford") == "0")
            value(Vocab::GeoscapeCannotAfford);
        if (RecField(rec, "comms") == "0")
            value(Vocab::GeoscapeNoComms);

        vt.OnActivate = [regionId, verb]() { CallGeoAction(regionId, verb); };

        b.AddItem(std::move(id), std::move(vt));
    }

    if (any)
        b.PopContext();
}

std::unique_ptr<GraphRender> BuildGeoscape()
{
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
        AddResourceStop(b);
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
            // moved onto this icon — so the icon is re-read live and handed over so the call can
            // CHECK that, not so it can send to it. When the two disagree the Avenger flies
            // somewhere the player never chose, which is the failure that has to be impossible.
            vt.OnActivate = [handle = icon.handle]() {
                GameUI::StrategyMapView live = GameUI::ReadStrategyMap(gScreen);
                for (const GameUI::TrayIcon& t : live.icons)
                    if (t.handle == handle) {
                        GameUI::ActivateTraySelection(live, t);
                        return;
                    }
            };
        }

        // Focus write-back, two INDEPENDENT halves. Conflating them was the first version's bug.
        //
        //  1. Move the game's own navigator AND its SelectedMapItem, so both follow what was just
        //     spoken. Those are two calls, not one — UINavigator.SetSelected fires no delegate;
        //     see SetTraySelection, which owns the pair. This applies to placeholders TOO —
        //     measured live, an unmet faction's slot IS in NavigableControls — and
        //     SetTraySelection no-ops on the navIndex < 0 case by itself.
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
                GameUI::SetTraySelection(live, t);
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

    // ---- stop 2: the two buttons on the region pins ----
    //
    // After the tray and before the HUD, which is both the screen's own order (the globe sits
    // between them) and the useful one: the tray stays the landing stop, so nothing a player
    // already knows moves. Silent when the globe is offering neither verb — an empty stop in the
    // Tab cycle is a place to get stuck.
    BuildRegionActions(b, view);

    // ---- stop 3: the HUD's four keyboard-unreachable verbs ----
    //
    // PARITY, which is what earns them a stop at all: Dark Events, Chosen Info and Resistance
    // Orders are drawn on screen and clickable with a mouse, and vanilla reaches none of them
    // from the keyboard (AddDarkEventsButton ends in DisableNavigation() on both the container
    // and the button; the two HUD buttons are children of StrategyMapHUD, which UpdateMissions'
    // Navigator.Clear() leaves out of the screen's navigator entirely). The ResNet toggle is a
    // nav-help hotlink with a controller-only key. So this is the ant-farm shape again — the
    // game has the verb and only the mouse can reach it.
    //
    // ⛔ And it is half of what justifies TAKING Tab on this screen (the region actions above are
    // the other half). The world ring and the ';' status block are AIDS — they enumerate regions
    // and continents no sighted player can read off the globe — so by the stop-list parity rule
    // they stay on their own keys and out of the Tab cycle. Everything IN the cycle is a control
    // the game has drawn and left unreachable, which is the test a stop has to pass.
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

    AddResourceStop(b);
    return b.Build();
}

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
    // Nothing joins gFocus here — Flash-driven throughout: no panel the game will accept focus on.
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
    //
    // ⚠ WHAT IT DOES NOT DO IS DROP THE "N of M" STAMP — this comment used to claim it did, and
    // the claim cost the research report recipe a live defect ("СОВЕРШЕННО СЕКРЕТНО, 1 of 14").
    // Single-item rows are NOT exempt: StampPositions groups them by (Parent, StopKey, segment)
    // and stamps the group (GraphBuilder.cpp:414). The only thing suppressing the stamp here is
    // the PushContext above, whose `positions` false sets SuppressChildPositions — which is why
    // it is the right call for "СОЛДАТ ПОЛУЧИЛ НОВОЕ ЗВАНИЕ, 1 of 6" reading as a menu position
    // on something that is a sentence.
    //
    // ⚠ AND IT IS CONDITIONAL, so a popup whose title reads empty still stamps its message lines.
    // Unverified live; the fix is an empty-label context (the research report recipe carries the
    // worked version), not a second PushContext with invented words.
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
    // Only two alerts in the whole family draw a strip — eAlert_Contact shows intel and
    // eAlert_Outpost shows supplies, both being the resource that alert is asking the player to
    // spend. Every other popup falls to UpdateResources' `default:` and hides it, so this adds
    // nothing there without the recipe needing to know the switch.
    AddResourceStop(b);
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

// ---- the mission blade (UIMission family) — docs/mission-blade-recon.md ----
//
// The geoscape's commit-to-a-mission screen: one skeleton, 16 subclasses, and until now the
// single biggest unreadable surface left in the strategy layer (popup-inventory.md G2). The
// popup recipe's shape — lines to Tab through, then the buttons — fits it exactly, but its
// activation rule does not: here every subclass wires REAL delegates onto its buttons, and two
// controls exist that the screen-level keys can never reach (GOps' mission-selector buttons and
// the locked blades' Back button — both mouse-only in vanilla, recon §3). So activation is
// UIButton.Click() through the companion, which keeps the game's own visible/disabled gates and
// every side effect of the delegate the game bound.
//
// Words come from X2AccessMissionReader calling the SCREEN'S OWN getters (the read-the-screen
// precedence — GetMissionDescString even does the region param expansion), so every line is in
// the player's language and no re-derivation can drift from what the blade shows.

constexpr const char* kMissionReaderClass = "X2AccessMissionReader";

// ⛔ The extra key is the MISSION, not just the screen: the GOps blade rewrites MissionRef IN
// PLACE when a selector button switches the displayed op (OnMissionOneClicked → UpdateData, no
// widget respawn), so the pointer key alone would keep serving the previous mission's words.
WireCache gMissionBladeWords(kMissionReaderClass, "DescribeMissionBlade");

const std::vector<WireRecord>& FetchMissionBladeWords()
{
    return gMissionBladeWords.Get({gScreen}, GameUI::MissionBladeMissionId(gScreen));
}

// One outbound call with a single int — the covert-actions slot-verb shape. The companion
// resolves the screen fresh and Click()s the named button, so a stale index degrades to a no-op.
void ClickMissionBladeButton(int index)
{
    // A selector click repopulates the blade in place; the uniform verb rule (WireCache's
    // header) says invalidate by hand rather than trust the key — the helper does. Launch and
    // cancel close the screen, where Detach()'s InvalidateWireCaches covers it anyway.
    CallCompanionVerb(gMissionBladeWords, kMissionReaderClass, "ActivateMissionButton", index);
}

std::unique_ptr<GraphRender> BuildMissionBlade()
{
    // Nothing joins gFocus: RefreshNavigation keeps the game's own navigator, but its Enter
    // never consults it (the screen's OnUnrealCommand fires Button1 first — recon §3), so
    // pushing focus at the buttons would move a highlight no key can act on.
    const std::vector<WireRecord>& words = FetchMissionBladeWords();

    const WireRecord* header = FindRecord(words, "blade");
    const std::string screenClass = header ? RecField(*header, "class") : std::string();

    // Identity carries class + mission: a second blade replacing this one through the same
    // screen slot, or a GOps selector switching the mission in place, must read as NEW nodes to
    // the differ or the fresh words go unspoken (the popup recipe's reason).
    const std::string key =
        screenClass + "/" + std::to_string(GameUI::MissionBladeMissionId(gScreen)) + "/";

    GraphBuilder b;
    b.BeginStop("mission");

    // Empty-label context, the research report's worked pattern: the heading is the SCREEN NAME
    // (see MissionBladeName), spoken once on attach, and these lines are a briefing, not a menu
    // — "ЗАДАЧА…, 3 of 7" is a position stamp on a sentence. The empty label contributes no
    // words and no separator; only SuppressChildPositions.
    b.PushContext("", "", /*positions*/ false);

    int added = 0;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "line")
            continue;
        const std::string text = RecField(rec, "text");
        if (text.empty())
            continue;
        b.AddLabel(ControlId::Structural(key + "line/" + std::to_string(added)),
            [text]() { return text; });
        added++;
    }
    if (added == 0) {
        // Recognised the blade and got no words back. Say so — the screen still holds the
        // keyboard, and silence here is indistinguishable from no screen at all.
        b.AddLabel(ControlId::Structural(key + "line/none"),
            []() { return std::string(Vocab::MissionBladeNoText); });
    }

    b.PopContext(); // the buttons are controls, not briefing lines — they keep their positions
    b.BeginStop("buttons");
    b.StartRow();
    int buttons = 0;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "button")
            continue;
        const std::string label = RecField(rec, "label");
        if (label.empty())
            continue;
        const int index = atoi(RecField(rec, "index").c_str());
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = [index]() { ClickMissionBladeButton(index); };
        b.AddItem(ControlId::Structural(key + "button/" + std::to_string(index)), std::move(vt));
        buttons++;
    }
    if (buttons == 0) {
        // No live button came over the wire. The screen's own accept/cancel keys still work at
        // screen level (base OnUnrealCommand), so offer them under our generic pair — a blade a
        // blind player cannot leave is worse than one that reads badly.
        NodeVtable accept;
        accept.Type = &ButtonType();
        accept.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::PopupContinue));
        accept.Announcements.back().Kind = AnnouncementKinds::Label;
        accept.OnActivate = []() { GameUI::SendScreenCommand(gScreen, GameUI::kCmdEnter); };
        b.AddItem(ControlId::Structural(key + "button/accept"), std::move(accept));
        NodeVtable cancel;
        cancel.Type = &ButtonType();
        cancel.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::PopupDismiss));
        cancel.Announcements.back().Kind = AnnouncementKinds::Label;
        cancel.OnActivate = []() { GameUI::SendScreenCommand(gScreen, GameUI::kCmdEscape); };
        b.AddItem(ControlId::Structural(key + "button/cancel"), std::move(cancel));
    }
    b.EndRow();
    return b.Build();
}

// The blade's own localized heading plus the region — "ОТВЕТНЫЙ УДАР, Восточная Африка" — read
// off the wire the way CovertActionsName reads its header. The lines below carry no repeat of
// it (their context label is empty), so it is spoken exactly once.
std::string MissionBladeName()
{
    const WireRecord* header = FindRecord(FetchMissionBladeWords(), "blade");
    if (!header)
        return Vocab::ScreenMissionBlade;
    const std::string name =
        JoinPhrases(RecField(*header, "title"), RecField(*header, "region"));
    return name.empty() ? std::string(Vocab::ScreenMissionBlade) : name;
}

// ---- covert actions (UICovertActions) — docs/covert-actions-recon.md ----
//
// The WOTC core strategy loop, and the screen where the mod earns the most per node. Vanilla
// leaves a mouse+keyboard player able to READ nothing but the row names and able to PRESS neither
// of the two controls that matter: CONFIRM ACTION is DisableNavigation()ed with its only key path
// on FXS_BUTTON_X, and EDIT loadout on FXS_BUTTON_Y — both gamepad-only constants with no
// keyboard binding in any input section. So this is not narration over something that works; like
// the debris room, it is what makes the screen operable at all.
//
// SIX STOPS in the page's own order — list, card, faction, risks, slots, confirm — and focus
// STARTS on the list, where the game itself puts it (BuildScreen ends with
// Navigator.SetSelected(List)). The card is a stop of its own rather than parts folded onto each
// row for the commodity list's reason: a narrative is a paragraph and a list is fifteen entries,
// so folding one into the other makes every arrow press a paragraph.
//
// ⛔ THE GAME'S OWN SLOT MODE IS SKIPPED. Enter on a row calls SelectStaffSlots(), which rewires
// the screen's navigator into a 2×2 grid with its own Escape meaning. Our recipe owns the arrows,
// so the slots are ordinary nodes in one graph and activation goes straight to the slot object's
// HandleClick through the companion. Geometry is not information: four cards read better as four
// rows than as a square, and inheriting a second input mode buys nothing.
//
// ⛔ A ROW HAS NO Enter, DELIBERATELY. Selecting IS the action here — the focus write-back drives
// UIList::SetSelectedIndex, which fires SelectedItemChanged and repopulates every other stop — so
// an Enter would have to mean something else, and the only thing vanilla gives it is the slot
// mode above. Tab is the way on to the slots.

constexpr const char* kCovertActionReaderClass = "X2AccessCovertActionReader";

// Keyed on the row pointers AND on the selected action, the commodity list's rule exactly: every
// panel on this screen is the selected action's shadow (SelectedItemChanged repopulates the card,
// the faction panel, the risks and all four slots), so a pointer-only key would describe the
// action the player just left. The extra is the ACTION's ObjectID rather than the list index —
// same identity the companion joins on, and it survives the list reordering under us.
WireCache gCovertActionWords(kCovertActionReaderClass, "DescribeCovertActions");

const std::vector<WireRecord>& FetchCovertActionWords(const GameUI::CovertActionsView& view)
{
    return FetchWordsKeyedOnRows(gCovertActionWords, view.screen, view.rows, view.actionId);
}

// One outbound call with a single int, the personnel verbs' shape. Both slot verbs resolve the
// screen themselves and gate on the slot's own IsDisabled, so there is nothing to pass but the
// index — which is the ActionSlots array index the companion emitted.
void CallCovertActionSlotVerb(const char* funcName, int index)
{
    // Both verbs change the slots — one opens the personnel picker and the other the armory, and
    // clearing a slot rewrites the card and the risks in place without respawning a single row.
    // The pointer key cannot see any of that, so the helper's by-hand invalidation is the point.
    CallCompanionVerb(gCovertActionWords, kCovertActionReaderClass, funcName, index);
}

const WireRecord* FindCovertRow(const std::vector<WireRecord>& words, int actionId)
{
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "row" && atoi(RecField(rec, "action").c_str()) == actionId)
            return &rec;
    }
    return nullptr;
}

// How many staff slots currently hold somebody — the input to the one warning this surface adds.
int CountStaffedSlots(const std::vector<WireRecord>& words)
{
    int staffed = 0;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "slot" && RecField(rec, "slot") == "staff"
            && RecField(rec, "state") == "good")
            staffed++;
    }
    return staffed;
}

// The action list. Two structural levels of context: the list's own heading, and a faction
// grouping inside it — both non-focusable, because the game makes them so as well
// (listHeader.DisableNavigation(), UICovertActions.uc:364). `head` arrives per row and is the
// heading the game would DRAW above it, so the grouping is the screen's own rather than ours.
void BuildCovertActionList(GraphBuilder& b, const GameUI::CovertActionsView& view,
    const std::vector<WireRecord>& words, ControlId& start)
{
    if (view.rows.empty())
        return;

    const WireRecord* screenRec = FindRecord(words, "screen");
    const std::string listHeader = screenRec ? RecField(*screenRec, "listheader") : std::string();
    const int staffed = CountStaffedSlots(words);

    b.BeginStop("actions");
    bool listContext = false;
    if (!listHeader.empty()) {
        b.PushContext(listHeader, "", /*positions*/ false);
        listContext = true;
    }

    bool factionContext = false;
    for (const GameUI::CovertActionRow& row : view.rows) {
        const WireRecord* rec = FindCovertRow(words, row.actionId);
        const std::string head = rec ? RecField(*rec, "head") : std::string();
        if (!head.empty()) {
            if (factionContext)
                b.PopContext();
            b.PushContext(head, "", /*positions*/ true);
            factionContext = true;
        }

        // Keyed on the ACTION, not the row's position: the list is sorted four times over and a
        // started action is moved to the top, so an index key would rebadge a different operation
        // as "the same control" the moment anything changed.
        ControlId id = ControlId::Referenced(
            row.handle, "covert/row/" + std::to_string(row.actionId));
        gFocus[id.StructuralKey] = FocusTarget{row.index, nullptr, false, nullptr};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(row.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;

        if (row.disabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            // The game's own tooltip text, re-derived rather than read off ButtonBG — it is one
            // of exactly two localized sentences and the companion knows which applies.
            const std::string reason = rec ? RecField(*rec, "reason") : std::string();
            if (!reason.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(reason));
                vt.Announcements.back().Kind = AnnouncementKinds::Reason;
            }
        }

        // ⛔ On the SELECTED row only, and only while something is in a slot: moving away empties
        // it. See Vocab::CovertActionStaffed for why this is preventive rather than a report.
        if (row.actionId == view.actionId && staffed > 0) {
            vt.Announcements.emplace_back(
                NodeAnnouncement::Static(Vocab::CovertActionStaffed));
            vt.Announcements.back().Kind = kKindAttention;
        }

        // No OnActivate — see the header. Space still reads the row's own hover tooltip, which is
        // where SetDisabled parked the refusal for a sighted player.
        vt.OnTooltip = ListItemTooltipAction(row.handle);

        if (!start.IsValid() && row.actionId == view.actionId)
            start = id;
        b.AddItem(std::move(id), std::move(vt));
    }

    if (factionContext)
        b.PopContext();
    if (listContext)
        b.PopContext();
}

// The detail card and the faction panel — read-only lines, one node each, in the page's order.
// Every label here is the screen's own localized header rather than a word of ours.
void BuildCovertActionCard(GraphBuilder& b, const std::vector<WireRecord>& words)
{
    const WireRecord* card = FindRecord(words, "card");
    if (!card)
        return;

    b.BeginStop("card");
    int line = 0;
    auto say = [&](const std::string& text) {
        if (text.empty())
            return;
        b.AddLabel(ControlId::Structural("covert/card/" + std::to_string(line++)),
            [text]() { return text; });
    };

    say(RecField(*card, "name"));
    // The lock reason rides straight after the name, where the screen draws it over the card —
    // it is the answer to "why can I not run this", and burying it under a paragraph of narrative
    // would be answering it after the question stopped mattering.
    say(JoinParts(*card, {"locklabel", "lockreason"}));
    say(JoinParts(*card, {"durlabel", "duration"}));
    say(JoinParts(*card, {"rewardlabel", "reward", "rewarddetails"}));
    say(JoinParts(*card, {"loclabel", "location"}));
    say(RecField(*card, "narrative"));

    const WireRecord* faction = FindRecord(words, "faction");
    if (faction) {
        b.BeginStop("faction");
        b.AddLabel(ControlId::Structural("covert/faction"),
            [text = JoinParts(*faction, {"title", "name", "inflabel", "influence"})]() {
                return text;
            });
    }
}

// The risk rows. A LIST, not a GraphSheet: two facts per row, no column headings drawn anywhere,
// and "Soldier captured, moderate" is a complete sentence. ⚠ Every risk GetRisksStrings returns is
// emitted, which is more than the screen draws — see the companion's own note and
// docs/covert-actions-recon.md §6.1 for why that asymmetry is deliberate.
void BuildCovertActionRisks(GraphBuilder& b, const std::vector<WireRecord>& words)
{
    const WireRecord* screenRec = FindRecord(words, "screen");
    const WireRecord* ambush = FindRecord(words, "ambush");
    bool any = false;
    bool context = false; // tracked rather than re-derived: the pop must match the push exactly

    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "risk")
            continue;
        if (!any) {
            b.BeginStop("risks");
            const std::string title
                = screenRec ? RecField(*screenRec, "risktitle") : std::string();
            if (!title.empty()) {
                b.PushContext(title, "", /*positions*/ true);
                context = true;
            }
            any = true;
        }
        b.AddLabel(ControlId::Structural("covert/risk/" + RecField(rec, "index")),
            [text = JoinParts(rec, {"label", "level"})]() { return text; });
    }

    if (any) {
        // The ambush warning is a paragraph the game prints under the rows, and it is the one
        // that changes what a player DOES (it is why the EDIT verb exists on a slot at all), so
        // it lives inside the risk group rather than after it.
        if (ambush) {
            b.AddLabel(ControlId::Structural("covert/ambush"),
                [text = RecField(*ambush, "text")]() { return text; });
        }
        if (context)
            b.PopContext();
    }
}

// The four slot cards, flattened to four rows. Each carries the whole card as parts — what the
// slot wants, what filling it earns, what the button will do — because a slot is one thing with
// one name, which is exactly the case a flat readout loses nothing on.
void BuildCovertActionSlots(GraphBuilder& b, const std::vector<WireRecord>& words)
{
    bool any = false;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "slot")
            continue;
        if (!any) {
            b.BeginStop("slots");
            any = true;
        }

        const int index = atoi(RecField(rec, "index").c_str());
        const bool disabled = RecField(rec, "state") == "disabled";
        const std::string edit = RecField(rec, "button2");

        NodeVtable vt;
        vt.Type = &ButtonType();
        // req + fame + the slot's own name: "REQUIRED: FAMOUS Ranger", or just the soldier's
        // rank and name once somebody is standing in it. Joined here, from parts the companion
        // kept separate, so a part CleanText reduced to nothing leaves no separator behind.
        vt.Announcements.emplace_back(
            NodeAnnouncement::Static(JoinParts(rec, {"req", "fame", "label"})));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;

        const std::string reward = RecField(rec, "reward");
        if (!reward.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(reward));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }

        if (disabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            const std::string why = RecField(rec, "unavailable");
            if (!why.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(why));
                vt.Announcements.back().Kind = AnnouncementKinds::Reason;
            }
        } else {
            // The button's own label is what Enter will DO, so it is announced rather than
            // implied — "+ ADD SOLDIER" and "CLEAR" are the same node in two different states.
            const std::string button = RecField(rec, "button");
            if (!button.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(button));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }
            vt.OnActivate
                = [index]() { CallCovertActionSlotVerb("ClickCovertActionSlot", index); };
        }

        // ⛔ EDIT is FXS_BUTTON_Y in vanilla — gamepad-only — so without this the loadout of a
        // soldier about to walk into a Chosen ambush is unreachable from a keyboard. Announced
        // as well as bound: an unadvertised verb is one nobody presses.
        if (!edit.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(edit));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
            vt.OnSecondary
                = [index]() { CallCovertActionSlotVerb("EditCovertActionLoadout", index); };
        }

        b.AddItem(ControlId::Structural("covert/slot/" + std::to_string(index)), std::move(vt));
    }
}

std::unique_ptr<GraphRender> BuildCovertActions()
{
    const GameUI::CovertActionsView view = GameUI::ReadCovertActions(gScreen);
    const std::vector<WireRecord>& words = FetchCovertActionWords(view);

    GraphBuilder b;
    ControlId start;

    BuildCovertActionList(b, view, words, start);
    BuildCovertActionCard(b, words);
    BuildCovertActionRisks(b, words);
    BuildCovertActionSlots(b, words);

    // ⛔ The confirm node exists only while the GAME is showing the button. UpdateData hides it
    // outright when another action is running or influence is unmet, and a stop over a hidden
    // control would offer a verb that cannot fire — the card's lock line is what says why in
    // that state, and it is already there.
    if (view.launch) {
        const WireRecord* rec = FindRecord(words, "confirm");
        b.BeginStop("confirm");
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(
            rec ? RecField(*rec, "label") : std::string()));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (view.launchDisabled) {
            // CanBeginAction() said no — a required slot is empty, or the total cost is
            // unaffordable. The game states neither in words anywhere, so the node reports the
            // refusal and the slots above it are where the reason is readable.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        } else {
            vt.OnActivate = []() {
                GameUI::ActivateCovertActionLaunch(GameUI::ReadCovertActions(gScreen));
            };
        }
        b.AddItem(ControlId::Structural("covert/confirm"), std::move(vt));
    }

    // ⛔ Before SetStart, not after: the landing node is one of the nodes above, and adding a
    // trailing stop afterwards would leave the start pointing across a stop boundary.
    AddResourceStop(b);
    if (start.IsValid())
        b.SetStart(start);
    return b.Build();
}

// ⚠ No second focus test here, unlike the choose-facility probe's: FindCovertActionsScreen already
// applies it, and GameUI::IsScreenFocused is a null-guarded IsFocusedRaw — the same predicate.
// The test is what keeps this recipe standing down under the personnel picker a slot opens, the
// armory the EDIT verb opens, and the confirm popup, all of which push ON TOP of this screen.
void* ProbeCovertActions()
{
    return GameUI::FindCovertActionsScreen();
}

std::string CovertActionsName()
{
    const GameUI::CovertActionsView view = GameUI::ReadCovertActions(gScreen);
    const WireRecord* rec = FindRecord(FetchCovertActionWords(view), "screen");
    const std::string header = rec ? RecField(*rec, "header") : std::string();
    return header.empty() ? std::string(Vocab::ScreenCovertActions) : header;
}

// ⛔ Read the destructive-selection note in game_ui.h before touching this. Driving the list is
// what repopulates every other stop, so it is not optional — but it also empties the OUTGOING
// action's slots, which is why the row carries its warning while it still has something to lose.
bool SetCovertActionListIndex(void* screen, int index)
{
    return GameUI::SetCovertActionSelection(GameUI::ReadCovertActions(screen), index);
}

// ---- the covert action report (UICovertActionReport) — docs/covert-actions-recon.md §8b ----
//
// The result screen of the loop the covert-actions recipe opens — game_ui_strategy.h for what it
// retains and the one ⛔ (Escape is Continue). Vanilla's keyboard is complete here, Enter, Space
// and Escape all continuing; what it lacks is every WORD: the card and the faction panel are Flash
// pushes, and the per-soldier result cards are retained on slot OBJECTS no widget walk can see.
// THREE STOPS in the screen's own order: the card (name, narrative, reward, faction), the RESULTS
// cards one node each, then CONTINUE — the button's retained label over the screen's own Enter.

constexpr const char* kCovertReportReaderClass = "X2AccessCovertActionReader";

// Keyed on the screen and discriminated on the action: nothing on this screen respawns, and the
// payload is fixed for the screen's life (BuildScreen runs once, in OnInit).
WireCache gCovertReportWords(kCovertReportReaderClass, "DescribeCovertActionReport");

const std::vector<WireRecord>& FetchCovertReportWords(const GameUI::CovertReportView& view)
{
    return gCovertReportWords.Get({view.screen}, view.actionId);
}

std::unique_ptr<GraphRender> BuildCovertReport()
{
    const GameUI::CovertReportView view = GameUI::ReadCovertReport(gScreen);
    const std::vector<WireRecord>& words = FetchCovertReportWords(view);
    const WireRecord* screenRec = FindRecord(words, "screen");
    const WireRecord* card = FindRecord(words, "card");
    const WireRecord* faction = FindRecord(words, "faction");
    std::vector<const WireRecord*> slots;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "slot")
            slots.push_back(&rec);
    }

    GraphBuilder b;

    b.BeginStop("report");
    b.PushContext("", "", /*positions*/ false);
    int added = 0;
    auto say = [&](const std::string& id, const std::string& text) {
        if (text.empty())
            return;
        b.AddLabel(ControlId::Structural("covertreport/" + id), [text]() { return text; });
        added++;
    };
    if (card) {
        say("name", RecField(*card, "name"));
        say("narrative", RecField(*card, "narrative"));
        say("reward", JoinParts(*card, {"rewardlabel", "reward", "rewarddetails"}));
    }
    if (faction)
        say("faction", JoinParts(*faction, {"title", "name"}));
    // Recognised the screen and the companion answered nothing at all — said for ReportNoText's
    // reason, since Continue still has to be pressed to leave.
    if (added == 0 && slots.empty())
        say("empty", Vocab::CovertReportNoText);
    b.PopContext();

    // The RESULTS cards: one node per slot the container built, under the game's own heading.
    // Each carries the whole card as parts — a card is one thing with one name, which is the
    // case a flat readout loses nothing on.
    if (!slots.empty()) {
        b.BeginStop("results");
        b.PushContext(screenRec ? RecField(*screenRec, "slotsheader") : std::string(), "",
            /*positions*/ true);
        for (const WireRecord* rec : slots) {
            NodeVtable vt;
            vt.Type = &TextType();
            std::string label = RecField(*rec, "label");
            if (label.empty())
                label = Vocab::CovertReportCardFallback;
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            // The promotion badge — the "!" a sighted player reads off the card.
            const std::string promote = RecField(*rec, "promote");
            if (!promote.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(promote));
                vt.Announcements.back().Kind = kKindAttention;
            }
            // The four value lines in the card's own order: XP, cohesion, the slot reward, and
            // last the risks that fired (or SOLDIER KILLED) — the card draws that one red; the
            // words say what it is.
            for (const char* key : {"value1", "value2", "value3", "value4"}) {
                const std::string value = RecField(*rec, key);
                if (value.empty())
                    continue;
                vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }
            b.AddItem(ControlId::Structural("covertreport/slot/" + RecField(*rec, "index")),
                std::move(vt));
        }
        b.PopContext();
    }

    b.BeginStop("actions");
    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(
        view.continueLabel.empty() ? std::string(Vocab::ReportContinue) : view.continueLabel));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    // The screen's own input path: OnUnrealCommand answers Enter with OnContinueClicked, which
    // closes, raises the reward popups and deletes the action. Click() on the button reaches the
    // same handler; the key is what a sighted player presses.
    vt.OnActivate = []() { GameUI::SendScreenCommand(gScreen, GameUI::kCmdEnter); };
    b.AddItem(ControlId::Structural("covertreport/continue"), std::move(vt));

    return b.Build();
}

std::string CovertReportName()
{
    const GameUI::CovertReportView view = GameUI::ReadCovertReport(gScreen);
    const std::vector<WireRecord>& words = FetchCovertReportWords(view);
    const WireRecord* screenRec = FindRecord(words, "screen");
    const WireRecord* card = FindRecord(words, "card");
    std::string name = screenRec ? RecField(*screenRec, "header") : std::string();
    AppendPhrase(name, card ? RecField(*card, "name") : std::string());
    return name.empty() ? std::string(Vocab::ScreenCovertReportFallback) : name;
}

// ---- resistance orders (UIStrategyPolicy) ----
//
// The card board: three faction columns of order SLOTS plus a wild-card column, the HAND of
// unplayed orders along the bottom, and Continue. This recipe is not narration over something
// that works — vanilla's keyboard has exactly one verb here (Escape and Enter both reach
// OnClickedContinue) and placing a card is drag-and-drop through the card's own mouse handler, so
// a blind player could previously read nothing and change nothing. See game_ui.h and the
// companion for the two gates that had to be reproduced.
//
// THE BOARD IS ITS OWN GEOMETRY, and menu mode already speaks it: one ROW per column, its items
// being that column's slots, all sharing a row key. So Left/Right walks one faction's slots and
// Up/Down steps between factions keeping your slot position — which is the comparison a player
// actually makes. The faction's name and influence are a PushContext around each row, announced
// once when focus enters it rather than repeated on every card.
//
// ⛔ THE PICK-UP IS A TWO-STEP GESTURE AND THE HELD CARD IS SAID, NOT SELECTED. Enter picks a card
// up, Enter on a second card puts it down (play / swap / return to hand — the game's own
// SelectCard decides which). The held state rides as an ordinary Value part rather than
// AnnouncementKinds::Selected, deliberately: Selected also makes a node its stop's LANDING node,
// which would silently reroute Tab past every card declared before it.

constexpr const char* kPolicyReaderClass = "X2AccessPolicyReader";

// Keyed on the card pointers plus the screen, and on the HELD card as the extra. RefreshAllDecks
// respawns every card whenever anything moves, so the pointer set covers placement — but picking
// a card UP moves no card at all, and the readout has to change the instant it happens.
WireCache gPolicyWords(kPolicyReaderClass, "DescribePolicy");

const std::vector<WireRecord>& FetchPolicyWords(const GameUI::StrategyPolicyView& view)
{
    std::vector<void*> key;
    key.reserve(view.handCards.size() + 8);
    for (const GameUI::PolicyColumn& col : view.columns) {
        for (const GameUI::PolicyCard& card : col.cards)
            key.push_back(card.handle);
    }
    for (const GameUI::PolicyCard& card : view.handCards)
        key.push_back(card.handle);
    key.push_back(view.screen);
    return gPolicyWords.Get(std::move(key), view.inspectingId);
}

// The verbs are addressed by the GAME's own (column, slot): column -1 is the hand, and the slot
// is the card's index in whichever list holds it. Both halves resolve that pair through the
// screen's own lists, which is what keeps them from ever disagreeing about which card is meant.
void CallPolicyVerb(const char* funcName, int column, int slot)
{
    struct {
        int32_t Column;
        int32_t Slot;
    } params{column, slot};
    if (!GameUI::CallCompanion(kPolicyReaderClass, funcName, &params, nullptr))
        return;
    // Every one of these rebuilds the board (RefreshAllDecks) or moves the held card, and a
    // respawn can land on the freed addresses of the cards it replaced.
    gPolicyWords.Invalidate();
}

const WireRecord* FindPolicyCard(const std::vector<WireRecord>& words, int column, int slot)
{
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "card")
            continue;
        if (atoi(RecField(rec, "column").c_str()) == column
            && atoi(RecField(rec, "slot").c_str()) == slot)
            return &rec;
    }
    return nullptr;
}

const WireRecord* FindPolicyColumn(const std::vector<WireRecord>& words, int index)
{
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "column"
            && atoi(RecField(rec, "index").c_str()) == index)
            return &rec;
    }
    return nullptr;
}

// Backspace while a card is held: put it back where it was. The game's controller build has this
// on B/Escape (UIStrategyPolicy.uc:1253); mouse+keyboard vanilla has NO way out of a pick-up at
// all — the mouse abandons a drag by releasing over nothing, and a two-step selection has no
// equivalent gesture unless we give it one. Installed on EVERY node while holding, in place of
// the remove verb, so "Backspace takes it back" is one rule whatever is focused. Spoken here
// because the rebuild only re-announces the FOCUSED node, and the change (the held marker, the
// drop targets) is usually on other nodes.
void PolicyCancelHold()
{
    TTS::SpeakInterrupt(Vocab::PolicyPutBack);
    CallPolicyVerb("ClearPolicySelection", 0, 0);
}

// One card, whether it is an order, a locked slot or an empty one. The three read completely
// differently and only the first has a name at all.
NodeVtable PolicyCardNode(const GameUI::PolicyCard& card, const WireRecord* rec,
    const std::string& removeLabel, bool canPlay, bool holding)
{
    const std::string name = rec ? RecField(*rec, "name") : std::string();
    const std::string hint = rec ? RecField(*rec, "hint") : std::string();

    NodeVtable vt;
    vt.Type = &ButtonType();
    // A locked slot has only its influence hint; an empty one may have nothing at all, and it is
    // the thing a player is aiming at — so it is NAMED rather than skipped.
    std::string label = name;
    if (label.empty())
        label = hint;
    if (label.empty())
        label = Vocab::PolicyEmptySlot;
    vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;

    if (!name.empty()) {
        // What the order actually DOES — the one fact that decides where a card goes. The quote
        // is deliberately not here: it is flavour, and speech is serial. It rides on Space.
        const std::string summary = RecField(*rec, "summary");
        if (!summary.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(summary));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        if (!hint.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(hint));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        if (RecField(*rec, "attention") == "1") {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Attention));
            vt.Announcements.back().Kind = kKindAttention;
        }
        const std::string quote = RecField(*rec, "quote");
        if (!quote.empty())
            vt.OnTooltip = [quote]() { TTS::SpeakInterrupt(quote); };
    }

    // ⛔ A plain Value part, NOT AnnouncementKinds::Selected — see the section header.
    if (card.inspecting) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::PolicyHeld));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    }

    // The companion's drop prediction (`drop=`, board cards only, only while holding): "1" is a
    // slot the game would accept the held card into — said, because the game SHOWS its legal
    // drop spots while a drag is live and a two-step selection has nothing to see.
    const std::string drop
        = (holding && rec) ? RecField(*rec, "drop") : std::string();
    if (drop == "1") {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::PolicyDropTarget));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    }

    const int column = card.column;
    const int slot = card.slot;
    // The refusals the game makes SILENTLY, captioned at the keypress. drop == "0" is
    // SelectCard's wordless negative click; empty hands on a blank or locked slot (cardId 0) or
    // a vital card is CanDragCard() refusing without a sound at all. ⛔ The verb is still sent
    // in every case — the game stays the authority on what happens, the caption only covers its
    // silence, and the read-only calendar gate keeps its own voice (UICantChangeOrders through
    // the popup recipe, announced once on the screen name).
    const char* refusal = nullptr;
    if (holding) {
        if (drop == "0")
            refusal = Vocab::PolicyCantPlace;
    } else if (canPlay) {
        if (card.cardId == 0)
            refusal = Vocab::PolicyCantPick;
        else if (card.vital)
            refusal = Vocab::PolicyCantMove;
    }
    vt.OnActivate = [column, slot, refusal]() {
        if (refusal)
            TTS::SpeakInterrupt(refusal);
        CallPolicyVerb("SelectPolicyCard", column, slot);
    };

    if (holding) {
        // While holding, Backspace means "put it back" everywhere — see PolicyCancelHold.
        vt.OnSecondary = []() { PolicyCancelHold(); };
    } else if (canPlay && column >= 0 && !card.locked && !card.vital && card.cardId != 0
        && (!rec || RecField(*rec, "removable") != "0")) {
        // Backspace takes a card back out of a slot — vanilla's FXS_BUTTON_X, a gamepad button.
        // Only where the game would allow it: a hand card has nowhere to go back to, and a vital
        // order (MarkCardAsSpecial) cannot be removed at all.
        vt.Announcements.emplace_back(NodeAnnouncement::Static(removeLabel));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
        vt.OnSecondary = [column, slot]() { CallPolicyVerb("RemovePolicyCard", column, slot); };
    }
    return vt;
}

std::unique_ptr<GraphRender> BuildStrategyPolicy()
{
    const GameUI::StrategyPolicyView view = GameUI::ReadStrategyPolicy(gScreen);
    const std::vector<WireRecord>& words = FetchPolicyWords(view);
    const WireRecord* screenRec = FindRecord(words, "screen");
    const std::string removeLabel
        = screenRec ? RecField(*screenRec, "removelabel") : std::string();

    GraphBuilder b;
    ControlId start;
    const bool holding = view.inspectingId != 0;

    b.BeginStop("board");
    for (const GameUI::PolicyColumn& col : view.columns) {
        if (col.cards.empty())
            continue;
        const WireRecord* colRec = FindPolicyColumn(words, col.index);
        // The faction's name and its influence, announced once on entering the row rather than
        // repeated on every card in it. An unmet faction reads as the game's own
        // "UNKNOWN FACTION" — that withholding is the screen's, not ours.
        if (colRec)
            b.PushContext(JoinParts(*colRec, {"label", "influence"}), "", /*positions*/ true);

        // One row per column, all sharing a key, so Up/Down keeps the slot position while
        // stepping between factions — the comparison this board exists for.
        b.StartRow("policy/board");
        for (const GameUI::PolicyCard& card : col.cards) {
            ControlId id = ControlId::Referenced(card.handle,
                "policy/" + std::to_string(col.index) + "/" + std::to_string(card.slot));
            gFocus[id.StructuralKey] = FocusTarget{-1, card.handle, false, nullptr};
            if (!start.IsValid())
                start = id;
            b.AddItem(std::move(id),
                PolicyCardNode(card, FindPolicyCard(words, col.index, card.slot), removeLabel,
                    view.canPlay, holding));
        }
        b.EndRow();

        if (colRec)
            b.PopContext();
    }

    if (!view.handCards.empty()) {
        b.BeginStop("hand");
        // DeckTitle names the HAND ("AVAILABLE ORDERS:"), not the screen — which is why the
        // screen name below is one of ours.
        const std::string deck
            = screenRec ? RecField(*screenRec, "decktitle") : std::string();
        if (!deck.empty())
            b.PushContext(deck, "", /*positions*/ true);
        b.StartRow("policy/hand");
        for (const GameUI::PolicyCard& card : view.handCards) {
            ControlId id
                = ControlId::Referenced(card.handle, "policy/hand/" + std::to_string(card.slot));
            gFocus[id.StructuralKey] = FocusTarget{-1, card.handle, false, nullptr};
            b.AddItem(std::move(id),
                PolicyCardNode(card, FindPolicyCard(words, -1, card.slot), removeLabel,
                    view.canPlay, holding));
        }
        b.EndRow();
        if (!deck.empty())
            b.PopContext();
    }

    // ⛔ Continue is a real node even though Escape already reaches it. OnClickedContinue is not
    // merely a close — it submits OnCardScreenConfirm and, with orders still unplayed, raises the
    // "you have orders left" dialog. A player who has just been told the board is read-only needs
    // a way OUT that is a control rather than a key they have to know.
    b.BeginStop("continue");
    {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(
            screenRec ? RecField(*screenRec, "continue") : std::string(Vocab::PopupContinue)));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = []() { CallPolicyVerb("ContinuePolicy", 0, 0); };
        // "Backspace puts the held card back" holds on this node too — one rule, whatever is
        // focused when the player changes their mind.
        if (holding)
            vt.OnSecondary = []() { PolicyCancelHold(); };
        b.AddItem(ControlId::Structural("policy/continue"), std::move(vt));
    }

    if (start.IsValid())
        b.SetStart(start);
    return b.Build();
}

void* ProbeStrategyPolicy()
{
    return GameUI::FindStrategyPolicyScreen();
}

std::string StrategyPolicyName()
{
    const GameUI::StrategyPolicyView view = GameUI::ReadStrategyPolicy(gScreen);
    std::string name = Vocab::ScreenPolicy;
    // Said here and nowhere else: outside the monthly report every card refuses, and repeating
    // that on a dozen cards would be noise where saying it once, on arrival, is the whole answer.
    if (!view.canPlay)
        AppendPhrase(name, Vocab::PolicyReadOnly);
    return name;
}

} // namespace

// ---- what the navigator calls back into this file for ----
//
// Detach() has to hand the tray icon we hovered its matching MOUSE_OUT, so this one function is
// outside the anonymous namespace above; it is declared in recipe_context.h and is the only
// symbol any recipe TU exports besides its Recipe objects. gGeoHovered stays file-local — a
// function in the enclosing namespace still sees the anonymous namespace's names.

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

// ---- the recipes this file defines ----
//
// ⛔ THE PROBE ORDER IS NOT HERE. It is the `kRecipes` array literal in menu_navigator.cpp,
// the one place that lists every recipe in a single readable sequence next to the loop that
// consumes it. Adding a recipe means adding it there as well as declaring it in recipes.h; a
// recipe declared and never listed is a screen that is silently never probed. Each definition
// below keeps the sentence that argues where in that order it belongs.

const Recipe kSquadSelectRecipe{
    "squad select", ProbeFocused<GameUI::FindSquadSelect>, BuildSquadSelect, SquadSelectName};

// Squad select's post-mission twin, and like it listed below everything it can push — the
// promotion screens, the bond screen, the photobooth, dialogs and popups all resolve first.
const Recipe kAfterActionRecipe{
    "after action", ProbeFocused<GameUI::FindAfterAction>, BuildAfterAction, AfterActionName};

const Recipe kArmoryLoadoutRecipe{
    "armory loadout", ProbeFocused<GameUI::FindArmoryLoadout>, BuildArmoryLoadout,
    ArmoryLoadoutName};

// The hub: UNDER every screen it opens — the loadout, the promotion screens, the bond screen,
// the implant and weapon-upgrade lists, the photobooth, its dismiss dialog — and over the Armory
// room. Its probe insists on focus, which is what stands it down under all of them.
const Recipe kArmoryMenuRecipe{"armory", ProbeFocused<GameUI::FindArmoryMenuScreen>,
    BuildArmoryMenu, ArmoryMenuName, SetArmoryMenuListIndex};

// Weapon upgrades and the pattern picker: one recipe, since the picker is a subclass pushed over
// the upgrade screen and the finder takes the topmost of the family. Over the hub; under the
// rename dialog (the text-entry observer's, a real screen) and the confirm dialog. No
// SetListIndex: THREE lists, each written back through its own FocusTarget.
// Soldier customization: pushed over the armory hub or the character pool, the picker over any
// screen of its own family (the probe resolves that itself), raising only the text-entry dialog
// and dialogs. The colour selector is a child of the screen, not a stack entry.
const Recipe kCustomizeRecipe{"customize", ProbeFocused<GameUI::FindCustomizeScreen>,
    BuildCustomize, CustomizeName, SetCustomizeListIndex};
// The strip-equipment menu: over the loadout and over squad select, under its own confirmations.
const Recipe kManageEquipmentRecipe{"manage equipment",
    ProbeFocused<GameUI::FindManageEquipmentScreen>, BuildManageEquipment, ManageEquipmentName,
    SetManageEquipmentListIndex};
const Recipe kWeaponUpgradeRecipe{"weapon upgrades",
    ProbeFocused<GameUI::FindWeaponUpgradeScreen>, BuildWeaponUpgrade, WeaponUpgradeName};

// Above the ant farm and the Avenger HUD it sits over, below anything it pushes (a mission
// briefing, the pause menu, a dialog) — which is what ProbeGeoscape's "must be the current
// screen" test enforces, mirroring UIStrategyMap's own input routing.
const Recipe kGeoscapeRecipe{"geoscape tray", ProbeGeoscape, BuildGeoscape, GeoscapeName};

const Recipe kPopupRecipe{"popup", ProbePopup, BuildPopup, PopupName};

// Below the modals (a dialog, an alert, the sitrep detail popup all push OVER a blade and must
// win while they are up) and above the geoscape tray the blade covers. The blade's probe insists
// on being the topmost stack screen, which enforces the same thing from its own side.
const Recipe kMissionBladeRecipe{
    "mission blade", ProbeFocused<GameUI::FindMissionBladeScreen>, BuildMissionBlade,
    MissionBladeName};

// Above the Resistance Ring it is pushed over (the room recipe) and above the personnel roster's
// family only in the sense that it can never be topmost at the same time — UIPersonnel_CovertAction
// is PUSHED ON TOP of this screen when a slot asks for a soldier, so the roster must win while it
// is up. ProbeCovertActions insists on being the current screen, which settles both: it stands
// down under the picker, under the confirm popup, and under the armory the EDIT verb opens.
const Recipe kCovertActionsRecipe{"covert actions", ProbeCovertActions, BuildCovertActions,
    CovertActionsName, SetCovertActionListIndex};

// The result screen: pushed over the geoscape by the action-complete sequence, and above it only
// the reward popups its Continue raises, which resolve earlier. Never on the stack with the picker.
// No SetListIndex: no UIList, and the one button is navigable on its own.
const Recipe kCovertReportRecipe{"covert report", ProbeFocused<GameUI::FindCovertReportScreen>,
    BuildCovertReport, CovertReportName};

// No SetListIndex: the board is FIVE lists (four columns and the hand), and Recipe's hook takes
// one index against one screen. Focus write-back goes through FocusTarget::button instead —
// UIStrategyPolicy_Card is a navigable UIPanel, and the screen's own navigator is live on
// mouse+keyboard (DisableNavigation is called only under `ISCONTROLLERACTIVE).
const Recipe kStrategyPolicyRecipe{
    "resistance orders", ProbeStrategyPolicy, BuildStrategyPolicy, StrategyPolicyName};

// ---- promotion recipe (UIArmory_Promotion, UIArmory_PromotionHero, UIArmory_PromotionPsiOp) ----
//
// THE TREE IS A TABLE, so it is emitted as one — ranks down, branches across, which is the
// GraphSheet case more squarely than the personnel roster was. Column 0 is the RANK and carries
// the whole row as extra parts, so arrowing down reads the tree the way a flat list would;
// stepping Right lands on one ability at a time under its branch heading; Up/Down then reads
// ONE BRANCH down the ranks, which is the question a player actually asks ("what does the
// Shadow path give me?") and the one no flattened list can answer.
//
// ⛔ THIS ORIENTATION IS A TRANSPOSE OF THE HERO SCREEN, which draws ranks as COLUMNS. Taken
// deliberately: rank is the ordinal spine on both screens, so Up/Down means "later" in both, and
// the classic and hero trees then speak identically. The sheet hands us the other axis for free,
// so nothing is lost by choosing.
//
// ⛔ THE HERO SCREEN HAS NO KEYBOARD AT ALL (see the note over ReadPromotion), so this recipe is
// not a reading aid there — it is the only way in. That also means focus write-back is not a
// nicety: SetPromotionSelection drives the screen's own cursor and calls PreviewAbility, so the
// description panel repaints and a sighted person can follow what the player is hearing.
//
// ⚠ DO NOT "RESTORE" TAB HERE. UIArmory_Promotion.OnUnrealCommand declares FXS_KEY_TAB TWICE:
// once inside `if(`notdefined(FINAL_RELEASE))` where it calls RankUpSoldier outright, and again
// below it for soldier cycling. The first label wins, and retail is NOT a FINAL_RELEASE build —
// that is the same fact the dev harness leans on for the cheat manager (`obj list
// class=XComTacticalCheatManager` answers in a live retail mission). So the layer's Tab claim
// (next stop) is standing between the player and a free promotion, and giving Tab back to the
// screen to recover soldier cycling would hand it to them. Unverified live; check before acting.
constexpr const char* kPromotionReaderClass = "X2AccessPromotionReader";

// Keyed on the row widgets plus the screen, exactly like the roster's. The SELECTION is not part
// of the key: nothing here is the selection's shadow — the description panel is the game's own
// repaint, not something we read — so re-asking on every arrow press would walk the whole tree
// through the template manager once per keystroke for no new words.
struct PromotionWords {
    std::vector<WireRecord> records;
    std::vector<void*> key;
};

PromotionWords gPromotionWords;

const std::vector<WireRecord>& FetchPromotionWords(const GameUI::PromotionView& view)
{
    std::vector<void*> key;
    key.reserve(view.rows.size() + 1);
    for (const GameUI::PromotionRow& row : view.rows)
        key.push_back(row.handle);
    key.push_back(view.screen);

    if (key != gPromotionWords.key) {
        gPromotionWords.records
            = FetchCompanionRecords(kPromotionReaderClass, "DescribePromotion");
        gPromotionWords.key = std::move(key);
    }
    return gPromotionWords.records;
}

// The companion addresses every cell by (rank, branch) rather than by position, so a slot the
// tree does not fill is simply absent and the ones after it keep their branch numbers.
const WireRecord* FindPromotionCell(const std::vector<WireRecord>& records, int rank, int branch)
{
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == "cell" && atoi(RecField(rec, "rank").c_str()) == rank
            && atoi(RecField(rec, "branch").c_str()) == branch)
            return &rec;
    }
    return nullptr;
}

const WireRecord* FindPromotionRowRec(const std::vector<WireRecord>& records, int rank)
{
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == "row" && atoi(RecField(rec, "rank").c_str()) == rank)
            return &rec;
    }
    return nullptr;
}

// Our own word for a state the screen carries as ICON COLOUR alone. "available" and
// "unaffordable" are BOTH eUIPromotionState_Normal on screen and differ only in the foreground
// colour, which is precisely the channel speech loses.
std::string PromotionStateWord(const std::string& state)
{
    if (state == "owned")
        return Vocab::PromoOwned;
    if (state == "available")
        return Vocab::PromoAvailable;
    if (state == "unaffordable")
        return Vocab::PromoUnaffordable;
    if (state == "prereq")
        return Vocab::PromoPrereq;
    if (state == "spent")
        return Vocab::PromoSpent;
    return std::string(); // locked — the game's own placeholder title already says so
}

// A row's index in the LIVE view, matched on the game's own rank rather than on our position:
// the handler runs from the input drain a frame or more after the render, and PopulateData
// respawns every widget whenever an ability is bought.
int PromotionRowByRank(const GameUI::PromotionView& view, int rank)
{
    for (size_t i = 0; i < view.rows.size(); i++) {
        if (view.rows[i].rank == rank)
            return static_cast<int>(i);
    }
    return -1;
}

SheetConfig PromotionSheetConfig()
{
    return SheetConfig{Vocab::TableBlank, Vocab::RoleTable, &TextType()};
}

std::unique_ptr<GraphRender> BuildPromotion()
{
    gFocus.clear();

    GameUI::PromotionView view = GameUI::ReadPromotion(gScreen);
    if (!view.valid)
        return nullptr; // InitPromotion has not run PopulateData yet — treated as "closed"

    const std::vector<WireRecord>& words = FetchPromotionWords(view);
    static const WireRecord kEmptyRecord;
    const WireRecord* screenRec = nullptr;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "screen") {
            screenRec = &rec;
            break;
        }
    }
    if (!screenRec)
        screenRec = &kEmptyRecord;

    const std::string apLabel = RecField(*screenRec, "aplabel");

    GraphBuilder b;

    // ---- the lead lines: who this is, and what they have to spend ----
    b.BeginStop("tree");

    {
        // The soldier and their class, which every armory screen draws in the header and which
        // is the context for everything below it.
        std::string line = RecField(*screenRec, "unit");
        AppendPhrase(line, RecField(*screenRec, "class"));
        AppendPhrase(line, RecField(*screenRec, "header"));
        if (!line.empty())
            b.AddLabel(ControlId::Structural("promo/who"), [line]() { return line; });
    }

    // The AP wallet. Hero screens only — the classic rank pick is free, so there is no wallet on
    // screen and inventing one would be information a sighted player does not have.
    if (!RecField(*screenRec, "sharedap").empty()) {
        std::string line = RecField(*screenRec, "sharedlabel");
        AppendPhrase(line, RecField(*screenRec, "sharedap"));
        std::string soldier = RecField(*screenRec, "soldierlabel");
        AppendPhrase(soldier, RecField(*screenRec, "soldierap"));
        AppendPhrase(line, soldier);
        AppendPhrase(line, RecField(*screenRec, "combatintel"));
        b.AddLabel(ControlId::Structural("promo/ap"), [line]() { return line; });
    }

    // ---- the tree ----
    std::vector<std::string> columns;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "cols") {
            columns = RecFieldAll(rec, "col");
            break;
        }
    }

    GraphSheet sheet(b, "promo/", PromotionSheetConfig());
    sheet.Region("", columns);

    ControlId start;
    bool haveStart = false;
    const int rowCount = static_cast<int>(view.rows.size());
    int position = 0;
    int rowIndex = -1;

    for (const GameUI::PromotionRow& row : view.rows) {
        const int rank = row.rank;
        const WireRecord* rowRec = FindPromotionRowRec(words, rank);
        position++;
        rowIndex++;

        NodeVtable primary;
        primary.Type = &ButtonType();
        std::string rankName = rowRec ? RecField(*rowRec, "name") : std::string();
        if (rankName.empty())
            rankName = Vocab::PromoRankFallback;
        primary.Announcements.emplace_back(NodeAnnouncement::Static(rankName));
        primary.Announcements.back().Kind = AnnouncementKinds::Label;

        // The whole-row readout, carried on the PRIMARY as extra parts — this is what keeps the
        // flat reading and the tabular one the same graph.
        std::vector<std::pair<int, NodeVtable>> cells;
        int lockedCells = 0;
        int realCells = 0;
        std::string lockedTitle;
        std::vector<std::string> rowParts;

        for (const GameUI::PromotionCell& cell : row.cells) {
            const WireRecord* cellRec = FindPromotionCell(words, rank, cell.branch);
            // A hidden icon and a missing record are the same thing said twice: the tree has no
            // ability at this branch. Left as a GAP so the branches after it keep their numbers.
            if (!cellRec || !cell.visible)
                continue;
            realCells++;

            const std::string name = RecField(*cellRec, "name");
            const std::string state = RecField(*cellRec, "state");
            const std::string stateWord = PromotionStateWord(state);
            const std::string cost = RecField(*cellRec, "cost");
            const std::string desc = RecField(*cellRec, "desc");
            const std::string prereq = RecField(*cellRec, "prereq");
            const std::string exclusive = RecField(*cellRec, "exclusive");
            if (state == "locked") {
                lockedCells++;
                if (lockedTitle.empty())
                    lockedTitle = name;
            }

            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(name));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            if (!stateWord.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(stateWord));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }
            if (!cost.empty()) {
                // The game's own label for the unit, so this is not a bare number in a sentence
                // that never says what it counts.
                std::string costLine = cost;
                AppendPhrase(costLine, apLabel);
                vt.Announcements.emplace_back(NodeAnnouncement::Static(costLine));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }
            for (const std::string& warning : {prereq, exclusive}) {
                if (warning.empty())
                    continue;
                vt.Announcements.emplace_back(NodeAnnouncement::Static(warning));
                vt.Announcements.back().Kind = AnnouncementKinds::Reason;
            }

            // Space: the long description. It is the one thing on this screen that would make
            // every arrow press a paragraph, so it stays behind the tooltip key like every other
            // description in the layer.
            if (!desc.empty())
                vt.OnTooltip = [desc]() { TTS::SpeakInterrupt(desc); };

            const int branch = cell.branch;
            vt.OnActivate = [rank, branch]() {
                GameUI::PromotionView live = GameUI::ReadPromotion(gScreen);
                int r = PromotionRowByRank(live, rank);
                if (r >= 0)
                    GameUI::ActivatePromotionCell(live, r, branch);
            };

            // Each cell's own line in the row summary: the name plus its state, which is what a
            // sighted player takes from the icon at a glance.
            std::string part = name;
            AppendPhrase(part, stateWord);
            rowParts.push_back(part);

            cells.emplace_back(cell.branch + 1, std::move(vt));
        }

        // A rank the soldier has not reached draws the same placeholder in every slot. Saying it
        // once is what the row LOOKS like; saying it four times is an artefact of reading cells.
        if (realCells > 0 && lockedCells == realCells) {
            if (!lockedTitle.empty()) {
                primary.Announcements.emplace_back(NodeAnnouncement::Static(lockedTitle));
                primary.Announcements.back().Kind = AnnouncementKinds::Value;
            }
        } else {
            for (const std::string& part : rowParts) {
                primary.Announcements.emplace_back(NodeAnnouncement::Static(part));
                primary.Announcements.back().Kind = AnnouncementKinds::Value;
            }
        }

        // The screen highlights this rank and banners m_strNewRank over it — a whole affordance
        // carried by a colour and a badge.
        if (rowRec && !RecField(*rowRec, "promote").empty()) {
            primary.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::PersonnelPromote));
            primary.Announcements.back().Kind = kKindAttention;
        }

        primary.Announcements.emplace_back(
            NodeAnnouncement::Static(Vocab::Position(position, rowCount)));
        primary.Announcements.back().Kind = AnnouncementKinds::Position;

        sheet.RowAt(std::move(primary), row.handle, std::move(cells));

        // Focus write-back, per cell: landing anywhere in the row must move the GAME's cursor to
        // that exact (rank, branch), because the row's own SelectAbility reads it back.
        for (int col = 0; col <= static_cast<int>(row.cells.size()); col++) {
            const int branch = col == 0 ? 0 : col - 1;
            std::string key = GraphSheet::RowKey("promo/", row.handle, col);
            gFocus[key] = FocusTarget{-1, nullptr, false, [rank, branch]() {
                GameUI::PromotionView live = GameUI::ReadPromotion(gScreen);
                int r = PromotionRowByRank(live, rank);
                if (r >= 0)
                    GameUI::SetPromotionSelection(live, r, branch);
            }};
            if (col == 0 && !haveStart && (rowIndex == view.selectedRow || position == 1)) {
                start = ControlId::Referenced(row.handle, key);
                haveStart = rowIndex == view.selectedRow;
            }
        }
    }
    sheet.Finish();

    // ---- the ability-summary strip ----
    //
    // Everything the soldier already has, drawn along the bottom. Its own stop rather than part
    // of the tree: it is a different question ("what can this soldier do?") and it does not
    // share the tree's columns.
    bool anyPerk = false;
    int perkIndex = 0;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "perk")
            continue;
        if (!anyPerk) {
            b.BeginStop("perks");
            b.SetRegion("perks");
            anyPerk = true;
        }
        const std::string name = RecField(rec, "name");
        const std::string desc = RecField(rec, "desc");
        NodeVtable vt;
        vt.Type = &TextType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(name));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (!desc.empty())
            vt.OnTooltip = [desc]() { TTS::SpeakInterrupt(desc); };
        // Indexed, not named: two abilities really can share a display name, and a duplicate
        // structural key would silently merge them into one node.
        b.AddItem(
            ControlId::Structural("promo/perk/" + std::to_string(perkIndex++)), std::move(vt));
    }

    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string PromotionName()
{
    return Vocab::ScreenPromotion;
}

const Recipe kPromotionRecipe{
    "promotion", ProbeFocused<GameUI::FindPromotionScreen>, BuildPromotion, PromotionName};

// ---- the Chosen dossier (UIChosenInfo) — docs/chosen-bond-recon.md ----
//
// Four tabs over one dossier. Every word is re-derived by X2AccessChosenReader from the screen's
// own SelectedChosen — the object it populated Flash from — so the reveal filter and the UNKNOWN
// CHOSEN placeholders come out exactly as the screen drew them. Native reads two integers: the
// selected tab (the cache discriminator, since SelectChosen re-pushes the whole panel without
// respawning a widget) and the tab count.
//
// Vanilla switches Chosen on the D-pad only and closes on Enter, Space and Escape alike, so the
// tab nodes' Enter is the one verb this recipe re-provides — through the screen's own
// SelectChosen(), which plays the sounds, moves the Flash highlight and refreshes the panel.

namespace {

constexpr const char* kChosenReaderClass = "X2AccessChosenReader";
WireCache gChosenWords(kChosenReaderClass, "DescribeChosenInfo");

const std::vector<WireRecord>& FetchChosenWords(const GameUI::ChosenInfoView& view)
{
    // Keyed on the screen, discriminated by the tab: the pointer set never changes on this
    // screen, so without `extra` the first Chosen's dossier would be served for every tab.
    return gChosenWords.Get({view.screen}, view.selected);
}

// The record of `kind` carrying `index`, optionally under a `which` section.
const WireRecord* FindIndexed(const std::vector<WireRecord>& records, const char* kind, int index,
    const char* which = nullptr)
{
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != kind)
            continue;
        if (which && RecField(rec, "which") != which)
            continue;
        if (atoi(RecField(rec, "index").c_str()) == index)
            return &rec;
    }
    return nullptr;
}

// A read-only "LABEL: value" line as one node — label part then value part, so the per-kind
// filter still applies and a missing half degrades to the other rather than to a bare colon.
// Returns whether anything was added.
bool AddLabelledLine(GraphBuilder& b, const std::string& id, const std::string& label,
    const std::string& value, std::function<void()> tooltip = nullptr)
{
    if (label.empty() && value.empty())
        return false;
    NodeVtable vt;
    vt.Type = &TextType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(label.empty() ? value : label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (!label.empty() && !value.empty()) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    }
    vt.OnTooltip = std::move(tooltip);
    b.AddItem(ControlId::Structural(id), std::move(vt));
    return true;
}

// A re-speak of a long description on Space, or nothing when there is none.
std::function<void()> SpeakAgain(const std::string& text)
{
    if (text.empty())
        return nullptr;
    return [text]() { TTS::SpeakInterrupt(text); };
}

// The Continue node the dossier ends on: the SCREEN's own input path (OnUnrealCommand →
// OnContinue), which is where its close sound and nav-help teardown live.
void AddScreenContinue(GraphBuilder& b, const std::string& id, std::string label)
{
    if (label.empty())
        label = Vocab::PopupContinue;
    b.BeginStop("actions");
    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(std::move(label)));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    vt.OnActivate = []() { GameUI::SendScreenCommand(gScreen, GameUI::kCmdEnter); };
    b.AddItem(ControlId::Structural(id), std::move(vt));
}

// One section of traits — strengths or weaknesses — as its own stop, the game's heading as the
// context and one node per trait. The unknown wording for an unrevealed trait arrives from the
// companion already substituted, so nothing here can leak a name the game withholds.
void AddTraitStop(GraphBuilder& b, const std::vector<WireRecord>& words, const char* which)
{
    std::string title;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") == "section" && RecField(rec, "which") == which) {
            title = RecField(rec, "title");
            break;
        }
    }
    bool any = false;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "trait" || RecField(rec, "which") != which)
            continue;
        if (!any) {
            b.BeginStop(which);
            b.PushContext(title);
            any = true;
        }
        // Space re-speaks the description alone: a long trait text is what a player most often
        // wants a second time, and the label-then-value readout is the whole thing.
        const std::string desc = RecField(rec, "desc");
        AddLabelledLine(b, std::string("chosen/") + which + "/" + RecField(rec, "index"),
            RecField(rec, "name"), desc, SpeakAgain(desc));
    }
    if (any)
        b.PopContext();
}

std::unique_ptr<GraphRender> BuildChosenInfo()
{
    const GameUI::ChosenInfoView view = GameUI::ReadChosenInfo(gScreen);
    const std::vector<WireRecord>& words = FetchChosenWords(view);

    GraphBuilder b;

    // The tab strip: four slots on the Flash stage, met Chosen first, UNKNOWN CHOSEN after.
    // Nothing joins gFocus — the tabs are Flash buttons the screen highlights itself through
    // SelectChosen, and the game's navigator never holds them.
    b.BeginStop("chosen");
    b.StartRow("chosen/tabs");
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "tab")
            continue;
        const int index = atoi(RecField(rec, "index").c_str());
        const bool unknown = !RecField(rec, "unknown").empty();
        std::string label = unknown ? RecField(rec, "label") : JoinParts(rec, {"name", "nick"});
        if (label.empty())
            label = Vocab::TabFallback(index);

        NodeVtable vt;
        vt.Type = &TabType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (unknown) {
            // On screen — a sighted player counts the greyed slots to learn how many Chosen are
            // unmet — but not actionable: vanilla's click on one wraps to Chosen 0, which is not
            // a verb worth re-providing.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        } else {
            const bool selected = RecField(rec, "selected") == "1";
            vt.Announcements.emplace_back(NodeAnnouncement(
                [selected]() { return selected ? std::string(Vocab::Selected) : std::string(); },
                false, AnnouncementKinds::Selected));
            vt.OnActivate = [index]() {
                CallCompanionVerb(gChosenWords, kChosenReaderClass, "SelectChosen", index);
            };
            // Read LIVE after the press: a flag captured at build time can only describe the
            // state before it.
            vt.StateText = [index]() {
                const std::vector<WireRecord>& fresh
                    = FetchChosenWords(GameUI::ReadChosenInfo(gScreen));
                const WireRecord* tab = FindIndexed(fresh, "tab", index);
                if (!tab)
                    return std::string();
                std::string line = JoinParts(*tab, {"name", "nick"});
                if (RecField(*tab, "selected") == "1")
                    AppendPhrase(line, Vocab::Selected);
                return line;
            };
        }
        b.AddItem(ControlId::Structural("chosen/tab/" + std::to_string(index)), std::move(vt));
    }
    b.EndRow();

    // The dossier — a document, so positions are suppressed (the mission summary's arrangement).
    b.BeginStop("dossier");
    b.PushContext("", "", /*positions*/ false);
    int added = 0;
    if (const WireRecord* id = FindRecord(words, "identity")) {
        // The DEFEATED badge rides as the value: present only when bDefeated, so an undefeated
        // Chosen's line ends on the nickname and nothing is said about an unlit badge.
        if (AddLabelledLine(b, "chosen/identity", JoinParts(*id, {"class", "name", "nick"}),
                RecField(*id, "defeated")))
            added++;
    }
    int statIndex = 0;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "stat")
            continue;
        if (AddLabelledLine(b, "chosen/stat/" + std::to_string(statIndex++),
                RecField(rec, "label"), RecField(rec, "value")))
            added++;
    }
    if (const WireRecord* k = FindRecord(words, "knowledge")) {
        // The meter is a percentage the game passes as a number; the current action is the
        // meter's own caption, already prefixed by the screen ("CURRENT ACTION: …").
        std::string value;
        const std::string percent = RecField(*k, "percent");
        if (!percent.empty())
            value = Vocab::Percent(atoi(percent.c_str()));
        AppendPhrase(value, RecField(*k, "action"));
        if (AddLabelledLine(b, "chosen/knowledge", RecField(*k, "label"), value))
            added++;
    }
    if (const WireRecord* h = FindRecord(words, "hunt")) {
        // No text operand at all on this meter — the game fills it to FragmentLevel and says
        // nothing — so the words are ours, keyed on the same number.
        std::string value;
        const std::string level = RecField(*h, "level");
        const std::string max = RecField(*h, "max");
        if (!level.empty() && !max.empty())
            value = Vocab::MeterLevel(atoi(level.c_str()), atoi(max.c_str()));
        if (AddLabelledLine(b, "chosen/hunt", RecField(*h, "label"), value))
            added++;
    }
    if (const WireRecord* f = FindRecord(words, "faction")) {
        if (AddLabelledLine(b, "chosen/faction", RecField(*f, "label"),
                JoinParts(*f, {"type", "name"})))
            added++;
    }
    if (added == 0) {
        // Recognised the screen, got no words — the companion is not loaded or the screen is
        // mid-build. Said out loud: the player still has to leave the screen, and the Continue
        // below is how.
        b.AddLabel(ControlId::Structural("chosen/none"),
            []() { return Vocab::SurfaceUnavailable("Chosen reader"); });
    }
    b.PopContext();

    AddTraitStop(b, words, "strengths");
    AddTraitStop(b, words, "weaknesses");

    std::string continueLabel;
    if (const WireRecord* c = FindRecord(words, "continue"))
        continueLabel = RecField(*c, "label");
    AddScreenContinue(b, "chosen/continue", continueLabel);

    return b.Build();
}

std::string ChosenInfoName()
{
    return NameFromHeader(
        FetchChosenWords(GameUI::ReadChosenInfo(gScreen)), Vocab::ScreenChosenInfoFallback);
}

// ---- soldier bonds (UISoldierBondScreen) — docs/chosen-bond-recon.md §1.4 ----
//
// THE ROSTER IS A TABLE and is emitted as one (GraphSheet, the personnel roster's arrangement):
// column 0 is the soldier and carries the whole row as extra parts; RANK, CLASS, COMPATIBILITY
// and COHESION sit under the game's own headings; a fifth, heading-less column holds the CREATE
// BOND button on exactly the rows the game shows it on. Two verbs vanilla leaves to the mouse
// come back here — making the bond (the button called DisableNavigation(), so Enter never reaches
// it) and changing the sort (controller constants only) — and the soldier cycle vanilla put on
// Tab and LeftShift, both displaced by the layer's own keys, comes back as Left/Right on the
// soldier node, only while the game itself offers it.

constexpr const char* kBondReaderClass = "X2AccessBondReader";
WireCache gBondWords(kBondReaderClass, "DescribeBonds");
WireCache gBondConfirmWords(kBondReaderClass, "DescribeBondConfirm");

const std::vector<WireRecord>& FetchBondWords(const GameUI::BondScreenView& view)
{
    // Rows respawn on every sort, switch and refresh (ClearItems + a fresh panel per soldier), so
    // their pointers are the key; the screen's soldier is the discriminator, because the header
    // describes them and a switch that happened to land every new row on an old address would
    // otherwise keep the previous soldier's name.
    return FetchWordsKeyedOnRows(gBondWords, view.screen, view.rows, view.unitId);
}

bool CallBondVerb(const char* funcName, int value)
{
    return CallCompanionVerb(gBondWords, kBondReaderClass, funcName, value);
}

SheetConfig BondSheetConfig()
{
    return SheetConfig{Vocab::TableBlank, Vocab::RoleTable, &TextType()};
}

// A read-only cell of the bond sheet.
NodeVtable BondCell(const std::string& text)
{
    NodeVtable vt;
    vt.Type = &TextType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(text));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    return vt;
}

// The sort-header line the personnel roster settled on: "sort by RANK, ascending".
std::string SortLine(const WireRecord& rec)
{
    std::string line = std::string(Vocab::PersonnelSortBy) + " " + RecField(rec, "label");
    if (RecField(rec, "selected") == "1")
        AppendPhrase(line,
            RecField(rec, "desc") == "1" ? Vocab::SortDescending : Vocab::SortAscending);
    return line;
}

std::string BondSoldierLine(const std::vector<WireRecord>& words)
{
    const WireRecord* soldier = FindRecord(words, "soldier");
    return soldier ? JoinParts(*soldier, {"rank", "name"}) : std::string();
}

std::unique_ptr<GraphRender> BuildBondScreen()
{
    const GameUI::BondScreenView view = GameUI::ReadBondScreen(gScreen);
    const std::vector<WireRecord>& words = FetchBondWords(view);
    const WireRecord* header = FindRecord(words, "header");
    const WireRecord* soldier = FindRecord(words, "soldier");
    // Both gates are the GAME's: cycle is UpdateNavHelp's own HasSoldiersToCycleThrough outside
    // the squad screens, switch is OnUnrealCommand's !IsInSquadScreen().
    const bool canCycle = soldier && RecField(*soldier, "cycle") == "1";
    const bool canSwitch = soldier && RecField(*soldier, "switch") == "1";
    std::string bondLabel = header ? RecField(*header, "bondlabel") : std::string();
    if (bondLabel.empty())
        bondLabel = Vocab::BondCreateFallback;

    GraphBuilder b;

    // Whose bonds, and with whom. A spinner while the game offers the soldier cycle, plain text
    // otherwise.
    b.BeginStop("soldier");
    b.PushContext("", "", /*positions*/ false);
    {
        std::string line = BondSoldierLine(words);
        if (line.empty())
            line = Vocab::SoldierFallback;
        NodeVtable vt;
        vt.Type = canCycle ? &SpinnerType() : &TextType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(line));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (canCycle) {
            // NextSoldier()/PrevSoldier() through the companion — the game's own CycleToSoldier,
            // which also re-points every armory screen underneath at the new soldier.
            vt.OnAdjust = [](int sign, bool) { CallBondVerb("CycleSoldier", sign > 0 ? 1 : -1); };
            vt.StateText = []() {
                return BondSoldierLine(FetchBondWords(GameUI::ReadBondScreen(gScreen)));
            };
        }
        b.AddItem(ControlId::Structural("bonds/soldier"), std::move(vt));
    }
    if (const WireRecord* mate = FindRecord(words, "bondmate")) {
        // The panel's level is a digit inside the icon and its cohesion a bar — both get words.
        // With no bondmate the record carries the title alone (the game's own "No bondmate").
        std::string value;
        const std::string level = RecField(*mate, "level");
        if (!level.empty())
            value = Vocab::BondLevel(atoi(level.c_str()));
        AppendPhrase(value, JoinParts(*mate, {"name", "nick", "rank", "class", "compat"}));
        const std::string cohesion = RecField(*mate, "cohesion");
        if (!cohesion.empty())
            AppendPhrase(value, Vocab::Percent(atoi(cohesion.c_str())));
        AddLabelledLine(b, "bonds/bondmate", RecField(*mate, "title"), value);
    }
    b.PopContext();

    // The sort header — the personnel roster's stop, word for word, over this screen's five
    // UIFlipSortButtons (which retain Label, bArrowVisible and bArrowFlipped).
    bool anySort = false;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "sort" || RecField(rec, "label").empty())
            continue;
        const int index = atoi(RecField(rec, "index").c_str());
        if (!anySort) {
            b.BeginStop("sort");
            b.StartRow("bonds/sort");
            anySort = true;
        }
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(SortLine(rec)));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The header's own mouse handler: deselects the others, flips when already selected,
        // re-sorts. Re-read afterwards rather than closing over the flag.
        vt.OnActivate = [index]() { CallBondVerb("ClickBondSort", index); };
        vt.StateText = [index]() {
            const std::vector<WireRecord>& fresh
                = FetchBondWords(GameUI::ReadBondScreen(gScreen));
            const WireRecord* rec = FindIndexed(fresh, "sort", index);
            return rec ? SortLine(*rec) : std::string();
        };
        b.AddItem(ControlId::Structural("bonds/sort/" + std::to_string(index)), std::move(vt));
    }
    if (anySort)
        b.EndRow();

    b.BeginStop("list");
    if (view.rows.empty()) {
        // The game draws an empty list and no line here, so the line is ours.
        std::string text = Vocab::BondsEmpty;
        text += ". ";
        text += Vocab::CommodityLeaveHint;
        b.AddLabel(ControlId::Structural("bonds/empty"), [text]() { return text; });
        AddResourceStop(b);
        return b.Build();
    }

    // The companion's headings, in the row's own cell order, with a BLANK fifth for the CREATE
    // BOND column the screen draws no heading over (positional, so the blank is sent, not
    // dropped).
    std::vector<std::string> columns;
    if (const WireRecord* cols = FindRecord(words, "cols"))
        columns = RecFieldAll(*cols, "col");

    GraphSheet sheet(b, "bonds/", BondSheetConfig());
    sheet.Region("", columns);

    ControlId start;
    bool haveStart = false;
    const int rowCount = static_cast<int>(view.rows.size());
    int position = 0;

    for (const GameUI::BondRow& row : view.rows) {
        const WireRecord* rec = FindIndexed(words, "row", row.index);
        // Name then nickname, joined here so an absent nickname takes its separator with it —
        // the row's own two Flash fields. The rank is cells[0] and never part of this.
        std::string name = rec ? JoinParts(*rec, {"name", "nick"}) : std::string();
        if (name.empty())
            name = Vocab::SoldierFallback;
        std::vector<std::string> cells
            = rec ? RecFieldAll(*rec, "cell") : std::vector<std::string>();
        // The wire's own column order (X2AccessBondReader.AppendColumns): rank, class,
        // compatibility, cohesion — and cohesion arrives as the bare percentage the bar is filled
        // to, so it takes its unit here.
        if (cells.size() >= 4 && !cells[3].empty())
            cells[3] = Vocab::Percent(atoi(cells[3].c_str()));

        const int index = row.index;
        const bool offered = row.bondOffered && !row.disabled;
        position++;

        NodeVtable primary;
        primary.Type = &ButtonType();
        primary.Announcements.emplace_back(NodeAnnouncement::Static(name));
        primary.Announcements.back().Kind = AnnouncementKinds::Label;
        for (size_t c = 0; c < cells.size() && c < 4; c++) {
            if (cells[c].empty())
                continue;
            primary.Announcements.emplace_back(NodeAnnouncement::Static(cells[c]));
            primary.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        if (offered) {
            // The CREATE BOND button as an attention part: a whole affordance a sighted player
            // sees on the row, heard here without arrowing right for it.
            primary.Announcements.emplace_back(NodeAnnouncement::Static(bondLabel));
            primary.Announcements.back().Kind = kKindAttention;
        }
        if (row.disabled) {
            primary.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            primary.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        // Enter on the row: the bond when the game offers one — the prominent button IS what a
        // sighted player clicks on that row — else vanilla's own Enter (switch the page to this
        // soldier) where vanilla allows it. Shift+Enter is the switch regardless, so a row that
        // offers a bond keeps the other verb too.
        if (offered)
            primary.OnActivate = [index]() { CallBondVerb("ConfirmBond", index); };
        else if (canSwitch)
            primary.OnActivate = [index]() { CallBondVerb("SwitchSoldier", index); };
        if (canSwitch)
            primary.OnActivateShift = [index]() { CallBondVerb("SwitchSoldier", index); };
        // A sheet is raw mode underneath and stamps no positions; "4 of 20" is one of the facts a
        // player most wants on a roster, so it is added explicitly under the position kind.
        primary.Announcements.emplace_back(
            NodeAnnouncement::Static(Vocab::Position(position, rowCount)));
        primary.Announcements.back().Kind = AnnouncementKinds::Position;

        // Explicit LOGICAL columns: an empty cell is simply not emitted (a sparse column is not
        // landable — the personnel roster's rule) while the columns after it keep their numbers,
        // and the CREATE BOND cell is a real button rather than a text producer.
        std::vector<std::pair<int, NodeVtable>> cellNodes;
        for (size_t c = 0; c < cells.size() && c < 4; c++) {
            if (!cells[c].empty())
                cellNodes.emplace_back(static_cast<int>(c) + 1, BondCell(cells[c]));
        }
        if (offered) {
            NodeVtable bond;
            bond.Type = &ButtonType();
            bond.Announcements.emplace_back(NodeAnnouncement::Static(bondLabel));
            bond.Announcements.back().Kind = AnnouncementKinds::Label;
            bond.OnActivate = [index]() { CallBondVerb("ConfirmBond", index); };
            cellNodes.emplace_back(5, std::move(bond));
        }
        sheet.RowAt(std::move(primary), row.handle, std::move(cellNodes));

        // Every cell inherits the row's list index: acting from column 3 must act on the row you
        // are in, and the write-back is what the screen's own handlers read.
        for (int col = 0; col <= 5; col++) {
            std::string key = GraphSheet::RowKey("bonds/", row.handle, col);
            gFocus[key] = FocusTarget{index, nullptr, false, nullptr};
            if (col == 0 && !haveStart && (index == view.selected || position == 1)) {
                start = ControlId::Referenced(row.handle, key);
                haveStart = index == view.selected;
            }
        }
    }
    sheet.Finish();

    // Land on the roster: the screen name already carries the soldier, and the list is what the
    // player came to act on.
    if (start.IsValid())
        b.SetStart(std::move(start));

    AddResourceStop(b);
    return b.Build();
}

std::string BondScreenName()
{
    return NameFromHeader(
        FetchBondWords(GameUI::ReadBondScreen(gScreen)), Vocab::ScreenBondsFallback);
}

bool SetBondListIndex(void* screen, int index)
{
    return GameUI::SetBondSelection(GameUI::ReadBondScreen(screen), index);
}

// ---- bond confirmation (UISoldierBondConfirmScreen) — docs/chosen-bond-recon.md §1.5 ----
//
// Two soldiers, the benefits the new level brings, CONFIRM BOND. Enter and Escape are the
// SCREEN's own input path: ConfirmClicked runs the level-1 creation or the Training Center
// project and then the poster dialog, and Cancel() empties the staff slots this screen may have
// been opened from — a Click() on the button or a CloseScreen() poke would lose one of those.

const std::vector<WireRecord>& FetchBondConfirmWords()
{
    return gBondConfirmWords.Get({gScreen});
}

std::unique_ptr<GraphRender> BuildBondConfirm()
{
    const std::vector<WireRecord>& words = FetchBondConfirmWords();

    GraphBuilder b;
    b.BeginStop("bond");
    b.PushContext("", "", /*positions*/ false);
    int added = 0;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "soldier")
            continue;
        const std::string line = JoinParts(rec, {"rank", "class", "name"});
        if (line.empty())
            continue;
        b.AddLabel(ControlId::Structural("bondconfirm/soldier/" + RecField(rec, "index")),
            [line]() { return line; });
        added++;
    }
    b.PopContext();

    bool anyBenefit = false;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "benefit")
            continue;
        if (!anyBenefit) {
            std::string title;
            if (const WireRecord* heading = FindRecord(words, "benefits"))
                title = RecField(*heading, "label");
            b.PushContext(title);
            anyBenefit = true;
        }
        const std::string desc = RecField(rec, "desc");
        if (AddLabelledLine(b, "bondconfirm/benefit/" + RecField(rec, "index"),
                RecField(rec, "name"), desc, SpeakAgain(desc)))
            added++;
    }
    if (anyBenefit)
        b.PopContext();
    if (added == 0) {
        b.AddLabel(ControlId::Structural("bondconfirm/none"),
            []() { return Vocab::SurfaceUnavailable("Bond reader"); });
    }

    // The buttons: CONFIRM BOND on Enter, cancel on Escape — both through the screen's own
    // OnUnrealCommand. Labels are the companion's; the generic pair stands in when it answered
    // nothing, because a modal a blind player cannot leave is worse than one that reads badly.
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
    if (choices.empty()) {
        choices.push_back({std::string(Vocab::PopupContinue), GameUI::kCmdEnter});
        choices.push_back({std::string(Vocab::PopupDismiss), GameUI::kCmdEscape});
    }
    b.BeginStop("buttons");
    b.StartRow("bondconfirm/buttons");
    for (size_t i = 0; i < choices.size(); i++) {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(choices[i].label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = [cmd = choices[i].cmd]() { GameUI::SendScreenCommand(gScreen, cmd); };
        b.AddItem(
            ControlId::Structural("bondconfirm/button/" + std::to_string(i)), std::move(vt));
    }
    b.EndRow();
    return b.Build();
}

std::string BondConfirmName()
{
    return NameFromHeader(FetchBondConfirmWords(), Vocab::ScreenBondConfirmFallback);
}

// ---- the monthly report (UIResistanceReport) — docs/resistance-report-recon.md ----
//
// The council's report card: one document stop (date, supplies, bonus and penalty lines, the
// two activity lists, Avatar progress) and Continue. Every word is X2AccessReportReader's, taken
// through the screen's own functions and the two HQ states it reads; the recipe only lays them
// out. Enter is the screen's own input path — OnContinue closes AND pushes the Chosen page.

constexpr const char* kReportReaderClass = "X2AccessReportReader";
WireCache gReportWords(kReportReaderClass, "DescribeResistanceReport");

const std::vector<WireRecord>& FetchReportWords()
{
    return gReportWords.Get({gScreen});
}

// One "LABEL: value" line off a companion record, with a `max` field turning the value into a
// count against its maximum (the Avatar pips). Returns whether a node was added.
bool AddReportLine(GraphBuilder& b, const std::string& id, const WireRecord& rec)
{
    std::string value = RecField(rec, "value");
    const std::string max = RecField(rec, "max");
    if (!value.empty() && !max.empty())
        value = Vocab::Fraction(atoi(value.c_str()), atoi(max.c_str()));
    return AddLabelledLine(b, id, RecField(rec, "label"), value);
}

std::unique_ptr<GraphRender> BuildResistanceReport()
{
    const std::vector<WireRecord>& words = FetchReportWords();

    GraphBuilder b;
    b.BeginStop("report");
    b.PushContext("", "", /*positions*/ false);
    int added = 0;
    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "line")
            continue;
        if (AddReportLine(b, "report/line/" + RecField(rec, "index"), rec))
            added++;
    }
    if (added == 0) {
        b.AddLabel(ControlId::Structural("report/none"),
            []() { return Vocab::SurfaceUnavailable("Report reader"); });
    }
    b.PopContext();

    std::string continueLabel;
    if (const WireRecord* c = FindRecord(words, "continue"))
        continueLabel = RecField(*c, "label");
    AddScreenContinue(b, "report/continue", continueLabel);
    return b.Build();
}

std::string ResistanceReportName()
{
    return NameFromHeader(FetchReportWords(), Vocab::ScreenMonthlyReportFallback);
}

// ---- the report's Chosen page (UIResistanceReport_ChosenEvents) ----
//
// One column per Chosen, met or not, then Continue. An unmet Chosen's column shows one phrase
// (UNKNOWN CHOSEN) and gets one node; a met one reads identity, the knowledge meter (both ends
// when it moved this month), any new knowledge level with its bullets, last month's activities,
// and the planned action under the screen's own PLANNED ACTIVITY heading. The first knowledge
// gain of a campaign draws a warning popup over all of it, and while that is up the screen's
// Enter dismisses the popup rather than closing — so the recipe leads with the popup's words and
// its button, and offers no Continue that would not do what it says.

WireCache gChosenEventsWords(kChosenReaderClass, "DescribeChosenEvents");

const std::vector<WireRecord>& FetchChosenEventsWords(const GameUI::ChosenEventsView& view)
{
    // Discriminated on the warning: dismissing it changes the graph (the Continue appears)
    // without respawning a single widget.
    return gChosenEventsWords.Get({view.screen}, view.warningShown ? 1 : 0);
}

std::unique_ptr<GraphRender> BuildChosenEvents()
{
    const GameUI::ChosenEventsView view = GameUI::ReadChosenEvents(gScreen);
    const std::vector<WireRecord>& words = FetchChosenEventsWords(view);

    GraphBuilder b;
    int added = 0;

    if (const WireRecord* warn = FindRecord(words, "warning")) {
        b.BeginStop("warning");
        b.PushContext("", "", /*positions*/ false);
        if (AddLabelledLine(b, "chosenevents/warning/title", RecField(*warn, "title"), ""))
            added++;
        if (AddLabelledLine(b, "chosenevents/warning/text", RecField(*warn, "text"), ""))
            added++;
        b.PopContext();
        // The popup's own button (ACCEPT), through the screen's input path: OnUnrealCommand
        // routes Enter to OnPopupButton while bShowChosenWarningPopup is set.
        std::string label = RecField(*warn, "button");
        if (label.empty())
            label = Vocab::PopupContinue;
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = []() { GameUI::SendScreenCommand(gScreen, GameUI::kCmdEnter); };
        b.AddItem(ControlId::Structural("chosenevents/warning/button"), std::move(vt));
        added++;
    }

    for (const WireRecord& rec : words) {
        if (RecField(rec, "kind") != "column")
            continue;
        const std::string index = RecField(rec, "index");
        const std::string key = "chosenevents/" + index + "/";
        b.BeginStop("column" + index);
        b.PushContext("", "", /*positions*/ false);

        const std::string unknown = RecField(rec, "unknown");
        if (!unknown.empty()) {
            // The column for a Chosen not yet met draws this one phrase and nothing else.
            if (AddLabelledLine(b, key + "unknown", unknown, ""))
                added++;
            b.PopContext();
            continue;
        }

        if (AddLabelledLine(b, key + "identity", JoinParts(rec, {"class", "name", "nick"}),
                RecField(rec, "defeated")))
            added++;

        // The meter fills from last month's value to this one; when it moved, both ends are the
        // content (the Chosen summary's rule), otherwise the one number.
        const std::string before = RecField(rec, "before");
        const std::string after = RecField(rec, "after");
        std::string knowledge;
        if (!after.empty()) {
            knowledge = (!before.empty() && before != after)
                ? Vocab::KnowledgeChange(atoi(before.c_str()), atoi(after.c_str()))
                : Vocab::Percent(atoi(after.c_str()));
        }
        if (AddLabelledLine(b, key + "knowledge", RecField(rec, "knowlabel"), knowledge))
            added++;

        // A new knowledge level: the prefixed title, then its bullets (one to three).
        if (AddLabelledLine(b, key + "gain", RecField(rec, "gain"), ""))
            added++;
        int bulletIndex = 0;
        for (const std::string& bullet : RecFieldAll(rec, "bullet")) {
            if (AddLabelledLine(b, key + "bullet/" + std::to_string(bulletIndex++), bullet, ""))
                added++;
        }

        // Last month's activities under the game's own heading, one line each.
        if (AddLabelledLine(b, key + "activities", RecField(rec, "acthead"), ""))
            added++;
        int actIndex = 0;
        for (const std::string& act : RecFieldAll(rec, "act")) {
            if (AddLabelledLine(b, key + "act/" + std::to_string(actIndex++), act, ""))
                added++;
        }

        // The planned action: the heading as context, the name with its summary, the quote.
        const std::string action = RecField(rec, "action");
        const std::string summary = RecField(rec, "summary");
        const std::string quote = RecField(rec, "quote");
        if (!action.empty() || !summary.empty() || !quote.empty()) {
            b.PushContext(RecField(rec, "actionheader"), "", /*positions*/ false);
            if (AddLabelledLine(b, key + "action", action, summary, SpeakAgain(summary)))
                added++;
            if (AddLabelledLine(b, key + "quote", quote, ""))
                added++;
            b.PopContext();
        }
        b.PopContext();
    }

    if (added == 0) {
        b.BeginStop("column0");
        b.AddLabel(ControlId::Structural("chosenevents/none"),
            []() { return Vocab::SurfaceUnavailable("Chosen reader"); });
    }

    // No Continue while the warning is up — the companion withholds the record, because the
    // screen's Enter would dismiss the popup rather than close, and a node must do what it says.
    if (const WireRecord* c = FindRecord(words, "continue"))
        AddScreenContinue(b, "chosenevents/continue", RecField(*c, "label"));
    else if (added == 0)
        AddScreenContinue(b, "chosenevents/continue", std::string());

    return b.Build();
}

std::string ChosenEventsName()
{
    return NameFromHeader(FetchChosenEventsWords(GameUI::ReadChosenEvents(gScreen)),
        Vocab::ScreenChosenEventsFallback);
}

} // namespace

const Recipe kChosenInfoRecipe{"chosen dossier", ProbeFocused<GameUI::FindChosenInfoScreen>,
    BuildChosenInfo, ChosenInfoName};

// SetListIndex is the roster's; the header and the sort strip hang off plain panels.
const Recipe kBondScreenRecipe{"soldier bonds", ProbeFocused<GameUI::FindBondScreen>,
    BuildBondScreen, BondScreenName, SetBondListIndex};

const Recipe kBondConfirmRecipe{"bond confirm", ProbeFocused<GameUI::FindBondConfirmScreen>,
    BuildBondConfirm, BondConfirmName};

const Recipe kResistanceReportRecipe{"monthly report",
    ProbeFocused<GameUI::FindResistanceReportScreen>, BuildResistanceReport, ResistanceReportName};

const Recipe kChosenEventsRecipe{"chosen activity", ProbeFocused<GameUI::FindChosenEventsScreen>,
    BuildChosenEvents, ChosenEventsName};

} // namespace Recipes
} // namespace MenuNavigator
