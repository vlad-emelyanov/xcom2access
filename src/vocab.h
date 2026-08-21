#pragma once

// The vocabulary module (spec A8): every string the LAYER itself speaks routes through here —
// hardcoded English at call sites is forbidden, because it is unfindable later. Game content
// (button labels, log lines) is already localized and passes through untouched. When P7
// localization lands, this module grows a string table behind the same names.

#include <string>

namespace Vocab {

inline constexpr const char* ScreenMainMenu = "Main menu";
inline constexpr const char* ScreenOptionsFallback = "Settings";
inline constexpr const char* RoleButton = "button";
inline constexpr const char* RoleTicker = "news ticker";
inline constexpr const char* RoleTab = "tab";
inline constexpr const char* RoleCheckbox = "checkbox";
inline constexpr const char* RoleSlider = "slider";
inline constexpr const char* RoleSpinner = "spinner";
inline constexpr const char* RoleDropdown = "dropdown";
inline constexpr const char* Selected = "selected";
inline constexpr const char* Disabled = "unavailable";

// Checkbox states — the same words the legacy UnrealScript narration used, so the switch to
// the native path doesn't change what the player hears.
inline constexpr const char* Checked = "checked";
inline constexpr const char* Unchecked = "unchecked";

// The game's "!" badge (UIButton.bNeedsAttention) — what a sighted player reads as
// "something new here" (a new challenge, unplayed Legacy content).
inline constexpr const char* Attention = "attention";

// My2K account button (bottom right of the shell) and its link states — same three states
// the game's own status text distinguishes.
inline constexpr const char* My2KAccount = "2K account";
inline constexpr const char* My2KLinked = "linked";
inline constexpr const char* My2KNotLinked = "not linked";
inline constexpr const char* My2KOffline = "offline";

// ---- new game flow ----

// Screen names for the campaign-setup screens. The difficulty and narrative screens carry
// their own localized titles and only fall back to these; the Ironman screen and the modal
// dialog have no title property of their own worth trusting, so these name them outright.
inline constexpr const char* ScreenDifficultyFallback = "New campaign";
inline constexpr const char* ScreenIronManFallback = "Ironman";
inline constexpr const char* ScreenNarrativeFallback = "Optional narrative content";
inline constexpr const char* ScreenSoundtrackFallback = "Soundtrack";
inline constexpr const char* ScreenDialog = "Dialog";

// The in-game Escape menu. It carries its own localized title and only falls back to this.
inline constexpr const char* ScreenPauseMenuFallback = "Pause menu";

// Squad select. The screen is named by its own operation name ("Операция ...") and reaches these
// only in the window between attaching and the companion answering — or, for the two below, when
// a card is present that the companion could not describe at all.
inline constexpr const char* ScreenSquadSelectFallback = "Squad select";
inline constexpr const char* SoldierFallback = "Soldier";
inline constexpr const char* UtilitySlotFallback = "Utility slot";

// Save / load. Both screens carry their own localized title and reach these only if it reads
// empty. The other three are the layer's own words for states the screen shows without text:
// an empty list, a save or load in flight, and the key that reaches the row's delete verb —
// the one affordance the graph does not carry as a node (docs/save-load-recon.md §2).
//
// BACKSPACE, not Delete, even though the screen's RawInputHandler takes either: everywhere
// else in this mod Delete means RE-ANNOUNCE (tile cursor, shot HUD, hacking pad, ant farm), so
// naming it here would teach the one exception to a rule the player has learned everywhere
// else — and get a save deleted by someone reaching for a re-read.
inline constexpr const char* ScreenSaveFallback = "Save game";
inline constexpr const char* ScreenLoadFallback = "Load game";
inline constexpr const char* SaveLoadEmpty = "No saved games";
inline constexpr const char* SaveLoadBusy = "Working, please wait";
inline constexpr const char* KeyBackspace = "Backspace";

// Inside a room on the Avenger. The room's menu is the game's own shortcut submenu, which sits
// on the HUD screen BELOW the room — so "nothing here" is a real and legitimate state (Living
// Quarters and the Bar are not in the shortcut bar at all), not a failure to find the list.
inline constexpr const char* ScreenRoomFallback = "Room";
inline constexpr const char* RoomNoActions = "Nothing to do here";
inline constexpr const char* RoomLeaveHint = "Escape to leave the room";
// The crew line the staff container carries even when it is hidden: "SCIENTISTS: 0". Both
// halves come off the widget already localized; this is only the joint between them.
inline constexpr const char* RoomCrewSeparator = ": ";
// bIsBad / bIsWarning on a submenu row — the game colours these red and amber, which is a fact
// a sighted player gets for free and speech otherwise loses entirely.
inline constexpr const char* RoomUrgent = "urgent";
inline constexpr const char* RoomWarning = "attention";

// The text-entry dialog (naming a save). The field is a Flash TextField, so no screen reader
// sees the keystrokes and this layer has to echo them itself — hence the boundary words and the
// punctuation table below.
inline constexpr const char* TextInputRole = "edit box";
inline constexpr const char* TextInputEmpty = "empty";
inline constexpr const char* TextInputStart = "start";
inline constexpr const char* TextInputEnd = "end";
inline constexpr const char* TextInputDeleted = "deleted";

// Character echo. A LETTER or a digit is spoken as itself and the reader pronounces it; a lone
// punctuation mark is not — most screen readers drop it entirely at their default punctuation
// level, so an echo that just forwarded the character would go silent on exactly the keys a
// player is least sure they pressed. Null means "speak the character itself".
inline const char* PunctuationName(wchar_t ch)
{
    switch (ch) {
    case L' ': return "space";
    case L'!': return "exclamation";
    case L'"': return "quote";
    case L'#': return "hash";
    case L'$': return "dollar";
    case L'%': return "percent";
    case L'&': return "ampersand";
    case L'\'': return "apostrophe";
    case L'(': return "left paren";
    case L')': return "right paren";
    case L'*': return "star";
    case L'+': return "plus";
    case L',': return "comma";
    case L'-': return "dash";
    case L'.': return "dot";
    case L'/': return "slash";
    case L':': return "colon";
    case L';': return "semicolon";
    case L'<': return "less than";
    case L'=': return "equals";
    case L'>': return "greater than";
    case L'?': return "question";
    case L'@': return "at";
    case L'[': return "left bracket";
    case L'\\': return "backslash";
    case L']': return "right bracket";
    case L'^': return "caret";
    case L'_': return "underscore";
    case L'`': return "backtick";
    case L'{': return "left brace";
    case L'|': return "bar";
    case L'}': return "right brace";
    case L'~': return "tilde";
    default: return nullptr;
    }
}

// Spoken while the game shows its loading overlay and the layer has nothing to read. A world
// change can take ten seconds or more, and unbroken silence there reads as a crash.
inline constexpr const char* Loading = "Loading";

// The two panels of the difficulty screen. The game draws them as separate blocks without
// headings; naming them is what makes Tab between them meaningful.
inline constexpr const char* GroupDifficulty = "Difficulty";
inline constexpr const char* GroupCampaignOptions = "Campaign options";

// ---- tutorial mission (Tutorial_01) ----

// The glyphs the game splices into tutorial text as <img src='...'> markup. Speech has to
// TRANSLATE these, never strip them: the icon sits exactly where the key name belongs, so
// dropping it leaves "Press , to go here" — a hole in the one sentence that matters.
//
// Only the mouse and arrow glyphs need naming. Everything the tutorial writes as %KEY:...%
// already resolves to a bracketed key name ("[Q]", "[TAB]") through UIUtilities_Input.HTML_KEYMAP
// and reads correctly as-is — and because that substitution is binding-aware, what we speak is
// the player's ACTUAL bound key.
inline constexpr const char* IconLeftMouse = "left mouse button";
inline constexpr const char* IconRightMouse = "right mouse button";
inline constexpr const char* IconMouseWheelClick = "mouse wheel click";
inline constexpr const char* IconMouseWheelScroll = "mouse wheel scroll";
inline constexpr const char* IconMouseButton4 = "mouse button 4";
inline constexpr const char* IconMouseButton5 = "mouse button 5";
inline constexpr const char* IconArrowLeft = "left arrow";
inline constexpr const char* IconArrowRight = "right arrow";
inline constexpr const char* IconArrowUp = "up arrow";
inline constexpr const char* IconArrowDown = "down arrow";

// How to leave the tutorial's modal popup. Without this a blind player is told nothing about
// dismissing a screen that has taken over the game: the box is modal and consumes all input, its
// one button calls DisableNavigation() so keyboard focus can never reach it, and the layer
// deliberately leaves Enter/Space/Escape unclaimed so the game's own handler still gets them.
inline constexpr const char* TutorialDismiss = "Press Enter to close.";

// The tutorial refused the submission. The game's own answer is a negative sound and NO TEXT at
// all, so this line is the entire explanation a blind player gets for why nothing happened.
inline constexpr const char* TutorialRejected = "Not what the tutorial is waiting for.";

// The re-read key found nothing to re-read.
inline constexpr const char* TutorialNoMessage = "No tutorial message.";

// The companion mod is not loaded, so the HUD blade's words cannot be recovered. Said only on a
// pressed key — never from the poll, which must not talk about its own failures.
inline constexpr const char* TutorialReaderUnavailable = "Tutorial reader unavailable.";

inline std::string Position(int index, int count)
{
    return std::to_string(index) + " of " + std::to_string(count);
}

inline std::string Percent(int value)
{
    return std::to_string(value) + " percent";
}

// Fallback tab name for when the screen's localized string reads empty (should not happen on a
// live screen; guards against a half-constructed read).
inline std::string TabFallback(int index)
{
    return "Tab " + std::to_string(index + 1);
}

} // namespace Vocab
