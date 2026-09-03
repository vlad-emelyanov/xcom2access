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

// ---- strategy (recipe_strategy.cpp) ----
extern const Recipe kSquadSelectRecipe;
extern const Recipe kAfterActionRecipe;
extern const Recipe kArmoryLoadoutRecipe;
extern const Recipe kGeoscapeRecipe;
extern const Recipe kPopupRecipe;
extern const Recipe kMissionBladeRecipe;
extern const Recipe kPromotionRecipe;
extern const Recipe kCovertActionsRecipe;
extern const Recipe kStrategyPolicyRecipe;
extern const Recipe kChosenInfoRecipe;   // the Chosen dossier (UIChosenInfo)
extern const Recipe kBondScreenRecipe;   // soldier bonds (UISoldierBondScreen)
extern const Recipe kBondConfirmRecipe;  // bond confirmation (UISoldierBondConfirmScreen)

// ---- Avenger (recipe_avenger.cpp) ----
extern const Recipe kRoomRecipe;
extern const Recipe kRoomBuildRecipe;
extern const Recipe kCommodityRecipe;
extern const Recipe kPersonnelRecipe;
extern const Recipe kChooseFacilityRecipe;
extern const Recipe kChooseUpgradeRecipe;
extern const Recipe kResearchReportRecipe;
extern const Recipe kResearchUnlockedRecipe;

// ---- tactical (recipe_tactical.cpp) ----
extern const Recipe kTacticalHudRecipe;
extern const Recipe kMissionSummaryRecipe;
extern const Recipe kChosenSummaryRecipe;

// How many of the above kRecipes must list. Pinned by a static_assert next to the array.
constexpr std::size_t kRecipeCount = 32;

} // namespace Recipes
} // namespace MenuNavigator
