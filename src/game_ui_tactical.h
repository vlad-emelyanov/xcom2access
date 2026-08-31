#pragma once

// Game-UI access, the TACTICAL family: the tactical HUD's gate and readable panels (ability
// bar, command buttons, objectives, mission counters) plus the tutorial's text surfaces — what
// recipe_tactical.cpp, tile_reader.cpp, objective_reader.cpp and tutorial_reader.cpp read.
// Implemented in game_ui_tactical.cpp.

#include "game_ui_core.h"

namespace GameUI {

// ---- tactical HUD (UITacticalHUD) — tile reader support ----

/// The tactical HUD screen instance, or null — same stack-first caching/validation contract as
/// the other tracked screens. Game thread only.
void* FindTacticalHUD();

/// Is the shot HUD (ability targeting) raised? While it is, the game's own targeting UI
/// consumes the arrow keys (UITacticalHUD_AbilityContainer), so the tile cursor stands down.
bool TacticalHUDMenuRaised(void* screen);

/// The tile reader's activation gate, computed from the game's own screen stack. In tactical
/// the FOCUS BIT lives on UIReplay — a permanent top-of-stack overlay whose flag dword (0x259)
/// is bit-for-bit identical to a focused pause menu's — so UITacticalHUD is NEVER the focused
/// screen and flags cannot tell a passive overlay from a modal (verified live 2026-08-19:
/// stack was UIReplay / UISpecialMissionHUD / UITurnOverlay / UITacticalHUD / ...). The gate
/// therefore walks the stack top-down over a whitelist of known-passive overlays; any other
/// screen above the HUD blocks, and its class name is reported so the whitelist can grow from
/// facts.
struct TacticalGate {
    bool hudOnStack = false; // UITacticalHUD present, inited, not removed, and NOTHING over it
    bool menuRaised = false; // shot HUD up — targeting owns the arrows
    bool blocked = false;    // a non-passive screen sits above the HUD
    char blocker[64] = {};   // its class name, for the log

    /// Is a mission running AT ALL — a UITacticalHUD anywhere on the stack, however deep? Unlike
    /// hudOnStack this stays true under a pause menu, a dialogue box, or a cinematic reveal, so
    /// it rises once when the mission starts and falls once when it ends. That is what makes it
    /// the right edge for per-mission state; hudOnStack is the right edge for "may we act now",
    /// and the two must not be confused — the frequent transitions all belong to the latter.
    bool missionActive = false;

    bool Active() const { return hudOnStack && !menuRaised && !blocked; }
};

/// Game thread only.
TacticalGate ReadTacticalGate();

// ---- the ability bar (UITacticalHUD_AbilityContainer + UITacticalHUD_Ability) ----
//
// The one tactical panel besides objectives that IS readable from widgets (engine-notes: the
// bar "needs NO companion call"). Each icon retains the strings the game last pushed:
// m_strAntennaText (the ability's localized friendly name — refreshed only while
// Movie.IsMouseActive(), which the mouse gate deliberately preserves), m_strHotkeyLabel,
// m_strCooldown / m_strCharge (localized prefixes baked in by the game), and IsAvailable.
//
// ⛔ The live icon count is the container's own ActiveAbilities, never "how many widgets hold
// text": ClearData() clears availability, cooldown and charge but NOT the antenna text, so a
// soldier switch leaves the previous soldier's ability names sitting on the trailing icons.
// ActiveAbilities is written by the same PopulateFlash pass that feeds the icons, and the
// widgets [0, ActiveAbilities) map 1:1 onto m_arrAbilities — commander abilities are compacted
// out into the mouse-controls panel by that same pass.
struct AbilityBarEntry {
    void* handle = nullptr;  // UITacticalHUD_Ability — opaque
    std::string name;        // m_strAntennaText, cleaned; may be empty (see the view note)
    std::string hotkey;      // m_strHotkeyLabel ("1".."0"); empty on unbound slots
    std::string cooldown;    // m_strCooldown — "" unless cooling down
    std::string charges;     // m_strCharge — "" when the ability has no charge count
    bool available = false;  // IsAvailable (AvailableCode == 'AA_Success')
};

struct AbilityBarView {
    bool valid = false;              // the container was found and read
    int selected = -1;               // m_iCurrentIndex — -1 while the shot HUD is down
    std::vector<AbilityBarEntry> entries; // the bar's live icons, [0, ActiveAbilities)
};

/// Read the bar off a UITacticalHUD screen. Game thread only.
AbilityBarView ReadAbilityBar(void* screen);

/// Activate bar slot `index` through the container's own AbilityClicked — the mouse path, with
/// every side effect a click has: the input-gate check (PreProcessCheckGameLogic refuses when
/// it is not the player's moment), the tutorial's disabled-trigger check, the select sound, and
/// SelectAbility raising the targeting UI. Returns the game's own answer. Game thread only.
bool ActivateBarAbility(void* screen, int index);

// ---- the command buttons (UITacticalHUD_MouseControls) ----
//
// The bottom-right strip — end turn, prev/next soldier, camera, evac/mission commands, Chosen
// info — and on mouse/keyboard that strip IS the HUD's command surface (the controller hotlink
// buttons are never spawned; see the sighted-parity hard rule). Unlike the rest of the tactical
// HUD it RETAINS its words: SetButtonItem assigns each button's localized Label, the player's
// actual bound-key string and its UIState into ButtonItems before pushing to Flash.
//
// ⛔ The VISIBLE set is not retained (SetNumActiveControls goes straight to Flash), so the read
// reconstructs it from the same three inputs UpdateControls used: buttons 0-4 always, slot 5
// only when CommandAbilities is non-empty, slot 6 only while a Chosen is activated
// (GetActivatedChosen — the game's own gate, asked fresh). Reading ButtonItems[0..N] raw would
// resurrect stale labels: an unused slot keeps whatever was last written to it, forever.
struct CommandButtonEntry {
    int index = -1;      // the game's own button index — what m_iCurrentSelection must be set to
    std::string label;   // ButtonItems[i].Label, cleaned; may be empty (caller supplies fallback)
    std::string key;     // ButtonItems[i].key — the player's current binding, already localized
    bool enabled = false; // UIState == eUIState_Normal, the exact test OnAccept applies
};

struct MouseControlsView {
    bool valid = false;
    // The panel's localized "no key bound" string. When a button's key slot holds THIS, the
    // game is saying the action is unbound — it is a sentence of its own, not a key name, so a
    // caller must not phrase it as one ("key KEY NOT BOUND").
    std::string noKeyBound;
    std::vector<CommandButtonEntry> buttons;
};

/// Read the world-mode button strip off a UITacticalHUD screen. Only meaningful while the shot
/// HUD is DOWN — while it is raised the strip shows Cancel Shot and slot 0's retained label is
/// that, not End Turn (callers gate on menuRaised). Game thread only.
MouseControlsView ReadMouseControls(void* screen);

/// Activate button `index` exactly the way the game's own mouse click does: write
/// m_iCurrentSelection FIRST, then call OnAccept() — the screen-accept-trap rule, and OnAccept
/// is also where a disabled button gets the game's own negative sound. Returns false only when
/// the call could not be made at all. Game thread only.
bool ActivateMouseControl(void* screen, int index);

// ---- the objectives list (UIObjectiveList) ----
//
// The panel that draws "ЗАДАЧИ / OBJECTIVES" and the mission's objective lines under it. Unlike
// every other tactical panel, this one RETAINS its words: UIObjectiveListItem.RefreshDisplay
// ends each of its Set*Row branches with `Data = NewData`, so the ObjectiveDisplayInfo struct the
// row was built from is still there to read. That is what lets native answer this without a
// companion call, and it is why the readout survives the TUTORIAL — the objective list is driven
// by UIObjectiveList.OnVisualizationBlockComplete, which a replay fires exactly as a live mission
// does (see the read-it-from-the-screen hard rule).
//
// ONE class serves both layers: UITacticalHUD.m_kObjectivesControl in a mission and
// UIAvengerHUD.Objectives on the ship. Only the owning screen and its offset differ.

/// One row of the list. The flags are the game's own, so `completed` here means exactly what the
/// ticked checkbox means on screen.
struct ObjectiveLine {
    std::string label;       // Data.DisplayLabel, cleaned. Empty on a title row.
    std::string missionType; // Data.MissionType — "TITLE"/"DARKEVENTTITLE" mark the headings
    bool title = false;      // the synthesized heading row, which carries no label of its own
    bool completed = false;
    bool failed = false;
    bool checkbox = false;
    bool warning = false;
    bool header = false;    // a clickable group header
    bool darkEvent = false;
    int timer = -1;         // Data.Timer — turns remaining; -1 when this row has no timer

    // Counter row (evidence recovered, civilians rescued, ...). All -1 when this is not one; the
    // images are the game's asset tokens, which is where the NOUN comes from — see the reader.
    int haveAmount = -1, haveMin = -1, availableAmount = -1, lostAmount = -1;
    std::string haveImage, availableImage, lostImage;

    /// Does this row carry anything worth speaking? A title row's words live on the class, not in
    /// Data, and an unlabelled non-counter row is a spacer.
    bool Speakable() const { return !label.empty() || haveAmount >= 0; }
};

struct ObjectiveListView {
    bool found = false;    // a UIObjectiveList was located and walked
    bool tactical = false; // ...off UITacticalHUD (false = UIAvengerHUD)
    std::string heading;     // UIObjectiveListItem.m_strObjectivesListTitle ("ЗАДАЧИ")
    std::string darkHeading; // ::m_strDarkEventsListTitle ("АКТИВНЫЕ СОБЫТИЯ")
    std::string inProgress;  // ::m_strInProgress ("- В ПРОЦЕССЕ")
    std::vector<ObjectiveLine> lines;
};

/// Cheap presence test for a key claim's gate: is an objectives list reachable right now? Walks
/// the screen stack and reads one pointer — no string reads, nothing allocated. Game thread only.
bool HasObjectiveList();

/// Read whichever objectives list is currently on the screen stack — tactical first, then the
/// Avenger. Game thread only. `found` is false when neither HUD is up, which is the honest
/// answer for the shell and for a loading screen.
ObjectiveListView ReadObjectives();

// ---- the mission stage counters (UISpecialMissionHUD_TurnCounter) ----
//
// The panel in the top-right corner that reads e.g. "Приведите солдат на запасной маршрут /
// Осталось солдат / 3". It answers a question the objectives list CANNOT: the list says what the
// goals are, this says how far through the current one you are. On a mission whose stage is a
// zone the squad must cross, the only other indicator is a line drawn on the ground — which
// carries no text and no state object, so this counter is the sole readable form of it.
//
// It retains everything: SetLabel/SetSubLabel/SetCounter each end by assigning m_sLabel /
// m_sSubLabel / m_sCounter whether or not the panel was inited, so the strings are authoritative.
//
// ⛔ `bIsVisible` is load-bearing here and must be honoured. Every AS_Set* calls Show() — Firaxis'
// own comment is *"don't show panel unless data is pumped into it"* — so an unfed counter is
// hidden, and the second of the two is routinely inited-but-hidden. Reading a hidden one would
// invent a counter the player cannot see.
struct MissionCounter {
    std::string label;    // m_sLabel    — what the stage is
    std::string subLabel; // m_sSubLabel — what the number counts
    std::string counter;  // m_sCounter  — the number, as the screen shows it
    bool expired = false; // m_bExpired
    bool infinity = false; // m_bInfinity — shows ∞ instead of a number
    bool locked = false;  // m_bLocked — control-point missions only
};

/// The stage counters this mission is showing, in screen order. Empty on a mission with none,
/// which is most of them. Game thread only.
std::vector<MissionCounter> ReadMissionCounters();

// ---- tutorial mission (Tutorial_01) — the text surfaces and the replay state ----
//
// The tutorial is a REPLAY: XComTutorialMgr extends XComReplayMgr, plays a recorded session back
// and halts at every frame that needed player input. Two consequences shape everything here.
// First, the WHOLE script — every popup, every required move — already sits in the history at
// mission start, so anything reading the history must gate on CurrentHistoryFrame or it speaks
// the future. Second, the player is on rails: a submission that does not match the recording is
// rejected with a sound and NO TEXT, which is the accessibility failure this reader exists to
// close.
//
// Four surfaces carry the tutorial's words; three of them are readable:
//   modal popup      UITutorialBox            — real script properties, read below
//   anchored blade   UIWorldMessageMgr        — retained in m_arrMsgs, read below
//   HUD blade        Flash only               — presence below, words from the companion
//   3D world markers XComTutorialMgr meshes   — the object scanner's, not this reader's

/// The tutorial's modal popup. Pushed over the tactical HUD with InputState eInputState_Consume,
/// and — unlike every other tactical panel, which pipes its text straight into Flash — it keeps
/// its text in ordinary script properties, so reading it is a plain offset read.
struct TutorialBoxView {
    std::string title;
    std::string body;
    std::string buttonHelp; // the confirm label; empty means the game shows its generic one
};

/// The focused UITutorialBox, or null. Game thread only.
void* FindTutorialBox();

TutorialBoxView ReadTutorialBox(void* screen);

/// One entry of UIWorldMessageMgr.m_arrMsgs that the manager currently lists as ACTIVE.
struct WorldMessage {
    int slot = -1;          // index into m_arrMsgs — the manager's own identity for the entry
    std::string id;         // m_sId; empty on pooled anonymous messages
    std::string text;       // m_txtMsg, RAW: <img> markup is the caller's to translate
    int type = 0;           // EHUDMessageType: 0 World, 1 Damage, 2 Blade
    bool visible = false;   // m_bVisible — on SCREEN this frame; an off-camera message reads
                            // false while still being a message the game chose to publish
    bool important = false; // m_bIsImportantMessage: named, so never recycled into the pool
    int followObjectId = 0; // m_kFollowObject.ObjectID; 0 when anchored to a bare location
};

/// The live world-message manager (XComPresentationLayerBase.m_kWorldMessageManager), reached
/// through the tactical HUD's own Movie -> PRES chain rather than a GObjects scan, so a manager
/// left over from a previous mission can never win. Null outside tactical. Game thread only.
void* FindWorldMessageMgr();

/// Everything the manager currently lists as active, in its own order.
///
/// This is the only readable form of the game's flyover channel. UIWorldMessageMgr.Message is
/// script->script and so never reaches our ProcessEvent hook — but the manager RETAINS every
/// message it is showing, which makes POLLING work where hooking cannot.
std::vector<WorldMessage> ReadWorldMessages(void* mgr);

/// Is the unanchored bottom-right tutorial blade up (BladeHUDMessage != none)? Its text went
/// straight into Flash through MC.FunctionString and is retained nowhere, so presence is all
/// this can answer; the words come from the companion's history walk.
bool HudBladeShowing(void* mgr);

/// The replay/tutorial manager's state. `valid` is false outside the tutorial mission, which is
/// also how "are we in the tutorial at all" gets answered.
struct TutorialState {
    bool valid = false;
    bool inTutorial = false;       // bInTutorial
    bool allowingControls = false; // bIsTutorialAllowingControls — false while the replay is
                                   // playing back, i.e. while the player owns nothing
    int historyFrame = 0;          // CurrentHistoryFrame — gate every history read on this
    int nextInputFrame = 0;        // NextPlayerInputFrame
    int invalidSubmissions = 0;    // InvalidSubmissionCnt: the on-rails rejections, which the
                                   // game reports with a sound and no text at all
    void* nextAbilityContext = nullptr; // XComGameStateContext_Ability*, opaque — the recorded
                                        // action the player must reproduce. Exposed for the
                                        // object scanner, which owns describing it; nothing in
                                        // the message reader dereferences it.
};

/// Game thread only.
TutorialState ReadTutorialState();

} // namespace GameUI
