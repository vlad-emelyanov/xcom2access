// transient_reader.cpp — the transient surfaces (docs/popup-inventory.md §4)
//
// The radio subtitle, the toast stack and the strategy help blade. All three are things the game
// shows for a few seconds and takes away, none is focusable or dismissable, and none belongs in
// a graph recipe: a tab stop over a line that is about to vanish is worse than useless, and
// attaching would take the arrows away from the screen the player is actually working on.
//
// So this is a POLL, the same shape as TacticalHUD::OnTick — one outbound call per interval, the
// companion decides what is NEW, and native only speaks what comes back.
//
// ⛔ THE CHANGE DETECTION IS NOT HERE, and that is deliberate. Every one of these surfaces
// re-renders continuously (the commlink retypes its subtitle as the voice line scrolls; the
// notice list is rebuilt while a notice is alive), so "what is on screen" reads true for hundreds
// of consecutive polls. Only the model can turn that level signal into an edge, which is why
// X2AccessTransientReader keeps the last-spoken markers and this file keeps none.
//
// Why the highest-value item in the project sits in this unglamorous file: UINarrativeCommLink is
// how essentially ALL story dialogue in XCOM 2 is delivered. Without it a blind player gets the
// voice acting and nothing else — and nothing at all in a language whose VO they do not have.

#include "transient_reader.h"

#include "script_call.h"
#include "log.h"
#include "narrator.h"
#include "ue3_reflection.h"

#include <windows.h>

#include <string>
#include <vector>

namespace TransientReader {

namespace {

constexpr const char* kReaderClass = "X2AccessTransientReader";

// Matches the companion's LINE_SEP. One burst can carry several lines — a conversation line
// landing in the same frame as a toast — and each is spoken as its own utterance so the screen
// reader can break between them.
constexpr const char* kLineSep = "~|~";

// 250 ms. Slower than the tactical HUD's 150 ms on purpose: nothing here is a response to a
// keypress, so latency costs the player nothing, while the poll runs for the WHOLE session rather
// than only during a mission. The companion's not-showing path is a screen lookup and two null
// checks, which is what makes leaving it running affordable.
constexpr uint64_t kPollIntervalMs = 250;

std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> parts;
    const size_t sepLen = strlen(kLineSep);
    size_t start = 0;
    for (;;) {
        const size_t hit = text.find(kLineSep, start);
        if (hit == std::string::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, hit - start));
        start = hit + sepLen;
    }
}

} // namespace

void OnTick()
{
    static uint64_t sLastMs = 0;
    const uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    void* cdo = GameUI::FindCompanionCDO(kReaderClass);
    if (!cdo)
        return; // FindCompanionCDO logs the miss (companion mod not loaded)

    // No-parameter function; ProcessEvent still copies ParmsSize bytes, so it gets a zeroed block.
    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, "PollTransient", zeros)) {
        LOG_WARN("TransientReader: PollTransient failed");
        return;
    }

    const std::string result
        = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    if (result.empty())
        return; // the overwhelmingly common case — nothing changed since the last poll

    for (const std::string& line : SplitLines(result)) {
        if (line.empty())
            continue;
        // Combat, not Alert or Focus. These lines are things that HAPPENED, so they must queue in
        // order behind whatever is already pending rather than drop it: a story line that
        // displaced the damage readout explaining a soldier's death would be a strictly worse
        // trade. CleanText because the strategy layer colours plenty of its own strings.
        Narrator::Say(Narrator::Channel::Combat, GameUI::CleanText(line), Narrator::Source::Flyover);
    }
}

} // namespace TransientReader
