#pragma once

// The vocabulary module (spec A8): every string the LAYER itself speaks routes through here —
// hardcoded English at call sites is forbidden. Game content (button labels, log lines)
// is already localized and passes through untouched. Our words resolve from X2AccessVocab.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

namespace Vocab {

// Resolved only after the companion package has loaded. Converting a Word is a cache lookup;
// it never touches engine memory, including when a UI callback reads it outside the tick.
const char* Lookup(const char* key);
void LoadFromGame(); // game tick only
void AnnounceStartup(); // after the game-localized CDO is ready

struct Word {
    const char* key;
    const char* c_str() const { return Lookup(key); }
    operator std::string() const { return c_str(); }
};

// Game-owned labels come from their own class defaults when a screen's live field is empty.
// The generic mod word is only for a missing game CDO, never a copy of the game's label.
inline constexpr Word ScreenUnnamed{"ScreenUnnamed"};
inline constexpr Word SectionUnnamed{"SectionUnnamed"};
std::string GameString(const char* className, const char* propertyName,
    const Word& emergency = ScreenUnnamed);

inline std::string operator+(const std::string& left, const Word& right)
{ return left + std::string(right); }
inline std::string operator+(const Word& left, const std::string& right)
{ return std::string(left) + right; }
inline std::string operator+(const Word& left, const char* right)
{ return std::string(left) + right; }
inline std::string operator+(const char* left, const Word& right)
{ return left + std::string(right); }

inline std::string Format(const Word& format, const std::string& first,
    const std::string& second = {})
{
    std::string text = format;
    for (const auto& pair : {std::pair{"{0}", first}, std::pair{"{1}", second}}) {
        size_t pos = 0;
        while ((pos = text.find(pair.first, pos)) != std::string::npos) {
            text.replace(pos, 3, pair.second);
            pos += pair.second.size();
        }
    }
    return text;
}

inline constexpr Word LadderRowFormat{"LadderRowFormat"};
inline constexpr Word BondLevelFormat{"BondLevelFormat"};
inline constexpr Word MeterLevelFormat{"MeterLevelFormat"};
inline constexpr Word FractionFormat{"FractionFormat"};
inline constexpr Word DoomUpFormat{"DoomUpFormat"};
inline constexpr Word DoomDownFormat{"DoomDownFormat"};
inline constexpr Word AbilityFallbackFormat{"AbilityFallbackFormat"};
inline constexpr Word AbilityHotkeyFormat{"AbilityHotkeyFormat"};
inline constexpr Word CommandFallbackFormat{"CommandFallbackFormat"};
inline constexpr Word SurfaceUnavailableFormat{"SurfaceUnavailableFormat"};
inline constexpr Word PositionFormat{"PositionFormat"};
inline constexpr Word PercentFormat{"PercentFormat"};
inline constexpr Word PercentFewFormat{"PercentFewFormat"};
inline constexpr Word PercentManyFormat{"PercentManyFormat"};
inline constexpr Word KnowledgeChangeFormat{"KnowledgeChangeFormat"};
inline constexpr Word TabFallbackFormat{"TabFallbackFormat"};
inline constexpr Word ColourBlack{"ColourBlack"};
inline constexpr Word ColourWhite{"ColourWhite"};
inline constexpr Word ColourGrey{"ColourGrey"};
inline constexpr Word ColourBrown{"ColourBrown"};
inline constexpr Word ColourGreyishBrown{"ColourGreyishBrown"};
inline constexpr Word ColourRed{"ColourRed"};
inline constexpr Word ColourOrange{"ColourOrange"};
inline constexpr Word ColourYellow{"ColourYellow"};
inline constexpr Word ColourGreen{"ColourGreen"};
inline constexpr Word ColourTeal{"ColourTeal"};
inline constexpr Word ColourBlue{"ColourBlue"};
inline constexpr Word ColourPurple{"ColourPurple"};
inline constexpr Word ColourPink{"ColourPink"};
inline constexpr Word ColourDarkPrefix{"ColourDarkPrefix"};
inline constexpr Word ColourLightPrefix{"ColourLightPrefix"};
inline constexpr Word ColourGreyishPrefix{"ColourGreyishPrefix"};
inline constexpr Word PunctSpace{"PunctSpace"};
inline constexpr Word PunctExclamation{"PunctExclamation"};
inline constexpr Word PunctQuote{"PunctQuote"};
inline constexpr Word PunctHash{"PunctHash"};
inline constexpr Word PunctDollar{"PunctDollar"};
inline constexpr Word PunctPercent{"PunctPercent"};
inline constexpr Word PunctAmpersand{"PunctAmpersand"};
inline constexpr Word PunctApostrophe{"PunctApostrophe"};
inline constexpr Word PunctLeftParen{"PunctLeftParen"};
inline constexpr Word PunctRightParen{"PunctRightParen"};
inline constexpr Word PunctStar{"PunctStar"};
inline constexpr Word PunctPlus{"PunctPlus"};
inline constexpr Word PunctComma{"PunctComma"};
inline constexpr Word PunctDash{"PunctDash"};
inline constexpr Word PunctDot{"PunctDot"};
inline constexpr Word PunctSlash{"PunctSlash"};
inline constexpr Word PunctColon{"PunctColon"};
inline constexpr Word PunctSemicolon{"PunctSemicolon"};
inline constexpr Word PunctLessThan{"PunctLessThan"};
inline constexpr Word PunctEquals{"PunctEquals"};
inline constexpr Word PunctGreaterThan{"PunctGreaterThan"};
inline constexpr Word PunctQuestion{"PunctQuestion"};
inline constexpr Word PunctAt{"PunctAt"};
inline constexpr Word PunctLeftBracket{"PunctLeftBracket"};
inline constexpr Word PunctBackslash{"PunctBackslash"};
inline constexpr Word PunctRightBracket{"PunctRightBracket"};
inline constexpr Word PunctCaret{"PunctCaret"};
inline constexpr Word PunctUnderscore{"PunctUnderscore"};
inline constexpr Word PunctBacktick{"PunctBacktick"};
inline constexpr Word PunctLeftBrace{"PunctLeftBrace"};
inline constexpr Word PunctBar{"PunctBar"};
inline constexpr Word PunctRightBrace{"PunctRightBrace"};
inline constexpr Word PunctTilde{"PunctTilde"};
inline constexpr Word ObjectiveCivilian{"ObjectiveCivilian"};
inline constexpr Word ObjectiveEvidence{"ObjectiveEvidence"};
inline constexpr Word ObjectiveCrates{"ObjectiveCrates"};
inline constexpr Word ObjectiveAvailable{"ObjectiveAvailable"};
inline constexpr Word ObjectiveDead{"ObjectiveDead"};
inline constexpr Word ObjectiveRescued{"ObjectiveRescued"};
inline constexpr Word ObjectiveDestroyed{"ObjectiveDestroyed"};
inline constexpr Word ObjectiveRecovered{"ObjectiveRecovered"};
inline constexpr Word ObjectivePickedUp{"ObjectivePickedUp"};
inline constexpr Word ObjectiveWarning{"ObjectiveWarning"};
inline constexpr Word ObjectiveCompleted{"ObjectiveCompleted"};
inline constexpr Word ObjectiveFailed{"ObjectiveFailed"};
inline constexpr Word ObjectiveNoneListed{"ObjectiveNoneListed"};
inline constexpr Word ObjectiveUnlimited{"ObjectiveUnlimited"};
inline constexpr Word ObjectiveExpired{"ObjectiveExpired"};
inline constexpr Word ObjectiveLocked{"ObjectiveLocked"};
inline constexpr Word ObjectiveUnavailable{"ObjectiveUnavailable"};
inline constexpr Word ObjectiveTurnsOne{"ObjectiveTurnsOne"};
inline constexpr Word ObjectiveTurnsFew{"ObjectiveTurnsFew"};
inline constexpr Word ObjectiveTurnsMany{"ObjectiveTurnsMany"};
inline constexpr Word ObjectiveOfFormat{"ObjectiveOfFormat"};
inline constexpr Word SurfaceAvengerReader{"SurfaceAvengerReader"};
inline constexpr Word SurfaceAimReader{"SurfaceAimReader"};
inline constexpr Word SurfaceGeoscapeScanner{"SurfaceGeoscapeScanner"};
inline constexpr Word SurfaceHackingReader{"SurfaceHackingReader"};
inline constexpr Word SurfaceObjectScanner{"SurfaceObjectScanner"};
inline constexpr Word SurfaceShotReader{"SurfaceShotReader"};
inline constexpr Word SurfaceTileReader{"SurfaceTileReader"};
inline constexpr Word SurfaceUnitReader{"SurfaceUnitReader"};
inline constexpr Word SurfaceSkyrangerReader{"SurfaceSkyrangerReader"};
inline constexpr Word SurfaceChosenReader{"SurfaceChosenReader"};
inline constexpr Word SurfaceBondReader{"SurfaceBondReader"};
inline constexpr Word SurfaceReportReader{"SurfaceReportReader"};
inline constexpr Word NavHelpUnavailable{"NavHelpUnavailable"};
inline constexpr Word EngineNone{"EngineNone"};
inline constexpr Word EngineTtsUnavailable{"EngineTtsUnavailable"};
inline constexpr Word EngineBackendUnavailable{"EngineBackendUnavailable"};
inline constexpr Word EngineLabel{"EngineLabel"};
inline constexpr Word EngineFeatures{"EngineFeatures"};
inline constexpr Word EngineSpeech{"EngineSpeech"};
inline constexpr Word EngineBraille{"EngineBraille"};
inline constexpr Word EngineRate{"EngineRate"};
inline constexpr Word EngineVolume{"EngineVolume"};
inline constexpr Word HackCancelled{"HackCancelled"};
inline constexpr Word StartupActive{"StartupActive"};

inline const Word& CountForm(int n, const Word& one, const Word& few, const Word& many)
{
    if (std::string(Lookup("PluralRule")) == "slavic") {
        int lastTwo = std::abs(n) % 100;
        int last = lastTwo % 10;
        if (lastTwo < 11 || lastTwo > 14) {
            if (last == 1) return one;
            if (last >= 2 && last <= 4) return few;
        }
        return many;
    }
    return n == 1 ? one : many;
}

inline constexpr Word ScreenMainMenu{"ScreenMainMenu"};
inline constexpr Word ScreenOptionsFallback{"ScreenOptionsFallback"};
// Mod Config Menu (docs/mod-config-menu-recon.md). The screen carries its own localized title
// (m_strTitle, read by name off the mod class) and reaches the first only when that read is
// empty. The second is OURS, for a state the screen shows with no words: the right-hand page is
// EMPTY until a tab is clicked (SelectedPageID starts at -1), and a sighted player sees a blank
// panel where a blind one would hear a stop with nothing in it — indistinguishable from a page
// we failed to read — so the stop says what the blank means.
inline constexpr Word ScreenModConfigFallback{"ScreenModConfigFallback"};
inline constexpr Word ModConfigNoPage{"ModConfigNoPage"};
inline constexpr Word RoleButton{"RoleButton"};
inline constexpr Word RoleTicker{"RoleTicker"};
inline constexpr Word RoleTab{"RoleTab"};
// A region emitted by GraphSheet with column headings, announced once on entry so a player knows
// Left/Right will step across facts rather than out of the list.
inline constexpr Word RoleTable{"RoleTable"};
/// An empty CELL in such a region. It must say something: silence there is indistinguishable
/// from a cell that failed to resolve, or from no cell at all.
inline constexpr Word TableBlank{"TableBlank"};
inline constexpr Word RoleCheckbox{"RoleCheckbox"};
inline constexpr Word RoleSlider{"RoleSlider"};
inline constexpr Word RoleSpinner{"RoleSpinner"};
inline constexpr Word RoleDropdown{"RoleDropdown"};
inline constexpr Word Selected{"Selected"};
inline constexpr Word Disabled{"Disabled"};

// Checkbox states — the same words the legacy UnrealScript narration used, so the switch to
// the native path doesn't change what the player hears.
inline constexpr Word Checked{"Checked"};
inline constexpr Word Unchecked{"Unchecked"};

// The game's "!" badge (UIButton.bNeedsAttention) — what a sighted player reads as
// "something new here" (a new challenge, unplayed Legacy content).
inline constexpr Word Attention{"Attention"};

// My2K account button (bottom right of the shell) and its link states — same three states
// the game's own status text distinguishes.
inline constexpr Word My2KAccount{"My2KAccount"};
inline constexpr Word My2KLinked{"My2KLinked"};
inline constexpr Word My2KNotLinked{"My2KNotLinked"};
inline constexpr Word My2KOffline{"My2KOffline"};

// ---- new game flow ----

// Screen names for the campaign-setup screens. The difficulty and narrative screens carry
// their own localized titles and only fall back to these; the Ironman screen and the modal
// dialog have no title property of their own worth trusting, so these name them outright.
inline constexpr Word ScreenDifficultyFallback{"ScreenDifficultyFallback"};
inline std::string ScreenIronManFallback() { return GameString("UIShellDifficulty", "m_strDifficultyEnableIronman", ScreenUnnamed); }
inline std::string ScreenNarrativeFallback() { return GameString("UIShellNarrativeContent", "NarrativeContentTitle", ScreenUnnamed); }
inline constexpr Word ScreenSoundtrackFallback{"ScreenSoundtrackFallback"};
inline constexpr Word ScreenDialog{"ScreenDialog"};
// The Tactical Legacy Pack hub carries its own localized title; this names it only when that
// read came back empty.
inline std::string ScreenTLEHubFallback() { return GameString("UITLEHub", "m_strTitle", ScreenUnnamed); }
// The Legacy Operations menu, its difficulty page and the soldier loadout popup carry their own
// localized titles (the difficulty page's through the companion); these name them only when the
// read came back empty.
inline std::string ScreenLadderMenuFallback() { return GameString("UITLE_LadderModeMenu", "m_ScreenTitle", ScreenUnnamed); }
inline constexpr Word ScreenLadderDifficultyFallback{"ScreenLadderDifficultyFallback"};
inline std::string ScreenUnitStatsFallback() { return GameString("UITLEUnitStats", "m_ScreenTitle", ScreenUnnamed); }
// A ladder row whose name neither the companion nor the widget supplied.
inline std::string LadderRowFallback(int n) { return Format(LadderRowFormat, std::to_string(n)); }
// A medal threshold on the ladder panel the score has reached (the game lights the icon).
inline constexpr Word MedalEarned{"MedalEarned"};
// A gear slot the loadout popup draws with nothing in it.
inline constexpr Word EmptySlot{"EmptySlot"};
// The loadout popup's PCS slot has an icon and no caption.
inline constexpr Word PCSLabel{"PCSLabel"};
// The Challenge Archives menu carries its own localized title; this names it only when the
// read came back empty.
inline std::string ScreenChallengeArchiveFallback() { return GameString("UITLE_ChallengeModeMenu", "m_ScreenTitle", ScreenUnnamed); }
// The Avenger Report (UIFacilitySummary) likewise: its own m_strTitle, from the class default when
// the live read came back empty.
inline std::string ScreenFacilitySummaryFallback() { return GameString("UIFacilitySummary", "m_strTitle", ScreenUnnamed); }
// The Skirmish Mode menu carries its own title and START GAME word; fallbacks for empty reads.
inline std::string ScreenSkirmishFallback() { return GameString("UITLE_SkirmishModeMenu", "m_ScreenTitle", ScreenUnnamed); }
inline std::string SkirmishStartFallback() { return GameString("UITLE_SkirmishModeMenu", "m_StartGame", RoleButton); }
// The Legacy Pack's mission-side screens (recipe_tle_mission.cpp): each carries its own
// localized header; these name them only when the read came back empty.
inline constexpr Word ScreenScoringFallback{"ScreenScoringFallback"};
inline constexpr Word ScreenLadderUpgradeFallback{"ScreenLadderUpgradeFallback"};
inline constexpr Word ScreenLadderSquadFallback{"ScreenLadderSquadFallback"};
inline constexpr Word ScreenLadderMedalFallback{"ScreenLadderMedalFallback"};
inline constexpr Word ScreenMedalProgressFallback{"ScreenMedalProgressFallback"};
inline constexpr Word ScreenLadderEndFallback{"ScreenLadderEndFallback"};
inline constexpr Word ScreenChallengePostFallback{"ScreenChallengePostFallback"};
inline constexpr Word ScreenChallengeEventFallback{"ScreenChallengeEventFallback"};
// The medal screen's rewards block, drawn dim when a previous run already earned them.
inline constexpr Word RewardAlreadyUnlocked{"RewardAlreadyUnlocked"};

// The modal popup family (docs/popup-inventory.md). The popup's own title and message ride in
// the graph, so the screen name only announces that a modal has appeared.
inline constexpr Word ScreenPopup{"ScreenPopup"};
// ⚠ These three are OURS, not the game's, and each is a last resort rather than a normal
// reading. PopupNoText fires when a popup was recognised but nothing readable came back — worth
// saying, because silence there is indistinguishable from no popup at all while the player still
// has to press something to escape it. The other two are the accept/cancel pair offered when
// neither the companion nor the Flash panel yielded a button label: a modal a blind player
// cannot dismiss is worse than one that reads badly. If a live run shows these often, the fix is
// a missing field name in the alert table, not better wording here.
inline constexpr Word PopupNoText{"PopupNoText"};
inline std::string PopupContinue() { return GameString("UIUtilities_Text", "m_strGenericContinue", RoleButton); }
inline constexpr Word PopupDismiss{"PopupDismiss"};

// The mission blade family (docs/mission-blade-recon.md). Every blade carries its own localized
// heading (the mission-type title plus the region) and the recipe reads that, so the fallback is
// reached only when the companion answered nothing for a recognised blade.
inline constexpr Word ScreenMissionBlade{"ScreenMissionBlade"};
// Same last-resort family as the popup's: a blade whose wire came back empty still holds the
// keyboard, so the player is told a blade is up and offered the screen's own accept/cancel keys.
inline constexpr Word MissionBladeNoText{"MissionBladeNoText"};

// The research report (docs/research-report-recon.md). The screen pushes its own localized
// "RESEARCH REPORT" and we read that, so the fallback is reached only when the Flash capture
// found nothing at all.
inline std::string ScreenResearchReport() { return GameString("XComHQPresentationLayer", "m_strResearchReportTitle", ScreenUnnamed); }
/// ⛔ The report's close key depends on the screen stack, so its BUTTON must too — one of these
/// two, never both. The game's own words are unreachable: AddContinueButton spawns a Flash
/// X2ContinueButton whose label lives in the movie, and on mouse-active PC the back button is an
/// icon rather than text (UINavigationHelp.uc:99-107).
inline std::string ReportContinue() { return GameString("UIUtilities_Text", "m_strGenericContinue", RoleButton); }
inline std::string ReportBack() { return GameString("UIUtilities_Text", "m_strGenericBack", RoleButton); }
/// The capture came back empty on a screen we recognised. Said out loud for the same reason
/// PopupNoText is: the player is looking at a modal they still have to dismiss, and silence
/// there is indistinguishable from no screen at all.
inline constexpr Word ReportNoText{"ReportNoText"};

// New research available (docs/research-unlocked-recon.md). Same arrangement as the report: the
// screen's own localized "RESEARCH AVAILABLE" rides in every card's payload and names the screen;
// this fallback is reached only when the capture found nothing. The continue button and the
// empty-capture line reuse the report's words above — same NavHelp widget, same failure.
inline constexpr Word ScreenResearchUnlocked{"ScreenResearchUnlocked"};

// The in-game Escape menu. It carries its own localized title and only falls back to this.
inline std::string ScreenPauseMenuFallback() { return GameString("UIPauseMenu", "m_sPauseMenu", ScreenUnnamed); }

// Squad select. The screen is named by its own operation name ("Операция ...") and reaches these
// only in the window between attaching and the companion answering — or, for the two below, when
// a card is present that the companion could not describe at all.
inline constexpr Word ScreenSquadSelectFallback{"ScreenSquadSelectFallback"};
inline constexpr Word SoldierFallback{"SoldierFallback"};
inline constexpr Word UtilitySlotFallback{"UtilitySlotFallback"};
inline constexpr Word ScreenLoadoutFallback{"ScreenLoadoutFallback"};
/// An equipped slot the soldier is carrying nothing in. The screen draws the slot's name over an
/// empty box, and the name alone would read the same as a filled slot whose item we failed to
/// resolve — so the emptiness is said out loud.
inline constexpr Word LoadoutEmptySlot{"LoadoutEmptySlot"};

// After action (docs/after-action-recon.md). The screen names itself with its own header +
// operation name, and — unlike the research report, whose Continue really is unreachable — the
// companion reaches the game's own m_strGenericContinue as a class default, so both fallbacks
// fire only in the window between attaching and the companion answering.
inline constexpr Word ScreenAfterActionFallback{"ScreenAfterActionFallback"};
inline std::string AfterActionContinueFallback() { return GameString("UIUtilities_Text", "m_strGenericContinue", RoleButton); }

// The mission bookends (docs/mission-summary-recon.md) — the two windows that open the instant a
// mission ends, ahead of the after-action walk-up. Both name themselves out of their own Flash
// push (the result headline, "CHOSEN ENCOUNTER"), so these two are reached only when the capture
// came back with nothing at all.
inline constexpr Word ScreenMissionSummaryFallback{"ScreenMissionSummaryFallback"};
inline std::string ScreenChosenSummaryFallback() { return GameString("UIChosenMissionSummary", "ChosenEncounterTitle", ScreenUnnamed); }
/// The mission summary's Continue. Unlike the research report's, the game's own word IS
/// reachable — XComGame.int populates m_strContinue under [UIMissionSummary] — so this fires
/// only if that field reads empty.
inline std::string SummaryContinueFallback() { return GameString("UIUtilities_Text", "m_strGenericContinue", RoleButton); }
/// Recognised the screen, captured nothing. Said out loud for ReportNoText's reason: the player
/// is looking at a window they still have to dismiss, and silence there is indistinguishable
/// from no screen at all. ⚠ It must NOT read as "the mission had no result" — hence "readout",
/// which names OUR failure rather than the game's outcome.
inline constexpr Word SummaryNoText{"SummaryNoText"};

// Save / load. Both screens carry their own localized title and reach these only if it reads
// empty. The other three are the layer's own words for states the screen shows without text:
// an empty list, a save or load in flight, and the key that reaches the row's delete verb —
// the one affordance the graph does not carry as a node (docs/save-load-recon.md §2).
//
// BACKSPACE, not Delete, even though the screen's RawInputHandler takes either: everywhere
// else in this mod Delete means RE-ANNOUNCE (tile cursor, shot HUD, hacking pad, ant farm), so
// naming it here would teach the one exception to a rule the player has learned everywhere
// else — and get a save deleted by someone reaching for a re-read.
inline std::string ScreenSaveFallback() { return GameString("UISaveGame", "m_sSaveTitle", ScreenUnnamed); }
inline std::string ScreenLoadFallback() { return GameString("UILoadGame", "m_sLoadTitle", ScreenUnnamed); }
inline constexpr Word SaveLoadEmpty{"SaveLoadEmpty"};
inline constexpr Word SaveLoadBusy{"SaveLoadBusy"};
inline constexpr Word KeyBackspace{"KeyBackspace"};

// Inside a room on the Avenger. A room's verbs come from two places: its own bottom bar, and
// the game's shortcut submenu on the HUD screen BELOW it. "Nothing here" is legitimate when a
// room has neither — but it must never be said of a room that has one, which is what it WAS
// said of the Bar and the Living Quarters (their shortcut category is -1, and both have a
// bottom-bar button).
inline constexpr Word ScreenRoomFallback{"ScreenRoomFallback"};
inline constexpr Word RoomNoActions{"RoomNoActions"};
// A bottom-bar button no shipped facility class declares a label for — i.e. a second button
// added by a mod. The widget retains no text of its own, so there is nothing else to say.
inline constexpr Word RoomFunctionFallback{"RoomFunctionFallback"};
// The crew line the staff container carries even when it is hidden: "SCIENTISTS: 0". Both
// halves come off the widget already localized; this is only the joint between them.
inline constexpr const char* RoomCrewSeparator = ": ";
// bIsBad / bIsWarning on a submenu row — the game colours these red and amber, which is a fact
// a sighted player gets for free and speech otherwise loses entirely.
inline constexpr Word RoomUrgent{"RoomUrgent"};
inline constexpr Word RoomWarning{"RoomWarning"};

// An un-excavated room (UIRoom): the engineer slot and the excavate button, neither of which
// vanilla lets a keyboard reach. "No one available" is a REASON, never a refusal — the game
// greys the slot but still opens its picker, and the picker is where a filled slot is emptied.
inline constexpr Word RoomNoStaff{"RoomNoStaff"};
inline constexpr Word RoomPickStaff{"RoomPickStaff"};
// A facility staff slot the game draws padlocked: it still takes Enter (HandleClick opens the
// upgrade screen that unlocks it), so this is a state word rather than Vocab::Disabled. The
// WHY is the game's own m_strSlotLockedTooltip, carried by the companion; this is only the
// one-word state a sighted player reads off the padlock.
inline constexpr Word RoomSlotLocked{"RoomSlotLocked"};
// The choose-upgrade screen's fallback name, reached only in the window between attaching and
// the companion answering with the screen's own m_strTitle.
inline constexpr Word ScreenChooseUpgradeFallback{"ScreenChooseUpgradeFallback"};

// The commodity list (research, build items, the archives, the market). Almost every word on
// that screen is the game's own — the row names, the card, both cost labels — so this is a short
// table on purpose. "Cannot afford" is the row's bIsBad tint, which the game shows in colour and
// speech would otherwise drop; the two empty-list lines are for a state the screen names in its
// own subtitle but leaves with nothing to stand on.
inline constexpr Word ScreenCommodityFallback{"ScreenCommodityFallback"};
inline constexpr Word CommodityEmpty{"CommodityEmpty"};
inline constexpr Word CommodityLeaveHint{"CommodityLeaveHint"};
inline constexpr Word CommodityUnaffordable{"CommodityUnaffordable"};
inline constexpr Word CommodityOwned{"CommodityOwned"};
/// The card's own answer to WHICH cost item is stopping the build: GetStrategyCostString draws
/// the ones the HQ cannot pay in red and says nothing in words, so the companion reads them back
/// off the colour (the `short` field) and this trails them as a line of their own, after the
/// full cost. The row's bIsBad says the item is unaffordable; this says why.
inline constexpr Word CommodityShortfall{"CommodityShortfall"};
/// The PCS picker's bIsBad (UIInventory_Implants.CanEquipImplant): the same red as the price
/// tint elsewhere in the family, meaning a weaker implant of a stat already boosted, or a psi
/// implant on a soldier who is not a psi operative. The game states neither in words.
inline constexpr Word ImplantCannotEquip{"ImplantCannotEquip"};

// The black market's two non-commodity screens. The entry menu names itself (m_strTitle) and the
// trading post takes the entry menu's own SELL INVENTORY label through the companion, so both
// fallbacks fire only in the window before those reads answer; the third is a sell row whose
// name the companion had not supplied. Every other word on both screens is the game's — the
// button labels, the greyed-out reasons, the column headings, the credits prefix.
inline std::string ScreenBlackMarketFallback() { return GameString("UIBlackMarket", "m_strTitle", ScreenUnnamed); }
inline constexpr Word ScreenBlackMarketSellFallback{"ScreenBlackMarketSellFallback"};
inline constexpr Word SellItemFallback{"SellItemFallback"};

// Choosing what to build in a room (UIChooseFacility). It carries its own localized title and
// list heading and reaches this only in the window between attaching and the companion answering.
inline std::string ScreenChooseFacilityFallback() { return GameString("XGBuildUI", "m_strLabelBuildFacility", ScreenUnnamed); }

// ---- the resource strip (UIAvengerHUD.ResourceContainer) ----
//
// Three words, and only three, because every resource NAME and every VALUE on that strip is the
// game's own — captured as it was drawn. What the game says in colour and speech cannot is the
// STATE, and these are that translation: `UIUtilities_Colors`' WARNING (yellow) and BAD (red),
// which on this strip mean a resource running out, contacts nearly or fully spent, and power
// over budget. Deliberately generic, because the colour is: the game paints one yellow and one
// red across ten different resources and expects the player to know what each means in context.
//
// ⛔ These are OURS and therefore English, which is wrong the moment the player is not — the same
// debt every table in this file carries. They are keyed by COLOUR rather than by resource, which
// is what keeps a missing entry a missing adjective instead of a shifted one.
inline constexpr Word ResourceLow{"ResourceLow"};
inline constexpr Word ResourceCritical{"ResourceCritical"};
// The stop's own name. The game never labels the strip as a group — to a sighted player it is a
// band across the top, and its grouping is spatial — so this word has no localized source.
inline constexpr Word Resources{"Resources"};
// Said only by the HOTKEY, and only in the one state the stop cannot reach: the strip is ON
// SCREEN and the Flash capture has nothing for it, because the game paints the strip on a screen
// change and nothing has changed screens since the capture acquired that panel. Silence there
// would say "there are no resources" — a different and false statement — so this says what is
// actually true, and the tick asks the HUD to repaint so the next press is answered properly.
inline constexpr Word ResourcesNotRead{"ResourcesNotRead"};

// ---- covert actions (docs/covert-actions-recon.md) ----
//
// Nearly every word on this screen is the game's own: the row text is retained on the widget, and
// the card, the risks and the slots come off the screen's own accessors through the companion.
// The screen-name fallback is ours; the committed action's marker while the list only browses is
// the generic Selected.
inline std::string ScreenCovertActions() { return GameString("UIFacility_CovertActions", "m_strViewActions", ScreenUnnamed); }

// ---- resistance orders (UIStrategyPolicy) ----
//
// The screen carries no title of its own — DeckTitle names the HAND, not the board — so this one
// is ours outright. The other three each name something the screen draws with no words at all.
inline std::string ScreenPolicy() { return GameString("UIStrategyMap", "m_srResistanceOrders", ScreenUnnamed); }
/// A slot with nothing in it. The game draws an empty card frame and gives it no text whatever
/// (RealizeColumn hints only the wild-card column's blanks), and an unnamed node is a control
/// that silently ceases to exist — on the one screen where the empty slots are the whole point.
inline constexpr Word PolicyEmptySlot{"PolicyEmptySlot"};
/// The card currently picked up. UIStrategyPolicy.SelectCard is a two-step state machine over
/// InspectingCard, and a sighted player sees which card is held because it is dragged under the
/// cursor. With no drag there is nothing to see, so the state is said.
inline constexpr Word PolicyHeld{"PolicyHeld"};
/// ⛔ Said ONCE, on the screen name, rather than on each of a dozen cards. Outside the monthly
/// report bEnableDragging is false and every card refuses — a sighted player discovers that by
/// clicking and reading the popup, which is cheap for them and costs a blind player a modal per
/// attempt. The cards keep their activation anyway, so pressing one still raises the game's own
/// explanation.
inline constexpr Word PolicyReadOnly{"PolicyReadOnly"};
/// While a card is held, a board slot the game would ACCEPT it into. The game shows this — legal
/// drop spots highlight while a drag is live — so saying it is parity. Value part, spoken after
/// the slot's own words.
inline constexpr Word PolicyDropTarget{"PolicyDropTarget"};
/// The refusal SelectCard makes silently: its final `else` plays a negative click and says
/// nothing, which a blind player cannot tell from a dead key. Spoken on activation, and the verb
/// is still sent — the game stays the authority, this is a caption on its refusal, not a gate.
inline constexpr Word PolicyCantPlace{"PolicyCantPlace"};
/// Enter with empty hands on a slot that has nothing to pick up (a blank or a locked slot):
/// CanDragCard() refuses and the companion returns without a word.
inline constexpr Word PolicyCantPick{"PolicyCantPick"};
/// Enter with empty hands on a VITAL card (MarkCardAsSpecial) — there IS a card, it just cannot
/// be moved, so "nothing to pick up" would be a lie.
inline constexpr Word PolicyCantMove{"PolicyCantMove"};
/// Backspace while holding: the held card returned to where it was (ClearInspector). The game's
/// controller build puts this on B/Escape (UIStrategyPolicy.uc:1253); vanilla mouse+keyboard has
/// no way to say it at all.
inline constexpr Word PolicyPutBack{"PolicyPutBack"};

// The personnel roster. Almost every word there is the game's own — the rows are re-derived from
// the unit, the tab names and the sort captions are localized strings off the screen, and even
// the empty-list line is the one PopulateListInstantly pushes into Flash. These five are the
// joints and the fallbacks.
//
// The sort words exist because the header draws its state as an ARROW: a little triangle beside
// one column, pointing up or down. That is two facts (which column sorts, and in which
// direction) carried entirely by a glyph, so speech has to supply the words for both.
inline constexpr Word ScreenPersonnelFallback{"ScreenPersonnelFallback"};
inline constexpr Word PersonnelEmpty{"PersonnelEmpty"};
// The row's promotion badge — an icon, and therefore a whole affordance speech loses by default.
inline constexpr Word PersonnelPromote{"PersonnelPromote"};

// ---- promotion / level-up ----
inline constexpr Word ScreenPromotion{"ScreenPromotion"};
// A rank the companion could not name. It still gets a row: an unnamed stop a player can move
// through beats a rank that is silently not there.
inline constexpr Word PromoRankFallback{"PromoRankFallback"};
// The four states an ability slot can be in. The screen carries every one of them as ICON
// COLOUR alone — "available" and "unaffordable" are BOTH eUIPromotionState_Normal and differ
// only in the foreground colour — so these words exist because that channel has no sound.
inline constexpr Word PromoOwned{"PromoOwned"};
inline constexpr Word PromoAvailable{"PromoAvailable"};
inline constexpr Word PromoUnaffordable{"PromoUnaffordable"};
inline constexpr Word PromoPrereq{"PromoPrereq"};
inline constexpr Word PromoSpent{"PromoSpent"};
inline constexpr Word PersonnelSortBy{"PersonnelSortBy"};
inline constexpr Word SortAscending{"SortAscending"};
inline constexpr Word SortDescending{"SortDescending"};

// ---- the Chosen dossier and the bond screens (docs/chosen-bond-recon.md) ----
//
// Every word on these three screens is the game's own, re-derived by the companion from the state
// the screen cached. These are the attach-window fallbacks and the words for what the game draws
// with no text at all: a bond level as a digit inside an icon, a meter filled to a level.
inline constexpr Word ScreenChosenInfoFallback{"ScreenChosenInfoFallback"};
inline std::string ScreenBondsFallback() { return GameString("XLocalizedData", "SoldierBondTutorialTitle", ScreenUnnamed); }
inline std::string ScreenBondConfirmFallback() { return GameString("UISoldierBondConfirmScreen", "m_strTitle", ScreenUnnamed); }
/// The Skyranger-arrived prompt before the companion answers; its real name is the screen's own
/// two localized blade strings.
inline constexpr Word ScreenSkyrangerArrivesFallback{"ScreenSkyrangerArrivesFallback"};
/// The bond roster with nobody else on it (a squad of one). The game draws an empty list and no
/// line, and an empty stop is indistinguishable from a broken one.
inline constexpr Word BondsEmpty{"BondsEmpty"};
/// The CREATE BOND button's label is the row class's own localized string and rides the wire;
/// this is only for the window before the companion answers.
inline constexpr Word BondCreateFallback{"BondCreateFallback"};
/// The bondmate panel's level, which Flash draws as a digit inside the bond icon.
inline std::string BondLevel(int level)
{
    return Format(BondLevelFormat, std::to_string(level));
}
/// A meter the game fills to one of a few discrete steps under a heading and no text (the
/// dossier's HUNT PROGRESS). Not Position(): "1 of 3" after a heading reads as a menu position.
inline std::string MeterLevel(int level, int max)
{
    return Format(MeterLevelFormat, std::to_string(level), std::to_string(max));
}

// ---- the monthly report and its Chosen page (docs/resistance-report-recon.md) ----
//
// Both screens name themselves from their own localized titles; these are the attach-window
// fallbacks. The Avatar meter is drawn as pips with only its heading for text.
inline constexpr Word ScreenMonthlyReportFallback{"ScreenMonthlyReportFallback"};
inline std::string ScreenChosenEventsFallback() { return GameString("UIResistanceReport_ChosenEvents", "ChosenActivityTitle", ScreenUnnamed); }
/// A count against its maximum where the game draws pips (Avatar progress). Not Position() —
/// "3 of 12" after a heading reads as a menu position — and not MeterLevel, which names a step.
inline std::string Fraction(int value, int max)
{
    return Format(FractionFormat, std::to_string(value), std::to_string(max));
}

// ---- recruiting (UIRecruitSoldiers) ----
//
// The screen names itself (m_strListTitle) and draws its own empty-list line (m_strNoRecruits);
// both fallbacks fire only in the window before the companion answers. Every other word — the
// names, the countries, the cost and the bio — is the game's own.
inline constexpr Word ScreenRecruitsFallback{"ScreenRecruitsFallback"};
inline constexpr Word RecruitsEmpty{"RecruitsEmpty"};

// ---- soldier customization, manage equipment, the character pool (docs/customize-recon.md) ----
//
// Nearly every word on these screens is the game's own: the row labels and the current choices
// are retained on the mecha rows, the titles are the screens' localized strings, the soldier
// line rides the wire. What the game states with NO words is a colour — a swatch on a row, a
// grid of chips in the selector — and that is the one vocabulary this section adds.
inline constexpr Word ScreenCustomizeFallback{"ScreenCustomizeFallback"};
/// The option picker names itself by the row that opened it (HAIR, VOICE, NATIONALITY…) — the
/// game keeps no title on it at all. This is the window before that read lands.
inline constexpr Word ScreenCustomizeOptionsFallback{"ScreenCustomizeOptionsFallback"};
/// The colour selector's grid, which the game likewise titles only by the row that opened it.
inline constexpr Word CustomizeColours{"CustomizeColours"};
/// A chip whose colour string did not parse. It keeps its node — an unnamed chip is a colour
/// that silently ceases to exist — and its drawn number still identifies it.
inline constexpr Word ColourUnknown{"ColourUnknown"};
inline std::string ScreenManageEquipmentFallback() { return GameString("UIManageEquipmentMenu", "m_strTitleLabel", ScreenUnnamed); }
inline constexpr Word ScreenCharacterPoolFallback{"ScreenCharacterPoolFallback"};
inline constexpr Word ScreenPoolFilesFallback{"ScreenPoolFilesFallback"};
inline constexpr Word PoolEmptyFallback{"PoolEmptyFallback"};
inline constexpr Word PoolEditFallback{"PoolEditFallback"};
/// The pool-files list with nothing in it — a file with no importable characters drops even
/// its IMPORT ENTIRE POOL row (GetImportList empties the array), and an empty stop is
/// indistinguishable from a broken one.
inline constexpr Word PoolFilesEmpty{"PoolFilesEmpty"};

/// ⛔ AN AID, and documented as one: a colour swatch carries no words for anyone, but a sighted
/// player sees the hue and a blind one hears nothing. Eight hue bands and three lightness bands,
/// coarse on purpose — "dark red" is honest where "crimson" would be a guess. `hex` is the game's
/// own Flash colour string ("0x00RRGGBB" from LinearColorToFlashHex; a bare or '#'-prefixed
/// triplet is accepted too). Empty when the string does not parse.
inline std::string ColourName(const std::string& hex)
{
    std::string digits;
    for (char c : hex) {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            digits += c;
    }
    if (digits.size() < 6)
        return std::string();
    digits = digits.substr(digits.size() - 6);
    const unsigned long rgb = std::strtoul(digits.c_str(), nullptr, 16);
    const double r = ((rgb >> 16) & 0xFF) / 255.0;
    const double g = ((rgb >> 8) & 0xFF) / 255.0;
    const double b = (rgb & 0xFF) / 255.0;
    const double mx = std::max(r, std::max(g, b));
    const double mn = std::min(r, std::min(g, b));
    const double l = (mx + mn) / 2.0;
    const double d = mx - mn;
    const double s = d <= 0.0 ? 0.0 : d / (1.0 - std::fabs(2.0 * l - 1.0));

    if (l < 0.06)
        return ColourBlack;
    if (l > 0.96)
        return ColourWhite;
    if (s < 0.12) {
        if (l < 0.3)
            return std::string(ColourDarkPrefix) + ColourGrey;
        if (l > 0.7)
            return std::string(ColourLightPrefix) + ColourGrey;
        return ColourGrey;
    }

    double h;
    if (mx == r)
        h = std::fmod((g - b) / d, 6.0);
    else if (mx == g)
        h = (b - r) / d + 2.0;
    else
        h = (r - g) / d + 4.0;
    h *= 60.0;
    if (h < 0.0)
        h += 360.0;

    const Word* hue;
    if (h < 15.0 || h >= 345.0)
        hue = &ColourRed;
    else if (h < 45.0)
        hue = &ColourOrange;
    else if (h < 70.0)
        hue = &ColourYellow;
    else if (h < 165.0)
        hue = &ColourGreen;
    else if (h < 195.0)
        hue = &ColourTeal;
    else if (h < 255.0)
        hue = &ColourBlue;
    else if (h < 290.0)
        hue = &ColourPurple;
    else
        hue = &ColourPink;

    // A dark orange or yellow is what everyone calls brown.
    if (l < 0.3 && (h >= 15.0 && h < 70.0))
        return s < 0.35 ? std::string(ColourGreyishBrown) : std::string(ColourBrown);

    std::string name;
    if (l < 0.3)
        name = ColourDarkPrefix;
    else if (l > 0.7)
        name = ColourLightPrefix;
    if (s < 0.35)
        name += std::string(ColourGreyishPrefix);
    name += std::string(*hue);
    return name;
}

// ---- the post-mission rewards recap (UIRewardsRecap) ----
//
// The screen has no title of its own worth speaking (its heading is a "SECURE TRANSMISSION"
// greeble), so it is named by the operation the companion reads off it and falls back to this.
// The Continue is the nav help's, whose label lives in the movie, so it reuses the report's word.
inline constexpr Word ScreenRewardsRecapFallback{"ScreenRewardsRecapFallback"};
/// Recognised the screen, the companion answered nothing. Said for ReportNoText's reason: the
/// player still has to press Continue, and silence is indistinguishable from no screen at all.
inline constexpr Word RecapNoText{"RecapNoText"};

// ---- the covert action report (UICovertActionReport) ----
//
// The screen carries its own heading (COVERT ACTION REPORT:) and its Continue really is a
// retained UILargeButton label, so both of these are attach-window fallbacks only.
inline constexpr Word ScreenCovertReportFallback{"ScreenCovertReportFallback"};
inline constexpr Word CovertReportNoText{"CovertReportNoText"};
/// A results card whose label the companion could not resolve (its staff slot's state object was
/// gone). It still gets a node: the retained result lines on it are the point of the screen.
inline constexpr Word CovertReportCardFallback{"CovertReportCardFallback"};

// ---- the armory hub (UIArmory_MainMenu) ----
//
// The screen is named by the soldier (the companion's header line, as the loadout is); this is
// the attach window's word. The row fallback is for a mod-inserted row whose retained Text read
// empty — it still gets a node, since dropping it would renumber nothing but would hide a verb.
inline constexpr Word ScreenArmoryFallback{"ScreenArmoryFallback"};
inline constexpr Word ArmoryRowFallback{"ArmoryRowFallback"};

// ---- weapon upgrades (UIArmory_WeaponUpgrade) and the pattern picker (UIArmory_WeaponTrait) ----
//
// The screen is named by the weapon (the companion's header line); the picker by the heading its
// caller passed it. Both fallbacks fire only in the attach window. The stop words are ours: the
// game draws the stat block and the customize rows as unlabelled panels, and the two lists' titles
// are the game's own (carried by the companion) and so need no word here.
inline constexpr Word ScreenWeaponUpgradeFallback{"ScreenWeaponUpgradeFallback"};
inline constexpr Word ScreenWeaponPatternFallback{"ScreenWeaponPatternFallback"};
inline constexpr Word StopWeaponStats{"StopWeaponStats"};
/// The slots list while the upgrade picker is open — a read-only mirror the game has switched
/// off, as the loadout's equipped list is; the word tells the player why Enter does nothing there.
inline constexpr Word WeaponSlotsMirror{"WeaponSlotsMirror"};

// ---- the loot receipt (UIInventoryTactical) ----
//
// The screen names itself (m_strTitle, LOOT RECOVERY) and its OK is m_strGenericOK retained on the
// button, so both fallbacks fire only in the attach window. The empty line is ours: UpdateData
// draws a row only for items that reached the soldier's inventory and says nothing when none did.
inline constexpr Word ScreenLootFallback{"ScreenLootFallback"};
inline constexpr Word LootEmpty{"LootEmpty"};
inline std::string LootOkFallback() { return GameString("UIUtilities_Text", "m_strGenericOK", RoleButton); }

// ---- soldier info (UITacticalCharInfoScreen) ----
//
// The screen has no title of its own — its heading IS the soldier's name — so this is the attach
// window's word. SoldierInfoFallback names the HUD graph's button when the HUD's own hint label
// (m_strNavHelpCharShow) reads empty. CharInfoNoValue is what a stat the screen draws as "---"
// (its own marker for zero) is said as; dashes read as dashes.
inline std::string ScreenCharInfoFallback() { return GameString("UITacticalHUD_MouseControls", "m_strSoldierInfo", ScreenUnnamed); }
inline std::string SoldierInfoFallback() { return GameString("UITacticalHUD_MouseControls", "m_strSoldierInfo", RoleButton); }
inline constexpr Word CharInfoNoValue{"CharInfoNoValue"};

// The geoscape's mission tray. Almost nothing here is ours: each icon's Title and Body are the
// game's own hover tooltip, already localized and already exactly what a sighted player reads.
//
// The one exception is the placeholder slot, and it has no localized source at all: the game
// draws a Faction_unknown icon for each of the three factions not yet met and
// SetMissionIconTooltip early-returns for them, so there is no string in the game to borrow.
// A sighted player still SEES those three slots, which is why they get a stop rather than being
// filtered out — the wording is ours only because the game wrote none.
// Escape, not Tab, even though UIStrategyMap answers both: Tab's case label falls through into
// Escape's own body inside `if(`notdefined(FINAL_RELEASE)), so it is a debug convenience rather
// than a shipped binding — and naming the key the whole game uses beats naming the odd one out.
inline std::string ScreenGeoscapeFallback() { return GameString("UIControllerMap", "m_sGeoscape", ScreenUnnamed); }
inline constexpr Word GeoscapeLeaveHint{"GeoscapeLeaveHint"};
// The resistance-network overlay is a map MODE, and a sighted player reads its state off the
// globe redrawing itself. The verb's own label ("ПЕРЕКЛЮЧИТЬ СЕТЬ СОПРОТИВЛЕНИЯ") names the
// action but not which way it is currently set, so speech supplies that.
inline constexpr Word GeoscapeResNetOn{"GeoscapeResNetOn"};
inline constexpr Word GeoscapeResNetOff{"GeoscapeResNetOff"};
inline constexpr Word GeoscapeTrayEmpty{"GeoscapeTrayEmpty"};
inline constexpr Word GeoscapeUnmetFaction{"GeoscapeUnmetFaction"};

// The region-actions stop: the two hotlink buttons a region pin draws (MAKE CONTACT, and the
// captionless radio-tower button), which vanilla reaches by mouse and controller only. The verb
// words, the cost, the time and the income all come from the game; only these three are ours.
//
// The stop's own name has no localized source — the buttons are drawn ON the pins and the game
// never labels the pair as a group, because to a sighted player their grouping is spatial.
inline constexpr Word GeoscapeRegionActions{"GeoscapeRegionActions"};
// Both are facts the game states in a channel speech does not have: the alert reddens a cost it
// cannot pay (UIAlert.BuildOutpostAlert colours it eUIState_Bad and disables the confirm), and
// the contact alert appends a whole sentence about Resistance Comms. Neither is on the PIN, so
// these are said where the choice is made rather than left to the alert that follows — the alert
// still says both, in the game's own words, one keypress later.
inline constexpr Word GeoscapeCannotAfford{"GeoscapeCannotAfford"};
inline constexpr Word GeoscapeNoComms{"GeoscapeNoComms"};

// The Avatar Project meter (docs/surfaces/strategy.md, "Avatar project meter"). Its title, the
// countdown's label and unit words, and the message the game writes over it when doom moves
// ("Alien Facility in West Africa", "SABOTAGE") are all the game's own. What the game states
// only as pips BLINKING is how many blocks just appeared or vanished, and that is the one
// vocabulary here. The title fallback is for the frame before the panel's localized string reads.
inline std::string DoomTitleFallback() { return GameString("UIStrategyMap_HUD", "m_strDoomTitle", ScreenUnnamed); }
/// The change a doom sequence just drew: blocks added blink in, blocks removed blink out.
inline std::string DoomDelta(int delta)
{
    return Format(delta > 0 ? DoomUpFormat : DoomDownFormat,
        std::to_string(delta > 0 ? delta : -delta));
}

// The text-entry dialog (naming a save). The field is a Flash TextField, so no screen reader
// sees the keystrokes and this layer has to echo them itself — hence the boundary words and the
// punctuation table below.
inline constexpr Word TextInputRole{"TextInputRole"};
inline constexpr Word TextInputEmpty{"TextInputEmpty"};
inline constexpr Word TextInputStart{"TextInputStart"};
inline constexpr Word TextInputEnd{"TextInputEnd"};
inline constexpr Word TextInputDeleted{"TextInputDeleted"};
inline constexpr Word TextInputConfirmHint{"TextInputConfirmHint"};

// Character echo. A LETTER or a digit is spoken as itself and the reader pronounces it; a lone
// punctuation mark is not — most screen readers drop it entirely at their default punctuation
// level, so an echo that just forwarded the character would go silent on exactly the keys a
// player is least sure they pressed. Null means "speak the character itself".
inline const char* PunctuationName(wchar_t ch)
{
    switch (ch) {
    case L' ': return PunctSpace.c_str();
    case L'!': return PunctExclamation.c_str();
    case L'"': return PunctQuote.c_str();
    case L'#': return PunctHash.c_str();
    case L'$': return PunctDollar.c_str();
    case L'%': return PunctPercent.c_str();
    case L'&': return PunctAmpersand.c_str();
    case L'\'': return PunctApostrophe.c_str();
    case L'(': return PunctLeftParen.c_str();
    case L')': return PunctRightParen.c_str();
    case L'*': return PunctStar.c_str();
    case L'+': return PunctPlus.c_str();
    case L',': return PunctComma.c_str();
    case L'-': return PunctDash.c_str();
    case L'.': return PunctDot.c_str();
    case L'/': return PunctSlash.c_str();
    case L':': return PunctColon.c_str();
    case L';': return PunctSemicolon.c_str();
    case L'<': return PunctLessThan.c_str();
    case L'=': return PunctEquals.c_str();
    case L'>': return PunctGreaterThan.c_str();
    case L'?': return PunctQuestion.c_str();
    case L'@': return PunctAt.c_str();
    case L'[': return PunctLeftBracket.c_str();
    case L'\\': return PunctBackslash.c_str();
    case L']': return PunctRightBracket.c_str();
    case L'^': return PunctCaret.c_str();
    case L'_': return PunctUnderscore.c_str();
    case L'`': return PunctBacktick.c_str();
    case L'{': return PunctLeftBrace.c_str();
    case L'|': return PunctBar.c_str();
    case L'}': return PunctRightBrace.c_str();
    case L'~': return PunctTilde.c_str();
    default: return nullptr;
    }
}

// Spoken while the game shows its loading overlay and the layer has nothing to read. A world
// change can take ten seconds or more, and unbroken silence there reads as a crash.
inline constexpr Word Loading{"Loading"};

// The two panels of the difficulty screen. The game draws them as separate blocks without
// headings; naming them is what makes Tab between them meaningful.
inline constexpr Word GroupDifficulty{"GroupDifficulty"};
inline constexpr Word GroupCampaignOptions{"GroupCampaignOptions"};

// ---- the tactical HUD graph (the ambient recipe, Tab) ----

// Spoken on Tab into the HUD layer, and by its refusal ("HUD unavailable.").
inline constexpr Word ScreenTacticalHud{"ScreenTacticalHud"};

// Spoken when Escape hands the keys back to the tile cursor. "Map" rather than
// "world mode" because it names what the arrows now drive, not an internal state.
inline constexpr Word TacticalWorldMode{"TacticalWorldMode"};

// The HUD graph's stop headings. Our own structure words, same standing as the role words above.
inline constexpr Word StopSoldier{"StopSoldier"};
inline std::string StopAbilities() { return GameString("XLocalizedData", "TacticalTextAbilitiesHeader", SectionUnnamed); }
inline constexpr Word StopCommands{"StopCommands"};
// The enemy strip (UITacticalHUD_Enemies). "Targets" rather than "Enemies" because the same strip
// carries a medkit's soldiers and a hack's terminals — it is the game's own name for the panel.
inline constexpr Word StopTargets{"StopTargets"};
// A head whose name the companion could not give. Never expected: DescribeNonUnitTarget already
// falls back to a word, so this is the wire-format's "field missing" answer, not the game's.
inline constexpr Word TargetFallback{"TargetFallback"};
inline std::string StopObjectives() { return GameString("UITacticalHUD_ObjectivesList", "m_strObjectivesListTitle", SectionUnnamed); }

// ---- the Avenger HUD graph (the ant farm's ambient recipe, Tab) ----
//
// The second ambient layer. Same shape as the tactical one: the game keeps the arrows (they
// walk the rooms, with the camera swoop and the room name the narrate-only reader speaks),
// Tab focuses the HUD's panels, Escape hands the arrows back.

// Spoken on entering the layer, and by its refusal ("Avenger HUD unavailable.").
inline constexpr Word ScreenAvengerHud{"ScreenAvengerHud"};
// Spoken when the keys go back to the grid — what the arrows now drive, as "Map" is for tactical.
inline constexpr Word AvengerRoomsMode{"AvengerRoomsMode"};
// Stop headings. The shortcut bar and the event queue draw no title of their own; the to-do
// strip is a row of icons. All three words are ours.
inline constexpr Word StopShortcuts{"StopShortcuts"};
inline constexpr Word StopToDo{"StopToDo"};
inline constexpr Word StopEvents{"StopEvents"};
// The Avenger objective list draws dark events under a second heading the widget retains
// (m_strDarkEventsListTitle); this is only the fallback for a list that gave none.
inline std::string StopDarkEventsFallback() { return GameString("UIControllerMap", "m_sDarkEvents", StopEvents); }
// An event-queue row whose ETA the game cannot compute prints "--" and tints the title amber —
// a build with no engineer, a project with nobody on it. Said in words rather than as dashes.
inline constexpr Word EventNoEstimate{"EventNoEstimate"};
// The hologlobe button on the nav help strip. Its label (m_strGeoscapeLabel) is the game's own
// word and is what is normally spoken; this is only for a nav help that has not populated it.
inline std::string GeoscapeButtonFallback() { return GameString("UIControllerMap", "m_sGeoscape", RoleButton); }

// An ability icon whose antenna text read empty (the game populates it only while the mouse is
// active, and a stale-cleared icon holds ""), so the node still gets a name a player can act on.
inline std::string AbilityFallback(int index)
{
    return Format(AbilityFallbackFormat, std::to_string(index + 1));
}

// The hotkey label drawn on an ability icon ("1".."0"), phrased so a bare digit at the end of
// the readout cannot be mistaken for a count or a position. The command buttons reuse it for
// their bound-key strings, which arrive already localized from the game's keybinding data.
inline std::string AbilityHotkey(const std::string& label)
{
    return Format(AbilityHotkeyFormat, label);
}

// A command button whose retained label read empty — same degradation as AbilityFallback, so the
// node still exists and still activates (the empty-answer trap: dropping it would silently
// renumber nothing, but it WOULD make a control cease to exist).
inline std::string CommandFallback(int index)
{
    return Format(CommandFallbackFormat, std::to_string(index + 1));
}

// ---- tutorial mission (Tutorial_01) ----

// The glyphs the game splices into tutorial text as <img src='...'> markup. Speech has to
// TRANSLATE these, never strip them: the icon sits exactly where the key name belongs, so
// dropping it leaves "Press , to go here" — a hole in the one sentence that matters.
//
// Only the mouse and arrow glyphs need naming. Everything the tutorial writes as %KEY:...%
// already resolves to a bracketed key name ("[Q]", "[TAB]") through UIUtilities_Input.HTML_KEYMAP
// and reads correctly as-is — and because that substitution is binding-aware, what we speak is
// the player's ACTUAL bound key.
inline constexpr Word IconLeftMouse{"IconLeftMouse"};
inline constexpr Word IconRightMouse{"IconRightMouse"};
inline constexpr Word IconMouseWheelClick{"IconMouseWheelClick"};
inline constexpr Word IconMouseWheelScroll{"IconMouseWheelScroll"};
inline constexpr Word IconMouseButton4{"IconMouseButton4"};
inline constexpr Word IconMouseButton5{"IconMouseButton5"};
inline constexpr Word IconArrowLeft{"IconArrowLeft"};
inline constexpr Word IconArrowRight{"IconArrowRight"};
inline constexpr Word IconArrowUp{"IconArrowUp"};
inline constexpr Word IconArrowDown{"IconArrowDown"};

// How to leave the tutorial's modal popup. Without this a blind player is told nothing about
// dismissing a screen that has taken over the game: the box is modal and consumes all input, its
// one button calls DisableNavigation() so keyboard focus can never reach it, and the layer
// deliberately leaves Enter/Space/Escape unclaimed so the game's own handler still gets them.
inline constexpr Word TutorialDismiss{"TutorialDismiss"};

// The tutorial refused the submission. The game's own answer is a negative sound and NO TEXT at
// all, so this line is the entire explanation a blind player gets for why nothing happened.
inline constexpr Word TutorialRejected{"TutorialRejected"};

// The re-read key found nothing to re-read.
inline constexpr Word TutorialNoMessage{"TutorialNoMessage"};

// The companion mod is not loaded, so the HUD blade's words cannot be recovered. Said only on a
// pressed key — never from the poll, which must not talk about its own failures.
inline constexpr Word TutorialReaderUnavailable{"TutorialReaderUnavailable"};

// "<Surface> unavailable." — what a surface says when `FindCompanionCDO` misses, i.e. when the
// companion `.u` did not load. That is this project's single most common deployment failure (see
// the memory note on XCOM 2 rewriting ModRootDirs and ActiveMods), so this one sentence is the
// mod's whole voice about it, and nine surfaces say it with their own name in front:
//
//   "Tile reader", "Object scanner", "Geoscape scanner", "Aim reader", "Shot reader",
//   "Unit reader", "Hacking reader", "Avenger reader", "Tutorial reader"
//
// The name stays at the call site — it is what tells the surfaces apart — and the SENTENCE lives
// here, so rewording it is one edit and localizing it is one entry rather than nine.
//
// The tutorial has a dedicated line; the other readers use this template with a localized
// surface name. Screen recipes pass their own localized title.
inline std::string SurfaceUnavailable(const std::string& surface)
{
    return Format(SurfaceUnavailableFormat, surface);
}


inline std::string Position(int index, int count)
{
    return Format(PositionFormat, std::to_string(index), std::to_string(count));
}

inline std::string Percent(int value)
{
    return Format(CountForm(value, PercentFormat, PercentFewFormat, PercentManyFormat),
        std::to_string(value));
}

// The Chosen's knowledge meter after an extraction. Flash draws a bar filling from one value to
// the other, so the DELTA is the whole content of that block and both ends are said; Percent()
// is reused so neither number can read as a count or a position.
inline std::string KnowledgeChange(int before, int after)
{
    return Format(KnowledgeChangeFormat, Percent(before), Percent(after));
}

// Fallback tab name for when the screen's localized string reads empty (should not happen on a
// live screen; guards against a half-constructed read).
inline std::string TabFallback(int index)
{
    return Format(TabFallbackFormat, std::to_string(index + 1));
}

} // namespace Vocab
