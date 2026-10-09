#pragma once

// Game-UI access, the My2K screen: UIFiraxisLiveLogin, which recipe_my2k.cpp reads. Implemented
// in game_ui_my2k.cpp. Rules: docs/surfaces/shell.md, "My2K legal documents and account login".
//
// ONE SCREEN, MANY STATES. The class is the whole Firaxis Live / My2K flow — legal documents,
// account linking, connection notices — switched by UnrealScript `state`, never by a new screen.
// The state that matters most is ReadEULA: on a first launch the 2K client signs in with the
// platform account, finds documents that account has not accepted, and XComOnlineEventMgr pushes
// this screen over the main menu to ask (FiraxisLiveClient_OnLegalNotify).
//
// ⛔ VANILLA IS A KEYBOARD DEAD END THERE. InitScreen turns the mouse on (Movie.ActivateMouse), and
// ReadEULA.BeginState, seeing the mouse active, pushes XComShellInput into BlockingInput — whose
// PreProcessCheckGameLogic answers false for EVERY command, so no key reaches any screen. The
// state's CloseScreen is empty too, so nothing but its two buttons ever closes it. The arbiter
// takes our keys before the engine, so the recipe works under the block; the game's own keyboard
// path does not exist to be preferred.
//
// EVERYTHING IT SHOWS IS RETAINED on the screen, unlike the house style: the title and message it
// pushes to Flash (m_2KTitle, m_2kMessage), the buttons' labels (plain UIButtons), one
// UIMechaListItem per document ("Read: <title>", in m_eulaButtons), and the documents themselves
// (m_EULAs — title, UID, full text). An opened document is drawn into m_eulaText, whose UIText
// keeps the HTML it was handed.

#include "game_ui_core.h"

namespace GameUI {

/// One document row in the list ("Read: Terms of Service").
struct My2KDocumentRow {
    void* handle = nullptr; // UIMechaListItem, from m_eulaButtons
    int index = 0;          // its slot in m_eulaButtons — which is also its slot in m_EULAs
    std::string label;      // the row's own Desc text, as drawn
    bool focused = false;   // the row's own focus bit: ReadEulas opens the FOCUSED row's document
};

struct My2KView {
    void* screen = nullptr;
    std::string title;   // m_2KTitle — 2K's own heading for the current state (read in both modes:
                         // it names the screen, even though an open document hides it)
    std::vector<std::string> messageLines; // m_2kMessage, split into its lines (list mode only)

    // ReadEULA's document list. Empty in every other state.
    std::vector<My2KDocumentRow> documents;

    // A document is open (m_eulaText is showing). The list, the title, the message and the buttons
    // are then hidden — ReadEulas hides the rows and HideMy2kPanel takes the panel down — so a
    // sighted player sees the document and its OK, and the view says only that.
    bool documentOpen = false;
    std::string documentTitle;              // m_eulaTitle's text
    std::vector<std::string> documentLines; // m_eulaText's text, one entry per line or paragraph
    std::string closeLabel;                 // m_InitOKLabel — the OK the nav help draws

    // The panel's buttons that are showing, in the order the game's own Navigator lists them
    // (username, password, generic 1, generic 4, generic 2). In ReadEULA that is the
    // decline-all and accept-all pair.
    std::vector<MenuItem> buttons;
};

/// The focused UIFiraxisLiveLogin, or null. Game thread only.
void* FindMy2KScreen();

My2KView ReadMy2K(void* screen);

/// Open the document behind row `index`: focus that row and no other, then the row's own Click(),
/// whose delegate is the screen's ReadEulas. ⛔ Focus first, not for tidiness: ReadEulas ignores
/// which row was clicked and opens the document of whichever row has bIsFocused. Game thread only.
bool OpenMy2KDocument(void* screen, int index);

/// Close the open document and go back to the list: the screen's Escape, whose handler calls the
/// state's AgreeToEULA while the document is showing (the game's name for "OK", not an
/// acceptance). Sent to the screen directly, so the BlockingInput state does not stand in the way.
/// Game thread only.
bool CloseMy2KDocument(void* screen);

} // namespace GameUI
