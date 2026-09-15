#include "dropship.h"

#include "Graph/GraphAnnouncer.hpp"
#include "game_ui.h"
#include "input_arbiter.h"
#include "log.h"
#include "narrator.h"
#include "ue3_reflection.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>

namespace Dropship {

namespace {

constexpr const char* kReaderClass = "X2AccessDropshipReader";

// 250 ms. Slower than the shot HUD's 150 ms and deliberately so: nothing here answers a keypress,
// and the one edge that matters — the map finishing loading — is published by the screen's OWN
// 1 s timer, so a quarter-second poll already resolves it four times finer than the game does.
// The cost outside the dropship is one stack walk and no outbound call at all.
constexpr uint64_t kPollIntervalMs = 250;

// The claim's active predicate result, recomputed on the tick. The arbiter evaluates activeWhen
// inside the window procedure, where a stack walk is forbidden, so this stays a flag read.
bool gActive = false;

// The screen we have already introduced. A POINTER, not a bool: the transition map destroys and
// rebuilds the presentation layer around this screen, and the mission-start and mission-end
// briefings are different objects, so a changed pointer is exactly "a new briefing to read".
void* gAnnounced = nullptr;
bool gWasReady = false;

// The briefing words are the one part that can be missing on the first poll and present on the
// next, so they get a bounded retry rather than one shot. Two independent reasons, and the
// second is not a race we can design away:
//
//   * FindCompanionCDO throttles its GObjects scan to once a SECOND and the throttle is SHARED
//     across every class it looks up — so a cold first lookup here answers null purely because
//     some other module scanned a beat earlier, whether or not our CDO exists.
//   * the screen is pushed from a 0.1 s retry timer on the transition map, and nothing
//     guarantees our first poll lands after the history has everything the companion reads.
//
// Failing to retry would leave a player with the loading line and no briefing at all — and a
// briefing that is silently absent sounds exactly like a mission that has none, which is the
// truncation failure mode this project treats as a hard rule.
bool gBriefingSaid = false;
uint64_t gAttachMs = 0;
constexpr uint64_t kBriefingRetryMs = 4000;

/// The briefing's own words: operation name, location, objectives, briefing paragraph.
///
/// UpdateBriefingScreen resolves all of these into LOCALS and pushes them through one
/// MC.BeginFunctionOp("updatePreBriefing"), retaining none — so the companion re-derives them
/// from the same two places the screen read them from (the BattleData singleton and the mission
/// template). Empty when the companion is not loaded, which is a degradation and not a failure:
/// the native half below still says that a load is running and when it is done.
std::string ReadBriefingWords()
{
    std::string words;
    if (!GameUI::CallCompanion(kReaderClass, "DescribeBriefing", nullptr, &words))
        return {}; // the shared helper logs both a missing companion and a failed call
    return GameUI::CleanText(words);
}

/// Append with the kernel's AppendPhrase rule. Every part here is a string the GAME wrote, and
/// the game ends plenty of them with ':' or '.' — "LOCATION:" and "OBJECTIVE:" are literally two
/// of them — so a bare ", " join would voice "ЗАДАЧА:," at the player. A self-punctuated part
/// takes a plain space; everything else takes the separator.
///
/// ⛔ The rule itself is NOT re-derived here. This used to spell the terminator set out by hand,
/// which is how the project ended up voicing one game string two ways depending on which surface
/// read it. `". "` rather than the default `", "` because the parts are whole SENTENCES the game
/// wrote — and a self-punctuated tail still takes a plain space, which is why this cannot just
/// append a period of its own.
void AppendPart(std::string& out, const std::string& part)
{
    X2Access::Graph::AppendPhrase(out, part, ". ");
}

/// The screen's own status line: the loading text, or the launch prompt once the map is there.
///
/// Never both — once the map is loaded the loading line is stale, and the game itself swaps one
/// for the other in the same statement block that flips the button.
const std::string& StatusLine(const GameUI::DropshipView& v)
{
    return v.ready ? v.prompt : v.loadingText;
}

/// The whole block, in SPEECH order rather than draw order.
///
/// The screen draws the briefing paragraph above the objective list. Speech reverses them (the
/// companion does that half): the objectives are what the player acts on and the paragraph is
/// flavour, and speech is serial — making someone sit through a paragraph to reach the objective
/// is the same mistake the save row's leading timestamp was. The tip goes last for the same
/// reason; it is the least actionable thing on the screen.
std::string BuildBlock(const GameUI::DropshipView& v, const std::string& briefing, bool includeTip)
{
    std::string out;
    AppendPart(out, briefing);
    AppendPart(out, StatusLine(v));
    if (includeTip)
        AppendPart(out, v.tip);
    return out;
}

void OnAnnounce(const InputArbiter::KeyEvent& e)
{
    if (!e.down || e.repeat) // a held key would only stutter the same line over itself
        return;
    GameUI::DropshipView v = GameUI::ReadDropship();
    if (!v.screen)
        return; // the gate went stale between the tick and the keypress
    // Aid: the player asked for a re-read. It queues in order like everything else; a reader set
    // to interrupt on new text is what cuts the VO subtitle they are re-reading past.
    Narrator::Say(
        Narrator::Channel::Aid, BuildBlock(v, ReadBriefingWords(), /*includeTip=*/true));
}

} // namespace

void Init()
{
    using namespace InputArbiter;

    // Delete: re-announce the briefing, tip included. The same verb the tile cursor, the shot
    // HUD, the hacking pad and the ant farm put on this key, and it contends with none of them —
    // every one of those gates on tactical or on a strategy screen, and this screen lives on the
    // transition map where none of them exists.
    //
    // ⚠ The cost is real and worth stating: a claimed key is swallowed in the window procedure,
    // so while this screen is up Delete stops being one of the "any key" that launches. That is
    // the same trade the ant farm made for the same key, and it is the right one here — the
    // player has to be able to re-hear a briefing the pre-mission VO talked over, and Enter,
    // Space, Escape and every other key still launch.
    RegisterClaim({VK_DELETE, ModNone, [] { return gActive; }, OnAnnounce,
        "Dropship briefing: re-announce"});

    LOG_INFO("Dropship: claims registered");
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    const uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    GameUI::DropshipView v = GameUI::ReadDropship();
    gActive = v.screen != nullptr;

    if (!v.screen) {
        if (gAnnounced) {
            LOG_INFO("Dropship: briefing gone");
            gAnnounced = nullptr;
            gWasReady = false;
            gBriefingSaid = false;
        }
        return;
    }

    if (v.screen != gAnnounced) {
        gAnnounced = v.screen;
        gWasReady = v.ready;
        gAttachMs = now;
        const std::string briefing = ReadBriefingWords();
        gBriefingSaid = !briefing.empty();
        LOG_INFO("Dropship: %s briefing up at %p (ready %d, briefing %d)",
            v.missionStart ? "mission-start" : "mission-end", v.screen, v.ready ? 1 : 0,
            gBriefingSaid ? 1 : 0);
        // Combat channel: this queues in order rather than dropping what is pending, because
        // the dropship's own narrative moment starts at roughly the same instant and
        // TransientReader is already reading its subtitle. Losing Central's line would be worse
        // than waiting our turn. The tip is left out here for the same reason — it is on Delete.
        const std::string block = BuildBlock(v, briefing, /*includeTip=*/false);
        if (!block.empty())
            Narrator::Say(Narrator::Channel::Combat, block, Narrator::Source::Flyover);
        return;
    }

    // The briefing retry. It says only the words that were missing, never the status line again:
    // the player has already been told the mission is loading, and repeating it would make the
    // retry sound like a second briefing rather than the rest of the first.
    if (!gBriefingSaid && now - gAttachMs < kBriefingRetryMs) {
        const std::string briefing = ReadBriefingWords();
        if (!briefing.empty()) {
            gBriefingSaid = true;
            LOG_INFO("Dropship: briefing words arrived %llu ms after the screen",
                static_cast<unsigned long long>(now - gAttachMs));
            Narrator::Say(Narrator::Channel::Combat, briefing, Narrator::Source::Flyover);
        }
    }

    // The edge this whole module exists for: the map has finished loading and the game is now
    // sitting there waiting for a keypress it will never announce. Alert, so it queues in order
    // without clearing what is pending behind it — the game changed what the player can DO.
    if (v.ready && !gWasReady) {
        gWasReady = true;
        LOG_INFO("Dropship: destination loaded — '%s'", v.prompt.c_str());
        if (!v.prompt.empty())
            Narrator::Say(Narrator::Channel::Alert, v.prompt, Narrator::Source::Flyover);
    }
}

} // namespace Dropship
