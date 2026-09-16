#include "objective_reader.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "narrator.h"
#include "log.h"

#include "Graph/GraphAnnouncer.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ObjectiveReader {

namespace {

using X2Access::Graph::AppendPhrase;

// The gate is a cached bool because a claim's activeWhen runs INSIDE the window procedure and may
// not touch engine memory. OnTick does the looking; this only records the answer.
bool gActive = false;

// Cheap, but not free — it walks the screen stack. Four times a second is far more often than a
// HUD panel can appear or vanish, and it keeps the key live within a frame or two of a mission
// starting.
constexpr uint64_t kPollIntervalMs = 250;

// End a part as a SENTENCE so the next one joins with a plain space instead of a comma.
//
// This is the reason: every part of this readout is optional and joined by AppendPhrase, so
// without it the separator between two different OBJECTIVES is the same comma as the one between
// an objective and its own state word — "...группу «Адвента», completed, Не дайте Моксу
// погибнуть..." — and a listener cannot hear where one objective ends. A sighted player has one
// row per line and never faces this; speech is serial and has to say so.
//
// ⛔ Checked before appending, never appended blind: the GAME ends plenty of its own strings with
// their own punctuation, and doubling it is exactly the defect AppendPhrase exists to prevent.
void EndSentence(std::string& s)
{
    if (s.empty())
        return;
    char last = s.back();
    if (last == '.' || last == '!' || last == '?' || last == ':' || last == ';')
        return;
    s += '.';
}

// A counter row carries no words at all: the game draws three ICONS and three numbers, and the
// only thing naming what is being counted is the asset path. So the noun comes from the token —
// "img:///UILibrary_Common.objectives_civilian_rescued" -> noun "civilian", state "rescued".
//
// Parsed rather than tabled deliberately. A table of the six shipped constants would be a table
// of English words that goes stale the moment a mod adds a counter; splitting the token gives a
// real word for any counter, including ones we have never seen. It is still English, which is the
// honest limit — the game supplies no localized string for these anywhere.
void CounterWords(const std::string& token, std::string* noun, std::string* state)
{
    noun->clear();
    state->clear();
    if (token.empty())
        return;

    size_t dot = token.find_last_of('.');
    std::string leaf = dot == std::string::npos ? token : token.substr(dot + 1);

    constexpr const char kPrefix[] = "objectives_";
    if (leaf.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0)
        leaf.erase(0, sizeof(kPrefix) - 1);

    // The expansion's counters do not follow that shape. Supply Extraction's row is drawn from
    // UILibrary_XPACK_StrategyImages.MissionIcon_SupplyPickup / _SupplyDrop / _SupplyDestroyed
    // (measured live 2026-09-16), which the split below read as noun "MissionIcon", state
    // "SupplyPickup". Keyed by the token's leaf so a missing entry degrades to the leaf's own tail
    // — "12 SupplyDrop" — rather than shifting anything or inventing a noun. The states are the
    // counter's three slots in SeqAct_DisplayMissionCounter's own words: Current, Available, Lost.
    struct CounterToken { const char* leaf; const char* noun; const char* state; };
    static constexpr CounterToken kMissionIconTokens[] = {
        {"MissionIcon_SupplyPickup", "crates", "picked up"},
        {"MissionIcon_SupplyDrop", "crates", "available"},
        {"MissionIcon_SupplyDestroyed", "crates", "destroyed"},
    };
    constexpr const char kMissionIcon[] = "MissionIcon_";
    if (leaf.compare(0, sizeof(kMissionIcon) - 1, kMissionIcon) == 0) {
        for (const CounterToken& t : kMissionIconTokens) {
            if (leaf == t.leaf) {
                *noun = t.noun;
                *state = t.state;
                return;
            }
        }
        *state = leaf.substr(sizeof(kMissionIcon) - 1);
        return;
    }

    size_t split = leaf.find_last_of('_');
    if (split == std::string::npos) {
        *noun = leaf;
        return;
    }
    *noun = leaf.substr(0, split);
    *state = leaf.substr(split + 1);
}

// The counter row's DisplayLabel is the game's own placeholder, not words: XComGameState_
// ObjectivesList.CounterDisplayLabel = "CounterLabel" exists because "the counter does not have
// text" and the list needs a key to find the row by. Speaking it read "CounterLabel" in front of
// every crate count.
constexpr const char kCounterPlaceholderLabel[] = "CounterLabel";

// "civilian, 1 rescued of 2, 3 available, 4 dead" — every part whose amount the game did not set
// is left out, and an amount of -1 is exactly that "not set" (the struct's own default).
std::string DescribeCounter(const GameUI::ObjectiveLine& line)
{
    std::string haveNoun, haveState, availNoun, availState, lostNoun, lostState;
    CounterWords(line.haveImage, &haveNoun, &haveState);
    CounterWords(line.availableImage, &availNoun, &availState);
    CounterWords(line.lostImage, &lostNoun, &lostState);

    std::string out;
    // All three icons name the same subject, so it is said once. Take it from whichever token
    // actually resolved rather than assuming the first one did.
    const std::string& noun = !haveNoun.empty() ? haveNoun : (!availNoun.empty() ? availNoun : lostNoun);
    AppendPhrase(out, noun);

    // A number and its state word are ONE phrase ("3 picked up"), joined with a space, never
    // through AppendPhrase — that joins with a comma, and "3, picked up" offers the number as a
    // separate fact (heard live 2026-09-16). Same rule the stage counter's sub-label follows.
    auto counted = [](int amount, const std::string& state) {
        std::string s = std::to_string(amount);
        if (!state.empty())
            s += " " + state;
        return s;
    };
    if (line.haveAmount >= 0) {
        std::string part = counted(line.haveAmount, haveState);
        // CounterHaveMin is the target ("1 of 2 recovered"), and it is -1 when there is no target
        // to reach — a count that is simply a count.
        if (line.haveMin > 0)
            part += " of " + std::to_string(line.haveMin);
        AppendPhrase(out, part);
    }
    if (line.availableAmount >= 0)
        AppendPhrase(out, counted(line.availableAmount, availState));
    if (line.lostAmount >= 0)
        AppendPhrase(out, counted(line.lostAmount, lostState));
    return out;
}

} // namespace

// One row, spoken. The state words are OURS and therefore English — the game conveys completed
// and failed with a tick and a colour and no words at all, so there is nothing localized to
// borrow, and a blind player who is told neither cannot tell a finished objective from a live one.
// Exported (with DescribeMissionCounter below) for the tactical HUD recipe's Objectives stop —
// one composer is what keeps Ctrl+X and the graph from ever disagreeing about a row.
std::string DescribeLine(const GameUI::ObjectiveLine& line)
{
    std::string out;

    // Warning first: it is the reason the row is drawn in an alert style, and it changes how the
    // rest of the row should be heard rather than qualifying it afterwards.
    if (line.warning)
        AppendPhrase(out, "warning");

    if (line.label != kCounterPlaceholderLabel)
        AppendPhrase(out, line.label);

    if (line.haveAmount >= 0)
        AppendPhrase(out, DescribeCounter(line));

    // Timer is -1 when the row has none; 0 is a real value and means this is the last turn.
    if (line.timer >= 0)
        AppendPhrase(out, std::to_string(line.timer) + (line.timer == 1 ? " turn" : " turns"));

    // Only one of these can be set — SetCheckboxRow and SetTimerRow both branch completed before
    // failed — and neither is set on an objective still in play, which is the common case and
    // correctly says nothing.
    if (line.completed)
        AppendPhrase(out, "completed");
    else if (line.failed)
        AppendPhrase(out, "failed");

    // One objective is one sentence, so the next one is audibly a new item rather than another
    // clause of this one.
    EndSentence(out);
    return out;
}

namespace {

std::string Compose(const GameUI::ObjectiveListView& view)
{
    std::string out;
    // The heading leads the readout, so it is punctuated as a lead-in. The game draws it as a
    // bare word ("ЗАДАЧИ") because a title bar above a column needs no colon; read aloud in front
    // of the rows it introduces, it does.
    std::string heading = view.heading;
    if (!heading.empty() && heading.back() != ':')
        heading += ':';
    AppendPhrase(out, heading);

    bool darkAnnounced = false;
    int spoken = 0;
    for (const GameUI::ObjectiveLine& line : view.lines) {
        // The heading rows carry no words of their own — the title row pushes a class default
        // into Flash and leaves Data.DisplayLabel empty — so they are skipped and the headings
        // come from the class instead.
        if (line.title || !line.Speakable())
            continue;
        // Dark events are a second, separately-titled block on the same panel (strategy only in
        // practice). Announce its heading once, where the panel draws it.
        if (line.darkEvent && !darkAnnounced) {
            darkAnnounced = true;
            std::string dark = view.darkHeading;
            if (!dark.empty() && dark.back() != ':')
                dark += ':';
            AppendPhrase(out, dark);
        }
        AppendPhrase(out, DescribeLine(line));
        spoken++;
    }

    if (spoken == 0) {
        // Distinguishable from a broken read: the panel was found and walked, and it genuinely
        // had nothing on it. Between missions and early in the Avenger this is the truth.
        AppendPhrase(out, "none listed");
    }
    return out;
}

} // namespace

// "Приведите солдат на запасной маршрут, Осталось солдат 3."
//
// The sub-label and the number are ONE phrase, not two: "Осталось солдат" and "3" only mean
// anything together, and joining them with a comma would offer the number as a separate fact.
std::string DescribeMissionCounter(const GameUI::MissionCounter& c)
{
    std::string out;
    AppendPhrase(out, c.label);

    std::string value = c.subLabel;
    // Infinity is drawn as a symbol rather than a number, so there is no digit to read and
    // m_sCounter holds whatever was last set. Say the meaning instead.
    if (c.infinity)
        AppendPhrase(value, "unlimited");
    else if (!c.counter.empty())
        value += (value.empty() ? "" : " ") + c.counter;

    AppendPhrase(out, value);
    if (c.expired)
        AppendPhrase(out, "expired");
    if (c.locked)
        AppendPhrase(out, "locked");

    EndSentence(out);
    return out;
}

namespace {

// A pressed key must never be silent, so every path here ends in speech — including the one where
// the list vanished between the gate saying yes and the handler running, which is a real race:
// activeWhen is evaluated in the window procedure and this runs later, on the tick.
void Announce()
{
    GameUI::ObjectiveListView view = GameUI::ReadObjectives();
    std::vector<GameUI::MissionCounter> counters = GameUI::ReadMissionCounters();

    // The counters can outlive the list's usefulness but never replace it, so a missing list is
    // still the failure case — with one exception worth taking: if counters ARE showing, the
    // player gets those rather than nothing.
    if (!view.found && counters.empty()) {
        Narrator::Say(Narrator::Channel::Aid, "Objectives unavailable.");
        return;
    }

    std::string line = view.found ? Compose(view) : std::string();
    // Stage counters come AFTER the goals, because they qualify them: the list says what has to
    // happen, the counter says how much of it is left. Most missions have none.
    for (const GameUI::MissionCounter& c : counters)
        AppendPhrase(line, DescribeMissionCounter(c));

    Narrator::Say(Narrator::Channel::Aid, line);
}

} // namespace

void Init()
{
    using namespace InputArbiter;

    // Ctrl+X, and the Ctrl is load-bearing in both directions.
    //
    // Bare X is live: [XComGame.XComTacticalInput] binds it to the generic
    // "X_Key_Press | onrelease X_Key_Release" forwarder, which pushes FXS_KEY_X into the UI input
    // chain (the ability-hotkey path). We must not take it.
    //
    // And because UE3 matches modifiers LENIENTLY — a bare binding fires under any modifier the
    // player merely happens to be holding — an UNCLAIMED Ctrl+X would fire that same vanilla
    // action alongside ours. Claiming the chord is what stops the double-fire. The player keeps
    // the vanilla action on bare X, which is where it was.
    auto active = [] { return gActive; };

    // Auto-repeat off, matching every other re-read key in the layer: holding it would only
    // stutter the same list over itself.
    RegisterClaim({'X', ModCtrl, active,
        [](const InputArbiter::KeyEvent& e) {
            if (e.down && !e.repeat)
                Announce();
        },
        "objectives: read the mission objectives"});

    LOG_INFO("ObjectiveReader: claims registered");
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    static bool sLastActive = false;

    uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    // ⛔ Stand down while a text-entry dialog is up. Ctrl+X is CUT in a Flash text field, and the
    // arbiter's claim would swallow it before the field ever saw it — breaking an edit the player
    // is in the middle of, on a screen where they cannot see what happened. The objectives list
    // is reachable underneath that dialog (UIAvengerHUD stays on the stack), so presence alone is
    // not a sufficient gate.
    bool active = !GameUI::FindTextInputScreen() && GameUI::HasObjectiveList();

    if (active != sLastActive) {
        sLastActive = active;
        LOG_DEBUG("ObjectiveReader: %s", active ? "armed" : "stood down");
    }
    gActive = active;
}

} // namespace ObjectiveReader
