#pragma once

// The vocabulary module (spec A8): every string the LAYER itself speaks routes through here —
// hardcoded English at call sites is forbidden, because it is unfindable later. Game content
// (button labels, log lines) is already localized and passes through untouched. When P7
// localization lands, this module grows a string table behind the same names.

#include <string>

namespace Vocab {

inline constexpr const char* ScreenMainMenu = "Main menu";
inline constexpr const char* ScreenOptionsFallback = "Settings";
inline constexpr const char* RoleButton = "button";
inline constexpr const char* RoleTicker = "news ticker";
inline constexpr const char* RoleTab = "tab";
// A region emitted by GraphSheet with column headings, announced once on entry so a player knows
// Left/Right will step across facts rather than out of the list.
inline constexpr const char* RoleTable = "table";
/// An empty CELL in such a region. It must say something: silence there is indistinguishable
/// from a cell that failed to resolve, or from no cell at all.
inline constexpr const char* TableBlank = "blank";
inline constexpr const char* RoleCheckbox = "checkbox";
inline constexpr const char* RoleSlider = "slider";
inline constexpr const char* RoleSpinner = "spinner";
inline constexpr const char* RoleDropdown = "dropdown";
inline constexpr const char* Selected = "selected";
inline constexpr const char* Disabled = "unavailable";

// Checkbox states — the same words the legacy UnrealScript narration used, so the switch to
// the native path doesn't change what the player hears.
inline constexpr const char* Checked = "checked";
inline constexpr const char* Unchecked = "unchecked";

// The game's "!" badge (UIButton.bNeedsAttention) — what a sighted player reads as
// "something new here" (a new challenge, unplayed Legacy content).
inline constexpr const char* Attention = "attention";

// My2K account button (bottom right of the shell) and its link states — same three states
// the game's own status text distinguishes.
inline constexpr const char* My2KAccount = "2K account";
inline constexpr const char* My2KLinked = "linked";
inline constexpr const char* My2KNotLinked = "not linked";
inline constexpr const char* My2KOffline = "offline";

// ---- new game flow ----

// Screen names for the campaign-setup screens. The difficulty and narrative screens carry
// their own localized titles and only fall back to these; the Ironman screen and the modal
// dialog have no title property of their own worth trusting, so these name them outright.
inline constexpr const char* ScreenDifficultyFallback = "New campaign";
inline constexpr const char* ScreenIronManFallback = "Ironman";
inline constexpr const char* ScreenNarrativeFallback = "Optional narrative content";
inline constexpr const char* ScreenSoundtrackFallback = "Soundtrack";
inline constexpr const char* ScreenDialog = "Dialog";

// The modal popup family (docs/popup-inventory.md). The popup's own title and message ride in
// the graph, so the screen name only announces that a modal has appeared.
inline constexpr const char* ScreenPopup = "Notification";
// ⚠ These three are OURS, not the game's, and each is a last resort rather than a normal
// reading. PopupNoText fires when a popup was recognised but nothing readable came back — worth
// saying, because silence there is indistinguishable from no popup at all while the player still
// has to press something to escape it. The other two are the accept/cancel pair offered when
// neither the companion nor the Flash panel yielded a button label: a modal a blind player
// cannot dismiss is worse than one that reads badly. If a live run shows these often, the fix is
// a missing field name in the alert table, not better wording here.
inline constexpr const char* PopupNoText = "No readable text";
inline constexpr const char* PopupContinue = "Continue";
inline constexpr const char* PopupDismiss = "Dismiss";

// The mission blade family (docs/mission-blade-recon.md). Every blade carries its own localized
// heading (the mission-type title plus the region) and the recipe reads that, so the fallback is
// reached only when the companion answered nothing for a recognised blade.
inline constexpr const char* ScreenMissionBlade = "Mission";
// Same last-resort family as the popup's: a blade whose wire came back empty still holds the
// keyboard, so the player is told a blade is up and offered the screen's own accept/cancel keys.
inline constexpr const char* MissionBladeNoText = "No readable text";

// The research report (docs/research-report-recon.md). The screen pushes its own localized
// "RESEARCH REPORT" and we read that, so the fallback is reached only when the Flash capture
// found nothing at all.
inline constexpr const char* ScreenResearchReport = "Research report";
/// ⛔ The report's close key depends on the screen stack, so its BUTTON must too — one of these
/// two, never both. The game's own words are unreachable: AddContinueButton spawns a Flash
/// X2ContinueButton whose label lives in the movie, and on mouse-active PC the back button is an
/// icon rather than text (UINavigationHelp.uc:99-107).
inline constexpr const char* ReportContinue = "Continue";
inline constexpr const char* ReportBack = "Back";
/// The capture came back empty on a screen we recognised. Said out loud for the same reason
/// PopupNoText is: the player is looking at a modal they still have to dismiss, and silence
/// there is indistinguishable from no screen at all.
inline constexpr const char* ReportNoText = "Report text unavailable";

// New research available (docs/research-unlocked-recon.md). Same arrangement as the report: the
// screen's own localized "RESEARCH AVAILABLE" rides in every card's payload and names the screen;
// this fallback is reached only when the capture found nothing. The continue button and the
// empty-capture line reuse the report's words above — same NavHelp widget, same failure.
inline constexpr const char* ScreenResearchUnlocked = "Research available";

// The in-game Escape menu. It carries its own localized title and only falls back to this.
inline constexpr const char* ScreenPauseMenuFallback = "Pause menu";

// Squad select. The screen is named by its own operation name ("Операция ...") and reaches these
// only in the window between attaching and the companion answering — or, for the two below, when
// a card is present that the companion could not describe at all.
inline constexpr const char* ScreenSquadSelectFallback = "Squad select";
inline constexpr const char* SoldierFallback = "Soldier";
inline constexpr const char* UtilitySlotFallback = "Utility slot";
inline constexpr const char* ScreenLoadoutFallback = "Loadout";
/// An equipped slot the soldier is carrying nothing in. The screen draws the slot's name over an
/// empty box, and the name alone would read the same as a filled slot whose item we failed to
/// resolve — so the emptiness is said out loud.
inline constexpr const char* LoadoutEmptySlot = "empty";

// After action (docs/after-action-recon.md). The screen names itself with its own header +
// operation name, and — unlike the research report, whose Continue really is unreachable — the
// companion reaches the game's own m_strGenericContinue as a class default, so both fallbacks
// fire only in the window between attaching and the companion answering.
inline constexpr const char* ScreenAfterActionFallback = "Mission results";
inline constexpr const char* AfterActionContinueFallback = "Continue";

// The mission bookends (docs/mission-summary-recon.md) — the two windows that open the instant a
// mission ends, ahead of the after-action walk-up. Both name themselves out of their own Flash
// push (the result headline, "CHOSEN ENCOUNTER"), so these two are reached only when the capture
// came back with nothing at all.
inline constexpr const char* ScreenMissionSummaryFallback = "Mission summary";
inline constexpr const char* ScreenChosenSummaryFallback = "Chosen encounter";
/// The mission summary's Continue. Unlike the research report's, the game's own word IS
/// reachable — XComGame.int populates m_strContinue under [UIMissionSummary] — so this fires
/// only if that field reads empty.
inline constexpr const char* SummaryContinueFallback = "Continue";
/// Recognised the screen, captured nothing. Said out loud for ReportNoText's reason: the player
/// is looking at a window they still have to dismiss, and silence there is indistinguishable
/// from no screen at all. ⚠ It must NOT read as "the mission had no result" — hence "readout",
/// which names OUR failure rather than the game's outcome.
inline constexpr const char* SummaryNoText = "Mission summary readout unavailable";

// Save / load. Both screens carry their own localized title and reach these only if it reads
// empty. The other three are the layer's own words for states the screen shows without text:
// an empty list, a save or load in flight, and the key that reaches the row's delete verb —
// the one affordance the graph does not carry as a node (docs/save-load-recon.md §2).
//
// BACKSPACE, not Delete, even though the screen's RawInputHandler takes either: everywhere
// else in this mod Delete means RE-ANNOUNCE (tile cursor, shot HUD, hacking pad, ant farm), so
// naming it here would teach the one exception to a rule the player has learned everywhere
// else — and get a save deleted by someone reaching for a re-read.
inline constexpr const char* ScreenSaveFallback = "Save game";
inline constexpr const char* ScreenLoadFallback = "Load game";
inline constexpr const char* SaveLoadEmpty = "No saved games";
inline constexpr const char* SaveLoadBusy = "Working, please wait";
inline constexpr const char* KeyBackspace = "Backspace";

// Inside a room on the Avenger. A room's verbs come from two places: its own bottom bar, and
// the game's shortcut submenu on the HUD screen BELOW it. "Nothing here" is legitimate when a
// room has neither — but it must never be said of a room that has one, which is what it WAS
// said of the Bar and the Living Quarters (their shortcut category is -1, and both have a
// bottom-bar button).
inline constexpr const char* ScreenRoomFallback = "Room";
inline constexpr const char* RoomNoActions = "Nothing to do here";
inline constexpr const char* RoomLeaveHint = "Escape to leave the room";
// A bottom-bar button no shipped facility class declares a label for — i.e. a second button
// added by a mod. The widget retains no text of its own, so there is nothing else to say.
inline constexpr const char* RoomFunctionFallback = "Room action";
// The crew line the staff container carries even when it is hidden: "SCIENTISTS: 0". Both
// halves come off the widget already localized; this is only the joint between them.
inline constexpr const char* RoomCrewSeparator = ": ";
// bIsBad / bIsWarning on a submenu row — the game colours these red and amber, which is a fact
// a sighted player gets for free and speech otherwise loses entirely.
inline constexpr const char* RoomUrgent = "urgent";
inline constexpr const char* RoomWarning = "attention";

// An un-excavated room (UIRoom): the engineer slot and the excavate button, neither of which
// vanilla lets a keyboard reach. "No one available" is a REASON, never a refusal — the game
// greys the slot but still opens its picker, and the picker is where a filled slot is emptied.
inline constexpr const char* RoomNoStaff = "no one available";
inline constexpr const char* RoomPickStaff = "choose who to assign";
// A facility staff slot the game draws padlocked: it still takes Enter (HandleClick opens the
// upgrade screen that unlocks it), so this is a state word rather than Vocab::Disabled. The
// WHY is the game's own m_strSlotLockedTooltip, carried by the companion; this is only the
// one-word state a sighted player reads off the padlock.
inline constexpr const char* RoomSlotLocked = "locked";
// The choose-upgrade screen's fallback name, reached only in the window between attaching and
// the companion answering with the screen's own m_strTitle.
inline constexpr const char* ScreenChooseUpgradeFallback = "Facility upgrades";

// The commodity list (research, build items, the archives, the market). Almost every word on
// that screen is the game's own — the row names, the card, both cost labels — so this is a short
// table on purpose. "Cannot afford" is the row's bIsBad tint, which the game shows in colour and
// speech would otherwise drop; the two empty-list lines are for a state the screen names in its
// own subtitle but leaves with nothing to stand on.
inline constexpr const char* ScreenCommodityFallback = "Item list";
inline constexpr const char* CommodityEmpty = "Nothing to choose here";
inline constexpr const char* CommodityLeaveHint = "Escape to go back";
inline constexpr const char* CommodityUnaffordable = "cannot afford";
inline constexpr const char* CommodityOwned = "owned";

// The black market's two non-commodity screens. The entry menu names itself (m_strTitle) and the
// trading post takes the entry menu's own SELL INVENTORY label through the companion, so both
// fallbacks fire only in the window before those reads answer; the third is a sell row whose
// name the companion had not supplied. Every other word on both screens is the game's — the
// button labels, the greyed-out reasons, the column headings, the credits prefix.
inline constexpr const char* ScreenBlackMarketFallback = "Black market";
inline constexpr const char* ScreenBlackMarketSellFallback = "Sell inventory";
inline constexpr const char* SellItemFallback = "Item";

// Choosing what to build in a room (UIChooseFacility). It carries its own localized title and
// list heading and reaches this only in the window between attaching and the companion answering.
inline constexpr const char* ScreenChooseFacilityFallback = "Build facility";

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
inline constexpr const char* ResourceLow = "low";
inline constexpr const char* ResourceCritical = "critical";
// The stop's own name. The game never labels the strip as a group — to a sighted player it is a
// band across the top, and its grouping is spatial — so this word has no localized source.
inline constexpr const char* Resources = "Resources";
// Said only by the HOTKEY, and only in the one state the stop cannot reach: the strip is ON
// SCREEN and the Flash capture has nothing for it, because the game paints the strip on a screen
// change and nothing has changed screens since the capture acquired that panel. Silence there
// would say "there are no resources" — a different and false statement — so this says what is
// actually true, and the tick asks the HUD to repaint so the next press is answered properly.
inline constexpr const char* ResourcesNotRead = "Resources not read yet";

// ---- covert actions (docs/covert-actions-recon.md) ----
//
// Nearly every word on this screen is the game's own: the row text is retained on the widget, and
// the card, the risks and the slots come off the screen's own accessors through the companion.
// Two are ours, and both name a thing the game states with no words at all.
inline constexpr const char* ScreenCovertActions = "Covert actions";
/// ⛔ The one AID on this surface, and it is a warning about a control's SIDE EFFECT.
/// SelectedItemChanged empties the outgoing action's staff and cost slots before switching
/// (UICovertActions.uc:883), so arrowing to a neighbouring action to compare it silently throws
/// away the squad you just assembled. A sighted player watches the portraits vanish; speech gets
/// nothing. Said on the SELECTED row only, and only while something is actually in a slot, so it
/// costs nothing until there is something to lose — and it is PREVENTIVE rather than a report,
/// because the write-back runs before the move's own readout and anything said there would be
/// interrupted by it.
inline constexpr const char* CovertActionStaffed
    = "staffed; moving to another action clears it";

// ---- resistance orders (UIStrategyPolicy) ----
//
// The screen carries no title of its own — DeckTitle names the HAND, not the board — so this one
// is ours outright. The other three each name something the screen draws with no words at all.
inline constexpr const char* ScreenPolicy = "Resistance orders";
/// A slot with nothing in it. The game draws an empty card frame and gives it no text whatever
/// (RealizeColumn hints only the wild-card column's blanks), and an unnamed node is a control
/// that silently ceases to exist — on the one screen where the empty slots are the whole point.
inline constexpr const char* PolicyEmptySlot = "empty slot";
/// The card currently picked up. UIStrategyPolicy.SelectCard is a two-step state machine over
/// InspectingCard, and a sighted player sees which card is held because it is dragged under the
/// cursor. With no drag there is nothing to see, so the state is said.
inline constexpr const char* PolicyHeld = "held";
/// ⛔ Said ONCE, on the screen name, rather than on each of a dozen cards. Outside the monthly
/// report bEnableDragging is false and every card refuses — a sighted player discovers that by
/// clicking and reading the popup, which is cheap for them and costs a blind player a modal per
/// attempt. The cards keep their activation anyway, so pressing one still raises the game's own
/// explanation.
inline constexpr const char* PolicyReadOnly = "orders cannot be changed now";
/// While a card is held, a board slot the game would ACCEPT it into. The game shows this — legal
/// drop spots highlight while a drag is live — so saying it is parity. Value part, spoken after
/// the slot's own words.
inline constexpr const char* PolicyDropTarget = "drop target";
/// The refusal SelectCard makes silently: its final `else` plays a negative click and says
/// nothing, which a blind player cannot tell from a dead key. Spoken on activation, and the verb
/// is still sent — the game stays the authority, this is a caption on its refusal, not a gate.
inline constexpr const char* PolicyCantPlace = "cannot place here";
/// Enter with empty hands on a slot that has nothing to pick up (a blank or a locked slot):
/// CanDragCard() refuses and the companion returns without a word.
inline constexpr const char* PolicyCantPick = "nothing to pick up";
/// Enter with empty hands on a VITAL card (MarkCardAsSpecial) — there IS a card, it just cannot
/// be moved, so "nothing to pick up" would be a lie.
inline constexpr const char* PolicyCantMove = "cannot be moved";
/// Backspace while holding: the held card returned to where it was (ClearInspector). The game's
/// controller build puts this on B/Escape (UIStrategyPolicy.uc:1253); vanilla mouse+keyboard has
/// no way to say it at all.
inline constexpr const char* PolicyPutBack = "put back";

// The personnel roster. Almost every word there is the game's own — the rows are re-derived from
// the unit, the tab names and the sort captions are localized strings off the screen, and even
// the empty-list line is the one PopulateListInstantly pushes into Flash. These five are the
// joints and the fallbacks.
//
// The sort words exist because the header draws its state as an ARROW: a little triangle beside
// one column, pointing up or down. That is two facts (which column sorts, and in which
// direction) carried entirely by a glyph, so speech has to supply the words for both.
inline constexpr const char* ScreenPersonnelFallback = "Personnel";
inline constexpr const char* PersonnelEmpty = "Nobody here";
// The row's promotion badge — an icon, and therefore a whole affordance speech loses by default.
inline constexpr const char* PersonnelPromote = "promotion available";

// ---- promotion / level-up ----
inline constexpr const char* ScreenPromotion = "Promotion";
// A rank the companion could not name. It still gets a row: an unnamed stop a player can move
// through beats a rank that is silently not there.
inline constexpr const char* PromoRankFallback = "rank";
// The four states an ability slot can be in. The screen carries every one of them as ICON
// COLOUR alone — "available" and "unaffordable" are BOTH eUIPromotionState_Normal and differ
// only in the foreground colour — so these words exist because that channel has no sound.
inline constexpr const char* PromoOwned = "learned";
inline constexpr const char* PromoAvailable = "available";
inline constexpr const char* PromoUnaffordable = "not enough ability points";
inline constexpr const char* PromoPrereq = "needs an earlier ability";
inline constexpr const char* PromoSpent = "no choice left at this rank";
inline constexpr const char* PersonnelSortBy = "sort by";
inline constexpr const char* SortAscending = "ascending";
inline constexpr const char* SortDescending = "descending";

// ---- the Chosen dossier and the bond screens (docs/chosen-bond-recon.md) ----
//
// Every word on these three screens is the game's own, re-derived by the companion from the state
// the screen cached. These are the attach-window fallbacks and the words for what the game draws
// with no text at all: a bond level as a digit inside an icon, a meter filled to a level.
inline constexpr const char* ScreenChosenInfoFallback = "Chosen database";
inline constexpr const char* ScreenBondsFallback = "Soldier bonds";
inline constexpr const char* ScreenBondConfirmFallback = "Confirm bond";
/// The bond roster with nobody else on it (a squad of one). The game draws an empty list and no
/// line, and an empty stop is indistinguishable from a broken one.
inline constexpr const char* BondsEmpty = "No other soldiers";
/// The CREATE BOND button's label is the row class's own localized string and rides the wire;
/// this is only for the window before the companion answers.
inline constexpr const char* BondCreateFallback = "Create bond";
/// The bondmate panel's level, which Flash draws as a digit inside the bond icon.
inline std::string BondLevel(int level)
{
    return "bond level " + std::to_string(level);
}
/// A meter the game fills to one of a few discrete steps under a heading and no text (the
/// dossier's HUNT PROGRESS). Not Position(): "1 of 3" after a heading reads as a menu position.
inline std::string MeterLevel(int level, int max)
{
    return "level " + std::to_string(level) + " of " + std::to_string(max);
}

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
inline constexpr const char* ScreenGeoscapeFallback = "Geoscape";
inline constexpr const char* GeoscapeLeaveHint = "Escape to leave the map";
// The resistance-network overlay is a map MODE, and a sighted player reads its state off the
// globe redrawing itself. The verb's own label ("ПЕРЕКЛЮЧИТЬ СЕТЬ СОПРОТИВЛЕНИЯ") names the
// action but not which way it is currently set, so speech supplies that.
inline constexpr const char* GeoscapeResNetOn = "on";
inline constexpr const char* GeoscapeResNetOff = "off";
inline constexpr const char* GeoscapeTrayEmpty = "Nothing on the map right now";
inline constexpr const char* GeoscapeUnmetFaction = "Faction not yet met";

// The region-actions stop: the two hotlink buttons a region pin draws (MAKE CONTACT, and the
// captionless radio-tower button), which vanilla reaches by mouse and controller only. The verb
// words, the cost, the time and the income all come from the game; only these three are ours.
//
// The stop's own name has no localized source — the buttons are drawn ON the pins and the game
// never labels the pair as a group, because to a sighted player their grouping is spatial.
inline constexpr const char* GeoscapeRegionActions = "Region actions";
// Both are facts the game states in a channel speech does not have: the alert reddens a cost it
// cannot pay (UIAlert.BuildOutpostAlert colours it eUIState_Bad and disables the confirm), and
// the contact alert appends a whole sentence about Resistance Comms. Neither is on the PIN, so
// these are said where the choice is made rather than left to the alert that follows — the alert
// still says both, in the game's own words, one keypress later.
inline constexpr const char* GeoscapeCannotAfford = "cannot afford";
inline constexpr const char* GeoscapeNoComms = "no comms capacity";

// The text-entry dialog (naming a save). The field is a Flash TextField, so no screen reader
// sees the keystrokes and this layer has to echo them itself — hence the boundary words and the
// punctuation table below.
inline constexpr const char* TextInputRole = "edit box";
inline constexpr const char* TextInputEmpty = "empty";
inline constexpr const char* TextInputStart = "start";
inline constexpr const char* TextInputEnd = "end";
inline constexpr const char* TextInputDeleted = "deleted";

// Character echo. A LETTER or a digit is spoken as itself and the reader pronounces it; a lone
// punctuation mark is not — most screen readers drop it entirely at their default punctuation
// level, so an echo that just forwarded the character would go silent on exactly the keys a
// player is least sure they pressed. Null means "speak the character itself".
inline const char* PunctuationName(wchar_t ch)
{
    switch (ch) {
    case L' ': return "space";
    case L'!': return "exclamation";
    case L'"': return "quote";
    case L'#': return "hash";
    case L'$': return "dollar";
    case L'%': return "percent";
    case L'&': return "ampersand";
    case L'\'': return "apostrophe";
    case L'(': return "left paren";
    case L')': return "right paren";
    case L'*': return "star";
    case L'+': return "plus";
    case L',': return "comma";
    case L'-': return "dash";
    case L'.': return "dot";
    case L'/': return "slash";
    case L':': return "colon";
    case L';': return "semicolon";
    case L'<': return "less than";
    case L'=': return "equals";
    case L'>': return "greater than";
    case L'?': return "question";
    case L'@': return "at";
    case L'[': return "left bracket";
    case L'\\': return "backslash";
    case L']': return "right bracket";
    case L'^': return "caret";
    case L'_': return "underscore";
    case L'`': return "backtick";
    case L'{': return "left brace";
    case L'|': return "bar";
    case L'}': return "right brace";
    case L'~': return "tilde";
    default: return nullptr;
    }
}

// Spoken while the game shows its loading overlay and the layer has nothing to read. A world
// change can take ten seconds or more, and unbroken silence there reads as a crash.
inline constexpr const char* Loading = "Loading";

// The two panels of the difficulty screen. The game draws them as separate blocks without
// headings; naming them is what makes Tab between them meaningful.
inline constexpr const char* GroupDifficulty = "Difficulty";
inline constexpr const char* GroupCampaignOptions = "Campaign options";

// ---- the tactical HUD graph (the ambient recipe, Ctrl+Tab) ----

// Spoken on Ctrl+Tab into the HUD layer, and by its refusal ("HUD unavailable.").
inline constexpr const char* ScreenTacticalHud = "HUD";

// Spoken when Ctrl+Tab or Escape hands the keys back to the tile cursor. "Map" rather than
// "world mode" because it names what the arrows now drive, not an internal state.
inline constexpr const char* TacticalWorldMode = "Map";

// The HUD graph's stop headings. Our own structure words, same standing as the role words above.
inline constexpr const char* StopSoldier = "Soldier";
inline constexpr const char* StopAbilities = "Abilities";
inline constexpr const char* StopCommands = "Commands";
inline constexpr const char* StopObjectives = "Objectives";

// ---- the Avenger HUD graph (the ant farm's ambient recipe, Tab / Ctrl+Tab) ----
//
// The second ambient layer. Same shape as the tactical one: the game keeps the arrows (they
// walk the rooms, with the camera swoop and the room name the narrate-only reader speaks),
// Tab or Ctrl+Tab focuses the HUD's panels, Escape hands the arrows back.

// Spoken on entering the layer, and by its refusal ("Avenger HUD unavailable.").
inline constexpr const char* ScreenAvengerHud = "Avenger HUD";
// Spoken when the keys go back to the grid — what the arrows now drive, as "Map" is for tactical.
inline constexpr const char* AvengerRoomsMode = "Rooms";
// Stop headings. The shortcut bar and the event queue draw no title of their own; the to-do
// strip is a row of icons. All three words are ours.
inline constexpr const char* StopShortcuts = "Room shortcuts";
inline constexpr const char* StopToDo = "To do";
inline constexpr const char* StopEvents = "Events";
// The Avenger objective list draws dark events under a second heading the widget retains
// (m_strDarkEventsListTitle); this is only the fallback for a list that gave none.
inline constexpr const char* StopDarkEventsFallback = "Dark events";
// An event-queue row whose ETA the game cannot compute prints "--" and tints the title amber —
// a build with no engineer, a project with nobody on it. Said in words rather than as dashes.
inline constexpr const char* EventNoEstimate = "no estimate";
// The hologlobe button on the nav help strip. Its label (m_strGeoscapeLabel) is the game's own
// word and is what is normally spoken; this is only for a nav help that has not populated it.
inline constexpr const char* GeoscapeButtonFallback = "Geoscape";

// An ability icon whose antenna text read empty (the game populates it only while the mouse is
// active, and a stale-cleared icon holds ""), so the node still gets a name a player can act on.
inline std::string AbilityFallback(int index)
{
    return "Ability " + std::to_string(index + 1);
}

// The hotkey label drawn on an ability icon ("1".."0"), phrased so a bare digit at the end of
// the readout cannot be mistaken for a count or a position. The command buttons reuse it for
// their bound-key strings, which arrive already localized from the game's keybinding data.
inline std::string AbilityHotkey(const std::string& label)
{
    return "key " + label;
}

// A command button whose retained label read empty — same degradation as AbilityFallback, so the
// node still exists and still activates (the empty-answer trap: dropping it would silently
// renumber nothing, but it WOULD make a control cease to exist).
inline std::string CommandFallback(int index)
{
    return "Button " + std::to_string(index + 1);
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
inline constexpr const char* IconLeftMouse = "left mouse button";
inline constexpr const char* IconRightMouse = "right mouse button";
inline constexpr const char* IconMouseWheelClick = "mouse wheel click";
inline constexpr const char* IconMouseWheelScroll = "mouse wheel scroll";
inline constexpr const char* IconMouseButton4 = "mouse button 4";
inline constexpr const char* IconMouseButton5 = "mouse button 5";
inline constexpr const char* IconArrowLeft = "left arrow";
inline constexpr const char* IconArrowRight = "right arrow";
inline constexpr const char* IconArrowUp = "up arrow";
inline constexpr const char* IconArrowDown = "down arrow";

// How to leave the tutorial's modal popup. Without this a blind player is told nothing about
// dismissing a screen that has taken over the game: the box is modal and consumes all input, its
// one button calls DisableNavigation() so keyboard focus can never reach it, and the layer
// deliberately leaves Enter/Space/Escape unclaimed so the game's own handler still gets them.
inline constexpr const char* TutorialDismiss = "Press Enter to close.";

// The tutorial refused the submission. The game's own answer is a negative sound and NO TEXT at
// all, so this line is the entire explanation a blind player gets for why nothing happened.
inline constexpr const char* TutorialRejected = "Not what the tutorial is waiting for.";

// The re-read key found nothing to re-read.
inline constexpr const char* TutorialNoMessage = "No tutorial message.";

// The companion mod is not loaded, so the HUD blade's words cannot be recovered. Said only on a
// pressed key — never from the poll, which must not talk about its own failures.
inline constexpr const char* TutorialReaderUnavailable = "Tutorial reader unavailable.";

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
// ⚠ Eight of the nine call this; the ninth is `TutorialReaderUnavailable` above, which stays a
// `constexpr const char*` because its one call site uses it inside a ternary against
// `TutorialNoMessage` and the two arms must share a type. Converting it means reworking that
// ternary — worth doing when `tutorial_reader.cpp` is next opened, not for its own sake.
inline std::string SurfaceUnavailable(const char* surface)
{
    return std::string(surface) + " unavailable.";
}

inline std::string Position(int index, int count)
{
    return std::to_string(index) + " of " + std::to_string(count);
}

inline std::string Percent(int value)
{
    return std::to_string(value) + " percent";
}

// The Chosen's knowledge meter after an extraction. Flash draws a bar filling from one value to
// the other, so the DELTA is the whole content of that block and both ends are said; Percent()
// is reused so neither number can read as a count or a position.
inline std::string KnowledgeChange(int before, int after)
{
    return Percent(before) + " to " + Percent(after);
}

// Fallback tab name for when the screen's localized string reads empty (should not happen on a
// live screen; guards against a half-constructed read).
inline std::string TabFallback(int index)
{
    return "Tab " + std::to_string(index + 1);
}

} // namespace Vocab
