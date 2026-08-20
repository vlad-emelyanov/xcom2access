#include "narrator.h"
#include "game_ui.h"
#include "tts.h"
#include "ue3_reflection.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <deque>
#include <mutex>

namespace Narrator {

namespace {

constexpr const char* kNarratorClass = "X2AccessNarrator";

// The tutorial's stand-in for the event bus. In a replay no game state is ever submitted, so not
// one X2EventListener fires — see X2AccessReplayNarration.uc for the chain that proves it. That
// class walks the visualized history instead and pushes into the same queue, so it is a producer
// like any other and needs only a nudge on the drain tick. It answers instantly when no replay is
// running, which is every normal mission.
constexpr const char* kReplayClass = "X2AccessReplayNarration";

// Must match X2AccessNarrator.ENTRY_SEP and its CHANNEL_ constants.
constexpr const char* kEntrySep = "~|~";
constexpr char kChannelAlert = 'A';

// How often we ask the companion whether anything happened. One outbound ProcessEvent per drain,
// and the answer is empty on almost all of them — but the call is cheap next to the thousands of
// ProcessEvents a running mission makes per second, and 100 ms is under the threshold at which a
// spoken reaction stops feeling attached to the event that caused it.
constexpr uint64_t kDrainIntervalMs = 100;

// Queue ceiling. Reaching it means one frame produced two dozen narratable events; speaking them
// all would run far past the moment they describe. The drop is LOGGED, never silent — the script
// side reports its own overflow on the alert channel for the same reason.
constexpr size_t kMaxPending = 24;

// Scrollback depth. Sized for "what happened this turn", which is what a review key needs.
constexpr size_t kHistoryDepth = 128;

struct Queued {
    Channel channel;
    Source source;
    std::string text;
};

// How long a flyover keeps the floor. World messages linger on screen about five seconds, and the
// state frame behind one arrives well inside that — measured live 2026-08-20, the flyover for an
// Overwatch landed one poll (~150 ms) ahead of the replay narrator's line for the same activation.
constexpr uint64_t kFlyoverEchoMs = 5000;

std::mutex sMutex;
std::deque<Queued> sPending;
std::deque<Line> sHistory;

// Set when an Alert joins the queue: the batch cuts whatever was still being read, but only at
// its FIRST line, so the alert does not truncate a combat line queued ahead of it in the same
// burst. Getting this wrong loses the damage line that explains the kill.
bool sBatchInterrupts = false;

bool IsInterrupt(Channel channel)
{
    return channel == Channel::Focus || channel == Channel::Aid;
}

// Caller holds sMutex.
void Record(Channel channel, Source source, const std::string& text)
{
    sHistory.push_back({GetTickCount64(), channel, source, text});
    while (sHistory.size() > kHistoryDepth)
        sHistory.pop_front();
}

// Trailing punctuation is the only difference between "Osei: Overwatch" off a flyover and
// "Osei: Overwatch." built from the state, so it cannot be part of the comparison.
std::string WithoutTrailingPunctuation(const std::string& text)
{
    size_t end = text.size();
    while (end > 0) {
        const unsigned char c = static_cast<unsigned char>(text[end - 1]);
        if (c > ' ' && c != '.' && c != '!')
            break;
        --end;
    }
    return text.substr(0, end);
}

// Did the game already put these exact words on screen itself? Caller holds sMutex. Checks the
// pending queue as well as the ring: a flyover queued this frame has not been recorded yet.
bool FlyoverAlreadySaid(const std::string& text, uint64_t now)
{
    const std::string key = WithoutTrailingPunctuation(text);
    if (key.empty())
        return false;

    for (const Queued& entry : sPending) {
        if (entry.source == Source::Flyover && WithoutTrailingPunctuation(entry.text) == key)
            return true;
    }
    for (auto it = sHistory.rbegin(); it != sHistory.rend(); ++it) {
        if (now - it->ms > kFlyoverEchoMs)
            break; // the ring is in time order, so everything older is older still
        if (it->source == Source::Flyover && WithoutTrailingPunctuation(it->text) == key)
            return true;
    }
    return false;
}

// Hand the queue to the backend. Only the first line of a batch may interrupt; the rest ride the
// screen reader's own queue, which is what keeps a burst in order.
void Flush()
{
    std::deque<Queued> batch;
    bool interruptFirst = false;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        if (sPending.empty())
            return;
        batch.swap(sPending);
        interruptFirst = sBatchInterrupts;
        sBatchInterrupts = false;
    }

    bool first = true;
    for (const Queued& entry : batch) {
        {
            std::lock_guard<std::mutex> lock(sMutex);
            Record(entry.channel, entry.source, entry.text);
        }
        TTS::Speak(entry.text, first && interruptFirst);
        first = false;
    }
}

// Let the replay narrator catch up to what the player has been shown. Runs BEFORE the drain, so a
// line it produces goes out on this tick rather than waiting 100 ms for the next one.
void PumpReplayNarration()
{
    void* cdo = GameUI::FindCompanionCDO(kReplayClass);
    if (!cdo)
        return;

    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, "PollReplay", zeros))
        LOG_WARN("Narrator: outbound PollReplay failed");
}

// One outbound call per drain, whatever the burst size — the payload is a single delimited string
// precisely so a twelve-event frame does not become twelve ProcessEvents.
void DrainCompanion()
{
    void* cdo = GameUI::FindCompanionCDO(kNarratorClass);
    if (!cdo) {
        // FindCompanionCDO logs the miss itself (companion mod not loaded).
        return;
    }

    // No-parameter function, but ProcessEvent still copies ParmsSize bytes out of our block.
    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, "DrainNarration", zeros)) {
        LOG_WARN("Narrator: outbound DrainNarration failed");
        return;
    }

    std::string payload
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (payload.empty())
        return;

    const size_t sepLen = strlen(kEntrySep);
    size_t pos = 0;
    while (true) {
        size_t next = payload.find(kEntrySep, pos);
        std::string entry = (next == std::string::npos) ? payload.substr(pos)
                                                        : payload.substr(pos, next - pos);
        if (!entry.empty()) {
            Channel channel = (entry[0] == kChannelAlert) ? Channel::Alert : Channel::Combat;
            Say(channel, entry.substr(1));
        }
        if (next == std::string::npos)
            break;
        pos = next + sepLen;
    }
}

} // namespace

const char* ChannelName(Channel channel)
{
    switch (channel) {
    case Channel::Focus: return "focus";
    case Channel::Aid: return "aid";
    case Channel::Alert: return "alert";
    case Channel::Combat: return "combat";
    }
    return "?";
}

void Say(Channel channel, const std::string& text, Source source)
{
    if (text.empty())
        return;

    if (IsInterrupt(channel)) {
        size_t dropped = 0;
        {
            std::lock_guard<std::mutex> lock(sMutex);
            dropped = sPending.size();
            sPending.clear();
            sBatchInterrupts = false;
            Record(channel, source, text);
        }
        // The player asked for this, so the queue behind it is stale by definition — but say so:
        // silence where narration was expected must never look like nothing happened.
        if (dropped)
            LOG_INFO("Narrator: %zu queued line(s) dropped by a %s read", dropped,
                ChannelName(channel));
        TTS::SpeakInterrupt(text);
        return;
    }

    std::lock_guard<std::mutex> lock(sMutex);

    // The same event can reach us twice (an effect that damages a unit once per source, a
    // re-triggered listener); saying it twice in a row is always wrong.
    if (!sPending.empty() && sPending.back().text == text)
        return;

    // The game already said it on screen. See Source in narrator.h for why this is one-way.
    if (source == Source::State && FlyoverAlreadySaid(text, GetTickCount64())) {
        LOG_DEBUG("Narrator: \"%s\" already shown as a flyover, not restating it", text.c_str());
        return;
    }

    if (sPending.size() >= kMaxPending) {
        LOG_WARN("Narrator: queue full (%zu), dropping \"%s\"", kMaxPending, text.c_str());
        return;
    }

    sPending.push_back({channel, source, text});
    if (channel == Channel::Alert)
        sBatchInterrupts = true;
}

void OnTick()
{
    static uint64_t sLastDrainMs = 0;
    uint64_t now = GetTickCount64();
    if (now - sLastDrainMs >= kDrainIntervalMs) {
        sLastDrainMs = now;
        PumpReplayNarration();
        DrainCompanion();
    }
    // Outside the drain throttle too: a line queued by native code this frame (the tactical HUD,
    // a future recipe) should not wait on the companion's cadence.
    Flush();
}

void DropPending()
{
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sPending.clear();
        sBatchInterrupts = false;
    }
    void* cdo = GameUI::FindCompanionCDO(kNarratorClass);
    if (cdo) {
        int64_t zeros[2] = {};
        (void)GameUI::CallScriptFunction(cdo, "ClearNarration", zeros);
    }
}

std::vector<Line> History(size_t maxLines)
{
    std::lock_guard<std::mutex> lock(sMutex);
    size_t count = sHistory.size();
    if (maxLines && maxLines < count)
        count = maxLines;
    return std::vector<Line>(sHistory.end() - static_cast<ptrdiff_t>(count), sHistory.end());
}

} // namespace Narrator
