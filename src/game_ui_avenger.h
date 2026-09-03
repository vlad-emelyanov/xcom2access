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

// ---- the resource strip (UIAvengerHUD.ResourceContainer) — docs/avenger-view-recon.md §2.2 ----
//
// The band across the top of every strategy screen: INCOME, SUPPLIES, INTEL, ELERIUM, ALLOYS,
// CORES, SCI, ENG, CONTACTS, POWER. It is ONE widget with ONE owner — a grep over all of SrcOrig
// finds `UIX2ResourceHeader` named in exactly two files — so this reader serves every screen that
// shows it, and there is no second strip anywhere in the game.
//
// ⛔ IT IS NOT A FIXED BAR, and that is the fact a reader has to carry. `UIAvengerHUD
// .UpdateResources()` switches on `SCREENSTACK.GetCurrentScreen().Class` and shows a DIFFERENT
// SUBSET per screen — nine resources on the ant farm, `power` alone in the Power Generator,
// `contacts` alone in Resistance Comms, nothing at all on the twenty-odd classes that fall to its
// `default:`. A sighted player never sees the whole strip at once, so neither do we: reading the
// current subset is parity, and reporting resources the screen is not showing is not.
//
// ✅ The words come from the Flash capture, and this is the case that most needed it.
// `AddResource` is `MC.BeginFunctionOp("addResource")` straight into Flash — the strip retains
// NOTHING, and `bIsNavigable = false`, so the widget tree has neither the words nor a way in. The
// alternative was re-deriving all ten values in the companion AND mirroring that per-screen switch,
// which would drift from the game's source and would be blind to any mod that adds a resource.
// The capture reads what was actually drawn, so both problems are somebody else's.
//
// ⛔ THE STATE IS THE COLOUR, and losing it loses the point of the strip. Every value is wrapped by
// `UIUtilities_Text.GetColoredText`, and the colour is the only thing separating "SUPPLIES 0" from
// "SUPPLIES 0, and that is why you cannot buy this". So the value is parsed for its `<font
// color='#…'>` BEFORE it is cleaned, against UIUtilities_Colors' own constants.
enum class ResourceState {
    Normal,   // NORMAL (cyan) or CASH/GOOD (green) — nothing to say
    Warning,  // WARNING (yellow): contacts with <= 2 free, power in ePowerState_Yellow
    Critical, // BAD (red): a resource at zero, contacts at capacity, power over budget
};

struct ResourceEntry {
    std::string label; // "SUPPLIES" — the game's own localized, Caps()'d word
    std::string value; // "§217", "4/6", "(+§40)" — cleaned, markup stripped
    ResourceState state = ResourceState::Normal;
};

struct ResourceStripView {
    void* hud = nullptr;       // the UIAvengerHUD, or null when not in the strategy layer
    void* container = nullptr; // its ResourceContainer
    /// The strip is on screen. ⛔ This is `bIsVisible` on a CHILD WIDGET, which is the documented
    /// exception to CLAUDE.md's never-gate-on-bIsVisible rule: `ShowResources`/`HideResources`
    /// toggle it deliberately, on every screen change, and it is ALSO how the game withholds the
    /// strip for the whole tutorial (`ShowResources` no-ops until `T0_M4_ReturnToAvenger`) and
    /// while the Avenger is in flight (`UIStrategyMap.UpdateResourceBar`). Reading resources the
    /// player cannot see would be a parity leak in the other direction.
    bool showing = false;
    std::vector<ResourceEntry> entries; // in the game's own push order, left to right
};

/// Read the strip as the game last drew it. Finds `UIAvengerHUD` itself — the strip belongs to the
/// HUD underneath, never to the screen asking. Game thread only.
ResourceStripView ReadResourceStrip();

/// Ask the HUD to repaint the strip, by calling its OWN `UpdateResources()`.
///
/// The capture holds what was PUSHED, so a strip that was last painted before the capture had the
/// panel — or whose entry the capture's LRU has since evicted — reads as empty even though the
/// player can see it. The game paints the strip only on a screen change, so on the ant farm that
/// state can last as long as the player stands there.
///
/// ⛔ This is the game's own function and it is what the game itself calls on every screen change
/// (`UIStrategyScreenListener.RealizeResourceHeader`), so the repaint is the one the game would
/// have done anyway — no state is poked and nothing is fabricated. It is still an outbound
/// ProcessEvent: game thread only, and the CALLER owns the throttle. Answers whether the call was
/// dispatched.
bool RefreshResourceStrip(const ResourceStripView& view);

// ---- the rest of the HUD: shortcuts, to-do strip, event queue (UIAvengerHUD) — recon §2.3-2.5 ----
//
// The three other panels the ant farm's ambient recipe walks. All three RETAIN their words in
// script properties — the opposite of the tactical HUD, and the reason none of these needs the
// Flash capture or a companion call: the shortcut bar keeps its categories and messages in
// `Categories`, the to-do widget keeps every message it computed in ITS `Categories`, and each
// event-queue row keeps Title/DaysLabel/DaysValue on the row widget (recon §2.5, corrected live
// 2026-08-21). Every view here is a plain widget read plus the one activation path the game's own
// mouse or digit key would have taken.

/// The HUD screen itself, or null outside the strategy layer. Stack-managed and persistent —
/// the same object for a whole campaign session, which is why the ambient recipe can attach to
/// it and stay attached across every room visit and geoscape trip.
void* FindAvengerHud();

// The row of category buttons along the bottom (`UIAvengerShortcuts`): Research, Engineering,
// Armory, Command Center, Shadow Chamber — the game's own primary navigation. ⛔ On
// mouse+keyboard the game gives these NO keyboard cycling (LBUMPER/RBUMPER are controller-only);
// a keyboard player reaches them only through the `2`–`6` digits, which call `SelectCategory`
// directly. That function is therefore the activation path here too — it IS the digit key.
struct ShortcutCategory {
    int index = -1;         // the game's category index, which is also the digit minus two
    std::string label;      // CategoryLabels[index], localized
    bool alert = false;     // bAlert — the "!" badge, FacilityState.NeedsAttention()
    bool disabled = false;  // Button.IsDisabled — SelectCategory refuses these itself
    /// The UIX2MenuButton, for Space: its tooltip is a CALLBACK tooltip (SetTooltipWIthCallback,
    /// resolving the player's real key binding on hover), so it is read through
    /// ReadCallbackTooltip at keypress time, never cold. Valid for this read only.
    void* button = nullptr;
};

struct ShortcutBarView {
    void* hud = nullptr;
    void* shortcuts = nullptr;
    void* navHelp = nullptr;
    bool showing = false; // the bar's own bIsVisible (RealizeShortcuts shows it on the grid)
    std::vector<ShortcutCategory> categories; // VISIBLE buttons only, in the game's order
    /// The hologlobe button on the nav help strip (digit `1`), which is not a category but sits
    /// beside them and is the only other button on that edge. `geoscapeLabel` is the
    /// localized word the controller branch draws; on mouse+keyboard the button is an icon.
    bool geoscapeButton = false;
    std::string geoscapeLabel;
    /// The hologlobe's hover text — TooltipGeoscape with the player's binding resolved, which
    /// AddLeftHelp registered against the button's Flash path rather than a panel. Found by
    /// locating HotlinkToGeoscape in m_arrButtonClickDelegates: the index IS the button's name.
    std::string geoscapeTooltip;
};

/// Read the bar. Game thread only.
ShortcutBarView ReadShortcutBar();

/// Go to a category the way the `2`–`6` keys do: `UIAvengerShortcuts.SelectCategory(index)`,
/// which swoops the camera and pushes the room screen. The game refuses a disabled category
/// inside that call, so a disabled button is passed through rather than pre-filtered. Game
/// thread only.
bool SelectShortcutCategory(const ShortcutBarView& view, int index);

/// Press the hologlobe button: `UINavigationHelp.HotlinkToGeoscape()`, which is what digit `1`
/// calls. Game thread only.
bool HotlinkToGeoscape(const ShortcutBarView& view);

// The to-do strip (`UIToDoWidget`): the small icon row bottom-left, one icon per category that
// has something to say, coloured by the worst urgency in it. This is the game's own
// pre-computed "what should I do next", and every message carries a working hotlink. ⛔ It is
// MOUSE-ONLY in vanilla: the messages appear on hover (`OnCategoryMouseEvent` → `ShowList`), and
// the only keyboard path is L3 on a controller. Reading it is parity with the hover; the
// activation reproduces the hover-then-click.
//
// ⚠ `UpdateCategories()` runs only while `UIFacilityGrid` is the current screen and visible, so
// the data is stale anywhere else — which matches the icons a sighted player sees, and does not
// bite the ant farm recipe, which lives exactly there.
struct ToDoMessage {
    std::string label;       // "PROMOTION: Sq. Jane Kelly"
    std::string description; // the hover tooltip
    int urgency = 0;         // EUIToDoMsgUrgency: 0 low, 1 medium (amber), 2 high (red)
};

struct ToDoCategory {
    int index = -1;    // UIToDoCategory — also the index into m_arrCategory_Labels
    std::string label; // the category's localized name
    std::vector<ToDoMessage> messages;
};

struct ToDoView {
    void* hud = nullptr;
    void* widget = nullptr;
    bool showing = false;                // the widget's own bIsVisible
    std::vector<ToDoCategory> categories; // categories WITH messages only — the drawn icons
};

/// Read the strip. Game thread only.
ToDoView ReadToDoWidget();

/// Fire a message's hotlink the way the mouse does: set `CurrentCategory` (what
/// `OnCategoryMouseEvent` writes before `ShowList`) and call `OnListItemCallback(List, index)`,
/// which dispatches the message's own `OnItemClicked` delegate with its refs. Game thread only.
bool ActivateToDoMessage(const ToDoView& view, int category, int index);

// The event queue (`UIEventQueue`): the timeline bottom-right — what is being researched, built
// or trained and how long is left, soonest first. Collapsed it shows ONE row (two when a covert
// action is running), and the expand button that reveals the rest is mouse-only in vanilla
// (`ExpandButton.DisableNavigation()`; R3 is the controller path). The date/time header is a
// Flash push (`RefreshDateTime`) and comes from the capture.
struct EventQueueEntry {
    std::string title;     // "RESEARCH: Hybrid Materials" — the row's own Title
    std::string daysLabel; // "days" — localized by GetTimeValueAndLabel; " " on an overdue row
    std::string daysValue; // "3", or "--" when the game cannot say
    bool covert = false;   // a UIEventQueue_CovertActionListItem — the action row
    /// The covert row's hover text: one tooltip per staff slot (`MCPath.slotN`), each the
    /// soldier's name and the slot's reward, joined. Plain rows register none, so this is empty
    /// there and Space is silent — as it is for a sighted player.
    std::string tooltip;
};

struct EventQueueView {
    void* hud = nullptr;
    void* queue = nullptr;
    bool showing = false;   // the queue's own bIsVisible
    bool expanded = false;  // bIsExpanded
    bool canToggle = false; // ExpandButton is visible — i.e. there is more than one event
    std::string toggleLabel; // ExpandButtonLabel or ShrinkButtonLabel, whichever is drawn
    std::string date;        // captured RefreshDateTime operand 0
    std::string time;        // operands 1-3 joined: "9:41 AM"
    std::vector<EventQueueEntry> entries; // the rows the list actually holds, top to bottom
};

/// Read the queue. Game thread only.
EventQueueView ReadEventQueue();

/// Expand or collapse the queue through its own `ToggleExpanded`, which respawns the rows via
/// `UIAvengerHUD.ShowEventQueue`. Game thread only.
bool ToggleEventQueue(const EventQueueView& view);

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
    // The UIFacility_StaffSlot widgets the game SPAWNED, in container order — empty on every
    // fixed room. ⛔ Container order is not template order: UIFacilityStaffContainer.Refresh
    // skips a slot that is locked with no upgrade to buy, and one whose template hides it, so
    // the widget's own StaffSlotRef is the only thing that says which slot a node is. Nothing
    // here indexes the facility's StaffSlots by this position.
    std::vector<void*> staffSlots;
    // The personnel picker (UIPersonnel_DropDown), only while it is OPEN. It is a PANEL on the
    // staff container, never a screen, so its bIsVisible is the only thing that says it owns the
    // keys — the same child-widget exception the debris room reads.
    void* dropDown = nullptr;
    void* dropList = nullptr; // its UIList
    int dropSelected = -1;
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

/// Open a staff slot's personnel picker through the widget's own HandleClick — the same entry
/// the mouse and UIStaffSlot's own Enter use. Shared by the debris room's build slots and the
/// built room's facility slots, because the widget is the same class either way and the
/// handler is where the game branches: a LOCKED slot opens the facility upgrade screen instead
/// of the picker, and a FILLED project slot (the GTS trainee, a Psi Lab operative, a bonding
/// pair) raises the game's own stop-training confirmation rather than a list. Calling anything
/// lower than HandleClick would have to reproduce those branches by hand. Game thread only.
bool OpenStaffSlotPicker(void* slotWidget);

/// P11 write-back inside an open picker (UIList::SetSelectedIndex on its list). Takes the
/// picker's LIST rather than a view because two views carry one: the debris room's and the
/// built room's. Game thread only.
bool SetStaffPickerSelection(void* dropList, int index);

/// Pick a candidate from an open picker: select, then hand the LIST an Enter, which fires
/// OnItemClicked → UIPersonnel_DropDown.OnStaffSelected — and that indexes m_staff by
/// SelectedIndex, so selecting first is the argument, not a courtesy. Game thread only.
bool ChooseStaffCandidate(void* dropList, int index);

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

// ---- choosing a facility upgrade (UIChooseUpgrade) ----
//
// ⛔ THE UPGRADE SCREEN A PLAYER SEES IS UIChooseUpgrade, NOT UIFacilityUpgrade. Every path the
// game has to it — the room's UPGRADE button, a locked staff slot, the to-do widget, the ant
// farm's overlay — goes through XComHQPresentationLayer.UIFacilityUpgrade(), and that function
// spawns a UIChooseUpgrade (XComHQPresentationLayer.uc:1994). UIFacilityUpgrade is shipped, has
// a list of UIFacilityUpgrade_ListItems with retained UIText, and is never instantiated; a
// reader built for it would probe a class that never reaches the stack.
//
// UIChooseUpgrade is UIChooseFacility's TWIN, down to the field layout: a UIList of
// UIListItemStrings at the same offset (0x418, pinned separately), a `SelectedIndex` only
// UpdateSelection writes, `OnAccept` reading that index, and a panel whose every value is a local
// on its way into Flash. So the same view struct and the same reader serve it, and only the
// finder and the pin are its own. The screen's own Package is even "gfxChooseFacility".
//
// Vanilla's keyboard here already works (Up/Down through UIScreen's navigator, Enter/Space →
// OnAccept, Escape → OnCancel); what it lacks is every word on the right-hand panel.

/// The choose-upgrade screen, when it is the CURRENT screen. Game thread only.
void* FindChooseUpgradeScreen();

/// Same layout as UIChooseFacility, same reader; see above. Game thread only.
ChooseFacilityView ReadChooseUpgrade(void* screen);

// ---- the black market (UIBlackMarket entry menu, UIBlackMarket_Sell) ----
//
// The geoscape POI's shop is three screens. The BUY side (UIBlackMarket_Buy) is a
// UISimpleCommodityScreen and rides the commodity reader above untouched; the other two are this
// section. UIBlackMarket is a two-button menu — BUY and SELL INVENTORY — under a random welcome
// line and the month's interests, and it is MOUSE-ONLY in vanilla: BuildScreen calls
// Navigator.Clear(), and its OnUnrealCommand answers FXS_BUTTON_A / FXS_BUTTON_X, both controller
// constants. UIBlackMarket_Sell is a UIList of UIBlackMarket_SellItem rows under column headings,
// with a CONFIRM button and a running total; Up/Down, Left/Right (the quantity) and Enter (sell)
// already work there on a keyboard.
//
// What each retains, and what does not:
//   * The entry menu's buttons keep their Text (InitButton → SetText) and their IsDisabled bit,
//     and RefreshButtons hands SetDisabled the reason — SOLD OUT THIS MONTH, EMPTY INVENTORY —
//     as the tooltip text, so label, state and the why are all native reads. The welcome line
//     is m_strWelcome[Rand(7)] pushed through LibraryPanel's SetMenuQuote and retained NOWHERE:
//     only the Flash capture knows which of the seven was drawn. The interest sentence is
//     GetInterestsString(), a game-state expansion the companion re-runs on the live screen.
//   * A sell row keeps its NUMBERS — NumSelling, Price, ItemRef — and pushes its name and every
//     column through MC.BeginFunctionOp("populateData"); the words come from the companion, the
//     numbers from here. The screen keeps SaleAmount (the running total) and every column
//     heading as a localized var, so the headings are native reads too.

struct BlackMarketButton {
    void* handle = nullptr;
    std::string label;     // UIButton::Text — "BUY" / "SELL INVENTORY"
    bool disabled = false; // IsDisabled
    std::string reason;    // the SetDisabled tooltip, present only while greyed
};

struct BlackMarketView {
    void* screen = nullptr;
    std::string title;         // m_strTitle — the screen's own name for itself
    std::string quote;         // the welcome line, off the Flash capture; empty when not captured
    std::string interestTitle; // m_strInterestTitle ("INTERESTS THIS MONTH:")
    BlackMarketButton buy;     // Button1
    BlackMarketButton sell;    // Button2
};

/// The entry menu, when it is on the stack (exact class). Game thread only.
void* FindBlackMarketScreen();

BlackMarketView ReadBlackMarket(void* screen);

struct SellRow {
    void* handle = nullptr; // UIBlackMarket_SellItem
    int index = 0;          // the game's own list index
    int selling = 0;        // NumSelling
    int price = 0;          // Price per unit, after the market's markup and the engineering cap
    int itemId = 0;         // ItemRef.ObjectID
};

struct SellView {
    void* screen = nullptr;
    void* list = nullptr; // UIBlackMarket_Sell::List
    std::vector<SellRow> rows;
    int selected = -1;            // UIList::SelectedIndex
    int saleAmount = 0;           // SaleAmount — the running total, before the credits prefix
    void* confirm = nullptr;      // ConfirmButton
    std::string confirmLabel;     // its Text ("CONFIRM")
    bool confirmDisabled = false; // IsDisabled — UpdateTotalValue greys it while the total is zero
    // The screen's own captions, localized vars on the instance: the list heading, the four column
    // headings and the card's interest line.
    std::string inventoryLabel;
    std::string quantityLabel;
    std::string costLabel;
    std::string sellLabel;
    std::string totalLabel;
    std::string interestedLabel;
};

/// The trading post, when it is on the stack (exact class). Game thread only.
void* FindBlackMarketSellScreen();

SellView ReadBlackMarketSell(void* screen);

/// P11 write-back: UIList::SetSelectedIndex. Load-bearing as on the commodity list — the list's
/// OnSelectionChanged is what repopulates the item card. Game thread only.
bool SetBlackMarketSellSelection(const SellView& view, int index);

/// Change a row's selling count by one, through the row's OWN arrow handler
/// (UIBlackMarket_SellItem.OnUnrealCommand with FXS_ARROW_RIGHT / FXS_ARROW_LEFT): the click
/// sound, ChangeQuantity's clamp to what the player owns, the screen's total and the card refresh
/// all come with it. `delta` is the sign only. Game thread only.
bool AdjustBlackMarketSellRow(const SellView& view, int index, int delta);

/// Sell everything marked, through the SCREEN's own Enter — UIBlackMarket_Sell.OnUnrealCommand
/// answers FXS_KEY_ENTER with OnConfirmButtonClicked ahead of its navigator, and a greyed button
/// (nothing marked) gets the game's refusal sound there. Game thread only.
bool ConfirmBlackMarketSale(const SellView& view);

} // namespace GameUI
