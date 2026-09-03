// The POPUP family's readers: the modal-notification finder, the mission blade, and UIAlert's
// Flash read-back. Split out of game_ui.cpp along the recipe-family axis; the raw primitives
// stay in game_ui.cpp behind game_ui_internal.h.

#include "game_ui_popup.h"
#include "game_ui_internal.h"
#include "log.h"
// For UIAlert's message text: its words live in the screen's own `localized` vars, reachable only
// by NAME — which is what reflection does and UnrealScript cannot. See ReadAlertLocalized.
#include "ue3_reflection.h"
#include "flash_shadow.h"

#include <algorithm>
#include <cstring>
#include <string>

class UObject;

namespace GameUI {

// ---- the popup family (docs/popup-inventory.md §1-3) ----
//
// Every modal notification in the game, behind ONE finder. What lets a dozen unrelated classes
// share it is that they agree on the only two things a reader needs: the popup is the topmost
// stack screen, and its OWN OnUnrealCommand answers FXS_KEY_ENTER / FXS_KEY_ESCAPE at screen
// level (checked class by class against SrcOrig). So activation is the screen's own accept path
// and needs no per-class button offsets — which is why this whole family hand-transcribes NO
// offsets and adds NO static_asserts, the same property the hacking screen has.

namespace {

// Ordered most-specific first only for readability; the match is exact on the class name, so
// order does not matter. UIAlert's two DLC subclasses are listed because the finder compares the
// concrete class name, not the base.
constexpr const char* kPopupClasses[] = {
    // Strategy alerts — one class, 118 variants (words come from Flash, see ReadAlertText)
    "UIAlert",
    "UIAlert_DLC_Day60",
    "UIAlert_DLC_Day90",
    // Bespoke strategy popups
    "UIFactionPopup",
    "UIAbilityPopup",
    "UISoldierBondAlert",
    "UIChosenFragmentRecovered",
    "UIChosenLevelUp",
    // Two continue-only screens (docs/chosen-bond-recon.md §1.2-1.3). They answer Enter and Space
    // at screen level and have NO Escape branch, so their companion record carries one button.
    "UIChosenEncountered",
    "UISoldierCaptured",
    "UIUFOAttack",
    "UISitRepInformation",
    "UIAdventOperations",
    // Tactical
    "UIChosenReveal",
    "UICombatLose",
    // Both layers
    "UIProgressDialogue",
    "UIRedScreen",
};

} // namespace

bool IsPopupClass(const char* className)
{
    if (!className)
        return false;
    for (const char* known : kPopupClasses) {
        if (strcmp(className, known) == 0)
            return true;
    }
    return false;
}

PopupView FindPopupScreen()
{
    PopupView v;
    // TOPMOST only, and a non-popup on top is a refusal rather than a reason to keep looking:
    // a popup under another screen has already lost the keys, and answering for it would put
    // our stops in front of a screen the player is actually on. Same rule as UIRoom's finder.
    char name[64] = {};
    v.screen = TopStackScreenRaw([](void*, const char* className) { return IsPopupClass(className); },
        name, static_cast<int>(sizeof(name)));
    if (v.screen)
        v.className = name;
    return v;
}

// ---- the mission blade family (UIMission + subclasses) — docs/mission-blade-recon.md ----

namespace {

// UIMission::MissionRef (StateObjectReference — its first int32 is the ObjectID). The ONE
// hand-transcribed offset this family costs; everything else is companion-side. Pinned in
// sdk_layout_checks.cpp.
constexpr int32_t kUIMissionMissionRefOffset = 0x600;

} // namespace

void* FindMissionBladeScreen()
{
    // IsKindOf, not a name table: 14 shipped subclasses, two DLC ones and any mod's all extend
    // UIMission, share the base skeleton the companion reads, and wire the same five button
    // slots. UIMissionSummary and UIMissionIntro extend UIScreen directly, so the chain walk
    // excludes them for free. TOPMOST only — a blade under another screen has already lost the
    // keys (the popup finder's refusal rule; the sitrep popup and dialogs push over the blade).
    return TopStackScreenRaw([](void* screen, const char*) {
        return IsKindOfRaw(screen, "UIMission");
    });
}

int32_t MissionBladeMissionId(void* screen)
{
    if (!screen)
        return 0;
    return static_cast<int32_t>(ReadU32Raw(screen, kUIMissionMissionRefOffset));
}

// ---- UIAlert's words, read back out of its Flash panel ----
//
// See the header for why this is the one deliberate exception to "verification only, never the
// speech path". The short version: 106 Build*Alert functions, not one retained string, and a
// popup is read once rather than once per keypress.
//
// The field lists come from decompiling the shipped movie — docs/popup-inventory.md §6 has the
// method and the full table. They are in UpdateData's own assignment order, which is the order a
// sighted player reads the panel top to bottom, so it is also the tab-stop order.

namespace {

// How a field carries its text. The decompile says which per field, so a KNOWN symbol never
// guesses; only the unknown-symbol fallback tries more than one shape.
enum class FlashTextShape : uint8_t {
    Getter, // XComScrollingTextField / XComButton — addProperty('text', …), so `<f>.text` runs it
    TheText, // a plain clip wrapping a TextField named theText — `<f>.theText.text`
};

struct AlertField {
    const char* name;
    FlashTextShape shape;
};

struct AlertPanel {
    const char* symbol;
    const AlertField* fields;
    int fieldCount;
};

#define X2A_FIELDS(name, ...)                                                                      \
    const AlertField name[] = {__VA_ARGS__};                                                       \
    constexpr int name##Count = sizeof(name) / sizeof(name[0])

constexpr FlashTextShape kGet = FlashTextShape::Getter;
constexpr FlashTextShape kHdr = FlashTextShape::TheText;

X2A_FIELDS(kAlienSplash, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet});
X2A_FIELDS(kAssignStaff, {"headerMC", kHdr}, {"nameMC", kGet}, {"bonusMC", kGet},
    {"titleMC", kGet}, {"descriptionMC", kGet});
X2A_FIELDS(kBlackMarket, {"logoTitle", kGet}, {"title", kGet}, {"subTitle", kGet},
    {"description", kGet}, {"footer", kGet}, {"leftText", kGet}, {"rightText", kGet});
X2A_FIELDS(kComplete, {"headerTitle", kHdr}, {"headerSubTitle", kGet}, {"alertTitle", kGet},
    {"alertDescription", kGet});
X2A_FIELDS(kContactMade, {"titleMC", kHdr}, {"nameMC", kGet}, {"descriptionMC", kGet},
    {"incomeLabel", kGet}, {"incomeValue", kGet}, {"rewardLabel", kGet}, {"rewardValue", kGet});
X2A_FIELDS(kCouncilReport, {"councilTitle", kHdr});
X2A_FIELDS(kDarkEvent, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet},
    {"quoteMC", kGet}, {"quoteNameMC", kGet});
X2A_FIELDS(kHelp, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet});
// ⚠ WOTC DROPPED `headerField` from this panel and the base-game movie still has it — checked by
// decompiling both. Kept in the list anyway: it costs one empty read under WOTC and is the right
// answer if this ever runs against the base game's movie.
X2A_FIELDS(kItemAvailable, {"headerTitle", kHdr}, {"headerField", kGet}, {"header", kGet},
    {"headTitle", kGet}, {"headName", kGet});

// ---- WOTC-only panels ----
//
// Recovered by decompressing WOTC's own packages with Oodle (see docs/popup-inventory.md §6).
// The four XPACK_* panels and the *Splash family carry no AS2 class of their own, so their fields
// come from the sprite's placed child-instance names rather than from an UpdateData constant
// pool — which is exactly why ReadAlertField always tries both text shapes.
X2A_FIELDS(kBreakthrough, {"breakthroughTitle", kHdr}, {"descriptionTitle", kGet},
    {"descriptionText", kGet});
X2A_FIELDS(kChosenSplash, {"titleMC", kHdr}, {"chosenType", kGet}, {"chosenName", kGet},
    {"chosenNickname", kGet}, {"descriptionMC", kGet});
X2A_FIELDS(kCouncilMissionSplash, {"splashTitle", kHdr}, {"splashSubtitle", kGet},
    {"splashDescription", kGet}, {"splashGreebleText", kGet});
X2A_FIELDS(kGuerrillaOpsSplash, {"GOPSSplashTitle", kHdr}, {"GOPSSplashHeader", kGet},
    {"GOPSSplashDescription", kGet});
X2A_FIELDS(kNegativeSoldierEvent, {"headerMC", kHdr}, {"rankNameMC", kGet}, {"nameMC", kGet},
    {"classNameMC", kGet}, {"abilityLabelMC", kGet}, {"abilityNameMC", kGet},
    {"descriptionMC", kGet});
X2A_FIELDS(kNewStaffSmall, {"NewStaffHeader", kHdr}, {"NewStaffTitle", kGet},
    {"NewStaffName", kGet}, {"NewStaffBonus", kGet});
X2A_FIELDS(kProvingGroundAvail, {"header", kHdr}, {"headTitle", kGet}, {"headName", kGet});
X2A_FIELDS(kRetaliationSplash, {"headerMC", kHdr}, {"subHeaderMC", kGet}, {"descriptionMC", kGet},
    {"footerMC", kGet});
X2A_FIELDS(kSupplyRaidSplash, {"splashHeader", kHdr}, {"splashTitle", kGet},
    {"splashDescription", kGet}, {"splashGreebleText", kGet});
X2A_FIELDS(kSitRep, {"theTitle", kHdr});
X2A_FIELDS(kCovertOpConfirm, {"titleMC", kHdr}, {"confirmHeader", kGet}, {"missionName", kGet},
    {"rewardLabel", kGet}, {"rewardDescription", kGet});
X2A_FIELDS(kCovertOps, {"titleMC", kHdr}, {"descriptionText", kGet});
X2A_FIELDS(kXpackMissionSplash, {"titleMC", kHdr}, {"descriptionText", kGet});
X2A_FIELDS(kRetribution, {"theRegion", kHdr}, {"theDescription", kGet}, {"bottomLeftText", kGet},
    {"bottomRightText", kGet});
X2A_FIELDS(kMakeContact, {"titleMC", kHdr}, {"nameMC", kGet}, {"costLabelMC", kGet},
    {"subLabelMC", kGet}, {"costValueMC", kGet}, {"costDescriptionMC", kGet},
    {"daysLabelMC", kGet}, {"daysValueMC", kGet}, {"descriptionMC", kGet}, {"hintMC", kGet},
    {"rewardLabelMC", kGet}, {"rewardValueMC", kGet});
X2A_FIELDS(kNewObjective, {"headerTitle", kHdr}, {"headerSubTitle", kGet}, {"alertTitle", kGet},
    {"alertDescription", kGet}, {"alertObjectiveText", kGet});
X2A_FIELDS(kNewRegion, {"titleMC", kHdr}, {"nameMC", kGet}, {"descriptionMC", kGet},
    {"hintMC", kGet});
X2A_FIELDS(kNewStaff, {"NewStaffHeader", kHdr}, {"NewStaffName", kGet}, {"NewStaffBonus", kGet},
    {"NewStaffTitle", kGet});
X2A_FIELDS(kOpenStaff, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet},
    {"wideTipMC", kGet});
X2A_FIELDS(kPOI, {"headerTitle", kHdr}, {"headerSubTitle", kGet}, {"alertDescription", kGet},
    {"POILabel01", kGet}, {"POIValue01", kGet}, {"POILabel02", kGet}, {"POIValue02", kGet});
X2A_FIELDS(kSpecialPOI, {"headerTitle", kHdr}, {"headerSubTitle", kGet},
    {"alertDescription", kGet}, {"POILabel01", kGet}, {"POIValue01", kGet});
X2A_FIELDS(kTrainingComplete, {"headerMC", kHdr}, {"rankNameMC", kGet}, {"nameMC", kGet},
    {"classNameMC", kGet}, {"abilityLabelMC", kGet}, {"abilityNameMC", kGet},
    {"descriptionMC", kGet});
X2A_FIELDS(kWarning, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet});
X2A_FIELDS(kXcomGeneric, {"headerMC", kHdr}, {"titleMC", kGet}, {"descriptionMC", kGet});

// The union, for a symbol the table does not know. It should now be UNREACHABLE for stock
// content: the table above covers all 30 names UIAlert.GetLibraryID() can return. It stays as the
// guard for a MOD that adds an alert of its own, and every miss is logged so a live run names it.
X2A_FIELDS(kUnionFields, {"headerMC", kHdr}, {"headerTitle", kHdr}, {"titleMC", kHdr},
    {"NewStaffHeader", kHdr}, {"councilTitle", kHdr}, {"headerSubTitle", kGet},
    {"headerField", kGet}, {"header", kGet}, {"logoTitle", kGet}, {"alertTitle", kGet},
    {"title", kGet}, {"subTitle", kGet}, {"nameMC", kGet}, {"headTitle", kGet},
    {"headName", kGet}, {"rankNameMC", kGet}, {"classNameMC", kGet}, {"NewStaffName", kGet},
    {"alertDescription", kGet}, {"descriptionMC", kGet}, {"description", kGet},
    {"alertObjectiveText", kGet}, {"abilityLabelMC", kGet}, {"abilityNameMC", kGet},
    {"bonusMC", kGet}, {"NewStaffBonus", kGet}, {"NewStaffTitle", kGet}, {"quoteMC", kGet},
    {"quoteNameMC", kGet}, {"hintMC", kGet}, {"wideTipMC", kGet}, {"footer", kGet},
    {"leftText", kGet}, {"rightText", kGet}, {"incomeLabel", kGet}, {"incomeValue", kGet},
    {"rewardLabel", kGet}, {"rewardValue", kGet}, {"POILabel01", kGet}, {"POIValue01", kGet},
    {"POILabel02", kGet}, {"POIValue02", kGet},
    // …and the WOTC vocabulary, so a modded panel built from an XPACK one still reads.
    {"theTitle", kHdr}, {"theRegion", kHdr}, {"splashTitle", kGet}, {"splashHeader", kGet},
    {"splashSubtitle", kGet}, {"splashDescription", kGet}, {"splashGreebleText", kGet},
    {"descriptionTitle", kGet}, {"descriptionText", kGet}, {"theDescription", kGet},
    {"breakthroughTitle", kGet}, {"chosenType", kGet}, {"chosenName", kGet},
    {"chosenNickname", kGet}, {"subHeaderMC", kGet}, {"footerMC", kGet}, {"missionName", kGet},
    {"confirmHeader", kGet}, {"rewardDescription", kGet}, {"bottomLeftText", kGet},
    {"bottomRightText", kGet});

#undef X2A_FIELDS

#define X2A_PANEL(sym, arr) {sym, arr, arr##Count}
const AlertPanel kAlertPanels[] = {
    X2A_PANEL("Alert_AlienSplash", kAlienSplash),
    X2A_PANEL("Alert_AssignStaff", kAssignStaff),
    X2A_PANEL("Alert_BlackMarket", kBlackMarket),
    X2A_PANEL("Alert_Complete", kComplete),
    X2A_PANEL("Alert_ContactMade", kContactMade),
    X2A_PANEL("Alert_CouncilReport", kCouncilReport),
    X2A_PANEL("Alert_DarkEventActive", kDarkEvent),
    X2A_PANEL("Alert_Help", kHelp),
    X2A_PANEL("Alert_ItemAvailable", kItemAvailable),
    X2A_PANEL("Alert_MakeContact", kMakeContact),
    X2A_PANEL("Alert_NewObjective", kNewObjective),
    X2A_PANEL("Alert_NewRegion", kNewRegion),
    X2A_PANEL("Alert_NewStaff", kNewStaff),
    X2A_PANEL("Alert_OpenStaff", kOpenStaff),
    X2A_PANEL("Alert_POI", kPOI),
    X2A_PANEL("Alert_ProvingGroundAvailable", kItemAvailable), // GetLibraryID aliases these two
    X2A_PANEL("Alert_SpecialPOI", kSpecialPOI),
    X2A_PANEL("Alert_TrainingComplete", kTrainingComplete),
    X2A_PANEL("Alert_Warning", kWarning),
    X2A_PANEL("Alert_XComGeneric", kXcomGeneric),
    X2A_PANEL("Alert_XcomGeneric", kXcomGeneric), // GetLibraryID's spelling vs the AS2 class's
    // WOTC-only. Every name here is one GetLibraryID() can actually return — the full set of 30
    // was read off UIAlert.uc's own switch, so this table and that switch cover each other.
    X2A_PANEL("Alert_Breakthrough", kBreakthrough),
    X2A_PANEL("Alert_ChosenSplash", kChosenSplash),
    X2A_PANEL("Alert_CouncilMissionSplash", kCouncilMissionSplash),
    X2A_PANEL("Alert_GuerrillaOpsSplash", kGuerrillaOpsSplash),
    X2A_PANEL("Alert_NegativeSoldierEvent", kNegativeSoldierEvent),
    X2A_PANEL("Alert_NewStaffSmall", kNewStaffSmall),
    X2A_PANEL("Alert_ProvingGroundAvailable", kProvingGroundAvail),
    X2A_PANEL("Alert_RetaliationSplash", kRetaliationSplash),
    X2A_PANEL("Alert_SupplyRaidSplash", kSupplyRaidSplash),
    X2A_PANEL("Alert_SitRep", kSitRep),
    X2A_PANEL("XPACK_Alert_CovertOpConfirm", kCovertOpConfirm),
    X2A_PANEL("XPACK_Alert_CovertOps", kCovertOps),
    X2A_PANEL("XPACK_Alert_MissionSplash", kXpackMissionSplash),
    X2A_PANEL("XPACK_Alert_Retribution", kRetribution),
};
#undef X2A_PANEL

const AlertPanel* FindAlertPanel(const std::string& symbol)
{
    for (const AlertPanel& panel : kAlertPanels) {
        if (symbol == panel.symbol)
            return &panel;
    }
    return nullptr;
}

// One field's text. `<f>.text` runs XComScrollingTextField's addProperty getter; `<f>.theText`
// is a raw TextField, whose own `.text` is a plain property. Blank is AMBIGUOUS in GFx — an
// unresolved path answers exactly like an empty field — which is fine here: both mean "nothing
// to say about this field", and the union fallback treats a blank as "not this shape".
// ⚠ The shape is a PREFERENCE, not an assertion, and both are always tried. The decompile shows
// which children a panel assigns but not reliably how each one wraps its text, and several
// symbols are recovered from sprite child-names with no AS2 class at all to consult. Guessing
// wrong would silently drop a line — the failure mode a blind player cannot report, because a
// missing line reads exactly like a screen that has nothing more to say. A second read on an
// empty field is cheap when the whole panel is read once, on attach.
std::string ReadAlertField(void* movie, const std::string& mcPath, const AlertField& field)
{
    const std::string base = mcPath + "." + field.name;
    const bool headerFirst = field.shape == FlashTextShape::TheText;
    const char* first = headerFirst ? ".theText.text" : ".text";
    const char* second = headerFirst ? ".text" : ".theText.text";

    std::wstring w = FlashString(movie, "GetVariableString", base + first);
    if (w.empty())
        w = FlashString(movie, "GetVariableString", base + second);
    if (w.empty())
        return std::string();
    return CleanText(WcharToUtf8(w.c_str(), static_cast<int>(w.size())));
}

// ⛔ THE MESSAGE TEXT DOES NOT COME FROM FLASH, and this is the measured correction to the whole
// read-back design (live 2026-08-22, eAlert_LostAndAbandoned / XPACK_Alert_MissionSplash):
//
//   * `ButtonGroup.Button0.text` READS — XComButton keeps `text` as a plain variable.
//   * `titleMC` and `descriptionText` RESOLVE as clips (`titleMC._width` = 665.75) but carry no
//     text at all: no `.text`, `.htmlText`, `._text`, `.textField`, `.theText`, and no `getText`.
//     XComScrollingTextField exposes its text through an addProperty GETTER, which GFx's
//     GetVariableString cannot run, and the underlying storage is not reachable by path either.
//   * `UIMCController.CommandQueue` is not a way round it: EndOp() flushes and clears it, and it
//     reads back as an empty array immediately after the build.
//
// What DOES answer is the screen itself. Every Build*Alert resolves its words out of UIAlert's
// own `localized` vars, and those are retained — 383 of them — under a naming convention:
// `eAlert_LostAndAbandoned` → `m_strLostAndAbandonedTitle` / `...Label` / `...Body`. Measured
// across the whole enum, 81 of 118 alert names follow it. Native can exploit that where script
// cannot, because reading a property BY NAME is exactly what UE3 reflection does and
// UnrealScript has no equivalent.
//
// Verified live against the screenshot: Title = "ВХОДЯЩЕЕ СООБЩЕНИЕ!",
// Body = "Шеф, нам надо выступить посредниками в делах двух фракций Сопротивления."
namespace AlertText {

// Reading order, matching how the panel stacks them top to bottom. `Confirm` is deliberately
// absent — it is a BUTTON label and the Flash button read already has it, so including it here
// would say it twice. `Flare` is the map caption, not shown on the popup, and `Image` is a path.
// Counted across UIAlert.uc's own QueueString calls, which is the only honest test of whether a
// suffix ever reaches the screen: Title 48, Header 24, Body 25, Label 20, Desc/Description 3 each,
// Text 0 (declared but never displayed — so it is not read). `Flare` is queued 7 times but only
// as the MAP caption, never on the popup, so it stays out too.
// ⚠ Known over-read: a given alert uses only some of these, and nothing in the naming convention
// says which. eAlert_LostAndAbandoned displays Title and Body but not Label, so its Label is
// spoken though a sighted player never sees it. That is bounded and one-directional — the
// alternative is dropping a line 20 other alerts really do show — but it is the first thing to
// measure on a live pass.
const char* const kAlertTextSuffixes[] = {
    "Title", "Header", "Label", "Body", "Description", "Desc",
};

// UIAlert::LibraryPanel — the panel every Build*Alert pushes its words through, and therefore the
// cache index the Flash capture keys those words by. Pinned in sdk_layout_checks.cpp.
constexpr int32_t kAlertLibraryPanelOffset = 0x1EE8;

// An operand that is an ASSET REFERENCE rather than something to say.
//
// UIAlert pushes image paths down the very same QueueString stream as its prose —
// BuildGOpsAlert queues title, subtitle, body, m_strGOpsImage, confirm, ignore — so the capture
// sees them and speech must not. The test is deliberately NARROW: a path token carries no
// whitespace at all AND either a URL scheme or a `Package.Asset` dot, with nothing outside the
// ASCII path alphabet. A one-word title ("RETALIATION") has no dot and survives; a localized
// title in any non-Latin script fails the alphabet test and survives. Every drop is logged, so a
// live run enumerates what this rejected instead of it going silently missing — the failure mode
// a blind player cannot report, because a dropped line reads exactly like a screen with nothing
// more to say.
bool LooksLikeAssetPath(const std::string& s)
{
    if (s.empty())
        return false;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            return false; // prose
    }
    if (s.find("://") != std::string::npos)
        return true;
    if (s.find('.') == std::string::npos)
        return false;
    for (unsigned char c : s) {
        const bool pathish = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '/' || c == '-';
        if (!pathish)
            return false;
    }
    return true;
}

// A batch that FEEDS a child list rather than painting the frame around it — pushed BEFORE the
// frame because Flash needs the rows in hand to lay them out, and therefore read AFTER it.
//
// `UpdateFacilityList` is the only one UIAlert has (BuildNewStaffAvailableAlert:3565 and
// BuildStaffInfoAlert:3694). It fills the `Alert_NewStaffListItem` rows — facility name and
// bonus, per empty staff slot — that sit UNDERNEATH the "available staff bonus" label the
// FOLLOWING `UpdateData` supplies. Taking push order literally there would read a new recruit's
// bonuses before naming the recruit. Everywhere else push order IS reading order, which is why
// this is a named exception of one rather than a policy.
bool IsListFeederBatch(const FlashShadow::Call& call)
{
    return call.name == "UpdateFacilityList";
}

// The words UIAlert actually handed Scaleform, in the order it handed them over.
//
// ⛔ THIS IS THE SOURCE OF TRUTH for the alert family, and it replaces two weaker readings. The
// localized-var convention below covers only 81 of 118 alert names and cannot tell which of a
// stem's six suffixes the panel actually DISPLAYS, so it over-reads (eAlert_LostAndAbandoned
// shows Title and Body, and the convention also speaks its Label). The Flash path read cannot
// reach an XComScrollingTextField at all. The capture has neither problem: it is exactly the
// argument list of the Update* calls that painted this panel, so it covers every variant, in
// display order, with nothing the panel did not receive.
//
// ⛔ EVERY BATCH ON THE PANEL, NEVER JUST THE NEWEST — the correction measured 2026-09-03, and
// the failure mode is the one CLAUDE.md says a blind player cannot report. `UIAlert` paints
// through more than one batch in at least seven builders, and for several of them the LAST batch
// is a PICTURE: `GetOrStartWaitingForStaffImage` (UIAlert.uc:3972) fires an ASYNCHRONOUS
// photobooth request whose callback `UpdateAlertImage` pushes a fresh `UpdateImage` carrying one
// operand — the headshot's texture path. Reading only the newest batch read that picture,
// LooksLikeAssetPath correctly dropped its single operand, and the whole screen fell through to
// the localized-var fallback, which for `Alert_NewStaff` yields the title and NOTHING ELSE. Shape-
// counted over four sessions' logs, eAlert_SoldierPromoted, ClassEarned, NewStaffAvailable(Small),
// StaffInfo, StaffSlotFilled, ClearRoomSlotFilled and XPACK_Retribution all lost their words that
// way — every popup in the game that carries a soldier headshot or a Chosen icon.
//
// Reading them all also recovers what a one-batch read could never see whatever order it arrived
// in: BuildRetributionAlert paints with FIVE batches, BuildConfirmCovertActionAlert with four,
// and the two staff builders feed a list before painting their frame.
//
// ✅ Concatenating is sound because the panel identity is PER ALERT: `BindLibraryItem`
// (UIAlert.uc:1044) spawns a fresh `UIPanel` for every UIAlert instance, so every batch recorded
// against this cache index belongs to THIS alert — and flash_shadow evicts the index on
// CLEANUP_OP when the panel goes, which is what makes a recycled index safe too.
//
// `buttons` are matched out by EXACT STRING. A confirm/ignore label rides in the same argument
// list as the prose, and the button read already speaks it — without this they would be said
// twice.
std::vector<std::string> ReadAlertCaptured(
    void* screen, const std::vector<std::string>& buttons, const std::string& alertName)
{
    std::vector<std::string> lines;
    void* panel = ReadPtrRaw(screen, kAlertLibraryPanelOffset);
    if (!panel)
        return lines;
    const int32_t index = FlashShadow::PanelCacheIndex(panel);
    if (index < 0)
        return lines;

    // Which batches actually contributed, for the log — so a live run can tell "this alert paints
    // with one call" from "we only found one of its calls".
    std::string batches;

    auto take = [&](const FlashShadow::Call& call) {
        const size_t before = lines.size();
        for (const FlashShadow::Value& arg : call.args) {
            if (arg.type != FlashShadow::ValueType::String || arg.s.empty())
                continue;
            std::string text = CleanText(arg.s);
            if (text.empty())
                continue;
            if (LooksLikeAssetPath(text)) {
                LOG_INFO("Popup: dropped asset-looking operand '%s' from %s.%s",
                    text.c_str(), alertName.c_str(), call.name.c_str());
                continue;
            }
            if (std::find(buttons.begin(), buttons.end(), text) != buttons.end())
                continue; // a button label; the button stop already says it
            if (std::find(lines.begin(), lines.end(), text) != lines.end())
                continue; // several panels push the same string into two fields
            lines.push_back(std::move(text));
        }
        if (lines.size() != before) {
            if (!batches.empty())
                batches += '+';
            batches += call.name;
        }
    };

    const std::vector<FlashShadow::Call> calls = FlashShadow::GetCalls(index);
    for (const FlashShadow::Call& call : calls) {
        if (!IsListFeederBatch(call))
            take(call);
    }
    for (const FlashShadow::Call& call : calls) {
        if (IsListFeederBatch(call))
            take(call);
    }

    if (!lines.empty()) {
        LOG_INFO("Popup: %s read %zu line(s) from the Flash capture (%s, cache index %d)",
            alertName.c_str(), lines.size(), batches.c_str(), index);
    }
    return lines;
}

std::vector<std::string> ReadAlertLocalized(void* screen, const std::string& alertName)
{
    std::vector<std::string> lines;
    constexpr const char* kPrefix = "eAlert_";
    if (alertName.compare(0, strlen(kPrefix), kPrefix) != 0)
        return lines;
    const std::string stem = alertName.substr(strlen(kPrefix));
    if (stem.empty())
        return lines;

    for (const char* suffix : kAlertTextSuffixes) {
        const std::string prop = "m_str" + stem + suffix;
        std::string value = UE3Reflect::ReadStringProperty(
            reinterpret_cast<UObject*>(screen), prop.c_str());
        if (value.empty())
            continue;
        value = CleanText(value);
        if (value.empty())
            continue;
        // A suffix that repeats an earlier one adds nothing but length — several alerts set
        // Title and Label to the same string.
        if (std::find(lines.begin(), lines.end(), value) == lines.end())
            lines.push_back(std::move(value));
    }
    return lines;
}

} // namespace AlertText

// A read is cached on the triple that decides its content. The alert NAME is in the key because
// one library symbol serves many alerts (Alert_Complete covers seven), and the MCPath because a
// second UIAlert gets a fresh panel — so a changed key is an exact, self-invalidating signal
// that the words are different, the same argument the save rows' pointer-vector key makes.
struct AlertTextCache {
    std::string symbol;
    std::string mcPath;
    std::string alertName;
    AlertView view;
    bool valid = false;
};

AlertTextCache gAlertText;

} // namespace

void ResetAlertTextCache()
{
    gAlertText = AlertTextCache();
}

AlertView ReadAlertText(
    void* screen, const std::string& mcPath, const std::string& symbol, const std::string& alertName)
{
    if (gAlertText.valid && gAlertText.symbol == symbol && gAlertText.mcPath == mcPath
        && gAlertText.alertName == alertName)
        return gAlertText.view;

    AlertView v;
    v.screen = screen;
    v.alertName = alertName;
    v.symbol = symbol;
    if (!screen || mcPath.empty())
        return v;

    void* movie = ReadPtrRaw(screen, kPanelMovieOffset);
    if (!movie)
        return v;

    const AlertPanel* panel = FindAlertPanel(symbol);
    v.knownSymbol = panel != nullptr;
    const AlertField* fields = panel ? panel->fields : kUnionFields;
    const int count = panel ? panel->fieldCount : kUnionFieldsCount;

    // ⛔ THE BUTTONS ARE READ FIRST, and that ordering is load-bearing: their labels ride in the
    // same argument list as the prose, so the capture below needs them in hand to match them out.
    //
    // Almost every panel puts them in a ButtonGroup and Flash names them 0 and 1 (while the
    // UnrealScript vars are Button1/Button2). A hidden button answers blank, which is exactly the
    // "no cancel option" case BuildCompleteAlert produces when strCarryOn is empty.
    // ⚠ Alert_Breakthrough is the exception the decompile turned up: it has NO ButtonGroup and
    // names its two buttons outright, so the confirm/cancel pair is looked for at both places.
    // Order matters — the first hit becomes the Enter action and the second the Escape action.
    const char* const kButtonPaths[] = {
        ".ButtonGroup.Button0",
        ".ButtonGroup.Button1",
        ".breakthroughButton",
        ".abandonButton",
    };
    for (const char* path : kButtonPaths) {
        std::wstring w = FlashString(movie, "GetVariableString", mcPath + path + ".text");
        if (w.empty())
            continue;
        std::string label = CleanText(WcharToUtf8(w.c_str(), static_cast<int>(w.size())));
        if (!label.empty())
            v.buttons.push_back(std::move(label));
    }

    // The capture LEADS: it is the argument list of the Update* call that actually painted this
    // panel, so it covers all 118 variants in display order. The two older readings stay as
    // fallbacks, in the order they are trustworthy — the screen's own localized vars (right for
    // the 81 names that follow the stem convention, over-reading by a suffix or two), then the
    // Flash field sweep (right where a field is a plain TextField rather than a scrolling one).
    v.lines = AlertText::ReadAlertCaptured(screen, v.buttons, alertName);

    if (v.lines.empty()) {
        LOG_INFO("Popup: nothing captured for '%s' — falling back to the localized vars",
            alertName.c_str());
        v.lines = AlertText::ReadAlertLocalized(screen, alertName);
    }

    if (v.lines.empty()) {
        if (!panel) {
            // An alert name outside the convention AND a symbol the table does not know. Log
            // both so a live run enumerates the gap rather than hiding it behind a sweep.
            LOG_INFO("Popup: no localized text for '%s' and unknown symbol '%s' — field sweep",
                alertName.c_str(), symbol.c_str());
        } else {
            LOG_INFO("Popup: no localized text for '%s' — falling back to Flash on '%s'",
                alertName.c_str(), symbol.c_str());
        }
        for (int i = 0; i < count; i++) {
            std::string text = ReadAlertField(movie, mcPath, fields[i]);
            if (!text.empty())
                v.lines.push_back(std::move(text));
        }
    }

    gAlertText.symbol = symbol;
    gAlertText.mcPath = mcPath;
    gAlertText.alertName = alertName;
    gAlertText.view = v;
    gAlertText.valid = true;
    return v;
}

} // namespace GameUI
