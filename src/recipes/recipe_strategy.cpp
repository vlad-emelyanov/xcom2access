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
    void* cdo = GameUI::FindCompanionCDO(kGeoScannerClass);
    if (!cdo)
        return;
    struct {
        int32_t RegionID;
        int32_t Verb;
    } params{regionId, verb};
    if (!GameUI::CallScriptFunction(cdo, "ActivateRegionAction", &params)) {
        LOG_WARN("MenuNavigator: %s.ActivateRegionAction failed", kGeoScannerClass);
        return;
    }
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
    std::vector<void*> key;
    key.reserve(view.rows.size() + 1);
    for (const GameUI::CovertActionRow& row : view.rows)
        key.push_back(row.handle);
    key.push_back(view.screen);
    return gCovertActionWords.Get(std::move(key), view.actionId);
}

// One outbound call with a single int, the personnel verbs' shape. Both slot verbs resolve the
// screen themselves and gate on the slot's own IsDisabled, so there is nothing to pass but the
// index — which is the ActionSlots array index the companion emitted.
void CallCovertActionSlotVerb(const char* funcName, int index)
{
    void* cdo = GameUI::FindCompanionCDO(kCovertActionReaderClass);
    if (!cdo)
        return;
    struct {
        int32_t Index;
    } params{index};
    if (!GameUI::CallScriptFunction(cdo, funcName, &params)) {
        LOG_WARN("MenuNavigator: %s.%s failed", kCovertActionReaderClass, funcName);
        return;
    }
    // Both verbs change the slots — one opens the personnel picker and the other the armory, and
    // clearing a slot rewrites the card and the risks in place without respawning a single row.
    // The pointer key cannot see any of that, so the cache is dropped by hand.
    gCovertActionWords.Invalidate();
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

    if (start.IsValid())
        b.SetStart(start);
    return b.Build();
}

// ⚠ No second focus test here, unlike ProbeChooseFacility's: FindCovertActionsScreen already
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
    void* cdo = GameUI::FindCompanionCDO(kPolicyReaderClass);
    if (!cdo)
        return;
    struct {
        int32_t Column;
        int32_t Slot;
    } params{column, slot};
    if (!GameUI::CallScriptFunction(cdo, funcName, &params)) {
        LOG_WARN("MenuNavigator: %s.%s failed", kPolicyReaderClass, funcName);
        return;
    }
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

// One card, whether it is an order, a locked slot or an empty one. The three read completely
// differently and only the first has a name at all.
NodeVtable PolicyCardNode(const GameUI::PolicyCard& card, const WireRecord* rec,
    const std::string& removeLabel, bool canPlay)
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

    const int column = card.column;
    const int slot = card.slot;
    // Offered even when the game will refuse: the refusal raises UICantChangeOrders(), which is
    // the only statement anywhere of WHY, and the popup recipe reads it. The screen name already
    // said so once on attach, so this is the player checking rather than being told twice.
    vt.OnActivate = [column, slot]() { CallPolicyVerb("SelectPolicyCard", column, slot); };

    // Backspace takes a card back out of a slot — vanilla's FXS_BUTTON_X, a gamepad button. Only
    // where the game would allow it: a hand card has nowhere to go back to, and a vital order
    // (MarkCardAsSpecial) cannot be removed at all.
    if (canPlay && column >= 0 && !card.locked && !card.vital && card.cardId != 0
        && (!rec || RecField(*rec, "removable") != "0")) {
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
                    view.canPlay));
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
                    view.canPlay));
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
    "squad select", ProbeSquadSelect, BuildSquadSelect, SquadSelectName};

const Recipe kArmoryLoadoutRecipe{
    "armory loadout", ProbeArmoryLoadout, BuildArmoryLoadout, ArmoryLoadoutName};

// Above the ant farm and the Avenger HUD it sits over, below anything it pushes (a mission
// briefing, the pause menu, a dialog) — which is what ProbeGeoscape's "must be the current
// screen" test enforces, mirroring UIStrategyMap's own input routing.
const Recipe kGeoscapeRecipe{"geoscape tray", ProbeGeoscape, BuildGeoscape, GeoscapeName};

const Recipe kPopupRecipe{"popup", ProbePopup, BuildPopup, PopupName};

// Above the Resistance Ring it is pushed over (the room recipe) and above the personnel roster's
// family only in the sense that it can never be topmost at the same time — UIPersonnel_CovertAction
// is PUSHED ON TOP of this screen when a slot asks for a soldier, so the roster must win while it
// is up. ProbeCovertActions insists on being the current screen, which settles both: it stands
// down under the picker, under the confirm popup, and under the armory the EDIT verb opens.
const Recipe kCovertActionsRecipe{"covert actions", ProbeCovertActions, BuildCovertActions,
    CovertActionsName, SetCovertActionListIndex};

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

void* ProbePromotion()
{
    void* screen = GameUI::FindPromotionScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string PromotionName()
{
    return Vocab::ScreenPromotion;
}

const Recipe kPromotionRecipe{"promotion", ProbePromotion, BuildPromotion, PromotionName};

} // namespace Recipes
} // namespace MenuNavigator
