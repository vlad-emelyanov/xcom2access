#pragma once

// Game-UI access, the TACTICAL LEGACY PACK family (shell half): the Legacy Operations menu, its
// difficulty page and the soldier loadout popup — the screens recipe_tle.cpp reads. The hub they
// hang off (UITLEHub) is read by game_ui_shell.cpp. Implemented in game_ui_tle.cpp.
//
// docs/surfaces/shell.md ("Tactical Legacy Pack") holds the rules; docs/tle-recon.md the recon.

#include "game_ui_core.h"

namespace GameUI {

// ---- the Legacy Operations menu (UITLE_LadderModeMenu) ----
//
// Two lists stacked in one left column under two headers — CENTRAL'S ARCHIVES (four narrative
// ladders, `UITLENarrativeLadder_ListItem` rows) and RESISTANCE ARCHIVES (`UIListItemString`
// rows: "[+] Generate New Resistance Operation" first, then one per procedural ladder file) — and
// a details panel on the right that `UpdateData` rebuilds from a ladder HISTORY FILE on every
// selection change: lock state, objective, mission progress, squad, medal thresholds, high score,
// difficulty, the narrative paragraph, and CONTINUE / BEGIN, ABANDON, DELETE buttons.
//
// What retains what: the narrative row keeps its `XComGameState_LadderProgress` (the name is
// `NarrativeLadderNames[LadderIndex]` on it) and its `bDisabled`; the procedural row keeps its
// Text; the screen keeps `ClickedList` + `clickedIndex` (the panel's subject) and
// `m_LadderSaveData` (which ladders have an in-progress save); the panel's words live in the
// HISTORY the screen loaded for it — X2AccessTLEReader.DescribeLadderMenu re-reads those state
// objects, never the file. ⛔ Vanilla keyboard: Enter works (the screen's own handler), but
// ABANDON and DELETE are mouse-only Flash buttons or gamepad X / R3, and the six soldier-info
// buttons are mouse-only; all are re-provided through the screen's own functions.

struct LadderRow {
    void* handle = nullptr; // the row panel
    int index = 0;          // index within its own list
    bool disabled = false;  // narrative rows: bDisabled (locked behind the previous ladder)
    std::string label;      // procedural rows only: the retained Text; narrative rows get theirs
                            // from the companion
};

struct LadderMenuView {
    void* screen = nullptr;
    std::string title;           // m_ScreenTitle
    std::string narrativeHeader; // m_NarrativeLaddersHeader
    std::string randomHeader;    // m_RandomLadderHeader
    void* narrativeList = nullptr; // NarrativeList
    std::vector<LadderRow> narrative;
    void* randomList = nullptr;    // List
    std::vector<LadderRow> random;
    void* clickedList = nullptr;   // ClickedList — which list the panel describes
    int clickedIndex = -1;         // clickedIndex — which row of it
    // The verbs, in the screen's own words.
    std::string beginLabel;    // m_strBeginLadder
    std::string continueLabel; // m_strContinueLadder
    std::string abandonLabel;  // m_strAbandonLadder
    std::string deleteLabel;   // m_strDeleteLadder
    std::string medalsLabel;   // m_Medals — "MEDALS:"
    // The six soldier-info buttons, VISIBLE ones only (UpdateData shows one per squad member and
    // hides the rest); index is the squad position.
    std::vector<std::pair<int, void*>> soldierButtons;
};

/// The focused UITLE_LadderModeMenu, or null. Game thread only.
void* FindLadderMenuScreen();

LadderMenuView ReadLadderMenu(void* screen);

/// Focus write-back onto a row: the row's own SetSelectedNavigation (which walks the game's
/// navigator chain up through the list and the left column), the other list deselected, then the
/// screen's OnSelectedChange — the call its own arrow handler makes after moving, and the one
/// that rebuilds the details panel. Game thread only.
bool SelectLadderRow(const LadderMenuView& view, bool random, int index);

/// Enter on a row: the screen's own OnLadderClicked — loads the in-progress save, or pushes the
/// difficulty page. A disabled narrative row is refused by the game itself. Game thread only.
bool ActivateLadderRow(const LadderMenuView& view, bool random, int index);

/// The two destructive verbs, each raising the game's own confirm dialog. Game thread only.
bool LadderAbandon(void* screen);
bool LadderDelete(void* screen);

/// Open the soldier loadout popup for a squad slot — OnClickSoldierInfo with the button and the
/// mouse-up command it tests for. Game thread only.
bool OpenLadderSoldierInfo(void* screen, void* button);

// ---- the ladder difficulty page (UITLE_LadderDifficulty) ----
//
// UIShellDifficulty's shape in the Legacy Pack's skin: four difficulty checkboxes on a top panel
// (a radio group), the narrative-VO checkbox and a read-only Ironman line on a bottom panel, and
// a LAUNCH button. Its title is UIShellDifficulty's own m_strSelectDifficulty pushed straight into
// Flash, and the description under the list is m_arrDifficultyDescStrings[selected] — both come
// from the companion (DescribeLadderDifficulty), which reads the same class defaults.

struct LadderDifficultyView {
    std::vector<OptionRow> difficulties; // Rookie/Story, Veteran, Commander/Nightmare, Legend
    std::vector<OptionRow> options;      // narrative VO (narrative ladders only), Ironman
    MenuItem start;                      // m_StartButton
    int selectedDifficulty = -1;         // m_iSelectedDifficulty
};

/// The focused UITLE_LadderDifficulty, or null. Game thread only.
void* FindLadderDifficultyScreen();

LadderDifficultyView ReadLadderDifficulty(void* screen);

// ---- the soldier loadout popup (UITLEUnitStats) ----
//
// One soldier at a time: header (name, rank, class), stats, gear, PCS, abilities — every word
// pushed into Flash from the unit state the screen retains in m_UnitArray[m_CurrentIndex]. Left
// and Right are the game's own Prev/Next soldier, and it CONSUMES ALL INPUT, so those arrive as
// an adjust on the soldier node rather than as claims the game would fight.

struct UnitStatsView {
    void* screen = nullptr;
    std::string title; // m_ScreenTitle
    int index = -1;    // m_CurrentIndex
    int count = 0;     // m_UnitArray.Length
};

/// The focused UITLEUnitStats, or null. Game thread only.
void* FindUnitStatsScreen();

UnitStatsView ReadUnitStats(void* screen);

/// The screen's own NextSoldier / PrevSoldier. Game thread only.
bool UnitStatsStep(void* screen, int sign);

} // namespace GameUI
