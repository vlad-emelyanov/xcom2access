#pragma once

// Game-UI access, the Tactical Legacy Pack's SKIRMISH MODE menu (UITLE_SkirmishModeMenu) — the
// screen recipe_tle_skirmish.cpp reads. Implemented in game_ui_tle_skirmish.cpp.
// Recon: docs/tle-skirmish-recon.md.
//
// ONE SCREEN, TWENTY-ONE STATES. The class is a state machine (EUISkirmishScreen) over a single
// UIList of UIMechaListItems: every state destroys the rows and respawns them (HideListItems),
// so a row pointer never survives a state change, and every row retains its own label and value
// (mecha rows do — the settings screen reads the same widget). The right-hand panel is pushed
// into Flash from the screen's OWN cached state objects (m_CurrentSquad, BattleDataState,
// ChallengeMissionSite, CampaignSettings) and retained nowhere; X2AccessSkirmishReader
// re-derives it from those same objects. The START GAME button is a UILargeButton spawned with
// no label — its word is m_StartGame, pushed to Flash through SetMissionInfo — so it is read by
// pointer and focus bit, the way the hub's buttons are.

#include "game_ui_core.h"

namespace GameUI {

/// EUISkirmishScreen, verbatim. The subtitle array is indexed by this, and the companion decides
/// which panel the screen is drawing from it.
enum class SkirmishState : int {
    Base = 0,
    Squad,
    SoldierData,
    SoldierClass,
    SoldierEquipment,
    PrimaryWeapon,
    SecondaryWeapon,
    Armor,
    PCS,
    UtilItem1,
    UtilItem2,
    GrenadePocket,
    HeavyWeapon,
    SoldierAbilities,
    SoldierRank,
    MapData,
    Map,
    Mission,
    Biome,
    Enemies,
    Difficulty,
    Count
};

struct SkirmishView {
    void* screen = nullptr;
    void* list = nullptr;          // UITLE_SkirmishModeMenu::List
    int state = 0;                 // m_CurrentState (a byte on the instance)
    std::string title;             // m_ScreenTitle
    std::string subtitle;          // m_arrSubtitles[m_CurrentState] — empty on the base menu
    std::vector<OptionRow> rows;   // the list's rows, sized from the list itself
    int selected = -1;             // List.SelectedIndex
    void* startButton = nullptr;   // ContinueButton — null when hidden
    bool startFocused = false;
    std::string startLabel;        // m_StartGame — the button's Flash-drawn word
};

/// The focused UITLE_SkirmishModeMenu, or null. Game thread only.
void* FindSkirmishScreen();

SkirmishView ReadSkirmish(void* screen);

/// P11 write-back into the option list (UIList.SetSelectedIndex). The list's OnSetSelectedIndex
/// is the screen's own selection hook — squad details, enemy portraits and ability text follow
/// it — so this drives the right-hand panel exactly as the game's arrows do. Game thread only.
bool SetSkirmishListIndex(void* screen, int index);

} // namespace GameUI
