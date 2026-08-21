#include "unit_info.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "narrator.h"
#include "tactical_hud.h"
#include "ue3_reflection.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>

namespace UnitInfo {

namespace {

constexpr const char* kReaderClass = "X2AccessUnitReader";

// The same 150 ms every other tactical poll uses: fast enough that a soldier cycled with Tab is
// spoken before the player presses again, cheap enough to leave running for a whole mission.
constexpr uint64_t kPollIntervalMs = 150;

// One outbound call to the companion reader; the result is whatever it wrote to NativeResult.
// Runs from the frame pump or the arbiter's tick drain — both the game thread, which is what
// outbound ProcessEvent requires. Returns false only when the call could not be MADE; an empty
// result is a deliberate "nothing to say" and the poll relies on it every tick.
bool Call(const char* funcName, std::string* out)
{
    void* cdo = GameUI::FindCompanionCDO(kReaderClass);
    if (!cdo)
        return false; // FindCompanionCDO already logs the miss (companion mod not loaded)
    // No-parameter functions still get a zeroed buffer — ProcessEvent copies ParmsSize bytes
    // out of whatever it is handed.
    int64_t zeros[2] = {};
    if (!GameUI::CallScriptFunction(cdo, funcName, zeros)) {
        LOG_WARN("UnitInfo: outbound call %s failed", funcName);
        return false;
    }
    if (out)
        *out = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    return true;
}

// A pressed key must never be silent, so this says so when the companion is missing entirely —
// the one case the poll path swallows.
void Announce()
{
    std::string line;
    if (!Call("AnnounceUnit", &line)) {
        Narrator::Say(Narrator::Channel::Aid, "Unit reader unavailable.");
        return;
    }
    if (!line.empty())
        Narrator::Say(Narrator::Channel::Aid, line);
}

} // namespace

void Init()
{
    using namespace InputArbiter;

    // Active across the whole tactical HUD window rather than world mode alone: "how many
    // actions do I have left" and "am I still concealed" are questions the player asks WHILE
    // deciding a shot, and the shot HUD's own two keys are both about the target rather than
    // about the shooter. Shared with TacticalHUD rather than recomputed, for the reason the
    // scanner shares TileReader::IsActive() — surfaces that answer the same question separately
    // eventually answer it differently.
    auto active = [] { return TacticalHUD::IsUsable(); };

    // Semicolon. Unbound in every input section of DefaultInput.ini: the only two lines in that
    // file mentioning it at all are WidgetInputAliases character lists for the editor's edit
    // boxes, which bind no command. So this displaces nothing — not even a Ctrl chord, and it
    // needs no leniency argument of the kind the bare letters do.
    //
    // Auto-repeat off, matching every other re-read key in the layer: a held key would only
    // stutter the same line over itself.
    RegisterClaim({VK_OEM_1, ModNone, active,
        [](const InputArbiter::KeyEvent& e) {
            if (e.down && !e.repeat)
                Announce();
        },
        "unit info: selected unit"});

    LOG_INFO("UnitInfo: claims registered");
}

void OnTick()
{
    static uint64_t sLastMs = 0;
    static bool sInMission = false;

    // ⛔ The reset edge is the MISSION, not usability. IsUsable() drops on every pause menu,
    // every confirmation dialogue and every cinematic pod reveal — resetting there would
    // re-announce the same soldier each time one of those closed, which on a reveal-heavy map
    // is most of them. IsMissionActive() rises once when the mission starts, and that is when
    // last mission's selection has to be forgotten: object IDs live in the campaign's history
    // and a soldier keeps hers across missions, so without this the first selection of the next
    // mission can match the last of the previous one and never be spoken.
    bool inMission = TacticalHUD::IsMissionActive();
    if (inMission != sInMission) {
        sInMission = inMission;
        if (inMission)
            Call("ResetSelection", nullptr);
    }

    if (!TacticalHUD::IsUsable())
        return;

    uint64_t now = GetTickCount64();
    if (now - sLastMs < kPollIntervalMs)
        return;
    sLastMs = now;

    // Focus, not Combat: the player pressed Tab, and what they asked for outranks whatever
    // narration is still queued behind it — the channel interrupts and clears the queue.
    std::string line;
    if (Call("PollSelection", &line) && !line.empty())
        Narrator::Say(Narrator::Channel::Focus, line);
}

} // namespace UnitInfo
