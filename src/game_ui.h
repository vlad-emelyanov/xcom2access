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
    std::string title;           // m_chooseSoundtrack
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

// ---- outbound calls into companion-mod script ----

/// The class default object of a script class, found by its "Default__<ClassName>" object
/// name. Cached; revalidated by re-reading the object's name each call; a full GObjects scan
/// runs at most once a second while the object is genuinely missing. Game thread only.
void* FindCompanionCDO(const char* className);

/// Resolve `funcName` on the object's class chain (cached per class) and ProcessEvent it.
/// `params` must match the UFunction's parameter layout. Game thread only.
bool CallScriptFunction(void* obj, const char* funcName, void* params);

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
