#pragma once

// Game-UI access, the MOD-PROVIDED SCREENS family: screens that no shipped package declares and
// that a Workshop mod pushes onto the game's own stack. Mod Config Menu (Workshop 667104300) is
// the first; LWOTC's screens will join it. Implemented in game_ui_mods.cpp.
//
// ⛔ What makes this family different from every other reader: the classes are NOT in the
// generated SDK, so there is no dump comment to transcribe an offset from and no `static_assert`
// to pin one with. Nothing here reads a mod class by offset. Every field on a mod class is either
// found by WALKING the widget tree from the screen (ChildPanels, matched by class name) or read
// BY NAME through UE3Reflect::ReadStringProperty, which resolves the property's offset at runtime
// off the class's own reflection data. The engine-class offsets it does use (UIPanel, UIList,
// UIButton, UIText, UIMechaListItem) are the ones game_ui_internal.h already carries, pinned in
// sdk_layout_checks.cpp like everyone else's.

#include "game_ui_core.h"

namespace GameUI {

// ---- Mod Config Menu (MCM_OptionsScreen) — docs/mod-config-menu-recon.md ----
//
// Reached from the settings screen through the MOD SETTINGS button MCM injects there (a plain
// UIButton the Options reader offers through its ChildPanels scan). The screen is a tab list on
// the left (one MCM_SettingsTab checkbox row per client mod; checked == that page is showing), a
// settings page on the right (LAZY: nothing is built until a tab is clicked), and two buttons at
// the bottom. Escape / Cancel DISCARDS every change on every page; Save and Exit commits them.

/// One entry of the page in on-screen order: a group heading (the mod's own separator widget,
/// which is a context and not a control) or a setting row.
struct ModConfigItem {
    bool heading = false;     // MCM_GroupLabelSeparator — its UIText child's words. Emitted for
                              // EVERY separator, words or none: it marks the group boundary.
    std::string text;         // the heading's words (heading entries only; may be empty)
    OptionRow row;            // the setting (row entries only); index is the list index
    std::string sliderValue;  // Slider rows: the row's own formatted value text ("3", "0.50"),
                              // which is what a sighted player reads — the bar's percent is not.
                              // Empty when the display is hidden (a slider made non-editable
                              // hides it AND the bar), and then there is no value to speak: the
                              // percent is drawn nowhere.
};

struct ModConfigView {
    void* screen = nullptr;
    void* tabsList = nullptr;      // TabsList (UIList 'ModTabSelectList')
    void* settingsList = nullptr;  // the current page's SettingsList, when a page is shown
    std::string title;             // m_strTitle, by name, cleaned ("Mod Settings")
    std::string subtitle;          // m_strSubtitle, by name, cleaned
    std::vector<OptionRow> tabs;   // checkbox rows: label is the mod's tab name, checked ==
                                   // this page is the one showing
    int selectedTab = -1;          // TabsList.SelectedIndex
    bool pageShown = false;        // a MCM_SettingsPanel child of the container is visible
    std::string pageTitle;         // the page's title line (the mod's name; "Mod Settings")
    std::vector<ModConfigItem> items; // headings and rows, top to bottom
    int selectedRow = -1;          // SettingsList.SelectedIndex
    std::vector<MenuItem> buttons; // Save and Exit, Cancel, then the page's Reset when shown
};

/// The focused MCM_OptionsScreen on the screen stack, or null. Stack-only on purpose: the mod
/// always pushes it (there is no other way to open it), and the object-scan fallback would cost a
/// GObjects pass every second on every install WITHOUT the mod, looking for a class that does not
/// exist. Usability is bIsInited && bIsFocused — never bIsVisible (the hard rule). Game thread only.
void* FindModConfigScreen();

ModConfigView ReadModConfig(void* screen);

/// The two screen-level words alone, for naming the screen without walking its tree.
void ReadModConfigHeading(void* screen, std::string* title, std::string* subtitle);

/// A setting row's tooltip: what the game's tooltip manager holds for the row (ReadRowTooltip),
/// else the text the row RETAINED in its own `DisplayTooltip`, read by name. MCM hands the
/// tooltip to the widget only in AfterParentPageDisplayed, which runs from the panel's Show(),
/// and a page's FIRST display goes through CreateUi without one — so on a freshly opened page
/// the manager is empty while the row already knows its words (live 2026-09-17: Space on Evac
/// All's second row said nothing though the mod ships a tooltip for it). Empty when the mod gave
/// the row none. Game thread only.
std::string ReadModConfigRowTooltip(void* row);

/// P11 write-back into the tab list (UIList.SetSelectedIndex; -1 deselects). Game thread only.
bool SetModConfigTabIndex(void* screen, int index);

/// P11 write-back into the current page's settings list. False when no page is shown.
/// Game thread only.
bool SetModConfigRowIndex(void* screen, int index);

} // namespace GameUI
