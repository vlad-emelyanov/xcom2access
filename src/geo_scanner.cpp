#include "geo_scanner.h"
#include "game_ui.h"
#include "input_arbiter.h"
#include "narrator.h"
#include "ue3_reflection.h"
#include "vocab.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace GeoScanner {

namespace {

constexpr const char* kScannerClass = "X2AccessGeoScanner";

// One outbound call to the companion, then speak whatever it wrote to NativeResult. Runs from the
// arbiter's tick drain — the game thread — which is what outbound ProcessEvent requires. An empty
// result is a deliberate "nothing to say", not an error.
//
// Aid, through the Narrator rather than TTS directly. Every line here is an on-demand read the
// player pressed a key for, so it outranks — and CLEARS — whatever is still queued. That matters
// on the geoscape specifically: TransientReader queues commlink and toast lines on the Combat
// channel alongside this scanner, and nothing else drops them here (TacticalHUD::OnTick's
// DropPending fires only on the tactical layer's falling edge). It is also what puts these lines
// in the scrollback ring, which a review key will need.
void Call(const char* funcName, void* params)
{
    // The missing-companion case is checked FIRST so a pressed key is never silent — the shared
    // helper would log it, but the player deserves the spoken line.
    if (!GameUI::FindCompanionCDO(kScannerClass)) {
        Narrator::Say(Narrator::Channel::Aid, Vocab::SurfaceUnavailable("Geoscape scanner"));
        return;
    }
    std::string result;
    if (!GameUI::CallCompanion(kScannerClass, funcName, params, &result))
        return;
    // A companion payload is a GAME string: the mission difficulty label arrives as
    // `<font color='#fdce2b'>Сложно</font>`, and the first facilities-ring hearing voiced the tag.
    if (!result.empty())
        Narrator::Say(Narrator::Channel::Aid, GameUI::CleanText(result));
}

void StepCall(const char* funcName, int32_t delta)
{
    struct {
        int32_t delta;
    } params{delta};
    Call(funcName, &params);
}

// A no-parameter companion function still gets a zeroed buffer — ProcessEvent copies ParmsSize
// bytes out of whatever it is handed.
void SimpleCall(const char* funcName)
{
    int64_t zeros[2] = {};
    Call(funcName, zeros);
}

// Once per press, no auto-repeat: a held cycle key would race through a short ring faster than the
// reader can speak any of it. Same discipline as the tactical scanner.
std::function<void(const InputArbiter::KeyEvent&)> OnPress(std::function<void()> fn)
{
    return [fn = std::move(fn)](const InputArbiter::KeyEvent& e) {
        if (e.down && !e.repeat)
            fn();
    };
}

} // namespace

bool IsActive()
{
    return GameUI::ReadGeoscapeGate().Active();
}

void Init()
{
    using namespace InputArbiter;

    auto active = [] { return IsActive(); };

    // ---- the ring ----
    //
    // PageUp/PageDown carry no Bindings= entry in ANY input section, so both the bare pair and the
    // Ctrl pair are free here — and the Ctrl pair cannot double-fire off UE3's lenient modifier
    // matching, because there is no bare binding for it to also match. Same keys as the tactical
    // scanner's ring, deliberately: one instrument, two layers.
    //
    // ⚠ The menu navigator claims PageUp/PageDown too, but only while the SETTINGS screen is
    // attached (its tab shortcut), so the two claims can never both be live.
    RegisterClaim({VK_NEXT, ModNone, active, OnPress([] { StepCall("ScanCurrent", 1); }),
        "geo ring: next in category"});
    RegisterClaim({VK_PRIOR, ModNone, active, OnPress([] { StepCall("ScanCurrent", -1); }),
        "geo ring: previous in category"});
    RegisterClaim({VK_NEXT, ModCtrl, active, OnPress([] { StepCall("StepCategory", 1); }),
        "geo ring: next category"});
    RegisterClaim({VK_PRIOR, ModCtrl, active, OnPress([] { StepCall("StepCategory", -1); }),
        "geo ring: previous category"});

    // Re-announce whatever was last said, wherever the ring cursor happens to be. Same key and
    // same meaning as the tactical scanner's.
    RegisterClaim({'O', ModNone, active, OnPress([] { StepCall("ScanCurrent", 0); }),
        "geo ring: re-announce selection"});

    // ---- the sort origin, which is this layer's whole answer to "where am I" ----
    //
    // ⛔ Home displaces the menu navigator's edge jump on this screen (see menu_navigator.cpp).
    // That is the intended trade: jump-to-first on a five-icon strip is a convenience, while the
    // origin is the only cursor the geoscape has.
    RegisterClaim({VK_HOME, ModNone, active, OnPress([] { SimpleCall("SetOriginToSelection"); }),
        "geo ring: sort from selection"});

    // C, the same recenter verb and the same key as the tile cursor's — and the game's own
    // LookAtAvenger, which vanilla binds only to the controller's right stick. It carries no
    // Bindings= entry in the strategy input sections, so it displaces nothing.
    RegisterClaim({'C', ModNone, active, OnPress([] { SimpleCall("ResetOrigin"); }),
        "geo ring: sort from the Avenger"});

    // Slash takes the CAMERA to the selection without moving the origin — looking at something is
    // not the same as deciding to measure everything from it. Shift+slash stays the global
    // navigation-help key; the bare key is unbound in every input section.
    RegisterClaim({VK_OEM_2, ModNone, active, OnPress([] { SimpleCall("LookAtSelection"); }),
        "geo ring: camera to selection"});

    LOG_INFO("GeoScanner: claims registered");
}

} // namespace GeoScanner
