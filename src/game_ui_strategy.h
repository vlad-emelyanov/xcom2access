#pragma once

// Game-UI access, the STRATEGY family: squad select, the armory loadout, after action,
// promotion, the dropship briefing, the geoscape, covert actions and resistance orders — the
// screens recipe_strategy.cpp (and dropship.cpp) read. Implemented in game_ui_strategy.cpp.

#include "game_ui_core.h"

namespace GameUI {

// ---- armory loadout (UIArmory_Loadout) ----
//
// The screen squad select hands the player to, and the one place armour and a soldier's actual
// inventory can be changed. Structurally it is the ANT FARM's pattern, not the tactical HUD's:
// every UIArmory_LoadoutItem keeps `Title` / `Subtitle` / `SlotType` / `Count` and its five state
// bits in real script properties, each written under a dirty check immediately before the matching
// Flash op — so the retained value IS what Flash was last told, and native can read the rows
// without a companion. What the rows do NOT retain is `disabledText` (declared, never assigned —
// SetDisabled pushes the reason straight into Flash) and the item's stat block, which is why
// X2AccessLoadoutReader exists at all.
//
// Only ONE list is live at a time. ChangeActiveList disables navigation and mouse hits on the
// other, so the recipe exposes the active one as the navigable stop and the equipped list as a
// READ-ONLY mirror while the locker is open — which is what a sighted player still sees, since
// "openList" only slides it aside.

enum class LoadoutList {
    Equipped, // the soldier's slots
    Locker,   // what could go in the slot just chosen
};

/// One row of either list. An EMPTY equipped slot is a row with `slotLabel` and no `title` —
/// exactly what the screen draws (the slot's name over an empty box).
struct LoadoutRow {
    void* handle = nullptr;    // UIArmory_LoadoutItem
    void* dropButton = nullptr;// its clear-this-slot button, or null
    std::string slotLabel;     // SlotType: the localized slot name, equipped rows only
    std::string title;         // the item's friendly name, "" for an empty slot
    std::string subtitle;      // ItemCat — an internal name, spoken only where it is not a dupe
    int count = 0;             // how many are in the inventory (locker rows)
    int equipmentSlot = -1;    // EInventorySlot
    bool infinite = false;     // an item the player can never run out of
    bool disabled = false;     // greyed — announce it with its reason, never skip it
    bool canBeCleared = false; // bCanBeCleared: the row offers "make available"
    bool loadoutSlot = false;  // bLoadoutSlot: an equipped SLOT rather than a locker item
};

struct LoadoutView {
    bool valid = false;
    LoadoutList active = LoadoutList::Equipped;
    std::vector<LoadoutRow> rows;     // the ACTIVE list, in its own order
    int selectedRow = -1;             // the game's own selection in that list
    std::vector<LoadoutRow> equipped; // the mirror, filled only while the locker is active
    std::string inventoryTitle;       // m_strInventoryTitle — the left panel's heading
    std::string lockerTitle;          // m_strLockerTitle — the right panel's heading
    std::string makeAvailable;        // m_strMakeAvailable — the unequip verb's own word
    /// The three nav-help verbs. On mouse+keyboard they are dispatched from OnMouseEvent and from
    /// CONTROLLER trigger codes only, so a keyboard player cannot reach any of them (recon §4).
    bool itemsStripped = false;
    bool gearStripped = false;
    bool weaponsStripped = false;
    std::string tipStripItems, tipStripItemsOff;
    std::string tipStripGear, tipStripGearOff;
    std::string tipStripWeapons, tipStripWeaponsOff;
};

/// The focused UIArmory_Loadout instance, or null. Game thread only.
void* FindArmoryLoadout();

LoadoutView ReadArmoryLoadout(void* screen);

/// Focus write-back into the ACTIVE list. Never call it for the read-only mirror: while the
/// locker is open the equipped list has had DisableNavigation() called on it, and moving a
/// selection the game has switched off is how the two models drift apart.
bool SetLoadoutSelection(void* screen, int index);

/// Activate the selected row through the screen's own OnAccept(), which is the argument-free
/// entry point into OnItemClicked(ActiveList, ActiveList.SelectedIndex) — so the selection has to
/// land FIRST. Same order rule the save/load rows earned.
bool ActivateLoadoutSelection(void* screen);

/// "Make available": the row's own OnDropItemClicked, whose UIButton parameter it never reads.
bool ClearLoadoutRow(const LoadoutRow& row);

// ---- squad select (UISquadSelect) ----
//
// The last gate before every mission, and structurally the tactical HUD's pattern rather than
// the shell's: the widget tree carries the SELECTION MODEL and not one word about the soldiers.
// So these reads answer "which card, which control, is it disabled" and the companion
// (X2AccessSquadReader) answers "who is that and what are they holding" — see
// docs/squad-select-recon.md §3.

/// ESquadSelectButton, the game's own per-card control enum (UISquadSelect_ListItem.uc:41). Its
/// declaration order IS the card's top-to-bottom order, which is why the recipe emits controls
/// in it.
enum class SquadButton {
    Promote = 0,
    Edit = 1,
    Dismiss = 2,
    WeaponPrime = 3,
    WeaponHeavy = 4,
    Utility = 5,
    Bond = 6,
    /// ⛔ NOT an ESquadSelectButton member — the game has no button enum for the empty card,
    /// only the `selectUnitButtonMC` mouse target and `HandleButtonSelect`'s own
    /// `if(ListItemIsEmpty())` special case. So this value must never reach
    /// SetSquadSelectButtonFocus, which takes the game's enum; it exists solely to name the
    /// activation. Without it Enter on an empty card fired Edit, whose body is
    /// `if( XComHQ.Squad[...].ObjectID > 0 )` — false on an empty slot, so it fell through and
    /// did nothing at all, not even a negative sound (live 2026-08-23).
    SelectUnit = 7,
};

/// One sub-control of a soldier card. A utility ROW expands into one entry per slot, because
/// each is separately focusable (the game's own Left/Right walks them) and separately locked.
struct SquadControl {
    void* handle = nullptr;   // the UISquadSelect_UtilityItem for a utility slot; else the card
    SquadButton button = SquadButton::WeaponPrime;
    int utilityIndex = -1;    // >= 0 only for Utility: index within UtilitySlots
    int utilitySlotType = -1; // that slot's EInventorySlot, or -1
    bool disabled = false;    // greyed: announce it, do not skip it (recon §7)
    /// The reason the game itself gives for the grey, from the widget's own tooltip — a locked
    /// utility slot is SetLocked(LockedReason) under the Highlander, which is the string a mouse
    /// player reads by hovering the padlock and the only place it exists.
    std::string tooltip;
};

/// One card. `slotIndex` is the index into XComHQ.Squad and is NOT the visual position — the
/// cards are laid out through SlotListOrder = [4,1,0,2,3,5] for the pre-mission cinematic, so
/// with four soldiers the strip reads left to right as squad 1, 0, 2, 3 (recon §5).
struct SquadSlot {
    void* handle = nullptr; // UISquadSelect_ListItem
    int slotIndex = -1;
    bool locked = false;  // bDisabled — the greyed "increase squad size" card, holds stale data
    bool focused = false; // bIsFocused: the authority, matching the game's GetActiveListItem
    int activeButton = -1;
    /// UIBondIcon — spawned on every card, but only ever CLICKABLE (and only carrying a tooltip)
    /// for a class that can bond, which is a fact only the unit state has. So the recipe emits
    /// the node on the companion's say-so and uses this handle for the tooltip.
    void* bondIcon = nullptr;
    /// The card's own flyover tooltip. CreateFlyoverTooltip sets bHasTooltip ONLY for a soldier
    /// at eMentalState_Tired, so its presence is the game's own "this one is tired" test and its
    /// text is the warning a mouse player hovers for.
    std::string tooltip;
    std::vector<SquadControl> controls;
};

/// One of the screen-wide verbs the game offers only as a NAV-HELP button, i.e. only to a mouse
/// (UINavigationHelp dispatches them from OnMouseEvent alone; the controller shortcuts are not
/// registered on PC). Each is an argument-free handler on the screen, so the recipe can carry it
/// as an ordinary node instead of spending a hotkey per verb — recon §4.
struct SquadVerb {
    std::string label;              // the screen's own localized label
    const char* handler = nullptr;  // its argument-free UnrealScript handler
    bool disabled = false;
};

struct SquadSelectView {
    std::vector<SquadSlot> slots; // VISUAL order (the list's own order)
    void* launch = nullptr;       // LaunchButton — bIsNavigable false, unreachable in vanilla
    bool launchDisabled = true;
    std::string launchTitle;      // m_strLaunch + m_strMission ("НАЧАТЬ" "ЗАДАНИЕ")
    bool walkupDone = false;      // bReceivedWalkupEvent — before it, the screen drops all input
    bool canCancel = false;       // the game's own Escape condition
    bool loadoutDisabled = false; // bDisableLoadout (tutorial gating)
    int soldierSlots = 0;
    std::vector<SquadVerb> verbs;
};

/// The focused UISquadSelect instance, or null. Game thread only.
void* FindSquadSelect();

SquadSelectView ReadSquadSelect(void* screen);

/// Focus write-back: put the game's own focus on a card and one of its controls, exactly as its
/// controller path does (ListItem.OnReceiveFocus + SetSquadSelectButtonFocus). Keeps the Flash
/// highlight and the card's edit/dismiss pop-up in step with ours. Game thread only.
bool SetSquadSelectFocus(void* screen, int visualIndex, const SquadControl& control);

/// Activate one card control through the widget's OWN handler — never through
/// HandleButtonSelect(), which dispatches on the game's m_eActiveButton rather than on ours and
/// would fire the wrong verb after a one-frame disagreement. Game thread only.
bool ActivateSquadControl(void* screen, const SquadSlot& slot, const SquadControl& control);

// ---- after action (UIAfterAction) ----
//
// The post-mission squad walk-up — squad select's sibling, same division of labor: the widget
// tree holds which card, whether it can promote, whether the soldier is dead, and where the two
// buttons are; the companion (X2AccessAfterActionReader) re-derives every word from the
// UnitReference the row retains. See docs/after-action-recon.md §3.

/// A card's inner controls, the pair the game's own Up/Down toggles between. Promote and
/// Memorial are the SAME widget (UpdateData reuses PromoteButton for a KIA card with the
/// delegate swapped); m_bKIA decides which verb it means, and reading the widget's presence
/// alone would call a memorial button "Повышение".
enum class AfterActionControl {
    Promote = 0,
    Memorial = 1,
    Bond = 2,
};

/// One soldier panel. List position IS visual position — UpdateData creates items only for
/// occupied squad slots, in walk-up order, so unlike squad select there are no locked or empty
/// cards and no SlotListOrder arithmetic on our side.
struct AfterActionSlot {
    void* handle = nullptr;      // UIAfterAction_ListItem
    bool focused = false;        // bIsFocused — same authority squad select settled on
    bool canPromote = false;     // m_bCanPromote — the screen's own cached ShowPromoteIcon()
    bool kia = false;            // m_bKIA — PromoteButton means Memorialize
    void* promoteButton = nullptr; // null when the card has neither promote nor memorial
    void* bondIcon = nullptr;
};

struct AfterActionView {
    std::vector<AfterActionSlot> slots; // m_kSlotList order == visual left-to-right
    /// bRecievedShowHUDRemoteEvent — before PostM_ShowSoldierHUD the panels are hidden and the
    /// screen swallows all input; do not attach (recon §5).
    bool hudShown = false;
};

/// The focused UIAfterAction instance, or null. Game thread only.
void* FindAfterAction();

AfterActionView ReadAfterAction(void* screen);

/// Focus write-back, exactly as the game's own path lands it: list selection + the card's
/// OnReceiveFocus, then the promote-vs-bond inner focus placed DETERMINISTICALLY (never through
/// OnUpDownInputHandler, which toggles from whatever state the game is in).
/// `control` may name a control the card lacks; the card focus alone is then the whole write.
bool SetAfterActionFocus(void* screen, int visualIndex, AfterActionControl control);

/// Activate one card control through its own handler: AfterActionPromote() (zero-param),
/// OnClickedMemorialize (its UIButton param is never read), OnClickBondIcon (likewise).
/// Continue is InvokeScreenVerb(screen, "OnContinue"). Game thread only.
bool ActivateAfterActionControl(const AfterActionSlot& slot, AfterActionControl control);

// ---- the Skyranger load screen (UIDropShipBriefing_MissionStart / _MissionEnd) ----
//
// The screen the game shows while a mission map loads: your squad in the dropship interior, with
// the operation name, location, briefing, objectives and a rotating gameplay tip beside them.
// Not a movie and not a UIProgressDialogue — a real UIScreen, pushed onto the stack by
// XComPlayerController.SetupDropshipBriefingScreen(). See docs/dropship-briefing-recon.md.
//
// Everything below is RETAINED. The labels and the current tip are ordinary properties on the
// screen; only the mission's own values went into Flash, and those are the companion's job.
struct DropshipView {
    void* screen = nullptr;    // null when neither variant is on the stack — the usual case
    bool missionStart = false; // false => the mission-END variant, i.e. the flight home

    /// LaunchButton.bIsVisible — the map has finished loading and ANY key now launches.
    ///
    /// This is the screen's own answer, set in the same statement block that writes the prompt
    /// into Flash (OnCheckLoading, on a 1 s timer), which is why it is read rather than
    /// XComPlayerController.bSeamlessTravelDestinationLoaded. It is also squarely inside the
    /// documented exception to "never gate readiness on bIsVisible": this is a child BUTTON,
    /// whose visibility the game toggles deliberately and which therefore does mean hidden.
    bool ready = false;

    std::string loadingText; // "TRAVELING TO DROP POINT" — shown for the whole load
    std::string prompt;      // "PRESS ANY KEY TO LAUNCH" / "...TO CONTINUE"; only true once ready
    std::string tip;         // m_strCurrentTip — MissionStart only (see below)
};

/// The dropship briefing that is on the stack, or a default-constructed view when none is.
///
/// ⛔ `tip` is empty on the mission-END variant and that is not a gap in this read: that class
/// passes GetTip(eTip_Strategy) straight into the Flash op and retains nothing, so there is no
/// tip to read. Only UIDropShipBriefing_MissionStart keeps m_strCurrentTip.
///
/// Game thread only.
DropshipView ReadDropship();

// ---- covert actions (UICovertActions) — docs/covert-actions-recon.md ----
//
// The WOTC core strategy loop, reached only from the Resistance Ring. It splits three ways and
// native owns exactly one of the three:
//
//   * RETAINED — the action ROWS. They are ordinary UIListItemStrings: InitListItem goes through
//     SetText, so Text is really there, along with bDisabled and, uniquely useful,
//     `metadataInt = ActionState.ObjectID` (UICovertActions.uc:368), written unconditionally for
//     every action row. That is a REAL join key, so the companion's records need carry no
//     positional coupling at all.
//   * NOTHING AT ALL — every other string. The detail card, the faction panel, the risks and the
//     four slots are `MC.BeginFunctionOp` pipes with locals on both sides, and the slots are not
//     even panels: `UICovertActionSlot extends Object`, so /gui cannot see them. The whole widget
//     tree here is the list, SlotContainer with four empty slotMC_N panels, and LaunchButton.
//   * RE-DERIVABLE — the screen keeps `arrActions` (the state objects it populated Flash FROM),
//     so X2AccessCovertActionReader rebuilds every word with no XCOMHISTORY lookup. Tier 2 of the
//     read-the-screen rule, verbatim.
//
// ⛔ SELECTING A ROW IS DESTRUCTIVE. UICovertActions.SelectedItemChanged (:883) calls ClearSlots()
// BEFORE switching ActionRef, and ClearSlots submits a game state that runs EmptyAllStaffSlots +
// ResetAllCostSlots on the OUTGOING action. So moving off an action you staffed throws the
// staffing away. That is vanilla — a sighted player clicking around gets it too — and the recipe
// mirrors it rather than decoupling, because the confirm handler acts on the screen's own
// ActionRef and a focus that disagreed with it would be the UISaveGame.OnAccept hazard at
// campaign scale. The recipe announces the loss instead.
//
// ⛔ CONFIRM ACTION IS MOUSE-ONLY IN VANILLA. LaunchButton calls DisableNavigation() (:433) and the
// only key path to OnLaunchClicked is FXS_BUTTON_X (:1087), which DefaultInput.ini produces only
// from XboxTypeS_X. A keyboard-only player cannot launch a covert action at all. Same for EDIT
// loadout (FXS_BUTTON_Y). Re-providing those two is most of what this surface is for.

struct CovertActionRow {
    void* handle = nullptr; // UIListItemString — an ACTION row; faction headings are excluded
    int index = 0;          // the list's own item index, headings INCLUDED (SetSelectedIndex's)
    int actionId = 0;       // metadataInt = XComGameState_CovertAction::ObjectID — the join key
    std::string label;      // Text — retained, already localized ("(NEW) Recruit Scientist")
    bool disabled = false;  // bDisabled: another action is running, or influence is too low
};

struct CovertActionsView {
    void* screen = nullptr;
    void* list = nullptr; // UICovertActions::List
    std::vector<CovertActionRow> rows;
    int selected = -1;           // UIList::SelectedIndex — in the list's own indices
    int actionId = 0;            // ActionRef.ObjectID: the action every panel is describing
    bool actionInProgress = false; // bActionInProgress: one is running, so the rest are locked
    bool selectingSlots = false; // bIsSelectingSlots — the game's own slot mode, which we skip
    /// LaunchButton, and ONLY while the game is showing it: UpdateData hides it outright when an
    /// action is in progress or influence is unmet, so a null here means the control is not on
    /// screen rather than merely greyed.
    void* launch = nullptr;
    bool launchDisabled = true; // IsDisabled — CanBeginAction() said no
};

/// The covert-actions screen, when it is the CURRENT screen. Stack-managed (HQPRES pushes it).
/// Game thread only.
void* FindCovertActionsScreen();

CovertActionsView ReadCovertActions(void* screen);

/// P11 write-back. Driving UIList::SetSelectedIndex fires OnSetSelectedIndex =
/// SelectedItemChanged, which is what repopulates every panel — so our focus and the words the
/// companion reads can never describe different actions. ⛔ Read the destructive-selection note
/// above before calling this on every render. Game thread only.
bool SetCovertActionSelection(const CovertActionsView& view, int index);

/// Launch, through the game's own path: UIButton::Click(), which self-gates on
/// IsDisabled && bIsVisible and fires OnLaunchClicked → ConfirmAction() → the
/// eAlert_ConfirmCovertAction popup the popup recipe already reads. Game thread only.
bool ActivateCovertActionLaunch(const CovertActionsView& view);

// ---- resistance orders (UIStrategyPolicy) ----
//
// The card board: three faction columns plus a wild-card column of SLOTS on top, the HAND of
// unplayed orders along the bottom, and a Continue button. Reached from the monthly Resistance
// Report and from the Resistance Ring's own screen.
//
// ⛔ IT IS ENTIRELY MOUSE-ONLY IN VANILLA, which is why this needs a recipe rather than a reader.
// `UIStrategyPolicy.OnUnrealCommand` gives a keyboard exactly two verbs — Escape and Enter both
// reach `OnClickedContinue()` — and every other branch is either a controller constant
// (`FXS_BUTTON_X` remove, `FXS_BUTTON_Y` filter, the DPAD navigation) or guarded by
// `` `ISCONTROLLERACTIVE ``. Placing a card is DRAG AND DROP: `UIStrategyPolicy_Card.OnMouseEvent`
// is the only path to `SelectCard`. So a keyboard player can read nothing and change nothing —
// they can only leave.
//
// ✅ The card panels DO retain their structure, which is the half that makes this tractable:
// `CardRef` (the join key), `bLocked` and `bVital` are real fields on `UIStrategyPolicy_Card`.
// ⛔ `ColumnIndex`/`ColumnSlot` are real fields too and are NOT trusted: only `RealizeColumn`'s
// slot loop assigns them, so the locked placeholder `CreateLockedCard` appends after that loop
// reads (0, 0) — colliding with the wild-card column's real slot 0. The address is the LIST
// POSITION, which is also how the companion's `ResolveCard` resolves every verb. What the panels
// do NOT retain is a single word — title, summary, quote and slot hint all go through
// `MC.BeginFunctionOp("SetCardData")` as parameters — so X2AccessPolicyReader re-derives them
// from the `XComGameState_StrategyCard` the ref names.
//
// ⛔ `SelectCard` IS A TWO-STEP STATE MACHINE over the screen's `InspectingCard`, and that is
// exactly the pick-up/put-down gesture a graph wants: the first call inspects, the second plays,
// swaps, or returns the card to the hand depending on where both ends are. The recipe drives it
// through the companion so every guard stays in the game's own code.

struct PolicyCard {
    void* handle = nullptr; // UIStrategyPolicy_Card
    /// The address the verbs take, resolved by LIST POSITION on both halves: `column` is the
    /// screen's Columns[] index (0 = the wild-card column) or -1 for a hand card, `slot` is the
    /// card's index in whichever list holds it. Never the card's own `ColumnIndex`/`ColumnSlot`
    /// fields — see the block comment above.
    int column = -1;
    int slot = 0;
    int cardId = 0;          // CardRef.ObjectID — 0 on an empty or locked slot
    bool locked = false;     // bLocked: the slot needs more influence to unlock
    bool vital = false;      // bVital: MarkCardAsSpecial — a card that cannot be removed
    bool inspecting = false; // this is the screen's InspectingCard — the card "picked up"
};

struct PolicyColumn {
    void* list = nullptr;
    int index = 0; // 0 = wild card, 1..3 = the faction columns in the screen's own sorted order
    std::vector<PolicyCard> cards;
};

struct StrategyPolicyView {
    void* screen = nullptr;
    void* hand = nullptr; // UIStrategyPolicy_DeckList (a UIList)
    std::vector<PolicyColumn> columns;
    std::vector<PolicyCard> handCards;
    /// bEnableDragging — the game's own answer to "can orders be changed right now". It is set
    /// only for the monthly report; outside that window the card's own mouse handler refuses and
    /// raises UICantChangeOrders(), so our verbs must refuse in the same place.
    bool canPlay = false;
    bool resistanceReport = false; // bResistanceReport
    int inspectingId = 0;          // InspectingCard's CardRef.ObjectID, 0 = nothing picked up
};

/// The resistance-orders screen, when it is the CURRENT screen. Game thread only.
void* FindStrategyPolicyScreen();

StrategyPolicyView ReadStrategyPolicy(void* screen);

// ---- promotion / level-up (UIArmory_Promotion + its two subclasses) ----
//
// THREE SCREENS, ONE ENTRY POINT. XComHQPresentationLayer.ShowPromotionUI picks the class, and
// the choice is not simply "hero or not":
//
//   UIArmory_PromotionHero   IsResistanceHero() OR the Training Center is in the stack
//   UIArmory_PromotionPsiOp  class is 'PsiOperative'
//   UIArmory_Promotion       everything else
//
// That Training Center clause is why the hero screen matters far beyond three faction classes:
// it is the screen ANY soldier gets whenever abilities are bought with AP, which in WOTC is the
// majority of promotion traffic.
//
// ⛔ THE HERO SCREEN IS A KEYBOARD DEAD END. UIArmory_PromotionHero.OnUnrealCommand and
// UIArmory_PromotionHeroColumn.OnUnrealCommand handle FXS_DPAD_* and FXS_VIRTUAL_LSTICK_* only —
// there is not one FXS_ARROW_* in either file — and activation is GetAdvanceButtonInputCode(),
// i.e. FXS_BUTTON_A, never Enter or Space. InitPromotion then calls DisableNavigation() on the
// screen and every column, so nothing falls through to a navigator either, and the inherited
// UIArmory_Promotion.OnUnrealCommand walks a null List. Mouse works; keyboard does nothing at
// all. So unlike the save/load recipe, this layer cannot lean on the game's own key handling —
// it must supply navigation AND activation. The classic screen is fine by contrast
// (UIArmory_PromotionItem.OnUnrealCommand takes all four arrows plus Enter/Space); its only gap
// is the ability-info popup, which is bound to FXS_BUTTON_L3 alone.
//
// ⛔ RANK HERE IS AN ABILITY-TREE INDEX, NOT A DISPLAYED RANK. Both screens set row.Rank = i and
// then draw GetRankName(i + 1): ClassRowItem.Rank is 0 while showing "Squaddie", and hero
// Columns[i].Rank is i while showing rank i+1. GetRankAbilities() takes the tree index. Every
// `rank` below is the tree index; the companion does the +1 when it names the row.
//
// WHAT NATIVE READS: which rows exist, which slots inside them are real, which row and branch
// hold focus, and the two state bits the game itself gates on. The WORDS — ability names,
// descriptions, AP costs, owned/locked/affordable, rank and branch headings — are all the
// companion's, because they live in templates and unit state rather than on the widgets.
//
// ⛔ A LOCKED RANK'S ABILITY NAMES MUST NOT BE SPOKEN. The rows retain the real FNames at every
// rank, including ranks above the soldier's, but PopulateData deliberately substitutes
// UnknownAbilityIcon and m_strAbilityLockedTitle for anything at `Rank >= Unit.GetRank()`. A
// sighted player cannot read those names, so neither may we — the companion applies the gate,
// which is why the names are resolved there and never here.

enum class PromotionKind { Classic, Hero, PsiOp };

/// One ability slot in a rank row. The ICON is the game's own answer to "does this slot exist":
/// both screens spawn every icon up front and Hide() the ones the tree has no ability for.
struct PromotionCell {
    void* icon = nullptr; // UIIcon
    int branch = 0;       // index into the rank's ability tree (0-1 classic, 0-3 hero)
    bool visible = false; // bIsVisible — a hidden icon is a slot the tree does not fill
};

/// One rank: a UIArmory_PromotionItem row on the classic screen, a UIArmory_PromotionHeroColumn
/// on the hero one. Both retain the same shape, which is what lets one view serve both.
struct PromotionRow {
    void* handle = nullptr;
    int rank = 0;             // ability-tree index; the drawn rank name is rank + 1
    bool disabled = false;    // bIsDisabled — the game greys it and refuses the click
    bool eligible = false;    // bEligibleForPromotion / bEligibleForPurchase
    int selectedBranch = -1;  // SelectedAbility / m_iPanelIndex, the game's own within-row cursor
    std::vector<PromotionCell> cells;
};

struct PromotionView {
    bool valid = false;
    void* screen = nullptr;
    PromotionKind kind = PromotionKind::Classic;
    int unitId = 0;          // UIArmory::UnitReference.ObjectID
    std::vector<PromotionRow> rows; // tree order, rank 0 first
    int selectedRow = -1;    // index INTO rows
    int selectedBranch = -1; // the screen's own SelectedAbilityIndex (classic) / column cursor
};

/// The focused promotion screen of any of the three classes, or null. Game thread only.
void* FindPromotionScreen();

PromotionView ReadPromotion(void* screen);

/// Move the game's own selection to (row, branch) so the screen shows what the player is hearing
/// and its handlers act on the right cell. Classic: the list index plus the row's own
/// SetSelectedAbility. Hero: m_iCurrentlySelectedColumn and the column's m_iPanelIndex, then
/// PreviewAbility — the repaint the game only reaches from its controller branch. Game thread
/// only.
bool SetPromotionSelection(const PromotionView& view, int row, int branch);

/// Buy (or explain) one ability through the row's OWN SelectAbility(int) — the same name on both
/// row classes. It is a three-way verb the game already wrote: owned opens the info popup,
/// purchasable raises the confirm dialog, locked plays the refusal. ⛔ Select first: the classic
/// row reads its own Rank and the hero column its own, so the pair must agree before the call.
/// Game thread only.
bool ActivatePromotionCell(const PromotionView& view, int row, int branch);

// ---- the geoscape (UIStrategyMap) — the mission tray ----
//
// ⛔ The globe has NO cursor for a mouse+keyboard player. XComHeadquartersInput's
// PostProcessCheckGameLogic runs ProcessGeoscapeSelection — and therefore
// UIStrategyMap.UpdateSelection -> SelectMapItemNearestLocation — only inside
// `if( `ISCONTROLLERACTIVE)`, so nothing sets SelectedMapItem by proximity and no pin on the
// map can be reached from the keyboard at all. Unlike the ant farm's InitializeNavigationGrid
// this is NOT a one-call ungate: it is a per-frame tick.
//
// What IS live is the mission TRAY — the strip of icons along the bottom, which is the game's
// own list of everything actionable right now (faction HQs, then scan sites, then missions,
// in UpdateMissions' order). UIStrategyMap.InitScreen sets Navigator.HorizontalNavigation, so
// Left/Right already reach Navigator.Prev/Next on mouse+keyboard — and because UpdateSelection
// never runs to overwrite it, whatever the arrows select STICKS. Measured live 2026-08-21: one
// synthetic Left moved SelectedIndex 0 -> 1 and SelectedMapItem POI -> Mission, unchanged 12 s
// later. So the game hands us a working list cursor that is simply invisible and silent.
//
// Three things it does NOT hand us, all because UIStrategyMap_MissionIcon.OnReceiveFocus opens
// with `if (Movie.IsMouseActive()) return;` — the selection brackets, the camera fly-to, and
// the tooltip refresh. Verified live: /call OnReceiveFocus returned normally and left bFocused
// false. Supplying those is this reader's whole job, and it supplies them through the icon's
// OWN OnMouseEvent rather than by poking state (see MoveTrayHover).
//
// Every word here is a RETAINED widget read. Tooltip.Title/Body are written by
// SetMissionIconTooltip from the state object's GetUIButtonTooltipTitle/Body at bind time —
// already localized, already exactly what a sighted player's hover tooltip says. Measured live:
// "НЕПОВРЕЖДЕННЫЕ СТРОЕНИЯ: АФРИКА" / "Новобранцы: 6 дн. осталось".

/// The geoscape reader's activation gate.
///
/// Same rule as the ant farm's, and for the same reason: UIStrategyMap is a screen whose input
/// only reaches it while it is the CURRENT screen (the first non-none stack entry, matching
/// GetCurrentScreen's own definition — no state-bit filter). Anything above it has already
/// taken the arrows before the map gets a say.
struct GeoscapeGate {
    /// The stack was readable and had entries. ⛔ When this is false, `mapOnStack` is NOT an
    /// answer: the stack finder goes blank for a poll or two while a popup is pushed over the map,
    /// and a watcher that took that blank for "the map left" reset itself and swallowed the doom
    /// edges under the popup (`docs/surfaces/strategy.md`, the Avatar project meter).
    bool stackRead = false;
    bool mapOnStack = false; // UIStrategyMap is somewhere on the stack
    bool current = false;    // ...and it is the first entry
    char blocker[64] = {};   // what sits on top instead, when it is not; for the log
    /// The map itself, and ONLY while `current` — the same walk that answered the gate already
    /// had it in hand, so re-walking to find it again is two answers to one question waiting to
    /// disagree. ⛔ Use it within the call that produced it and never store it: this is a
    /// return-by-value convenience, not a cache, and the never-hold-a-UObject rule applies.
    void* map = nullptr;

    bool Active() const { return current; }
};

/// Game thread only.
GeoscapeGate ReadGeoscapeGate();

/// One icon of the mission tray.
struct TrayIcon {
    void* handle = nullptr; // UIStrategyMap_MissionIcon
    void* mapItem = nullptr; // its UIStrategyMapItem pin on the globe, or null
    int slot = 0;            // index in MissionItemUI.MissionIcons — the on-screen order
    /// The GAME's Navigator index, or -1 when this icon is not navigable.
    ///
    /// ⚠ These two numbers are deliberately different. UpdateMissions calls Navigator.AddControl
    /// only for icons it wants reachable, so the three Faction_unknown placeholders for factions
    /// not yet met are VISIBLE and NOT navigable — measured live: five visible icons, two
    /// navigable. We give the placeholders stops (a sighted player sees them) but must never
    /// move the game's navigator while sitting on one, or SelectedMapItem silently disagrees
    /// with what was last spoken.
    int navIndex = -1;
    int idNum = 0;
    int entityId = 0;   // GeoscapeEntityRef.ObjectID off the pin — stable across a re-sort
    std::string title;  // Tooltip.Title — EMPTY when the icon has no tooltip object at all
    std::string body;   // Tooltip.Body
    /// The PIN's MapPin_Header: a localized, generic noun for the kind of place ("RESISTANCE
    /// HQ"). A CPF_Localized class default, so every instance of the pin class holds it and the
    /// §4.3 staleness rule does not apply — which is what makes it usable as the last word for a
    /// slot the game gave no tooltip.
    std::string pinHeader;
    bool mission = false;   // MissionSite != none
    bool scanSite = false;  // ScanSite != none
    /// The faction slot of a faction not yet met.
    ///
    /// ⛔ Decided by the ART (ImagePath contains "Faction_unknown"), because the two obvious
    /// tests are both FALSE for it — measured live 2026-08-21. Its ScanSite IS set, to the
    /// faction's XComGameState_Haven; and it IS in NavigableControls. What is true of it is that
    /// SetMissionIconTooltip early-returned on `!IsResistanceFactionMet()` before spawning a
    /// tooltip, so ToolTip is null and there are no words. The art is the same fact a sighted
    /// player reads off the screen, and unlike "has no tooltip" it cannot be confused with an
    /// icon whose tooltip has merely not been built yet.
    bool placeholder = false;
};

/// One of the geoscape HUD's four verbs — the buttons a sighted player can see and click and no
/// keyboard in the game can reach (recon §6.3-§6.5).
///
/// These are PARITY content, not an aid, which is what earns them a Tab stop: each is drawn on
/// screen, each carries a localized label the screen itself owns, and each has a handler the
/// game already calls. The world ring and the `;` status block are the opposite case and stay
/// off the Tab cycle — they enumerate regions and continents nobody can read off the globe.
struct HudVerb {
    enum class Kind {
        DarkEvents,       // the ADVENT operations board
        ChosenInfo,       // UIChosenInformation
        ResistanceOrders, // UIStrategyPolicy
        ResNetToggle,     // the resistance-network map overlay
    };
    Kind kind = Kind::DarkEvents;
    void* button = nullptr; // the UIButton to Click(), or null for a screen-function verb
    std::string label;      // already localized
    bool on = false;        // ResNetToggle only: m_bResNetForcedOn
};

struct StrategyMapView {
    void* screen = nullptr;
    void* navigator = nullptr;
    int selected = -1;  // Navigator::SelectedIndex — the GAME's index, not a slot
    int uiState = 0;    // EStrategyMapState
    std::vector<TrayIcon> icons;
    /// Only the verbs the game is actually SHOWING. Each is gated on the widget's own visible
    /// bit, which here is the game's own answer rather than a rule of ours: RefreshData calls
    /// Show()/Hide() on the two HUD buttons off HaveMetAnyChosen() / HaveMetAnyFactions(), and
    /// UpdateDarkEvents shows the Dark Events container only when ShowDarkEventsButton() says
    /// so. (This is the documented child-widget exception to "never gate readiness on
    /// bIsVisible" — a BUTTON's visible bit is toggled deliberately and does mean hidden.)
    std::vector<HudVerb> verbs;
};

/// UIStrategyMap when it is the current screen. Game thread only.
void* FindStrategyMapScreen();

StrategyMapView ReadStrategyMap(void* screen);

/// The Avatar Project meter, top centre of the geoscape — what `UIStrategyMap_HUD` RETAINS of it.
///
/// The pips themselves go to Flash as a `QueueNumber` sequence and are gone, but the panel keeps
/// the one number they were drawn from: `CachedDoom`, rewritten by every `UpdateData` from
/// `GetCurrentDoom()` AFTER the pips are pushed, so it is exactly the count on screen (pending
/// doom still mid-animation is excluded, as it is from the pips). The MAXIMUM is not retained at
/// all — it is the loop bound of that push — so the companion answers it. The countdown that
/// replaces the pips once the project completes (`bDoomCounterVisible`) is likewise pushed as
/// nine strings and retained as nothing but the flag; its four unit words and its label are the
/// panel's own localized strings, populated under `[UIStrategyMap_HUD]`.
struct DoomMeterView {
    void* hud = nullptr;      // UIStrategyMap_HUD, or null when the screen has no HUD yet
    int cachedDoom = 0;       // ::CachedDoom — the pip count as last drawn
    bool countdown = false;   // ::bDoomCounterVisible — the AVATAR PROJECT COMPLETE IN timer is up
    std::string title;        // ::m_strDoomTitle ("AVATAR PROJECT")
    std::string counterLabel; // ::m_strDoomCounterLabel ("AVATAR PROJECT COMPLETE IN:")
    std::string days;         // ::m_strDoomDays .. the four unit words the countdown is drawn with
    std::string hours;
    std::string minutes;
    std::string seconds;
};

/// Game thread only. `hud` is null when the screen has no HUD panel (mid-construction).
DoomMeterView ReadDoomMeter(void* screen);

/// P11 write-back: move the game's own tray navigator onto `icon`, AND put SelectedMapItem on its
/// pin. Refuses an icon with navIndex < 0 — that is a placeholder, and the caller must leave the
/// game's navigator where it was. Game thread only.
///
/// ⛔ THE TWO HALVES ARE SEPARATE CALLS, and believing otherwise is what left the geoscape unable
/// to send the Avenger anywhere for two sessions. `UINavigator.SetSelected(Control)` does NOT fire
/// `OnSelectedIndexChanged` — read it (`UINavigator.uc:677`): it assigns `SelectedIndex`, calls
/// `NavigableControls[Index].OnReceiveFocus()`, and returns. The delegate is fired by `Next()`
/// (`:508`), `Prev()` (`:631`), `SelectFirstAvailable*` (`:719`, `:751`) and the navigation-target
/// route (`:206`) — every mover EXCEPT this one. So the moment our layer claimed Left/Right and
/// replaced the game's `Navigator.Next()/Prev()` with a `SetSelected`, `UIStrategyMap`'s
/// `OnNavigationChanged` stopped running and `SelectedMapItem` froze on whatever `OnInit`'s
/// one-shot `OnNavigationChanged(0)` had put there — tray icon 0, for the rest of the session.
///
/// ⚠ And it is `SetSelectedMapItem`, not `OnNavigationChanged`, deliberately. Calling the delegate
/// would drag in its icon-focus loop — `OnLoseFocus()` on all twenty slots, `OnReceiveFocus()` on
/// the match — and those are the game's route into `UIStrategyMap_MissionIcon.OnMouseEvent`, which
/// is where CAMSaveCurrentLocation/CAMRestoreSavedLocation live and which MoveTrayHover already
/// owns. Two owners of that pair is exactly the unmatched-IN hazard MoveTrayHover documents. The
/// one line the delegate exists to reach is this one, so reach it directly.
///
/// `NavigationSelect = true` matters too: the false branch ends in `Navigator.OnLoseFocus()` +
/// `Navigator.SetSelected()` (no argument — a DESELECT), which would undo the half above it.
///
/// Skipping the delegate also skips its `CanBeScanned()` filter (`UIStrategyMap.uc:466`, a
/// bsg-jneal gamepad-stutter guard), which leaves the selection on the PREVIOUS pin when the new
/// one is a scan site that cannot currently be scanned. For a controller that is a cosmetic
/// stutter; for us it would be a stale travel target, so not inheriting it is the point.
bool SetTraySelection(const StrategyMapView& view, const TrayIcon& icon);

/// Hand an icon the mouse-hover event the game itself uses, which is where the camera fly-to,
/// the pin highlight and the tooltip refresh all live:
///
///   OnMouseEvent(FXS_L_MOUSE_IN)  -> CAMSaveCurrentLocation, MapItem.OnMouseIn,
///                                    SetMissionIconTooltip(<site>.GetUIButtonTooltipTitle/Body),
///                                    CAMLookAtEarth(<site>.Get2DLocation(), ZoomLevel)
///   OnMouseEvent(FXS_L_MOUSE_OUT) -> MapItem.OnMouseOut, CAMRestoreSavedLocation
///
/// The camera half is gated on `bMoveCamera || `ISCONTROLLERACTIVE == false`, and on
/// mouse+keyboard that second term is already true — so it runs without our setting anything.
/// The tooltip half matters as much: it re-fetches the scan site from the history first (the
/// game's own comment says "Force an update of Scan Site game state"), so a days-remaining
/// count that has ticked since UpdateMissions last ran is refreshed by the same call.
///
/// ⛔ Call these in PAIRS, OUT on the icon being left before IN on the one being entered. They
/// are symmetric by design — IN saves the camera location and sets bOverrideZoomHide, OUT
/// restores and clears both — so an unmatched IN leaves the pin stuck in its hover state and
/// overwrites the saved location with a site the player never asked to return to.
///
/// ⛔ Never on a placeholder: with both ScanSite and MissionSite none, OnMouseEvent's final
/// else branch dereferences MissionSite.Get2DLocation(). An unmet faction's slot is saved from
/// that only by the Haven early-return at the top, which is not a guarantee worth leaning on.
/// Game thread only.
bool MoveTrayHover(const TrayIcon& icon, bool entering);

/// Run a HUD verb the way the game runs it.
///
/// The two HUD buttons go through UIButton::Click(), which fires their OnClickedDelegate and
/// self-gates on `!IsDisabled && bIsVisible` — the same guard the mouse path gets.
///
/// ⛔ Dark Events does NOT: AddDarkEventsButton wires its handler with ProcessMouseEvents(), so
/// the button carries an OnMouseEventDelegate and NO OnClickedDelegate, and Click() would return
/// false having done nothing. The game's own keyboard route is what this reproduces verbatim,
/// `none` argument included — UIStrategyMap.OnUnrealCommand's FXS_BUTTON_Y case is
/// `if (ShowDarkEventsButton() && DarkEventsButton != None) OnDarkEventsClicked(none,
/// FXS_L_MOUSE_UP);` — and the handler ignores the panel it is handed, testing only the command.
///
/// ResNetToggle calls the screen's own zero-parameter OnResNetClicked(), which owns the
/// forced-on flag and the SetUIState ordering its comment warns about. Game thread only.
bool ActivateHudVerb(const StrategyMapView& view, const HudVerb& verb);

/// Activate whatever the tray cursor is on — for a site, "send the Avenger there".
///
/// ⛔ THIS SENDS THE MOUSE'S OWN CLICK TO THE ICON, and the constant is the whole subtlety:
/// `FXS_L_MOUSE_UP_DELAYED` (400), never `FXS_L_MOUSE_UP` (391). `UIIcon.OnMouseEvent:280` fires
/// `OnClickedDelegate` on 400 and has no case for 391 at all.
///
///   icon.OnMouseEvent(FXS_L_MOUSE_UP_DELAYED)
///     -> UIIcon.OnMouseEvent -> OnClickedDelegate()          // bound by SetScanSite/SetMissionSite
///          -> <this icon's site>.AttemptSelectionCheckInterruption()
///               -> CanInteract() && !DisplayInterruptionPopup()
///                    -> AttemptSelection -> ConfirmSelection
///                         -> XComHQ.SetPendingPointOfTravel(entity)   // the Avenger flies
///
/// ⛔ The obvious-looking alternatives are both traps, and one of them shipped in this file:
///
///  - `FXS_BUTTON_A` **to the screen** was the first attempt. `UIStrategyMap.OnUnrealCommand`
///    acts on its own `SelectedMapItem` and ignores everything the caller passes, so it is only
///    ever as right as the focus write-back — and the write-back was silently doing nothing (see
///    SetTraySelection). `Enter` on a POI therefore fired at tray icon 0, which when the Avenger
///    was parked on it answered `ToggleScan()`: the player pressed *fly here* and heard the scan
///    start, indistinguishable from Space. Depending on a mutable selection to name the target is
///    the defect; sending to the icon removes the dependency rather than guarding it.
///  - `FXS_BUTTON_A` **to the icon** looks better still — `UIStrategyMap_MissionIcon` has its own
///    handler at `:228` with a parked-here `ToggleScan` case more general than the screen's. But
///    its else branch sends `OnMouseEvent(FXS_L_MOUSE_UP)`, i.e. 391, so the branch that should
///    reach `AttemptSelectionCheckInterruption` does nothing but play a click sound. It is dead
///    code in the shipped game, unnoticed because the screen's block returns first on a
///    controller. **Do not resurrect it; go through the click.**
///
/// `AS_SetLock` is a Flash-only `MC.FunctionBool` and never touches `IsDisabled`, so a visibly
/// locked pin still reaches the delegate and is refused by the game's own `CanInteract()` — the
/// same answer a mouse click gets. Every gate along the chain stays the game's.
///
/// It also nudges `SelectedMapItem` onto this icon's pin first, but that is COSMETIC — the 3D
/// cursor, the pin highlight and the nav-help read it. Nothing about which site gets activated
/// depends on it. Game thread only.
bool ActivateTraySelection(const StrategyMapView& view, const TrayIcon& icon);

// ---- the Chosen dossier (UIChosenInfo) — docs/chosen-bond-recon.md ----
//
// The Chosen database: four tab buttons (one per Chosen met, UNKNOWN CHOSEN for the rest) over one
// dossier panel — counters, two meters, strengths, weaknesses, the rival faction. Every word is
// pushed through MC.BeginFunctionOp with locals on both sides; what the screen RETAINS is the
// state it populated Flash from (AllActiveChosen, SelectedChosen) and the tab index
// (CurrentChosenIndex), so X2AccessChosenReader re-derives the panel from SelectedChosen exactly
// as RefreshDisplay does — reveal filter included — and native reads only two integers.
//
// ⛔ SWITCHING CHOSEN IS A KEYBOARD DEAD END IN VANILLA: OnUnrealCommand takes FXS_DPAD_UP/DOWN
// and the virtual stick only (no FXS_ARROW_*), and Enter, Space and Escape all CLOSE the screen.
// On mouse+keyboard the second Chosen is reachable only by clicking its tab, so the recipe
// supplies the switch through the screen's own SelectChosen().

struct ChosenInfoView {
    void* screen = nullptr;
    int selected = -1; // CurrentChosenIndex — which tab the panel is describing
    int count = 0;     // AllActiveChosen.Length — how many of the four tabs name a real Chosen
};

/// The focused UIChosenInfo, or null. Stack-managed (XComPresentationLayerBase pushes it, in
/// both layers). Game thread only.
void* FindChosenInfoScreen();

ChosenInfoView ReadChosenInfo(void* screen);

// ---- soldier bonds (UISoldierBondScreen) — docs/chosen-bond-recon.md §1.4 ----
//
// One soldier's bond page: the soldier and their bondmate up top, a sortable roster of every
// other living soldier below (the crew, or the squad when bSquadOnly), each row showing rank,
// class, compatibility, cohesion and — on rows where CanHaveBondAtLevel says so — a CREATE BOND
// button. The rows are UISoldierBondListItem (a UIButton) and retain the UnitRef they were built
// from and nothing else; the companion re-derives their cells the way UpdateData does.
//
// ⛔ MAKING A BOND IS MOUSE-ONLY IN VANILLA. The row's ActivateBondButton calls DisableNavigation()
// and OnConfirmBond is reached from its mouse handler or FXS_BUTTON_A; the keyboard Enter is
// OnSelectSoldier (switch the page to that soldier), and sorting is FXS_DPAD_LEFT/RIGHT +
// FXS_BUTTON_L3 — controller constants all three. Tab / LeftShift cycle soldiers (outside the
// squad screens), which the layer's own Tab displaces and re-provides on the soldier node.

struct BondRow {
    void* handle = nullptr; // UISoldierBondListItem
    int index = 0;          // the list's own item index (SetSelectedIndex's)
    int unitId = 0;         // UnitRef.ObjectID — the join key to the companion's row record
    bool disabled = false;  // IsDisabled: bonded to someone else, in either direction
    /// ActivateBondButton.bIsVisible — the game's own "a bond can be made with this one right
    /// now" (UpdateData Show()s it exactly when CanHaveBondAtLevel passes). The documented
    /// child-widget exception to never-gate-on-bIsVisible: a BUTTON's visibility is deliberate.
    bool bondOffered = false;
};

struct BondScreenView {
    void* screen = nullptr;
    void* list = nullptr; // UISoldierBondScreen::List
    std::vector<BondRow> rows;
    int selected = -1;      // UIList::SelectedIndex
    int unitId = 0;         // UnitRef.ObjectID — whose bonds these are; the wire-cache discriminator
    bool squadOnly = false; // bSquadOnly — opened from squad select or after action
    int sortType = 0;       // ESoldierBondSortType
    bool flipSort = false;  // m_bFlipSort — the header arrow's direction
};

/// The focused UISoldierBondScreen, or null. Game thread only.
void* FindBondScreen();

BondScreenView ReadBondScreen(void* screen);

/// P11 write-back into the roster. Game thread only.
bool SetBondSelection(const BondScreenView& view, int index);

// ---- bond confirmation (UISoldierBondConfirmScreen) — docs/chosen-bond-recon.md §1.5 ----
//
// "DEVELOP BOND - LEVEL n": the two soldiers, the benefits the new level brings, CONFIRM BOND.
// Every word is companion-side (the screen retains the two unit states and the staff slot it was
// opened from); activation is the screen's own input path — Enter is ConfirmClicked, Escape is
// Cancel(), and Cancel EMPTIES the staff slots when the screen came from the Training Center, so
// nothing here may reach the handlers any other way. No offsets.

/// The focused UISoldierBondConfirmScreen, or null. Game thread only.
void* FindBondConfirmScreen();

// ---- the monthly report (UIResistanceReport) and its Chosen page
//      (UIResistanceReport_ChosenEvents) — docs/resistance-report-recon.md ----
//
// Two Enter-only documents shown back to back at the end of every month: the council's report
// card (supplies, activity lists, Avatar progress), then — pushed by the report's OWN CloseScreen
// — one column per Chosen with the knowledge meter, any new knowledge level, last month's
// activities and the action planned for this one. Both push every word through
// MC.BeginFunctionOp and retain nothing; both re-derive cleanly, because every value comes off a
// screen function (GetDateString, GetSupplyRewardString) or the state objects the screen names
// (ChosenRefs). The Chosen page also carries a ONE-TIME warning popup (the first knowledge gain
// of a campaign), drawn over the columns; while it is up the screen's Enter dismisses it instead
// of closing, so the recipe reads the flag and offers exactly that.

struct ChosenEventsView {
    void* screen = nullptr;
    int count = 0;             // ChosenRefs.Length — the columns, met or not
    bool warningShown = false; // bShowChosenWarningPopup — the knowledge warning is up
    bool simpleView = false;   // bSimpleView — opened from somewhere other than the report
};

/// The focused UIResistanceReport_ChosenEvents, or null. Game thread only.
void* FindChosenEventsScreen();

ChosenEventsView ReadChosenEvents(void* screen);

/// The focused UIResistanceReport, or null. Every word is companion-side, so no view struct.
/// Game thread only.
void* FindResistanceReportScreen();

// ---- the covert action report (UICovertActionReport) — docs/covert-actions-recon.md §8b ----
//
// The RESULT screen of the covert-action loop: pushed by XComHQPresentationLayer.UIActionCompleted
// once the action's clock has run out and the geoscape camera has finished panning to it — never
// from the Ring, the map pin or the event queue. The action's name and post-mission narrative, the
// reward it GRANTED (already given by the time the screen exists), the faction, and one card per
// soldier who went: promotion, XP, cohesion, the slot reward, and — in red — the risks that fired
// on them, or SOLDIER KILLED.
//
// The widget tree is two nodes (the CONTINUE button and an empty slot container — on this screen
// InitSlotContainer spawns no slotMC_N panels), so native reads the button and the action's id and
// X2AccessCovertActionReader.DescribeCovertActionReport supplies the words. ✅ The per-soldier
// results are the one part of the whole covert-action family that IS retained: UICovertActionSlot
// (a UObject, not a panel) keeps PromoteLabel and Value1..Value4, and the report's Update branch is
// what passes them to Flash — so the companion reads them off the slot object rather than
// re-deriving, which also makes it immune to the Highlander's RewardsNotGivenOnCompletion change.
//
// ⛔ ESCAPE IS CONTINUE. OnUnrealCommand answers A, Enter, Space and Escape with one call,
// OnContinueClicked, which closes the screen, raises the reward popups and DELETES the action's
// state object — GetAction() answers none from then on. There is no abort, no second verb, and no
// slot verb either (HandleClick gates on !bStarted, which is false forever on a completed action).

struct CovertReportView {
    void* screen = nullptr;
    int actionId = 0;               // ActionRef.ObjectID — the wire cache's discriminator
    void* continueButton = nullptr; // ContinueButton (UILargeButton), once RefreshConfirmPanel ran
    std::string continueLabel;      // its retained Text — m_strGenericContinue on mouse+keyboard
};

/// The focused UICovertActionReport, or null. Game thread only.
void* FindCovertReportScreen();

CovertReportView ReadCovertReport(void* screen);

// ---- the armory hub (UIArmory_MainMenu) — docs/armory-recon.md §1 ----
//
// The menu every soldier operation routes through: the soldier header, a list of eight verbs
// (customize, loadout, PCS, weapon upgrade, promote, propaganda, bonds, dismiss) with a description
// panel under it, and the ability / trait summary on the right. ⛔ THE HIGHLANDER REWROTE THIS
// SCREEN (Issue #47): the rows are named fields with MCNames `ArmoryMainMenu_<X>Button`, a mod
// may insert rows through `OnArmoryMainMenuUpdate`, and OnAccept re-sends Enter to the list. So a
// row's identity is its MCNAME, never its index, and the reader is written against the Highlander
// class the mod requires.
//
// ✅ The rows RETAIN their words — plain UIListItemStrings through InitListItem → SetText — along
// with bDisabled and the attention icon; what they do not keep is the disabled REASON (SetDisabled
// parks it in the tooltip manager) or the description (OnSelectionChanged writes it straight into
// a Flash child), and the header and the summary panel are MC ops. X2AccessArmoryReader supplies
// those. Vanilla's keyboard walks the list and Enters already; what it lacks beyond the words is
// the soldier cycle (Tab / LeftShift, both the layer's) and the abilities/traits toggle
// (FXS_BUTTON_X, or a nav-help button the mouse reaches).

struct ArmoryMenuRow {
    void* handle = nullptr; // UIListItemString
    int index = 0;          // the list's own item index (SetSelectedIndex's)
    std::string key;        // MCName — `ArmoryMainMenu_LoadoutButton` …; the join to the companion
    std::string label;      // Text, cleaned — the promote row's <img> prefix stripped
    bool disabled = false;  // bDisabled
    bool attention = false; // the "!" badge: an AttentionIcon spawned and showing
};

struct ArmoryMenuView {
    void* screen = nullptr;
    void* list = nullptr; // UIArmory_MainMenu::List
    std::vector<ArmoryMenuRow> rows;
    int selected = -1; // UIList::SelectedIndex
    int unitId = 0;    // UIArmory::UnitReference.ObjectID — the wire cache's discriminator
};

/// The focused UIArmory_MainMenu, or null — every screen it opens pushes over it. Game thread only.
void* FindArmoryMenuScreen();

ArmoryMenuView ReadArmoryMenu(void* screen);

/// P11 write-back: UIList::SetSelectedIndex, which fires OnSelectionChanged and repaints the
/// description panel. Game thread only.
bool SetArmoryMenuSelection(const ArmoryMenuView& view, int index);

/// Activate a row by the game's own keyboard path: select, then hand the SCREEN an Enter —
/// UIArmory.OnUnrealCommand → OnAccept, which reads the list's selection and (Highlander) re-sends
/// Enter to the list so a mod-wrapped row delegate runs. A greyed row is passed through: OnAccept
/// answers it with the game's own refusal sound. Game thread only.
bool ActivateArmoryMenuRow(const ArmoryMenuView& view, int index);

// ---- weapon upgrades (UIArmory_WeaponUpgrade) and its pattern picker (UIArmory_WeaponTrait)
//      — docs/armory-recon.md §2 ----
//
// The weapon-mod screen the hub's WEAPON UPGRADE row opens: the soldier header, a left list of
// the weapon's upgrade SLOTS (filled ones first, then EMPTY SLOT rows), a right list of the
// upgrades in the inventory that opens when a slot is chosen, the customize rows under the left
// list (name, colour, pattern), the previewed stat block and the upgrade description box.
//
// ⛔ ONLY ONE OF THE TWO UPGRADE LISTS IS LIVE, and `ActiveList` says which — the loadout's
// arrangement exactly. ChangeActiveList DisableNavigation()s the other container, so the slots list
// becomes a read-only mirror while the upgrade picker is open (the selected slot row is bIsLocked
// then), and Escape backs out of the picker before it leaves the screen. Both lists' rows are
// UIArmory_WeaponUpgradeItems and RETAIN their words (title, subTitle, Count and three bits — the
// loadout row's layout through Count); what they do not keep is a greyed row's reason
// (`disabledText` is never assigned, and here not even a tooltip gets it). Everything else on the
// screen — the panel titles, the slot counter, whose weapon it is, the description box, the whole
// stat block (UIArmory_WeaponUpgradeStats has no members at all) — is Flash, and
// X2AccessWeaponUpgradeReader supplies it, including the PREVIEW the stats show for the row under
// the cursor.
//
// Enter is OnAccept → OnItemClicked(ActiveList, ActiveList.SelectedIndex): on a slot row it opens
// the picker, on an upgrade row it raises the confirm dialog (or commits outright under the
// reuse-upgrades continent bonus). With NO selection in the active list, OnAccept forwards to the
// customize list — the customize rows are otherwise reached by D-pad only, so they are activated
// through their own row handler here. The pattern picker is a SUBCLASS that reuses SlotsList as a
// plain UIListItemString list with the options retained in `Data`; Enter picks, Escape REVERTS.

enum class WeaponUpgradeList { Slots, Upgrades };

struct WeaponUpgradeRow {
    void* handle = nullptr;  // UIArmory_WeaponUpgradeItem (or a UIListItemString on the picker)
    int index = 0;           // the list's own item index
    std::string title;       // the upgrade's name, or m_strEmptySlot
    std::string subtitle;    // GetUpgradeEffectForUI — "Aim +10", or m_strUpgradeAvailable
    int count = 0;           // Count: how many are in the inventory (upgrade rows)
    bool infinite = false;   // bIsInfinite
    bool locked = false;     // bIsLocked — the chosen slot, frozen while the picker is open
    bool disabled = false;   // bIsDisabled
    bool hasUpgrade = false; // UpgradeTemplate != none (a filled slot; every upgrade row)
    int slotIndex = -1;      // SlotIndex — the weapon's slot number on slot rows, -1 on upgrade rows
};

struct WeaponUpgradeView {
    void* screen = nullptr;
    bool trait = false; // a UIArmory_WeaponTrait — the pattern picker
    void* slotsList = nullptr;
    void* upgradesList = nullptr;
    void* customizeList = nullptr;
    bool upgradesActive = false; // ActiveList == UpgradesList
    std::vector<WeaponUpgradeRow> slots;
    int slotsSelected = -1;
    std::vector<WeaponUpgradeRow> upgrades;
    int upgradesSelected = -1;
    std::vector<OptionRow> customize; // the three UIMechaListItems: name, colour, pattern
    int customizeSelected = -1;
    int weaponId = 0; // WeaponRef.ObjectID — the wire cache's discriminator
    int unitId = 0;   // UIArmory::UnitReference.ObjectID
    // The pattern picker: SlotsList's UIListItemString rows, and where the game landed.
    std::vector<MenuItem> options;
    int startingIndex = -1;
    int optionsSelected = -1;
};

/// The focused UIArmory_WeaponUpgrade or UIArmory_WeaponTrait, or null. The picker is pushed OVER
/// the upgrade screen, so the topmost-entry test is what tells them apart. Game thread only.
void* FindWeaponUpgradeScreen();

WeaponUpgradeView ReadWeaponUpgrade(void* screen);

/// P11 write-back into one of the two upgrade lists. ⛔ Refuses the SLOTS list while the picker
/// is open: the game has DisableNavigation()ed it, and PreviewUpgrade reads the slot from that
/// list's selection whichever list the cursor is in — moving it under an open picker would
/// preview the wrong slot. Game thread only.
bool SelectWeaponUpgradeRow(const WeaponUpgradeView& view, WeaponUpgradeList which, int index);

/// Activate a row by the game's own path: select, then hand the SCREEN an Enter (UIArmory's
/// OnUnrealCommand → OnAccept → OnItemClicked on the active list). Game thread only.
bool ActivateWeaponUpgradeRow(const WeaponUpgradeView& view, WeaponUpgradeList which, int index);

/// P11 write-back into the customize list. Game thread only.
bool SelectWeaponCustomizeRow(const WeaponUpgradeView& view, int index);

/// Press a customize row through its own OnUnrealCommand(Enter) — the rename dialog, the colour
/// selector, the pattern picker. Game thread only.
bool ActivateWeaponCustomizeRow(const WeaponUpgradeView& view, int index);

/// The pattern picker's write-back and activation: its rows live in SlotsList. Game thread only.
bool SelectWeaponPattern(const WeaponUpgradeView& view, int index);
bool ActivateWeaponPattern(const WeaponUpgradeView& view, int index);

// ---- soldier customization (UICustomize and its family) — docs/customize-recon.md §2 ----
//
// One list of UIMechaListItems under a soldier header, on every screen of the family: the
// category menu (UICustomize_Menu), the category screens (Info, Head, Body, Weapon, Props — the
// Templar / Skirmisher / SPARK variants are subclasses that only change the rows), and the option
// picker (UICustomize_Trait; UICustomize_Voice is its controller-only variant). ✅ THE ROWS RETAIN
// THEIR WORDS: every one is built through UpdateDataDescription / UpdateDataValue /
// UpdateDataColorChip / UpdateDataCheckbox / UpdateDataButton, so the label, the current choice,
// a checkbox's state, a swatch's colour and a button's label are all on the widget, and a greyed
// row's reason is the tooltip SetDisabled parks on its BG. What is NOT retained: the soldier
// header (a Flash push — X2AccessCustomizeReader re-derives it with the loadout's composer), the
// info screen's biography (setCharacterBio, likewise), and the picker's TITLE — UpdateTrait never
// stores the string it is handed, so the picker is named by the row on the screen beneath that
// opened it, which keeps its selection across the push.
//
// ⛔ THE COLOUR SELECTOR REPLACES THE LIST. GetColorSelector hides the list and spawns a
// UIColorSelector: a grid of UIColorChips, each retaining its Index / Row / Col and its colour on
// its BG box, and the screen routes every key to the selector first for as long as it exists
// (ColorSelector != none). Accepting a chip runs the screen's own Set* delegate — commit,
// UpdateData, CloseColorSelector — and Escape is the screen's own OnCancel → CloseColorSelector(true),
// which REVERTS to InitialSelection.
//
// Enter is the screen's own OnUnrealCommand: UICustomize routes it to the list, whose selected row
// answers it (a description / value / colour row Clicks its delegate), and UICustomize_Trait
// answers it at SCREEN level (OnClickLocal: pick, then close). ⛔ The menu screen's checkboxes are
// bReadOnly (UICustomize_Menu.OnListInited), so the row's own Enter is CONSUMED WITHOUT TOGGLING —
// the game's route is FXS_BUTTON_A → SimulateMouseClickOnCheckbox, and that function is what a
// checkbox row's activation calls (directly: the A-button branch is gated on bIsVisible, the flag
// the game leaves false on screens it is plainly showing).

struct ColorChip {
    void* handle = nullptr; // UIColorChip
    int index = 0;          // Index — the accept delegate's argument, and the number the chip draws
    int row = 0;            // Row / Col — the grid position the game laid the chip out at
    int col = 0;
    std::string hex;        // BG.BGColor, the game's own "0x00RRGGBB"
    bool focused = false;   // the chip the container's navigator holds (its highlight is showing)
};

struct CustomizeView {
    void* screen = nullptr;
    void* list = nullptr;   // UICustomize::List
    bool trait = false;     // a UICustomize_Trait (or _Voice): the option picker
    bool inArmory = false;  // bInArmory — the soldier cycle exists only there
    void* unit = nullptr;   // UICustomize::Unit — a wire-cache KEY, compared and never dereferenced
    int unitId = 0;         // UnitRef.ObjectID (0 for a character-pool unit, which is in no history)
    std::vector<OptionRow> rows;
    int selected = -1;      // List.SelectedIndex
    // The picker: where the game landed (the current choice, which Escape reverts to), and the
    // label of the row beneath that opened it.
    int startingIndex = -1;
    std::string openerLabel;
    // The colour selector, while it is open.
    void* colorSelector = nullptr;
    std::vector<ColorChip> chips; // in Index order, which is row-major
    int initialColor = -1;        // InitialSelection — the colour worn now
};

/// The topmost screen of the family, focused, or null: the picker is pushed OVER the screen that
/// opened it, so the top of the stack being one of the family is exactly the screen the keys
/// reach. Game thread only.
void* FindCustomizeScreen();

CustomizeView ReadCustomize(void* screen);

/// P11 write-back into the list — UIList.SetSelectedIndex, which on a picker fires the
/// OnSelectionChanged delegate that previews the option on the pawn. Game thread only.
bool SetCustomizeSelection(const CustomizeView& view, int index);

/// Activate a row by the game's own path: select, then the SCREEN's Enter — or, for a checkbox
/// row, the screen's SimulateMouseClickOnCheckbox (see the comment above). Game thread only.
bool ActivateCustomizeRow(const CustomizeView& view, int index);

/// The row's OWN Enter, with the row selected: on a picker row carrying a button (the voice
/// picker's PREVIEW) that presses the button, and nothing closes. Game thread only.
bool PressCustomizeRow(const CustomizeView& view, int index);

/// P11 write-back onto a colour chip: SetSelectedNavigation, i.e. the chip container's
/// navigator, whose OnSelectedIndexChanged previews the colour on the pawn exactly as the game's
/// own arrowing does. Game thread only.
bool SelectColorChip(const CustomizeView& view, int index);

/// Accept a chip: select it, then the screen's Enter — routed to the selector, and by the
/// selector to the chip its navigator holds. Game thread only.
bool ActivateColorChip(const CustomizeView& view, int index);

// ---- manage equipment (UIManageEquipmentMenu) — docs/customize-recon.md §1 ----
//
// The three-row menu the loadout and squad select open from a nav-help button (a gamepad's LT;
// no key on mouse+keyboard): strip items, strip gear, strip weapons. Modelled on the pause menu
// by its author and read like it — UIListItemString rows retaining their Text, the title a
// localized string on the screen — and, unlike the pause menu, the rows consume nothing on Enter,
// so an Enter sent to the LIST reaches its OnItemClicked delegate (the screen's OnSelectionPressed,
// which runs the callback and closes).

struct ManageEquipmentView {
    void* screen = nullptr;
    void* list = nullptr;          // UIManageEquipmentMenu::List
    std::string title;             // m_strTitleLabel — the title SetTitle pushes
    std::vector<MenuItem> options; // rows in list order; handle is the UIListItemString
    int selected = -1;             // List.SelectedIndex
};

/// The focused UIManageEquipmentMenu, or null. Game thread only.
void* FindManageEquipmentScreen();

ManageEquipmentView ReadManageEquipment(void* screen);

/// P11 write-back. Game thread only.
bool SetManageEquipmentSelection(const ManageEquipmentView& view, int index);

/// Select, then Enter to the LIST. Game thread only.
bool ActivateManageEquipmentRow(const ManageEquipmentView& view, int index);

} // namespace GameUI
