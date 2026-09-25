#pragma once

// Every screen recipe there is, one line each — and NOT the order they are probed in.
//
// ⛔ THE ORDER LIVES IN `kRecipes` IN menu_navigator.cpp, deliberately. Probe order is what
// resolves a stacked screen (CLAUDE.md: "listed in kRecipes TOPMOST SCREEN FIRST"), and after
// the recipes moved into four files the tempting shape — let each TU register itself from a
// static initializer — would have destroyed that rule silently: cross-TU static initialization
// order is unspecified, so the roster would order itself by LINK order. This header exists so
// the array can stay an explicit literal, one readable list beside the loop that scans it,
// while the definitions live with their surface family.
//
// Adding a recipe: define it in its family TU, declare it here, bump kRecipeCount, and place
// it in kRecipes. The count makes the third step a build error rather than a screen that is
// never probed.

#include "recipes/recipe_context.h"

#include <cstddef>

namespace MenuNavigator {
namespace Recipes {

// ---- shell (recipe_shell.cpp) ----
extern const Recipe kMainMenuRecipe;
extern const Recipe kOptionsRecipe;
extern const Recipe kDifficultyRecipe;
extern const Recipe kIronManRecipe;
extern const Recipe kNarrativeRecipe;
extern const Recipe kSoundtrackRecipe;
extern const Recipe kPauseMenuRecipe;
extern const Recipe kDialogRecipe;
extern const Recipe kSaveLoadRecipe;
extern const Recipe kCharacterPoolRecipe; // the main menu's character pool (UICharacterPool)
extern const Recipe kPoolFilesRecipe;     // its import / export picker (UICharacterPool_ListPools)
extern const Recipe kTLEHubRecipe;        // the Tactical Legacy Pack hub (UITLEHub)

// ---- Tactical Legacy Pack, shell half (recipe_tle.cpp) ----
extern const Recipe kLadderMenuRecipe;       // Legacy Operations (UITLE_LadderModeMenu)
extern const Recipe kLadderDifficultyRecipe; // its difficulty page (UITLE_LadderDifficulty)
extern const Recipe kUnitStatsRecipe;        // the soldier loadout popup (UITLEUnitStats)
// ---- Tactical Legacy Pack, the Challenge Archives (recipe_tle_challenge.cpp) ----
extern const Recipe kChallengeMenuRecipe;    // the offline daily-challenge archive (UITLE_ChallengeModeMenu)
// ---- Tactical Legacy Pack, Skirmish Mode (recipe_tle_skirmish.cpp) ----
extern const Recipe kSkirmishRecipe;         // the skirmish builder, all 21 states (UITLE_SkirmishModeMenu)
// ---- Tactical Legacy Pack, mission half (recipe_tle_mission.cpp) ----
extern const Recipe kScoringDialogRecipe;       // MISSION / CHALLENGE SCORING before turn 1 (UILadderModeScoringDialog, UIChallengeModeScoringDialog)
extern const Recipe kLadderUpgradeRecipe;       // the between-missions upgrade pick (UILadderUpgradeScreen)
extern const Recipe kLadderSoldiersRecipe;      // the squad review (UILadderSoldierInfo)
extern const Recipe kLadderMedalProgressRecipe; // medal progress between missions (UITLELadderMedalProgressScreen)
extern const Recipe kLadderMedalRecipe;         // the finished ladder's medal (UITLELadderMedalScreen)
extern const Recipe kLadderEndRecipe;           // the run's statistics (UITLELadderEndScreen)
extern const Recipe kChallengePostRecipe;       // the challenge result (UIChallengePostScreen)
extern const Recipe kChallengeEventRecipe;      // the networked challenge's event notice (UIChallengeModeEventNotify)

// ---- mod-provided screens (recipe_mods.cpp) ----
extern const Recipe kModConfigRecipe;        // Mod Config Menu (MCM_OptionsScreen), over settings

// ---- strategy (recipe_strategy.cpp) ----
extern const Recipe kEndGameRecipe;         // campaign win/loss, five pages
extern const Recipe kMemorialDetailsRecipe; // fallen soldier detail card
extern const Recipe kSquadSelectRecipe;
extern const Recipe kAfterActionRecipe;
extern const Recipe kArmoryLoadoutRecipe;
extern const Recipe kGeoscapeRecipe;
extern const Recipe kPopupRecipe;
extern const Recipe kMissionBladeRecipe;
extern const Recipe kSkyrangerArrivesRecipe; // the Skyranger-arrived prompt (UISkyrangerArrives)
extern const Recipe kPromotionRecipe;
extern const Recipe kCovertActionsRecipe;
extern const Recipe kStrategyPolicyRecipe;
extern const Recipe kChosenInfoRecipe;   // the Chosen dossier (UIChosenInfo)
extern const Recipe kBondScreenRecipe;   // soldier bonds (UISoldierBondScreen)
extern const Recipe kBondConfirmRecipe;  // bond confirmation (UISoldierBondConfirmScreen)
extern const Recipe kResistanceReportRecipe; // the monthly council report (UIResistanceReport)
extern const Recipe kChosenEventsRecipe;     // its Chosen page (UIResistanceReport_ChosenEvents)
extern const Recipe kCovertReportRecipe;     // the covert action result (UICovertActionReport)
extern const Recipe kArmoryMenuRecipe;       // the armory hub (UIArmory_MainMenu)
extern const Recipe kWeaponUpgradeRecipe;    // weapon mods + the pattern picker (UIArmory_WeaponUpgrade)
extern const Recipe kCustomizeRecipe;        // soldier customization, the whole UICustomize family
extern const Recipe kManageEquipmentRecipe;  // the strip-equipment menu (UIManageEquipmentMenu)

// ---- Avenger (recipe_avenger.cpp) ----
extern const Recipe kRoomRecipe;
extern const Recipe kRoomBuildRecipe;
extern const Recipe kCommodityRecipe;
extern const Recipe kPersonnelRecipe;
extern const Recipe kChooseFacilityRecipe;
extern const Recipe kChooseUpgradeRecipe;
extern const Recipe kResearchReportRecipe;
extern const Recipe kResearchUnlockedRecipe;
extern const Recipe kAvengerHudRecipe; // ambient — the ant farm's Tab layer
extern const Recipe kBlackMarketRecipe;     // the shop's entry menu (UIBlackMarket)
extern const Recipe kBlackMarketSellRecipe; // the trading post (UIBlackMarket_Sell)
extern const Recipe kRecruitsRecipe;        // the Armory's recruit list (UIRecruitSoldiers)
extern const Recipe kRewardsRecapRecipe;    // the post-mission rewards recap (UIRewardsRecap)

// ---- tactical (recipe_tactical.cpp) ----
extern const Recipe kTacticalHudRecipe;
extern const Recipe kMissionSummaryRecipe;
extern const Recipe kChosenSummaryRecipe;
extern const Recipe kLootRecipe;     // the mid-mission loot receipt (UIInventoryTactical)
extern const Recipe kCharInfoRecipe; // the soldier sheet (UITacticalCharInfoScreen)

// How many of the above kRecipes must list. Pinned by a static_assert next to the array.
constexpr std::size_t kRecipeCount = 66;

} // namespace Recipes
} // namespace MenuNavigator
