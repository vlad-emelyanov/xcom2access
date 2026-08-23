// Avenger recipes: inside a room, the un-excavated debris room, the thirteen-screen commodity
// list, the personnel roster and the choose-facility screen — docs/surfaces/avenger.md.

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

constexpr const char* kFacilityReaderClass = "X2AccessFacilityReader";

// Cached on the button pointers, and this one is not merely an optimisation: the frame differ
// rebuilds the graph on every poll, so an uncached fetch would make an outbound ProcessEvent ten
// times a second for a set of labels that CANNOT change — AddFacilityButton runs once, in
// InitScreen, and nothing ever relabels a UIFacility_RoomFunc. A changed pointer list means a
// different room, which is the only thing that can change the answer.
WireCache gFacilityWords(kFacilityReaderClass, "DescribeFacility");

const std::vector<WireRecord>& FetchFacilityWords(const GameUI::RoomView& view)
{
    std::vector<void*> key;
    key.reserve(view.funcs.size() + 1);
    for (const GameUI::RoomFunc& f : view.funcs)
        key.push_back(f.handle);
    key.push_back(view.facility);
    return gFacilityWords.Get(std::move(key));
}

// The room's own bottom-bar buttons, and the upgrade button beside them. Split out of BuildRoom
// because it is the half that needs the companion: a UIFacility_RoomFunc retains no text at all.
void AddRoomFunctions(GraphBuilder& b, const GameUI::RoomView& view)
{
    if (view.funcs.empty() && !view.upgrade)
        return;

    b.BeginStop("room");

    if (!view.funcs.empty()) {
        const std::vector<WireRecord>& words = FetchFacilityWords(view);

        for (const GameUI::RoomFunc& f : view.funcs) {
            std::string label;
            for (const WireRecord& rec : words) {
                if (RecField(rec, "kind") == "func"
                    && atoi(RecField(rec, "index").c_str()) == f.index) {
                    label = RecField(rec, "label");
                    break;
                }
            }
            // Only a facility no shipped class covers reaches this — a mod's second button. Say
            // something placeable rather than nothing: an unnamed node the player can still
            // press beats a node that is not there.
            if (label.empty())
                label = Vocab::RoomFunctionFallback;

            ControlId id = ControlId::Referenced(f.handle, "room/func/" + std::to_string(f.index));
            const int index = f.index;
            // Focus the button itself. The shortcut list underneath keeps its own selection —
            // deliberately not cleared, because it belongs to a different screen and the game
            // leaves it alone here too.
            gFocus[id.StructuralKey] = FocusTarget{-1, f.handle, false, nullptr};

            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            vt.OnActivate = [index]() {
                GameUI::ActivateRoomFunc(GameUI::ReadRoom(gScreen), index);
            };
            b.AddItem(std::move(id), std::move(vt));
        }
    }

    if (view.upgrade) {
        ControlId id = ControlId::Referenced(view.upgrade, "room/upgrade");
        void* handle = view.upgrade;
        gFocus[id.StructuralKey] = FocusTarget{-1, handle, false, nullptr};

        NodeVtable vt;
        vt.Type = &ButtonType();
        // UIButton::Text is retained here — ShowUpgradeButton goes through SetText — so unlike
        // the bar buttons this one needs no companion. It already reads "УЛУЧШИТЬ" or
        // "ПРОСМОТРЕТЬ УЛУЧШЕНИЯ" depending on which of the three states the facility is in,
        // which is the whole distinction a sighted player gets from it.
        vt.Announcements.emplace_back(NodeAnnouncement::Static(view.upgradeLabel));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (view.upgradeDisabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        } else {
            vt.OnActivate = [handle]() { GameUI::ClickButton(handle); };
        }
        b.AddItem(std::move(id), std::move(vt));
    }
}

std::unique_ptr<GraphRender> BuildRoom()
{
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

    // The room's OWN controls before the HUD's submenu, because that is the order the game
    // itself reads them in: UIFacility.OnUnrealCommand offers the keys to the room screen first
    // and only lets them fall through to UIAvengerShortcuts when the room handles nothing. A
    // room with a bottom bar therefore consumes the arrows here, and a player arriving in the
    // Bar should land on the button the Bar actually has.
    AddRoomFunctions(b, view);

    if (view.actions.empty()) {
        if (view.funcs.empty() && !view.upgrade) {
            // Now genuinely nothing, and still not a fault: this is also the state while the
            // submenu is being rebuilt. ⛔ It used to be reached by the Bar and the Living
            // Quarters as well, whose shortcut category really is -1 — but those rooms DO have a
            // bottom-bar button, so "nothing to do here" was a lie about a screen with a visible
            // control on it (reported live 2026-08-21). The empty case has to mean empty.
            b.BeginStop("actions");
            b.AddLabel(ControlId::Structural("room/empty"), []() {
                return std::string(Vocab::RoomNoActions) + ". " + Vocab::RoomLeaveHint;
            });
        }
        return b.Build();
    }

    b.BeginStop("actions");

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
WireCache gRoomWords(kRoomReaderClass, "DescribeRoom");

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
    return gRoomWords.Get(std::move(key), dropOpen ? 1 : 0, /*alwaysStale*/ dropOpen);
}

std::unique_ptr<GraphRender> BuildRoomBuild()
{
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
WireCache gCommodityWords(kCommodityReaderClass, "DescribeCommodity");

const std::vector<WireRecord>& FetchCommodityWords(const GameUI::CommodityView& view)
{
    std::vector<void*> key;
    key.reserve(view.rows.size() + 1);
    for (const GameUI::CommodityRow& row : view.rows)
        key.push_back(row.handle);
    // Switching a Build Items tab is USUALLY covered by the row pointers alone: SwitchTab calls
    // PopulateData, which runs List.ClearItems() and spawns a fresh panel per item. "Usually" is
    // the whole reason the tab verb calls Invalidate() as well — a respawn can land on the freed
    // addresses. The screen pointer goes in too, so a second screen of the family can never
    // inherit a warm cache from the one it replaced.
    key.push_back(view.screen);
    return gCommodityWords.Get(std::move(key), view.selected);
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
            // ⛔ The respawn USUALLY changes the key, and "usually" is not good enough: the fresh
            // rows can land on the addresses the old ones just freed, and a key that happens to
            // match would keep the previous category's words on screen. Same reasoning the
            // personnel verbs have always applied — one outbound call on a keypress the player
            // just made, against a silent wrong readout.
            gCommodityWords.Invalidate();
        };
        // Post-activation feedback, read LIVE rather than from this render's closure. A captured
        // flag can only ever describe the state before the press — which spoke a bare "selected"
        // when the player pressed the tab they were already on, and nothing at all when they
        // actually switched. The invalidation above is what makes the re-ask a fresh answer.
        vt.StateText = [index]() {
            const std::vector<WireRecord>& fresh
                = FetchCommodityWords(GameUI::ReadCommodity(gScreen));
            for (const WireRecord& rec : fresh) {
                if (RecField(rec, "kind") != "tab"
                    || atoi(RecField(rec, "index").c_str()) != index)
                    continue;
                std::string line = RecField(rec, "label");
                if (RecField(rec, "selected") == "1")
                    AppendPhrase(line, Vocab::Selected);
                return line;
            }
            return std::string();
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

        // The right-hand quantity column, spoken only where the row draws one. `label` is the
        // game's own item name, so the join is AppendPhrase's — never a bare comma.
        const std::string qty = RecField(rec, "qty");
        if (!qty.empty())
            AppendPhrase(label, qty + " " + Vocab::CommodityOwned);

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

// ---- the personnel roster (UIPersonnel and its ten subclasses) ----
//
// One recipe for every door the game opens when it asks "which person?" — the Living Quarters'
// roster, the Bar's obituaries, the Armory's soldiers, staffing a facility, choosing a scientist,
// a covert action's squad. The subclasses swap which array is filled and which tabs exist; the
// layout, the rows and the input path all belong to UIPersonnel.
//
// This screen is already OPERABLE from a keyboard — Up/Down walk the list, Left/Right switch
// tabs, Enter chooses, Escape backs out — so, like save/load, the recipe is a READER first. What
// it adds is the three things the keyboard cannot get to:
//
//   * The ROWS' words. UIPersonnel_ListItem extends UIButton and then bypasses SetText, so the
//     retained Text is empty on a row visibly showing a soldier — the companion re-derives every
//     column from the UnitRef the row DID keep.
//   * The TAB STRIP's voice. Left/Right already switch tabs; CreateTabButton calls
//     DisableNavigation() and SwitchTab announces the change only into Flash, so a blind player
//     could switch all day and never learn which tab they were on.
//   * SORTING, which on mouse+keyboard is CLICK-ONLY: the keyboard branches for it are
//     FXS_BUTTON_X and FXS_DPAD_LEFT/RIGHT, all three controller constants. On a roster of twenty
//     soldiers the sort column IS the reading order, which makes it the one verb here worth
//     re-providing rather than merely narrating.
//
// STOPS in the screen's own top-to-bottom order — tabs, sort header, list — but focus STARTS on
// the list, because that is where the game itself puts it (UpdateList ends with
// Navigator.SetSelected(m_kList)). Reading order and landing spot are different questions, and
// answering them separately is what keeps a player from Tabbing twice to reach the thing they
// opened the screen for.

constexpr const char* kPersonnelReaderClass = "X2AccessPersonnelReader";

// Same self-invalidating cache as the commodity list, and for the same two reasons: the row
// pointers change wholesale whenever the list is rebuilt (UpdateList calls ClearItems and
// PopulateListInstantly respawns every item, which is what a tab switch and every sort do), and
// the screen pointer keeps a second roster from inheriting the first one's words.
//
// ⛔ The SELECTION is deliberately NOT part of the key, unlike the commodity list's. Nothing on
// this screen is the selection's shadow — there is no card — so re-asking on every arrow press
// would walk the whole roster through XCOMHISTORY once per keystroke for no new words.
WireCache gPersonnelWords(kPersonnelReaderClass, "DescribePersonnel");

const std::vector<WireRecord>& FetchPersonnelWords(const GameUI::PersonnelView& view)
{
    std::vector<void*> key;
    key.reserve(view.rows.size() + 1);
    for (const GameUI::PersonnelRow& row : view.rows)
        key.push_back(row.handle);
    key.push_back(view.screen);
    return gPersonnelWords.Get(std::move(key));
}

// One outbound call with a single int, for the two verbs the companion owns. Both resolve the
// screen themselves from the stack, exactly as DescribePersonnel does, so there is nothing to
// pass but the index.
bool CallPersonnelVerb(const char* funcName, int value)
{
    void* cdo = GameUI::FindCompanionCDO(kPersonnelReaderClass);
    if (!cdo)
        return false;
    struct {
        int32_t Value;
    } params{value};
    if (!GameUI::CallScriptFunction(cdo, funcName, &params)) {
        LOG_WARN("MenuNavigator: %s.%s failed", kPersonnelReaderClass, funcName);
        return false;
    }
    // Both verbs respawn the rows, so the key USUALLY changes on its own — but the fresh
    // pointers can land on the freed addresses of the old ones, and a key that happens to match
    // would keep the previous tab's words on screen.
    gPersonnelWords.Invalidate();
    return true;
}

void BuildPersonnelTabs(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    bool any = false;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "tab")
            continue;
        const std::string label = RecField(rec, "label");
        if (label.empty())
            continue;
        const int tab = atoi(RecField(rec, "tab").c_str());
        const bool selected = RecField(rec, "selected") == "1";

        if (!any) {
            b.BeginStop("tabs");
            b.StartRow("personnel/tabs");
            any = true;
        }

        // Not in gFocus: every tab button called DisableNavigation(), so asking the navigator to
        // hold one would be asking for something the game refuses to keep.
        ControlId id = ControlId::Structural("personnel/tab/" + std::to_string(tab));
        NodeVtable vt;
        vt.Type = &TabType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement(
            [selected]() { return selected ? std::string(Vocab::Selected) : std::string(); },
            false, AnnouncementKinds::Selected));
        vt.OnActivate = [tab]() { CallPersonnelVerb("SwitchPersonnelTab", tab); };
        // Read LIVE, for the reason the commodity strip earned it: a flag captured at build time
        // can only describe the state before the press.
        vt.StateText = [tab]() {
            const std::vector<WireRecord>& fresh
                = FetchPersonnelWords(GameUI::ReadPersonnel(gScreen));
            for (const WireRecord& rec : fresh) {
                if (RecField(rec, "kind") != "tab" || atoi(RecField(rec, "tab").c_str()) != tab)
                    continue;
                std::string line = RecField(rec, "label");
                if (RecField(rec, "selected") == "1")
                    AppendPhrase(line, Vocab::Selected);
                return line;
            }
            return std::string();
        };
        b.AddItem(std::move(id), std::move(vt));
    }
    if (any)
        b.EndRow();
}

void BuildPersonnelSort(GraphBuilder& b, const std::vector<WireRecord>& records)
{
    bool any = false;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "sort")
            continue;
        const std::string label = RecField(rec, "label");
        if (label.empty())
            continue;
        const int index = atoi(RecField(rec, "index").c_str());
        const bool selected = RecField(rec, "selected") == "1";
        const bool descending = RecField(rec, "desc") == "1";

        if (!any) {
            b.BeginStop("sort");
            b.StartRow("personnel/sort");
            any = true;
        }

        ControlId id = ControlId::Structural("personnel/sort/" + std::to_string(index));
        NodeVtable vt;
        vt.Type = &ButtonType();
        std::string line = std::string(Vocab::PersonnelSortBy) + " " + label;
        if (selected)
            AppendPhrase(line, descending ? Vocab::SortDescending : Vocab::SortAscending);
        vt.Announcements.emplace_back(NodeAnnouncement::Static(line));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The game's own mouse handler, which flips the direction when the column is already the
        // sort column and re-sorts the list either way. Re-read afterwards rather than closing
        // over the flag: pressing this changes exactly the thing the node reports.
        vt.OnActivate = [index]() { CallPersonnelVerb("ClickPersonnelSort", index); };
        vt.StateText = [index]() {
            const std::vector<WireRecord>& fresh
                = FetchPersonnelWords(GameUI::ReadPersonnel(gScreen));
            for (const WireRecord& rec : fresh) {
                if (RecField(rec, "kind") != "sort"
                    || atoi(RecField(rec, "index").c_str()) != index)
                    continue;
                std::string text
                    = std::string(Vocab::PersonnelSortBy) + " " + RecField(rec, "label");
                if (RecField(rec, "selected") == "1") {
                    AppendPhrase(text,
                        RecField(rec, "desc") == "1" ? Vocab::SortDescending
                                                     : Vocab::SortAscending);
                }
                return text;
            }
            return std::string();
        };
        b.AddItem(std::move(id), std::move(vt));
    }
    if (any)
        b.EndRow();
}

// The roster is a TABLE, so it is emitted as one — GraphSheet, not a list of flattened
// sentences. Column 0 is the person's NAME and carries every other cell as extra announcement
// parts, so walking DOWN reads the whole row exactly as a flat list would; stepping RIGHT lands
// on one fact at a time, each announced with the column heading the game draws over it, and
// Up/Down then reads that ONE column down the roster. On twenty soldiers, "who is wounded" stops
// being twenty paragraphs and becomes twenty words.

SheetConfig PersonnelSheetConfig()
{
    return SheetConfig{Vocab::TableBlank, Vocab::RoleTable, &TextType()};
}

std::unique_ptr<GraphRender> BuildPersonnel()
{
    GameUI::PersonnelView view = GameUI::ReadPersonnel(gScreen);
    const std::vector<WireRecord>& records = FetchPersonnelWords(view);

    GraphBuilder b;

    BuildPersonnelTabs(b, records);
    BuildPersonnelSort(b, records);

    b.BeginStop("list");

    if (view.rows.empty()) {
        // The game's own empty line — "НЕТ ПОГИБШИХ СОЛДАТ" in the memorial, and one per tab
        // elsewhere. It exists on screen (PopulateListInstantly pushes it into Flash, and the
        // Bar spawns a UIText for its own), so this is parity, not a placeholder.
        std::string text;
        if (const WireRecord* empty = FindRecord(records, "empty"))
            text = RecField(*empty, "text");
        if (text.empty())
            text = Vocab::PersonnelEmpty;
        text += ". ";
        text += Vocab::CommodityLeaveHint;
        b.AddLabel(ControlId::Structural("personnel/empty"), [text]() { return text; });
        return b.Build();
    }

    std::vector<std::string> columns;
    if (const WireRecord* cols = FindRecord(records, "cols"))
        columns = RecFieldAll(*cols, "col");

    GraphSheet sheet(b, "personnel/", PersonnelSheetConfig());
    // No region label: the screen is already named by RecipeName, and a second title here would
    // repeat it on every entry.
    sheet.Region("", columns);

    ControlId start;
    bool haveStart = false;
    const int rowCount = static_cast<int>(view.rows.size());
    int position = 0;

    for (const GameUI::PersonnelRow& row : view.rows) {
        const WireRecord* rec = nullptr;
        for (const WireRecord& candidate : records) {
            if (RecField(candidate, "kind") == "row"
                && atoi(RecField(candidate, "index").c_str()) == row.index) {
                rec = &candidate;
                break;
            }
        }

        std::string name = rec ? RecField(*rec, "name") : std::string();
        if (name.empty())
            name = Vocab::SoldierFallback;
        std::vector<std::string> cells
            = rec ? RecFieldAll(*rec, "cell") : std::vector<std::string>();
        const bool promote = rec && !RecField(*rec, "promote").empty();

        const int index = row.index;
        position++;

        NodeVtable primary;
        primary.Type = &ButtonType();
        primary.Announcements.emplace_back(NodeAnnouncement::Static(name));
        primary.Announcements.back().Kind = AnnouncementKinds::Label;
        // The row's other facts, carried on the PRIMARY as extra parts — this is what keeps the
        // flat reading and the tabular one the same graph. Value-kind so they land where a
        // control's value lands rather than after the position count, and stable_sort holds
        // their declaration order, so they read left to right as the row is drawn.
        for (const std::string& cell : cells) {
            if (cell.empty())
                continue;
            primary.Announcements.emplace_back(NodeAnnouncement::Static(cell));
            primary.Announcements.back().Kind = AnnouncementKinds::Value;
        }

        if (promote) {
            // The row draws a badge for a soldier ready to be promoted — a whole affordance
            // carried by an icon, which speech otherwise loses entirely.
            primary.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::PersonnelPromote));
            primary.Announcements.back().Kind = kKindAttention;
        }

        if (row.disabled) {
            // Real, and the game enforces it: every OnXSelected delegate opens by testing
            // IsDisabled and doing nothing. The armory sets it on soldiers away on covert
            // actions; the tutorial sets it on everyone but the one soldier the script wants.
            primary.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            primary.Announcements.back().Kind = AnnouncementKinds::Enabled;
        } else {
            // Re-read at activation: the handler runs from the input drain, a frame or more
            // after this render, and any sort or tab change respawns every row. The index is
            // the game's own, which is what makes matching on it safe.
            primary.OnActivate = [index]() {
                GameUI::ActivatePersonnelRow(GameUI::ReadPersonnel(gScreen), index);
            };
        }

        // A sheet is raw mode underneath, so the builder stamps no automatic positions — and on
        // a roster "4 of 20" is one of the facts a blind player most wants, so it is added
        // explicitly. Position-kind, so the user's per-kind filter still governs it.
        primary.Announcements.emplace_back(
            NodeAnnouncement::Static(Vocab::Position(position, rowCount)));
        primary.Announcements.back().Kind = AnnouncementKinds::Position;

        std::vector<std::function<std::string()>> producers;
        producers.reserve(cells.size());
        for (const std::string& cell : cells) {
            // An empty cell is left NULL rather than blank: a sparse column is not landable, so
            // arrowing across a scientist's row does not stop on the rank they do not have —
            // while the columns AFTER it keep their numbers, which is what lets Up/Down match
            // columns across rows of different shapes.
            if (cell.empty())
                producers.push_back(nullptr);
            else
                producers.push_back([cell]() { return cell; });
        }

        sheet.Row(std::move(primary), row.handle, std::move(producers));

        // The primary's own key, rebuilt the way GraphSheet builds it, so the focus write-back
        // finds it. Every CELL inherits the row's list index too: selecting is what Enter reads
        // back, and a player acting from column 3 must act on the row they are in.
        for (int col = 0; col <= static_cast<int>(cells.size()); col++) {
            std::string key = GraphSheet::RowKey("personnel/", row.handle, col);
            gFocus[key] = FocusTarget{index, nullptr, false, nullptr};
            if (col == 0 && !haveStart && (index == view.selected || position == 1)) {
                start = ControlId::Referenced(row.handle, key);
                haveStart = index == view.selected;
            }
        }
    }
    sheet.Finish();

    // Land where the game itself lands. UpdateList ends with Navigator.SetSelected(m_kList), so
    // the roster — not the tab strip above it — is what a sighted player is already on.
    if (start.IsValid())
        b.SetStart(std::move(start));

    return b.Build();
}

void* ProbePersonnel()
{
    void* screen = GameUI::FindPersonnelScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string PersonnelName()
{
    // UIPersonnel.Title is retained under a dirty check — but SetScreenHeader is called ONLY by
    // RefreshTitle, and only when the screen needs a single tab. With a strip up there is no
    // header at all, and the tab names are the heading a sighted player reads instead, so the
    // selected tab's label stands in for it.
    const std::vector<WireRecord>& records = FetchPersonnelWords(GameUI::ReadPersonnel(gScreen));
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == "tab" && RecField(rec, "selected") == "1")
            return RecField(rec, "label");
    }
    if (const WireRecord* header = FindRecord(records, "header")) {
        const std::string title = RecField(*header, "title");
        if (!title.empty())
            return title;
    }
    return Vocab::ScreenPersonnelFallback;
}

bool SetPersonnelListIndex(void* screen, int index)
{
    return GameUI::SetPersonnelSelection(GameUI::ReadPersonnel(screen), index);
}

// ---- choosing what to build in a room (UIChooseFacility) ----
//
// The screen build mode's Enter opens, and the only way a facility ever gets onto the Avenger. It
// LOOKS like a commodity screen — a list beside a panel of cost, power, time and description —
// but it `extends UIScreen` rather than UIInventory, so the thirteen-screen recipe never saw it
// and it was SILENT (reported live 2026-08-21: "I selected an empty room and the mod went
// silent").
//
// Two stops, in the screen's own left-to-right order: the list, then the panel with one node per
// LINE. Split for the same reason the commodity card is: the description is a paragraph and
// "what does it cost" should not require sitting through it.
//
// ⛔ Native reads the ROWS and the companion reads the PANEL, because the two halves fail in
// opposite directions — PopulateList goes through UIListItemString.SetText (names retained,
// bDisabled retained, attention icon spawned) while UpdateSelection pushes every number into
// Flash and keeps none.

constexpr const char* kBuildFacilityReaderClass = "X2AccessBuildFacilityReader";

// Keyed on the rows AND the selection, like the commodity list's and for the same reason: the
// panel is the selection's shadow (UIList::OnSelectionChanged = UpdateSelection repopulates it on
// every move), so a pointer-only key would describe the facility the player just left.
WireCache gBuildFacilityWords(kBuildFacilityReaderClass, "DescribeFacilityChoice");

const std::vector<WireRecord>& FetchBuildFacilityWords(const GameUI::ChooseFacilityView& view)
{
    std::vector<void*> key;
    key.reserve(view.choices.size() + 1);
    for (const GameUI::FacilityChoice& choice : view.choices)
        key.push_back(choice.handle);
    key.push_back(view.screen);
    return gBuildFacilityWords.Get(std::move(key), view.selected);
}

void BuildFacilityCard(GraphBuilder& b, const WireRecord& card)
{
    struct Line {
        const char* label;
        const char* value;
    };
    // The panel's own order: what it is, what it costs, what it draws, what else it needs, how
    // long, what it costs every month, why you cannot have it, and finally what it does.
    const Line lines[] = {
        {nullptr, "title"},
        {nullptr, "cost"},
        {nullptr, "power"},
        {nullptr, "reqs"},
        {"timeLabel", "time"},
        {nullptr, "upkeep"},
        {nullptr, "warning"},
        {nullptr, "desc"},
    };

    bool any = false;
    for (const Line& line : lines) {
        if (RecField(card, line.value).empty())
            continue; // the panel is not drawing this line
        if (!any) {
            b.BeginStop("details");
            any = true;
        }

        // Read at ANNOUNCE time with the build-time text as fallback: UpdateSelection repopulates
        // the panel the instant our write-back moves the selection, while this graph is up to one
        // poll old — so a Tab pressed hard on the heels of an arrow would describe the facility
        // just left. Same guard the commodity card earned.
        std::string fallback = ComposeCardLine(card, line.label, line.value);
        b.AddLabel(ControlId::Structural(std::string("buildfacility/card/") + line.value),
            [labelKey = std::string(line.label ? line.label : ""),
                valueKey = std::string(line.value), fallback]() {
                const std::vector<WireRecord>& fresh
                    = FetchBuildFacilityWords(GameUI::ReadChooseFacility(gScreen));
                const WireRecord* live = FindRecord(fresh, "card");
                if (!live)
                    return fallback;
                std::string text = ComposeCardLine(
                    *live, labelKey.empty() ? nullptr : labelKey.c_str(), valueKey.c_str());
                return text.empty() ? fallback : text;
            });
    }
}

std::unique_ptr<GraphRender> BuildChooseFacility()
{
    GameUI::ChooseFacilityView view = GameUI::ReadChooseFacility(gScreen);
    const std::vector<WireRecord>& records = FetchBuildFacilityWords(view);

    GraphBuilder b;
    b.BeginStop("list");
    b.SetRegion("list");

    if (view.choices.empty()) {
        b.AddLabel(ControlId::Structural("buildfacility/empty"), []() {
            return std::string(Vocab::CommodityEmpty) + ". " + Vocab::CommodityLeaveHint;
        });
        return b.Build();
    }

    for (const GameUI::FacilityChoice& choice : view.choices) {
        ControlId id = ControlId::Referenced(
            choice.handle, "buildfacility/" + std::to_string(choice.index));
        const int index = choice.index;
        gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false, nullptr};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(choice.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;

        if (choice.priority) {
            // The game spawns an attention icon for a bPriority facility and sorts it to the
            // front — a nudge carried entirely by a glyph and a position.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Attention));
            vt.Announcements.back().Kind = kKindAttention;
        }
        if (choice.disabled) {
            // CanBuildFacility said no. The REASON is on the card (insufficient power, or the
            // cost line the player cannot meet), which is why this is a flag and not a sentence.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        // Passed through even when disabled: OnAccept re-tests and plays its own refusal, which
        // keeps the refusal where the game can be right about why.
        vt.OnActivate = [index]() {
            GameUI::ActivateChooseFacility(GameUI::ReadChooseFacility(gScreen), index);
        };
        b.AddItem(std::move(id), std::move(vt));
    }

    if (const WireRecord* card = FindRecord(records, "card"))
        BuildFacilityCard(b, *card);

    return b.Build();
}

void* ProbeChooseFacility()
{
    void* screen = GameUI::FindChooseFacilityScreen();
    return (screen && GameUI::IsScreenFocused(screen)) ? screen : nullptr;
}

std::string ChooseFacilityName()
{
    const std::vector<WireRecord>& records
        = FetchBuildFacilityWords(GameUI::ReadChooseFacility(gScreen));
    std::string name;
    if (const WireRecord* header = FindRecord(records, "header")) {
        name = RecField(*header, "title");
        AppendPhrase(name, RecField(*header, "subtitle"));
    }
    if (name.empty())
        name = Vocab::ScreenChooseFacilityFallback;
    return name;
}

bool SetChooseFacilityListIndex(void* screen, int index)
{
    return GameUI::SetChooseFacilitySelection(GameUI::ReadChooseFacility(screen), index);
}

} // namespace

// ---- the recipes this file defines ----
//
// ⛔ THE PROBE ORDER IS NOT HERE. It is the `kRecipes` array literal in menu_navigator.cpp,
// the one place that lists every recipe in a single readable sequence next to the loop that
// consumes it. Adding a recipe means adding it there as well as declaring it in recipes.h; a
// recipe declared and never listed is a screen that is silently never probed. Each definition
// below keeps the sentence that argues where in that order it belongs.

// Above the ant farm it is entered from, below anything the room's own verbs push over it
// (choose-research, the facility summary, a dialog) — which is also why ProbeRoom insists the
// facility screen is TOPMOST rather than merely present.
const Recipe kRoomRecipe{"avenger room", ProbeRoom, BuildRoom, RoomName, SetRoomListIndex};

const Recipe kRoomBuildRecipe{
    "avenger debris room", ProbeRoomBuild, BuildRoomBuild, RoomBuildName, SetRoomBuildListIndex};

// Below anything its own rows push (the build-item confirmation, the black market's dialog) and
// below the pause menu, but above the room screen it is opened from — the Research lab's
// UIFacility is still on the stack underneath and probes ready.
const Recipe kCommodityRecipe{
    "commodity list", ProbeCommodity, BuildCommodity, CommodityName, SetCommodityListIndex};

// Above every screen it is opened from — the room, the armory, squad select, the debris room's
// build slots — and below any dialog it raises.
const Recipe kPersonnelRecipe{
    "personnel roster", ProbePersonnel, BuildPersonnel, PersonnelName, SetPersonnelListIndex};

// Above the facility grid and UIBuildFacilities it is opened from, below any dialog it raises.
const Recipe kChooseFacilityRecipe{"choose facility", ProbeChooseFacility, BuildChooseFacility,
    ChooseFacilityName, SetChooseFacilityListIndex};

} // namespace Recipes
} // namespace MenuNavigator
