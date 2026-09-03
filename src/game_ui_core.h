#pragma once

// Game-UI access, the GENERIC half: the row shapes every family shares, the screen-stack state,
// focus/click/sound plumbing, the input-command constants and the tooltip readers. Per-screen
// views live in the family headers (game_ui_shell/strategy/popup/avenger/tactical.h);
// game_ui.h is the umbrella that includes them all.
//
// XComGame/Engine fields are read through EXPLICIT offsets from the generated SDK's
// reflection-dump comments (validated against a live process) — never through the generated
// class layouts, whose pack(0x8) compilation drifts from the engine's real layout. This
// interface stays SDK-free. Handles are opaque game object pointers: valid to USE only
// immediately after the read that produced them, on the game thread — never stored across
// frames (store them in ControlId::Reference only as comparison tokens, per the kernel's rule).

#include "script_call.h"

#include <cstdint>
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

// ---- UIMechaListItem rows (shared: settings AND the new-game flow screens) ----
//
// A "mecha list item" is the game's one-line settings widget — a label plus at most one of
// checkbox / spinner / dropdown / slider / button. Whole screens are built out of them,
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

/// P11 write-back: drive the game's own navigation onto this button
/// (UIPanel.SetSelectedNavigation) — brings the game's highlight and hover sound with it.
bool FocusButton(void* button);

/// P2: run the game's own click handler (UIButton.Click).
bool ClickButton(void* button);

/// Play the game's own menu-scroll cue ("Play_Mouseover") on the current world's sound
/// manager. The native cue lives only in UINavigator's key handler — a path our claimed keys
/// bypass — so kernel-driven moves must plumb it explicitly. Game thread only.
bool PlayNavSound();

// CleanText is declared in script_call.h, included above — the recipes call it on widget
// text just as the bridge's consumers call it on payloads.

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

/// Hand a SCREEN one of its own input commands. Identical to SendRowCommand — a UIScreen's
/// OnUnrealCommand has the same signature as a row's — but named separately because the intent
/// differs: it is the screen's own accept/cancel path, which is the ONLY route several of the
/// popups offer (UIAlert's buttons carry no retained label and no navigable focus).
/// Game thread only.
bool SendScreenCommand(void* screen, int cmd);

/// Step a dropdown row's selection by ±1 through the widget's own open → move → confirm-select
/// path, so the selection-changed delegate fires just as a real interaction. Game thread only.
bool AdjustDropdown(void* rowItem, int delta);

/// Invoke one of a screen's own argument-free handlers by name (squad select's strip verbs,
/// after action's OnContinue). Game thread only.
bool InvokeScreenVerb(void* screen, const char* funcName);

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

/// A panel's tooltip when it is a CALLBACK tooltip — `SetTooltipWIthCallback` and friends
/// register a placeholder ("Init") plus a `del_OnMouseIn` delegate, and the real words are
/// computed only when the mouse arrives: the Avenger shortcut buttons resolve the player's own
/// key binding that way (`RefreshTooltipResearch` → `FindAbilityKey(TooltipResearch,
/// "%KEY:TWO%", …)`), and ability icons do the same. A cold ReadPanelTooltip on one of these
/// speaks the placeholder. This runs the game's own hover sequence without the showing — select
/// the element on the shared UITextTooltip (MatchesID), fire the element's delegate, read back —
/// and degrades to ReadPanelTooltip when the panel's tooltip has no callback. One outbound call
/// per delegate: game thread only, and from a key handler rather than a poll.
std::string ReadCallbackTooltip(void* panel);

} // namespace GameUI
