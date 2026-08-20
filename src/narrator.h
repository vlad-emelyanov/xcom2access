#pragma once

// Narrator — bridge v2's native half: the one place that decides whether a line CUTS IN or WAITS
// ITS TURN, and the scrollback of everything that was said.
//
// Until now every line in the project went out through TTS::SpeakInterrupt, which is right for
// focus (the player pressed a key; they want this answer, not the last one) and wrong for combat:
// a turn that produces six flyovers would speak six first syllables. Speech is serial where the
// screen is parallel, so the queue is not a nicety — it is the only way a burst survives at all.
//
// Channels, and why each sits where it does:
//   Focus  — a selection or screen changed under the player's own key. Cuts everything, including
//            queued combat: what they just asked for outranks what already happened.
//   Aid    — an on-demand read (re-announce, breakdown). Same reasoning, same treatment.
//   Alert  — the game changed what the player can DO: turn handover, concealment, a soldier down.
//            Queued IN ORDER with combat so a burst stays coherent, but the burst as a whole cuts
//            whatever was still being read.
//   Combat — something happened: hits, damage, kills, status. Queued, spoken in order.
//
// Everything in the queue is flushed on the next frame pump, so the queue is a within-frame
// ordering device, not a delay. Beyond the flush the SCREEN READER's own queue takes over — which
// is also the current limit: once handed off, a line can only be cancelled wholesale by an
// interrupt, never trimmed. See kMaxPending for what that costs.
//
// Thread rules: Say() and OnTick() are game-thread only (OnTick makes outbound ProcessEvent
// calls). History() is safe from any thread, because the dev harness reads it off-thread.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Narrator {

enum class Channel {
    Focus,
    Aid,
    Alert,
    Combat,
};

/// Where a line came from. Not a channel — two sources can share a channel and route identically.
/// This exists for exactly one job: the game shows a thing ONCE but we can reach it twice, from
/// the flyover it published and from the game state behind it. Inside the tutorial both paths are
/// live at once (the replay narrator stands in for the dead event bus), so an ability with no
/// target gets said twice, word for word.
///
/// ⛔ The suppression runs ONE WAY ONLY: a State line is dropped when a Flyover line already said
/// it. Never the reverse, and never State-against-State — identical text from two state events is
/// normal and load-bearing (two ADVENT troopers really are both called "ADVENT Trooper", and
/// collapsing a double kill into one line loses a kill). That is the same reasoning CLAUDE.md
/// records for per-unit world messages, applied to the other side of the bridge.
enum class Source {
    State,   // derived from the game state: event listeners, the replay narrator, HUD reads
    Flyover, // read off a surface the game itself put on screen
};

struct Line {
    uint64_t ms;
    Channel channel;
    Source source;
    std::string text;
};

const char* ChannelName(Channel channel);

/// Narrate one line. Routing is entirely a function of the channel — callers never decide whether
/// to interrupt. Pass Source::Flyover when the words were read off the game's own display, so a
/// state-derived restatement of the same thing can stand down.
void Say(Channel channel, const std::string& text, Source source = Source::State);

/// Game-tick pump: drain the companion's queued narration, then flush ours to the backend.
/// Throttled internally. Game thread only.
void OnTick();

/// Throw away anything not yet spoken, on both sides of the bridge. For a hard context change
/// (mission over, back to the shell) where finishing the previous burst would be wrong.
void DropPending();

/// Spoken-line scrollback, oldest first. `maxLines` 0 means everything held.
std::vector<Line> History(size_t maxLines = 0);

} // namespace Narrator
