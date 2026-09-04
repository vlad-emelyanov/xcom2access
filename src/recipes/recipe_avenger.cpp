// Avenger recipes: inside a room, the un-excavated debris room, the thirteen-screen commodity
// list, the personnel roster and the choose-facility screen — docs/surfaces/avenger.md.

// Shared by every recipe TU: the recipe contract, the attach state a builder reads, the
// control-type registry and the companion-wire cache. See src/recipes/recipe_context.h.
#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "game_ui.h"
#include "log.h"
#include "objective_reader.h"
#include "tts.h"
#include "vocab.h"

// <cstdint> for the int32_t in CallPersonnelVerb's hand-rolled param struct. It used to arrive
// by accident, through input_arbiter.h, back when this code lived in menu_navigator.cpp; that
// header is gone from this TU and nothing else here promises the fixed-width types.
#include <cstdint>
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

// Cached on the widget pointers, and this one is not merely an optimisation: the frame differ
// rebuilds the graph on every poll, so an uncached fetch would make an outbound ProcessEvent ten
// times a second. The bottom-bar labels CANNOT change (AddFacilityButton runs once, in
// InitScreen, and nothing ever relabels a UIFacility_RoomFunc); the staff slots CAN, and in
// place — RealizeStaffSlots calls UpdateData() on the widgets it already has rather than
// respawning them — which is why the key below carries the picker as well as the pointers and
// re-asks unconditionally while it is open, exactly as the debris room does.
WireCache gFacilityWords(kFacilityReaderClass, "DescribeFacility");

const std::vector<WireRecord>& FetchFacilityWords(const GameUI::RoomView& view)
{
    std::vector<void*> key;
    key.reserve(view.funcs.size() + view.staffSlots.size() + 2);
    for (const GameUI::RoomFunc& f : view.funcs)
        key.push_back(f.handle);
    key.insert(key.end(), view.staffSlots.begin(), view.staffSlots.end());
    key.push_back(view.dropDown);
    key.push_back(view.facility);
    // ⛔ Re-ask on every render while the picker is OPEN: choosing a staffer rewrites the slot's
    // occupant (UpdateData on the same widget) and, on the project slots, the game closes the
    // picker BEFORE the dialog that commits the choice — so neither the pointer set nor the
    // open/closed edge alone can be trusted to have seen the new words. The picker being up is
    // the only state in which the room changes under the player without a screen change, and a
    // screen change invalidates every cache anyway.
    const bool dropOpen = view.dropDown != nullptr;
    return gFacilityWords.Get(std::move(key), dropOpen ? 1 : 0, /*alwaysStale*/ dropOpen);
}

// ---- staff slots and the personnel picker — shared by the built room and the debris room ----
//
// One widget class (UIStaffSlot, in a UIStaffContainer), one picker class (UIPersonnel_DropDown
// on that container), one companion record shape (X2AccessStaffSlots emits kind=slot and
// kind=candidate for BOTH readers) — so one node builder and one picker builder. The two rooms
// differ only in which view carries the pointers, which is what the two `live*` callbacks below
// abstract over. ⛔ Two copies of this were the trap: the debris room had a working picker and
// the built room had none, so a player could clear a room by keyboard and then could not staff
// the facility they built in it (reported 2026-09-03: "I can't assign engineers to the power
// relay or soldiers to the training centre").

// A slot node: the occupant, or the template's own open-slot wording, then the bonus — and then
// the state a sighted player reads off the widget's padlock or greying, in the game's own words
// where it has any (the hover tooltip UIFacility_StaffSlot.RefreshTooltip would show).
//
// ⛔ Every state here is still ACTIVATABLE, because the game's own HandleClick is: a padlocked
// slot opens the upgrade screen that unlocks it, a greyed slot still opens its (empty) picker,
// and a filled project slot raises the stop-training confirmation. Marking any of them
// Vocab::Disabled would take Enter away from a control the mouse can press.
NodeVtable StaffSlotVtable(const WireRecord& rec, std::function<void*()> liveWidget)
{
    std::string label = RecField(rec, "label");
    AppendPhrase(label, RecField(rec, "bonus"));
    const bool locked = RecField(rec, "locked") == "1";
    const bool empty = RecField(rec, "filled") != "1";
    const bool available = RecField(rec, "available") == "1";
    const std::string hint = RecField(rec, "hint");

    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;

    if (locked) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::RoomSlotLocked));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        if (!hint.empty()) {
            // "Upgrade the facility to unlock the staff slot" — the reason AND what Enter does.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(hint));
            vt.Announcements.back().Kind = AnnouncementKinds::Reason;
        }
    } else if (empty && !available) {
        // "No one is available" is NOT the same as "you cannot press this": the game greys the
        // slot but still opens its picker. Spoken as a Reason, in the game's words when the slot
        // class has them (a facility slot) and in ours when it does not (a build slot).
        vt.Announcements.emplace_back(
            NodeAnnouncement::Static(hint.empty() ? std::string(Vocab::RoomNoStaff) : hint));
        vt.Announcements.back().Kind = AnnouncementKinds::Reason;
    } else if (!hint.empty()) {
        // The hover tooltip of a slot that can be filled ("Assign an engineer to receive the
        // bonus") is instruction rather than state, so it goes where every other control's
        // game tooltip goes: Space.
        vt.OnTooltip = [hint]() { TTS::SpeakInterrupt(hint); };
    }

    vt.OnActivate = [liveWidget]() {
        if (void* widget = liveWidget())
            GameUI::OpenStaffSlotPicker(widget);
    };
    return vt;
}

// The picker's rows as the WHOLE graph while it is open. The game routes every key into the
// picker while it shows (UIStaffContainer.OnUnrealCommand tests its bIsVisible before anything
// else), so offering the room's own stops behind it would be a keyboard the game does not have.
// `liveDropList` re-resolves the list at activation, a frame or more after this render.
void BuildStaffPicker(GraphBuilder& b, const std::vector<WireRecord>& records,
    const std::string& keyPrefix, void* (*liveDropList)())
{
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

        ControlId id = ControlId::Structural(keyPrefix + "/pick/" + std::to_string(index));
        gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false, nullptr};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = [index, liveDropList]() {
            GameUI::ChooseStaffCandidate(liveDropList(), index);
        };
        b.AddItem(std::move(id), std::move(vt));
        shown++;
    }
    if (shown == 0) {
        // The picker opens even with nothing to put in it — IsUnitAvailableForThisSlot is what
        // greys the SLOT, not what closes the list. Saying so beats an empty graph, which would
        // read as the layer having failed.
        b.AddLabel(ControlId::Structural(keyPrefix + "/pick/empty"),
            []() { return std::string(Vocab::RoomNoStaff); });
    }
}

void* LiveRoomDropList()
{
    return GameUI::ReadRoom(gScreen).dropList;
}

void* LiveRoomBuildDropList()
{
    return GameUI::ReadRoomBuild(gScreen).dropList;
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

// The Resistance Ring's covert-op readout — "COVERT ACTIONS, Recruit Scientist, 12 Days", or the
// game's own "NO CURRENT COVERT ACTION, Assign a new action". UIFacility_CovertOpsStatus retains
// nothing (see the companion's header), so this is a wire read like the bottom-bar labels; the
// record is absent on every room but the Ring, which is the only one that spawns the panel.
std::string RoomCovertOpsLine(const GameUI::RoomView& view)
{
    // ⛔ NOT gated on view.funcs being non-empty, tempting though it is — the Ring happens to
    // have a bottom-bar button too, so the gate would work today and would turn any future room
    // that grows a status panel without a button into SILENCE, which is the one failure mode a
    // blind player cannot tell from "there is nothing here". The cost of dropping it is one
    // outbound call per room ENTRY, not per render: the cache below keys on the screen.
    const std::vector<WireRecord>& words = FetchFacilityWords(view);
    const WireRecord* rec = FindRecord(words, "covertops");
    if (!rec)
        return {};
    return JoinParts(*rec, {"label", "title", "time"});
}

std::unique_ptr<GraphRender> BuildRoom()
{
    GameUI::RoomView view = GameUI::ReadRoom(gScreen);

    GraphBuilder b;

    // --- shape 2: the picker owns the keys --- (see BuildStaffPicker)
    if (view.dropDown) {
        BuildStaffPicker(b, FetchFacilityWords(view), "room", LiveRoomDropList);
        return b.Build();
    }

    // --- shape 1: the room itself ---

    // The room's STATUS lines first, as their own stop: "SCIENTISTS: 0" is drawn in the room and
    // is reachable by no keyboard in the game. It is also the one number that decides whether the
    // room's verbs will do anything — a lab with no scientists researches nothing — so it reads
    // before the actions rather than after them. The Ring's covert-op panel is the same kind of
    // fact about the same kind of question, so it joins the same stop.
    bool status = false;
    if (!view.staffTitle.empty() && !view.staffSkill.empty()) {
        b.BeginStop("crew");
        status = true;
        const std::string crew = view.staffTitle + Vocab::RoomCrewSeparator + view.staffSkill;
        b.AddLabel(ControlId::Structural("room/crew"), [crew]() { return crew; });
    }

    // The staff SLOTS, under the crew line they are drawn under: the same widget (the crew line
    // is the container's title, the slots its children), so the same stop — walking down from
    // "ENGINEERS: 2" lands on the first slot. Only a BUILT facility has any; the seven fixed
    // rooms declare none, which is the whole reason the recipe existed for weeks without them.
    // ⛔ The record's index is the CONTAINER's, matched against the widgets the same read saw,
    // and never used to index the facility's own StaffSlots (the container skips slots it does
    // not show).
    if (!view.staffSlots.empty()) {
        for (const WireRecord& rec : FetchFacilityWords(view)) {
            if (RecField(rec, "kind") != "slot")
                continue;
            const int index = atoi(RecField(rec, "index").c_str());
            if (index < 0 || static_cast<size_t>(index) >= view.staffSlots.size())
                continue;
            if (!status) {
                b.BeginStop("crew");
                status = true;
            }
            void* handle = view.staffSlots[static_cast<size_t>(index)];
            ControlId id = ControlId::Referenced(handle, "room/slot/" + std::to_string(index));
            // Focus the widget itself, as the bottom-bar buttons do: UIStaffSlot.OnReceiveFocus
            // draws nothing on mouse+keyboard, so this keeps the container's own SelectedStaffSlot
            // honest and changes nothing the player can see.
            gFocus[id.StructuralKey] = FocusTarget{-1, handle, false, nullptr};
            const size_t pos = static_cast<size_t>(index);
            b.AddItem(std::move(id), StaffSlotVtable(rec, [pos]() -> void* {
                GameUI::RoomView live = GameUI::ReadRoom(gScreen);
                return pos < live.staffSlots.size() ? live.staffSlots[pos] : nullptr;
            }));
        }
    }

    const std::string covertOps = RoomCovertOpsLine(view);
    if (!covertOps.empty()) {
        // Same stop KEY whether or not the crew line opened it, so the stop's remembered position
        // survives a room that has one line and not the other.
        if (!status)
            b.BeginStop("crew");
        b.AddLabel(ControlId::Structural("room/covertops"), [covertOps]() { return covertOps; });
    }

    // The room's OWN controls before the HUD's submenu, because that is the order the game
    // itself reads them in: UIFacility.OnUnrealCommand offers the keys to the room screen first
    // and only lets them fall through to UIAvengerShortcuts when the room handles nothing. A
    // room with a bottom bar therefore consumes the arrows here, and a player arriving in the
    // Bar should land on the button the Bar actually has.
    AddRoomFunctions(b, view);

    if (view.actions.empty()) {
        if (view.funcs.empty() && !view.upgrade && view.staffSlots.empty()) {
            // Now genuinely nothing, and still not a fault: this is also the state while the
            // submenu is being rebuilt. ⛔ It used to be reached by the Bar and the Living
            // Quarters as well, whose shortcut category really is -1 — but those rooms DO have a
            // bottom-bar button, so "nothing to do here" was a lie about a screen with a visible
            // control on it (reported live 2026-08-21). The empty case has to mean empty — and
            // the Power Relay, which has no shortcut category, no bar and (until upgraded) no
            // upgrade button, has two staff slots, so they count too.
            b.BeginStop("actions");
            b.AddLabel(ControlId::Structural("room/empty"), []() {
                return std::string(Vocab::RoomNoActions) + ". " + Vocab::RoomLeaveHint;
            });
        }
        // ⚠ The empty case gets the strip too, and it is the case that needs it most: the Labs,
        // the Power Generator and Resistance Comms are exactly the rooms whose strip the game
        // narrows to their ONE relevant resource, and a room with no actions would otherwise
        // leave the player nothing to move between.
        AddResourceStop(b);
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
        vt.OnTooltip = ListItemTooltipAction(action.handle);

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
    AddResourceStop(b);
    return b.Build();
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
    // The picker opening is a shape change the differ announces through the screen name, the
    // same way the debris room does it.
    if (view.dropDown)
        AppendPhrase(name, Vocab::RoomPickStaff);
    return name;
}

// P11 write-back for the submenu — or, while the picker is open, for the picker's list, which
// is the only list a node can be on in that shape. Driving the game's own UIList.SetSelectedIndex
// is what keeps its highlight, its scroll position and its OnSelectionChanged delegates in step
// with our focus — verified live 2026-08-21 to do all three.
bool SetRoomListIndex(void* screen, int index)
{
    GameUI::RoomView view = GameUI::ReadRoom(screen);
    if (view.dropDown)
        return GameUI::SetStaffPickerSelection(view.dropList, index);
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

    // --- shape 2: the picker owns the keys --- (see BuildStaffPicker)
    if (view.dropDown) {
        BuildStaffPicker(b, records, "roombuild", LiveRoomBuildDropList);
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
            if (index < 0)
                continue;
            ControlId id = ControlId::Structural("roombuild/slot/" + std::to_string(index));
            const size_t slotPos = static_cast<size_t>(index);
            b.AddItem(std::move(id), StaffSlotVtable(rec, [slotPos]() -> void* {
                GameUI::RoomBuildView live = GameUI::ReadRoomBuild(gScreen);
                return slotPos < live.slots.size() ? live.slots[slotPos] : nullptr;
            }));
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
    return GameUI::SetStaffPickerSelection(view.dropList, index);
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
    // Switching a Build Items tab is USUALLY covered by the row pointers alone: SwitchTab calls
    // PopulateData, which runs List.ClearItems() and spawns a fresh panel per item. "Usually" is
    // the whole reason the tab verb calls Invalidate() as well — a respawn can land on the freed
    // addresses. The screen pointer is in the key too, so a second screen of the family can
    // never inherit a warm cache from the one it replaced.
    return FetchWordsKeyedOnRows(gCommodityWords, view.screen, view.rows, view.selected);
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
        // The PCS picker's stat change ("AIM +5"), which the game draws in the armory hub's header
        // one screen down — with mislabelled stats (X2AccessCommodityReader.ImplantDelta). Absent
        // on every other screen of the family, so the line simply never appears there.
        {nullptr, "delta"},
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
                // The same red on the PCS picker means CanEquipImplant said no, not the price.
                vt.Announcements.emplace_back(NodeAnnouncement::Static(
                    view.implants ? Vocab::ImplantCannotEquip : Vocab::CommodityUnaffordable));
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

    AddResourceStop(b);
    return b.Build();
}

std::string CommodityName()
{
    // The header the screen itself is drawing. UIX2PanelHeader retains both strings under a
    // dirty check right before the Flash op, so this is what is on screen — including the
    // EMPTY-LIST subtitle, which PopulateData swaps in when the list came back with nothing.
    return NameFromHeader(FetchCommodityWords(GameUI::ReadCommodity(gScreen)),
        Vocab::ScreenCommodityFallback);
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
    return FetchWordsKeyedOnRows(gPersonnelWords, view.screen, view.rows);
}

// One outbound call with a single int, for the two verbs the companion owns. Both resolve the
// screen themselves from the stack, exactly as DescribePersonnel does, so there is nothing to
// pass but the index.
bool CallPersonnelVerb(const char* funcName, int value)
{
    // Both verbs respawn the rows, so the key USUALLY changes on its own — but the fresh
    // pointers can land on the freed addresses of the old ones, and a key that happens to match
    // would keep the previous tab's words on screen. The helper's invalidation is the guard.
    return CallCompanionVerb(gPersonnelWords, kPersonnelReaderClass, funcName, value);
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

        // Name then nickname — the two fields the row hands Flash, joined here rather than in
        // the companion so a soldier with no nickname (which arrives as a lone space) loses the
        // separator with it. The RANK is a column, never part of this: see
        // X2AccessPersonnelReader.SoldierCells.
        std::string name = rec ? JoinParts(*rec, {"name", "nick"}) : std::string();
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

        // The one hover fact on this screen, and it is on the ROW rather than a child:
        // UIPersonnel_SoldierListItem derives from UIButton and calls SetTooltipText on itself
        // to name the bondmate ("%SOLDIERNAME" expanded) or announce that a bond is available.
        // GraphSheet passes it down to every cell, so it answers from any column of the row.
        primary.OnTooltip = PanelTooltipAction(row.handle);
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
    return FetchWordsKeyedOnRows(gBuildFacilityWords, view.screen, view.choices, view.selected);
}

// The two choice screens — what to build, and what to upgrade it with — are one shape: a list
// of names native reads, a panel the companion re-derives, Enter to the SCREEN after the
// selection lands. These two callbacks are the only things that differ per screen: how the
// live view is read, and which cache holds its words.
using ReadChoiceFn = GameUI::ChooseFacilityView (*)(void*);
using LiveWordsFn = const std::vector<WireRecord>& (*)();

const std::vector<WireRecord>& FetchBuildFacilityWordsLive()
{
    return FetchBuildFacilityWords(GameUI::ReadChooseFacility(gScreen));
}

void BuildChoiceCard(GraphBuilder& b, const WireRecord& card, const std::string& keyPrefix,
    LiveWordsFn fetchLive)
{
    struct Line {
        const char* label;
        const char* value;
    };
    // The panel's own order: what it is, what it costs, what it draws, what else it needs, how
    // long, what it costs every month, why you cannot have it, whether you already have it
    // (upgrades only), and finally what it does. A line the record does not carry is a line the
    // panel is not drawing, so the two screens share one table.
    const Line lines[] = {
        {nullptr, "title"},
        {nullptr, "cost"},
        {nullptr, "power"},
        {nullptr, "reqs"},
        {"timeLabel", "time"},
        {nullptr, "upkeep"},
        {nullptr, "warning"},
        {nullptr, "status"},
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
        b.AddLabel(ControlId::Structural(keyPrefix + "card/" + line.value),
            [labelKey = std::string(line.label ? line.label : ""),
                valueKey = std::string(line.value), fallback, fetchLive]() {
                const WireRecord* live = FindRecord(fetchLive(), "card");
                if (!live)
                    return fallback;
                std::string text = ComposeCardLine(
                    *live, labelKey.empty() ? nullptr : labelKey.c_str(), valueKey.c_str());
                return text.empty() ? fallback : text;
            });
    }
}

// The whole graph of a choice screen: the list (or the empty line), the card, the strip.
std::unique_ptr<GraphRender> BuildChoiceScreen(const GameUI::ChooseFacilityView& view,
    const std::vector<WireRecord>& records, const std::string& keyPrefix, ReadChoiceFn readLive,
    LiveWordsFn fetchLive)
{
    GraphBuilder b;
    b.BeginStop("list");
    b.SetRegion("list");

    if (view.choices.empty()) {
        b.AddLabel(ControlId::Structural(keyPrefix + "empty"), []() {
            return std::string(Vocab::CommodityEmpty) + ". " + Vocab::CommodityLeaveHint;
        });
        AddResourceStop(b);
        return b.Build();
    }

    for (const GameUI::FacilityChoice& choice : view.choices) {
        ControlId id
            = ControlId::Referenced(choice.handle, keyPrefix + std::to_string(choice.index));
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
        vt.OnActivate = [index, readLive]() {
            GameUI::ActivateChooseFacility(readLive(gScreen), index);
        };
        vt.OnTooltip = ListItemTooltipAction(choice.handle);
        b.AddItem(std::move(id), std::move(vt));
    }

    if (const WireRecord* card = FindRecord(records, "card"))
        BuildChoiceCard(b, *card, keyPrefix, fetchLive);

    AddResourceStop(b);
    return b.Build();
}

std::unique_ptr<GraphRender> BuildChooseFacility()
{
    GameUI::ChooseFacilityView view = GameUI::ReadChooseFacility(gScreen);
    return BuildChoiceScreen(view, FetchBuildFacilityWords(view), "buildfacility/",
        GameUI::ReadChooseFacility, FetchBuildFacilityWordsLive);
}

std::string ChooseFacilityName()
{
    return NameFromHeader(FetchBuildFacilityWordsLive(), Vocab::ScreenChooseFacilityFallback);
}

bool SetChooseFacilityListIndex(void* screen, int index)
{
    return GameUI::SetChooseFacilitySelection(GameUI::ReadChooseFacility(screen), index);
}

// ---- choosing a facility upgrade (UIChooseUpgrade) ----
//
// The screen behind the room's UPGRADE button, a padlocked staff slot and the to-do widget's
// upgrade nudge. ⛔ UIChooseUpgrade, not UIFacilityUpgrade: XComHQPresentationLayer
// .UIFacilityUpgrade() spawns the former, and the latter — the class the census had marked as
// the natively readable one — is never instantiated (game_ui_avenger.h has the trace).
//
// UIChooseFacility's twin, and built as one: same list read, same Enter-to-the-screen commit
// (OnAccept re-tests UpgradeAlreadyPerformed / HasEnoughPower / CanBuildUpgrade and refuses with
// its own sound), same panel shape re-derived by the companion through the screen's own
// functions. What differs is the words — no build time (an upgrade lands the moment it is paid
// for), and a COMPLETED line on the card for an upgrade already installed, which is the only
// thing a greyed row on this screen can mean besides "cannot afford".

constexpr const char* kUpgradeReaderClass = "X2AccessUpgradeReader";

// Keyed on the rows AND the selection, like the facility picker's and for the same reason: the
// panel is the selection's shadow.
WireCache gUpgradeWords(kUpgradeReaderClass, "DescribeUpgradeChoice");

const std::vector<WireRecord>& FetchUpgradeWords(const GameUI::ChooseFacilityView& view)
{
    return FetchWordsKeyedOnRows(gUpgradeWords, view.screen, view.choices, view.selected);
}

const std::vector<WireRecord>& FetchUpgradeWordsLive()
{
    return FetchUpgradeWords(GameUI::ReadChooseUpgrade(gScreen));
}

std::unique_ptr<GraphRender> BuildChooseUpgrade()
{
    GameUI::ChooseFacilityView view = GameUI::ReadChooseUpgrade(gScreen);
    return BuildChoiceScreen(view, FetchUpgradeWords(view), "upgrade/",
        GameUI::ReadChooseUpgrade, FetchUpgradeWordsLive);
}

std::string ChooseUpgradeName()
{
    return NameFromHeader(FetchUpgradeWordsLive(), Vocab::ScreenChooseUpgradeFallback);
}

bool SetChooseUpgradeListIndex(void* screen, int index)
{
    return GameUI::SetChooseFacilitySelection(GameUI::ReadChooseUpgrade(screen), index);
}

// ---- the research report (UIResearchReport) — docs/research-report-recon.md ----
//
// The research-complete dossier: letterhead, the project's name and codename, the completion
// date, what it unlocked, and Tygan's write-up. Raised from four places — the Labs on entry, the
// Shadow Chamber, either archive's "View Report" row, and the GEOSCAPE when an instant tech
// finishes with no Labs visit at all.
//
// EVERY WORD COMES FROM THE FLASH CAPTURE, and this is the first recipe with no UnrealScript
// behind it. The screen resolves eight strings into locals, pushes them once, and retains none of
// them — not even the TechRef it was called with, which is what rules out a companion class
// re-deriving them. It spawns no widgets either, so the tree is empty as well.
// GameUI::ReadResearchReport is the whole reading.
//
// A DOCUMENT, NOT A CONTROL SET. One node per line and NO StartRow anywhere: an item added
// outside a row becomes its own single-item row, so the report reads DOWN the way it is laid out,
// and GraphBuilder stamps no "N of M" — it only does that within a row holding more than one item,
// and "TOP SECRET, 2 of 11" reads as a menu position on a classification banner. The sentence
// axis a paragraph could carry (a row per paragraph, sentences to the Right) was considered and
// dropped: prose stacked vertically on screen should not be walked sideways.
//
// The cost that buys: one paragraph is one node, so a node runs to ~600 characters and a
// description with no internal break could be one node of 2438. Pressing Down interrupts it
// (Focus clears the queue), which is the escape hatch — a deal made deliberately rather than
// discovered live.
//
// ⛔ ONE KEY CLOSES IT, AND WHICH ONE DEPENDS ON THE STACK. OnUnrealCommand (:199) answers Escape
// only while an exact UIResearchArchives is in the stack and Enter only while it is not, marking
// the other one handled either way — so the wrong key is SWALLOWED and does nothing. The button
// node therefore carries the matching key and the matching word together, or it advertises a way
// out of a modal that does not work.

std::unique_ptr<GraphRender> BuildResearchReport()
{
    const GameUI::ResearchReportView view = GameUI::ReadResearchReport(gScreen);

    // The project name rides in every node id, for the popup recipe's reason: the differ compares
    // node IDENTITY, so two reports read back to back through the same screen slot would look
    // like one unchanged graph and the second would go unspoken.
    const std::string key = "report/" + view.project + "/";

    GraphBuilder b;
    b.BeginStop("report");

    // ⛔ AN EMPTY-LABEL CONTEXT, AND THE LABEL IS EMPTY ON PURPOSE. This exists only to reach
    // SuppressChildPositions, which is the kernel's own answer for "a log-like stream where
    // '37 of 200' is noise" — and a document is exactly that. Without it every line gets stamped:
    // single-item rows do NOT escape StampPositions, they are grouped by (Parent, StopKey,
    // segment) and stamped as a group (GraphBuilder.cpp:414), so the first live run said
    // "СОВЕРШЕННО СЕКРЕТНО, 1 of 14" — a menu position on a classification banner, which is the
    // very thing the popup recipe's comment warns about.
    //
    // The label stays empty because the header is the SCREEN NAME (see ResearchReportName), spoken
    // once on attach; as a context label too it would be said twice in a row. An empty
    // announcement part is silent by contract — AppendPhrase returns early on it — so this
    // contributes no words and no stray separator, only the suppression.
    b.PushContext("", "", /*positions*/ false);

    // Nothing joins gFocus: the screen has no navigable widget at all to push the game's focus at
    // (ContinueButton is declared and never assigned, UIResearchReport.uc:23).
    int added = 0;
    auto say = [&](const char* id, const std::string& text) {
        // An empty field is ordinary here — a repeatable tech has no codename, a datapad has no
        // unlocks — and an unlabelled node is a control that silently ceases to exist. Drop it.
        if (text.empty())
            return ControlId();
        ControlId node = ControlId::Structural(key + id);
        b.AddLabel(node, [text]() { return text; });
        added++;
        return node;
    };

    // ⛔ THE HEADER IS NOT A NODE. It is the screen name (see ResearchReportName), spoken once on
    // attach — as a node too it would be said twice in a row, which is the wording defect class
    // CLAUDE.md already lists against the recentering readout.
    //
    // Order is the PAGE's, not the push's: the greeble is the letterhead's second half and belongs
    // beside the header, not after a 2400-character description where arg 7 sits.
    say("greeble", view.greeble);
    const ControlId project = say("project", view.project);
    say("code", view.code);
    say("date", view.date);
    for (size_t i = 0; i < view.unlockLines.size(); i++)
        say(("unlock/" + std::to_string(i)).c_str(), view.unlockLines[i]);
    for (size_t i = 0; i < view.descriptionLines.size(); i++)
        say(("para/" + std::to_string(i)).c_str(), view.descriptionLines[i]);

    if (added == 0) {
        // Recognised the screen and captured nothing. Say so rather than attaching a graph that is
        // only a close button: silence here is indistinguishable from no screen at all, and the
        // player still has to press something to escape it.
        say("empty", Vocab::ReportNoText);
    } else if (project.IsValid()) {
        // Land on the tech, not on the letterhead: it is the fact the player opened this for, and
        // the greeble above it stays one Up away. Everything below is the document in page order,
        // with the unlocks — the half a player acts on — BEFORE the prose, so nothing forces them
        // through Tygan to reach it. That is why the whole report can be one stop.
        b.SetStart(project);
    }

    b.PopContext(); // the button is not part of the document
    b.BeginStop("continue");
    {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(
            view.fromArchives ? Vocab::ReportBack : Vocab::ReportContinue));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // gScreen is read AT ACTIVATION rather than captured — the handler runs from the input
        // drain, a frame or more after this render. `fromArchives` is captured, and that is safe
        // for a reason worth stating: it is a fact about a screen BELOW this one, which cannot
        // leave the stack while this one sits on top of it.
        vt.OnActivate = [archives = view.fromArchives]() {
            GameUI::SendScreenCommand(
                gScreen, archives ? GameUI::kCmdEscape : GameUI::kCmdEnter);
        };
        b.AddItem(ControlId::Structural(key + "close"), std::move(vt));
    }

    return b.Build();
}

std::string ResearchReportName()
{
    // The screen's OWN localized letterhead ("RESEARCH REPORT"), which the capture hands us along
    // with everything else. Not joined with the project name: that is the landing node, and the
    // two would be spoken back to back.
    const GameUI::ResearchReportView view = GameUI::ReadResearchReport(gScreen);
    return view.header.empty() ? std::string(Vocab::ScreenResearchReport) : view.header;
}

// ---- new research available (UIResearchUnlocked) — docs/research-unlocked-recon.md ----
//
// The post-mission deck: one card per newly unlocked tech, one shown at a time, every accept key
// flipping to the next, the screen closing itself after the last. Words from the Flash capture,
// the shown-card index off the screen — GameUI::ReadResearchUnlocked is the whole reading.
//
// Three deliberate departures from the report recipe it otherwise copies:
//
// ⛔ EVERY NODE ID CARRIES THE CARD ORDINAL, the continue node included. A flip then invalidates
// every identity at once, Reconcile falls through to the start node, and the differ speaks the
// new card's title — that IS the flip announcement, and it costs nothing. A per-screen continue
// id would instead keep focus parked on the button across the flip and card 2 would go unspoken.
//
// EVERY NODE ACTIVATES, not just the button. The game's own OnUnrealCommand answers Enter,
// Escape, A, B and RMB alike with NextResearch (UIResearchUnlocked.uc:140-166) — "press anything
// to continue" is the screen's real input contract, and a player flipping through a deck should
// not walk back down to the button after every card. There is nothing else Enter could mean here.
//
// THE DECK POSITION IS HAND-STAMPED on the title node (current+1 of NumUnlockedTechs, only when
// the deck holds more than one), through the node's PositionIndex/Count so it reads via the
// kernel's own PositionText hook. The automatic stamp cannot say this: it counts nodes within
// the stop — title plus description lines — not cards in the deck, and the document context
// suppresses it anyway.

std::unique_ptr<GraphRender> BuildResearchUnlocked()
{
    const GameUI::ResearchUnlockedView view = GameUI::ReadResearchUnlocked(gScreen);
    const std::string key = "unlocked/" + std::to_string(view.current) + "/";

    // gScreen is read AT ACTIVATION, not captured — the handler runs from the input drain, a
    // frame or more after this render (the report recipe's rule).
    const auto advance = []() { GameUI::SendScreenCommand(gScreen, GameUI::kCmdEnter); };

    GraphBuilder b;
    b.BeginStop("card");
    // Empty-label context, for the report's reason: SuppressChildPositions, so description lines
    // are not stamped "2 of 5" like menu rows. The header is the SCREEN NAME, spoken once on
    // attach — as a context label too it would be said twice in a row.
    b.PushContext("", "", /*positions*/ false);

    int added = 0;
    auto say = [&](const std::string& id, const std::string& text) {
        if (text.empty())
            return ControlId(); // an empty field is ordinary; an unlabelled node is a ghost
        ControlId node = ControlId::Structural(key + id);
        NodeVtable vt;
        vt.Announcements.emplace_back(NodeAnnouncement::Static(text));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = advance;
        b.AddItem(node, std::move(vt));
        added++;
        return node;
    };

    ControlId title;
    if (view.current >= 0 && view.current < static_cast<int>(view.cards.size())) {
        const GameUI::ResearchUnlockedCard& card = view.cards[view.current];
        title = say("title", card.title);
        for (size_t i = 0; i < card.descriptionLines.size(); i++)
            say("para/" + std::to_string(i), card.descriptionLines[i]);
    }

    if (added == 0) {
        // Recognised the screen and captured nothing — say so, for PopupNoText's reason: the
        // player still has to press something to leave, and silence reads as no screen at all.
        say("empty", Vocab::ReportNoText);
    } else if (title.IsValid()) {
        b.SetStart(title); // land on the tech name; the description reads down from it
    }

    b.PopContext(); // the button is not part of the document
    b.BeginStop("continue");
    {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::ReportContinue));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = advance;
        b.AddItem(ControlId::Structural(key + "close"), std::move(vt));
    }

    std::unique_ptr<GraphRender> render = b.Build();
    if (title.IsValid() && view.total > 1) {
        if (GraphNode* node = render->NodeAt(title)) {
            node->PositionIndex = view.current + 1;
            node->PositionCount = view.total;
        }
    }
    return render;
}

std::string ResearchUnlockedName()
{
    // The screen's own localized "RESEARCH AVAILABLE", identical on every card of the deck, so
    // it is spoken once on attach and never again across flips (the differ speaks titles, and
    // the screen name is not re-announced while the recipe stays attached).
    const GameUI::ResearchUnlockedView view = GameUI::ReadResearchUnlocked(gScreen);
    return view.header.empty() ? std::string(Vocab::ScreenResearchUnlocked) : view.header;
}

// ---- the black market (UIBlackMarket entry menu, UIBlackMarket_Sell) ----
//
// The geoscape POI's shop is three screens, and the BUY side is a UISimpleCommodityScreen the
// commodity recipe above already reads. These are the other two — game_ui_avenger.h for what each
// retains, docs/surfaces/avenger.md "Black market" for the rules.
//
// THE ENTRY MENU is mouse-only in vanilla: BuildScreen calls Navigator.Clear(), and the key
// branches in OnUnrealCommand are FXS_BUTTON_A and FXS_BUTTON_X, both controller constants — so a
// keyboard player can reach the shop and never buy or sell. One stop in the screen's own order:
// the welcome line (one of seven at random, retained nowhere, so it comes off the Flash capture
// and is simply absent when the capture has let it go), the month's interests (the caption plus
// the sentence the companion re-expands through the screen's own GetInterestsString), then BUY
// and SELL INVENTORY as buttons — label, greyed state and the game's own reason for it (the
// tooltip RefreshButtons parks on a disabled button) — activated through UIButton.Click so the
// screen's OnBuyClicked / OnSellClicked run their own guards.
//
// THE TRADING POST already walks on the keyboard — Up/Down through the list, Left/Right on a row
// for the quantity, Enter to confirm — and lacks every WORD: a row pushes its name and four
// columns straight into Flash and keeps only NumSelling, Price and the item reference; the card,
// the headings and the running total are Flash-only too. It draws column headings, so it is a
// SHEET: column 0 is the item name carrying the row's cells as heading-prefixed parts (the flat
// reading), and the cells to its right are the same facts under the game's own headings. The
// quantity verb is the kernel's ADJUST on +/-: in a sheet Left/Right are column travel and cannot
// also be the game's quantity keys, so the row's OnAdjust sends the row its own FXS_ARROW_RIGHT /
// FXS_ARROW_LEFT and speaks the new count and line total. Enter anywhere is the SCREEN's Enter
// (OnConfirmButtonClicked), because that is what the screen itself does with the key ahead of its
// navigator — and nothing is marked for sale until the player marks it.

constexpr const char* kBlackMarketReaderClass = "X2AccessBlackMarketReader";

// The entry menu's one companion fact, the interest sentence, keyed on the screen alone: it is
// fixed for the month, and RefreshButtons (which runs on every return to this screen) never
// touches it.
WireCache gBlackMarketWords(kBlackMarketReaderClass, "DescribeBlackMarket");

// The trading post's words — names, owned counts, interest flags, the card. Keyed on the rows and
// the SELECTION (the card is the selection's shadow, exactly as on the commodity list) and
// deliberately NOT on the quantities: +/- changes NumSelling, which native reads off the row, and
// never a word the companion supplied — so a press costs no outbound call. The owned count is
// carried RAW (Item.Quantity) for the same reason: the row draws owned minus selling, and the
// subtraction is done here against the live number.
WireCache gSellWords(kBlackMarketReaderClass, "DescribeBlackMarketSell");

const std::vector<WireRecord>& FetchSellWords(const GameUI::SellView& view)
{
    return FetchWordsKeyedOnRows(gSellWords, view.screen, view.rows, view.selected);
}

// One of the entry menu's two buttons.
void AddMarketButton(GraphBuilder& b, const char* key, const GameUI::BlackMarketButton& button,
    bool sellSide)
{
    if (!button.handle || button.label.empty())
        return;

    // Not in gFocus: Navigator.Clear() left these buttons in no navigator, so there is nothing
    // for the game's focus to follow onto — Click() is the whole activation.
    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(button.label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (button.disabled) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
        vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        if (!button.reason.empty()) {
            // The game's own why — SOLD OUT THIS MONTH, EMPTY INVENTORY — which a sighted player
            // reads off the hover tooltip and which is the only explanation the screen gives.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(button.reason));
            vt.Announcements.back().Kind = AnnouncementKinds::Reason;
        }
    }
    // Offered even when greyed: UIButton.Click refuses a disabled button itself, and the pointer
    // is re-read on the press rather than closed over — RefreshButtons runs on every
    // OnReceiveFocus, so the button can have changed under a graph one poll old.
    vt.OnActivate = [sellSide]() {
        GameUI::BlackMarketView live = GameUI::ReadBlackMarket(gScreen);
        GameUI::ClickButton(sellSide ? live.sell.handle : live.buy.handle);
    };
    vt.OnTooltip = PanelTooltipAction(button.handle);
    b.AddItem(ControlId::Structural(key), std::move(vt));
}

std::unique_ptr<GraphRender> BuildBlackMarket()
{
    GameUI::BlackMarketView view = GameUI::ReadBlackMarket(gScreen);
    const std::vector<WireRecord>& records = gBlackMarketWords.Get({view.screen});

    GraphBuilder b;
    b.BeginStop("market");

    // The welcome line, while the capture still holds it. Build-time text: BuildScreen pushes it
    // once and nothing repaints it for the life of the screen.
    if (!view.quote.empty())
        b.AddLabel(ControlId::Structural("market/quote"), [text = view.quote]() { return text; });

    // "INTERESTS THIS MONTH:" and the sentence, as one node — the caption alone says nothing and
    // the sentence names its own subject. The caption is the screen's own localized var; the
    // sentence is the companion's re-expansion, empty when the market wants nothing this month
    // (GetInterestsString returns "" and the game draws the caption over a blank).
    std::string interests;
    if (const WireRecord* rec = FindRecord(records, "interest"))
        interests = RecField(*rec, "text");
    const std::string interestLine = JoinPhrases(view.interestTitle, interests);
    if (!interestLine.empty()) {
        b.AddLabel(
            ControlId::Structural("market/interests"), [interestLine]() { return interestLine; });
    }

    AddMarketButton(b, "market/buy", view.buy, /*sellSide*/ false);
    AddMarketButton(b, "market/sell", view.sell, /*sellSide*/ true);

    AddResourceStop(b);
    return b.Build();
}

std::string BlackMarketName()
{
    const GameUI::BlackMarketView view = GameUI::ReadBlackMarket(gScreen);
    return view.title.empty() ? std::string(Vocab::ScreenBlackMarketFallback) : view.title;
}

// ---- the trading post ----

SheetConfig SellSheetConfig()
{
    return SheetConfig{Vocab::TableBlank, Vocab::RoleTable, &TextType()};
}

const WireRecord* FindSellRow(const std::vector<WireRecord>& records, int index)
{
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == "row" && atoi(RecField(rec, "index").c_str()) == index)
            return &rec;
    }
    return nullptr;
}

// A heading and its value, the way the flat reading and the total line say them: "QTY 3". Empty
// when the value is, so a column the row is not drawing contributes no part.
std::string Headed(const std::string& heading, const std::string& value)
{
    if (value.empty())
        return std::string();
    if (heading.empty())
        return value;
    return heading + " " + value;
}

// The four cells a row draws, from the numbers native read and the owned count the companion
// carried. `owned` < 0 means the companion had no answer, and the QTY cell stays blank.
struct SellCells {
    std::string qty;     // Item.Quantity - NumSelling, as the row draws it
    std::string cost;    // the credits prefix and the unit price
    std::string selling; // NumSelling — the row draws "-" for none; the number is what it means
    std::string total;   // prefix and price × selling; EMPTY when nothing is marked, as the row's is
};

SellCells ComposeSellCells(const GameUI::SellRow& row, int owned, const std::string& prefix)
{
    SellCells c;
    if (owned >= 0)
        c.qty = std::to_string(owned - row.selling);
    c.cost = prefix + std::to_string(row.price);
    c.selling = std::to_string(row.selling);
    if (row.selling > 0)
        c.total = prefix + std::to_string(row.price * row.selling);
    return c;
}

int SellOwned(const WireRecord* rec)
{
    if (!rec)
        return -1;
    const std::string qty = RecField(*rec, "qty");
    return qty.empty() ? -1 : atoi(qty.c_str());
}

std::string SellPrefix(const std::vector<WireRecord>& records)
{
    const WireRecord* header = FindRecord(records, "header");
    return header ? RecField(*header, "prefix") : std::string();
}

// What a +/- press changed, read LIVE off the row's own numbers: "SELL 2, TOTAL §40". The cells
// re-render from the same numbers, so this is the reading the next arrow press confirms.
std::string SellRowState(int index)
{
    const GameUI::SellView live = GameUI::ReadBlackMarketSell(gScreen);
    const std::vector<WireRecord>& records = FetchSellWords(live);
    for (const GameUI::SellRow& row : live.rows) {
        if (row.index != index)
            continue;
        const SellCells cells
            = ComposeSellCells(row, SellOwned(FindSellRow(records, index)), SellPrefix(records));
        std::string line = Headed(live.sellLabel, cells.selling);
        AppendPhrase(line, Headed(live.totalLabel, cells.total));
        return line;
    }
    return std::string();
}

// The footer's running total, live: "TOTAL §40".
std::string SellTotalLine()
{
    const GameUI::SellView live = GameUI::ReadBlackMarketSell(gScreen);
    const std::string prefix = SellPrefix(FetchSellWords(live));
    return Headed(live.totalLabel, prefix + std::to_string(live.saleAmount));
}

void ConfirmSale()
{
    GameUI::ConfirmBlackMarketSale(GameUI::ReadBlackMarketSell(gScreen));
    // A sale respawns every row (PopulateData → ClearItems), and the fresh panels can land on the
    // freed addresses — the WireCache hazard — so the words are dropped rather than trusted.
    gSellWords.Invalidate();
}

// One metadata cell, built by hand rather than through GraphSheet::Row so it can carry the row's
// adjust verb and its state line — the sheet passes activate, secondary and tooltip down to its
// cells, and those two are this screen's own.
NodeVtable SellCell(const std::string& text, const std::string& rowName, int index)
{
    NodeVtable vt;
    vt.Type = &TextType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(text));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    vt.SearchText = [rowName]() { return rowName; };
    vt.OnAdjust = [index](int sign, bool) {
        GameUI::AdjustBlackMarketSellRow(GameUI::ReadBlackMarketSell(gScreen), index, sign);
    };
    vt.StateText = [index]() { return SellRowState(index); };
    return vt;
}

// The card — PopulateItemCard's lines, one node each, read at ANNOUNCE time with the build-time
// text as the fallback, for the reason the commodity card earned it: the card is repopulated the
// instant our write-back moves the selection, and the graph is up to one poll old.
void BuildSellCard(GraphBuilder& b, const WireRecord& card)
{
    struct Line {
        const char* label;
        const char* value;
    };
    const Line lines[] = {
        {nullptr, "title"},
        {"costLabel", "cost"},
        {nullptr, "interested"},
        {nullptr, "desc"},
    };
    bool any = false;
    for (const Line& line : lines) {
        if (RecField(card, line.value).empty())
            continue;
        if (!any) {
            b.BeginStop("details");
            any = true;
        }
        std::string fallback = ComposeCardLine(card, line.label, line.value);
        b.AddLabel(ControlId::Structural(std::string("sell/card/") + line.value),
            [labelKey = std::string(line.label ? line.label : ""),
                valueKey = std::string(line.value), fallback]() {
                const std::vector<WireRecord>& fresh
                    = FetchSellWords(GameUI::ReadBlackMarketSell(gScreen));
                const WireRecord* live = FindRecord(fresh, "card");
                if (!live)
                    return fallback;
                std::string text = ComposeCardLine(
                    *live, labelKey.empty() ? nullptr : labelKey.c_str(), valueKey.c_str());
                return text.empty() ? fallback : text;
            });
    }
}

std::unique_ptr<GraphRender> BuildBlackMarketSell()
{
    GameUI::SellView view = GameUI::ReadBlackMarketSell(gScreen);
    const std::vector<WireRecord>& records = FetchSellWords(view);
    const std::string prefix = SellPrefix(records);

    GraphBuilder b;
    b.BeginStop("items");

    if (view.rows.empty()) {
        // Transient at most: PopulateData closes the screen itself when nothing is left to sell.
        b.AddLabel(ControlId::Structural("sell/empty"), []() {
            return std::string(Vocab::CommodityEmpty) + ". " + Vocab::CommodityLeaveHint;
        });
        return b.Build();
    }

    // The headings, in the order the row draws its columns: quantity owned, unit value, number
    // selling, line total. ⚠ The pairing of heading to column is read off the .uc's push order
    // and the loc strings, not off the Flash layout — the first live pass checks it against a
    // screenshot (docs/next-steps.md).
    const std::vector<std::string> columns
        = {view.quantityLabel, view.costLabel, view.sellLabel, view.totalLabel};

    GraphSheet sheet(b, "sell/", SellSheetConfig());
    // No region label: the screen is already named by the recipe, and a second title here would
    // repeat it on every entry.
    sheet.Region("", columns);

    ControlId start;
    bool haveStart = false;
    const int rowCount = static_cast<int>(view.rows.size());
    int position = 0;

    for (const GameUI::SellRow& row : view.rows) {
        const WireRecord* rec = FindSellRow(records, row.index);
        std::string name = rec ? RecField(*rec, "name") : std::string();
        if (name.empty())
            name = Vocab::SellItemFallback;
        const bool interested = rec && RecField(*rec, "interested") == "1";
        const SellCells cells = ComposeSellCells(row, SellOwned(rec), prefix);
        const int index = row.index;
        position++;

        NodeVtable primary;
        primary.Type = &TextType();
        primary.Announcements.emplace_back(NodeAnnouncement::Static(name));
        primary.Announcements.back().Kind = AnnouncementKinds::Label;
        // The row's other cells as extra parts, each under its heading — "QTY 3, COST §20" —
        // because unlike a roster's rank or class a bare number here means nothing without one.
        // Value-kind, declaration order, left to right as the row is drawn; the selling pair only
        // once something is marked, which is when the row draws them.
        const std::string parts[] = {Headed(columns[0], cells.qty), Headed(columns[1], cells.cost),
            row.selling > 0 ? Headed(columns[2], cells.selling) : std::string(),
            Headed(columns[3], cells.total)};
        for (const std::string& part : parts) {
            if (part.empty())
                continue;
            primary.Announcements.emplace_back(NodeAnnouncement::Static(part));
            primary.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        if (interested) {
            // The market's premium item. The screen sorts these to the top and the card says
            // VERY INTERESTED for the selected one — and the selection is on this row whenever it
            // is read, because the focus write-back puts it there.
            primary.Announcements.emplace_back(NodeAnnouncement::Static(view.interestedLabel));
            primary.Announcements.back().Kind = kKindAttention;
        }
        // A sheet is raw mode underneath, so the builder stamps no automatic positions; "3 of 9"
        // is added explicitly, Position-kind so the per-kind filter still governs it.
        primary.Announcements.emplace_back(
            NodeAnnouncement::Static(Vocab::Position(position, rowCount)));
        primary.Announcements.back().Kind = AnnouncementKinds::Position;

        primary.OnActivate = ConfirmSale;
        primary.OnAdjust = [index](int sign, bool) {
            GameUI::AdjustBlackMarketSellRow(GameUI::ReadBlackMarketSell(gScreen), index, sign);
        };
        primary.StateText = [index]() { return SellRowState(index); };

        std::vector<std::pair<int, NodeVtable>> cellNodes;
        if (!cells.qty.empty())
            cellNodes.emplace_back(1, SellCell(cells.qty, name, index));
        cellNodes.emplace_back(2, SellCell(cells.cost, name, index));
        cellNodes.emplace_back(3, SellCell(cells.selling, name, index));
        // Sparse when nothing is marked: the row draws nothing there, and an unlandable column
        // keeps its number, so Up/Down still match the TOTAL column across rows.
        if (!cells.total.empty())
            cellNodes.emplace_back(4, SellCell(cells.total, name, index));

        sheet.RowAt(std::move(primary), row.handle, std::move(cellNodes));

        // Every cell inherits the row's list index: the selection is what the card shadows, and a
        // player pressing + from column 3 is pressing it on the row they are in.
        for (int col = 0; col <= 4; col++) {
            std::string key = GraphSheet::RowKey("sell/", row.handle, col);
            gFocus[key] = FocusTarget{index, nullptr, false, nullptr};
            if (col == 0 && !haveStart && (index == view.selected || position == 1)) {
                start = ControlId::Referenced(row.handle, key);
                haveStart = index == view.selected;
            }
        }
    }
    sheet.Finish();

    // The footer: the running total and CONFIRM. Both live — the total moves on every +/- and
    // the button greys itself the moment the total is zero (UpdateTotalValue).
    b.BeginStop("confirm");
    b.AddLabel(ControlId::Structural("sell/total"), []() { return SellTotalLine(); });
    if (view.confirm && !view.confirmLabel.empty()) {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(view.confirmLabel));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement(
            []() {
                return GameUI::ReadBlackMarketSell(gScreen).confirmDisabled
                    ? std::string(Vocab::Disabled)
                    : std::string();
            },
            false, AnnouncementKinds::Enabled));
        // The screen's own Enter rather than Click(): OnConfirmButtonClicked answers a greyed
        // button with the game's refusal sound, where Click() would answer it with nothing.
        vt.OnActivate = ConfirmSale;
        vt.StateText = SellTotalLine;
        b.AddItem(ControlId::Structural("sell/confirm"), std::move(vt));
    }

    if (const WireRecord* card = FindRecord(records, "card"))
        BuildSellCard(b, *card);

    AddResourceStop(b);

    // Land where the game does: BuildScreen ends with List.SetSelectedIndex(0, true) under
    // Navigator.SetSelected(List), so the list — not the footer — is where a sighted player is.
    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string BlackMarketSellName()
{
    return NameFromHeader(FetchSellWords(GameUI::ReadBlackMarketSell(gScreen)),
        Vocab::ScreenBlackMarketSellFallback);
}

bool SetBlackMarketSellListIndex(void* screen, int index)
{
    return GameUI::SetBlackMarketSellSelection(GameUI::ReadBlackMarketSell(screen), index);
}

// ---- recruiting (UIRecruitSoldiers) ----
//
// The Armory's RECRUIT door. game_ui_avenger.h for what the screen retains (nothing on a row; the
// recruit array beside the list), docs/surfaces/avenger.md "Recruits" for the rules. Vanilla walks
// the list and recruits on Enter already — what it lacks is every WORD: a row's name is a Flash
// push it keeps nothing of, and the cost, the time and the bio on the right are repainted straight
// into Flash on every selection change.
//
// TWO STOPS in the screen's own order: the list (name and country per row, greyed when supplies
// are short — the game still lets Enter through there and answers with its own NOT ENOUGH SUPPLIES
// dialog, which the dialog recipe reads), then the panel as one node per line. Enter recruits
// through the SCREEN's own Enter, which acts on the selection the write-back already moved; the
// purchase is immediate, with no confirmation, and rebuilds every row.

constexpr const char* kRecruitReaderClass = "X2AccessRecruitReader";

// Keyed on the rows and the SELECTION: the panel is the selection's shadow (OnRecruitChanged
// repaints it), exactly as the commodity card is. A purchase respawns every row (UpdateData:
// ClearItems + a fresh panel each), so the verb invalidates as well — the address-reuse hazard.
WireCache gRecruitWords(kRecruitReaderClass, "DescribeRecruits");

const std::vector<WireRecord>& FetchRecruitWords(const GameUI::RecruitView& view)
{
    return FetchWordsKeyedOnRows(gRecruitWords, view.screen, view.rows, view.selected);
}

const WireRecord* FindRecruitRow(const std::vector<WireRecord>& records, int index)
{
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == "row" && atoi(RecField(rec, "index").c_str()) == index)
            return &rec;
    }
    return nullptr;
}

void RecruitRowAt(int index)
{
    GameUI::RecruitSelected(GameUI::ReadRecruits(gScreen), index);
    gRecruitWords.Invalidate();
}

// The right-hand panel — cost, time, the bio — one node per line, read at ANNOUNCE time with the
// build-time text as the fallback, for the reason the commodity card earned it: the panel is
// repainted the instant our write-back moves the selection, and the graph is up to one poll old.
void BuildRecruitCard(GraphBuilder& b, const WireRecord& card)
{
    struct Line {
        const char* label;
        const char* value;
    };
    const Line lines[] = {
        {"costLabel", "cost"},
        {"timeLabel", "time"},
        {nullptr, "desc"},
    };
    bool any = false;
    for (const Line& line : lines) {
        if (RecField(card, line.value).empty())
            continue;
        if (!any) {
            b.BeginStop("details");
            any = true;
        }
        std::string fallback = ComposeCardLine(card, line.label, line.value);
        b.AddLabel(ControlId::Structural(std::string("recruits/card/") + line.value),
            [labelKey = std::string(line.label ? line.label : ""),
                valueKey = std::string(line.value), fallback]() {
                const std::vector<WireRecord>& fresh
                    = FetchRecruitWords(GameUI::ReadRecruits(gScreen));
                const WireRecord* live = FindRecord(fresh, "card");
                if (!live)
                    return fallback;
                std::string text = ComposeCardLine(
                    *live, labelKey.empty() ? nullptr : labelKey.c_str(), valueKey.c_str());
                return text.empty() ? fallback : text;
            });
    }
}

std::unique_ptr<GraphRender> BuildRecruits()
{
    GameUI::RecruitView view = GameUI::ReadRecruits(gScreen);
    const std::vector<WireRecord>& records = FetchRecruitWords(view);

    GraphBuilder b;
    b.BeginStop("recruits");

    if (view.rows.empty()) {
        // The game's own line — the one UpdateData pushes through setEmpty — then the way out.
        const WireRecord* empty = FindRecord(records, "empty");
        std::string text = empty ? RecField(*empty, "text") : std::string();
        if (text.empty())
            text = view.emptyText;
        if (text.empty())
            text = Vocab::RecruitsEmpty;
        text += ". ";
        text += Vocab::CommodityLeaveHint;
        b.AddLabel(ControlId::Structural("recruits/empty"), [text]() { return text; });
        AddResourceStop(b);
        return b.Build();
    }

    ControlId start;
    bool haveStart = false;
    for (const GameUI::RecruitRow& row : view.rows) {
        const WireRecord* rec = FindRecruitRow(records, row.index);
        std::string name = rec ? RecField(*rec, "name") : std::string();
        if (name.empty())
            name = Vocab::SoldierFallback;
        const int index = row.index;

        ControlId id = ControlId::Referenced(row.handle, "recruits/row/" + std::to_string(index));
        gFocus[id.StructuralKey] = FocusTarget{index, nullptr, false, nullptr};

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(name));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The flag beside the name is the country; its name is what the flag means.
        const std::string country = rec ? RecField(*rec, "country") : std::string();
        if (!country.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(country));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        if (row.disabled) {
            // Greyed at build time because supplies were short of the cost — the commodity
            // list's bIsBad, worded the same. The game still takes Enter here and explains
            // itself with its own dialog, so the activation stays.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            vt.Announcements.emplace_back(
                NodeAnnouncement::Static(Vocab::CommodityUnaffordable));
            vt.Announcements.back().Kind = AnnouncementKinds::Reason;
        }
        vt.OnActivate = [index]() { RecruitRowAt(index); };
        vt.OnTooltip = ListItemTooltipAction(row.handle);

        // Land where the game does: UpdateData ends with SetSelectedIndex(0), and a return from
        // its dialog rebuilds and lands there again.
        if (!haveStart && (index == view.selected || !start.IsValid())) {
            start = id;
            haveStart = index == view.selected;
        }
        b.AddItem(std::move(id), std::move(vt));
    }

    if (const WireRecord* card = FindRecord(records, "card"))
        BuildRecruitCard(b, *card);

    AddResourceStop(b);
    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string RecruitsName()
{
    return NameFromHeader(
        FetchRecruitWords(GameUI::ReadRecruits(gScreen)), Vocab::ScreenRecruitsFallback);
}

bool SetRecruitListIndex(void* screen, int index)
{
    return GameUI::SetRecruitSelection(GameUI::ReadRecruits(screen), index);
}

// ---- the post-mission rewards recap (UIRewardsRecap) ----
//
// The tail of the post-mission chain, and the hole docs/research-unlocked-recon.md §5 named: the
// council's after-action letter. game_ui_avenger.h for why native supplies only the finder — the
// screen has no widget tree at all, and X2AccessRecapReader re-runs the accessors BuildScreen used.
//
// ONE DOCUMENT STOP, then Continue. The letter is read in the screen's own order — the outcome
// (borrowed from the mission summary's own words, because the recap states it in COLOUR alone),
// the operation and its region, the paragraph, the rewards under their caption, the global effects
// under theirs — with the rewards and the effects one node each rather than the run-on strings the
// screen paints, because speech is serial. Every key but Start is Continue on this screen, Escape
// included, so the button is the whole action list.

constexpr const char* kRecapReaderClass = "X2AccessRecapReader";

// Keyed on the screen alone: BuildScreen runs once, in InitScreen, and nothing repaints after it.
WireCache gRecapWords(kRecapReaderClass, "DescribeRewardsRecap");

const std::vector<WireRecord>& FetchRecapWords()
{
    return gRecapWords.Get({gScreen});
}

std::unique_ptr<GraphRender> BuildRewardsRecap()
{
    const std::vector<WireRecord>& records = FetchRecapWords();
    const WireRecord* header = FindRecord(records, "header");

    GraphBuilder b;
    b.BeginStop("report");
    // A document: no positions, as on the research report and the mission summary.
    b.PushContext("", "", /*positions*/ false);

    int added = 0;
    ControlId start;
    auto line = [&](const std::string& id, const std::string& text) {
        if (text.empty())
            return;
        ControlId node = ControlId::Structural("recap/" + id);
        if (!start.IsValid())
            start = node;
        b.AddLabel(node, [text]() { return text; });
        added++;
    };

    if (header) {
        line("result", RecField(*header, "result"));
        line("mission", JoinParts(*header, {"mission", "location"}));
    }
    if (const WireRecord* summary = FindRecord(records, "summary"))
        line("summary", RecField(*summary, "text"));

    // The rewards, under the caption the screen draws only when it drew a reward.
    bool context = false;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "reward")
            continue;
        if (!context) {
            b.PushContext(header ? RecField(*header, "rewardlabel") : std::string(), "",
                /*positions*/ true);
            context = true;
        }
        line("reward/" + RecField(rec, "index"), RecField(rec, "text"));
    }
    if (context)
        b.PopContext();

    // The global effects, under theirs — good first, then bad, the screen's own order.
    context = false;
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") != "effect")
            continue;
        if (!context) {
            b.PushContext(header ? RecField(*header, "effectslabel") : std::string(), "",
                /*positions*/ true);
            context = true;
        }
        line("effect/" + RecField(rec, "index"), RecField(rec, "text"));
    }
    if (context)
        b.PopContext();

    // Recognised the screen, the companion answered nothing — said for ReportNoText's reason.
    if (added == 0)
        line("empty", Vocab::RecapNoText);
    b.PopContext();

    // The nav help's Continue, whose label lives in the movie; the screen's own Enter is the path.
    b.BeginStop("actions");
    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::ReportContinue));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    vt.OnActivate = []() { GameUI::SendScreenCommand(gScreen, GameUI::kCmdEnter); };
    b.AddItem(ControlId::Structural("recap/continue"), std::move(vt));

    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string RewardsRecapName()
{
    const WireRecord* header = FindRecord(FetchRecapWords(), "header");
    const std::string name = header ? RecField(*header, "mission") : std::string();
    return name.empty() ? std::string(Vocab::ScreenRewardsRecapFallback) : name;
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
// (choose-research, the facility summary, a dialog) — which is also why the room probe insists the
// facility screen is TOPMOST rather than merely present.
const Recipe kRoomRecipe{"avenger room", ProbeFocused<GameUI::FindRoomScreen>, BuildRoom,
    RoomName, SetRoomListIndex};

const Recipe kRoomBuildRecipe{
    "avenger debris room", ProbeFocused<GameUI::FindRoomBuildScreen>, BuildRoomBuild,
    RoomBuildName, SetRoomBuildListIndex};

// Below anything its own rows push (the build-item confirmation, the black market's dialog) and
// below the pause menu, but above the room screen it is opened from — the Research lab's
// UIFacility is still on the stack underneath and probes ready.
const Recipe kCommodityRecipe{
    "commodity list", ProbeFocused<GameUI::FindCommodityScreen>, BuildCommodity, CommodityName,
    SetCommodityListIndex};

// Above every screen it is opened from — the room, the armory, squad select, the debris room's
// build slots — and below any dialog it raises.
const Recipe kPersonnelRecipe{
    "personnel roster", ProbeFocused<GameUI::FindPersonnelScreen>, BuildPersonnel, PersonnelName,
    SetPersonnelListIndex};

// Above the facility grid and UIBuildFacilities it is opened from, below any dialog it raises.
const Recipe kChooseFacilityRecipe{"choose facility",
    ProbeFocused<GameUI::FindChooseFacilityScreen>, BuildChooseFacility,
    ChooseFacilityName, SetChooseFacilityListIndex};

// Above the room (and the grid, via the to-do widget) it is pushed over, below the dialogs it
// raises — its own "insufficient power" warning and the narrative an upgrade can trigger.
const Recipe kChooseUpgradeRecipe{"facility upgrades",
    ProbeFocused<GameUI::FindChooseUpgradeScreen>, BuildChooseUpgrade, ChooseUpgradeName,
    SetChooseUpgradeListIndex};

// HIGH — above every screen it can be raised over, which is an unusually wide set: the Labs and
// the Shadow Chamber (a UIFacility), the two archives (the commodity list), and the GEOSCAPE, for
// an instant tech that completes with no Labs visit. It is a modal in all four cases, so it wins
// against each of them; only the dialog and popup recipes sit above it. No SetListIndex — the
// screen has no UIList, and no navigable widget of any kind.
const Recipe kResearchReportRecipe{
    "research report", ProbeFocused<GameUI::FindResearchReportScreen>, BuildResearchReport,
    ResearchReportName};

// HIGH, beside the report and for the same reason: a modal over the whole post-mission Avenger
// sequence, with only the dialog and popup recipes above it. It never co-exists with the report
// (one is post-mission, the other facility-raised), so their relative order is moot. No
// SetListIndex — no UIList, no navigable widget of any kind.
const Recipe kResearchUnlockedRecipe{"research unlocked",
    ProbeFocused<GameUI::FindResearchUnlockedScreen>,
    BuildResearchUnlocked, ResearchUnlockedName};

// The shop's entry menu: pushed over the geoscape by the POI, and UNDER both of its own screens —
// the Buy side (the commodity recipe) and the trading post — so it lists after both. No
// SetListIndex: two buttons in no navigator.
const Recipe kBlackMarketRecipe{"black market", ProbeFocused<GameUI::FindBlackMarketScreen>,
    BuildBlackMarket, BlackMarketName};

// The trading post: pushed over the entry menu, and only the game's dialogs stack over it.
const Recipe kBlackMarketSellRecipe{"black market sell",
    ProbeFocused<GameUI::FindBlackMarketSellScreen>, BuildBlackMarketSell, BlackMarketSellName,
    SetBlackMarketSellListIndex};

// The recruit list: pushed over the Armory room it is opened from, and raising only its own
// not-enough-supplies dialog, which the dialog recipe resolves first.
const Recipe kRecruitsRecipe{"recruits", ProbeFocused<GameUI::FindRecruitScreen>, BuildRecruits,
    RecruitsName, SetRecruitListIndex};

// HIGH, beside the research deck it follows: the last modal of the post-mission chain, over the
// whole Avenger sequence, with only the dialog and popup recipes above it. No SetListIndex — no
// UIList, no navigable widget of any kind.
const Recipe kRewardsRecapRecipe{"rewards recap", ProbeFocused<GameUI::FindRewardsRecapScreen>,
    BuildRewardsRecap, RewardsRecapName};

// ---- the Avenger HUD (UIAvengerHUD) — the ant farm's ambient Tab layer ----
//
// docs/surfaces/avenger.md §Avenger HUD layer. The mod's second AMBIENT recipe, and the way the
// narrate-only rule and a Tab layer coexist on one screen: the ant farm keeps its arrows (the
// game's own UINavigator walks the rooms, with the camera swoop, the highlight box and the room
// name that is set only under `if( bIsFocused )` — the three things the rule says our own focus
// model would destroy), and this recipe attaches SILENTLY underneath. Tab or Ctrl+Tab focuses it;
// then the arrows walk the HUD's panels and Escape hands them back. Nothing about room
// navigation changes while it is unfocused, which is the whole argument.
//
// Five stops, mirroring what is ON the persistent HUD and nothing that is not: the resource
// strip, the objectives (and the dark events under their own heading), the row of category
// buttons along the bottom, the to-do strip, and the event queue with its date. Every word is the
// game's own — three of the five panels retain theirs in script and the other two come through
// the capture — and every activation is the function the game's own digit key or mouse would
// have called (game_ui_avenger.h, "the rest of the HUD").
//
// ⛔ The probe is the ant farm's gate, not the HUD's presence. UIAvengerHUD is on the stack for
// the whole strategy layer, but the layer belongs to the GRID: a room, a picker, the geoscape
// all make the grid not-current, and each of those has its own recipe (or is the ambient state
// of another surface). The dialog refusal is the tactical recipe's: a UIDialogueBox takes no
// stack entry, so an attached recipe has to refuse it by hand or the player answers it blind.

namespace {

void* ProbeAvengerHud()
{
    if (GameUI::FindDialogScreen())
        return nullptr;
    if (!GameUI::ReadAvengerGate().Active())
        return nullptr;
    return GameUI::FindAvengerHud();
}

// The objectives panel — two stops when the ship has dark events, one otherwise — through the
// Ctrl+X composers, so the key and the stop cannot disagree about a row.
bool AddAvengerObjectiveStops(GraphBuilder& b)
{
    GameUI::ObjectiveListView obj = GameUI::ReadObjectives();
    if (!obj.found)
        return false;
    std::vector<std::pair<std::string, std::string>> lines, dark; // (id suffix, spoken text)
    int n = 0;
    for (const GameUI::ObjectiveLine& line : obj.lines) {
        if (line.title || !line.Speakable())
            continue;
        (line.darkEvent ? dark : lines)
            .emplace_back(std::to_string(n++) + "/" + line.label,
                ObjectiveReader::DescribeLine(line));
    }
    if (!lines.empty()) {
        b.BeginStop("objectives");
        b.PushContext(!obj.heading.empty() ? obj.heading : std::string(Vocab::StopObjectives));
        for (const auto& [suffix, text] : lines)
            b.AddLabel(ControlId::Structural("ahud/obj/" + suffix), [text = text]() { return text; });
        b.PopContext();
    }
    if (!dark.empty()) {
        b.BeginStop("darkevents");
        b.PushContext(!obj.darkHeading.empty() ? obj.darkHeading
                                               : std::string(Vocab::StopDarkEventsFallback));
        for (const auto& [suffix, text] : dark)
            b.AddLabel(ControlId::Structural("ahud/dark/" + suffix), [text = text]() { return text; });
        b.PopContext();
    }
    return !lines.empty() || !dark.empty();
}

// The row of buttons along the bottom: the hologlobe (digit 1) and the category buttons
// (digits 2-6). Walked with Left/Right, as they are drawn. Each node teaches its digit, because
// that digit is the game's own keyboard path and works from the grid without this layer.
bool AddShortcutStop(GraphBuilder& b)
{
    const GameUI::ShortcutBarView bar = GameUI::ReadShortcutBar();
    if (!bar.showing || (bar.categories.empty() && !bar.geoscapeButton))
        return false;

    b.BeginStop("shortcuts");
    b.PushContext(Vocab::StopShortcuts);
    b.StartRow("shortcuts");

    if (bar.geoscapeButton) {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(
            bar.geoscapeLabel.empty() ? std::string(Vocab::GeoscapeButtonFallback)
                                      : bar.geoscapeLabel));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::AbilityHotkey("1")));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
        vt.OnActivate = []() {
            // Re-read at activation: the handler runs from the input drain, a frame or more
            // after this render. HotlinkToGeoscape refuses by itself when the bridge is
            // tutorial-locked, silently — so the refusal is owed here.
            if (!GameUI::HotlinkToGeoscape(GameUI::ReadShortcutBar()))
                TTS::SpeakInterrupt(Vocab::Disabled);
        };
        // Space: the hover text AddLeftHelp registered for this button — TooltipGeoscape with
        // the player's own binding resolved. Static, so the render's copy is the answer.
        if (!bar.geoscapeTooltip.empty()) {
            const std::string tip = bar.geoscapeTooltip;
            vt.OnTooltip = [tip]() { TTS::SpeakInterrupt(tip); };
        }
        b.AddItem(ControlId::Structural("ahud/sc/geoscape"), std::move(vt));
    }

    for (const GameUI::ShortcutCategory& c : bar.categories) {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(c.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.Announcements.emplace_back(
            NodeAnnouncement::Static(Vocab::AbilityHotkey(std::to_string(c.index + 2))));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
        if (c.alert) {
            // bAlert is FacilityState.NeedsAttention() — the "!" badge on the button.
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Attention));
            vt.Announcements.back().Kind = kKindAttention;
        }
        if (c.disabled) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
            vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
        }
        // Passed through even when disabled: SelectCategory tests Button.IsDisabled itself and
        // moves the highlight without opening the room, which is what the digit does too.
        const int index = c.index;
        vt.OnActivate = [index]() {
            if (!GameUI::SelectShortcutCategory(GameUI::ReadShortcutBar(), index))
                TTS::SpeakInterrupt(Vocab::Disabled);
        };
        // Space: a CALLBACK tooltip (SetTooltipWIthCallback) — the words exist only once the
        // hover delegate has run, so it is read at keypress through ReadCallbackTooltip on a
        // fresh button pointer, never captured cold (cold, it says "Init").
        vt.OnTooltip = [index]() {
            const GameUI::ShortcutBarView live = GameUI::ReadShortcutBar();
            for (const GameUI::ShortcutCategory& lc : live.categories) {
                if (lc.index != index)
                    continue;
                const std::string tip = GameUI::ReadCallbackTooltip(lc.button);
                if (!tip.empty())
                    TTS::SpeakInterrupt(tip);
                return;
            }
        };
        // Label in the identity: the Shadow Chamber's button appears mid-campaign at the same
        // index the layout always reserved for it, and must read as a new node.
        b.AddItem(ControlId::Structural("ahud/sc/" + std::to_string(c.index) + "/" + c.label),
            std::move(vt));
    }
    b.EndRow();
    b.PopContext();
    return true;
}

// The to-do strip: one context per category that has messages (the drawn icons), one button
// per message. The label is the headline a hovering player sees in the list; the description is
// the tooltip, on Space. Enter fires the message's own hotlink.
bool AddToDoStop(GraphBuilder& b)
{
    const GameUI::ToDoView todo = GameUI::ReadToDoWidget();
    if (!todo.showing || todo.categories.empty())
        return false;

    b.BeginStop("todo");
    b.PushContext(Vocab::StopToDo);
    for (const GameUI::ToDoCategory& cat : todo.categories) {
        const bool named = !cat.label.empty();
        if (named)
            b.PushContext(cat.label);
        for (size_t m = 0; m < cat.messages.size(); m++) {
            const GameUI::ToDoMessage& msg = cat.messages[m];
            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(msg.label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            // The urgency is the icon's colour (GetUrgencyColor: High = red, Medium = amber) and
            // the row's tint (SetBad / SetWarning) — the same two words the room recipe uses for
            // the same enum on the shortcut submenu, so one fact reads the same on both surfaces.
            if (msg.urgency >= 2) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::RoomUrgent));
                vt.Announcements.back().Kind = AnnouncementKinds::Reason;
            } else if (msg.urgency == 1) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::RoomWarning));
                vt.Announcements.back().Kind = kKindAttention;
            }
            if (!msg.description.empty()) {
                const std::string desc = msg.description;
                vt.OnTooltip = [desc]() { TTS::SpeakInterrupt(desc); };
            }
            const int category = cat.index;
            const int index = static_cast<int>(m);
            vt.OnActivate = [category, index]() {
                // The hover-then-click the mouse would have made, on a fresh read. The hotlink
                // pushes a screen (a room, the research picker, the personnel list), and the
                // grid going not-current detaches this layer for that screen's own recipe.
                if (!GameUI::ActivateToDoMessage(GameUI::ReadToDoWidget(), category, index))
                    TTS::SpeakInterrupt(Vocab::Disabled);
            };
            // Label in the identity: the strip is recomputed on every grid visit and a message
            // that changed (a different soldier due a promotion) must read as a new node.
            b.AddItem(ControlId::Structural("ahud/todo/" + std::to_string(cat.index) + "/"
                          + std::to_string(m) + "/" + msg.label),
                std::move(vt));
        }
        if (named)
            b.PopContext();
    }
    b.PopContext();
    return true;
}

// The event queue: the date and time first (the only date readout on the ship), then the rows
// the list actually holds, then the expand/collapse button when the game draws one.
bool AddEventStop(GraphBuilder& b)
{
    const GameUI::EventQueueView q = GameUI::ReadEventQueue();
    if (!q.showing)
        return false;
    const bool hasDate = !q.date.empty() || !q.time.empty();
    if (!hasDate && q.entries.empty() && !q.canToggle)
        return false;

    b.BeginStop("events");
    b.PushContext(Vocab::StopEvents);

    if (hasDate) {
        std::string when = q.date;
        AppendPhrase(when, q.time);
        b.AddLabel(ControlId::Structural("ahud/ev/date"), [when]() { return when; });
    }

    for (size_t i = 0; i < q.entries.size(); i++) {
        const GameUI::EventQueueEntry& e = q.entries[i];
        NodeVtable vt;
        vt.Type = &TextType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(e.title));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // "3 days", or the game's "--" said in words. The label is localized and may be a
        // single space on an overdue row, which AppendPhrase treats as nothing to add.
        std::string eta;
        if (e.daysValue == "--") {
            eta = Vocab::EventNoEstimate;
        } else {
            eta = e.daysValue;
            AppendPhrase(eta, e.daysLabel, " ");
        }
        if (!eta.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(eta));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        // Space on the covert-action row: who is on it and what each slot pays — the per-slot
        // hover tooltips, which are the only place the game states either on this HUD. Plain
        // rows register no tooltip and stay silent, as they are for a sighted player.
        if (!e.tooltip.empty()) {
            const std::string tip = e.tooltip;
            vt.OnTooltip = [tip]() { TTS::SpeakInterrupt(tip); };
        }
        // Title in the identity: the queue re-sorts as projects finish, and row i becoming a
        // different project is a new node, not a relabel.
        b.AddItem(ControlId::Structural("ahud/ev/" + std::to_string(i) + "/" + e.title),
            std::move(vt));
    }

    if (q.canToggle) {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(q.toggleLabel));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = []() {
            // ToggleExpanded respawns the rows through ShowEventQueue; the differ picks up the
            // new set on the next tick and the button's own label flips with bIsExpanded.
            if (!GameUI::ToggleEventQueue(GameUI::ReadEventQueue()))
                TTS::SpeakInterrupt(Vocab::Disabled);
        };
        // ⛔ ONE identity for both faces of the button, and the new face is the StateText. The
        // first live run had the label in the id, on the theory that the differ would land on
        // "the new button" after a press — it does not: the old id is simply gone, focus fell
        // onto the neighbouring row, and the player heard a row speak where they had pressed a
        // button (18:43, 2026-09-03 — "nothing happens"). With the id held, the navigator keeps
        // focus here and speaks StateText, which re-reads the queue so the answer is the label
        // the game just drew, not the one this render captured.
        vt.StateText = []() { return GameUI::ReadEventQueue().toggleLabel; };
        b.AddItem(ControlId::Structural("ahud/ev/toggle"), std::move(vt));
    }
    b.PopContext();
    return true;
}

std::unique_ptr<GraphRender> BuildAvengerHud()
{
    GraphBuilder b;
    bool any = false;
    // Information first, verbs last: the strip, the objectives, the to-do strip and the queue
    // are what a player opens this layer to READ, and the shortcut buttons are the one stop
    // whose Enter leaves the screen — which is also why they sit last, where Shift+Tab from the
    // landing reaches them in one press. (The buttons have their own digits from the grid
    // anyway, so this layer is the least-needed way to reach them.)
    any = AddResourceStop(b) || any;
    any = AddAvengerObjectiveStops(b) || any;
    any = AddToDoStop(b) || any;
    any = AddEventStop(b) || any;
    any = AddShortcutStop(b) || any;
    // A HUD with nothing readable (the first frames of the strategy layer, or the tutorial
    // before the strip and the queue exist) is not attachable yet — the navigator retries.
    if (!any)
        return nullptr;
    return b.Build();
}

std::string AvengerHudName()
{
    return Vocab::ScreenAvengerHud;
}

} // namespace

// Probed near the end of kRecipes, beside the tactical HUD recipe: below every screen that can
// stack over the grid, and never in contention with tactical (no stack carries both). Ambient —
// attaches silently while the grid keeps the arrows; no yield test, because nothing the game
// raises over the grid leaves it current (each such screen detaches this recipe through the
// probe instead). No SetListIndex: the HUD's panels hold no list this layer drives.
const Recipe kAvengerHudRecipe{"Avenger HUD", ProbeAvengerHud, BuildAvengerHud, AvengerHudName,
    /*SetListIndex*/ nullptr, /*ambient*/ true, /*YieldFocus*/ nullptr,
    /*ambientExitName*/ Vocab::AvengerRoomsMode};

} // namespace Recipes
} // namespace MenuNavigator
