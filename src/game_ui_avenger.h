#pragma once

// Game-UI access, the AVENGER family: the ant-farm gate, the rooms (built and un-excavated),
// the choose-facility screen, the commodity and personnel list families, and the research
// screens — what recipe_avenger.cpp and avenger.cpp read. Implemented in game_ui_avenger.cpp.

#include "game_ui_core.h"

namespace GameUI {

// ---- the Avenger's ant farm (UIFacilityGrid) ----

/// The Avenger reader's activation gate.
///
/// Unlike tactical — where UIReplay permanently owns the top of the stack and the gate has to
/// walk a whitelist of passive overlays — the ant farm's rule is simply that it is the CURRENT
/// screen. UIFacilityGrid.OnUnrealCommand's mouse+keyboard branch opens with
/// `if( Movie.Stack.GetCurrentScreen() != self ) return false;`, so anything above the grid has
/// already taken the arrow keys before the grid gets a say. Mirroring the game's own test — and
/// GetCurrentScreen's own definition, the first non-none entry, with no state-bit filter — is
/// what keeps our gate from ever disagreeing with which screen the arrows actually reach.
struct AvengerGate {
    bool gridOnStack = false; // UIFacilityGrid is somewhere on the stack
    bool current = false;     // ...and it is the first entry, i.e. GetCurrentScreen() == it
    bool buildMode = false;   // UIBuildFacilities is above it — which does NOT block
    char blocker[64] = {};    // what sits on top instead, when it is not; for the log

    bool Active() const { return current; }
};

/// Game thread only.
AvengerGate ReadAvengerGate();

// ---- inside a room (UIFacility and its 22 subclasses) ----
//
// ⛔ The room SCREEN is not the room's content. All seven FIXED rooms declare ZERO staff slots —
// CommandersQuarters, CIC, PowerCore (Research), Hangar (Armory), LivingQuarters, BarMemorial
// and Storage (Engineering) — so a reader built around the staff strip would be silent on every
// room a new player can reach. (Measured live 2026-08-21: the Research lab is 3 panels, staff
// container hidden, and all twelve buildable slots on that Avenger were still unexcavated.)
//
// A room's verbs live in THREE places, and this view spans all of them because the game does:
//
//  1. UIRoomContainer — the room's own bottom bar, one UIFacility_RoomFunc per button, added by
//     the subclass's CreateFacilityButtons(). Only six facilities have one, and two of those are
//     FIXED rooms a new player walks into on day one: the Bar's "Obituaries" and the Living
//     Quarters' "Personnel". ⛔ Missing this bar is what made the Bar/Memorial announce "nothing
//     to do here" at a screen that visibly had a button (reported live 2026-08-21) — the
//     shortcut submenu below is genuinely empty there, so the only content was the one place
//     this reader was not looking.
//  2. UpgradeButton — a UILargeButton the facility spawns when it can be upgraded or has been.
//     Navigable on mouse+keyboard (only the CONTROLLER branch calls DisableNavigation), so a
//     sighted keyboard player can already Tab to it, which makes it parity rather than an aid.
//  3. UIAvengerHUD.Shortcuts — the submenu the game auto-opens for the room just entered, where
//     the Lab's "New Research / View Scientists" and the Armory's "Soldiers / Recruit /
//     Memorial" live. It is on a screen UNDERNEATH this one, which is not a layering accident to
//     route around: UIFacility.OnUnrealCommand returns false when the room itself handles
//     nothing, UIScreenStack.OnInput walks LIFO, and the keys land there. The corollary matters
//     for reading ORDER — a room WITH its own buttons consumes the arrows first, so the room's
//     own bar is read before the submenu.
//
// Every field is a RETAINED widget read — no XCOMHISTORY, no Flash, nothing to go stale — with
// one exception the companion covers: a RoomFunc's label (see the struct note).

struct RoomAction {          // one row of the room's shortcut submenu
    void* handle = nullptr;  // UIListItemString
    int index = 0;           // the GAME's list index (padding rows are skipped, not renumbered)
    std::string label;       // Text, P10-cleaned — already localized
    bool disabled = false;   // bDisabled: the game draws it greyed and refuses the click
    bool urgent = false;     // bIsBad
    bool warning = false;    // bIsWarning
};

// One button on the room's OWN bottom bar (UIFacility_RoomFunc in UIRoomContainer::RoomFuncs) —
// "Obituaries" in the Bar, "Personnel" in the Living Quarters, "Start project" in the Proving
// Ground. ⛔ It retains NO TEXT: InitRoomFunc pushes the label straight into Flash with
// MC.FunctionString("update", label) and keeps only the delegate, so the words have to come from
// the companion. Reading the delegate's FName instead would hand a Russian player
// "OnShowObituaries".
struct RoomFunc {
    void* handle = nullptr;
    int index = 0; // position in RoomFuncs, which is also CreateFacilityButtons' call order
};

struct RoomView {
    void* facility = nullptr;  // the UIFacility subclass on top of the stack
    void* shortcuts = nullptr; // UIAvengerShortcuts, on UIAvengerHUD below it
    void* list = nullptr;      // UIAvengerShortcuts::List — the submenu
    std::string location;      // FacilityHeader.LocationField ("AVENGER")
    std::string room;          // FacilityHeader.RoomField ("Research lab") — the room's name
    std::string category;      // CategoryLabels[CurrentCategory]; empty when not in the bar
    std::string staffTitle;    // UIStaffContainer::Title ("SCIENTISTS")
    std::string staffSkill;    // UIStaffContainer::Skill — the ship-wide crew count for the kind
    bool staffVisible = false; // the container's own bIsVisible, which the GAME sets meaningfully
    int staffSlots = 0;        // StaffSlots.Length — zero on every fixed room
    std::vector<RoomFunc> funcs;   // the room's own bottom-bar buttons; empty on most rooms
    std::vector<RoomAction> actions;
    int selected = -1;             // List::SelectedIndex
    void* upgrade = nullptr;       // UIFacility::UpgradeButton, only while the game is showing it
    std::string upgradeLabel;      // UIButton::Text — retained and already localized
    bool upgradeDisabled = false;
};

/// The room screen, when one is the CURRENT screen (topmost non-null stack entry, matching
/// GetCurrentScreen). Matches any UIFacility subclass by walking the instance's class chain —
/// an exact name compare would miss all 22. Game thread only.
void* FindRoomScreen();

RoomView ReadRoom(void* screen);

/// P11 write-back: UIList::SetSelectedIndex on the submenu. Verified live to fire
/// OnSelectionChanged, move Navigator's selection and call the row's OnReceiveFocus — so the
/// game's own highlight follows ours rather than drifting from it. Game thread only.
bool SetRoomActionSelection(const RoomView& view, int index);

/// Activate a row the way the game's own keyboard path does: select it, then hand the LIST an
/// Enter, which fires OnItemClicked(self, SelectedIndex). Selecting first is load-bearing —
/// that delegate reads SelectedIndex and ignores the row the caller had in mind, the same trap
/// UISaveGame.OnAccept sets. Refuses a disabled row, as the game does. Game thread only.
bool ActivateRoomAction(const RoomView& view, const RoomAction& action);

/// Press one of the room's own bottom-bar buttons, through the only handler the widget has:
/// UIFacility_RoomFunc.OnUnrealCommand(FXS_BUTTON_A), which calls its onRoomFunctionDelegate.
/// The mouse path (OnClickRoomFunc) reaches the same delegate; this one needs no fabricated
/// control argument. Game thread only.
bool ActivateRoomFunc(const RoomView& view, int index);

// ---- an un-excavated room (UIRoom): assign an engineer, clear the debris ----
//
// ⛔ NOT UIFacility. A room with no facility gets UIRoom, whose staffing is over the ROOM's
// BuildSlots rather than a facility's StaffSlots — and both halves of the player's task
// dead-end on mouse+keyboard in vanilla:
//   * ExcavateButton calls DisableNavigation(), and UIRoom.OnAccept — what Enter and Space
//     reach — is an EMPTY FUNCTION BODY. StartExcavation is bound only to FXS_BUTTON_X, a
//     controller button. Clicking is the only way to start clearing a room.
//   * The staff slots move on FXS_DPAD_LEFT/RIGHT; the ARROW constants are absent from that
//     switch entirely.
//   * ExcavationPanel calls DisableNavigation() as well, so the status line, the time estimate
//     and the loot are unreachable text even for a sighted keyboard player.
// The recipe over this screen is therefore not narration over something that works — it is what
// makes the screen operable at all.
//
// Native reads STRUCTURE only. Every word comes from X2AccessRoomReader, because this screen
// retains almost none: ExcavationPanel is a pure Flash pipe, UIStaffSlot keeps only its
// StaffSlotRef, and the picker's rows call AS_UpdateData directly — bypassing UIButton.SetText,
// so even the retained UIButton.Text is empty on them.

struct RoomBuildView {
    void* screen = nullptr;
    std::vector<void*> slots;     // UIRoom_StaffSlot widgets, in list order
    void* excavate = nullptr;     // ExcavateButton, only when it is actually showing
    bool excavateDisabled = false;
    void* dropDown = nullptr;     // UIPersonnel_DropDown, only while it is OPEN
    void* dropList = nullptr;     // its UIList
    int dropSelected = -1;
};

/// The UIRoom, when one is the current screen. Game thread only.
void* FindRoomBuildScreen();

RoomBuildView ReadRoomBuild(void* screen);

/// Open a build slot's personnel picker through the widget's own HandleClick — the same entry
/// the mouse and UIStaffSlot's own Enter use, so a LOCKED slot still offers the facility
/// upgrade instead, as the game intends. Game thread only.
bool OpenStaffSlotPicker(void* slotWidget);

/// P11 write-back inside the open picker (UIList::SetSelectedIndex). Game thread only.
bool SetStaffPickerSelection(const RoomBuildView& view, int index);

/// Pick a candidate from the open picker: select, then hand the LIST an Enter, which fires
/// OnItemClicked → UIPersonnel_DropDown.OnStaffSelected — and that indexes m_staff by
/// SelectedIndex, so selecting first is the argument, not a courtesy. Game thread only.
bool ChooseStaffCandidate(const RoomBuildView& view, int index);

/// Press Excavate through the button's own Click, i.e. the mouse path, firing the
/// StartExcavation delegate. ⛔ Not UIRoom.OnAccept (empty), and not StartClearRoom on the room
/// state (StartExcavation also closes the screen; a room left open over a started project is a
/// state the game never produces). Game thread only.
bool StartRoomExcavation(const RoomBuildView& view);

// ---- the research report (UIResearchReport) ----
//
// docs/research-report-recon.md. The whole screen is ONE Flash push: InitResearchReport
// (UIResearchReport.uc:79) resolves eight strings into LOCALS and hands them to
// AS_UpdateResearchReport, retaining not one of them — not even the TechRef it was called with,
// which is what rules out re-deriving the words in the companion. It spawns no widgets either
// (ContinueButton is declared at :23 and never assigned), so the widget tree is empty too.
//
// ✅ So the words come from the Flash command-queue capture, and this is the cleanest case for it
// yet. The batch is matched by NAME — "UpdateResearchReport" is unique to this screen — rather
// than by UIAlert's "most recent batch carrying a string", and the argument layout is fixed by the
// .uc, so the image path at index 4 is skipped by POSITION rather than by looking like an asset.
struct ResearchReportView {
    void* screen = nullptr;
    bool captured = false; // the Update batch was found; false => there is nothing to say

    /// ⛔ Which key closes the screen, and NOT "did the player come from an archive" — see
    /// ReadResearchReport for why the two differ.
    bool fromArchives = false;

    std::string header;  // "RESEARCH REPORT" (localized, arg 0)
    std::string project; // the tech's display name (arg 1)
    std::string code;    // "CODENAME: Orsted" — the game joined it itself with @ (arg 2)
    std::string date;    // completion date, localized field order (arg 3)
    std::string greeble; // "TOP SECRET" (localized, arg 7) — rendered text, so it is read

    /// Args 5 and 6, already split on their newlines and cleaned. ⛔ SPLIT BEFORE CLEANING: both
    /// arrive as one string with \n (and \n\n between the description's paragraphs), and CleanText
    /// collapses every run of whitespace — so cleaning first would fuse a four-paragraph report
    /// into one 2438-character node. Same family as the join-after-cleaning rule.
    std::vector<std::string> unlockLines;
    std::vector<std::string> descriptionLines;
};

/// Read the report's eight strings out of the Flash capture. Game thread only.
ResearchReportView ReadResearchReport(void* screen);

/// The report screen, or null. Stack-managed — four call sites push it, all through
/// XComHQPresentationLayer.
void* FindResearchReportScreen();

// ---- new research available (UIResearchUnlocked) — docs/research-unlocked-recon.md ----
//
// The post-mission deck of research cards: Tygan on the dais, one card per newly unlocked tech,
// shown one at a time with the rest stacked behind, every accept key flipping to the next. Same
// house style as the report — PopulateData resolves each tech into locals and pushes one
// "addResearch" batch per card (header, title, image, description, shadow bool), retaining
// none of it, not even the UnlockedTechs array it was called with.
//
// ✅ So the CARDS come from the Flash capture (matched by name — the screen's MC also pushes an
// addTweenBetween animation, so "calls on this index" is not enough), and the two ints the
// screen DOES retain answer the one question the capture cannot: which card is showing now.
struct ResearchUnlockedCard {
    std::string title; // the tech's display name (arg 1)
    /// Arg 3, split on \n then cleaned — same split-before-cleaning rule as the report's
    /// paragraphs (GetSummary is usually one paragraph, but the rule costs nothing).
    std::vector<std::string> descriptionLines;
    bool shadowProject = false; // arg 4 — captured, deliberately unspoken (recon §5.3)
};

struct ResearchUnlockedView {
    void* screen = nullptr;
    bool captured = false; // at least one addResearch batch was found

    std::string header; // "RESEARCH AVAILABLE" (localized, arg 0 — identical on every card)

    /// UIResearchUnlocked::CurrentTechIndex, CLAMPED into the deck: the game's own counter is a
    /// safety net that sits one past the last card between the final flip and the close.
    int current = 0;
    /// UIResearchUnlocked::NumUnlockedTechs — the deck size a sighted player sees stacked behind
    /// the top card. Normally equals cards.size(); a mismatch is logged.
    int total = 0;

    std::vector<ResearchUnlockedCard> cards; // capture order == tech order
};

/// Read the deck out of the Flash capture and the shown-card index off the screen. Game thread.
ResearchUnlockedView ReadResearchUnlocked(void* screen);

/// The screen, or null. Stack-managed — one call site pushes it, from the loot recap's close.
void* FindResearchUnlockedScreen();

// ---- the commodity list (UIInventory and everything derived from it) ----
//
// ONE view over about a dozen screens, because Firaxis built them as one: choose research,
// choose project, choose a psi ability, view objectives, the three archives, build items,
// storage, implants, the XCOM database, the black market's buy side, resistance goods and the
// officer school are all a UIList of UIInventory_ListItems on the left and a UIItemCard on the
// right. Research is the one that matters most — a tech's cost, its duration and what it
// unlocks are the whole decision — and it is the case with nothing readable at all.
//
// ⛔ BOTH halves of this screen are Flash-only, which is why native reads STRUCTURE and STATE
// here and X2AccessCommodityReader supplies every word:
//   * UIItemCard retains NOTHING. PopulateData and PopulateCostData resolve the title, the
//     description, the cost, the time and the requirements into locals and push them through
//     MC.BeginFunctionOp; the class's only retained vars are a Texture2D and two flags. It is
//     also in no navigator, so no keyboard in the game can focus it either.
//   * UIInventory_ListItem.PopulateData writes the row's name through
//     MC.BeginFunctionOp("populateData"), BYPASSING UIListItemString.SetText — so the retained
//     Text is EMPTY on a row that is visibly showing a name. Same trap the debris room's
//     personnel picker sets.
//
// What IS readable natively is the part native should own anyway: which rows exist, which one
// is selected, and the four colour bits the game tinted each row with. Those bits are the whole
// affordability channel — RealizeBadState sets bIsBad from CanAffordItem and bDisabled from
// MeetsItemReqs/IsItemPurchased — and speech loses a colour for free unless we say it.

struct CommodityRow {
    void* handle = nullptr; // UIInventory_ListItem
    int index = 0;          // the game's own list index
    bool disabled = false;  // bDisabled: requirements unmet, or already bought
    bool bad = false;       // bIsBad: cannot afford it
    bool good = false;      // bIsGood
    bool warning = false;   // bIsWarning: a priority build item
};

struct CommodityView {
    void* screen = nullptr;
    void* list = nullptr; // UIInventory::List
    std::vector<CommodityRow> rows;
    int selected = -1; // UIList::SelectedIndex
    bool tabbed = false; // a UIInventory_BuildItems, i.e. it carries the three-category strip
};

/// The commodity screen, when one is the CURRENT screen (topmost non-null stack entry, matching
/// GetCurrentScreen — the same definition the companion uses, so the two halves cannot read
/// different screens). Matches any UIInventory subclass by walking the instance's class chain.
/// Game thread only.
void* FindCommodityScreen();

CommodityView ReadCommodity(void* screen);

/// P11 write-back: UIList::SetSelectedIndex. Load-bearing beyond keeping the highlight in step
/// — the list's OnSelectionChanged is what REPOPULATES THE CARD, so without it our card stop
/// would describe whatever row the game happened to be on. Game thread only.
bool SetCommoditySelection(const CommodityView& view, int index);

/// Activate a row through the top of the game's own routing: select it, then hand the SCREEN an
/// Enter. ⛔ NOT the row directly — the screen's OnUnrealCommand runs first and two screens use
/// that window (UIInventory_LootRecovered closes on Enter; UISimpleCommodityScreen re-syncs
/// iSelectedItem there). Everything else falls through to the navigator and reaches the selected
/// row anyway, which is why selecting first is the argument. Game thread only.
bool ActivateCommodityRow(const CommodityView& view, const CommodityRow& row);

/// Switch the Build Items category through the screen's own SwitchTab, which is exactly what its
/// Left/Right handler calls. Refuses on any screen without the strip. Game thread only.
bool SwitchCommodityTab(const CommodityView& view, int tab);

// ---- the personnel roster (UIPersonnel and its ten subclasses) ----
//
// The crew list, wherever the game asks "which person?": the Living Quarters' roster, the Bar's
// obituaries, the Armory's soldiers, choosing a scientist for a project, staffing a facility, a
// covert action's squad, the training centre. The subclasses change which array is filled and
// which tabs exist; the layout, the row widgets and the input path are all the base class's, so
// ONE OFFSET covers every door.
//
// ⛔ A row retains only its UnitRef — UIPersonnel_ListItem extends UIButton but UpdateData
// bypasses SetText, so the retained Text is EMPTY on a row that is visibly showing a name. Words
// come from X2AccessPersonnelReader; native reads the list, the selection and the disabled bit.

struct PersonnelRow {
    void* handle = nullptr; // UIPersonnel_ListItem, or its Soldier/Deceased subclass
    int index = 0;          // the list's own item index — what OnButtonCallback reads back
    bool disabled = false;  // UIButton::IsDisabled, which every OnXSelected delegate tests
};

struct PersonnelView {
    void* screen = nullptr;
    void* list = nullptr; // UIPersonnel::m_kList
    std::vector<PersonnelRow> rows;
    int selected = -1; // UIList::SelectedIndex
};

/// The roster screen, when one is the CURRENT screen. Matches any UIPersonnel subclass by
/// walking the instance's class chain. Game thread only.
void* FindPersonnelScreen();

PersonnelView ReadPersonnel(void* screen);

/// P11 write-back: UIList::SetSelectedIndex on the roster. Game thread only.
bool SetPersonnelSelection(const PersonnelView& view, int index);

/// Choose the person on a row, by the game's own keyboard path: set the selection, then hand the
/// SCREEN an Enter. ⛔ Selecting first is load-bearing — UIPersonnel.OnAccept → OnButtonCallback
/// reads m_kList.selectedIndex and ignores anything else. Game thread only.
bool ActivatePersonnelRow(const PersonnelView& view, int index);

// ---- choosing what to build in a room (UIChooseFacility) ----
//
// ⛔ It LOOKS like a commodity screen — a UIList beside a panel carrying cost, power, upkeep,
// build time, requirements and a description — but it `extends UIScreen`, not UIInventory, so the
// thirteen-screen recipe never sees it. Reported live 2026-08-21 as "I selected an empty room and
// the mod went silent": it is the screen build mode's Enter opens, and the only way a player ever
// puts a facility on the Avenger.
//
// Its two halves fail in OPPOSITE directions, which is why this needs both a native read and a
// companion. The ROWS are the good case: PopulateList calls UIListItemString.SetText, so the
// names really are retained, along with bDisabled (CanBuildFacility) and the AttentionIcon the
// game spawns for a bPriority facility. The PANEL is the bad case: UpdateSelection resolves every
// number into LOCALS and pushes them through AS_SetDescription / AS_SetResources, retaining
// nothing — the UIItemCard problem again, on the screen that decides how you spend your supplies.
//
// Vanilla's keyboard here is fine: Up/Down walk the list through the default branch, Enter and
// Space reach OnAccept, Escape cancels. Left/Right are deliberately swallowed and do nothing.

struct FacilityChoice {
    void* handle = nullptr; // UIListItemString
    int index = 0;          // the list's own index, which is also m_arrFacilities'
    std::string label;      // Text — retained, already localized
    bool disabled = false;  // bDisabled: CanBuildFacility said no (cost, power or staffing)
    bool priority = false;  // the game spawned an attention icon (X2FacilityTemplate.bPriority)
};

struct ChooseFacilityView {
    void* screen = nullptr;
    void* list = nullptr; // UIChooseFacility::m_List
    std::vector<FacilityChoice> choices;
    int selected = -1; // UIList::SelectedIndex
};

/// The choose-facility screen, when it is the CURRENT screen. Game thread only.
void* FindChooseFacilityScreen();

ChooseFacilityView ReadChooseFacility(void* screen);

/// P11 write-back. Driving UIList::SetSelectedIndex also fires OnSelectionChanged =
/// UpdateSelection, which is what repopulates the detail panel — so our focus and the panel the
/// companion reads can never describe different facilities. Game thread only.
bool SetChooseFacilitySelection(const ChooseFacilityView& view, int index);

/// Commit the build, by the game's own keyboard path: select, then hand the SCREEN an Enter.
/// ⛔ Selecting first is load-bearing — OnAccept reads the screen's own SelectedIndex, which only
/// UpdateSelection ever writes. A disabled row is passed through: OnAccept re-tests
/// CanBuildFacility and plays its own refusal sound. Game thread only.
bool ActivateChooseFacility(const ChooseFacilityView& view, int index);

} // namespace GameUI
