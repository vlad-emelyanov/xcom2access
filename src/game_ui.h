#pragma once

// Game-UI access for screen recipes (spec P1 read, P2 invoke, P11 focus write-back).
//
// XComGame/Engine fields are read through EXPLICIT offsets from the generated SDK's
// reflection-dump comments (validated against a live process) — never through the generated
// class layouts, whose pack(0x8) compilation drifts from the engine's real layout. This
// interface stays SDK-free. Handles are opaque game object pointers: valid to USE only
// immediately after the read that produced them, on the game thread — never stored across
// frames (store them in ControlId::Reference only as comparison tokens, per the kernel's rule).

#include <string>
#include <vector>

namespace GameUI {

struct MenuItem {
    void* handle = nullptr; // AUIButton* — opaque
    std::string label;      // P10-cleaned display text
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

/// Minimal P10 text cleaning: strip markup tags, collapse whitespace.
std::string CleanText(const std::string& text);

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
};

/// One sub-control of a soldier card. A utility ROW expands into one entry per slot, because
/// each is separately focusable (the game's own Left/Right walks them) and separately locked.
struct SquadControl {
    void* handle = nullptr;   // the UISquadSelect_UtilityItem for a utility slot; else the card
    SquadButton button = SquadButton::WeaponPrime;
    int utilityIndex = -1;    // >= 0 only for Utility: index within UtilitySlots
    int utilitySlotType = -1; // that slot's EInventorySlot, or -1
    bool disabled = false;    // greyed: announce it, do not skip it (recon §7)
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
bool InvokeSquadSelectVerb(void* screen, const char* funcName);

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
    bool hudOnStack = false; // UITacticalHUD present, inited, not removed
    bool menuRaised = false; // shot HUD up — targeting owns the arrows
    bool blocked = false;    // a non-passive screen sits above the HUD
    char blocker[64] = {};   // its class name, for the log

    bool Active() const { return hudOnStack && !menuRaised && !blocked; }
};

/// Game thread only.
TacticalGate ReadTacticalGate();

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
    char blocker[64] = {};    // what sits on top instead, when it is not; for the log

    bool Active() const { return current; }
};

/// Game thread only.
AvengerGate ReadAvengerGate();

// ---- inside a room (UIFacility and its 22 subclasses) ----
//
// ⛔ The room SCREEN is not the room's content. All seven FIXED rooms declare ZERO staff slots —
// CommandersQuarters, CIC, PowerCore (Research), Hangar (Armory), LivingQuarters, BarMemorial
// and Storage (Engineering) — so their UIFacility screens hold nothing navigable at all, and a
// reader built around the staff strip would be silent on every room a new player can reach.
// (Measured live 2026-08-21: the Research lab is 3 panels, staff container hidden, and all
// twelve buildable slots on that Avenger were still unexcavated.)
//
// The content is one screen DOWN, in UIAvengerHUD.Shortcuts — the submenu the game auto-opens
// for the room you just entered, and where the Lab's "New Research / View Scientists" and the
// Armory's "Soldiers / Recruit / Memorial" actually live. That is not a layering accident to
// route around; it is where the game itself puts a room's verbs, and its own input path agrees
// (UIFacility.OnUnrealCommand returns false, UIScreenStack.OnInput walks LIFO, the keys land on
// the shortcut list). So this view spans both screens deliberately.
//
// Every field is a RETAINED widget read — no XCOMHISTORY, no Flash, nothing to go stale.

struct RoomAction {          // one row of the room's shortcut submenu
    void* handle = nullptr;  // UIListItemString
    int index = 0;           // the GAME's list index (padding rows are skipped, not renumbered)
    std::string label;       // Text, P10-cleaned — already localized
    bool disabled = false;   // bDisabled: the game draws it greyed and refuses the click
    bool urgent = false;     // bIsBad
    bool warning = false;    // bIsWarning
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
    std::vector<RoomAction> actions;
    int selected = -1;         // List::SelectedIndex
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

// ---- outbound calls into companion-mod script ----

/// The class default object of a script class, found by its "Default__<ClassName>" object
/// name. Cached; revalidated by re-reading the object's name each call; a full GObjects scan
/// runs at most once a second while the object is genuinely missing. Game thread only.
void* FindCompanionCDO(const char* className);

/// Resolve `funcName` on the object's class chain (cached per class) and ProcessEvent it.
/// `params` must match the UFunction's parameter layout. Game thread only.
bool CallScriptFunction(void* obj, const char* funcName, void* params);

/// The UFunction object for `className.funcName`, resolved through the class object's field
/// chain (supers included) — for callers that need the function OBJECT rather than a call
/// (the mouse gate rewrites its dispatch fields). Cached; a missing class is retried at most
/// once a second. Returns null until resolvable. Game thread only.
void* FindScriptFunction(const char* className, const char* funcName);

/// The UFunction object for a function declared inside a `state`, which lives one level deeper
/// than FindScriptFunction looks: the state is itself a UStruct on the class's field chain, and
/// its functions hang off that. Needed because the same function name can be declared once per
/// state (XComTacticalInput declares Mouse_CheckForFreeAim in both targeting states), so the
/// state must be named to pick one. Cached per triple; returns null until resolvable.
/// Game thread only.
void* FindStateFunction(const char* className, const char* stateName, const char* funcName);

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

#endif // X2ACCESS_DEVSERVER

/// Run `command` through the game's own console (`Actor.ConsoleCommand` on the local player
/// controller) and return what it printed — empty for the many commands that say nothing, or a
/// `[exec] …` string when the call could not be made at all. This is the whole `exec function`
/// surface (1246 of them across the SDK, the cheat managers included) reachable by name, which
/// is why the dev harness needs no scripting bridge of its own.
///
/// The reply comes back in an engine-allocated FString that we read and deliberately leak; see
/// the implementation. Game thread only.
std::string ConsoleCommand(const std::string& command);

} // namespace GameUI
