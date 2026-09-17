// The Tactical Legacy Pack's SKIRMISH MODE menu reader (UITLE_SkirmishModeMenu). One screen, a
// state machine over one mecha-item list — see game_ui_tle_skirmish.h and
// docs/tle-skirmish-recon.md. The raw primitives it is built on stay in game_ui.cpp behind
// game_ui_internal.h.

#include "game_ui_tle_skirmish.h"
#include "game_ui_internal.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace GameUI {

namespace {

// Engine field offsets, from the generated SDK's reflection-dump comments (UITLE_SkirmishModeMenu;
// each pinned in sdk_layout_checks.cpp). ⚠ This class is packed to 4: the manager pointers from
// 0x44C on sit at unaligned offsets, which is why the reads below go through the memcpy-based
// Read*Raw helpers and never through a typed pointer.
constexpr int32_t kSkirmishListOffset = 0x420;         // List (UIList*)
constexpr int32_t kSkirmishStartButtonOffset = 0x428;  // ContinueButton (UILargeButton*)
constexpr int32_t kSkirmishStateOffset = 0x48C;        // m_CurrentState (EUISkirmishScreen, byte)
constexpr int32_t kSkirmishSubtitlesOffset = 0x540;    // m_arrSubtitles[21] (FString, stride 0x10)
constexpr int32_t kSkirmishSubtitleStride = 0x10;
constexpr int32_t kSkirmishStartGameOffset = 0x690;    // m_StartGame
constexpr int32_t kSkirmishTitleOffset = 0x710;        // m_ScreenTitle

} // namespace

void* FindSkirmishScreen()
{
    static ScreenFinder sFinder{"UITLE_SkirmishModeMenu"};
    static bool sReported = false;
    void* screen = PreferStack("UITLE_SkirmishModeMenu", sFinder, sReported, ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

SkirmishView ReadSkirmish(void* screen)
{
    SkirmishView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.title = CleanText(ReadFStringField(screen, kSkirmishTitleOffset));
    v.startLabel = CleanText(ReadFStringField(screen, kSkirmishStartGameOffset));

    v.state = ReadU8Raw(screen, kSkirmishStateOffset);
    if (v.state >= 0 && v.state < static_cast<int>(SkirmishState::Count)) {
        v.subtitle = CleanText(ReadFStringField(
            screen, kSkirmishSubtitlesOffset + v.state * kSkirmishSubtitleStride));
    } else {
        // A value outside the enum is a layout drift, not a state: say so once and read nothing
        // that depends on it.
        static bool sReported = false;
        if (!sReported) {
            sReported = true;
            LOG_WARN("GameUI: skirmish menu state %d is outside EUISkirmishScreen", v.state);
        }
    }

    v.list = ReadPtrRaw(screen, kSkirmishListOffset);
    if (v.list) {
        v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);
        // Sized from the list: a picker's length is the game's (every armor, every mission type).
        v.rows = ReadListRowsAll("Skirmish: rows", v.list);
    }

    // Not ReadButtonField: InitLargeButton is handed no label (the START GAME word reaches Flash
    // through SetMissionInfo), and that reader takes an empty label as "no button here".
    void* btn = ReadPtrRaw(screen, kSkirmishStartButtonOffset);
    if (btn) {
        const uint32_t flags = ReadU32Raw(btn, kPanelFlagsOffset);
        if (flags & kPanelVisible) {
            v.startButton = btn;
            v.startFocused = (flags & kPanelFocused) != 0;
        }
    }
    return v;
}

bool SetSkirmishListIndex(void* screen, int index)
{
    void* list = screen ? ReadPtrRaw(screen, kSkirmishListOffset) : nullptr;
    if (!list)
        return false;
    return SetListIndex(list, index);
}

} // namespace GameUI
