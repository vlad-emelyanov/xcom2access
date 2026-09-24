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

// Case-insensitive equality over UTF-8, folded by Windows so Cyrillic folds too: the fortress's
// generic addition writes the meter's own title as its message, in title case where the panel
// draws it in capitals (`Проект "Аватар"` over `ПРОЕКТ "АВАТАР"`), and saying both is the title
// twice.
bool SameWords(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty())
        return false;
    std::wstring wa(a.size(), L'\0'), wb(b.size(), L'\0');
    const int la = MultiByteToWideChar(CP_UTF8, 0, a.data(), static_cast<int>(a.size()),
        wa.data(), static_cast<int>(wa.size()));
    const int lb = MultiByteToWideChar(CP_UTF8, 0, b.data(), static_cast<int>(b.size()),
        wb.data(), static_cast<int>(wb.size()));
    if (la <= 0 || lb <= 0)
        return false;
    return CompareStringEx(LOCALE_NAME_INVARIANT, NORM_IGNORECASE | NORM_IGNOREWIDTH,
               wa.data(), la, wb.data(), lb, nullptr, nullptr, 0) == CSTR_EQUAL;
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

std::string MeterValue(const GameUI::DoomMeterView& view, int max)
{
    return max > 0 ? Vocab::Fraction(view.cachedDoom, max) : std::to_string(view.cachedDoom);
}

std::string MeterTitle(const GameUI::DoomMeterView& view)
{
    return view.title.empty() ? std::string(Vocab::DoomTitleFallback()) : view.title;
}

std::string MeterLine(const GameUI::DoomMeterView& view, int max)
{
    std::string line = MeterTitle(view);
    // Same join as the Tab stop's label/value, so the change line and the stop read alike:
    // the first live run had the stop say `title, N out of M` and the watch `title N out of M`.
    X2Access::Graph::AppendPhrase(line, MeterValue(view, max));
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
    if (!gate.stackRead)
        return; // the stack goes blank for a poll while a popup is pushed: hold, decide nothing
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
    // ...but the STATE can, and the first UpdateData after the popup closes redraws it all at
    // once — which is why the values are held across the popup and compared on return, never
    // re-baselined. The max-doom sequence is the case: the cheat's five pips, the Lose-mode
    // popup and the countdown all landed under one popup on the 2026-09-14 run.

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

    // The project completing is the red overlay and the timer replacing the pips; the timer
    // leaving (a facility destroyed in time) is the pips coming back. When the flip arrives with
    // a doom edge — the last pips and the countdown land in one redraw, and so do the cheat's
    // reversal and the pips' return — the edge's line carries it, so the meter is not read twice.
    const bool countdownFlipped = view.countdown != gCountdown;
    gCountdown = view.countdown;
    if (countdownFlipped && !gEdgePending)
        Speak(gCountdown ? CountdownLine(view) : MeterLine(view, gMax));

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
            // A message that IS the title (the fortress's own progress) gets the count alone.
            const bool titled = SameWords(line, MeterTitle(view));
            if (delta != 0)
                X2Access::Graph::AppendPhrase(line, Vocab::DoomDelta(delta));
            std::string meter;
            if (gCountdown)
                meter = CountdownLine(view);
            if (meter.empty())
                meter = titled ? MeterValue(view, gMax) : MeterLine(view, gMax);
            X2Access::Graph::AppendPhrase(line, meter);
            LOG_INFO("DoomMeter: %d -> %d, message seq %llu, countdown %d", gEdgeFrom, gDoom,
                static_cast<unsigned long long>(seq), gCountdown ? 1 : 0);
            Speak(line);
        }
    }
}

} // namespace DoomMeter
