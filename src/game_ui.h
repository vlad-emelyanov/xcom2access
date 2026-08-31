#pragma once

// Game-UI access for screen recipes (spec P1 read, P2 invoke, P11 focus write-back).
//
// XComGame/Engine fields are read through EXPLICIT offsets from the generated SDK's
// reflection-dump comments (validated against a live process) — never through the generated
// class layouts, whose pack(0x8) compilation drifts from the engine's real layout. This
// interface stays SDK-free. Handles are opaque game object pointers: valid to USE only
// immediately after the read that produced them, on the game thread — never stored across
// frames (store them in ControlId::Reference only as comparison tokens, per the kernel's rule).

// The companion bridge (FindCompanionCDO / CallScriptFunction / Find*Function / CleanText) used
// to be declared here too. It moved to its own header because it is not a UI interface and six
// TUs wanted only it; this include keeps every existing consumer of game_ui.h seeing the same
// set of names it always did.
#include "script_call.h"

#include <string>
#include <vector>

namespace GameUI {

struct MenuItem {
    void* handle = nullptr; // AUIButton* — opaque
    std::string label;      // P10-cleaned display text
    int index = -1;         // the row's REAL UIList index where the producer walks one (-1
                            // elsewhere): options positions drift past a skipped unlabeled
                            // row, so selection and activation must use this, never the
                            // vector position
    bool focused = false;   // the game's own focus bit (drives the kernel's selected part)
    bool disabled = false;
    bool attention = false; // the game's "!" badge (UIButton.bNeedsAttention)
};

/// My2K account-link state, decided exactly as UIFinalShell.UpdateMy2KButtonStatus decides
/// which status text to show the sighted player.
enum class My2KStatus { Unknown, Offline, NotLinked, Linked };

/// The shell's non-menu content a sighted player sees: the My2K account button (bottom right)
/// and the MOTD news ticker (bottom edge). Absent parts stay null/empty.
struct ShellExtras {
    void* my2kHandle = nullptr;  // visible My2K button, or null
    My2KStatus my2kStatus = My2KStatus::Unknown;
    void* tickerHandle = nullptr; // visible ticker, or null
    std::string tickerText;       // P10-cleaned ticker content
};

/// The game's loading overlay (UILoadScreenAnimation), or null. It takes no stack focus — it is
/// drawn over whatever is transitioning out — so presence alone says nothing; ask
/// LoadScreenShowing. Game thread only.
void* FindLoadScreen();

/// Is the loading overlay actually up? The layer has nothing to read while a world changes, and
/// silence there is indistinguishable from a hang, so this is what a "Loading" cue keys off.
bool LoadScreenShowing();

// ---- the screen stack's own view of whether anything is navigable ----

/// A screen can be on top of the stack, inited, focused — and still be nothing a player can
/// touch, because the game has gated input or hidden the whole UI over it. Starting a campaign
/// pops the narrative screen and leaves the difficulty screen underneath looking perfectly live
/// for the twelve seconds the loading movie plays, which is exactly this.
struct StackState {
    bool valid = false; // false when the stack itself could not be read — then assume nothing
    bool inputBlocked = false;   // IsInputBlocked: the UI system is dropping input outright
    bool screensHidden = false;  // Hide() took the whole UI down (transitions, loading movies)
    bool cinematic = false;      // HideUIForCinematics()

    bool Interactive() const { return !inputBlocked && !screensHidden && !cinematic; }
};

/// Game thread only. `valid` is false when there is no readable stack, in which case callers
/// must fall back to their own judgement rather than treating the game as non-interactive.
StackState ReadStackState();

/// Is a full-screen movie playing (XComEngine.IsAnyMoviePlaying)? A movie is not a UIScreen — it
/// plays OVER the stack — so nothing else in the UI state reflects it, and the screen underneath
/// goes on reporting itself inited, visible, focused and topmost the whole time. Calls into
/// UnrealScript, so: GAME THREAD ONLY.
bool MoviePlaying();

// ---- in-game pause menu (UIPauseMenu) ----

/// The Escape menu shown over a running campaign, in both the tactical and the strategy layer
/// (UIShellStrategy extends UIPauseMenu, so the same read covers it). A plain UIList of
/// UIListItemString rows whose contents depend on where the game is — no fixed row set.
struct PauseMenuView {
    std::string title;             // m_sPauseMenu, P10-cleaned
    std::vector<MenuItem> options; // rows in list order; handle is the UIListItemString
    int selected = -1;             // List.SelectedIndex
};

/// The focused UIPauseMenu instance, or null. Shares the new-game flow's single GObjects pass.
/// Game thread only.
void* FindPauseMenu();

PauseMenuView ReadPauseMenu(void* screen);

/// Drive the list's selection (focus write-back). Game thread only.
bool SetPauseMenuIndex(void* screen, int index);

/// Activate a row through the screen's own OnChildClicked — the same entry point its mouse and
/// keyboard paths use, so every side effect (selection, sound, confirmation dialog) still
/// happens. Game thread only.
bool ActivatePauseMenuItem(void* screen, int index);

/// The UIFinalShell (main menu) instance, or null. Caches the found instance and re-validates
/// it cheaply on every call; a full GObjects scan runs at most once a second. Game thread only.
void* FindMainMenuShell();

/// Screen is inited and is the focused screen (nothing stacked on top of it). Works for any
/// UIPanel-derived object (the flag dword lives at the UIPanel level). Deliberately does NOT
/// test bIsVisible: the game leaves that false on screens it is plainly showing.
bool IsScreenFocused(void* screen);

/// Drop every screen finder's rate limit, so the next lookup runs a full GObjects pass instead
/// of answering from a cache that may predate the change. Call it when a screen change is known
/// to have happened (the navigator detaching): a finder that waits out its second there costs
/// the player a silent second on every screen entry. Game thread only.
void HintScreenChange();

/// One line naming every screen the shared lookup tracks, with its pointer and state bits.
/// Diagnostics only: it says whether a screen we failed to read was missing, unresolved, or
/// merely not focused — the three failures that look identical from the outside.
std::string DescribeTrackedScreens();

/// The game's own screen stack, top first, one line per entry: index, class name, pointer,
/// state bits, and a decode of the bits that decide reachability. Unlike DescribeTrackedScreens
/// this is not filtered to classes we know about, which is the point — it answers "what is
/// actually on screen right now?" for a screen that has no recipe yet, and it shows the
/// not-inited and removed entries the finders skip. Diagnostics only. Game thread only.
std::string DescribeScreenStack();

/// The raw UIPanel state dword behind IsScreenFocused. Diagnostics only — the navigator must
/// key off IsScreenFocused, never off bits picked out of this by hand.
uint32_t ScreenStateBits(void* screen);

/// The shell's MAINMENU buttons: visible ones, in declaration order. Empty while the menu is
/// still building.
std::vector<MenuItem> ReadMainMenu(void* shell);

/// The shell's My2K button and news ticker (see ShellExtras). The account status is refreshed
/// through the game's own FiraxisLive query functions at most once a second — the same cadence
/// the game's own status timer uses. Game thread only (makes outbound calls).
ShellExtras ReadShellExtras(void* shell);

/// P11 write-back: drive the game's own navigation onto this button
/// (UIPanel.SetSelectedNavigation) — brings the game's highlight and hover sound with it.
bool FocusButton(void* button);

/// P2: run the game's own click handler (UIButton.Click).
bool ClickButton(void* button);

/// Play the game's own menu-scroll cue ("Play_Mouseover") on the current world's sound
/// manager. The native cue lives only in UINavigator's key handler — a path our claimed keys
/// bypass — so kernel-driven moves must plumb it explicitly. Game thread only.
bool PlayNavSound();

// CleanText is declared in script_call.h, included above — the recipes here call it on widget
// text just as the bridge's consumers call it on payloads.

// ---- UIMechaListItem rows (shared: settings AND the new-game flow screens) ----
//
// A "mecha list item" is the game's one-line settings widget — a label plus at most one of
// checkbox / spinner / dropdown / slider / button. Every screen below is built out of them,
// so one row type and one reader serve them all.

/// The widget a row carries (UIMechaListItem's EUILineItemType, folded to what the recipes
/// distinguish: SpinnerAndButton reads as Spinner, ColorChip as Value).
enum class OptionWidget { Description, Spinner, Dropdown, Slider, Checkbox, Button, Value };

struct OptionRow {
    void* handle = nullptr; // AUIMechaListItem* — opaque
    int index = 0;          // row index within its owning list (List.SetSelectedIndex target);
                            // -1 on rows that hang off a plain panel instead of a UIList
    OptionWidget widget = OptionWidget::Description;
    std::string label;      // P10-cleaned Desc text
    std::string value;      // spinner value / dropdown selection / value text / button label
    int percent = 0;        // slider position 0-100 (Slider rows only)
    bool checked = false;   // checkbox state (Checkbox rows only)
    bool disabled = false;
    bool focused = false;   // the game's own focus bit — meaningful on rows parented to a
                            // plain panel, where there is no list index to compare against
};

// ---- Settings screen (UIOptionsPCScreen) ----

inline constexpr int kOptionsTabCount = 5;

/// One read of the whole settings surface: title, tabs (Flash-side — names come from the
/// screen's localized strings, state from m_iCurrentTab), the current tab's visible rows, the
/// game's own selected row, and the visible bottom buttons in on-screen order.
struct OptionsView {
    std::string title;                  // m_strTitle, P10-cleaned
    int currentTab = 0;                 // EUI_PCOptions_Tabs index
    std::string tabNames[kOptionsTabCount];
    std::vector<OptionRow> rows;
    int selectedRow = -1;               // List.SelectedIndex (-1 = none)
    std::vector<MenuItem> buttons;      // Save&Exit, Discard, Credits, GPU auto-detect (visible only)
};

/// The UIOptionsPCScreen instance, or null — same caching/validation contract as
/// FindMainMenuShell. Game thread only.
void* FindOptionsScreen();

OptionsView ReadOptions(void* screen);

/// m_iCurrentTab alone — for handlers that don't need the whole view.
int GetOptionsTab(void* screen);

/// The localized tab name, or empty. `tab` is the EUI_PCOptions_Tabs index.
std::string GetOptionsTabName(void* screen, int tab);

/// The game's own tab switch (UIOptionsPCScreen.SetSelectedTab — rebuilds the row list, wraps
/// out-of-range indices). Game thread only.
bool SetOptionsTab(void* screen, int tab);

/// P11 write-back into the row list (UIList.SetSelectedIndex; -1 deselects). Game thread only.
bool SetOptionsListIndex(void* screen, int index);

// The engine input commands rows understand (UIUtilities_Input consts). Sending these through
// the row's own OnUnrealCommand is exactly what a sighted keyboard press does — the widget
// updates AND every screen delegate fires.
inline constexpr int kCmdArrowRight = 501;
inline constexpr int kCmdArrowLeft = 503;
inline constexpr int kCmdEnter = 511;
// The popup family's cancel path. Every class in kPopupClasses handles this at SCREEN level
// alongside FXS_BUTTON_B, so it is the keyboard route their own code already offers.
inline constexpr int kCmdEscape = 510;
// The controller accept button. Normally not ours to send — but a few widgets handle ONLY it
// and never the keyboard constants, so it is the only entry their own code offers. The room's
// bottom-bar buttons are the case in point: UIFacility_RoomFunc.OnUnrealCommand tests
// FXS_BUTTON_A and nothing else, which is why they are mouse-or-gamepad in vanilla.
inline constexpr int kCmdButtonA = 300;

/// Feed one input command to a row (UIMechaListItem.OnUnrealCommand with the release action —
/// the phase the game's own handlers act on). Game thread only.
bool SendRowCommand(void* rowItem, int cmd);

/// Step a dropdown row's selection by ±1 through the widget's own open → move → confirm-select
/// path, so the selection-changed delegate fires just as a real interaction. Game thread only.
bool AdjustDropdown(void* rowItem, int delta);

/// The panel's mouse-over tooltip text ("title. body" when both exist), or empty. Resolved
/// through the panel's own Movie → PRES → tooltip manager, so it works on any screen. Game
/// thread only.
std::string ReadPanelTooltip(void* panel);

/// A settings row's tooltip: the BG child panel carries it (falls back to the row panel).
std::string ReadRowTooltip(void* rowItem);

/// A UIListItemString row's tooltip. That class OVERRIDES SetTooltipText to forward to its
/// ButtonBG child, and SetDisabled(bDisabled, Reason) parks the why-disabled text there too, so
/// a panel-keyed read of the row itself always comes back empty. Falls back to the row.
std::string ReadListItemTooltip(void* rowItem);

/// A tooltip registered STRAIGHT on the manager against a Flash path, for elements that have no
/// UIPanel of their own and so carry neither bHasTooltip nor CachedTooltipId — an armory
/// screen's ability-summary strip is the pattern. `childPath` is appended to `panel`'s own
/// MCPath (so it starts with a dot) and matched exactly. Empty when nothing is registered there,
/// which is indistinguishable from an empty tooltip. Game thread only.
std::string ReadTooltipByPath(void* panel, const char* childPath);

// ---- New game flow ----
//
// Main menu CAMPAIGN opens UIShellDifficulty; its start button leads either straight to
// UIShellNarrativeContent (when the tutorial is on — tutorial and Ironman do not mix) or via
// UIChooseIronMan first. Confirming on UIShellNarrativeContent starts the campaign. Every step
// can raise a modal UIDialogueBox over itself (tutorial notice, Ironman warning, XPack
// narrative notice), which is why the dialog is covered here too.

/// The difficulty screen as three surfaces: the four difficulty checkboxes (the game treats
/// them as a radio group), the bottom option rows, and the start button — plus the Second Wave
/// panel, which REPLACES the other two while it is open (the game disables their navigation).
struct DifficultyView {
    std::string title;                   // m_strSelectDifficulty / m_strChangeDifficulty
    std::vector<OptionRow> difficulties; // Rookie/Veteran/Commander/Legend, index 0-3
    std::vector<OptionRow> options;      // Second Wave, tutorial, first-time VO, subtitles,
                                         // soundtrack — visible ones, on-screen order
    MenuItem start;                      // m_StartButton (handle null if unreadable)
    bool secondWaveOpen = false;         // m_bSecondWaveOpen
    std::string secondWaveTitle;         // SecondWavePanelTitle
    std::vector<OptionRow> secondWave;   // m_SecondWaveList rows (only while open)
    int secondWaveSelected = -1;         // m_SecondWaveList.SelectedIndex (-1 = none)
};

/// The UIShellDifficulty instance, or null — same caching/validation contract as
/// FindMainMenuShell. Game thread only.
void* FindDifficultyScreen();

DifficultyView ReadDifficulty(void* screen);

/// A Second Wave option's own tooltip text (SecondWaveToolTips[index]), or empty. The rows
/// carry no panel tooltip — the text lives in a localized array on the screen.
std::string GetSecondWaveTooltip(void* screen, int index);

/// P11 write-back into the Second Wave list (UIList.SetSelectedIndex). Game thread only.
bool SetSecondWaveIndex(void* screen, int index);

/// Toggle a Second Wave option through the screen's own OnSecondWaveOptionClicked — the path
/// a mouse click takes, and the only one that keeps the mutually exclusive faction-HQ options
/// consistent. The rows' checkboxes are bReadOnly, so feeding the row Enter does nothing.
/// Game thread only.
bool ToggleSecondWaveOption(void* screen, int index);

/// The Ironman choice screen: a warning with two large buttons and no other controls. Title
/// and body are localized properties on the screen (Flash only renders them).
struct IronManView {
    std::string title;             // m_strIronmanTitle
    std::string body;              // m_strIronmanBody
    std::vector<MenuItem> buttons; // enable Ironman, then start without it
};

void* FindIronManScreen();
IronManView ReadIronMan(void* screen);

/// The optional-narrative screen: the XPack narrative checkbox (standalone, above the list),
/// the DLC list, and the confirm button that actually starts the campaign.
struct NarrativeView {
    std::string title;             // NarrativeContentTitle
    OptionRow xpack;               // m_XpacknarrativeMechaItem (handle null when absent)
    std::string xpackTooltip;      // its disabled-reason tooltip, when set
    std::vector<OptionRow> rows;   // m_List rows: integrated-DLC option, then one per DLC
    std::vector<std::string> summaries; // parallel to rows: the side-panel description text
    int selectedRow = -1;          // m_List.SelectedIndex (-1 = none)
    MenuItem start;                // m_StartButton
};

void* FindNarrativeScreen();
NarrativeView ReadNarrative(void* screen);

/// P11 write-back into the narrative DLC list (UIList.SetSelectedIndex). Game thread only.
bool SetNarrativeIndex(void* screen, int index);

/// The soundtrack picker, reachable from the difficulty screen's soundtrack row (Tactical
/// Legacy Pack only): a three-way choice list plus a confirm button, structurally the
/// narrative screen without the standalone toggle.
struct SoundtrackView {
    std::vector<OptionRow> rows;
    int selectedRow = -1;
    MenuItem start;
};

void* FindSoundtrackScreen();
SoundtrackView ReadSoundtrack(void* screen);

/// P11 write-back into the soundtrack list (UIList.SetSelectedIndex). Game thread only.
bool SetSoundtrackIndex(void* screen, int index);

/// A modal dialog (UIDialogueBox). The screen is PERMANENT — it exists from movie init and
/// merely holds no data when nothing is showing, so presence is never the readiness test:
/// DialogShowing is.
struct DialogView {
    std::string title;
    std::string body;
    std::string accept; // empty when the dialog offers no accept
    std::string cancel; // empty when the dialog offers no cancel (one-button notice)
};

void* FindDialogScreen();

/// A dialog is up: the queue is non-empty and the box is visible. UIRaiseDialog does not push
/// a screen, so the screen underneath keeps its focus bit — this is the only reliable gate.
bool DialogShowing(void* screen);

DialogView ReadDialog(void* screen);

/// Resolve the dialog through the game's own handlers (which fire the caller's callback and
/// pop the entry). Game thread only.
bool DialogAccept(void* screen);
bool DialogCancel(void* screen);

// ---- save / load (UILoadGame, UISaveGame) ----
//
// Two screens built from one row widget (UISaveLoadGameListItem), and the shell's first case of
// the tactical HUD's problem: the row resolves date, name and mission into LOCALS, pushes them
// into Flash and retains none of them. What it does retain is the OnlineSaveGame struct it was
// built from — the data Flash was told — so native reads the row's STRUCTURE (which rows, which
// is selected, which buttons the row is offering) and X2AccessSaveReader supplies the words.
// See docs/save-load-recon.md §3.

/// One row. `index` is the row's OWN Index field, which is what the screen's SetSelection and
/// OnAccept path talk in — on the save screen it is offset by one from the save list, because
/// row 0 is the "new save" slot.
struct SaveSlot {
    void* handle = nullptr;  // UISaveLoadGameListItem
    void* accept = nullptr;  // AcceptButton: load / save / name-a-new-save, per the row
    void* remove = nullptr;  // DeleteButton — null-handled by visibility, see below
    int index = 0;
    int id = -1;             // save ID; -1 marks the save screen's new-save row
    bool acceptDisabled = false;
    std::string acceptLabel; // the row's own localized verb, read off the widget
    std::string deleteLabel; // empty when the game is not offering the delete verb here
};

struct SaveLoadView {
    bool saving = false;         // UISaveGame rather than UILoadGame
    std::string title;           // m_sSaveTitle / m_sLoadTitle, P10-cleaned
    std::vector<SaveSlot> slots; // list order (newest first; new-save row leads when saving)
    int selected = -1;           // m_iCurrentSelection
    bool busy = false;           // a save or load is in flight — the screen drops all input
};

/// The focused UISaveGame or UILoadGame, or null. Game thread only.
void* FindSaveLoadScreen();

/// Which of the two it is. The screens' shared fields sit at identical offsets, so this is what
/// picks the title and the in-flight flag apart.
bool SaveScreenIsSaving(void* screen);

SaveLoadView ReadSaveLoad(void* screen);

/// P11 write-back through the screen's own SetSelection: it moves the Flash highlight AND sets
/// m_iCurrentSelection, which is what the game's own Delete key acts on. Game thread only.
bool SetSaveLoadSelection(void* screen, int index);

/// Activate a row through its own AcceptButton — the mouse path. The button carries the rename
/// delegate on the new-save row and the accept delegate everywhere else, so this dispatches
/// "name a new save" versus "load / overwrite" without the caller deciding.
///
/// Selects the row first, which on the SAVE screen is load-bearing rather than tidy:
/// UISaveGame.OnAccept ignores the control it is handed and acts on m_iCurrentSelection, so a
/// click sent while the two disagreed would overwrite the wrong save. Game thread only.
bool ActivateSaveSlot(void* screen, const SaveSlot& slot);

/// The row's own DeleteButton, with the same select-then-click guard. Not bound to a key by
/// this layer — the game's own Delete and Backspace already reach OnDelete — but the recipe
/// names the verb, and a later phase may want to offer it. Game thread only.
bool DeleteSaveSlot(void* screen, const SaveSlot& slot);

// ---- text input dialog (UIInputDialogue) ----
//
// Raised to name a new save (and by the error reporter, and MP). The typed text lives in Flash;
// only the prompt and the prefill are retained. ⛔ This is deliberately NOT a graph recipe: a
// recipe attaching turns on the navigator's claims — arrows, Enter, Space, Tab — every one of
// which the player is using to type or to answer. See docs/save-load-recon.md §7.

struct TextInputView {
    std::string prompt;   // m_kData.strTitle
    std::string prefill;  // m_kData.strInputBoxText — what the box starts with
    int maxChars = 0;
    bool multiLine = false; // eDialogType_MultiLine / _Report: Enter inserts a line break
};

/// The focused UIInputDialogue, or null. Game thread only.
void* FindTextInputScreen();

TextInputView ReadTextInput(void* screen);

/// Everything about the Scaleform TextField that currently has keyboard focus — read from the
/// MOVIE, not modelled. VERIFIED LIVE 2026-08-21 on the save-name dialog.
///
/// This is the answer to "the widget tree holds no words" taken one level further: the words are
/// not in UnrealScript either, they are in Flash, and GFxMoviePlayer's path API reaches them.
/// AS2's global `Selection` class resolves through `ActionScriptInt` / `ActionScriptString` with
/// or without the `_global.` prefix, which is what makes the caret and the selection EXACT
/// rather than inferred.
struct TextFieldState {
    bool valid = false;    // false when nothing has AS focus (getFocus answers "null")
    std::string focusPath; // "_level0.theInterfaceMgr.UIInputDialogue_0....textInputBox" — also
                           // says WHICH field, which the crash-report dialog needs (title + body)
    std::wstring text;     // RAW and WIDE: the player typed it, and the caret indexes characters
    int caret = -1;        // Selection.getCaretIndex()
    int selBegin = -1;     // Selection.getBeginIndex() — equal to selEnd when nothing is selected
    int selEnd = -1;

    bool HasSelection() const { return selBegin >= 0 && selEnd > selBegin; }
};

/// `screen` is any UIPanel-derived object on the movie to ask (its `Movie` is what gets called).
///
/// Costs four outbound calls, two of which answer with an engine-allocated FString that is read
/// and deliberately leaked — the same bargain ConsoleCommand makes. Affordable behind a
/// keystroke, NOT on a free-running poll. Game thread only.
TextFieldState ReadFocusedTextField(void* screen);

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

/// Invoke one of the screen's own argument-free handlers by name (OnLaunchMission's button is
/// clicked instead; this is OnStripItems / OnStripGear / OnStripWeapons / OnBuildItems /
/// OnBoostSoldier). Game thread only.
bool InvokeScreenVerb(void* screen, const char* funcName);

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
/// OnUpDownInputHandler, which toggles from whatever state the game happens to be in).
/// `control` may name a control the card lacks; the card focus alone is then the whole write.
bool SetAfterActionFocus(void* screen, int visualIndex, AfterActionControl control);

/// Activate one card control through its own handler: AfterActionPromote() (zero-param),
/// OnClickedMemorialize (its UIButton param is never read), OnClickBondIcon (likewise).
/// Continue is InvokeScreenVerb(screen, "OnContinue"). Game thread only.
bool ActivateAfterActionControl(const AfterActionSlot& slot, AfterActionControl control);

// ---- tactical HUD (UITacticalHUD) — tile reader support ----

/// The tactical HUD screen instance, or null — same stack-first caching/validation contract as
/// the other tracked screens. Game thread only.
void* FindTacticalHUD();

/// Is the shot HUD (ability targeting) raised? While it is, the game's own targeting UI
/// consumes the arrow keys (UITacticalHUD_AbilityContainer), so the tile cursor stands down.
bool TacticalHUDMenuRaised(void* screen);

/// The tile reader's activation gate, computed from the game's own screen stack. In tactical
/// the FOCUS BIT lives on UIReplay — a permanent top-of-stack overlay whose flag dword (0x259)
/// is bit-for-bit identical to a focused pause menu's — so UITacticalHUD is NEVER the focused
/// screen and flags cannot tell a passive overlay from a modal (verified live 2026-08-19:
/// stack was UIReplay / UISpecialMissionHUD / UITurnOverlay / UITacticalHUD / ...). The gate
/// therefore walks the stack top-down over a whitelist of known-passive overlays; any other
/// screen above the HUD blocks, and its class name is reported so the whitelist can grow from
/// facts.
struct TacticalGate {
    bool hudOnStack = false; // UITacticalHUD present, inited, not removed, and NOTHING over it
    bool menuRaised = false; // shot HUD up — targeting owns the arrows
    bool blocked = false;    // a non-passive screen sits above the HUD
    char blocker[64] = {};   // its class name, for the log

    /// Is a mission running AT ALL — a UITacticalHUD anywhere on the stack, however deep? Unlike
    /// hudOnStack this stays true under a pause menu, a dialogue box, or a cinematic reveal, so
    /// it rises once when the mission starts and falls once when it ends. That is what makes it
    /// the right edge for per-mission state; hudOnStack is the right edge for "may we act now",
    /// and the two must not be confused — the frequent transitions all belong to the latter.
    bool missionActive = false;

    bool Active() const { return hudOnStack && !menuRaised && !blocked; }
};

/// Game thread only.
TacticalGate ReadTacticalGate();

// ---- the ability bar (UITacticalHUD_AbilityContainer + UITacticalHUD_Ability) ----
//
// The one tactical panel besides objectives that IS readable from widgets (engine-notes: the
// bar "needs NO companion call"). Each icon retains the strings the game last pushed:
// m_strAntennaText (the ability's localized friendly name — refreshed only while
// Movie.IsMouseActive(), which the mouse gate deliberately preserves), m_strHotkeyLabel,
// m_strCooldown / m_strCharge (localized prefixes baked in by the game), and IsAvailable.
//
// ⛔ The live icon count is the container's own ActiveAbilities, never "how many widgets hold
// text": ClearData() clears availability, cooldown and charge but NOT the antenna text, so a
// soldier switch leaves the previous soldier's ability names sitting on the trailing icons.
// ActiveAbilities is written by the same PopulateFlash pass that feeds the icons, and the
// widgets [0, ActiveAbilities) map 1:1 onto m_arrAbilities — commander abilities are compacted
// out into the mouse-controls panel by that same pass.
struct AbilityBarEntry {
    void* handle = nullptr;  // UITacticalHUD_Ability — opaque
    std::string name;        // m_strAntennaText, cleaned; may be empty (see the view note)
    std::string hotkey;      // m_strHotkeyLabel ("1".."0"); empty on unbound slots
    std::string cooldown;    // m_strCooldown — "" unless cooling down
    std::string charges;     // m_strCharge — "" when the ability has no charge count
    bool available = false;  // IsAvailable (AvailableCode == 'AA_Success')
};

struct AbilityBarView {
    bool valid = false;              // the container was found and read
    int selected = -1;               // m_iCurrentIndex — -1 while the shot HUD is down
    std::vector<AbilityBarEntry> entries; // the bar's live icons, [0, ActiveAbilities)
};

/// Read the bar off a UITacticalHUD screen. Game thread only.
AbilityBarView ReadAbilityBar(void* screen);

/// Activate bar slot `index` through the container's own AbilityClicked — the mouse path, with
/// every side effect a click has: the input-gate check (PreProcessCheckGameLogic refuses when
/// it is not the player's moment), the tutorial's disabled-trigger check, the select sound, and
/// SelectAbility raising the targeting UI. Returns the game's own answer. Game thread only.
bool ActivateBarAbility(void* screen, int index);

// ---- the command buttons (UITacticalHUD_MouseControls) ----
//
// The bottom-right strip — end turn, prev/next soldier, camera, evac/mission commands, Chosen
// info — and on mouse/keyboard that strip IS the HUD's command surface (the controller hotlink
// buttons are never spawned; see the sighted-parity hard rule). Unlike the rest of the tactical
// HUD it RETAINS its words: SetButtonItem assigns each button's localized Label, the player's
// actual bound-key string and its UIState into ButtonItems before pushing to Flash.
//
// ⛔ The VISIBLE set is not retained (SetNumActiveControls goes straight to Flash), so the read
// reconstructs it from the same three inputs UpdateControls used: buttons 0-4 always, slot 5
// only when CommandAbilities is non-empty, slot 6 only while a Chosen is activated
// (GetActivatedChosen — the game's own gate, asked fresh). Reading ButtonItems[0..N] raw would
// resurrect stale labels: an unused slot keeps whatever was last written to it, forever.
struct CommandButtonEntry {
    int index = -1;      // the game's own button index — what m_iCurrentSelection must be set to
    std::string label;   // ButtonItems[i].Label, cleaned; may be empty (caller supplies fallback)
    std::string key;     // ButtonItems[i].key — the player's current binding, already localized
    bool enabled = false; // UIState == eUIState_Normal, the exact test OnAccept applies
};

struct MouseControlsView {
    bool valid = false;
    // The panel's localized "no key bound" string. When a button's key slot holds THIS, the
    // game is saying the action is unbound — it is a sentence of its own, not a key name, so a
    // caller must not phrase it as one ("key KEY NOT BOUND").
    std::string noKeyBound;
    std::vector<CommandButtonEntry> buttons;
};

/// Read the world-mode button strip off a UITacticalHUD screen. Only meaningful while the shot
/// HUD is DOWN — while it is raised the strip shows Cancel Shot and slot 0's retained label is
/// that, not End Turn (callers gate on menuRaised). Game thread only.
MouseControlsView ReadMouseControls(void* screen);

/// Activate button `index` exactly the way the game's own mouse click does: write
/// m_iCurrentSelection FIRST, then call OnAccept() — the screen-accept-trap rule, and OnAccept
/// is also where a disabled button gets the game's own negative sound. Returns false only when
/// the call could not be made at all. Game thread only.
bool ActivateMouseControl(void* screen, int index);

// ---- the objectives list (UIObjectiveList) ----
//
// The panel that draws "ЗАДАЧИ / OBJECTIVES" and the mission's objective lines under it. Unlike
// every other tactical panel, this one RETAINS its words: UIObjectiveListItem.RefreshDisplay
// ends each of its Set*Row branches with `Data = NewData`, so the ObjectiveDisplayInfo struct the
// row was built from is still there to read. That is what lets native answer this without a
// companion call, and it is why the readout survives the TUTORIAL — the objective list is driven
// by UIObjectiveList.OnVisualizationBlockComplete, which a replay fires exactly as a live mission
// does (see the read-it-from-the-screen hard rule).
//
// ONE class serves both layers: UITacticalHUD.m_kObjectivesControl in a mission and
// UIAvengerHUD.Objectives on the ship. Only the owning screen and its offset differ.

/// One row of the list. The flags are the game's own, so `completed` here means exactly what the
/// ticked checkbox means on screen.
struct ObjectiveLine {
    std::string label;       // Data.DisplayLabel, cleaned. Empty on a title row.
    std::string missionType; // Data.MissionType — "TITLE"/"DARKEVENTTITLE" mark the headings
    bool title = false;      // the synthesized heading row, which carries no label of its own
    bool completed = false;
    bool failed = false;
    bool checkbox = false;
    bool warning = false;
    bool header = false;    // a clickable group header
    bool darkEvent = false;
    int timer = -1;         // Data.Timer — turns remaining; -1 when this row has no timer

    // Counter row (evidence recovered, civilians rescued, ...). All -1 when this is not one; the
    // images are the game's asset tokens, which is where the NOUN comes from — see the reader.
    int haveAmount = -1, haveMin = -1, availableAmount = -1, lostAmount = -1;
    std::string haveImage, availableImage, lostImage;

    /// Does this row carry anything worth speaking? A title row's words live on the class, not in
    /// Data, and an unlabelled non-counter row is a spacer.
    bool Speakable() const { return !label.empty() || haveAmount >= 0; }
};

struct ObjectiveListView {
    bool found = false;    // a UIObjectiveList was located and walked
    bool tactical = false; // ...off UITacticalHUD (false = UIAvengerHUD)
    std::string heading;     // UIObjectiveListItem.m_strObjectivesListTitle ("ЗАДАЧИ")
    std::string darkHeading; // ::m_strDarkEventsListTitle ("АКТИВНЫЕ СОБЫТИЯ")
    std::string inProgress;  // ::m_strInProgress ("- В ПРОЦЕССЕ")
    std::vector<ObjectiveLine> lines;
};

/// Cheap presence test for a key claim's gate: is an objectives list reachable right now? Walks
/// the screen stack and reads one pointer — no string reads, nothing allocated. Game thread only.
bool HasObjectiveList();

// ---- the mission stage counters (UISpecialMissionHUD_TurnCounter) ----
//
// The panel in the top-right corner that reads e.g. "Приведите солдат на запасной маршрут /
// Осталось солдат / 3". It answers a question the objectives list CANNOT: the list says what the
// goals are, this says how far through the current one you are. On a mission whose stage is a
// zone the squad must cross, the only other indicator is a line drawn on the ground — which
// carries no text and no state object, so this counter is the sole readable form of it.
//
// It retains everything: SetLabel/SetSubLabel/SetCounter each end by assigning m_sLabel /
// m_sSubLabel / m_sCounter whether or not the panel was inited, so the strings are authoritative.
//
// ⛔ `bIsVisible` is load-bearing here and must be honoured. Every AS_Set* calls Show() — Firaxis'
// own comment is *"don't show panel unless data is pumped into it"* — so an unfed counter is
// hidden, and the second of the two is routinely inited-but-hidden. Reading a hidden one would
// invent a counter the player cannot see.
struct MissionCounter {
    std::string label;    // m_sLabel    — what the stage is
    std::string subLabel; // m_sSubLabel — what the number counts
    std::string counter;  // m_sCounter  — the number, as the screen shows it
    bool expired = false; // m_bExpired
    bool infinity = false; // m_bInfinity — shows ∞ instead of a number
    bool locked = false;  // m_bLocked — control-point missions only
};

/// The stage counters this mission is showing, in screen order. Empty on a mission with none,
/// which is most of them. Game thread only.
std::vector<MissionCounter> ReadMissionCounters();

/// Read whichever objectives list is currently on the screen stack — tactical first, then the
/// Avenger. Game thread only. `found` is false when neither HUD is up, which is the honest
/// answer for the shell and for a loading screen.
ObjectiveListView ReadObjectives();

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

// ---- popups (the modal notification family across all three layers) ----
//
// One finder for every screen in docs/popup-inventory.md §1-3 that is a MODAL with a message and
// a couple of buttons. They share the only two things a reader needs: the screen is the topmost
// stack entry, and its own OnUnrealCommand answers FXS_KEY_ENTER / FXS_KEY_ESCAPE at SCREEN
// level — so activation needs no per-class button offsets, and this whole family costs ZERO
// hand-transcribed offsets and zero static_asserts. Words come from X2AccessPopupReader, except
// UIAlert's, which come from Flash (see ReadAlertText).

struct PopupView {
    void* screen = nullptr;
    std::string className; // the concrete class, so the companion can branch on it
};

/// The topmost stack screen, when it is one of the known popup classes and is inited, not
/// removed and focused. Anything above a popup has taken the keys, so a popup that is not
/// topmost is deliberately refused. Game thread only.
PopupView FindPopupScreen();

/// Is `className` in the popup table? Exposed so the recipe can keep its probe and its build in
/// agreement without re-listing the classes.
bool IsPopupClass(const char* className);

/// Hand a SCREEN one of its own input commands. Identical to SendRowCommand — a UIScreen's
/// OnUnrealCommand has the same signature as a row's — but named separately because the intent
/// differs: here it is the screen's own accept/cancel path, which is the ONLY route several of
/// these popups offer (UIAlert's buttons carry no retained label and no navigable focus).
/// Game thread only.
bool SendScreenCommand(void* screen, int cmd);

// ---- the mission blade family (UIMission + subclasses) — docs/mission-blade-recon.md ----
//
// The geoscape's commit-to-a-mission screen: one base skeleton, 14 shipped subclasses plus two
// DLC ones. Words come from X2AccessMissionReader (the screen's own getters); activation goes
// through the reader's ActivateMissionButton (UIButton.Click(), which GOps' mission selectors
// and the locked blades' Back button need — the popup family's screen-level keys cannot reach
// them). Native's whole contribution is the finder and ONE offset.

/// The topmost stack screen when it derives from UIMission (IsKindOf, so DLC and mod blades
/// match) and is live. A blade under another screen has lost the keys — the popup finder's
/// refusal rule. Game thread only.
void* FindMissionBladeScreen();

/// UIMission::MissionRef.ObjectID, or 0 on any fault. The recipe's wire-cache discriminator:
/// the GOps blade rewrites MissionRef IN PLACE when a selector button switches the displayed
/// mission (no widget respawn), so the screen pointer alone would serve stale words.
int32_t MissionBladeMissionId(void* screen);

// ---- UIAlert: the 118-variant strategy alert, read back out of Flash ----
//
// ⛔ This is the ONE place in the project that reads the speech text out of the movie, and it is
// a deliberate, bounded exception to "verification only, never the speech path". The reason is
// that UIAlert has 106 separate Build*Alert functions, none of which retain a single string:
// each resolves its own state object, picks from 389 localized vars, and pushes the result
// through LibraryPanel.MC.BeginFunctionOp("UpdateData"). There is no accessor, no shared data
// path, and re-deriving all 106 in the companion would be thousands of lines that silently drift
// from the game's own source.
//
// What makes the exception affordable is that a popup is read ONCE, on attach (plus a Delete
// re-announce) — never per arrow press, which is the cost the "use sparingly" warning is about.
// And it is bounded rather than a blind sweep: the panel's Flash library symbol is known
// (UIAlert.GetLibraryID), and each symbol's exact ordered field list was recovered by
// decompiling the shipped movie, so a known alert costs one read per field it actually has.
// An UNKNOWN symbol (the WOTC XPACK panels, whose package ships LZO-encrypted) falls back to the
// union of every field name the base movie uses, and logs the symbol so the gap is enumerable
// from a live run rather than silent.

struct AlertView {
    void* screen = nullptr;
    std::string alertName;             // eAlertName, e.g. "eAlert_ResearchComplete"
    std::string symbol;                // Flash library symbol, e.g. "Alert_Complete"
    std::vector<std::string> lines;    // message lines, in the order UpdateData wrote them
    std::vector<std::string> buttons;  // Button0 then Button1, empty entries dropped
    bool knownSymbol = false;          // false => the union fallback ran
};

/// Read an alert's text out of its Flash panel. `mcPath` is LibraryPanel.MCPath and `symbol` is
/// GetLibraryID(), both supplied by the companion (which can reach them in one script call).
/// Results are cached per symbol+path+alertName, so a re-announce costs nothing. Game thread only.
AlertView ReadAlertText(
    void* screen, const std::string& mcPath, const std::string& symbol, const std::string& alertName);

/// Drop the alert text cache. Called when the popup layer detaches, so a second alert of the
/// same kind with different words is never answered from the first one's reading.
void ResetAlertTextCache();

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
/// UpdateSelection ever writes. Game thread only.
bool ActivateChooseFacility(const ChooseFacilityView& view, int index);

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

// ---- tutorial mission (Tutorial_01) — the text surfaces and the replay state ----
//
// The tutorial is a REPLAY: XComTutorialMgr extends XComReplayMgr, plays a recorded session back
// and halts at every frame that needed player input. Two consequences shape everything here.
// First, the WHOLE script — every popup, every required move — already sits in the history at
// mission start, so anything reading the history must gate on CurrentHistoryFrame or it speaks
// the future. Second, the player is on rails: a submission that does not match the recording is
// rejected with a sound and NO TEXT, which is the accessibility failure this reader exists to
// close.
//
// Four surfaces carry the tutorial's words; three of them are readable:
//   modal popup      UITutorialBox            — real script properties, read below
//   anchored blade   UIWorldMessageMgr        — retained in m_arrMsgs, read below
//   HUD blade        Flash only               — presence below, words from the companion
//   3D world markers XComTutorialMgr meshes   — the object scanner's, not this reader's

/// The tutorial's modal popup. Pushed over the tactical HUD with InputState eInputState_Consume,
/// and — unlike every other tactical panel, which pipes its text straight into Flash — it keeps
/// its text in ordinary script properties, so reading it is a plain offset read.
struct TutorialBoxView {
    std::string title;
    std::string body;
    std::string buttonHelp; // the confirm label; empty means the game shows its generic one
};

/// The focused UITutorialBox, or null. Game thread only.
void* FindTutorialBox();

TutorialBoxView ReadTutorialBox(void* screen);

/// One entry of UIWorldMessageMgr.m_arrMsgs that the manager currently lists as ACTIVE.
struct WorldMessage {
    int slot = -1;          // index into m_arrMsgs — the manager's own identity for the entry
    std::string id;         // m_sId; empty on pooled anonymous messages
    std::string text;       // m_txtMsg, RAW: <img> markup is the caller's to translate
    int type = 0;           // EHUDMessageType: 0 World, 1 Damage, 2 Blade
    bool visible = false;   // m_bVisible — on SCREEN this frame; an off-camera message reads
                            // false while still being a message the game chose to publish
    bool important = false; // m_bIsImportantMessage: named, so never recycled into the pool
    int followObjectId = 0; // m_kFollowObject.ObjectID; 0 when anchored to a bare location
};

/// The live world-message manager (XComPresentationLayerBase.m_kWorldMessageManager), reached
/// through the tactical HUD's own Movie -> PRES chain rather than a GObjects scan, so a manager
/// left over from a previous mission can never win. Null outside tactical. Game thread only.
void* FindWorldMessageMgr();

/// Everything the manager currently lists as active, in its own order.
///
/// This is the only readable form of the game's flyover channel. UIWorldMessageMgr.Message is
/// script->script and so never reaches our ProcessEvent hook — but the manager RETAINS every
/// message it is showing, which makes POLLING work where hooking cannot.
std::vector<WorldMessage> ReadWorldMessages(void* mgr);

/// Is the unanchored bottom-right tutorial blade up (BladeHUDMessage != none)? Its text went
/// straight into Flash through MC.FunctionString and is retained nowhere, so presence is all
/// this can answer; the words come from the companion's history walk.
bool HudBladeShowing(void* mgr);

/// The replay/tutorial manager's state. `valid` is false outside the tutorial mission, which is
/// also how "are we in the tutorial at all" gets answered.
struct TutorialState {
    bool valid = false;
    bool inTutorial = false;       // bInTutorial
    bool allowingControls = false; // bIsTutorialAllowingControls — false while the replay is
                                   // playing back, i.e. while the player owns nothing
    int historyFrame = 0;          // CurrentHistoryFrame — gate every history read on this
    int nextInputFrame = 0;        // NextPlayerInputFrame
    int invalidSubmissions = 0;    // InvalidSubmissionCnt: the on-rails rejections, which the
                                   // game reports with a sound and no text at all
    void* nextAbilityContext = nullptr; // XComGameStateContext_Ability*, opaque — the recorded
                                        // action the player must reproduce. Exposed for the
                                        // object scanner, which owns describing it; nothing in
                                        // the message reader dereferences it.
};

/// Game thread only.
TutorialState ReadTutorialState();

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

// ---- outbound calls into companion-mod script ----
//
// Declared in script_call.h (included at the top), because they are not a UI interface: the six
// TUs that want FindCompanionCDO / CallScriptFunction / FindScriptFunction / FindStateFunction
// and nothing else here should not be reading this file to find them. The implementation is
// still in game_ui.cpp, next to the reflection primitives it is built on.

// ---- dev harness only: raw object access for the /gui and /props dumps ----
//
// These are the four primitives the widget-tree dumper cannot get any other way; everything
// else it needs it reads through UE3 reflection, so it hand-transcribes no offsets of its own.
#ifdef X2ACCESS_DEVSERVER

/// The screen stack's entry for `className`, or the TOPMOST usable screen when `className` is
/// null or empty. Same inited/not-removed filter the recipes use. Game thread only.
void* FindStackScreen(const char* className);

/// Every live instance of `className` in GObjects, in scan order, up to `maxItems`; returns the
/// count written. No usability filter, so it reaches objects the screen stack never sees
/// (XCom3DCursor, a cheat manager, a tooltip manager). Matching is by CLASS POINTER with class
/// default objects excluded by name — deliberately not by the object's own name, which the
/// recipes' finders can rely on only because screens happen to keep the default naming.
/// ALL of them are returned rather than one, because with several alive the dumper has no basis
/// for picking and the caller deserves to see the ambiguity. Game thread only.
int FindInstances(const char* className, void** out, int maxItems);

/// The CLASS DEFAULT OBJECT of `className` (`Default__<className>`), or null when the class is
/// not loaded. FindInstances deliberately skips CDOs so one cannot shadow a live screen, but for
/// the companion classes the CDO is the only instance there ever is — X2AccessObjectScanner,
/// X2AccessTileReader and X2AccessNarrator keep their entire model on it — so without this the
/// dumper cannot reach the objects this mod's own subsystems actually use. Game thread only.
void* FindDefaultObject(const char* className);

/// Is this pointer a live entry in GObjects? The dumpers take a `?ptr=` from a previous dump,
/// and a pointer that has since been collected must be refused rather than walked.
bool IsLiveObject(void* obj);

/// UIPanel::ChildPanels into `out`; returns the count written.
int ReadChildPanels(void* panel, void** out, int maxItems);

/// One call into GFxMoviePlayer's path API, for the harness's /flash endpoint.
///
/// Every one of those functions has the SAME parameter shape — an FString path in at 0x00, the
/// answer at 0x10 — so the only thing that varies is the return KIND, and it is decided by the
/// name rather than guessed at. That distinction matters: `GetVariable` answers with an FASValue
/// struct and `Invoke` takes an ARRAY of them, so reading either through this block would print
/// a plausible-looking number that is really half a pointer. Unknown names are refused.
///
/// ⚠ An unresolved path and an empty field answer ALIKE here (empty string, 0, false) — the
/// distinction the first live probe used was that the real `Selection` accessors answer -1 when
/// nothing is focused while an unresolved path answers 0. Ask for something you can predict.
struct FlashValue {
    enum class Kind { Void, Text, Number, Int, Bool };
    bool called = false;  // the UFunction was found and ProcessEvent ran
    std::string error;    // why not, when it did not
    Kind kind = Kind::Text;
    std::string text;  // Kind::Text
    double number = 0; // Kind::Number / Int / Bool (1 or 0)
};

/// `movie` is the UIMovie / GFxMoviePlayer to ask; `path` is an ActionScript path in it.
/// Costs one outbound call, and for a string answer leaks the engine-allocated FString it comes
/// back in — the same bargain ConsoleCommand makes, and the reason this is dev-only.
/// Game thread only.
FlashValue FlashCall(void* movie, const char* funcName, const char* path);

/// Run `command` through the game's own console (`Actor.ConsoleCommand` on the local player
/// controller) and return what it printed — empty for the many commands that say nothing, or a
/// `[exec] …` string when the call could not be made at all. This is the whole `exec function`
/// surface (1246 of them across the SDK, the cheat managers included) reachable by name, which
/// is why the dev harness needs no scripting bridge of its own.
///
/// The reply comes back in an engine-allocated FString that we read and deliberately leak; see
/// the implementation. Game thread only.
std::string ConsoleCommand(const std::string& command);

#endif // X2ACCESS_DEVSERVER

} // namespace GameUI
