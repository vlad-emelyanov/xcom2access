#include "resource_strip.h"

#include "Graph/GraphAnnouncer.hpp"
#include "game_ui.h"
#include "input_arbiter.h"
#include "log.h"
#include "narrator.h"
#include "vocab.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>

namespace ResourceStrip {

namespace {

// 250 ms, the same beat the dropship reader uses and for the same reason: nothing here answers a
// keypress. The strip is repainted by the game only on a screen change, so a quarter-second poll
// already resolves every edge there is. Outside the strategy layer the cost is one stack walk.
constexpr uint64_t kPollIntervalMs = 250;

// The claim's gate, recomputed on the tick. ⛔ The arbiter evaluates `activeWhen` INSIDE the
// window procedure, where reading engine memory is forbidden — so this is a flag read and the
// stack walk that sets it happens on the game thread.
bool gActive = false;

// The self-heal, and its throttle. `RefreshResourceStrip` is an outbound ProcessEvent, so it is
// rate-limited even though it is the same call the game's own screen listener makes on every
// focus change. Ten seconds is far longer than the case needs — the repaint is instant and the
// next poll sees it — and short enough that a player who alt-tabbed away for a while comes back
// to a working key.
constexpr uint64_t kRepairIntervalMs = 10000;
uint64_t gLastRepairMs = 0;

/// The whole strip as one line: every resource, its value, and the state its colour carries.
///
/// ⛔ This is the AID, so it deliberately does what the focused-item rule forbids a stop from
/// doing — it reads everything. The rule is about not dumping a screen the player is navigating;
/// a key pressed to ask "what have I got" has no other honest answer, and the strip is nine short
/// pairs rather than a list of unknown length.
std::string BuildLine(const GameUI::ResourceStripView& strip)
{
    std::string out;
    for (const GameUI::ResourceEntry& entry : strip.entries) {
        // ⛔ Every part goes through AppendPhrase, never a bare ", ". These are the GAME's own
        // words — `GetResourceDisplayName` returns whole localized nouns and the labels are
        // Caps()'d localized strings — and the project has already been bitten by joining such
        // strings blind. The value takes a space after its own label rather than a comma, which
        // is how the strip reads on screen ("SUPPLIES §217"), while the pairs are separated from
        // each other.
        std::string pair = entry.label;
        X2Access::Graph::AppendPhrase(pair, entry.value, " ");
        if (entry.state == GameUI::ResourceState::Critical)
            X2Access::Graph::AppendPhrase(pair, Vocab::ResourceCritical);
        else if (entry.state == GameUI::ResourceState::Warning)
            X2Access::Graph::AppendPhrase(pair, Vocab::ResourceLow);
        X2Access::Graph::AppendPhrase(out, pair);
    }
    return out;
}

void OnAnnounce(const InputArbiter::KeyEvent& e)
{
    if (!e.down || e.repeat) // a held key would only stutter the same line over itself
        return;

    const GameUI::ResourceStripView strip = GameUI::ReadResourceStrip();
    if (!strip.showing)
        return; // the gate went stale between the tick and the keypress

    // ⛔ An empty answer must not be SILENCE. The strip is on screen — the player can see it and
    // a sighted player is reading it — so saying nothing would mean "there are no resources",
    // which is a different and false statement. This is the same failure mode as a truncated
    // tail: indistinguishable from the truth, and therefore never reported as a bug.
    const std::string line = strip.entries.empty() ? Vocab::ResourcesNotRead : BuildLine(strip);

    // Aid: an on-demand read the player asked for, labelled as such in the scrollback.
    Narrator::Say(Narrator::Channel::Aid, line);
}

} // namespace

void Init()
{
    using namespace InputArbiter;

    // ⚠ A bare letter, and the leniency argument is in the header: `R` carries no bare binding in
    // either input section that can be live here, so this displaces nothing and needs no config
    // snippet. It is also gated tightly enough that it cannot reach tactical, where `R` IS bound.
    RegisterClaim({'R', ModNone, [] { return gActive; }, OnAnnounce, "Resources: read the strip"});

    LOG_INFO("ResourceStrip: claims registered");
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    const uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    const GameUI::ResourceStripView strip = GameUI::ReadResourceStrip();
    gActive = strip.showing;
    if (!gActive)
        return;

    // The strip is up and the capture has nothing for it — see RefreshResourceStrip for the two
    // ways that happens. Ask the game to repaint, which is what makes the key work on a screen
    // the player has been sitting on since before the capture had the panel.
    if (strip.entries.empty() && now - gLastRepairMs >= kRepairIntervalMs) {
        gLastRepairMs = now;
        if (GameUI::RefreshResourceStrip(strip))
            LOG_INFO("ResourceStrip: strip is showing with nothing captured — asked the HUD to "
                     "repaint");
    }
}

} // namespace ResourceStrip
