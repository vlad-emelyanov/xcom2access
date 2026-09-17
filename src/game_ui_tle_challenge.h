#pragma once

// Game-UI access, the Tactical Legacy Pack's CHALLENGE ARCHIVES branch: the offline challenge
// menu (UITLE_ChallengeModeMenu) the hub's third card opens — the screen recipe_tle_challenge.cpp
// reads. Implemented in game_ui_tle_challenge.cpp. Recon: docs/tle-challenge-recon.md.
//
// One screen is the whole branch. OnChallengeClicked reads the challenge's history file and runs
// its map command straight away — no squad select, no leaderboard, no difficulty page in between
// (UIChallengeMode_SquadSelect is the ONLINE daily-challenge path, spawned only by
// XComChallengeModeManager; UIChallengeLeaderboards is pushed only from that screen and from the
// post-mission UIChallengePostScreen). The one thing pushed over this menu before a mission is
// the LOADING CHALLENGES progress dialog while its `LoadingItems` state reads 107 history files,
// and that is UIProgressDialogue — the popup recipe's.

#include "game_ui_core.h"

namespace GameUI {

/// One challenge row (UITLEChallenge_ListItem). The row RETAINS its words: `Data`, the
/// ChallengeListItemData struct the screen built from the challenge's history file, is the exact
/// payload PopulateData hands Flash — so unlike most rows in this game, these are read off the
/// widget and no companion re-derivation is needed.
struct ChallengeRow {
    void* handle = nullptr;  // UITLEChallenge_ListItem
    int index = 0;           // the list's own item index — what OnChallengeClicked reads back
    int dataIndex = -1;      // Data.ChallengeListDataIndex: the challenge's file index, stable
                             // across sorts (the list re-Refreshes rows in place, so `index`
                             // and `handle` are NOT stable identities across a sort — this is)
    std::string mission;     // Data.MissionName — the operation name
    std::string objective;   // Data.MissionObjective — the mission template's display name
    std::string map;         // Data.MapName — the localized plot-type name
    std::string squad;       // Data.Squad — soldier class selector, alien selector, or both
    std::string enemies;     // Data.Enemies — the enemy-forces selector's display name
};

inline constexpr int kChallengeSortColumns = 5;

struct ChallengeMenuView {
    void* screen = nullptr;
    void* list = nullptr;              // UITLE_ChallengeModeMenu::List
    std::string title;                 // m_ScreenTitle
    // m_strButtonLabels, in EChallengeModeMenuSortType order — the words the five sort headers
    // draw over the columns (mission, objective, map, squad, enemies).
    std::string columns[kChallengeSortColumns];
    int sortType = 0;                  // m_eSortType (EChallengeModeMenuSortType)
    bool sortFlipped = false;          // m_bFlipSort — the arrow the active header draws
    std::vector<ChallengeRow> rows;    // list order, which is the current SORT order
    int selected = -1;                 // List.SelectedIndex
};

/// The focused UITLE_ChallengeModeMenu, or null. Game thread only.
void* FindChallengeMenuScreen();

/// Unbounded read of the rows: the archive ships 107 challenges, past kMaxListRows.
ChallengeMenuView ReadChallengeMenu(void* screen);

/// P11 write-back: UIList.SetSelectedIndex, whose OnSelectionChanged the screen wires to
/// OnSelectedChange — so this also repaints the details panel exactly as an arrow press does.
/// Game thread only.
bool SetChallengeMenuIndex(void* screen, int index);

/// Start the challenge on row `index`: select it, then the screen's own OnChallengeClicked(List,
/// index) — the exact call its Enter branch makes. Reads the history file, bootstraps the
/// challenge manager and runs the map command; nothing comes back. Game thread only.
bool ActivateChallengeRow(void* screen, int index);

} // namespace GameUI
