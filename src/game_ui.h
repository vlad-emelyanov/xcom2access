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

/// The UIFinalShell (main menu) instance, or null. Caches the found instance and re-validates
/// it cheaply on every call; a full GObjects scan runs at most once a second. Game thread only.
void* FindMainMenuShell();

/// Screen is inited, visible, and the focused screen (nothing stacked on top of it). Works for
/// any UIPanel-derived object (the flag dword lives at the UIPanel level).
bool IsScreenFocused(void* screen);

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

// ---- Settings screen (UIOptionsPCScreen) ----

/// The widget a settings row carries (UIMechaListItem's EUILineItemType, folded to what the
/// recipe distinguishes: SpinnerAndButton reads as Spinner, ColorChip as Value).
enum class OptionWidget { Description, Spinner, Dropdown, Slider, Checkbox, Button, Value };

struct OptionRow {
    void* handle = nullptr; // AUIMechaListItem* — opaque
    int index = 0;          // row index in m_arrMechaItems (List.SetSelectedIndex target)
    OptionWidget widget = OptionWidget::Description;
    std::string label;      // P10-cleaned Desc text
    std::string value;      // spinner value / dropdown selection / value text / button label
    int percent = 0;        // slider position 0-100 (Slider rows only)
    bool checked = false;   // checkbox state (Checkbox rows only)
    bool disabled = false;
};

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

} // namespace GameUI
