#include "tutorial_reader.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "narrator.h"
#include "ue3_reflection.h"
#include "vocab.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace TutorialReader {

namespace {

constexpr const char* kCompanionClass = "X2AccessTutorial";

// Poll cadence. The same 150 ms the tile reader and the shot HUD use: a world message lives about
// five seconds, so this is far inside the window, and the cost is two pointer reads and a small
// array walk on the frames where nothing changed.
constexpr uint64_t kPollIntervalMs = 150;

// The HUD blade's words cost an outbound ProcessEvent to recover, so that one is asked less often
// than the rest. 500 ms is under the threshold at which a spoken reaction stops feeling attached
// to the thing that caused it, and the blade is not a per-frame surface.
constexpr uint64_t kBladePollIntervalMs = 500;

// EHUDMessageType (UIWorldMessageMgr.uc).
constexpr int kMsgWorld = 0;
constexpr int kMsgDamage = 1;
constexpr int kMsgBlade = 2;

// The claim's active predicate result, recomputed on the tick. The arbiter evaluates activeWhen
// inside the window procedure, so it must stay a flag read — the screen-stack walk behind it
// belongs on the tick, the same discipline as the tile reader and the menu navigator.
bool gActive = false;

// What has already been said, so the poll speaks CHANGES and not state.
std::string gSpokenBox; // title + body of the modal currently announced

// What is known about one m_arrMsgs slot between polls. `spoken` and `identity` are separate
// because a message can be PRESENT without having been read — an off-camera flyover stays
// pending, and is announced later if the camera reaches it.
struct SlotState {
    std::string identity;    // id + text of whatever occupies the slot; "" for an empty slot
    bool spoken = false;     // it has been read out
    bool heldLogged = false; // the "off screen" line has been written once for THIS identity
};
std::map<int, SlotState> gSlots;
bool gBladeShowing = false;
uint64_t gLastBladePollMs = 0;

// Rejection tracking. Both halves are needed — see PollRejection for why the counter alone is
// ambiguous. The baseline is taken from the first read rather than assumed zero: entering the
// tutorial mid-session with rejections already on the counter must not announce them
// retroactively.
bool gHaveInvalidBaseline = false;
int gLastInvalid = 0;
int gLastFrame = 0;

// Idempotent, and called on every tick outside the tutorial: what has been spoken must not
// survive the mission it was spoken in, and the world-message slot map in particular would
// otherwise silence a fresh mission's first messages by matching stale slots.
void ResetSpokenState()
{
    gSpokenBox.clear();
    gSlots.clear();
    gBladeShowing = false;
    gHaveInvalidBaseline = false;
    gLastInvalid = 0;
    gLastFrame = 0;
}

// ---- markup translation ------------------------------------------------------------------------
//
// Tutorial text arrives with the game's own Flash markup already spliced in. Two kinds reach us:
// bracketed key names ("[Q]", "[TAB]"), which read correctly as they stand, and <img> tags for the
// mouse and arrow glyphs, which must be NAMED. CleanText would drop the tag silently and leave a
// hole exactly where the instruction is, so this walks the markup instead of stripping it.

struct IconName {
    const char* src;
    const char* spoken;
};

// UIUtilities_Input's ICON_PC_* constants — the complete keyboard-and-mouse set. Gamepad glyphs
// are deliberately absent: they are spliced only while a controller is active, which is not a
// configuration this mod supports, and naming one we never see would be a guess.
constexpr IconName kIconNames[] = {
    {"lmb_icon", Vocab::IconLeftMouse},
    {"rmb_icon", Vocab::IconRightMouse},
    {"PC_mouseWheelClick", Vocab::IconMouseWheelClick},
    {"PC_mouseWheelScroll", Vocab::IconMouseWheelScroll},
    {"PC_mouseLeft4", Vocab::IconMouseButton4},
    {"PC_mouseLeft5", Vocab::IconMouseButton5},
    {"PC_arrowLEFT", Vocab::IconArrowLeft},
    {"PC_arrowRIGHT", Vocab::IconArrowRight},
    {"PC_arrowUP", Vocab::IconArrowUp},
    {"PC_arrowDOWN", Vocab::IconArrowDown},
};

// ---- the game's own words -------------------------------------------------------------------
//
// The table above is ENGLISH, and that is wrong the moment the player is not playing in English:
// "Нажмите right mouse button, чтобы пойти сюда" is our wording shouldering into the game's, three
// words from the game's own `[ПРАВАЯ КНОПКА МЫШИ]` on the very next line (observed live
// 2026-08-20). The companion hands over what the game itself prints — XComKeybindingData's
// localized key names — and those override the built-ins wherever they arrive. A token the
// companion has no answer for keeps the English word rather than going silent.

constexpr const char* kHelperClass = "X2AccessHelper";

// Must match X2AccessHelper.VOCAB_SEP.
constexpr const char* kVocabSep = "~|~";

std::map<std::string, std::string> gVocabulary;   // markup token -> the game's spoken word
std::vector<std::string> gResultFlyovers;         // "MISSED!" and friends, verbatim
bool gVocabularyLoaded = false;

std::vector<std::string> SplitEntries(const std::string& payload)
{
    std::vector<std::string> out;
    const size_t sepLen = strlen(kVocabSep);
    size_t pos = 0;
    for (;;) {
        const size_t next = payload.find(kVocabSep, pos);
        std::string entry
            = (next == std::string::npos) ? payload.substr(pos) : payload.substr(pos, next - pos);
        if (!entry.empty())
            out.push_back(std::move(entry));
        if (next == std::string::npos)
            break;
        pos = next + sepLen;
    }
    return out;
}

// The spoken form of one markup token — the game's word if it gave us one, ours otherwise.
std::string SpokenToken(const std::string& token, const char* fallback)
{
    const auto it = gVocabulary.find(token);
    if (it != gVocabulary.end() && !it->second.empty())
        return it->second;
    return fallback ? std::string(fallback) : std::string();
}

// The src attribute of an <img> tag body, or empty when the tag is not an image.
std::string ImageSource(const std::string& tag)
{
    if (tag.size() < 3 || (tag[0] != 'i' && tag[0] != 'I'))
        return {};
    if (_strnicmp(tag.c_str(), "img", 3) != 0)
        return {};
    const size_t at = tag.find("src=");
    if (at == std::string::npos)
        return {};
    size_t start = at + 4;
    if (start >= tag.size())
        return {};
    const char quote = tag[start];
    if (quote != '\'' && quote != '"')
        return {};
    const size_t end = tag.find(quote, ++start);
    return end == std::string::npos ? std::string() : tag.substr(start, end - start);
}

// An icon we have no word for is dropped rather than guessed at — but it is LOGGED once, so the
// table above can grow from observation the way the tactical passive-screen list does.
void ReportUnknownIcon(const std::string& src)
{
    static std::set<std::string> sSeen;
    if (sSeen.insert(src).second)
        LOG_INFO("TutorialReader: no spoken name for icon \"%s\" (dropped from speech)", src.c_str());
}

std::string TranslateMarkup(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        // %KEY:NAME% — a key the game meant to substitute and did not. X2MissionNarrativeTemplate
        // .KeyBindingMarkups only covers RMB, ENTER, TAB, Q, E and P; %KEY:LMB% is substituted
        // solely by UIUtilities_Input.InsertPCIcons, which the blade path never calls, so it
        // reaches us RAW. Speaking the token verbatim reads as a bug and hides the instruction.
        if (text[i] == '%' && text.compare(i, 5, "%KEY:") == 0) {
            const size_t close = text.find('%', i + 5);
            if (close != std::string::npos) {
                const std::string name = text.substr(i + 5, close - i - 5);
                // Fall back to the token's own NAME, not to nothing: "%KEY:HOME%" unresolved is
                // still readable as "HOME", and the alternative is an instruction with a hole in it.
                out += SpokenToken("KEY:" + name, name.c_str());
                i = close + 1;
                continue;
            }
        }

        if (text[i] != '<') {
            out += text[i++];
            continue;
        }
        const size_t close = text.find('>', i);
        if (close == std::string::npos) {
            // An unterminated '<' is ordinary text, not markup.
            out += text.substr(i);
            break;
        }
        const std::string tag = text.substr(i + 1, close - i - 1);
        i = close + 1;

        const std::string src = ImageSource(tag);
        if (src.empty())
            continue; // <font>, </font>, and every other formatting tag: no words in it

        const char* builtin = nullptr;
        for (const IconName& icon : kIconNames) {
            if (_stricmp(icon.src, src.c_str()) == 0) {
                builtin = icon.spoken;
                break;
            }
        }
        const std::string spoken = SpokenToken(src, builtin);
        if (!spoken.empty())
            out += spoken;
        else
            ReportUnknownIcon(src);
    }

    // Collapse whitespace and pull it back off punctuation. A dropped icon otherwise leaves
    // "Press , to go here", and even a translated one leaves the double spaces the markup sat in.
    std::string tidy;
    tidy.reserve(out.size());
    bool pendingSpace = false;
    for (char c : out) {
        if (static_cast<unsigned char>(c) <= ' ') {
            pendingSpace = !tidy.empty();
            continue;
        }
        if (pendingSpace && !strchr(",.;:!?)", c))
            tidy += ' ';
        pendingSpace = false;
        tidy += c;
    }
    return tidy;
}

// ---- outbound calls to the companion ------------------------------------------------------------

// One call, then whatever it wrote to NativeResult. Runs from the frame pump or the arbiter's
// tick drain — both the game thread, which is what outbound ProcessEvent requires. An empty
// result is a deliberate "nothing to say", which is what the blade poll returns on almost every
// call.
std::string CallCompanion(const char* funcName, void* params)
{
    void* cdo = GameUI::FindCompanionCDO(kCompanionClass);
    if (!cdo)
        return {}; // FindCompanionCDO already logs the miss (companion mod not loaded)
    if (!GameUI::CallScriptFunction(cdo, funcName, params)) {
        LOG_WARN("TutorialReader: outbound call %s failed", funcName);
        return {};
    }
    return UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
}

std::string CallCompanion(const char* funcName)
{
    // No-parameter functions still get a zeroed buffer — ProcessEvent copies ParmsSize bytes.
    int64_t zeros[2] = {};
    return CallCompanion(funcName, zeros);
}

// Same shape, different companion class: the vocabulary is a plain localized lookup with no state
// behind it, so it lives with the other shared description helpers rather than with the tutorial.
std::string CallHelper(const char* funcName)
{
    void* cdo = GameUI::FindCompanionCDO(kHelperClass);
    if (!cdo)
        return {};
    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, funcName, zeros)) {
        LOG_WARN("TutorialReader: outbound call %s failed", funcName);
        return {};
    }
    return UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
}

// Ask once per session. Localized strings do not change under us, and an empty answer means the
// companion is not up yet rather than that there is nothing to say — so this stays unlatched and
// retries on the next tick.
void LoadVocabulary()
{
    if (gVocabularyLoaded)
        return;

    const std::string words = CallHelper("GetInputVocabulary");
    if (words.empty())
        return;

    for (const std::string& entry : SplitEntries(words)) {
        const size_t eq = entry.find('=');
        if (eq == std::string::npos || eq + 1 >= entry.size())
            continue; // a key with no localization on this build: keep our own word
        gVocabulary[entry.substr(0, eq)] = entry.substr(eq + 1);
    }

    // Run these through the same translator the messages get, so a comparison later is between two
    // strings that were normalised the same way rather than between raw markup and speech.
    for (const std::string& word : SplitEntries(CallHelper("GetResultFlyovers"))) {
        std::string tidy = TranslateMarkup(word);
        if (!tidy.empty())
            gResultFlyovers.push_back(std::move(tidy));
    }

    gVocabularyLoaded = true;
    LOG_INFO("TutorialReader: vocabulary loaded — %zu spoken tokens, %zu result flyovers",
        gVocabulary.size(), gResultFlyovers.size());
}

// Is this flyover just the game restating a shot's outcome? Compared verbatim: both sides come
// from the same localization table (X2Action_ApplyWeaponDamageToUnit hands MissedMessage straight
// to UIWorldMessageMgr.DamageDisplay), so there is no case folding to get wrong — which matters,
// because folding case correctly for Cyrillic is not something _stricmp does.
bool IsResultFlyover(const std::string& text)
{
    for (const std::string& word : gResultFlyovers) {
        if (text == word)
            return true;
    }
    return false;
}

// The unit a flyover is pinned to, or "" when the anchor is not a unit, is not visible to the
// squad, or the companion is absent. One outbound call per newly-spoken anchored message — they
// are rare enough that caching would buy nothing and would risk holding a name across the frame
// on which it changed.
std::string AnchorName(int objectId)
{
    struct {
        int32_t ObjectID;
    } params{objectId};
    return CallCompanion("DescribeMessageAnchor", &params);
}

// The modal read as one utterance. Both the poll and the re-read key go through this, so the
// dismiss hint can never be attached to one and forgotten on the other.
// Join one sentence onto another WITHOUT doubling its full stop. Game text usually ends in its
// own punctuation and sometimes does not (a title never does), so neither "always add a period"
// nor "never add one" is right — the first produced "...одно действие.. Press Enter to close." live, and a screen
// reader gives that stutter its own pause.
void AppendSentence(std::string& text, const std::string& next)
{
    if (next.empty())
        return;
    if (text.empty()) {
        text = next;
        return;
    }
    const char last = text.back();
    if (!strchr(".!?:;", last))
        text += '.';
    text += ' ';
    text += next;
}

std::string ModalSpeech(const GameUI::TutorialBoxView& view)
{
    std::string text = view.title;
    AppendSentence(text, view.body);

    // The confirm label only when a Kismet author set one explicitly — then it is authored
    // content and is on screen. The tutorial's own popups never do: X2Action_ShowTutorialPopup
    // calls Pres.UITutorialBox with three arguments, leaving ButtonHelp at its "" default, and
    // the screen falls back to UIUtilities_Text.m_strGenericConfirm inside Flash.
    AppendSentence(text, view.buttonHelp);

    if (text.empty())
        return text;

    // ...which is exactly why the way OUT has to be spoken outright. A sighted player sees a
    // confirm button; a blind player has no way to discover one, because the box consumes all
    // input, its button calls DisableNavigation(), and we leave the dismiss keys to the game.
    AppendSentence(text, Vocab::TutorialDismiss);
    return text;
}

// ---- the re-read key ----------------------------------------------------------------------------

// Whatever the tutorial is saying RIGHT NOW, on demand. The modal wins when one is up — it is
// covering the screen — and otherwise the blade does, which is the surface a player is most
// likely to have talked over. A pressed key must never be silent, so every path answers.
void AnnounceCurrent()
{
    // A modal covers the screen, so nothing behind it is what the player is being asked to read.
    if (void* box = GameUI::FindTutorialBox()) {
        const std::string text = ModalSpeech(GameUI::ReadTutorialBox(box));
        if (!text.empty()) {
            Narrator::Say(Narrator::Channel::Aid, TranslateMarkup(text));
            return;
        }
    }

    // Otherwise: everything the tutorial has on screen, in the order the poll would have said it
    // — the ANCHORED blades native reads for itself, then the HUD blade only the companion can
    // recover. Asking the companion alone was a gap: the poll speaks anchored blades but the
    // re-read key could not, so pressing it while "press RMB to go here" was up answered "no
    // tutorial message" about a message that was plainly on screen.
    //
    // Composed into ONE utterance on purpose. Aid interrupts and clears the queue, so two calls
    // would leave only the second.
    std::string text;
    if (void* mgr = GameUI::FindWorldMessageMgr()) {
        for (const GameUI::WorldMessage& msg : GameUI::ReadWorldMessages(mgr)) {
            if (msg.type == kMsgBlade)
                AppendSentence(text, TranslateMarkup(msg.text));
        }
    }

    const bool haveCompanion = GameUI::FindCompanionCDO(kCompanionClass) != nullptr;
    if (haveCompanion)
        AppendSentence(text, TranslateMarkup(CallCompanion("AnnounceBlade")));

    if (!text.empty()) {
        Narrator::Say(Narrator::Channel::Aid, text);
        return;
    }
    // Nothing found. Which "nothing" it is matters: without the companion the HUD blade is
    // unreadable, so silence there is our failure rather than the tutorial's.
    Narrator::Say(Narrator::Channel::Aid,
        haveCompanion ? Vocab::TutorialNoMessage : Vocab::TutorialReaderUnavailable);
}

// ---- the poll ------------------------------------------------------------------------------------

// The modal popup (UITutorialBox). Identity is title + body rather than the screen pointer: one
// popup can be replaced by the next between two polls, and the pointer alone would stay silent
// about a page the player never heard.
void PollModal(void* box)
{
    if (!box) {
        gSpokenBox.clear();
        return;
    }
    const GameUI::TutorialBoxView view = GameUI::ReadTutorialBox(box);
    const std::string key = view.title + "\n" + view.body;
    if (key == gSpokenBox)
        return;
    gSpokenBox = key;

    const std::string text = ModalSpeech(view);
    if (!text.empty())
        Narrator::Say(Narrator::Channel::Alert, TranslateMarkup(text));
}

// Decide what to do about ONE message that has not been spoken yet, updating its slot state.
// Split out of the loop so the state write-back below happens on exactly one path.
void ConsiderWorldMessage(const GameUI::WorldMessage& msg, bool inTutorial, SlotState& state)
{
    if (msg.text.empty())
        return;

    // Damage flyovers are NOT read here. The companion's X2AccessTacticalEvents listener already
    // narrates damage, kills and status out of the game state, where it can name the unit and sum
    // the entries of one grenade; speaking the flyover too would double every hit. World messages
    // get the same treatment OUTSIDE the tutorial for the same reason — inside it they are the
    // tutorial's own cursor-help strings, which nothing else narrates.
    if (msg.type == kMsgDamage)
        return;
    if (msg.type == kMsgWorld && !inTutorial)
        return;

    // ⚠️ A MISS arrives as an ordinary world message, not a damage one — QueueWorldMessage only
    // stamps eHUDMessage_Damage when m_iDamagePrimary > 0, and a miss carries zero. So inside the
    // tutorial the miss flyover slips past the filter above, and X2AccessReplayNarration has
    // already spoken the same outcome WITH attribution ("Osei: Fire at ADVENT Trooper, Missed.").
    // Dropping the bare word leaves the attributed line standing rather than the other way round.
    if (msg.type == kMsgWorld && IsResultFlyover(TranslateMarkup(msg.text)))
        return;

    // ⚠️ Sighted parity: a world flyover is anchored in the WORLD, and UpdateMessageLocation
    // clears m_bVisible whenever that point projects off screen. An off-camera one was never read
    // by anyone, and speaking it hands the player a bare status word ("Unconscious") with nothing
    // to attach it to — which is exactly how it landed in the first live run. It stays PENDING
    // rather than being dropped, so panning the camera onto it still announces it.
    //
    // Blades are exempt, and must be: they are instructions the game means the player to act on,
    // they sit at the DESTINATION rather than wherever the camera is, and the tutorial's own
    // anchored blade read vis=0 at the exact moment it was the only thing that mattered.
    if (msg.type == kMsgWorld && !msg.visible) {
        // Once per message, not once per poll: a held message is re-examined every 150 ms, and
        // logging each look buried the ring under 58 copies of one line in the first run.
        if (!state.heldLogged) {
            state.heldLogged = true;
            LOG_DEBUG("TutorialReader: world message off screen, holding slot %d id \"%s\": %s",
                msg.slot, msg.id.c_str(), msg.text.c_str());
        }
        return;
    }

    std::string spoken = TranslateMarkup(msg.text);

    // Name who it is about. A flyover floats over the soldier it belongs to, so for a sighted
    // player the anchor IS the referent — without it "Unconscious" names nobody, and because the
    // game queues these PER UNIT the same bare word arrives once per soldier. Blades are excluded
    // deliberately: an anchored blade is an instruction about a PLACE ("press RMB to go here"),
    // so prefixing it with whoever stands there would be actively wrong.
    if (msg.type == kMsgWorld && msg.followObjectId > 0) {
        const std::string who = AnchorName(msg.followObjectId);
        if (!who.empty())
            spoken = who + ": " + spoken;
    }

    // A blade is an instruction: it changes what the player is supposed to do, so it cuts in.
    // A world message is a status flyover and queues behind whatever is already being read, which
    // is also how a sighted player takes them — at their own pace.
    //
    // Source::Flyover, and it matters: inside the tutorial the replay narrator is describing the
    // very same game states from the other side. An ability with no target ("Osei: Overwatch")
    // comes out word for word identical from both, and the flyover gets here first. Tagging the
    // source lets the state-derived twin stand down — one way only, see narrator.h.
    Narrator::Say(msg.type == kMsgBlade ? Narrator::Channel::Alert : Narrator::Channel::Combat,
        spoken, Narrator::Source::Flyover);
    state.spoken = true;
}

// The world-message manager's active set. Diffed per SLOT, because the manager recycles slots
// from a pool: the slot is its own identity for an entry, and pairing it with the id and text
// catches a recycled slot carrying something new.
void PollWorldMessages(void* mgr, bool inTutorial)
{
    const std::vector<GameUI::WorldMessage> messages = GameUI::ReadWorldMessages(mgr);

    std::map<int, SlotState> stillActive;
    for (const GameUI::WorldMessage& msg : messages) {
        std::string identity = msg.id + "\x1f" + msg.text;

        // Carry the previous poll's flags only while the SAME entry still occupies the slot. The
        // manager recycles slots from a pool, so slot number alone is not identity.
        SlotState state;
        const auto it = gSlots.find(msg.slot);
        if (it != gSlots.end() && it->second.identity == identity)
            state = it->second;
        else
            state.identity = identity;

        if (!state.spoken)
            ConsiderWorldMessage(msg, inTutorial, state);
        stillActive.emplace(msg.slot, std::move(state));
    }

    // Slots that dropped out are forgotten, so a message the game publishes AGAIN is announced
    // again — it republished it on purpose.
    gSlots.swap(stillActive);
}

// The bottom-right HUD blade. Its text went straight into Flash through MC.FunctionString and is
// retained nowhere, so presence is all C++ can see; the words come back from the companion, which
// walks the history for the XComGameStateContext_TutorialBox that caused it.
//
// Presence alone is not enough as a trigger: CreateHUDBladeMessage reuses the same panel and just
// calls SetText again, so a REPLACED blade looks identical from here. The companion therefore owns
// the change detection (it compares against the text it last reported) and this only has to ask.
void PollHudBlade(void* mgr)
{
    const bool showing = GameUI::HudBladeShowing(mgr);
    if (showing != gBladeShowing) {
        gBladeShowing = showing;
        LOG_DEBUG("TutorialReader: HUD blade %s", showing ? "up" : "down");
        // Ask about the next raise on the very next tick rather than waiting out an interval
        // that was started for a blade which has since gone.
        gLastBladePollMs = 0;
    }
    if (!showing)
        return;

    const uint64_t now = GetTickCount64();
    if (now - gLastBladePollMs < kBladePollIntervalMs)
        return;
    gLastBladePollMs = now;

    const std::string text = CallCompanion("PollBlade");
    if (!text.empty())
        Narrator::Say(Narrator::Channel::Alert, TranslateMarkup(text));
}

// The on-rails rejection. `X2GameRuleset.SubmitGameStateContext` routes every submission to
// XComTutorialMgr while the tutorial runs, and a context that does not match the recording is
// dropped with `eSUISound_MenuClickNegative` and NO TEXT — the player is told nothing about why
// their move did not happen. Announcing it is the single highest-value line in this reader.
//
// Note what this does NOT do: describe where the tutorial wanted the player to go. That reads
// XComTutorialMgr.NextPlayerAbilityContext, which is the 3D marker's data and belongs to the
// object scanner — two surfaces speaking the same thing would be worse than one.
//
// ⚠️ A RISE in the counter is not the test, and using one would miss every third rejection.
// HandleSubmittedGameStateContext does `InvalidSubmissionCnt++` and then, once it passes
// INVALID_SUBMISSION_MAX (2), calls HandleTooManyInvalidSubmissions and sets it back to 0 in the
// same breath — so a poll sees 1, 2, 0, 1, 2, 0. An ACCEPTED submission zeroes it too. What
// separates the two is that the accept path runs StepReplayForward() first: the replay's frame
// ADVANCES on success and stands still on a rejection. So the test is "the counter moved and the
// frame did not".
void PollRejection(const GameUI::TutorialState& tut)
{
    if (!gHaveInvalidBaseline) {
        gHaveInvalidBaseline = true;
        gLastInvalid = tut.invalidSubmissions;
        gLastFrame = tut.historyFrame;
        return;
    }
    const bool rejected
        = tut.invalidSubmissions != gLastInvalid && tut.historyFrame <= gLastFrame;
    gLastInvalid = tut.invalidSubmissions;
    gLastFrame = tut.historyFrame;
    if (rejected)
        Narrator::Say(Narrator::Channel::Alert, Vocab::TutorialRejected);
}

} // namespace

void Init()
{
    using namespace InputArbiter;

    // I: re-read whatever the tutorial is currently saying. Exactly TWO letters are unbound
    // across every section of DefaultInput.ini — H and I — and H is already the shot HUD's
    // breakdown key. Every other letter forwards a key event into the UI input pipeline through a
    // generic X_Key_Press binding, so an empty-looking one is not free: Y is Overwatch's default
    // ability hotkey, and U is bound in [Engine.PlayerInput] rather than the tactical block, which
    // is exactly the trap a tactical-section-only grep falls into.
    //
    // Enter, Space and Escape are deliberately NOT claimed: they are how the modal is dismissed,
    // and swallowing them would seal a blind player inside a box they cannot close.
    RegisterClaim({'I', ModNone, [] { return gActive; },
        [](const KeyEvent& e) {
            if (e.down && !e.repeat)
                AnnounceCurrent();
        },
        "tutorial: re-read message"});

    LOG_INFO("TutorialReader: claims registered");
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    const uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    // XComTutorialMgr exists only in the tutorial mission, so this doubles as the "are we in the
    // tutorial" test — every other mission pays one cached pointer revalidation per tick.
    static bool sInTutorial = false;
    const GameUI::TutorialState tut = GameUI::ReadTutorialState();
    if (!tut.valid) {
        if (sInTutorial) {
            sInTutorial = false;
            LOG_INFO("TutorialReader: inactive — no tutorial manager");
        }
        ResetSpokenState();
        gActive = false;
        return;
    }
    sInTutorial = true;

    // Cheap once loaded, and it must run before anything is translated: the whole point is that
    // the first tutorial line already speaks the game's own words rather than our English ones.
    LoadVocabulary();

    // The KEY needs a gate; the narration does not. A tutorial message must still be spoken while
    // the shot HUD is raised or a dialog is up — but claiming a key there would fight whoever owns
    // the surface.
    //
    // A raised modal is its own answer, and has to be: UITutorialBox is pushed ABOVE the tactical
    // HUD, so the tactical gate reports it as a BLOCKER, and it may be shown during a cinematic
    // (OnInit calls AllowShowDuringCinematic), which trips the screen stack's own gate too. Either
    // one would have taken the re-read key away at exactly the moment a player most wants it.
    void* box = GameUI::FindTutorialBox();
    const GameUI::TacticalGate gate = GameUI::ReadTacticalGate();
    const GameUI::StackState stack = GameUI::ReadStackState();
    const bool active
        = box || (gate.hudOnStack && !gate.blocked && (!stack.valid || stack.Interactive()));
    if (active != gActive) {
        gActive = active;
        LOG_INFO("TutorialReader: %s (frame %d, controls %s)", active ? "active" : "inactive",
            tut.historyFrame, tut.allowingControls ? "player" : "replay");
    }

    PollRejection(tut);
    PollModal(box);

    void* mgr = GameUI::FindWorldMessageMgr();
    if (!mgr)
        return; // no messenger to read (between missions, or mid-transition)
    PollWorldMessages(mgr, tut.inTutorial);
    PollHudBlade(mgr);
}

#ifdef X2ACCESS_DEVSERVER

std::string Describe()
{
    const GameUI::TutorialState tut = GameUI::ReadTutorialState();
    std::string out = "tutorial-mgr: ";
    if (!tut.valid)
        out += "absent (not the tutorial mission)\n";
    else {
        char line[256];
        snprintf(line, sizeof(line),
            "present  in-tutorial=%d  controls=%s  frame=%d  next-input-frame=%d  "
            "invalid-submissions=%d  next-ability-context=%p\n",
            tut.inTutorial, tut.allowingControls ? "player" : "replay", tut.historyFrame,
            tut.nextInputFrame, tut.invalidSubmissions, tut.nextAbilityContext);
        out += line;
    }

    out += "claim-active: ";
    out += gActive ? "yes\n" : "no\n";

    void* box = GameUI::FindTutorialBox();
    if (box) {
        const GameUI::TutorialBoxView view = GameUI::ReadTutorialBox(box);
        out += "modal: \"" + view.title + "\" / \"" + view.body + "\" [" + view.buttonHelp + "]\n";
    } else {
        out += "modal: none\n";
    }

    void* mgr = GameUI::FindWorldMessageMgr();
    if (!mgr) {
        out += "world-message-mgr: unreachable (not in tactical?)\n";
        return out;
    }
    char head[128];
    snprintf(head, sizeof(head), "world-message-mgr: %p  hud-blade=%s\n", mgr,
        GameUI::HudBladeShowing(mgr) ? "up" : "down");
    out += head;

    static const char* const kTypeNames[] = {"World", "Damage", "Blade"};
    const std::vector<GameUI::WorldMessage> messages = GameUI::ReadWorldMessages(mgr);
    if (messages.empty())
        out += "  (no active messages)\n";
    for (const GameUI::WorldMessage& msg : messages) {
        char line[256];
        snprintf(line, sizeof(line), "  [%d] %-6s vis=%d imp=%d follow=%d id=\"%s\"\n", msg.slot,
            msg.type >= 0 && msg.type <= 2 ? kTypeNames[msg.type] : "?", msg.visible,
            msg.important, msg.followObjectId, msg.id.c_str());
        out += line;
        out += "       raw:    " + msg.text + "\n";
        std::string spoken = TranslateMarkup(msg.text);
        // Resolve the anchor here too, so the dump shows the line as it would actually be
        // SPOKEN rather than a version that quietly omits who it is about.
        if (msg.type == kMsgWorld && msg.followObjectId > 0) {
            const std::string who = AnchorName(msg.followObjectId);
            out += "       anchor: " + (who.empty() ? std::string("(unresolved)") : who) + "\n";
            if (!who.empty())
                spoken = who + ": " + spoken;
        }
        out += "       spoken: " + spoken + "\n";
    }
    return out;
}

#endif // X2ACCESS_DEVSERVER

} // namespace TutorialReader
