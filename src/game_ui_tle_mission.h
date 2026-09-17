#pragma once

// Game-UI access, the Tactical Legacy Pack's MISSION-SIDE family: every screen the pack pushes
// over UITacticalHUD during or after a ladder or challenge mission — the two scoring dialogs, the
// between-mission upgrade picker and squad review, the medal, medal-progress and end screens, the
// challenge result screen and the challenge event notice — plus the two score HUDs, which are not
// screens a player acts on and get no recipe (docs/tle-mission-recon.md). Implemented in
// game_ui_tle_mission.cpp; the recipes are src/recipes/recipe_tle_mission.cpp; the words the
// widgets do not retain come from X2AccessLadderReader.uc.
//
// ⛔ EVERY SCREEN HERE PUSHES ITS WORDS INTO FLASH AND KEEPS THE LOCALIZED LABELS ALONE. The
// numbers (scores, thresholds, the squad's changes, the two upgrade offers) are read by each
// screen's OnInit out of the history and the challenge manager and retained nowhere, so native
// reads the labels and the buttons off the instance and the companion re-runs OnInit's own
// arithmetic over the same state objects — the save-reader split, once per screen.
//
// ⛔ THE SCORE HUDS ARE ON THE STACK FOR THE WHOLE MISSION. UIChallengeModeHUD (and its subclass
// UILadderModeHUD) is pushed ABOVE UITacticalHUD with InputState eInputState_None, which makes it
// a non-passive entry to the tactical gate — unlisted in kTacticalPassives it reads as `blocked`
// and takes the whole tactical layer silent for every ladder and challenge mission. Both class
// names belong in that list (game_ui_tactical.cpp); this family reads nothing off the HUD itself,
// because it retains nothing.

#include "game_ui_core.h"

namespace GameUI {

// ---- the scoring dialogs (UILadderModeScoringDialog, UIChallengeModeScoringDialog) ----
//
// The modal the game raises before the first turn of every ladder mission and every challenge:
// "MISSION SCORING" / "CHALLENGE SCORING", one row per scoring category with its maximum points
// (the ladder dialog adds the two penalty rows as negatives), the total, two description lines and
// a large Continue button whose label is START MISSION / START CHALLENGE. Two sibling classes
// with the same shape and DIFFERENT field offsets (the challenge dialog declares m_Header first
// and shifts everything after it by one string), so the view carries which one it is.

struct ScoringDialogView {
    void* screen = nullptr;
    bool ladder = false;        // UILadderModeScoringDialog rather than the challenge one
    std::string header;         // m_LadderHeader / m_Header
    std::string totalLabel;     // m_TotalLabel
    std::string description1;   // m_ScoreDescription1
    std::string description2;   // m_ScoreDescription2
    MenuItem continueButton;    // m_ContinueButton — a UILargeButton retaining its label
};

/// The focused scoring dialog of either class, or null. Game thread only.
void* FindScoringDialog();

ScoringDialogView ReadScoringDialog(void* screen);

// ---- the ladder upgrade picker (UILadderUpgradeScreen) ----
//
// Between two ladder missions: two cards side by side, each an upgrade offer — title, a paragraph,
// up to six "<class> will gain: <item>" lines — with an EQUIP UPGRADE button under each. The
// game's own Left/Right is SelectColumn(0/1) and Enter chooses the selected column; the two
// UIButtons carry the CHOOSE UPGRADE label and their click delegates are the two choices.

struct LadderUpgradeView {
    void* screen = nullptr;
    int selectedColumn = 0;       // SelectedColumn — the game's own highlight
    std::string chooseLabel;      // m_strChooseUpgrade (also the screen's subtitle)
    std::string equipLabel;       // m_strEquipUpgrade — the button's word
    MenuItem buttons[2];          // Upgrade0Button, Upgrade1Button
};

/// The focused UILadderUpgradeScreen, or null. Game thread only.
void* FindLadderUpgradeScreen();

LadderUpgradeView ReadLadderUpgrade(void* screen);

/// The game's own column highlight (SelectColumn) — focus write-back. Game thread only.
bool SelectLadderUpgradeColumn(void* screen, int column);

// ---- the ladder squad review (UILadderSoldierInfo) ----
//
// After each ladder mission: three soldiers per page, each with a header (RANK UP / NEW SOLDIER /
// KIA), portrait, class, rank and name, then the abilities and items that are NEW since the last
// mission. Left/Right page (the game's own arrows call the Prev/Next button delegates), Enter is
// NEXT MISSION, and each soldier carries an info button that opens UITLEUnitStats.

struct LadderSoldierInfoView {
    void* screen = nullptr;
    int startIndex = 0;           // m_SoldierStartIndex — first soldier on this page
    int numSoldiers = 0;          // m_NumSoldiers
    MenuItem prevButton;          // PrevButton — hidden on the first page
    MenuItem nextButton;          // NextButton — hidden on the last page
    void* infoButtons[3] = {};    // LadderSoldierInfoButtons — visible per shown soldier
    std::string nextMission;      // m_NextMission
    std::string missionLabel;     // m_MissionLabel
    std::string missionComplete;  // m_MissionComplete
    std::string newAbilities;     // m_NewAbilities — the abilities column's heading
    std::string newItems;         // m_NewItems — the items column's heading
    std::string helpPrevPage;     // m_strHelpPrevPage
    std::string helpNextPage;     // m_strHelpNextPage
    std::string helpSoldierInfo;  // m_strHelpSoldierInfo
};

/// The focused UILadderSoldierInfo, or null. Game thread only.
void* FindLadderSoldierInfoScreen();

LadderSoldierInfoView ReadLadderSoldierInfo(void* screen);

/// Open the stats sheet for the soldier at page slot `slot` (0-2) — the screen's own
/// DisplaySoldierInfo, the info button's delegate. Game thread only.
bool OpenLadderSoldierStats(void* screen, int slot);

// ---- the three one-button ladder screens ----
//
// UITLELadderMedalScreen (the medal the finished ladder earned, plus what it unlocked),
// UITLELadderMedalProgressScreen (between missions: how far along the ladder and towards each
// medal), UITLELadderEndScreen (the run's six statistics). All three are read-only with a single
// Continue, which is the nav help's own button; Enter and Space reach ContinueButtonClicked.

struct LadderMedalView {
    void* screen = nullptr;
    std::string rewardsLabel;       // m_RewardsLabel
    std::string scoreLabel;         // m_ScoreLabel
    std::string medalScoreLabel;    // m_MedalScoreLabel
    std::string medalUnlocked;      // m_MedalUnlockedString — REPLACED IN PLACE at OnInit with
                                    // the medal's name, so the instance holds the filled line
    std::string victory;            // m_VictoryString
    std::string unlockedNextLadder; // m_UnlockedNextLadder
    std::string unlockDescription;  // m_LadderUnlockDescription
};

void* FindLadderMedalScreen();
LadderMedalView ReadLadderMedal(void* screen);

void* FindLadderMedalProgressScreen();

struct LadderEndView {
    void* screen = nullptr;
    std::string statLabels[6];  // m_RunStats — the six captions, in drawn order
    std::string description;    // m_Description
};

void* FindLadderEndScreen();
LadderEndView ReadLadderEnd(void* screen);

// ---- the challenge result (UIChallengePostScreen) ----
//
// The end of a challenge: header (CHALLENGE SCORING, or the failure header), the player's name and
// the time, TOTAL SCORE, the maximum, a description, and Continue. The LEADERBOARD button is hidden
// for offline challenges; the photobooth button is hidden always (its branch is `if (false)`).

struct ChallengePostView {
    void* screen = nullptr;
    bool failed = false;          // bMissionFailed
    std::string header;           // m_Header / m_HeaderLose by `failed`
    std::string scoreLabel;       // m_ScoreLabel
    std::string rankLabel;        // m_RankLabel — pushed with an EMPTY value; never drawn with one
    std::string leaderboardLabel; // m_LeaderboardLabel — the leaderboard button's word
    std::string description;      // m_Description / m_DescriptionLose by `failed`
    MenuItem continueButton;      // m_ContinueButton — retains m_strGenericContinue
    void* leaderboardButton = nullptr; // m_LeaderboardButton when visible (no label of its own)
};

void* FindChallengePostScreen();
ChallengePostView ReadChallengePost(void* screen);

// ---- the challenge event notice (UIChallengeModeEventNotify) ----
//
// The small window a networked challenge raises to say how other players fared at an event:
// a panel header, a text container holding the whole notice, and Close / Previous / Next
// buttons, each greyed by DisableButton when there is nothing to page to. Everything is
// retained (the header's Title, the UIText's Text, the buttons' Text and IsDisabled), so this is a
// pure widget read. The screen's own strings are English literals in the shipped source.

struct ChallengeEventView {
    void* screen = nullptr;
    std::string title;            // m_TitleHeader.Title
    std::string body;             // m_kDetailText.Text.Text — line breaks collapsed by CleanText
    MenuItem closeButton;         // m_CloseButton
    MenuItem previousButton;      // m_PreviousButton
    MenuItem nextButton;          // m_NextButton
};

void* FindChallengeEventScreen();
ChallengeEventView ReadChallengeEvent(void* screen);

} // namespace GameUI
