#include "text_input.h"
#include "Graph/GraphAnnouncer.hpp"
#include "game_ui.h"
#include "input_arbiter.h"
#include "narrator.h"
#include "vocab.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>

namespace TextInput {

namespace {

// The appearance poll. Off this screen it costs one screen-stack walk and no outbound call at
// all; while the dialog IS up, reading is driven by key traffic instead of by this clock.
constexpr uint64_t kPollIntervalMs = 150;

// A field read costs four outbound calls, two of which leak the FString they answer with, so it
// runs on a clock of its own rather than on the frame pump's ~60 Hz.
constexpr uint64_t kReadIntervalMs = 33;

// How long a key gets to show up in the field before we stop waiting for it. The window
// procedure sees the key before the engine has handed it to Flash, so the first read after a
// keystroke can still show the old state; but a key the field REFUSED (the box is full, or
// Backspace at position zero) never changes anything, and that must not spin.
constexpr uint64_t kSettleMs = 400;

// A slow heartbeat, so a change the keyboard did not cause — a mouse click placing the caret, a
// drag selecting a word — is still noticed. Two reads a second is affordable; polling at frame
// rate would not be.
constexpr uint64_t kHeartbeatMs = 500;

// ---- what the window procedure recorded ----
//
// UE3 pumps window messages on the game thread, so this is written by the observer and read by
// OnTick on ONE thread (see the note at the top of input_arbiter.h). The observer does nothing
// else — no engine reads, no speech — and it records only that a key HAPPENED: what the key did
// is the field's business, and since 2026-08-21 the field itself is readable.
bool gKeySeen = false;

// ---- state ----
void* gScreen = nullptr; // the dialog being tracked; compared only, never dereferenced
GameUI::TextFieldState gLast;
bool gHaveLast = false;
bool gPending = false; // a key is still settling
uint64_t gPendingUntilMs = 0;
uint64_t gLastReadMs = 0;

// Everything this tick has to say, joined into ONE utterance and flushed at the end of it, so
// two things noticed in the same read arrive as one line rather than two. Whether the echo cuts
// off the reader's current utterance is the screen reader's own call (TTS carries no interrupt
// flag); a reader set to interrupt on new text keeps the echo at typing speed.
std::string gSpoken;

void Reset()
{
    gScreen = nullptr;
    gLast = GameUI::TextFieldState{};
    gHaveLast = false;
    gPending = false;
    gLastReadMs = 0;
    gKeySeen = false;
    gSpoken.clear();
}

std::string Utf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], needed,
        nullptr, nullptr);
    return out;
}

// One character, as a screen reader should hear it.
std::string SpeakableChar(wchar_t ch)
{
    if (const char* name = Vocab::PunctuationName(ch))
        return name;
    return Utf8(std::wstring(1, ch));
}

// A run: one character gets its spoken name, several are spoken as the text they are.
std::string SpeakableRun(const std::wstring& run)
{
    return run.size() == 1 ? SpeakableChar(run[0]) : Utf8(run);
}

void Say(const std::string& part)
{
    if (part.empty())
        return;
    if (!gSpoken.empty())
        gSpoken += ", "; // a separator the reader pauses on, so letters stay letters
    gSpoken += part;
}

void Flush()
{
    if (gSpoken.empty())
        return;
    Narrator::Say(Narrator::Channel::Focus, gSpoken);
    gSpoken.clear();
}

// The observer. Runs inside the window procedure: record and nothing else.
void OnObservedKey(const InputArbiter::KeyEvent& e)
{
    if (e.down && gScreen)
        gKeySeen = true;
}

void OnObservedChar(wchar_t)
{
    if (gScreen)
        gKeySeen = true;
}

// What the caret is standing on. For a text cursor that is the character to its RIGHT — the one
// the next Delete removes and the one an insertion pushes along.
std::string CaretText(const GameUI::TextFieldState& s)
{
    const int len = static_cast<int>(s.text.size());
    if (len == 0)
        return Vocab::TextInputEmpty;
    if (s.caret <= 0)
        return std::string(Vocab::TextInputStart) + ", " + SpeakableChar(s.text[0]);
    if (s.caret >= len)
        return Vocab::TextInputEnd;
    return SpeakableChar(s.text[static_cast<size_t>(s.caret)]);
}

std::string SelectionText(const GameUI::TextFieldState& s)
{
    if (!s.HasSelection())
        return {};
    const size_t begin = static_cast<size_t>(s.selBegin);
    const size_t count = static_cast<size_t>(s.selEnd - s.selBegin);
    if (begin + count > s.text.size())
        return {};
    // AppendPhrase for the same reason AnnounceDialog uses it: the selected run is whatever the
    // player typed, and a name ending in "?" or ":" must not pick up a comma on top of it.
    std::string out = Utf8(s.text.substr(begin, count));
    X2Access::Graph::AppendPhrase(out, Vocab::Selected);
    return out;
}

// The whole field, said once when the dialog appears. A fully selected prefill is the case that
// matters most, and the one a caret model could never have seen: it means the next keystroke
// REPLACES the name, which the player has every right to know before pressing a key. Measured
// live 2026-08-21 — the save-name box really does open with begin 0, end 12, caret 12.
void AnnounceDialog(const GameUI::TextInputView& view, const GameUI::TextFieldState& s)
{
    // ⛔ AppendPhrase, not a bare ", ". The PROMPT is the game's own string and these end in a
    // colon — "ВВЕДИТЕ НАЗВАНИЕ:" spoke as "ВВЕДИТЕ НАЗВАНИЕ:, edit box" in the 2026-09-04 ring.
    // This is the case CLAUDE.md means by "any surface that composes speech outside a recipe owes
    // the same call": the hand-rolled lambda that used to live here was a fork of the rule.
    std::string line = view.prompt;
    auto append
        = [&line](const std::string& part) { X2Access::Graph::AppendPhrase(line, part); };
    append(Vocab::TextInputRole);
    append(s.text.empty() ? std::string(Vocab::TextInputEmpty) : Utf8(s.text));
    if (s.HasSelection() && static_cast<size_t>(s.selEnd - s.selBegin) == s.text.size())
        append(Vocab::Selected);
    if (line.empty())
        return;
    // Alert, not Focus: the player did not press a key to get HERE — a screen appeared over what
    // they were doing. The channel is the label the scrollback shows; routing is the same.
    Narrator::Say(Narrator::Channel::Alert, line);
}

size_t CommonPrefix(const std::wstring& a, const std::wstring& b)
{
    size_t n = 0;
    while (n < a.size() && n < b.size() && a[n] == b[n])
        n++;
    return n;
}

// Say what changed between two readings. Ordered by what a player needs first: an edit outranks
// a selection change, which outranks a bare caret move — and one keystroke can only really be
// one of the three.
void AnnounceChange(const GameUI::TextFieldState& before, const GameUI::TextFieldState& now)
{
    if (now.text != before.text) {
        const size_t prefix = CommonPrefix(before.text, now.text);
        size_t suffix = 0;
        while (suffix < before.text.size() - prefix && suffix < now.text.size() - prefix
            && before.text[before.text.size() - 1 - suffix]
                == now.text[now.text.size() - 1 - suffix])
            suffix++;
        const std::wstring removed
            = before.text.substr(prefix, before.text.size() - prefix - suffix);
        const std::wstring inserted = now.text.substr(prefix, now.text.size() - prefix - suffix);

        // An insertion outranks the deletion it may have replaced: typing over a selection is
        // one action, and the player means the character they typed.
        if (!inserted.empty())
            Say(SpeakableRun(inserted));
        else if (!removed.empty())
            Say(SpeakableRun(removed));
        if (now.text.empty())
            Say(Vocab::TextInputEmpty);
        return;
    }

    if (now.HasSelection() && (now.selBegin != before.selBegin || now.selEnd != before.selEnd)) {
        Say(SelectionText(now));
        return;
    }

    if (now.caret != before.caret)
        Say(CaretText(now));
}

} // namespace

void Init()
{
    // No claims, and that is the design: every key here belongs to the text field. The observer
    // watches the traffic on its way past — the field still receives everything — and exists to
    // TRIGGER a read, because reading the field costs outbound calls that must not free-run.
    InputArbiter::RegisterObserver(InputArbiter::Observer{
        OnObservedKey, OnObservedChar, "text input: read trigger"});
    LOG_INFO("TextInput: observer registered");
}

void OnTick()
{
    static uint64_t sLastPollMs = 0;
    uint64_t now = GetTickCount64();

    // While no dialog is tracked this is a throttled stack walk and nothing else.
    if (!gScreen && now - sLastPollMs < kPollIntervalMs)
        return;
    sLastPollMs = now;

    void* screen = GameUI::FindTextInputScreen();
    if (!screen) {
        if (gScreen)
            LOG_INFO("TextInput: dialog closed");
        Reset();
        return;
    }

    if (screen != gScreen) {
        Reset();
        gScreen = screen;
        GameUI::TextInputView view = GameUI::ReadTextInput(screen);
        gLast = GameUI::ReadFocusedTextField(screen);
        gHaveLast = gLast.valid;
        gLastReadMs = now;
        LOG_INFO("TextInput: dialog up (prompt '%s', max %d, multiline %d) — field '%s', "
                 "%zu chars, caret %d, selection %d..%d",
            view.prompt.c_str(), view.maxChars, view.multiLine ? 1 : 0, gLast.focusPath.c_str(),
            gLast.text.size(), gLast.caret, gLast.selBegin, gLast.selEnd);
        AnnounceDialog(view, gLast);
        return;
    }

    if (gKeySeen) {
        gKeySeen = false;
        gPending = true;
        gPendingUntilMs = now + kSettleMs;
    }

    // Read while a key is settling, and on the slow heartbeat that catches what the keyboard did
    // not cause.
    const bool heartbeat = now - gLastReadMs >= kHeartbeatMs;
    if ((!gPending && !heartbeat) || now - gLastReadMs < kReadIntervalMs)
        return;
    gLastReadMs = now;

    const GameUI::TextFieldState state = GameUI::ReadFocusedTextField(screen);
    if (!state.valid)
        return; // nothing has AS focus; there is no field to describe

    if (gHaveLast) {
        const bool changed = state.text != gLast.text || state.caret != gLast.caret
            || state.selBegin != gLast.selBegin || state.selEnd != gLast.selEnd;
        if (changed) {
            AnnounceChange(gLast, state);
            gPending = false; // the keystroke landed
        } else if (gPending && static_cast<int64_t>(now - gPendingUntilMs) >= 0) {
            // It never will: the field refused the key (box full, Backspace at position zero, an
            // arrow at the edge). Refusals stay silent, the same rule the rest of the layer
            // follows.
            gPending = false;
        }
    }
    gLast = state;
    gHaveLast = true;
    Flush();
}

} // namespace TextInput
