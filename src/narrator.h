#pragma once

// Narrator — bridge v2's native half: the one place that decides the ORDER lines go out in, and
// the scrollback of everything that was said.
//
// ⛔ Nothing here interrupts, and nothing here drops a line for another. TTS::Speak carries no
// interrupt flag: every line is handed to the screen reader as queued text, in arrival order,
// and the READER decides whether a new line cuts in, under the player's own settings. What the
// Narrator owns is the order — speech is serial where the screen is parallel, so a turn that
// produces six flyovers needs a queue to survive at all.
//
// Channels are a LABEL on the line — what the scrollback ring and the log show it as — and all
// four route identically:
//   Focus  — a selection or screen changed under the player's own key.
//   Aid    — an on-demand read (re-announce, breakdown).
//   Alert  — the game changed what the player can DO: turn handover, concealment, a soldier down.
//   Combat — something happened: hits, damage, kills, status.
//
// Everything in the queue is flushed on the next frame pump, so the queue is a within-frame
// ordering device, not a delay. Beyond the flush the SCREEN READER's own queue takes over — which
// is also the current limit: once handed off, a line is the reader's. See kMaxPending for what
// that costs. DropPending() is the one deliberate discard, for a hard context change.
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

/// Narrate one line, queued in arrival order whatever the channel — callers never decide how a
/// line is ordered. Pass Source::Flyover when the words were read off the game's own display, so a
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
