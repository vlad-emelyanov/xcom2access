#pragma once

// Game-UI access, the SHELL family: the main menu, the settings screen, the new-game flow, the
// pause menu, the modal dialog, save/load and the text-input dialog — the screens
// recipe_shell.cpp and text_input.cpp read. Implemented in game_ui_shell.cpp.

#include "game_ui_core.h"

namespace GameUI {

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

/// The shell's MAINMENU buttons: visible ones, in declaration order. Empty while the menu is
/// still building.
std::vector<MenuItem> ReadMainMenu(void* shell);

/// The shell's My2K button and news ticker (see ShellExtras). The account status is refreshed
/// through the game's own FiraxisLive query functions at most once a second — the same cadence
/// the game's own status timer uses. Game thread only (makes outbound calls).
ShellExtras ReadShellExtras(void* shell);

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

// ---- the character pool (UICharacterPool) and its pool files (UICharacterPool_ListPools)
//      — docs/customize-recon.md §3 ----
//
// The main menu's CHARACTER POOL: the pool's soldiers as CHECKBOX rows (UpdateDataCheckbox — the
// name retained on the row, the box the selection for export / delete), six buttons the mouse
// build spawns (create, import, export, delete, select all, deselect all — each greyed with its
// own reason as a tooltip), a one-row options list holding the pool-usage DROPDOWN, and a
// UIX2PanelHeader that retains its title and subtitle. ⛔ Vanilla's keyboard here is a dead end:
// Enter on a row toggles its box and nothing else, and EDIT is FXS_BUTTON_X — a gamepad button —
// so the reason the screen exists is unreachable without a mouse. EditSoldier is re-provided.
//
// The pool-files screen is the import / export picker: the same header saying which of its three
// modes it is in (a file to export into; a file to import from; a character out of that file),
// mecha rows retaining the names (Value rows, or Button rows carrying a DELETE button), and
// ⛔ another dead end — OnAccept picks only under `ISCONTROLLERACTIVE`, and the rows consume Enter
// (a value row Clicks nothing; a button row presses its DELETE, which looks up a list index the
// button does not have) before the list's OnItemClicked can fire. The activation calls
// OnClickLocal with the list and the index, which is exactly the controller branch.

struct CharacterPoolView {
    void* screen = nullptr;
    void* list = nullptr;        // UICharacterPool::List
    std::string title;           // TitleHeader.Title
    std::string subtitle;        // TitleHeader.Label
    std::vector<OptionRow> rows; // checkbox rows: label is the name, checked is the selection
    int selected = -1;           // List.SelectedIndex
    std::vector<MenuItem> buttons; // the visible ones, in on-screen order
    std::string editLabel;       // m_strEditCharacter — the verb vanilla puts on a gamepad button
    std::string emptyLabel;      // m_strNoCharacters — the game's word for an empty pool
    OptionRow usage;             // the pool-usage dropdown row; handle null when absent
};

/// The focused UICharacterPool, or null. Game thread only.
void* FindCharacterPoolScreen();

CharacterPoolView ReadCharacterPool(void* screen);

/// P11 write-back. Game thread only.
bool SetCharacterPoolSelection(const CharacterPoolView& view, int index);

/// Toggle a row's box: select it, then the screen's own SimulateMouseClickOnCheckbox — the
/// FXS_BUTTON_A branch minus its bIsVisible gate. Game thread only.
bool ToggleCharacterPoolRow(const CharacterPoolView& view, int index);

/// Open the customization menu for a row: select it, then the screen's own EditSoldier.
/// Game thread only.
bool EditCharacterPoolRow(const CharacterPoolView& view, int index);

struct PoolFilesView {
    void* screen = nullptr;
    void* list = nullptr;        // UICharacterPool_ListPools::List
    std::string title;           // TitleHeader.Title
    std::string subtitle;        // TitleHeader.Label — the export / import caption, or the file
    std::vector<OptionRow> rows; // Value rows, or Button rows whose value is the DELETE label
    int selected = -1;
    bool exporting = false;      // bIsExporting
    bool inFile = false;         // bHasSelectedImportLocation — picking a character, not a file
};

/// The focused UICharacterPool_ListPools, or null. Game thread only.
void* FindPoolFilesScreen();

PoolFilesView ReadPoolFiles(void* screen);

/// P11 write-back. Game thread only.
bool SetPoolFilesSelection(const PoolFilesView& view, int index);

/// Pick a row: select, then the screen's own OnClickLocal(List, index). Game thread only.
bool ActivatePoolFilesRow(const PoolFilesView& view, int index);

/// Delete the row's pool: select, then the screen's FXS_BUTTON_Y — its own confirm dialog
/// follows. Offered by the recipe only on rows the game draws a DELETE button on. Game thread only.
bool DeletePoolFilesRow(const PoolFilesView& view, int index);

} // namespace GameUI
