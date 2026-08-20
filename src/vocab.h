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
