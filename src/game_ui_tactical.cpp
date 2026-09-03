// The TACTICAL family's readers: the tactical gate, the ability bar, the command buttons, the
// objectives list, the mission counters, and the tutorial's text surfaces. Split out of
// game_ui.cpp along the recipe-family axis; the raw primitives stay in game_ui.cpp behind
// game_ui_internal.h.

#include "game_ui_tactical.h"
#include "game_ui_internal.h"
#include "log.h"
#include "ue3_reflection.h"
#include "flash_shadow.h"

// The kernel's phrase-join rule. The mission bookends compose several operands of ONE Flash
// push into one spoken line, and every label on them already ends in ':' — a bare ", " would
// voice a doubled stop (CLAUDE.md, AppendPhrase). Same use game_ui.cpp makes of it for
// tooltips; nothing else here reaches into the kernel.
#include "Graph/GraphAnnouncer.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <string>

class UObject;

// Hand-rolled ProcessEvent parameter blocks below must match the engine's pack(4) layout.
#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GameUI {

namespace {

// ---- tactical HUD (UITacticalHUD) ----
constexpr int32_t kTacHudFlagsOffset = 0x410;       // flag dword holding m_isMenuRaised
constexpr uint32_t kTacHudMenuRaised = 0x02;        // m_isMenuRaised (shot HUD up)
constexpr int32_t kTacHudObjectivesOffset = 0x444;  // m_kObjectivesControl (UIObjectiveList*)
constexpr int32_t kTacHudAbilityHudOffset = 0x42C;  // m_kAbilityHUD (UITacticalHUD_AbilityContainer*)

// ---- the ability bar (UITacticalHUD_AbilityContainer / UITacticalHUD_Ability) ----
constexpr int32_t kAbilityBarCurrentOffset = 0x3DC;   // m_iCurrentIndex
constexpr int32_t kAbilityBarActiveOffset = 0x3EC;    // ActiveAbilities (live icon count)
constexpr int32_t kAbilityBarWidgetsOffset = 0x408;   // m_arrUIAbilities (TArray<UITacticalHUD_Ability*>)
constexpr int32_t kAbilityIconCooldownOffset = 0x3F8; // m_strCooldown
constexpr int32_t kAbilityIconChargeOffset = 0x408;   // m_strCharge
constexpr int32_t kAbilityIconAntennaOffset = 0x418;  // m_strAntennaText (ability name, PC only)
constexpr int32_t kAbilityIconHotkeyOffset = 0x428;   // m_strHotkeyLabel
constexpr int32_t kAbilityIconFlagsOffset = 0x438;    // dword holding IsAvailable / bIsShiney
constexpr uint32_t kAbilityIconAvailable = 0x01;      // IsAvailable
// The game's own bar capacity (UITacticalHUD_AbilityContainer.MAX_NUM_ABILITIES) — m_arrUIAbilities
// is created once at that length, so a structural cap two above it can only clip if the game does.
constexpr int kAbilityBarMax = 32;

// ---- the command buttons (UITacticalHUD_MouseControls) ----
// All four confirmed live 2026-08-31 via /props against the running class.
constexpr int32_t kTacHudMouseControlsOffset = 0x44C;  // UITacticalHUD::m_kMouseControls
constexpr int32_t kMouseCtlSelectionOffset = 0x46C;    // m_iCurrentSelection (OnAccept reads it)
constexpr int32_t kMouseCtlButtonItemsOffset = 0x474;  // ButtonItems (TArray<TButtonItems>)
constexpr int32_t kMouseCtlCommandsOffset = 0x484;     // CommandAbilities (TArray<AvailableAction>)
// Inside one TButtonItems element. The element STRIDE is deliberately not a constant here: the
// ScriptStruct's runtime size is 0x21 and whether the engine rounds it to 4 when striding is the
// engine's call, so ReadMouseControls asks the live UArrayProperty for its Inner->ElementSize.
constexpr int32_t kMouseCtlNoKeyOffset = 0x408;        // m_strNoKeyBoundString (localized)
constexpr int32_t kButtonItemLabelOffset = 0x00;       // Label (FString)
constexpr int32_t kButtonItemKeyOffset = 0x10;         // key (FString)
constexpr int32_t kButtonItemStateOffset = 0x20;       // UIState (byte; 0 = eUIState_Normal)
// The world-mode button indices, from the class defaults (m_opt*): 0-4 fixed, 5 the first
// command-ability slot, 6 Chosen info. InitMouseControls sets ButtonItems.Length = 10 and
// nothing ever resizes it — the count doubles as the is-this-panel-sane check.
constexpr int kMouseCtlFixedButtons = 5;               // end turn, prev, next, cam left, cam right
constexpr int kMouseCtlCommandSlot = 5;                // m_optCallSkyranger
constexpr int kMouseCtlChosenSlot = 6;                 // m_optChosenInfo
constexpr int kMouseCtlButtonCount = 10;               // ButtonItems.Length, set once at init

// ---- the objectives list (UIObjectiveList / UIObjectiveListItem) ----
// Verified live 2026-08-23 against the running class on LostAndAbandonedB: m_kObjectivesControl
// 0x444, Items 0x3D8 (4 entries), Data 0x3DC. The Avenger's owner offset is from the dump only —
// the ship was not reachable in that session (see docs/surfaces/tactical.md).
constexpr int32_t kAvengerHudObjectivesOffset = 0x44C; // UIAvengerHUD::Objectives
constexpr int32_t kObjListItemsOffset = 0x3D8;        // UIObjectiveList::Items
constexpr int32_t kObjItemDataOffset = 0x3DC;         // UIObjectiveListItem::Data
// Localized, and CONFIRMED populated on the running class (unlike UITacticalHUD_InfoPanel's
// copies) — so the heading is spoken in the player's language rather than ours.
constexpr int32_t kObjItemListTitleOffset = 0x47C;    // ::m_strObjectivesListTitle
constexpr int32_t kObjItemDarkTitleOffset = 0x48C;    // ::m_strDarkEventsListTitle
constexpr int32_t kObjItemInProgressOffset = 0x4AC;   // ::m_strInProgress
// FObjectiveDisplayInfo, RELATIVE to Data.
constexpr int32_t kObjInfoMissionType = 0x0C;    // FString
constexpr int32_t kObjInfoDisplayLabel = 0x1C;   // FString
constexpr int32_t kObjInfoFlags = 0x2C;          // bitfield dword
constexpr uint32_t kObjInfoCompleted = 0x01;
constexpr uint32_t kObjInfoFailed = 0x02;
constexpr uint32_t kObjInfoCheckBox = 0x04;
constexpr uint32_t kObjInfoWarning = 0x08;
constexpr uint32_t kObjInfoHeader = 0x10;
constexpr uint32_t kObjInfoDarkEvent = 0x20;
constexpr int32_t kObjInfoTimer = 0x30;          // int, -1 = no timer
constexpr int32_t kObjInfoHaveImage = 0x34;      // FString
constexpr int32_t kObjInfoAvailImage = 0x44;     // FString
constexpr int32_t kObjInfoLostImage = 0x54;      // FString
constexpr int32_t kObjInfoHaveAmount = 0x64;     // int, -1 = not a counter row
constexpr int32_t kObjInfoHaveMin = 0x68;
constexpr int32_t kObjInfoAvailAmount = 0x6C;
constexpr int32_t kObjInfoLostAmount = 0x70;
// The list is short by construction (one row per objective plus a heading); 64 is a runaway
// guard, not an expected bound, and a list that hit it would be logged by the caller.
constexpr int kMaxObjectiveItems = 64;

// ---- the mission stage counters (UISpecialMissionHUD / _TurnCounter) ----
// Verified live 2026-08-23 on LostAndAbandonedB: counter 1 visible carrying
// "Приведите солдат на запасной маршрут" / "Осталось солдат" / "3", counter 2 inited and hidden.
constexpr int32_t kSpecHudCounter1Offset = 0x418; // UISpecialMissionHUD::m_kGenericTurnCounter
constexpr int32_t kSpecHudCounter2Offset = 0x420; // ::m_kTurnCounter2
constexpr int32_t kTurnCounterLabelOffset = 0x3FC;    // ::m_sLabel
constexpr int32_t kTurnCounterSubLabelOffset = 0x40C; // ::m_sSubLabel
constexpr int32_t kTurnCounterCounterOffset = 0x41C;  // ::m_sCounter
constexpr int32_t kTurnCounterFlagsOffset = 0x42C;    // bitfield dword
constexpr uint32_t kTurnCounterLocked = 0x01;
constexpr uint32_t kTurnCounterExpired = 0x02;
constexpr uint32_t kTurnCounterInfinity = 0x04;

// ---- tutorial mission: the modal popup (UITutorialBox) ----
constexpr int32_t kTutBoxTitleOffset = 0x410;      // Title (FString)
constexpr int32_t kTutBoxDescOffset = 0x420;       // Desc (FString)
constexpr int32_t kTutBoxButtonHelpOffset = 0x430; // ButtonHelp (FString)

// ---- tutorial mission: world messages (UIWorldMessageMgr, reached via the presentation layer) ----
// The manager is a UIScreen the stack never holds, so it is reached the way the game reaches it:
// XComPresentationLayerBase.m_kWorldMessageManager, off the tactical HUD's own Movie -> PRES.
constexpr int32_t kPresWorldMsgMgrOffset = 0x344; // XComPresentationLayerBase::m_kWorldMessageManager
constexpr int32_t kWorldMsgArrayOffset = 0x450;   // UIWorldMessageMgr::m_arrMsgs (TArray<FTHUDMsg>)
constexpr int32_t kWorldMsgActiveOffset = 0x460;  // ::m_arrActiveMessages (TArray<int>) — indices
constexpr int32_t kWorldMsgBladeOffset = 0x480;   // ::BladeHUDMessage (UIPanel*), the HUD blade
constexpr int32_t kHudMsgStride = 0x78;           // sizeof(FTHUDMsg)
constexpr int32_t kHudMsgIdOffset = 0x00;         // FTHUDMsg::m_sId (FString)
constexpr int32_t kHudMsgTextOffset = 0x10;       // FTHUDMsg::m_txtMsg (FString)
constexpr int32_t kHudMsgFollowOffset = 0x30;     // FTHUDMsg::m_kFollowObject.ObjectID
constexpr int32_t kHudMsgFlagsOffset = 0x34;      // FTHUDMsg bitfield dword
constexpr uint32_t kHudMsgVisible = 0x2;          // m_bVisible (0x1 is m_bFollowWorldLocation)
constexpr uint32_t kHudMsgImportant = 0x4;        // m_bIsImportantMessage
constexpr int32_t kHudMsgTypeOffset = 0x74;       // FTHUDMsg::m_eType (byte, EHUDMessageType)
// The pool is NUM_INITIAL_INACTIVE_MESSAGES (8) recycled slots plus one appended per NAMED
// message, so the array is small but not fixed; 64 is far past anything a mission produces.
constexpr int kMaxWorldMessages = 64;

// ---- tutorial mission: the replay manager (XComTutorialMgr : XComReplayMgr : Actor) ----
constexpr int32_t kReplayFrameOffset = 0x274;       // XComReplayMgr::CurrentHistoryFrame
constexpr int32_t kReplayFlagsOffset = 0x278;       // XComReplayMgr flag dword
constexpr uint32_t kReplayInTutorial = 0x2;         // bInTutorial (0x1 is bInReplay)
constexpr int32_t kTutFlagsOffset = 0x294;          // XComTutorialMgr flag dword
constexpr uint32_t kTutAllowingControls = 0x8;      // bIsTutorialAllowingControls
constexpr int32_t kTutNextInputFrameOffset = 0x298; // NextPlayerInputFrame
constexpr int32_t kTutInvalidCountOffset = 0x31C;   // InvalidSubmissionCnt
constexpr int32_t kTutNextAbilityCtxOffset = 0x338; // NextPlayerAbilityContext

} // namespace

// ---- tactical HUD (UITacticalHUD) — tile reader support ----

void* FindTacticalHUD()
{
    return FindScreen(kScrTacticalHUD);
}

bool TacticalHUDMenuRaised(void* screen)
{
    return screen && (ReadU32Raw(screen, kTacHudFlagsOffset) & kTacHudMenuRaised) != 0;
}

// ---- the ability bar ----

AbilityBarView ReadAbilityBar(void* screen)
{
    AbilityBarView v;
    if (!screen)
        return v;
    void* container = ReadPtrRaw(screen, kTacHudAbilityHudOffset);
    if (!container)
        return v;

    void* widgets[kAbilityBarMax] = {};
    int held = TakeArrayRaw("ability bar", container, kAbilityBarWidgetsOffset, widgets,
        kAbilityBarMax);

    // ⛔ The live count is the container's own ActiveAbilities, clamped to the widgets actually
    // held — never the widget array's length (a constant 30) and never "which icons hold text"
    // (ClearData leaves the previous soldier's antenna text behind). See the header.
    int active = ReadI32Raw(container, kAbilityBarActiveOffset);
    if (active < 0)
        active = 0;
    if (active > held)
        active = held;

    v.valid = true;
    v.selected = ReadI32Raw(container, kAbilityBarCurrentOffset);
    v.entries.reserve(static_cast<size_t>(active));
    for (int i = 0; i < active; i++) {
        AbilityBarEntry e;
        e.handle = widgets[i];
        e.name = ReadFStringField(widgets[i], kAbilityIconAntennaOffset);
        e.hotkey = ReadFStringField(widgets[i], kAbilityIconHotkeyOffset);
        e.cooldown = ReadFStringField(widgets[i], kAbilityIconCooldownOffset);
        e.charges = ReadFStringField(widgets[i], kAbilityIconChargeOffset);
        e.available = (ReadU32Raw(widgets[i], kAbilityIconFlagsOffset) & kAbilityIconAvailable) != 0;
        v.entries.push_back(std::move(e));
    }
    return v;
}

bool ActivateBarAbility(void* screen, int index)
{
    if (!screen || index < 0)
        return false;
    void* container = ReadPtrRaw(screen, kTacHudAbilityHudOffset);
    if (!container)
        return false;
    // The game's own mouse path for an icon click. Its parameter block is the int plus the bool
    // return slot, in declaration order — the same hand-rolled layout every outbound call uses.
    struct {
        int32_t Index;
        uint32_t ReturnValue;
    } params{index, 0};
    if (!CallNamedFunction(container, "AbilityClicked", &params))
        return false;
    return params.ReturnValue != 0;
}

// ---- the command buttons (UITacticalHUD_MouseControls) ----

namespace {

// PODs only (SEH): the ButtonItems span. The count is set once at InitMouseControls (Length =
// 10) and nothing ever resizes it, so any other count means the panel — or our offset — is not
// what we think it is, and the read refuses rather than stride into it.
bool MouseControlSpanRaw(const void* controls, const uint8_t** outData)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(controls) + kMouseCtlButtonItemsOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n != kMouseCtlButtonCount)
            return false;
        *outData = data;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The engine's OWN stride over TButtonItems, from the live UArrayProperty. Metadata on the
// class, so resolved once and kept — but only an IMPLAUSIBLE answer is latched as refusal,
// because the plausible band [script size .. one padded pointer over] is where the truth must
// live whichever way the engine rounds. -1 = resolved and refused, 0 = not yet resolved.
int32_t ButtonItemStride(void* controls)
{
    static int32_t sStride = 0;
    if (sStride == 0) {
        int32_t stride = UE3Reflect::ArrayElementSize(
            reinterpret_cast<UObject*>(controls), "ButtonItems");
        if (stride < 0x21 || stride > 0x30) {
            LOG_WARN("GameUI: TButtonItems stride reads 0x%X — implausible, command buttons "
                     "disabled",
                stride);
            sStride = -1;
        } else {
            LOG_INFO("GameUI: TButtonItems stride 0x%X (compiled sizeof 0x24)", stride);
            sStride = stride;
        }
    }
    return sStride;
}

// Is a Chosen activated on this map — the exact gate UpdateControls runs before it shows the
// Chosen Info button, asked of the same static.
bool ChosenActivatedRawCall()
{
    struct {
        void* ReturnValue;
    } params{nullptr};
    if (!CallStaticNative("XComGameState_Unit", "GetActivatedChosen", &params))
        return false;
    return params.ReturnValue != nullptr;
}

} // namespace

MouseControlsView ReadMouseControls(void* screen)
{
    MouseControlsView v;
    if (!screen)
        return v;
    void* controls = ReadPtrRaw(screen, kTacHudMouseControlsOffset);
    if (!controls)
        return v;

    const uint8_t* data = nullptr;
    if (!MouseControlSpanRaw(controls, &data))
        return v;
    int32_t stride = ButtonItemStride(controls);
    if (stride <= 0)
        return v;

    // The visible set, reconstructed from the same three inputs UpdateControls used (the count
    // itself is pushed straight to Flash and retained nowhere). Slots outside this set retain
    // stale labels forever — an unused command slot, a Chosen button after the Chosen left —
    // so membership comes from the inputs, never from "does the slot hold text".
    int indices[kMouseCtlFixedButtons + 2];
    int count = 0;
    for (int i = 0; i < kMouseCtlFixedButtons; i++)
        indices[count++] = i;
    if (ReadI32Raw(controls, kMouseCtlCommandsOffset + 8) > 0)
        indices[count++] = kMouseCtlCommandSlot;
    if (ChosenActivatedRawCall())
        indices[count++] = kMouseCtlChosenSlot;

    v.valid = true;
    v.noKeyBound = ReadFStringField(controls, kMouseCtlNoKeyOffset);
    v.buttons.reserve(static_cast<size_t>(count));
    for (int k = 0; k < count; k++) {
        const uint8_t* el = data + static_cast<size_t>(indices[k]) * stride;
        CommandButtonEntry e;
        e.index = indices[k];
        e.label = ReadFStringField(el, kButtonItemLabelOffset);
        e.key = ReadFStringField(el, kButtonItemKeyOffset);
        // OnAccept's own test is != eUIState_Normal (0), not == eUIState_Disabled — mirror the
        // gate the activation will actually hit so the spoken state can never disagree with it.
        e.enabled = ReadU8Raw(el, kButtonItemStateOffset) == 0;
        v.buttons.push_back(std::move(e));
    }
    return v;
}

bool ActivateMouseControl(void* screen, int index)
{
    if (!screen || index < 0 || index >= kMouseCtlButtonCount)
        return false;
    void* controls = ReadPtrRaw(screen, kTacHudMouseControlsOffset);
    if (!controls)
        return false;
    // Selection FIRST, then the click — OnAccept acts on m_iCurrentSelection and ignores
    // everything else (the screen-accept-trap rule). This is byte-for-byte what the panel's own
    // FXS_L_MOUSE_UP handler does.
    WriteI32Raw(controls, kMouseCtlSelectionOffset, index);
    struct {
        uint32_t ReturnValue;
    } params{0};
    return CallNamedFunction(controls, "OnAccept", &params);
}

// ---- the objectives list (UIObjectiveList) ----

namespace {

// The owning screen differs per layer and nothing else does, so the walk answers both at once.
// Stack order decides: in a mission UITacticalHUD is the one that matters even if an Avenger HUD
// corpse is still allocated, and the stack is the only structure that knows which is live.
void* FindObjectiveListPanel(bool* tacticalOut)
{
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        // Same readiness rule the tactical gate uses, and for the same reason: a removed screen
        // keeps every state bit it had, so bIsRemoved is the only bit that settles it.
        if (!StackEntryLiveRaw(screens[i]))
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;

        bool tactical = strcmp(name, "UITacticalHUD") == 0;
        if (!tactical && strcmp(name, "UIAvengerHUD") != 0)
            continue;

        void* panel = ReadPtrRaw(screens[i],
            tactical ? kTacHudObjectivesOffset : kAvengerHudObjectivesOffset);
        if (!panel)
            continue;
        if (tacticalOut)
            *tacticalOut = tactical;
        return panel;
    }
    return nullptr;
}

} // namespace

bool HasObjectiveList()
{
    return FindObjectiveListPanel(nullptr) != nullptr;
}

ObjectiveListView ReadObjectives()
{
    ObjectiveListView v;
    bool tactical = false;
    void* panel = FindObjectiveListPanel(&tactical);
    if (!panel)
        return v;
    v.found = true;
    v.tactical = tactical;

    void* items[kMaxObjectiveItems] = {};
    int count = ReadPtrArrayRaw(panel, kObjListItemsOffset, items, kMaxObjectiveItems);
    int hidden = 0;
    for (int i = 0; i < count; i++) {
        if (!items[i])
            continue;

        uint32_t flags = ReadU32Raw(items[i], kPanelFlagsOffset);
        if (flags & kPanelRemoved)
            continue;
        // The documented child-widget exception: on a ROW the visible bit is toggled
        // deliberately — UIObjectiveListItem.ToggleVisibility hides the members of a collapsed
        // group — so an invisible row is one a sighted player cannot read either. Counted rather
        // than swallowed, because a list that silently shrank is the failure mode that reads as
        // "the mission has fewer objectives" and never gets reported as a bug.
        if (!(flags & kPanelVisible)) {
            hidden++;
            continue;
        }

        const int32_t data = kObjItemDataOffset;
        ObjectiveLine line;
        // RAW for MissionType: it is matched against the game's own literals, and CleanText
        // would be free to alter a token we are about to strcmp.
        line.missionType = ReadFStringFieldRaw(items[i], data + kObjInfoMissionType);
        line.title = line.missionType == "TITLE" || line.missionType == "DARKEVENTTITLE";
        line.label = ReadFStringField(items[i], data + kObjInfoDisplayLabel);

        uint32_t f = ReadU32Raw(items[i], data + kObjInfoFlags);
        line.completed = (f & kObjInfoCompleted) != 0;
        line.failed = (f & kObjInfoFailed) != 0;
        line.checkbox = (f & kObjInfoCheckBox) != 0;
        line.warning = (f & kObjInfoWarning) != 0;
        line.header = (f & kObjInfoHeader) != 0;
        line.darkEvent = (f & kObjInfoDarkEvent) != 0;

        line.timer = ReadI32Raw(items[i], data + kObjInfoTimer);
        line.haveAmount = ReadI32Raw(items[i], data + kObjInfoHaveAmount);
        line.haveMin = ReadI32Raw(items[i], data + kObjInfoHaveMin);
        line.availableAmount = ReadI32Raw(items[i], data + kObjInfoAvailAmount);
        line.lostAmount = ReadI32Raw(items[i], data + kObjInfoLostAmount);
        if (line.haveAmount >= 0) {
            line.haveImage = ReadFStringFieldRaw(items[i], data + kObjInfoHaveImage);
            line.availableImage = ReadFStringFieldRaw(items[i], data + kObjInfoAvailImage);
            line.lostImage = ReadFStringFieldRaw(items[i], data + kObjInfoLostImage);
        }

        // The heading words live on the ITEM class, not in Data — the title row pushes
        // m_strObjectivesListTitle straight into Flash. Any row can supply them (they are class
        // defaults), so take them from the first row that answers rather than requiring the
        // title row to be present.
        if (v.heading.empty())
            v.heading = ReadFStringField(items[i], kObjItemListTitleOffset);
        if (v.darkHeading.empty())
            v.darkHeading = ReadFStringField(items[i], kObjItemDarkTitleOffset);
        if (v.inProgress.empty())
            v.inProgress = ReadFStringField(items[i], kObjItemInProgressOffset);

        v.lines.push_back(std::move(line));
    }

    if (hidden > 0)
        LOG_DEBUG("Objectives: %d row(s) hidden by a collapsed group, not spoken", hidden);
    if (count >= kMaxObjectiveItems)
        LOG_WARN("Objectives: list hit the %d-row cap — rows beyond it were NOT read",
            kMaxObjectiveItems);
    return v;
}

// ---- the mission stage counters (UISpecialMissionHUD) ----

namespace {

// One counter, or false when this slot is empty / hidden / wordless.
bool ReadCounterPanel(void* panel, MissionCounter* out)
{
    if (!panel)
        return false;
    uint32_t flags = ReadU32Raw(panel, kPanelFlagsOffset);
    // ⛔ Visible is REQUIRED, not optional: the panel shows itself only when data is pumped in
    // (every AS_Set* ends in Show()), so a hidden counter is one the player is not being shown.
    // The second slot is routinely inited-and-hidden, and reading it would invent a counter.
    if (!(flags & kPanelInited) || (flags & kPanelRemoved) || !(flags & kPanelVisible))
        return false;

    out->label = ReadFStringField(panel, kTurnCounterLabelOffset);
    out->subLabel = ReadFStringField(panel, kTurnCounterSubLabelOffset);
    out->counter = ReadFStringField(panel, kTurnCounterCounterOffset);

    uint32_t f = ReadU32Raw(panel, kTurnCounterFlagsOffset);
    out->locked = (f & kTurnCounterLocked) != 0;
    out->expired = (f & kTurnCounterExpired) != 0;
    out->infinity = (f & kTurnCounterInfinity) != 0;

    // A visible panel with nothing in any of the three fields has no readout to give. Not an
    // error — the MP turn-timer path blanks the sub-label to a single space.
    return !out->label.empty() || !out->subLabel.empty() || !out->counter.empty();
}

} // namespace

std::vector<MissionCounter> ReadMissionCounters()
{
    std::vector<MissionCounter> counters;
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        if (!StackEntryLiveRaw(screens[i]))
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UISpecialMissionHUD") != 0)
            continue;

        // Screen order, so the readout matches the column the player sees.
        const int32_t slots[] = {kSpecHudCounter1Offset, kSpecHudCounter2Offset};
        for (int32_t slot : slots) {
            MissionCounter c;
            if (ReadCounterPanel(ReadPtrRaw(screens[i], slot), &c))
                counters.push_back(std::move(c));
        }
        return counters; // only one such HUD is ever up
    }
    return counters;
}

// ---- tactical activation gate ----

namespace {

// Screens that legitimately sit ABOVE UITacticalHUD during normal play (live stack dump,
// 2026-08-19: UIReplay is the permanent top screen and holds the focus bit for the whole
// mission). Anything not listed blocks the tile cursor — the conservative direction: an
// unknown modal (mission summary, a mod's screen) keeps its arrow keys, and the log names it
// so this list grows from observation, not guesswork.
const char* const kTacticalPassives[] = {
    "UIReplay",
    "UISpecialMissionHUD",
    "UITurnOverlay",
    "UIMouseGuard",
};

bool IsTacticalPassive(const char* name)
{
    for (const char* passive : kTacticalPassives)
        if (strcmp(name, passive) == 0)
            return true;
    return false;
}

} // namespace

TacticalGate ReadTacticalGate()
{
    TacticalGate gate;
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        // A corpse above us is SKIPPED rather than treated as a blocker: it has already been
        // popped and is holding no keys. That is this gate's own policy, deliberately not the
        // one the two GetCurrentScreen gates use.
        if (!screens[i] || !StackEntryLiveRaw(screens[i]))
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UITacticalHUD") == 0) {
            // Reached the HUD. If nothing above it claimed the surface, this is also the live
            // gate; if something did, we came here only to answer "is a mission running".
            gate.missionActive = true;
            if (!gate.blocked) {
                gate.hudOnStack = true;
                gate.menuRaised
                    = (ReadU32Raw(screens[i], kTacHudFlagsOffset) & kTacHudMenuRaised) != 0;
            }
            return gate;
        }
        if (!gate.blocked && !IsTacticalPassive(name)) {
            // Whatever this is owns the surface — the cursor stands down under it whether or
            // not a HUD sits further below. Keep walking rather than returning: whether a
            // MISSION is running underneath is a different question from whether we may act,
            // and only the entries below this one can answer it. The extra work is a few class
            // name reads on a stack that is a handful of entries deep.
            gate.blocked = true;
            strncpy_s(gate.blocker, name, _TRUNCATE);
        }
    }
    return gate; // no tactical HUD anywhere on the stack: not in a mission
}

// ---- tutorial mission (Tutorial_01) ----

namespace {

// XComTutorialMgr is an Actor, not a screen, so it gets its own finder rather than a slot in the
// shared screen pass. No usability predicate: it has no UIPanel flag dword to test, it exists
// only during the tutorial mission, and there is exactly one per session — so "is it still an
// instance of its class" (which ScreenFinder revalidates on every call) is the whole test.
ScreenFinder gTutorialMgrFinder{"XComTutorialMgr", nullptr};

// The base of element `index` of the m_arrMsgs TArray, bounds-checked against its own count.
const uint8_t* WorldMessageSlotRaw(void* mgr, int index)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(mgr) + kWorldMsgArrayOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || index < 0 || index >= n || n > kMaxWorldMessages)
            return nullptr;
        return data + static_cast<size_t>(index) * kHudMsgStride;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

} // namespace

void* FindTutorialBox()
{
    return FindScreen(kScrTutorialBox);
}

TutorialBoxView ReadTutorialBox(void* screen)
{
    TutorialBoxView v;
    if (!screen)
        return v;
    v.title = CleanText(ReadFStringField(screen, kTutBoxTitleOffset));
    v.body = CleanText(ReadFStringField(screen, kTutBoxDescOffset));
    v.buttonHelp = CleanText(ReadFStringField(screen, kTutBoxButtonHelpOffset));
    return v;
}

void* FindWorldMessageMgr()
{
    // Through the presentation layer, the way the game reaches it (`PRES.GetWorldMessenger()`),
    // rather than by scanning GObjects for the class: a manager from a previous mission stays
    // allocated until GC and would win a "newest instance" scan while being wired to nothing.
    void* hud = FindTacticalHUD();
    if (!hud)
        return nullptr;
    void* movie = ReadPtrRaw(hud, kPanelMovieOffset);
    if (!movie)
        return nullptr;
    void* pres = ReadPtrRaw(movie, kMoviePresOffset);
    if (!pres)
        return nullptr;
    return ReadPtrRaw(pres, kPresWorldMsgMgrOffset);
}

std::vector<WorldMessage> ReadWorldMessages(void* mgr)
{
    std::vector<WorldMessage> out;
    if (!mgr)
        return out;

    // m_arrActiveMessages holds INDICES into m_arrMsgs. Reading the index array rather than
    // walking m_arrMsgs is what distinguishes "showing" from "pooled and waiting": a deactivated
    // message keeps its text in the slot it will be recycled from.
    int32_t active[kMaxWorldMessages];
    bool clipped = false;
    int count = NoteArrayRead("WorldMessages: m_arrActiveMessages",
        ReadIntArrayRaw(mgr, kWorldMsgActiveOffset, active, kMaxWorldMessages, &clipped), clipped,
        kMaxWorldMessages);
    if (count <= 0)
        return out;

    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        const uint8_t* slot = WorldMessageSlotRaw(mgr, active[i]);
        if (!slot)
            continue;
        WorldMessage m;
        m.slot = active[i];
        // RAW (no CleanText): world-message text carries <img src='...'> markup that the caller
        // has to TRANSLATE rather than drop — cleaning would leave a hole exactly where the key
        // name belongs ("Press , to go here").
        m.id = ReadFStringFieldRaw(slot, kHudMsgIdOffset);
        m.text = ReadFStringFieldRaw(slot, kHudMsgTextOffset);
        m.type = static_cast<int>(ReadU32Raw(slot, kHudMsgTypeOffset) & 0xFF);
        const uint32_t flags = ReadU32Raw(slot, kHudMsgFlagsOffset);
        m.visible = (flags & kHudMsgVisible) != 0;
        m.important = (flags & kHudMsgImportant) != 0;
        m.followObjectId = static_cast<int>(ReadU32Raw(slot, kHudMsgFollowOffset));
        out.push_back(std::move(m));
    }
    return out;
}

bool HudBladeShowing(void* mgr)
{
    return mgr && ReadPtrRaw(mgr, kWorldMsgBladeOffset) != nullptr;
}

TutorialState ReadTutorialState()
{
    TutorialState s;
    void* mgr = gTutorialMgrFinder.Find();
    if (!mgr)
        return s;
    s.valid = true;
    s.inTutorial = (ReadU32Raw(mgr, kReplayFlagsOffset) & kReplayInTutorial) != 0;
    s.allowingControls = (ReadU32Raw(mgr, kTutFlagsOffset) & kTutAllowingControls) != 0;
    s.historyFrame = static_cast<int>(ReadU32Raw(mgr, kReplayFrameOffset));
    s.nextInputFrame = static_cast<int>(ReadU32Raw(mgr, kTutNextInputFrameOffset));
    s.invalidSubmissions = static_cast<int>(ReadU32Raw(mgr, kTutInvalidCountOffset));
    s.nextAbilityContext = ReadPtrRaw(mgr, kTutNextAbilityCtxOffset);
    return s;
}

// ---- the mission bookends (UIMissionSummary / UIChosenMissionSummary) --------------------
//
// See the header and docs/mission-summary-recon.md. Words from the Flash capture; two plain
// offset reads on the mission summary and none at all on the Chosen one.

namespace {

// UIMissionSummary's two retained fields, from the regenerated dump, pinned in
// sdk_layout_checks.cpp.
constexpr int32_t kSummaryPosterButtonOffset = 0x464; // m_PosterButton (UIButton*)
constexpr int32_t kSummaryContinueOffset = 0x4C4;     // m_strContinue (localized FString)

// SetMissionInfo's parameter order (UIMissionSummary.uc:533-575), read BY POSITION — operands
// 0, 19 and 20 are numbers, which StringArgs() renders as empty slots precisely so nothing
// after them shifts. 19/20 duplicate the enemies-killed value and are not spoken.
enum SummaryArg {
    kSumStatus = 0, // MissionStatus — a number
    kSumResult = 1,
    kSumOpName = 2,
    kSumTypeLabel = 3,
    kSumObjectiveLabel = 5,
    kSumTurnsLabel = 7,
    kSumLootLabel = 9, // ALWAYS "" at both call sites — see below
    kSumEnemiesLabel = 11,
    kSumWoundedLabel = 13,
    kSumKilledLabel = 15,
    kSumRatingLabel = 17,
};

// Every label/value pair the call carries, in drawn order. Each pair is (label index, id); the
// value always sits at label + 1.
//
// ⛔ THE LOOT PAIR IS LISTED and then drops out by the empty rule, deliberately rather than by
// omission. `m_strLootRecoveredLabel` ("ITEMS RECOVERED:") is declared, localized and passed ""
// by BOTH call sites — loot is reported two screens later — so a reader sourcing labels from
// the loc file would invent a row the screen does not draw. Keeping the slot here documents
// that, and costs one skipped iteration.
struct SummaryPairSpec {
    SummaryArg label;
    const char* id;
};

constexpr SummaryPairSpec kSummaryPairs[] = {
    {kSumTypeLabel, "type"},
    {kSumObjectiveLabel, "objective"}, // "OBJECTIVE:" or "CIVILIANS RESCUED:" — the label the
                                       // screen chose, never one we picked
    {kSumTurnsLabel, "turns"},
    {kSumLootLabel, "loot"},
    {kSumEnemiesLabel, "enemies"},
    {kSumWoundedLabel, "wounded"},
    {kSumKilledLabel, "killed"},
    {kSumRatingLabel, "rating"},
};

// SetChosenData's parameter order (UIChosenMissionSummary.uc:118-131).
enum ChosenDataArg {
    kChosenTitle = 0,
    kChosenSubtitle = 1,
    kChosenClass = 2,
    kChosenName = 3,
    kChosenNickname = 4,
    kChosenFlavor = 5,
    kChosenStrengthsLabel = 6,
    kChosenStrengths = 7,
    kChosenWeaknessesLabel = 8,
    kChosenWeaknesses = 9,
};

// The remaining four calls. ⛔ Every index NOT named here is an asset path: SetChosenImage's
// operand 0 is the portrait, SetSoldierCapture's 1, 2 and 6 are the class icon, the rank icon
// and the headshot, and SetChosenIcon is nothing but "img:///" paths and is never read at all.
enum ChosenImageArg {
    kChosenDefeated = 1, // 0 is the portrait path
};

enum ChosenRewardArg {
    kChosenRewardLabel = 0,
    kChosenRewardValue = 1,
    kChosenRewardType = 2,
};

enum ChosenKnowledgeArg {
    kChosenKnowledgeLabel = 0,
    kChosenKnowledgeStart = 1, // QueueNumber — read off args[i].n, empty in StringArgs()
    kChosenKnowledgeEnd = 2,   // QueueNumber
    kChosenKnowledgeText = 3,
};

enum ChosenCaptureArg {
    kChosenCaptureLabel = 0,
    kChosenCaptureRank = 3,
    kChosenCaptureName = 4,
    kChosenCaptureClass = 5,
};

// A number operand as an integer, or -1 when the slot is missing or is not a number. The two
// knowledge percentages are the only numbers either screen means for the player.
int FlashNumber(const FlashShadow::Call& call, size_t index)
{
    if (index >= call.args.size() || call.args[index].type != FlashShadow::ValueType::Number)
        return -1;
    return static_cast<int>(call.args[index].n);
}

// Two spoken phrases joined by the kernel's rule, never a bare ", ": every label on these
// screens ends in ':' already, which AppendPhrase answers with a plain space.
std::string JoinPhrase(const std::string& first, const std::string& second)
{
    std::string out = first;
    X2Access::Graph::AppendPhrase(out, second);
    return out;
}

} // namespace

void* FindMissionSummary()
{
    return FindStackScreenRaw("UIMissionSummary");
}

MissionSummaryView ReadMissionSummary(void* screen)
{
    MissionSummaryView v;
    v.screen = screen;
    if (!screen)
        return v;

    // Read regardless of the capture: these two are the screen's own properties, and a capture
    // that came back empty is exactly when the Continue label matters most.
    v.continueLabel = ReadFStringField(screen, kSummaryContinueOffset);
    v.photobooth = ReadButtonField(screen, kSummaryPosterButtonOffset);

    // The screen pushes through its OWN MC (MCName "theMissionSummary"), so the identity the
    // command stream is keyed by is the screen's own cache index — no library panel involved.
    const int32_t index = FlashShadow::PanelCacheIndex(screen);
    if (index < 0)
        return v;

    FlashShadow::Call call;
    if (!FlashShadow::GetCall(index, "SetMissionInfo", call))
        return v;

    const std::vector<std::string> args = call.StringArgs();
    v.captured = true;
    v.result = FlashArg(args, kSumResult);
    v.opName = FlashArg(args, kSumOpName);
    for (const SummaryPairSpec& spec : kSummaryPairs) {
        SummaryStat stat;
        stat.id = spec.id;
        stat.label = FlashArg(args, spec.label);
        stat.value = FlashArg(args, spec.label + 1);
        // A pair the screen drew nothing for is not a row. Both halves are tested, not just the
        // value: a label with no value is just as absent, and an empty answer here is ordinary
        // rather than a failure.
        if (!stat.label.empty() || !stat.value.empty())
            v.stats.push_back(std::move(stat));
    }

    // ⛔ LOG ON CHANGE, NEVER PER READ — this runs from Build() AND ScreenName(), so about
    // twenty times a second. The screen pointer is COMPARED and never dereferenced, and the
    // result rides in the key so a second mission landing on a reused address still logs.
    static void* sLastScreen = nullptr;
    static std::string sLastResult;
    if (screen != sLastScreen || v.result != sLastResult) {
        sLastScreen = screen;
        sLastResult = v.result;
        LOG_INFO("MissionSummary: '%s' / '%s' from cache index %d — %zu operand(s), %zu stat "
                 "row(s), photobooth %s",
            v.result.c_str(), v.opName.c_str(), index, args.size(), v.stats.size(),
            v.photobooth.handle ? "shown" : "hidden");
    }
    return v;
}

void* FindChosenSummary()
{
    return FindStackScreenRaw("UIChosenMissionSummary");
}

ChosenSummaryView ReadChosenSummary(void* screen)
{
    ChosenSummaryView v;
    v.screen = screen;
    if (!screen)
        return v;

    const int32_t index = FlashShadow::PanelCacheIndex(screen);
    if (index < 0)
        return v;

    FlashShadow::Call call;
    if (FlashShadow::GetCall(index, "SetChosenData", call)) {
        const std::vector<std::string> args = call.StringArgs();
        v.captured = true;
        v.title = FlashArg(args, kChosenTitle);
        v.subtitle = FlashArg(args, kChosenSubtitle);
        // Class, name and nickname are three fields of one identity and are read as one line —
        // joined AFTER cleaning, so a nickname that cleans away leaves no separator behind.
        v.identity = JoinPhrase(
            JoinPhrase(FlashArg(args, kChosenClass), FlashArg(args, kChosenName)),
            FlashArg(args, kChosenNickname));
        v.flavorLines = FlashArgLines(args, kChosenFlavor);
        v.strengths.id = "strengths";
        v.strengths.label = FlashArg(args, kChosenStrengthsLabel);
        v.strengths.value = FlashArg(args, kChosenStrengths);
        v.weaknesses.id = "weaknesses";
        v.weaknesses.label = FlashArg(args, kChosenWeaknessesLabel);
        v.weaknesses.value = FlashArg(args, kChosenWeaknesses);
    }

    // Operand 0 is the portrait asset path and is never touched; operand 1 is the badge, which
    // is "" unless the Chosen died on this mission.
    if (FlashShadow::GetCall(index, "SetChosenImage", call))
        v.defeated = FlashArg(call.StringArgs(), kChosenDefeated);

    // The three outcome blocks are mutually exclusive in OnInit, but SetChosenReward is ALSO
    // pushed with three empty strings on the plain-encounter branch — so the empty test, not
    // the call's presence, is what says a reward was given.
    if (FlashShadow::GetCall(index, "SetChosenReward", call)) {
        const std::vector<std::string> args = call.StringArgs();
        v.reward.id = "reward";
        v.reward.label = FlashArg(args, kChosenRewardLabel);
        v.reward.value = JoinPhrase(
            FlashArg(args, kChosenRewardValue), FlashArg(args, kChosenRewardType));
    }

    if (FlashShadow::GetCall(index, "SetChosenKnowledge", call)) {
        const std::vector<std::string> args = call.StringArgs();
        v.knowledge.id = "knowledge";
        v.knowledge.label = FlashArg(args, kChosenKnowledgeLabel);
        v.knowledge.value = FlashArg(args, kChosenKnowledgeText);
        // Both percentages are QueueNumber operands, so they are absent from StringArgs() by
        // design and come off the typed values instead.
        v.knowledgeBefore = FlashNumber(call, kChosenKnowledgeStart);
        v.knowledgeAfter = FlashNumber(call, kChosenKnowledgeEnd);
    }

    if (FlashShadow::GetCall(index, "SetSoldierCapture", call)) {
        const std::vector<std::string> args = call.StringArgs();
        v.capturedSoldier = JoinPhrase(
            JoinPhrase(JoinPhrase(FlashArg(args, kChosenCaptureLabel),
                           FlashArg(args, kChosenCaptureRank)),
                FlashArg(args, kChosenCaptureName)),
            FlashArg(args, kChosenCaptureClass));
    }

    // Log on change, never per read — same reason as above; the identity rides in the key so a
    // second Chosen on a reused address still logs.
    static void* sLastScreen = nullptr;
    static std::string sLastIdentity;
    if (screen != sLastScreen || v.identity != sLastIdentity) {
        sLastScreen = screen;
        sLastIdentity = v.identity;
        LOG_INFO("ChosenSummary: '%s' from cache index %d — %s%s%s%s",
            v.identity.c_str(), index, v.captured ? "data" : "NOTHING CAPTURED",
            v.defeated.empty() ? "" : ", defeated",
            v.knowledge.label.empty() && v.knowledge.value.empty() ? "" : ", knowledge extracted",
            v.capturedSoldier.empty() ? "" : ", soldier captured");
    }
    return v;
}

// ---- the loot receipt (UIInventoryTactical) -------------------------------------------------
//
// See the header. The list, the OK button and the title off the screen; the item's ObjectID off
// a row, which retains nothing else. All pinned in sdk_layout_checks.cpp.

namespace {

constexpr int32_t kLootListOffset = 0x418;      // m_kLootList
constexpr int32_t kLootOkButtonOffset = 0x428;  // m_kButton_OK
constexpr int32_t kLootTitleOffset = 0x458;     // m_strTitle
constexpr int32_t kLootRowItemIdOffset = 0x3D8; // UIInventoryTactical_LootingItem::GameStateObjectID

// A corpse yields a handful of items, a supply crate a few more. Loud when clipped.
constexpr int kMaxLootRows = 32;

} // namespace

void* FindLootScreen()
{
    void* screen = FindStackScreenRaw("UIInventoryTactical");
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

LootView ReadLoot(void* screen)
{
    LootView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.title = ReadFStringField(screen, kLootTitleOffset);
    v.okButton = ReadPtrRaw(screen, kLootOkButtonOffset);
    if (v.okButton) {
        // InitButton → SetText, so Text is retained: m_strGenericOK wrapped in AlignCenter's
        // <p align='CENTER'> markup, which the cleaning read strips.
        v.okLabel = ReadFStringField(v.okButton, kButtonTextOffset);
    }

    v.list = ReadPtrRaw(screen, kLootListOffset);
    if (!v.list)
        return v;
    void* rows[kMaxLootRows] = {};
    const int rowCount = TakeListItemsRaw("Loot: rows", v.list, rows, kMaxLootRows);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i] || !IsKindOfRaw(rows[i], "UIInventoryTactical_LootingItem"))
            continue;
        LootRow r;
        r.handle = rows[i];
        r.index = i;
        r.itemId = ReadI32Raw(rows[i], kLootRowItemIdOffset);
        v.rows.push_back(r);
    }
    return v;
}

bool CloseLoot(const LootView& view)
{
    if (!view.screen)
        return false;
    // The screen's own Enter. OnUnrealCommand answers it — and Space, Escape, A and B alike —
    // with OnButtonClicked(m_kButton_OK): the looting sound, CloseScreen, and the callback the
    // visualizer is sleeping on. The button itself is out of the navigator.
    return SendScreenCommand(view.screen, kCmdEnter);
}

// ---- soldier info (UITacticalCharInfoScreen) ------------------------------------------------
//
// See the header. Everything is a retained widget read; the labels are found by Flash name
// because the screen keeps no pointer to them.

namespace {

constexpr int32_t kCharInfoListOffset = 0x410;       // UITacticalCharInfoScreen::ListPerks
constexpr int32_t kTacHudCharInfoHintOffset = 0x510; // UITacticalHUD::m_strNavHelpCharShow
constexpr int32_t kPanelMCNameOffset = 0x27C;        // UIPanel::MCName
// UIText::Text and UIScrollingText::Text sit at the same offset; both classes are read here.
constexpr int32_t kWidgetTextOffset = 0x3D8;
// UIListItemPerk::Perk — a UISummary_UnitEffect held BY VALUE on the row, so its members are the
// row's own offsets.
constexpr int32_t kPerkNameOffset = 0x3E0;        // Perk.Name
constexpr int32_t kPerkDescriptionOffset = 0x3F0; // Perk.Description
constexpr int32_t kPerkSourceOffset = 0x418;      // Perk.AbilitySourceName (FName)
// The FLA's seven fixed stat slots (mc_statLabel00..06 / mc_statValue00..06).
constexpr int kCharInfoStatSlots = 7;
// Headings and effects interleaved; a veteran with a long passive list is still far below this.
constexpr int kMaxCharInfoRows = 128;
constexpr int kMaxCharInfoChildren = 64;

// A direct child of `panel` named `name` in Flash, or null. The sheet spawns its labels into
// LOCALS and keeps no pointer to them, so the widget's Flash name is the only handle they have.
void* FindChildByMCName(void* panel, const char* name)
{
    void* children[kMaxCharInfoChildren] = {};
    const int count = TakeArrayRaw(
        "CharInfo: children", panel, kPanelChildPanelsOffset, children, kMaxCharInfoChildren);
    for (int i = 0; i < count; i++) {
        if (!children[i])
            continue;
        char buf[64];
        if (FNameAtAnsiRaw(static_cast<const uint8_t*>(children[i]) + kPanelMCNameOffset, buf,
                sizeof(buf))
            && strcmp(buf, name) == 0)
            return children[i];
    }
    return nullptr;
}

// A named label's retained text, cleaned — SetCenteredText stores the <p align='CENTER'> wrapper
// with it, which the cleaning read strips. ⛔ Text FIRST, HtmlText as the fallback: the class
// label is written with SetHTMLText, which fills only the html field (heard live 2026-09-04 as a
// heading with the name and no class), while the name and the stats go through SetText/SetTitle
// and fill both. The html field carries font markup on top of the alignment, all of it stripped.
std::string ChildText(void* panel, const std::string& name)
{
    void* child = FindChildByMCName(panel, name.c_str());
    if (!child)
        return std::string();
    std::string text = ReadFStringField(child, kWidgetTextOffset);
    if (text.empty())
        text = ReadFStringField(child, kScrollHtmlOffset);
    return text;
}

} // namespace

std::string ReadCharInfoHint(void* hud)
{
    return hud ? ReadFStringField(hud, kTacHudCharInfoHintOffset) : std::string();
}

void* FindCharInfoScreen()
{
    void* screen = FindStackScreenRaw("UITacticalCharInfoScreen");
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

CharInfoView ReadCharInfo(void* screen)
{
    CharInfoView v;
    if (!screen)
        return v;
    v.screen = screen;
    // The list is spawned in OnInit, which runs AFTER the push (InitCharacterInfoScreen comes
    // first in Stick_L3) — a null here is the screen before it has built itself.
    void* list = ReadPtrRaw(screen, kCharInfoListOffset);
    if (!list)
        return v;
    v.valid = true;

    v.name = ChildText(screen, "mc_nameLabel");
    v.className = ChildText(screen, "mc_classLabel");
    for (int i = 0; i < kCharInfoStatSlots; i++) {
        CharInfoStat stat;
        stat.label = ChildText(screen, "mc_statLabel0" + std::to_string(i));
        stat.value = ChildText(screen, "mc_statValue0" + std::to_string(i));
        if (stat.label.empty() && stat.value.empty())
            continue;
        v.stats.push_back(std::move(stat));
    }

    void* rows[kMaxCharInfoRows] = {};
    const int rowCount = TakeListItemsRaw("CharInfo: effects", list, rows, kMaxCharInfoRows);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        CharInfoEntry e;
        e.handle = rows[i];
        e.index = i;
        // ⛔ Two kinds of row, matched by CLASS: AddEffectsToList spawns a bare UIText for each
        // category heading and a UIListItemPerk per effect, into the same container. An empty
        // Text cannot tell them apart; the class can.
        if (ObjectClassNamedRaw(rows[i], "UIText")) {
            e.header = true;
            e.text = ReadFStringField(rows[i], kWidgetTextOffset);
        } else if (ObjectClassNamedRaw(rows[i], "UIListItemPerk")) {
            e.text = ReadFStringField(rows[i], kPerkNameOffset);
            e.description = ReadFStringField(rows[i], kPerkDescriptionOffset);
            char src[64];
            if (FNameAtAnsiRaw(
                    static_cast<const uint8_t*>(rows[i]) + kPerkSourceOffset, src, sizeof(src)))
                e.source = src;
        } else {
            continue;
        }
        v.entries.push_back(std::move(e));
    }
    return v;
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
