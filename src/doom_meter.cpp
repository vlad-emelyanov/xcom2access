#include "doom_meter.h"

#include "Graph/GraphAnnouncer.hpp"
#include "flash_shadow.h"
#include "game_ui.h"
#include "log.h"
#include "narrator.h"
#include "script_call.h"
#include "vocab.h"
#include "wire_record.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace DoomMeter {

namespace {

using X2Access::Wire::ParseRecords;
using X2Access::Wire::RecField;
using X2Access::Wire::WireRecord;

constexpr const char* kScannerClass = "X2AccessGeoScanner";

// 150 ms. The pips are redrawn on the UI update after ClearPendingDoom submits, and two entries
// in one sequence are four seconds apart (DoomCameraPanComplete's re-arm), so this cannot merge
// two changes into one line. Off the geoscape the cost is one stack walk.
constexpr uint64_t kPollIntervalMs = 150;

// How long an edge waits for its message before speaking the numbers alone. The push and the
// redraw are the same script frame (ClearPendingDoom: SetDoomMessage, then Remove, then Submit;
// the next UpdateData draws), so the message is normally already recorded when the edge is seen.
// The wait covers the capture flushing a frame later than the read, not a missing message — a
// sequence that pushes none (the hidden fortress with no label) costs this much and no more.
constexpr uint64_t kMessageGraceMs = 400;

// The two pushes the game writes over the meter (UIStrategyMap_HUD.SetDoomMessage).
constexpr const char* kMessageAdded = "SetDoomMessage";
constexpr const char* kMessageRemoved = "SetDoomMessageRemove";

// ---- state: comparison tokens and last-seen values, never dereferenced across frames ----

void* gHud = nullptr;    // the HUD instance the values below belong to
void* gScreen = nullptr; // its screen — a fresh map spawns a fresh HUD, and both are compared
int gDoom = 0;           // CachedDoom as last seen
bool gCountdown = false; // bDoomCounterVisible as last seen
int gMax = 0;            // GetMaxDoom(), fetched once per HUD instance
uint64_t gMessageSeq = 0; // the newest message push already spoken (or already on the panel)

// An edge waiting for its words.
bool gEdgePending = false;
int gEdgeFrom = 0;
uint64_t gEdgeSinceMs = 0;

void Reset()
{
    gHud = nullptr;
    gScreen = nullptr;
    gDoom = 0;
    gCountdown = false;
    gMax = 0;
    gMessageSeq = 0;
    gEdgePending = false;
}

/// The newest message push on the HUD recorded AFTER `afterSeq`. Returns its sequence stamp and
/// fills `text`, or returns 0 when the only messages on the panel have already been spoken.
uint64_t NewestMessage(int32_t cacheIndex, uint64_t afterSeq, std::string& text)
{
    if (cacheIndex < 0)
        return 0;
    const std::vector<FlashShadow::Call> calls = FlashShadow::GetCalls(cacheIndex);
    for (auto it = calls.rbegin(); it != calls.rend(); ++it) {
        const FlashShadow::Call& call = *it;
        if (call.isField || !call.child.empty())
            continue;
        if (call.name != kMessageAdded && call.name != kMessageRemoved)
            continue;
        if (call.seq <= afterSeq)
            return 0; // everything older is older still
        const std::vector<std::string> args = call.StringArgs();
        text = args.empty() ? std::string() : args[0];
        return call.seq;
    }
    return 0;
}

/// The message sequence stamp to start from on a fresh HUD: whatever is already on the panel is
/// history, not news. Returns the newest message seq recorded, or 0.
uint64_t NewestMessageSeq(int32_t cacheIndex)
{
    std::string ignored;
    return NewestMessage(cacheIndex, 0, ignored);
}

const WireRecord* FetchRecord(const char* funcName, const char* kind,
    std::vector<WireRecord>& storage)
{
    std::string raw;
    if (!GameUI::CallCompanion(kScannerClass, funcName, nullptr, &raw))
        return nullptr;
    storage = ParseRecords(raw, GameUI::CleanText);
    for (const WireRecord& rec : storage)
        if (RecField(rec, "kind") == kind)
            return &rec;
    return nullptr;
}

/// A number and its unit word, or the number alone when the panel's word read empty — the
/// localized-var trap's failure shape is an empty string, and "0 , 3" is worse than "0, 3".
std::string Unit(const std::string& value, const std::string& word)
{
    std::string out = value;
    X2Access::Graph::AppendPhrase(out, word, " ");
    return out;
}

void Speak(const std::string& line)
{
    if (line.empty())
        return;
    // Alert: the game changed what the player is up against, and the line queues in order behind
    // whatever a burst is already saying rather than cutting it. Flyover, because every word here
    // was read off the screen — the message the game wrote and the pips it drew.
    Narrator::Say(Narrator::Channel::Alert, line, Narrator::Source::Flyover);
}

} // namespace

int FetchMax()
{
    std::vector<WireRecord> storage;
    const WireRecord* rec = FetchRecord("DescribeDoom", "doom", storage);
    if (!rec)
        return 0;
    return atoi(RecField(*rec, "max").c_str());
}

std::string MeterLine(const GameUI::DoomMeterView& view, int max)
{
    std::string line = view.title.empty() ? std::string(Vocab::DoomTitleFallback) : view.title;
    const std::string value = max > 0 ? Vocab::Fraction(view.cachedDoom, max)
                                      : std::to_string(view.cachedDoom);
    X2Access::Graph::AppendPhrase(line, value, " ");
    return line;
}

std::string CountdownLine(const GameUI::DoomMeterView& view)
{
    std::vector<WireRecord> storage;
    const WireRecord* rec = FetchRecord("DescribeDoomTimer", "timer", storage);
    if (!rec)
        return std::string();

    // The label ends in its own colon, which AppendPhrase answers with a plain space; the four
    // units read as a list, which is how the screen lays them out.
    std::string line = view.counterLabel;
    X2Access::Graph::AppendPhrase(line, Unit(RecField(*rec, "days"), view.days));
    X2Access::Graph::AppendPhrase(line, Unit(RecField(*rec, "hours"), view.hours));
    X2Access::Graph::AppendPhrase(line, Unit(RecField(*rec, "minutes"), view.minutes));
    X2Access::Graph::AppendPhrase(line, Unit(RecField(*rec, "seconds"), view.seconds));
    return line;
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    const uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    const GameUI::GeoscapeGate gate = GameUI::ReadGeoscapeGate();
    if (!gate.mapOnStack) {
        // Left the geoscape. The next map spawns a fresh HUD whose CachedDoom starts from the
        // current total, so nothing carries over — and doom that moved during a mission is
        // animated by the game on re-entry (HandlePendingDoom in the entry path), where this
        // sees it as an ordinary edge.
        if (gHud)
            Reset();
        return;
    }
    if (!gate.map)
        return; // a screen over the map: UpdateData early-returns there, so nothing can move

    const GameUI::DoomMeterView view = GameUI::ReadDoomMeter(gate.map);
    if (!view.hud)
        return;
    const int32_t cacheIndex = FlashShadow::PanelCacheIndex(view.hud);

    if (view.hud != gHud || gate.map != gScreen) {
        // A new HUD instance: take its values as the baseline and say nothing. Every message
        // already on its panel is history.
        gHud = view.hud;
        gScreen = gate.map;
        gDoom = view.cachedDoom;
        gCountdown = view.countdown;
        gEdgePending = false;
        gMessageSeq = NewestMessageSeq(cacheIndex);
        gMax = FetchMax();
        LOG_INFO("DoomMeter: watching HUD, doom %d of %d, countdown %d", gDoom, gMax,
            gCountdown ? 1 : 0);
        return;
    }

    if (view.cachedDoom != gDoom) {
        if (!gEdgePending) {
            gEdgePending = true;
            gEdgeFrom = gDoom;
            gEdgeSinceMs = now;
        }
        gDoom = view.cachedDoom;
    }

    if (gEdgePending) {
        std::string message;
        const uint64_t seq = NewestMessage(cacheIndex, gMessageSeq, message);
        if (seq || now - gEdgeSinceMs >= kMessageGraceMs) {
            if (seq)
                gMessageSeq = seq;
            const int delta = gDoom - gEdgeFrom;
            gEdgePending = false;

            // The message the game wrote over the meter, then how many blocks blinked, then the
            // meter as it now reads — the order the eye takes them in. Through AppendPhrase:
            // the message is the game's own string and may end in its own punctuation.
            std::string line = GameUI::CleanText(message);
            if (delta != 0)
                X2Access::Graph::AppendPhrase(line, Vocab::DoomDelta(delta));
            X2Access::Graph::AppendPhrase(line, MeterLine(view, gMax));
            LOG_INFO("DoomMeter: %d -> %d, message seq %llu", gEdgeFrom, gDoom,
                static_cast<unsigned long long>(seq));
            Speak(line);
        }
    }

    if (view.countdown != gCountdown) {
        gCountdown = view.countdown;
        // The project completing is the red overlay and the timer replacing the pips; the timer
        // leaving (a facility destroyed in time) is the pips coming back.
        Speak(gCountdown ? CountdownLine(view) : MeterLine(view, gMax));
    }
}

} // namespace DoomMeter
