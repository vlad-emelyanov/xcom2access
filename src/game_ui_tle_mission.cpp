// The Tactical Legacy Pack's MISSION-SIDE readers: the two scoring dialogs, the ladder upgrade
// picker, the ladder squad review, the medal / medal-progress / end screens, the challenge result
// and the challenge event notice. See game_ui_tle_mission.h for what each screen is and what it
// retains; docs/tle-mission-recon.md for how each was measured.
//
// Every offset here is transcribed from the generated SDK's reflection-dump comments and pinned
// in sdk_layout_checks.cpp. ⚠ Two of these classes pack at 4 (UILadderSoldierInfo,
// UIChallengePostScreen): their pointers sit at 0x4AC-style unaligned offsets, which the raw
// readers handle and the static_asserts pin exactly as the dump has them.

#include "game_ui_tle_mission.h"
#include "game_ui_internal.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <string>

// Hand-rolled ProcessEvent parameter blocks below must match the engine's pack(4) layout.
#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GameUI {

namespace {

// ---- the scoring dialogs ----
// UILadderModeScoringDialog
constexpr int32_t kLadderScoreContinueOffset = 0x410;  // m_ContinueButton (UIButton*)
constexpr int32_t kLadderScoreTotalOffset = 0x418;     // m_TotalLabel
constexpr int32_t kLadderScoreDesc1Offset = 0x4C8;     // m_ScoreDescription1
constexpr int32_t kLadderScoreDesc2Offset = 0x4D8;     // m_ScoreDescription2
constexpr int32_t kLadderScoreHeaderOffset = 0x528;    // m_LadderHeader
// UIChallengeModeScoringDialog — m_Header is declared FIRST, shifting every string after it
constexpr int32_t kChalScoreContinueOffset = 0x410;    // m_ContinueButton (UIButton*)
constexpr int32_t kChalScoreHeaderOffset = 0x418;      // m_Header
constexpr int32_t kChalScoreTotalOffset = 0x428;       // m_TotalLabel
constexpr int32_t kChalScoreDesc1Offset = 0x4D8;       // m_ScoreDescription1
constexpr int32_t kChalScoreDesc2Offset = 0x4E8;       // m_ScoreDescription2

// ---- the ladder upgrade picker (UILadderUpgradeScreen) ----
constexpr int32_t kUpgradeButton0Offset = 0x410;       // Upgrade0Button (UIButton*)
constexpr int32_t kUpgradeButton1Offset = 0x418;       // Upgrade1Button (UIButton*)
constexpr int32_t kUpgradeSelectedColumnOffset = 0x420; // SelectedColumn (int)
constexpr int32_t kUpgradeChooseOffset = 0x42C;        // m_strChooseUpgrade
constexpr int32_t kUpgradeEquipOffset = 0x43C;         // m_strEquipUpgrade

// ---- the ladder squad review (UILadderSoldierInfo), pack(4) ----
constexpr int32_t kSoldierInfoStartOffset = 0x410;     // m_SoldierStartIndex (int)
constexpr int32_t kSoldierInfoCountOffset = 0x414;     // m_NumSoldiers (int)
constexpr int32_t kSoldierInfoPrevOffset = 0x464;      // PrevButton (UIButton*)
constexpr int32_t kSoldierInfoNextOffset = 0x46C;      // NextButton (UIButton*)
constexpr int32_t kSoldierInfoButtonsOffset = 0x474;   // LadderSoldierInfoButtons (TArray<UIButton*>)
constexpr int32_t kSoldierInfoNextMissionOffset = 0x49C;   // m_NextMission
constexpr int32_t kSoldierInfoMissionLabelOffset = 0x4AC;  // m_MissionLabel
constexpr int32_t kSoldierInfoCompleteOffset = 0x4BC;      // m_MissionComplete
constexpr int32_t kSoldierInfoNewAbilitiesOffset = 0x4EC;  // m_NewAbilities
constexpr int32_t kSoldierInfoNewItemsOffset = 0x50C;      // m_NewItems
constexpr int32_t kSoldierInfoHelpPrevOffset = 0x51C;      // m_strHelpPrevPage
constexpr int32_t kSoldierInfoHelpNextOffset = 0x52C;      // m_strHelpNextPage
constexpr int32_t kSoldierInfoHelpInfoOffset = 0x53C;      // m_strHelpSoldierInfo

// ---- the medal screen (UITLELadderMedalScreen) ----
constexpr int32_t kMedalRewardsOffset = 0x420;         // m_RewardsLabel
constexpr int32_t kMedalScoreLabelOffset = 0x430;      // m_ScoreLabel
constexpr int32_t kMedalMedalScoreOffset = 0x440;      // m_MedalScoreLabel
constexpr int32_t kMedalUnlockedOffset = 0x450;        // m_MedalUnlockedString (filled at OnInit)
constexpr int32_t kMedalVictoryOffset = 0x460;         // m_VictoryString
constexpr int32_t kMedalNextLadderOffset = 0x470;      // m_UnlockedNextLadder
constexpr int32_t kMedalUnlockDescOffset = 0x4A0;      // m_LadderUnlockDescription

// ---- the end screen (UITLELadderEndScreen) ----
constexpr int32_t kEndRunStatsOffset = 0x420;          // m_RunStats (TArray<FString>)
constexpr int32_t kEndDescriptionOffset = 0x430;       // m_Description

// ---- the challenge result (UIChallengePostScreen), pack(4) ----
constexpr int32_t kPostHeaderOffset = 0x410;           // m_Header
constexpr int32_t kPostHeaderLoseOffset = 0x420;       // m_HeaderLose
constexpr int32_t kPostScoreLabelOffset = 0x430;       // m_ScoreLabel
constexpr int32_t kPostRankLabelOffset = 0x440;        // m_RankLabel
constexpr int32_t kPostLeaderboardOffset = 0x450;      // m_LeaderboardLabel
constexpr int32_t kPostDescriptionOffset = 0x460;      // m_Description
constexpr int32_t kPostDescriptionLoseOffset = 0x470;  // m_DescriptionLose
constexpr int32_t kPostFlagsOffset = 0x498;            // dword: bAllSoldiersDead 1, bMissionFailed 2
constexpr uint32_t kPostMissionFailed = 0x02;
constexpr int32_t kPostContinueOffset = 0x4AC;         // m_ContinueButton (UIButton*)
constexpr int32_t kPostLeaderboardBtnOffset = 0x4B4;   // m_LeaderboardButton (UIButton*)

// ---- the challenge event notice (UIChallengeModeEventNotify) ----
constexpr int32_t kEventHeaderOffset = 0x418;          // m_TitleHeader (UIX2PanelHeader*)
constexpr int32_t kEventCloseOffset = 0x420;           // m_CloseButton
constexpr int32_t kEventNextOffset = 0x428;            // m_NextButton
constexpr int32_t kEventPrevOffset = 0x430;            // m_PreviousButton
constexpr int32_t kEventDetailOffset = 0x438;          // m_kDetailText (UITextContainer*)
constexpr int32_t kPanelHeaderTitleOffset = 0x3D8;     // UIX2PanelHeader::Title
constexpr int32_t kTextContainerTextOffset = 0x3E0;    // UITextContainer::Text (UIText*)
constexpr int32_t kUITextTextOffset = 0x3D8;           // UIText::Text
constexpr int32_t kUITextHtmlOffset = 0x3E8;           // UIText::HtmlText

// The stack-first finder every screen here shares: pushed over the tactical HUD as ordinary stack
// entries, all of them focused while up, none of them permanent — so the loot receipt's finder
// shape is the right one, and a class the stack does not hold is simply not showing.
void* FindFocusedByClass(const char* className)
{
    void* screen = FindStackScreenRaw(className, ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

// A button by pointer alone — present and visible — for the buttons that carry no Text (the
// squad review's info buttons, the challenge result's leaderboard button). ReadButtonField treats
// an empty label as "no button", which is right for a labelled widget and wrong for these.
void* VisibleButton(void* screen, int32_t offset)
{
    void* btn = ReadPtrRaw(screen, offset);
    if (!btn)
        return nullptr;
    return (ReadU32Raw(btn, kPanelFlagsOffset) & kPanelVisible) ? btn : nullptr;
}

std::string ReadStringElem(const void* obj, int32_t offset, int index)
{
    wchar_t buf[512];
    const int len = CopyFStringElemRaw(obj, offset, index, buf, 512);
    return len > 0 ? CleanText(WcharToUtf8(buf, len)) : std::string();
}

} // namespace

// ---- the scoring dialogs ----

void* FindScoringDialog()
{
    // Sequential, never both: a mission is a ladder mission or a challenge.
    if (void* ladder = FindFocusedByClass("UILadderModeScoringDialog"))
        return ladder;
    return FindFocusedByClass("UIChallengeModeScoringDialog");
}

ScoringDialogView ReadScoringDialog(void* screen)
{
    ScoringDialogView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.ladder = ObjectClassNamedRaw(screen, "UILadderModeScoringDialog", ClassMatch::Derived);
    if (v.ladder) {
        v.header = CleanText(ReadFStringField(screen, kLadderScoreHeaderOffset));
        v.totalLabel = CleanText(ReadFStringField(screen, kLadderScoreTotalOffset));
        v.description1 = CleanText(ReadFStringField(screen, kLadderScoreDesc1Offset));
        v.description2 = CleanText(ReadFStringField(screen, kLadderScoreDesc2Offset));
        v.continueButton = ReadButtonField(screen, kLadderScoreContinueOffset);
    } else {
        v.header = CleanText(ReadFStringField(screen, kChalScoreHeaderOffset));
        v.totalLabel = CleanText(ReadFStringField(screen, kChalScoreTotalOffset));
        v.description1 = CleanText(ReadFStringField(screen, kChalScoreDesc1Offset));
        v.description2 = CleanText(ReadFStringField(screen, kChalScoreDesc2Offset));
        v.continueButton = ReadButtonField(screen, kChalScoreContinueOffset);
    }
    return v;
}

// ---- the ladder upgrade picker ----

void* FindLadderUpgradeScreen()
{
    return FindFocusedByClass("UILadderUpgradeScreen");
}

LadderUpgradeView ReadLadderUpgrade(void* screen)
{
    LadderUpgradeView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.selectedColumn = ReadI32Raw(screen, kUpgradeSelectedColumnOffset);
    v.chooseLabel = CleanText(ReadFStringField(screen, kUpgradeChooseOffset));
    v.equipLabel = CleanText(ReadFStringField(screen, kUpgradeEquipOffset));
    // Both buttons are InitButton'd with m_strChooseUpgrade as their Text, so the labelled read
    // applies; the game never hides either.
    v.buttons[0] = ReadButtonField(screen, kUpgradeButton0Offset);
    v.buttons[1] = ReadButtonField(screen, kUpgradeButton1Offset);
    return v;
}

bool SelectLadderUpgradeColumn(void* screen, int column)
{
    if (!screen || column < 0 || column > 1)
        return false;
    // SelectColumn(int iCol): writes SelectedColumn, moves the Flash highlight and refreshes both
    // EQUIP labels — the body the game's own Left/Right runs.
    struct {
        int32_t Column;
    } params{column};
    return CallNamedFunction(screen, "SelectColumn", &params);
}

// ---- the ladder squad review ----

void* FindLadderSoldierInfoScreen()
{
    return FindFocusedByClass("UILadderSoldierInfo");
}

LadderSoldierInfoView ReadLadderSoldierInfo(void* screen)
{
    LadderSoldierInfoView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.startIndex = ReadI32Raw(screen, kSoldierInfoStartOffset);
    v.numSoldiers = ReadI32Raw(screen, kSoldierInfoCountOffset);
    // Prev / Next carry an EMPTY label ("" at InitButton) — so, like the info buttons, they are
    // read by pointer and visibility; Hide() is exactly how the game marks the first and last page.
    if (void* prev = VisibleButton(screen, kSoldierInfoPrevOffset)) {
        v.prevButton.handle = prev;
        v.prevButton.label = CleanText(ReadFStringField(screen, kSoldierInfoHelpPrevOffset));
    }
    if (void* next = VisibleButton(screen, kSoldierInfoNextOffset)) {
        v.nextButton.handle = next;
        v.nextButton.label = CleanText(ReadFStringField(screen, kSoldierInfoHelpNextOffset));
    }
    void* buttons[3] = {};
    const int count = TakeArrayRaw(
        "LadderSoldierInfo: info buttons", screen, kSoldierInfoButtonsOffset, buttons, 3);
    for (int i = 0; i < count && i < 3; i++) {
        if (buttons[i] && (ReadU32Raw(buttons[i], kPanelFlagsOffset) & kPanelVisible))
            v.infoButtons[i] = buttons[i];
    }
    v.nextMission = CleanText(ReadFStringField(screen, kSoldierInfoNextMissionOffset));
    v.missionLabel = CleanText(ReadFStringField(screen, kSoldierInfoMissionLabelOffset));
    v.missionComplete = CleanText(ReadFStringField(screen, kSoldierInfoCompleteOffset));
    v.newAbilities = CleanText(ReadFStringField(screen, kSoldierInfoNewAbilitiesOffset));
    v.newItems = CleanText(ReadFStringField(screen, kSoldierInfoNewItemsOffset));
    v.helpPrevPage = CleanText(ReadFStringField(screen, kSoldierInfoHelpPrevOffset));
    v.helpNextPage = CleanText(ReadFStringField(screen, kSoldierInfoHelpNextOffset));
    v.helpSoldierInfo = CleanText(ReadFStringField(screen, kSoldierInfoHelpInfoOffset));
    return v;
}

bool OpenLadderSoldierStats(void* screen, int slot)
{
    if (!screen || slot < 0 || slot > 2)
        return false;
    // DisplaySoldierInfo(int index): the info button's delegate body — adds the page offset,
    // skips the dead, and pushes UITLEUnitStats. The gamepad's Y calls it with 0.
    struct {
        int32_t Index;
    } params{slot};
    return CallNamedFunction(screen, "DisplaySoldierInfo", &params);
}

// ---- the three one-button ladder screens ----

void* FindLadderMedalScreen()
{
    return FindFocusedByClass("UITLELadderMedalScreen");
}

LadderMedalView ReadLadderMedal(void* screen)
{
    LadderMedalView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.rewardsLabel = CleanText(ReadFStringField(screen, kMedalRewardsOffset));
    v.scoreLabel = CleanText(ReadFStringField(screen, kMedalScoreLabelOffset));
    v.medalScoreLabel = CleanText(ReadFStringField(screen, kMedalMedalScoreOffset));
    v.medalUnlocked = CleanText(ReadFStringField(screen, kMedalUnlockedOffset));
    v.victory = CleanText(ReadFStringField(screen, kMedalVictoryOffset));
    v.unlockedNextLadder = CleanText(ReadFStringField(screen, kMedalNextLadderOffset));
    v.unlockDescription = CleanText(ReadFStringField(screen, kMedalUnlockDescOffset));
    return v;
}

void* FindLadderMedalProgressScreen()
{
    return FindFocusedByClass("UITLELadderMedalProgressScreen");
}

void* FindLadderEndScreen()
{
    return FindFocusedByClass("UITLELadderEndScreen");
}

LadderEndView ReadLadderEnd(void* screen)
{
    LadderEndView v;
    if (!screen)
        return v;
    v.screen = screen;
    // The six captions are a localized array; OnInit draws exactly min(6, Length) of them.
    const int32_t count = ReadArrayCountRaw(screen, kEndRunStatsOffset);
    for (int i = 0; i < 6 && i < count; i++)
        v.statLabels[i] = ReadStringElem(screen, kEndRunStatsOffset, i);
    v.description = CleanText(ReadFStringField(screen, kEndDescriptionOffset));
    return v;
}

// ---- the challenge result ----

void* FindChallengePostScreen()
{
    return FindFocusedByClass("UIChallengePostScreen");
}

ChallengePostView ReadChallengePost(void* screen)
{
    ChallengePostView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.failed = (ReadU32Raw(screen, kPostFlagsOffset) & kPostMissionFailed) != 0;
    // ⚠ OnInit queues m_Header on both branches and then calls setFailureState; whether the
    // movie swaps in the failure header is the FLA's business, so the failure words are chosen
    // here on the same flag the movie is told. The first live failure settles it.
    v.header = CleanText(
        ReadFStringField(screen, v.failed ? kPostHeaderLoseOffset : kPostHeaderOffset));
    v.scoreLabel = CleanText(ReadFStringField(screen, kPostScoreLabelOffset));
    v.rankLabel = CleanText(ReadFStringField(screen, kPostRankLabelOffset));
    v.leaderboardLabel = CleanText(ReadFStringField(screen, kPostLeaderboardOffset));
    v.description = CleanText(ReadFStringField(
        screen, v.failed ? kPostDescriptionLoseOffset : kPostDescriptionOffset));
    v.continueButton = ReadButtonField(screen, kPostContinueOffset);
    // No Text of its own (InitButton is handed none); the movie draws the leaderboard label.
    v.leaderboardButton = VisibleButton(screen, kPostLeaderboardBtnOffset);
    return v;
}

// ---- the challenge event notice ----

void* FindChallengeEventScreen()
{
    return FindFocusedByClass("UIChallengeModeEventNotify");
}

ChallengeEventView ReadChallengeEvent(void* screen)
{
    ChallengeEventView v;
    if (!screen)
        return v;
    v.screen = screen;
    if (void* header = ReadPtrRaw(screen, kEventHeaderOffset))
        v.title = CleanText(ReadFStringField(header, kPanelHeaderTitleOffset));
    if (void* container = ReadPtrRaw(screen, kEventDetailOffset)) {
        if (void* text = ReadPtrRaw(container, kTextContainerTextOffset)) {
            // Text first, HtmlText as the fallback — the soldier sheet's rule for UIText.
            std::string body = ReadFStringField(text, kUITextTextOffset);
            if (body.empty())
                body = ReadFStringField(text, kUITextHtmlOffset);
            v.body = CleanText(body);
        }
    }
    v.closeButton = ReadButtonField(screen, kEventCloseOffset);
    v.previousButton = ReadButtonField(screen, kEventPrevOffset);
    v.nextButton = ReadButtonField(screen, kEventNextOffset);
    return v;
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
