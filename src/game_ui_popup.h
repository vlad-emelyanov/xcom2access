#pragma once

// Game-UI access, the POPUP family: the modal-notification classes across all three layers, the
// mission blade, and UIAlert's Flash read-back. The recipes that consume these live in
// recipe_strategy.cpp. Implemented in game_ui_popup.cpp.

#include "game_ui_core.h"

namespace GameUI {

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

/// Is this screen one of the popup family — one of the listed classes, or a mod's subclass of
/// one? Takes the SCREEN, not its name: a ModClassOverrides subclass has a name we have never
/// heard of and can only be recognised by its class chain. Exposed so the recipe can keep its
/// probe and its build in agreement without re-listing the classes.
bool IsPopupScreen(void* screen);

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

// ---- the dark events board (UIAdventOperations) ----
//
// The words come from X2AccessPopupReader's DescribeAdventOps; the keys are the screen's own
// (Y flips pending and active, B or Enter leaves). The one thing no key reaches per card is
// revealing a SPECIFIC hidden event: the controller's X reveals the first affordable one and the
// mouse click names a card, so the click's handler is called directly.

/// UIAdventOperations.OnRevealClicked(index) — what a mouse click on a locked card runs. A no-op
/// for a card that is open or unaffordable (the handler's own checks). Game thread only.
bool RevealDarkEvent(void* screen, int index);

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

} // namespace GameUI
