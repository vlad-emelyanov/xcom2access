// GameUI's CORE: the SEH-guarded raw primitives, the screen-stack walkers, the bounded array
// reads, the tracked-screen lookup, the outbound-call plumbing, the generic row/tooltip helpers
// and the companion-CDO bridge. The per-screen readers live in the game_ui_*.cpp family TUs
// (shell / strategy / popup / avenger / tactical / dev), which consume everything here through
// game_ui_internal.h.

#include "game_ui.h"
#include "game_ui_internal.h"
#include "log.h"
#include "ue3_reflection.h"
#include "Graph/GraphAnnouncer.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Core reflection types only (UObject, UClass, FName, TArray) — the same proven set the other
// game-facing TUs use. Deliberately NOT the full generated SDK: its class headers are emitted
// under pack(0x8) and their COMPILED layouts drift from the engine's real ones, so all XComGame
// field access below goes through explicit offsets taken from the generator's reflection-dump
// comments and verified against a live process (see kOffsets below).
#include "GameDefines.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GameUI {

namespace {

// Engine field offsets/masks, from the generated SDK's reflection-dump comments — the comments
// are authoritative, the compiled classes are not. Core set validated live (ReadProcessMemory
// against a running WOTC shell, 2026-08-18). The GENERIC offsets more than one family TU needs
// live in game_ui_internal.h; what remains here is what only the core helpers read. Every
// hand-transcribed offset is pinned by a static_assert in sdk_layout_checks.cpp either way.

// UEngine's flag dword. `bSubtitlesEnabled` (0x10) is the game's Show Subtitles option after
// XComOnlineProfileSettings.ApplyOptions has copied the profile value onto the engine — the copy
// the game itself tests before drawing any subtitle, so it is the one parity is defined against.
constexpr int32_t kEngineFlagsOffset = 0x46C;
constexpr uint32_t kEngineSubtitlesEnabled = 0x10;

// A localized line is as long as the translator made it; this is the runaway guard on a count
// read out of engine memory, not a working limit (the longest stock movie subtitle is ~250).
constexpr int32_t kMaxPlausibleLocalizedChars = 1 << 16;

// UIMechaListItem's row widgets, same reflection-dump provenance.
constexpr int32_t kMechaDescOffset = 0x404;       // UIMechaListItem::Desc (UIScrollingText*)
constexpr int32_t kMechaFlagsOffset = 0x414;      // UIMechaListItem flag dword
constexpr uint32_t kMechaDisabled = 0x01;         // bDisabled
constexpr int32_t kMechaValueOffset = 0x418;      // UIMechaListItem::Value (UIScrollingText*)
constexpr int32_t kMechaColorChipOffset = 0x420;  // UIMechaListItem::ColorChip (UIBGBox*)
constexpr int32_t kBGBoxColorOffset = 0x3D8;      // UIBGBox::BGColor (FString, "0x00RRGGBB")
constexpr int32_t kMechaSpinnerOffset = 0x428;    // UIMechaListItem::Spinner (UIListItemSpinner*)
constexpr int32_t kMechaDropdownOffset = 0x430;   // UIMechaListItem::Dropdown (UIDropdown*)
constexpr int32_t kMechaButtonOffset = 0x438;     // UIMechaListItem::Button (UIButton*)
constexpr int32_t kMechaCheckboxOffset = 0x440;   // UIMechaListItem::Checkbox (UICheckbox*)
constexpr int32_t kMechaSliderOffset = 0x448;     // UIMechaListItem::Slider (UISlider*)
constexpr int32_t kMechaTypeOffset = 0x450;       // UIMechaListItem::Type (byte, EUILineItemType)
constexpr int32_t kCheckboxFlagsOffset = 0x3E8;   // UICheckbox flag dword
constexpr uint32_t kCheckboxChecked = 0x01;       // bChecked
constexpr int32_t kSliderPercentOffset = 0x3E8;   // UISlider::Percent (float, 0-100)
constexpr int32_t kSpinnerValueOffset = 0x3DC;    // UIListItemSpinner::Value (FString)
constexpr int32_t kDropdownItemsOffset = 0x3F8;   // UIDropdown::Items (TArray<FString>)
constexpr int32_t kDropdownSelectedOffset = 0x418; // UIDropdown::SelectedItem (int)

// Tooltip plumbing (mouse-over text a sighted player gets on hover). A panel with a tooltip
// carries the has-tooltip bit and the id of its entry in ITS movie's tooltip manager; the
// manager is reached through the panel's own Movie → PRES → m_kTooltipMgr chain — never a
// global scan, because two managers can be alive at once (2D HUD + 3D strategy movie) with
// independently-counted ids.
constexpr uint32_t kPanelHasTooltip = 0x100;      // UIPanel flag dword bit
constexpr int32_t kPanelTooltipIdOffset = 0x2AC;  // UIPanel::CachedTooltipId
constexpr int32_t kPresTooltipMgrOffset = 0x55C;  // XComPresentationLayerBase::m_kTooltipMgr
constexpr int32_t kMgrTooltipsOffset = 0x41C;     // UITooltipMgr::Tooltips (TArray<UITooltip*>)
constexpr int32_t kTooltipIdOffset = 0x3D8;       // UITooltip::Id
constexpr int32_t kTextTooltipTitleOffset = 0x48C; // UITextTooltip::sTitle (FString)
constexpr int32_t kTextTooltipBodyOffset = 0x49C;  // UITextTooltip::sBody (FString)

// SetTooltipText tooltips are NOT objects in the Tooltips array: AddNewTooltipTextBox appends
// a TTextTooltipData STRUCT to the manager's single shared TextTooltip object. That data array
// is where hover text actually lives (verified 2026-08-19 after the object-array lookup read
// empty on every settings row).
constexpr int32_t kMgrTextTooltipOffset = 0x414;  // UITooltipMgr::TextTooltip (UITextTooltip*)
constexpr int32_t kTextTooltipDataOffset = 0x4BC; // UITextTooltip::TextTooltipData (TArray)
constexpr int32_t kTTDataStride = 0xAC;           // sizeof(FTTextTooltipData)
constexpr int32_t kTTDataIdOffset = 0x00;         // FTTextTooltipData::Id
constexpr int32_t kTTDataTitleOffset = 0x44;      // FTTextTooltipData::sTitle (FString)
constexpr int32_t kTTDataBodyOffset = 0x54;       // FTTextTooltipData::sBody (FString)

// ⛔ A tooltip's id is not always retained ON a panel. `AddNewTooltipTextBox` can be called
// STRAIGHT on the manager, in which case nothing sets bHasTooltip or CachedTooltipId and the
// only link back is `targetPath` — the Flash path the tooltip was registered against.
// UIUtilities_Strategy.AddAbilitySummaryTooltip is the pattern: every ability in an armory
// screen's summary strip gets its full description that way, against
// `<screen MCPath>.abilitySummaryList.theObject.AbilitySummaryItem<N>`, and there is no UIPanel
// for those items at all. So a panel-keyed read finds nothing and the text is nevertheless
// sitting in the same array. Matching on the path is the way in.
constexpr int32_t kTTDataTargetPathOffset = 0x1C; // FTTextTooltipData::targetPath (FString)
constexpr int32_t kTTDataMouseInOffset = 0x6C;    // FTTextTooltipData::del_OnMouseIn (FScriptDelegate)
constexpr int32_t kDelegateObjectOffset = 0x00;   // FScriptDelegate::Object
constexpr int32_t kDelegateFunctionOffset = 0x08; // FScriptDelegate::FunctionName (FName)

constexpr int32_t kMechaBGOffset = 0x40C;         // UIMechaListItem::BG (UIPanel*)
// ⛔ UIListItemString OVERRIDES SetTooltipText to forward to its ButtonBG child ("Set tooltip on
// BG since that's where our mouse events originate from"), and SetDisabled(bDisabled, Reason)
// parks the why-disabled text there too. So a list row's tooltip is never on the row.
constexpr int32_t kListItemBGOffset = 0x454;      // UIListItemString::ButtonBG (UIButton*)
constexpr int32_t kPanelMCPathOffset = 0x2B0;     // UIPanel::MCPath (FName)

// ---- UIScreenStack: the game's own list of what is on screen ----
constexpr int32_t kStackFlagsOffset = 0x60;    // UIScreenStack flag dword
constexpr uint32_t kStackInputBlocked = 0x1;   // IsInputBlocked — "ignore input, system is gated"
constexpr uint32_t kStackScreensVisible = 0x2; // ScreensVisible — the whole UI is showing
constexpr uint32_t kStackCinematic = 0x8;      // bCinematicMode — screens hidden for cinematics
constexpr int32_t kStackScreensOffset = 0x6C;  // UIScreenStack::Screens (TArray<UIScreen*>)

// ---- UIScreen's own flag dword (UIPanel's is at 0x284; this one is the SCREEN subclass's) ----
// bShowDuringCinematic is the game's OWN answer to "does a cutscene take this screen away":
// HideUIForCinematics() walks the stack calling HideForCinematics(), which hides a screen only
// when the flag is CLEAR. See ScreenShowsDuringCinematic.
constexpr int32_t kScreenFlagsOffset = 0x3E0;
constexpr uint32_t kScreenShowDuringCinematic = 0x10;

// EUILineItemType values (must match UIMechaListItem.uc / Flash).
constexpr uint8_t kLineDescription = 0;
constexpr uint8_t kLineColorChip = 1;
constexpr uint8_t kLineSpinner = 2;
constexpr uint8_t kLineDropdown = 3;
constexpr uint8_t kLineSlider = 4;
constexpr uint8_t kLineCheckbox = 5;
constexpr uint8_t kLineButton = 6;
constexpr uint8_t kLineValue = 7;
constexpr uint8_t kLineSpinnerAndButton = 8;

// UIUtilities_Input action masks. The widgets gate input through the native
// CheckInputIsReleaseOrDirectionRepeat: NON-direction commands (Enter) act on the RELEASE
// phase, while DIRECTION commands (arrows) act on press/repeat and their release is
// deliberately excluded (releasing a held arrow must not add a step) — verified live
// 2026-08-19: arrows sent with RELEASE were swallowed by the gate (sliders/spinners never
// moved) while Enter with RELEASE worked.
constexpr int32_t kActionPress = 1;
constexpr int32_t kActionRelease = 32;

} // namespace

// ---- raw, SEH-safe primitives (no C++ objects with destructors inside __try) ----
//
// External linkage since the family split: game_ui_internal.h is their interface, and the
// family TUs plus gui_dump.cpp are their only intended callers.

// The only WRITE primitive into engine memory. Everything else here reads; a write is reserved
// for a field the GAME itself treats as a plain cursor (the hero promotion screen's column and
// panel indices), never for state the engine derives or reference-counts.
void WriteI32Raw(void* obj, int32_t offset, int32_t value)
{
    __try {
        *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(obj) + offset) = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

uint32_t ReadU32Raw(const void* obj, int32_t offset)
{
    __try {
        return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(obj) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void* ReadPtrRaw(const void* obj, int32_t offset)
{
    __try {
        return *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(obj) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// UE3 FString on Windows is TArray<wchar_t> despite the SDK declaring char:
// [wchar_t* data (0x00)] [int32 count (0x08)] [int32 max (0x0C)].
int CopyFStringRaw(const void* fstr, wchar_t* buf, int bufWchars)
{
    __try {
        auto* raw = reinterpret_cast<const uint8_t*>(fstr);
        auto* data = *reinterpret_cast<wchar_t* const*>(raw);
        int32_t count = *reinterpret_cast<const int32_t*>(raw + 8);
        if (!data || count <= 1)
            return 0;
        int len = count - 1; // count includes the terminator
        if (len >= bufWchars)
            len = bufWchars - 1;
        memcpy(buf, data, static_cast<size_t>(len) * sizeof(wchar_t));
        buf[len] = L'\0';
        return len;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buf[0] = L'\0';
        return 0;
    }
}

// Name-entry access rule: NEVER walk GNames linearly — the array can hold malformed slots whose
// access violations would be silently absorbed by our guards and poison the lookup. Only
// dereference entries via display indices taken from LIVE objects, with bounds checks.

// SEH-safe: the object's own ANSI name into `buf`; false on any fault or wide/missing entry.
bool ObjNameAnsiRaw(UObject* obj, char* buf, int bufLen)
{
    __try {
        int32_t idx = obj->Name.GetDisplayIndex();
        TArray<FNameEntry*>* names = FName::Names();
        if (!names || idx < 0 || idx >= names->size())
            return false;
        FNameEntry* entry = names->at(idx);
        if (!entry)
            return false;
        const char* ansi = entry->GetAnsiName();
        if (!ansi)
            return false;
        for (int i = 0; i < bufLen - 1; i++) {
            buf[i] = ansi[i];
            if (!ansi[i])
                return true;
        }
        buf[bufLen - 1] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A CLASS object: the object named `className` whose own class is named "Class". Class
// objects persist for the whole session, so these resolve once.
UObject* FindClassRaw(const char* className)
{
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return nullptr;
        char name[64];
        char clsName[64];
        for (int32_t i = 0; i < objects->size(); i++) {
            UObject* obj = objects->at(i);
            if (!obj)
                continue;
            if (!ObjNameAnsiRaw(obj, name, sizeof(name)) || strcmp(name, className) != 0)
                continue;
            UClass* cls = obj->Class;
            if (!cls)
                continue;
            if (ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), clsName, sizeof(clsName))
                && strcmp(clsName, "Class") == 0)
                return obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// The name of an object's CLASS, for matching without having resolved a UClass pointer first.
bool ObjClassNameAnsiRaw(void* obj, char* buf, int bufLen)
{
    __try {
        UClass* cls = reinterpret_cast<UObject*>(obj)->Class;
        return cls && ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), buf, bufLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A live INSTANCE: class matches by POINTER; its name is the class's display name, which
// excludes the class default object ("Default__<name>") for free.
//
// ⛔ This is BOTH the scan filter (ScanInstanceRaw, ScanInstancesRaw) and the cache revalidation
// in ScreenFinder::Find, FindTrackedScreen and FindSoundManager, so the two must be handed the
// SAME `match`. A derived-aware scan revalidated exactly would have its cache rejected on every
// call — the finder would rescan forever and, because of the one-second rate limit, answer null
// between scans, making the screen flicker in and out of existence.
bool IsInstanceRaw(UObject* obj, UObject* classObj, const char* className, ClassMatch match)
{
    __try {
        if (!obj || !classObj)
            return false;
        char name[64];
        if (match == ClassMatch::Derived) {
            if (!IsKindOfClassRaw(obj, classObj))
                return false;
            // The exact form gets this for free: an instance's display name IS its class's, so
            // "Default__<Class>" already fails the compare. A pointer walk up the chain does not,
            // and a CDO is a perfectly well-formed instance of its own class — it would be the
            // first hit of every scan. Its name is the one thing that tells it apart.
            return ObjNameAnsiRaw(obj, name, sizeof(name)) && strncmp(name, "Default__", 9) != 0;
        }
        if (reinterpret_cast<UObject*>(obj->Class) != classObj)
            return false;
        return ObjNameAnsiRaw(obj, name, sizeof(name)) && strcmp(name, className) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

namespace {

// Defined further down with the other reflection primitives; the scans below need it here.
void* ReadObjectClassRaw(UObject* obj);

// The class-level half of IsInstanceRaw for EVERY entry at once: bit `c` is set when `cls` could
// possibly satisfy entry `c`. Exact needs `cls` to BE the class, Derived needs it anywhere in the
// chain — so ONE walk answers all `count` questions, where asking per entry walked the whole
// chain once per entry.
//
// ⛔ This is a PRE-FILTER and must never produce a false NEGATIVE. A set bit means only "ask
// IsInstanceRaw"; the real test still decides, which is what keeps a scan's predicate identical
// to the unfiltered form — CDO exclusion, name compare and SEH behaviour included. A false
// POSITIVE costs one call on an object that then fails, which is why the fault path returns
// "everything is a candidate" rather than nothing.
uint32_t ClassCandidacyMaskRaw(
    void* cls, UObject* const* classes, const ClassMatch* match, int count)
{
    uint32_t mask = 0;
    __try {
        auto* cur = reinterpret_cast<UStruct*>(cls);
        // Bounded like every other SuperField walk here: a UE3 chain is a dozen deep, and a
        // corrupt link must not spin.
        for (int step = 0; cur && step < 32; step++) {
            for (int c = 0; c < count; c++) {
                if (!classes[c] || (mask & (1u << c)))
                    continue;
                if (reinterpret_cast<UObject*>(cur) != classes[c])
                    continue;
                // Exact only accepts the object's OWN class, which is step 0.
                if (match[c] == ClassMatch::Derived || step == 0)
                    mask |= 1u << c;
            }
            cur = reinterpret_cast<UStruct*>(cur->SuperField);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return ~0u; // unreadable chain: filter nothing, exactly as before this filter existed
    }
    return mask;
}

// The per-call memo both scans share: GObjects holds hundreds of thousands of instances of a few
// thousand classes, so the candidacy answer is worth computing once per CLASS rather than once
// per object. Direct mapped — a collision just recomputes, and there is nothing to invalidate
// because the memo dies with the call.
//
// ⛔ Per CALL, never static. `classes[]` grows between scans as packages load, and a class object
// that resolved late would otherwise be judged against a stale answer forever.
constexpr int kMemoSlots = 256;
struct ClassMemo {
    void* cls;
    uint32_t mask;
};

// The memo's answer for `cls`, computing it on a miss. `cls` is never null, and the slots start
// null, so a zero slot can never read as a hit.
uint32_t MemoMaskRaw(ClassMemo* memo, void* cls, UObject* const* classes, const ClassMatch* match,
    int count)
{
    const int slot
        = static_cast<int>((reinterpret_cast<uintptr_t>(cls) >> 4) & (kMemoSlots - 1));
    if (memo[slot].cls != cls) {
        memo[slot].cls = cls;
        memo[slot].mask = ClassCandidacyMaskRaw(cls, classes, match, count);
    }
    return memo[slot].mask;
}

} // namespace

// The instance of `classObj` that `usable` accepts, searching from the end of GObjects.
//
// Searching from the end used to be justified as "newest allocation first", and that is WRONG:
// UE3 hands out freed slots again, so a freshly spawned screen routinely sits at a LOWER index
// than corpses of its own class. Live UIPauseMenu at 383831 with removed ones at 383887 and
// 383932 is a real observation, and since a removed screen keeps its inited/visible/focused
// bits, taking the last match handed back a corpse that passed every readiness test, read as
// empty, and left the layer silently detached with the menu open on screen. So the direction is
// now just a tie-break; `usable` is what actually decides.
//
// ⛔ This pass carries the SAME class-level memo as the multi-class one, and for a blunter reason:
// there are NINETEEN Derived ScreenFinders and each owns its own full GObjects pass, so the
// un-memoized path would run some nineteen times more often than the shared one. Worse, a
// `gScanEpoch` bump cancels every finder's rate limit at once — so all nineteen passes fire in the
// same tick, at a screen change, which is exactly when the player is waiting to be told what just
// appeared. A single-class scan needs a one-bit mask, which is `count == 1`.
UObject* ScanInstanceRaw(UObject* classObj, const char* className, bool (*usable)(void*),
    ClassMatch match)
{
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return nullptr;
        ClassMemo memo[kMemoSlots] = {};
        for (int32_t i = objects->size() - 1; i >= 0; i--) {
            UObject* obj = objects->at(i);
            if (!obj)
                continue;
            void* cls = ReadObjectClassRaw(obj);
            if (!cls || !MemoMaskRaw(memo, cls, &classObj, &match, 1))
                continue;
            if (IsInstanceRaw(obj, classObj, className, match) && (!usable || usable(obj)))
                return obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

namespace {

// The same walk resolving SEVERAL classes at once: `out[i]` receives the newest instance of
// `classes[i]`. One pass, so N screens cost what one costs — the point of the shared
// tracked-screen finder below.
// Returns false when the walk did not finish — GObjects unreadable, or a fault partway (the
// object array is mutated by the game thread during a world change, so this genuinely happens).
// The caller MUST NOT treat a partial result as "these screens are absent".
//
// ⛔ The per-object work here is why the class-level pre-filter exists. This pass runs over the
// WHOLE of GObjects — a few hundred thousand entries on a mission — once a second, continuously,
// because a tracked screen is absent almost always. Under ClassMatch::Exact the inner test was one
// pointer compare that failed immediately; asking the Derived form per entry instead walks the
// object's class chain to the root once PER ENTRY, thirteen times over, before failing. The memo
// below turns that back into one deref and one mask test for every object whose class has been
// seen before — which, over hundreds of thousands of instances of a few thousand classes, is
// essentially all of them.
bool ScanInstancesRaw(UObject* const* classes, const char* const* names,
    bool (*const* usable)(void*), const ClassMatch* match, void** out, int count)
{
    // The candidacy mask is one bit per entry. No caller comes close, but a silently truncated
    // mask would drop screens rather than announce itself.
    if (count < 0 || count > 32)
        return false;
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return false;
        int remaining = 0;
        for (int i = 0; i < count; i++) {
            out[i] = nullptr;
            if (classes[i])
                remaining++;
        }
        ClassMemo memo[kMemoSlots] = {};
        for (int32_t i = objects->size() - 1; i >= 0 && remaining > 0; i--) {
            UObject* obj = objects->at(i);
            if (!obj)
                continue;
            // ⛔ Through ReadObjectClassRaw, not `obj->Class`. GObjects is mutated by the game
            // thread during a world change, so a stale entry here is a fault the enclosing
            // __except would turn into a whole ABORTED pass — where the per-entry IsInstanceRaw
            // it replaces had its own guard and merely skipped the object. Same fault tolerance
            // as before, and on x64 the guard is table-driven and costs nothing until it fires.
            void* cls = ReadObjectClassRaw(obj);
            if (!cls)
                continue;
            const uint32_t mask = MemoMaskRaw(memo, cls, classes, match, count);
            if (!mask)
                continue;
            for (int c = 0; c < count; c++) {
                if (!(mask & (1u << c)) || out[c] || !classes[c]
                    || !IsInstanceRaw(obj, classes[c], names[c], match[c]))
                    continue;
                if (usable[c] && !usable[c](obj))
                    continue; // a corpse or a half-built screen is not an answer
                out[c] = obj;
                remaining--;
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

} // namespace

// Inited + focused, straight off the UIPanel flag dword. Same test as the public
// IsScreenFocused, available this early for the finders' usability check.
//
// bIsVisible is DELIBERATELY not tested, and that cost several rounds of testing to learn: the
// game leaves it false on screens that are plainly on screen and taking input. A live focused
// pause menu reads 0x259 and its exit-to-main-menu confirmation dialog reads 0x259 too — inited,
// navigable, focused, NOT visible. Requiring it meant staying silent at a modal question the
// player then had to answer blind. bIsFocused is the flag the screen stack maintains for "this
// is the screen on top", which is the thing we actually need to know.
bool IsFocusedRaw(void* screen)
{
    uint32_t flags = ReadU32Raw(screen, kPanelFlagsOffset);
    return (flags & kPanelInited) && (flags & kPanelFocused) && !(flags & kPanelRemoved);
}

// Inited + visible + not removed — for a screen nobody focuses (the loading overlay is shown
// over whatever is transitioning out), and for a cached instance that must still be showing.
bool IsShownRaw(void* screen)
{
    uint32_t flags = ReadU32Raw(screen, kPanelFlagsOffset);
    return (flags & kPanelInited) && (flags & kPanelVisible) && !(flags & kPanelRemoved);
}

// Is `obj` an instance of `baseClass`, or of anything derived from it? A screen family is often
// N subclasses (UIFacility has 22, UIInventory a dozen), so an exact class-name compare would
// miss every one of them — the same trap ScanInstanceRaw documents for XCom3DCursor.
// Walking the instance's own class chain costs a dozen name compares and needs no GObjects pass.
bool IsKindOfRaw(void* obj, const char* baseClass)
{
    if (!obj)
        return false;
    __try {
        auto* cls = reinterpret_cast<UStruct*>(reinterpret_cast<UObject*>(obj)->Class);
        char name[64];
        // Bounded like every other SuperField walk here: a UE3 chain is a dozen deep, and a
        // corrupt link must not spin.
        for (int step = 0; cls && step < 32; step++) {
            if (!ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), name, sizeof(name)))
                return false;
            if (strcmp(name, baseClass) == 0)
                return true;
            cls = reinterpret_cast<UStruct*>(cls->SuperField);
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The same question as IsKindOfRaw, decided by CLASS POINTER instead of by name. Once the base
// class object is in hand this is a handful of derefs where the name form is a GNames lookup and
// a strcmp per level, and that difference is what lets the every-tick gates tolerate a subclass:
// ReadTacticalGate walks up to 32 stack entries every frame, and the ant farm and geoscape gates
// do the same on the strategy layer.
bool IsKindOfClassRaw(void* obj, UObject* baseClassObj)
{
    if (!obj || !baseClassObj)
        return false;
    __try {
        auto* cls = reinterpret_cast<UStruct*>(reinterpret_cast<UObject*>(obj)->Class);
        // Bounded like every other SuperField walk here: a UE3 chain is a dozen deep, and a
        // corrupt link must not spin.
        for (int step = 0; cls && step < 32; step++) {
            if (reinterpret_cast<UObject*>(cls) == baseClassObj)
                return true;
            cls = reinterpret_cast<UStruct*>(cls->SuperField);
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

namespace {

// The base class object behind a ClassMatch::Derived name, resolved once. Class objects are
// created at package load and persist for the session, so a HIT is permanent.
//
// ⛔ A MISS is never cached PERMANENTLY. Packages load while the game runs — a mod's override
// class does not exist during the first seconds after injection, and the Highlander's rebuilt
// XComGame arrives with the map — so writing a class off for good would silently lose the screen
// for the rest of the session.
//
// ⛔ But a miss must BACK OFF, and a fixed one-second retry is not backing off. `FindClassRaw` is
// a full linear GObjects pass doing an `ObjNameAnsiRaw` — a nested SEH frame and a GNames deref —
// per object, and these matchers run on the TICK. A name that resolves to nothing resolves to
// nothing for the whole session, which is the ordinary case for any class in a package the
// player does not own: retried every second, that is a permanent full scan per unresolvable name,
// on the game thread, forever. It cost two of them before the popup table's DLC entries went back
// to Exact (`game_ui_popup.cpp`).
//
// So: double the interval on each miss, from one second up to a minute. A class that loads late
// is still picked up within a minute of loading — packages arrive at world changes, which are
// minutes apart — while a name that will never resolve settles at 1/60th of the old cost. The
// one-line give-up note is what turns "this screen is never found" from silent into diagnosable.
//
// ⛔ A FIXED ARRAY keyed by strcmp, not `unordered_map<std::string, …>`. The obvious map costs a
// std::string construction — and a malloc for any name past MSVC's 15-char SSO limit, which is
// most of ours — on EVERY call INCLUDING the hits. This is not a cold path: FindStackScreenRaw
// asks once per stack entry, and the three layer gates walk the stack every tick. The `sLast`
// front cache is what makes that loop one strcmp per entry, since a walk asks for the same name
// all the way down.
constexpr uint64_t kFirstRetryMs = 1000;
constexpr uint64_t kMaxRetryMs = 60000;
// Every Derived name in the tree is a string literal in a class table; ~35 of them today.
constexpr int kBaseClassCacheMax = 128;

UObject* ResolveBaseClassCached(const char* className)
{
    struct Entry {
        const char* name;
        UObject* cls;
        uint64_t lastTryMs;
        uint64_t retryMs;
        bool noted;
    };
    static Entry sCache[kBaseClassCacheMax] = {}; // game thread only
    static int sCount = 0;
    static int sLast = -1;

    Entry* entry = nullptr;
    if (sLast >= 0 && strcmp(sCache[sLast].name, className) == 0) {
        entry = &sCache[sLast];
    } else {
        for (int i = 0; i < sCount; i++) {
            if (strcmp(sCache[i].name, className) != 0)
                continue;
            sLast = i;
            entry = &sCache[i];
            break;
        }
    }
    if (!entry) {
        if (sCount >= kBaseClassCacheMax) {
            // Not reachable from a literal table, so it means someone started resolving a
            // RUNTIME name — which would also mean `name` may not outlive this call.
            LOG_WARN("GameUI: base-class cache is full at %d names — %s not resolved",
                kBaseClassCacheMax, className);
            return nullptr;
        }
        sLast = sCount++;
        entry = &sCache[sLast];
        entry->name = className; // a literal from a class table; it outlives us
        entry->retryMs = kFirstRetryMs;
    }

    if (entry->cls)
        return entry->cls;
    const uint64_t now = GetTickCount64();
    if (entry->lastTryMs && now - entry->lastTryMs < entry->retryMs)
        return nullptr;
    entry->lastTryMs = now;
    entry->cls = FindClassRaw(className);
    if (entry->cls) {
        LOG_INFO("GameUI: %s base class object at 0x%p — subclass matching armed", className,
            entry->cls);
        return entry->cls;
    }
    if (entry->retryMs < kMaxRetryMs) {
        entry->retryMs *= 2;
        if (entry->retryMs > kMaxRetryMs)
            entry->retryMs = kMaxRetryMs;
    } else if (!entry->noted) {
        // ⚠ "About two minutes", not one: the interval doubles BEFORE each wait, so the attempts
        // land at 0, 2, 6, 14, 30, 62 and 122 seconds and this branch is the eighth miss. Saying
        // "a minute" would have a tester call the mechanism broken while it was still counting.
        entry->noted = true;
        LOG_INFO("GameUI: %s still has no class object after about two minutes — subclass "
                 "matching for it stays off, re-checked once a minute from here",
            className);
    }
    return nullptr;
}

// One line the first time a Derived match lands on a class that is NOT the one asked for. This
// is how a tester's log says WHICH override is live: without it a mod screen that we now find
// and read is indistinguishable in the log from the vanilla one, and a screen we find and read
// WRONG produces a bug report about silence somewhere else entirely.
//
// Latched per (base, actual) pair rather than per call site, because the same base is asked for
// from several sites and the fact worth recording is which subclass is installed, not which of
// our finders noticed it first.
//
// ⚠ It says "subclass", not "override", because it cannot tell the two apart and a log line that
// guessed would be worse than none: `UIAlert_DLC_Day60` and `UIArmory_Loadout_MP` are stock
// Firaxis classes that match their base here, and a tester reading "override" would go looking for
// a mod that is not installed.
//
// ⛔ The latch allocates NOTHING on the repeat path, which is the only path that matters here: for
// a player who actually has the subclass — the whole audience this line exists for — a per-tick
// finder matches on every tick and takes this route every time. `std::unordered_set<std::string>`
// cannot do that: its hash and equality are not transparent, so even a `find(key)` on a `char[]`
// materialises a temporary std::string, and two class names are always past the SSO limit. A flat
// array of fixed pairs, searched with strcmp, is both smaller and honest about being tiny — the
// whole point is that it stops growing after the first few lines.
constexpr int kNoteMax = 64;

void NoteOverrideRaw(const char* base, void* obj)
{
    char actual[64];
    if (!ObjClassNameAnsiRaw(obj, actual, sizeof(actual)) || strcmp(actual, base) == 0)
        return;
    struct Note {
        char base[64];
        char actual[64];
    };
    static Note sSeen[kNoteMax] = {}; // game thread only
    static int sCount = 0;
    for (int i = 0; i < sCount; i++) {
        if (strcmp(sSeen[i].base, base) == 0 && strcmp(sSeen[i].actual, actual) == 0)
            return;
    }
    if (sCount < kNoteMax) {
        strncpy_s(sSeen[sCount].base, base, _TRUNCATE);
        strncpy_s(sSeen[sCount].actual, actual, _TRUNCATE);
        sCount++;
    }
    LOG_INFO("GameUI: %s matched subclass %s", base, actual);
}

} // namespace

// Bumped by HintScreenChange. A finder that scanned in an older epoch has a cache from before a
// known screen change, so it rescans immediately instead of waiting out its rate limit — the
// rate limit is there to bound idle cost, not to make a screen change take a second to notice.
uint64_t gScanEpoch = 1;

// The cached find-a-screen pattern shared by every screen finder: resolve the class object once,
// keep the newest live instance, and rescan GObjects at most once a second.
//
// Revalidating the cache by TYPE alone is not enough, and getting that wrong cost 63 seconds of
// silence in testing: returning to the main menu builds a NEW UIFinalShell while the old one
// sits in GObjects unfocused until GC gets to it. The dead one still passes IsInstanceRaw, so
// the finder kept handing it back, every probe saw an unfocused screen, and nothing rescanned
// until the allocation was finally reused. So the cache short-circuits the scan only while it
// is USABLE (`usable`, e.g. still the focused screen); otherwise the rescan runs and takes
// whatever is newest now. A screen the scan cannot find clears the cache rather than lingering.
void* ScreenFinder::Find()
{
    // The scan only ever returns a USABLE instance, so the cache holds one too and the
    // revalidation here is just "is it still that". Anything else — collected, removed,
    // covered by another screen — drops it and waits for the next scan rather than handing
    // back a pointer that already failed the test.
    if (cached
        && IsInstanceRaw(reinterpret_cast<UObject*>(cached), classObj, className, match)
        && (!usable || usable(cached)))
        return cached;
    cached = nullptr;

    uint64_t now = GetTickCount64();
    if (scanEpoch == gScanEpoch && now - lastScanMs < 1000)
        return cached;
    lastScanMs = now;
    scanEpoch = gScanEpoch;

    if (!classObj) {
        classObj = FindClassRaw(className);
        if (classObj)
            LOG_INFO("GameUI: %s class object at 0x%p", className, classObj);
        else if (++emptyScans % 30 == 1)
            LOG_DEBUG("GameUI: %s class object not found yet (scan #%d)", className, emptyScans);
        if (!classObj)
            return nullptr;
        emptyScans = 0;
    }

    void* found = ScanInstanceRaw(classObj, className, usable, match);
    // Only a CHANGE is worth a line. The rescan runs once a second for as long as the screen
    // exists but is covered (that is the whole point of it), so logging every hit would bury
    // the log in repeats of the same address.
    if (found && found != cached) {
        LOG_INFO("GameUI: found %s instance at 0x%p", className, found);
        if (match == ClassMatch::Derived)
            NoteOverrideRaw(className, found);
    }
    cached = found;
    if (!cached && ++emptyScans % 30 == 1)
        LOG_DEBUG("GameUI: no %s instance yet (scan #%d)", className, emptyScans);
    return cached;
}

// ---- plain array reads ----

// Any TArray<T*> at `offset`: copies up to maxItems pointers out, indices preserved. Returns
// the count copied, or -1 if the array itself could not be read. `clipped`, when asked for, says
// whether the array held MORE than maxItems — a different fact from "the count came back equal
// to the cap", and the one TakeArrayRaw below has to report.
int ReadPtrArrayRaw(const void* obj, int32_t offset, void** out, int maxItems, bool* clipped)
{
    if (clipped)
        *clipped = false;
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(obj) + offset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0)
            return 0;
        if (n > maxItems) {
            n = maxItems;
            if (clipped)
                *clipped = true;
        }
        for (int32_t i = 0; i < n; i++)
            out[i] = data[i];
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// A TArray's own count at `offset`, without reading one element: -1 if it could not be read.
// This is what lets a read be sized from the DATA rather than from a buffer we picked in
// advance — see ReadPtrArrayAllRaw.
int32_t ReadArrayCountRaw(const void* obj, int32_t offset)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(obj) + offset;
        if (!*reinterpret_cast<void* const* const*>(arr))
            return 0;
        return *reinterpret_cast<const int32_t*>(arr + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Element `index` of a TArray<FString> at `offset` (16-byte elements).
int CopyFStringElemRaw(const void* obj, int32_t offset, int index, wchar_t* buf, int bufWchars)
{
    buf[0] = L'\0';
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(obj) + offset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || index < 0 || index >= n || n > 4096)
            return 0;
        return CopyFStringRaw(data + static_cast<size_t>(index) * 16, buf, bufWchars);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buf[0] = L'\0';
        return 0;
    }
}

// A UIList's items in index order. GetItem(i) IS ItemContainer.ChildPanels[i] (UIList.uc), so
// reading the child array directly matches the game's own indexing — which is what
// SetSelectedIndex and the OnItemClicked delegates are expressed in.
int ReadListItemsRaw(void* list, void** out, int maxItems, bool* clipped)
{
    if (clipped)
        *clipped = false;
    if (!list)
        return 0;
    void* container = ReadPtrRaw(list, kListItemContainerOffset);
    if (!container)
        return 0;
    return ReadPtrArrayRaw(container, kPanelChildPanelsOffset, out, maxItems, clipped);
}

// A TArray<int32> at `offset`: copies up to maxItems values out. Returns the count, or -1 if the
// array itself could not be read; `clipped` follows ReadPtrArrayRaw's contract.
int ReadIntArrayRaw(const void* obj, int32_t offset, int32_t* out, int maxItems, bool* clipped)
{
    if (clipped)
        *clipped = false;
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(obj) + offset;
        auto* data = *reinterpret_cast<const int32_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0)
            return 0;
        if (n > maxItems) {
            n = maxItems;
            if (clipped)
                *clipped = true;
        }
        for (int32_t i = 0; i < n; i++)
            out[i] = data[i];
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// ---- bounded array reads that SAY SO when they clip ----
//
// ⛔ Size the buffer from the data, never the data to the buffer — and a cap that drops the tail
// must be LOUD. A silently truncated list reads to a blind player as *the screen has nothing
// more*: indistinguishable from the truth, and therefore never reported as a bug. That is the
// failure mode the 2051-character payload had, and every fixed-cap array read in this file is
// the same bug waiting for a long enough list.
//
// Most caps here are structural — a room has eight staff slots, the strategy map has
// MAX_NUM_STRATEGYICONS — but "structural" is a claim about the GAME that holds only until a mod
// or a patch disagrees, and the -1 the primitives above return on a FAULTED read is otherwise
// indistinguishable from an empty list. Both go through here.
//
// Once per SITE, not once per render: these run on every graph rebuild, ten times a second. The
// latch map is touched only on the report path, so the ordinary case costs one compare.

namespace {

constexpr uint8_t kNoteFault = 0x1;       // the read faulted
constexpr uint8_t kNoteClip = 0x2;        // the tail was dropped
constexpr uint8_t kNoteImplausible = 0x4; // the count was too large to believe

// True the FIRST time this (site, kind) pair comes up. The latch is what makes a warning
// affordable on a path that runs ten times a second.
bool NoteOnce(const char* what, uint8_t kind)
{
    static std::unordered_map<std::string, uint8_t> sSaid;
    uint8_t& said = sSaid[what ? what : "?"];
    if (said & kind)
        return false;
    said |= kind;
    return true;
}

} // namespace

int NoteArrayRead(const char* what, int count, bool clipped, int maxItems)
{
    if (count < 0) {
        if (NoteOnce(what, kNoteFault))
            LOG_WARN("%s: the array could not be read — treating it as empty", what);
        return 0;
    }
    if (clipped && NoteOnce(what, kNoteClip))
        LOG_WARN("%s: more than %d entries — the tail is NOT being read", what, maxItems);
    return count;
}

// ReadPtrArrayRaw with the -1 sentinel honoured and a dropped tail logged. `what` names the site
// and doubles as the latch key, so pass a literal.
int TakeArrayRaw(const char* what, const void* obj, int32_t offset, void** out, int maxItems)
{
    bool clipped = false;
    const int count = ReadPtrArrayRaw(obj, offset, out, maxItems, &clipped);
    return NoteArrayRead(what, count, clipped, maxItems);
}

// ReadListItemsRaw under the same contract.
int TakeListItemsRaw(const char* what, void* list, void** out, int maxItems)
{
    bool clipped = false;
    const int count = ReadListItemsRaw(list, out, maxItems, &clipped);
    return NoteArrayRead(what, count, clipped, maxItems);
}

// The WHOLE of a TArray<T*>, sized from the array's own count. For a list whose length the GAME
// decides and nothing bounds — the save list is the one that really grows — a cap is not
// something to log about, it is something not to have: the entries it drops are saves the player
// made, and a list that stops reads exactly like a list that ended. A count too large to be a
// screen's list is refused with a line rather than walked.
std::vector<void*> ReadPtrArrayAllRaw(const char* what, const void* obj, int32_t offset)
{
    std::vector<void*> out;
    const int32_t n = ReadArrayCountRaw(obj, offset);
    if (n == 0)
        return out;
    if (n < 0) {
        NoteArrayRead(what, -1, false, 0);
        return out;
    }
    // Not a guess about this screen: past this a count is far likelier a bad offset than a long
    // list, and allocating for it would be the fault we are trying to avoid.
    constexpr int32_t kImplausibleCount = 65536;
    if (n > kImplausibleCount) {
        if (NoteOnce(what, kNoteImplausible))
            LOG_WARN("%s: the array claims %d entries — refusing to read it", what, n);
        return out;
    }
    out.resize(static_cast<size_t>(n));
    const int got = ReadPtrArrayRaw(obj, offset, out.data(), n);
    out.resize(static_cast<size_t>(NoteArrayRead(what, got, false, n)));
    return out;
}

// ---- the game's own screen stack ----
//
// This is the authoritative answer to "what is on screen", and it is why the GObjects scans
// are now only a fallback. A popped screen is off the stack the instant it is popped,
// whereas as an OBJECT it lingers with all its state bits intact until GC — which is how a
// corpse ended up cached as the live pause menu (see ScanInstanceRaw).
//
// Screens is TOP-FIRST: Push does Screens.InsertItem(0, Screen) and GetCurrentScreen returns
// the first non-none entry.

namespace {

// A stack worth trusting: it has screens and its top one is live. During a world change the
// outgoing stack is torn down, so this is what distinguishes it from the incoming one.
bool StackIsLiveRaw(void* stack)
{
    void* top[1] = {};
    if (ReadPtrArrayRaw(stack, kStackScreensOffset, top, 1) < 1 || !top[0])
        return false;
    uint32_t flags = ReadU32Raw(top[0], kPanelFlagsOffset);
    return (flags & kPanelInited) && !(flags & kPanelRemoved);
}

ScreenFinder gStackFinder{"UIScreenStack", StackIsLiveRaw};

} // namespace

// Screens, top first, into `out`; 0 when there is no live stack. Every walker opens with
// this — one place to find the stack, one place that says so if the array outgrows the buffer.
int WalkStackRaw(void** out, int max)
{
    void* stack = gStackFinder.Find();
    if (!stack)
        return 0;
    return TakeArrayRaw("ScreenStack: Screens", stack, kStackScreensOffset, out, max);
}

// ⛔ THE READINESS PREDICATE FOR A SCREEN ON THE STACK, in one place, because it is spelled out
// at a dozen walkers and two hard rules are about it.
//
// `bIsInited && !bIsRemoved`, and NOT bIsVisible: the game leaves the visible bit false on
// screens it is plainly showing (a live pause menu reads 0x259), so testing it means silence at
// a modal the player then answers blind. bIsRemoved is the one flag a corpse does not keep —
// removal sets it and clears nothing else, so inited/visible/focused all still read true on a
// dead screen.
//
// This deliberately does NOT also require bIsFocused, which IS the right test for a GObjects
// scan (see IsFocusedRaw) and would be redundant here: position in this array already answers
// "is it on top", and it is the game's own answer rather than a bit we hope is in step.
bool StackEntryLiveRaw(void* screen)
{
    const uint32_t flags = ReadU32Raw(screen, kPanelFlagsOffset);
    return (flags & kPanelInited) && !(flags & kPanelRemoved);
}

// The GetCurrentScreen rule, which the current-screen finders share: the TOPMOST non-null entry,
// and only when it matches and is live. A non-match on top is a REFUSAL, not a reason to keep
// looking — a screen under another screen has already lost the keys, and answering for it would
// put our stops in front of the screen the player is actually on.
//
// `matches` is handed the entry and its class name, so a matcher may compare either; `nameOut`
// receives that name when an entry is returned.
void* TopStackScreenRaw(bool (*matches)(void* screen, const char* className),
    char* nameOut, int nameCap)
{
    void* screens[kMaxStackScreens] = {};
    const int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            return nullptr;
        if (!matches(screens[i], name) || !StackEntryLiveRaw(screens[i]))
            return nullptr;
        if (nameOut && nameCap > 0)
            strncpy_s(nameOut, static_cast<size_t>(nameCap), name, _TRUNCATE);
        return screens[i];
    }
    return nullptr;
}

// The topmost live screen of `className` on the stack, or null if the stack does not hold one.
//
// Unlike TopStackScreenRaw this keeps LOOKING past a non-match: the caller asked for a specific
// screen wherever it sits, not for the one on top.
//
// This is the cheapest half of the ModClassOverrides story and the one that fixes most of it: the
// stack is the authority on what is on screen and it is a handful of entries, so a tolerant walk
// here finds an override the instant it is pushed.
//
// ⚠ The two policies cost differently, and the difference is not where it looks. Exact compares
// class NAMES and resolves no UClass at all — a strcmp per entry over a few dozen entries, which
// is why this was always cheap. Derived must RESOLVE `className` to a class object first, and
// `ResolveBaseClassCached` does that with a full linear GObjects pass. A hit is resolved once and
// then it is a bounded pointer chase per entry, so the steady state is fine; a name that resolves
// to NOTHING is the case to think about, and the resolver backs off to once a minute rather than
// scanning every second forever. Do not read "the stack is small" as "Derived is free here".
void* FindStackScreenRaw(const char* className, ClassMatch match)
{
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (!screens[i] || !StackEntryLiveRaw(screens[i]))
            continue;
        if (!ObjectClassNamedRaw(screens[i], className, match))
            continue;
        if (match == ClassMatch::Derived)
            NoteOverrideRaw(className, screens[i]);
        return screens[i];
    }
    return nullptr;
}

// Stack first, object scan second. The stack answers correctly and instantly for anything it
// manages; the fallback covers screens it does not (UIDialogueBox is permanent and never
// pushed) and the window during a world change while the stack itself is being rebuilt. The
// first time a class can only be found by the fallback we say so once — that is how we learn
// which screens are not stack-managed, instead of assuming.
void* PreferStack(const char* className, ScreenFinder& finder, bool& reportedFallback,
    ClassMatch match)
{
    // ⛔ A gate is only as good as its other half. Writing the policy into the finder rather than
    // trusting its own initializer is what makes it impossible for a call site to tolerate an
    // override on the stack and lose it to the scan — which would show up only during a world
    // change, or never for UIDialogueBox, which the stack does not manage at all.
    finder.match = match;
    if (void* onStack = FindStackScreenRaw(className, match))
        return onStack;
    void* found = finder.Find();
    if (found && !reportedFallback) {
        reportedFallback = true;
        LOG_INFO("GameUI: %s is not on the screen stack — falling back to an object scan",
            className);
    }
    return found;
}

namespace {

// The TTextTooltipData element with this id in the manager's shared TextTooltip (where
// SetTooltipText text lands), or null.
const uint8_t* FindTextTooltipDataRaw(void* mgr, int32_t id)
{
    __try {
        void* shared = *reinterpret_cast<void* const*>(
            reinterpret_cast<const uint8_t*>(mgr) + kMgrTextTooltipOffset);
        if (!shared)
            return nullptr;
        auto* arr = reinterpret_cast<const uint8_t*>(shared) + kTextTooltipDataOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 4096)
            return nullptr;
        for (int32_t i = 0; i < n; i++) {
            const uint8_t* el = data + static_cast<size_t>(i) * kTTDataStride;
            if (*reinterpret_cast<const int32_t*>(el + kTTDataIdOffset) == id)
                return el;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

} // namespace

// SEH-safe: the ANSI text of the FName stored AT `addr` (entry id, then instance number), the
// way the engine prints it — instance N shows as "_N-1", which is what makes a live panel's
// MCPath read "UIFinalShell_0". Distinct from ObjNameAnsiRaw, which reads an OBJECT's own name.
bool FNameAtAnsiRaw(const void* addr, char* buf, int bufLen)
{
    __try {
        const int32_t id = *reinterpret_cast<const int32_t*>(addr);
        const int32_t instance = *(reinterpret_cast<const int32_t*>(addr) + 1);
        TArray<FNameEntry*>* names = FName::Names();
        if (!names || id < 0 || id >= names->size())
            return false;
        FNameEntry* entry = names->at(id);
        if (!entry)
            return false;
        const char* ansi = entry->GetAnsiName();
        if (!ansi)
            return false;
        int i = 0;
        for (; i < bufLen - 1 && ansi[i]; i++)
            buf[i] = ansi[i];
        buf[i] = '\0';
        if (instance > 0 && i < bufLen - 1)
            snprintf(buf + i, static_cast<size_t>(bufLen - i), "_%d", instance - 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

namespace {

// The bounds of the manager's shared tooltip-data array, PODs only. Split out from the search
// below because an SEH __try cannot live in a frame that needs object unwinding (C2712), and
// matching a path means building std::strings.
bool TextTooltipDataSpanRaw(void* mgr, const uint8_t** outData, int32_t* outCount)
{
    __try {
        void* shared = *reinterpret_cast<void* const*>(
            reinterpret_cast<const uint8_t*>(mgr) + kMgrTextTooltipOffset);
        if (!shared)
            return false;
        auto* arr = reinterpret_cast<const uint8_t*>(shared) + kTextTooltipDataOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 4096)
            return false;
        *outData = data;
        *outCount = n;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The TTextTooltipData element registered against exactly this Flash path, or null. Exact match
// rather than a suffix: the caller composes the whole path from a live panel's own MCPath, and a
// suffix test would happily answer with another screen's row of the same name.
const uint8_t* FindTextTooltipDataByPathRaw(void* mgr, const std::string& path)
{
    const uint8_t* data = nullptr;
    int32_t n = 0;
    if (!TextTooltipDataSpanRaw(mgr, &data, &n))
        return nullptr;
    // Newest LAST is how the array grows, so search backwards: a screen that re-populates
    // registers a second entry against the same path without removing the first, and the one
    // the player is actually hovering is the one added most recently.
    for (int32_t i = n - 1; i >= 0; i--) {
        const uint8_t* el = data + static_cast<size_t>(i) * kTTDataStride;
        if (ReadFStringFieldRaw(el, kTTDataTargetPathOffset) == path)
            return el;
    }
    return nullptr;
}

// The tooltip with this id in the manager's Tooltips array (preformed tooltips only), or null.
void* FindTooltipByIdRaw(void* mgr, int32_t id)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(mgr) + kMgrTooltipsOffset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 4096)
            return nullptr;
        for (int32_t i = 0; i < n; i++) {
            void* tip = data[i];
            if (tip
                && *reinterpret_cast<const int32_t*>(
                       reinterpret_cast<const uint8_t*>(tip) + kTooltipIdOffset)
                    == id)
                return tip;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

void* ReadObjectClassRaw(UObject* obj)
{
    __try {
        return obj->Class;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

} // namespace

// The object's class is named `className` — exactly by default (no inheritance walk, for where
// the concrete class is known), or anywhere on its class chain under Derived.
//
// ⛔ Derived FALLS BACK to the exact compare while the base class object is still unresolved,
// rather than answering "no". The first seconds after injection are exactly that window —
// packages are still loading — and a flat refusal there would lose every screen until the class
// turned up, which is the silence this whole mechanism exists to remove.
bool ObjectClassNamedRaw(void* obj, const char* className, ClassMatch match)
{
    if (match == ClassMatch::Derived) {
        if (UObject* base = ResolveBaseClassCached(className))
            return IsKindOfClassRaw(obj, base);
    }
    void* cls = ReadObjectClassRaw(reinterpret_cast<UObject*>(obj));
    char name[64];
    return cls && ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), name, sizeof(name))
        && strcmp(name, className) == 0;
}

// The index of the FIRST name `obj` answers to, or -1.
//
// ⛔ THE TABLE'S ORDER IS THE DISCRIMINATION, and under Derived it is the ONLY thing doing that
// work. UIArmory_PromotionHero and UIArmory_PromotionPsiOp both extend UIArmory_Promotion, so a
// tolerant compare against the base answers true for all three — most-derived first is what keeps
// the Community Promotion Screen's CPS_UIArmory_PromotionHero reading with HERO offsets instead
// of Classic ones. `matches` is per-entry for the other shape of the same problem: the popup
// allow-list wants tolerance on fourteen classes and exactness on three — UIProgressDialogue,
// whose UIReconnectController descendant no recipe has ever read, and the two DLC alerts, which
// a tolerant entry 0 makes unreachable anyway (`game_ui_popup.cpp`).
int MatchClassIndexRaw(void* obj, const char* const* names, const ClassMatch* matches, int count)
{
    for (int i = 0; i < count; i++) {
        if (!ObjectClassNamedRaw(obj, names[i], matches[i]))
            continue;
        if (matches[i] == ClassMatch::Derived)
            NoteOverrideRaw(names[i], obj);
        return i;
    }
    return -1;
}

namespace {

// Find a UFunction by name walking a class's field chain (super classes included) — avoids the
// SDK's whole-GObjects function cache, whose first build stalls the game thread for seconds.
// Field names resolve through ObjNameAnsiRaw (bounds-checked, per-field SEH), so one malformed
// field skips rather than aborting the whole walk.
UFunction* FindFunctionInClassRaw(UObject* obj, const char* funcName)
{
    __try {
        char name[64];
        for (UStruct* cur = reinterpret_cast<UStruct*>(obj->Class); cur;
             cur = reinterpret_cast<UStruct*>(cur->SuperField)) {
            for (UField* field = cur->Children; field; field = field->Next) {
                if (!ObjNameAnsiRaw(field, name, sizeof(name)) || strcmp(name, funcName) != 0)
                    continue;
                // Properties and functions share the field chain; require a Function object.
                UClass* fieldClass = field->Class;
                if (!fieldClass)
                    continue;
                if (ObjNameAnsiRaw(reinterpret_cast<UObject*>(fieldClass), name, sizeof(name))
                    && strcmp(name, "Function") == 0)
                    return reinterpret_cast<UFunction*>(field);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// One level of a UStruct's own field chain, matched by BOTH name and field class. The class
// check is what makes this usable for states: properties, functions and states all share the
// single `Children` chain, so a name match alone would happily hand back a UProperty.
UField* FindFieldInStructRaw(void* structObj, const char* fieldName, const char* fieldClass)
{
    __try {
        char name[64];
        for (UField* field = reinterpret_cast<UStruct*>(structObj)->Children; field;
             field = field->Next) {
            if (!ObjNameAnsiRaw(field, name, sizeof(name)) || strcmp(name, fieldName) != 0)
                continue;
            UClass* cls = field->Class;
            if (!cls)
                continue;
            if (ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), name, sizeof(name))
                && strcmp(name, fieldClass) == 0)
                return field;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// Same walk as FindFunctionInClassRaw, but starting AT the given UStruct instead of at its
// class — for looking a function up on a UClass object directly, which is how statics are
// called (the generated wrappers pass the class object as `this`).
UFunction* FindFunctionInClassStructRaw(void* classObj, const char* funcName)
{
    __try {
        char name[64];
        for (UStruct* cur = reinterpret_cast<UStruct*>(classObj); cur;
             cur = reinterpret_cast<UStruct*>(cur->SuperField)) {
            for (UField* field = cur->Children; field; field = field->Next) {
                if (!ObjNameAnsiRaw(field, name, sizeof(name)) || strcmp(name, funcName) != 0)
                    continue;
                UClass* fieldClass = field->Class;
                if (!fieldClass)
                    continue;
                if (ObjNameAnsiRaw(reinterpret_cast<UObject*>(fieldClass), name, sizeof(name))
                    && strcmp(name, "Function") == 0)
                    return reinterpret_cast<UFunction*>(field);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// Resolution cache + first-resolution logging (game thread only). Keyed per (class, name) —
// two classes can each define the same function name (OnUnrealCommand, Click), and a cache
// keyed by name alone would hand one class's UFunction to the other's instances. UFunction and
// class objects persist for the session. A NOT-FOUND is logged loudly because a silent
// write-back failure costs a debug session to find.
UFunction* ResolveFunctionCached(void* obj, const char* funcName)
{
    static std::unordered_map<std::string, UFunction*> sCache;
    void* cls = ReadObjectClassRaw(reinterpret_cast<UObject*>(obj));
    if (!cls)
        return nullptr; // unreadable object — don't resolve against it, don't cache
    std::string key = std::string(funcName) + "@" + std::to_string(reinterpret_cast<uintptr_t>(cls));
    auto it = sCache.find(key);
    if (it != sCache.end())
        return it->second;
    UFunction* fn = FindFunctionInClassRaw(reinterpret_cast<UObject*>(obj), funcName);
    if (fn)
        LOG_INFO("GameUI: resolved function \"%s\" (class 0x%p) at 0x%p", funcName, cls, fn);
    else
        LOG_WARN("GameUI: function \"%s\" NOT FOUND on the object's class chain", funcName);
    sCache.emplace(std::move(key), fn);
    return fn;
}

bool CallFunctionRaw(void* obj, UFunction* fn, void* params)
{
    __try {
        reinterpret_cast<UObject*>(obj)->ProcessEvent(fn, params, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

bool CallNamedFunction(void* obj, const char* funcName, void* params)
{
    if (!obj)
        return false;
    UFunction* fn = ResolveFunctionCached(obj, funcName);
    return fn && CallFunctionRaw(obj, fn, params);
}

// A `static native` script function, called on the CLASS object itself — what the generated
// wrappers pass as `this` for statics.
//
// Call it through plain ProcessEvent, exactly like a script function. The generated SDK wrappers
// clear FUNC_Native (0x400) around the call and copying that is a TRAP here: a native function's
// Script array holds a 2-byte stub, not a body (measured: IsAnyMoviePlaying has Script.Count 2,
// against 1663 for UIPauseMenu.OnChildClicked), so clearing the flag makes ProcessEvent run the
// stub instead of the native and return a zeroed param block — indistinguishable from a real
// "no". X2FiraxisLiveClient.IsLoggedIn is FUNC_Native too and has always worked here through
// plain ProcessEvent; that is the proof this is the right call.
bool CallStaticNative(const char* className, const char* funcName, void* params)
{
    static std::unordered_map<std::string, UObject*> sClasses;
    static std::unordered_map<std::string, UFunction*> sFuncs;
    static uint64_t sLastTryMs = 0;

    UObject*& cls = sClasses[className];
    if (!cls) {
        // Resolving a class is a full GObjects walk; if it is genuinely missing, don't pay that
        // on every poll.
        uint64_t now = GetTickCount64();
        if (now - sLastTryMs < 1000)
            return false;
        sLastTryMs = now;
        cls = FindClassRaw(className);
        if (!cls)
            return false;
        LOG_INFO("GameUI: %s class object at 0x%p", className, cls);
    }

    UFunction*& fn = sFuncs[std::string(className) + "." + funcName];
    if (!fn) {
        fn = FindFunctionInClassStructRaw(cls, funcName);
        if (!fn) {
            LOG_WARN("GameUI: static native \"%s.%s\" NOT FOUND", className, funcName);
            return false;
        }
        LOG_INFO("GameUI: resolved static native \"%s.%s\" at 0x%p", className, funcName, fn);
    }
    return CallFunctionRaw(cls, fn, params);
}

namespace {

// The current world's sound manager instance. The GRI spawns it as a SUBCLASS: XComShellGRI
// extends XComTacticalGRI, so the shell (main menu) world's manager is an
// XComTacticalSoundManager too; the Avenger uses XComStrategySoundManager. Both register the
// UI cues (DefaultGameData [XComGame.XCom*SoundManager] includes SoundGlobalUI.Play_Mouseover).
void* FindSoundManager()
{
    static const char* const kClassNames[2]
        = {"XComTacticalSoundManager", "XComStrategySoundManager"};
    static UObject* sClasses[2] = {nullptr, nullptr};
    static void* sCached = nullptr;
    static int sCachedIdx = -1;
    static uint64_t sLastScanMs = 0;
    static uint64_t sScanEpoch = 0;

    // Type-revalidation alone is NOT enough here, for the same reason it is not enough for a
    // screen (see ScreenFinder): a world change spawns a new manager while the old one stays a
    // perfectly well-formed instance of its class until GC gets to it. A cue sent to the dead
    // one is swallowed in silence — which is why the arrow-key click stopped after exiting a
    // campaign to the main menu, while narration carried on fine. There is no focus bit to ask
    // a sound manager about, so instead the cache is trusted for only a second at a time and
    // every rescan takes the NEWEST instance of either subclass.
    uint64_t now = GetTickCount64();
    bool live = sCached && sCachedIdx >= 0
        && IsInstanceRaw(reinterpret_cast<UObject*>(sCached), sClasses[sCachedIdx],
            kClassNames[sCachedIdx]);
    if (!live) {
        sCached = nullptr;
        sCachedIdx = -1;
    }
    if (sScanEpoch == gScanEpoch && now - sLastScanMs < 1000)
        return sCached;
    sLastScanMs = now;
    sScanEpoch = gScanEpoch;

    for (int i = 0; i < 2; i++)
        if (!sClasses[i])
            sClasses[i] = FindClassRaw(kClassNames[i]);

    // One walk from the newest object down, matching either subclass — the first hit is the
    // newest manager of either kind, which is the current world's.
    void* found = nullptr;
    int foundIdx = -1;
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        for (int32_t o = objects ? objects->size() - 1 : -1; o >= 0 && !found; o--) {
            UObject* obj = objects->at(o);
            if (!obj)
                continue;
            for (int i = 0; i < 2; i++) {
                if (sClasses[i] && IsInstanceRaw(obj, sClasses[i], kClassNames[i])) {
                    found = obj;
                    foundIdx = i;
                    break;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    if (found && found != sCached)
        LOG_INFO("GameUI: sound manager (%s) at 0x%p", kClassNames[foundIdx], found);
    sCached = found;
    sCachedIdx = foundIdx;
    return sCached;
}

} // namespace

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (needed <= 0)
        return {};
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0],
        needed);
    return result;
}

std::string WcharToUtf8(const wchar_t* wbuf, int len)
{
    if (len <= 0)
        return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, wbuf, len, nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf, len, &result[0], needed, nullptr, nullptr);
    return result;
}

// An FString field read straight to cleaned UTF-8 (empty on fault or empty string).
std::string ReadFStringField(const void* obj, int32_t offset)
{
    wchar_t buf[1024];
    int len = CopyFStringRaw(reinterpret_cast<const uint8_t*>(obj) + offset, buf, 1024);
    return len > 0 ? CleanText(WcharToUtf8(buf, len)) : std::string();
}

// The same read with NO cleaning, for a field that is an identifier rather than a phrase — a
// Flash path being matched, markup a caller has to translate. CleanText would strip <...> and
// collapse whitespace, so a cleaned value can never be compared against a raw one.
std::string ReadFStringFieldRaw(const void* obj, int32_t offset)
{
    wchar_t buf[1024];
    int len = CopyFStringRaw(reinterpret_cast<const uint8_t*>(obj) + offset, buf, 1024);
    return len > 0 ? WcharToUtf8(buf, len) : std::string();
}

// ---- Flash capture operands, by position (see the header) ----

std::string FlashArg(const std::vector<std::string>& args, size_t index)
{
    return index < args.size() ? CleanText(args[index]) : std::string();
}

std::vector<std::string> FlashArgLines(const std::vector<std::string>& args, size_t index)
{
    std::vector<std::string> lines;
    if (index >= args.size())
        return lines;
    const std::string& raw = args[index];
    size_t start = 0;
    for (;;) {
        const size_t hit = raw.find('\n', start);
        std::string line
            = CleanText(raw.substr(start, hit == std::string::npos ? hit : hit - start));
        if (!line.empty())
            lines.push_back(std::move(line));
        if (hit == std::string::npos)
            return lines;
        start = hit + 1;
    }
}

// ---- small scalar reads ----

uint8_t ReadU8Raw(const void* obj, int32_t offset)
{
    __try {
        return *(reinterpret_cast<const uint8_t*>(obj) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFF;
    }
}

float ReadF32Raw(const void* obj, int32_t offset)
{
    __try {
        return *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(obj) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

int32_t ReadI32Raw(const void* obj, int32_t offset)
{
    return static_cast<int32_t>(ReadU32Raw(obj, offset));
}

namespace {

// The dropdown's current selection text: Items[SelectedItem] out of a TArray<FString>
// (16-byte elements: data/count/max).
int ReadDropdownSelectionRaw(const void* dropdown, wchar_t* buf, int bufWchars)
{
    buf[0] = L'\0';
    __try {
        int32_t sel = *reinterpret_cast<const int32_t*>(
            reinterpret_cast<const uint8_t*>(dropdown) + kDropdownSelectedOffset);
        auto* arr = reinterpret_cast<const uint8_t*>(dropdown) + kDropdownItemsOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || sel < 0 || sel >= n || n > 4096)
            return 0;
        return CopyFStringRaw(data + static_cast<size_t>(sel) * 16, buf, bufWchars);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buf[0] = L'\0';
        return 0;
    }
}

struct RawOptionRow {
    uint8_t type;
    bool disabled;
    bool checked;
    int percent;
    wchar_t label[256];
    wchar_t value[256];
};

// One row: label from Desc (SetHTMLText fills the HTML slot; plain Text is the fallback) and
// the active widget's current state, selected by the row's Type byte. False = the row is
// hidden or unreadable and should not become a node.
bool ReadOptionRowRaw(void* item, RawOptionRow* out)
{
    out->type = 0xFF;
    out->disabled = false;
    out->checked = false;
    out->percent = -1;
    out->label[0] = L'\0';
    out->value[0] = L'\0';

    if (!item || !(ReadU32Raw(item, kPanelFlagsOffset) & kPanelVisible))
        return false;

    out->type = ReadU8Raw(item, kMechaTypeOffset);
    out->disabled = (ReadU32Raw(item, kMechaFlagsOffset) & kMechaDisabled) != 0;

    // A row's declared Type is NOT a reliable answer to "is this a checkbox". Every Second Wave
    // row reads Type = Description while showing a live, visible UICheckbox whose bChecked is
    // the very bit the campaign start reads (GetSelectedSecondWaveOptionNames) — verified live
    // 2026-08-19 with the box ticked on screen. Reading state only in the Type branch made those
    // rows report "unchecked" forever, which looked exactly like a toggle that would not take.
    //
    // ⚠ WHERE THAT Description COMES FROM, since it would not reproduce for anyone reading
    // SrcOrig: the GAME types those rows Checkbox (UIShellDifficulty.uc:472, and again inside
    // UpdateDataCheckbox at UIMechaListItem.uc:549). OUR OWN companion rewrites them —
    // UIScreenListener_Difficulty.FixSecondWaveItems, once per Second Wave panel open, so that
    // Enter stops being eaten by a bReadOnly checkbox. So on this screen the Type tag is a field
    // two of our layers touch, and it must never be read as evidence about the engine.
    //
    // The rule survives that correction, because the reason to prefer the VISIBLE widget is the
    // GAME's: SetWidgetType hides the widgets it switches away from and leaves their pointers
    // behind, so presence alone means nothing while visibility means the row is showing one.
    // Reading the widget POINTER rather than the tag is also what makes this immune to a Type a
    // mod — ours included — has rewritten. State comes from the checkbox whenever there is one;
    // only the row's KIND needs it visible.
    void* checkbox = ReadPtrRaw(item, kMechaCheckboxOffset);
    if (checkbox) {
        out->checked = (ReadU32Raw(checkbox, kCheckboxFlagsOffset) & kCheckboxChecked) != 0;
        if (ReadU32Raw(checkbox, kPanelFlagsOffset) & kPanelVisible)
            out->type = kLineCheckbox;
    }

    void* desc = ReadPtrRaw(item, kMechaDescOffset);
    if (desc) {
        if (CopyFStringRaw(reinterpret_cast<const uint8_t*>(desc) + kScrollHtmlOffset,
                out->label, 256) <= 0)
            CopyFStringRaw(reinterpret_cast<const uint8_t*>(desc) + kScrollTextOffset,
                out->label, 256);
    }

    switch (out->type) {
    case kLineSpinner:
    case kLineSpinnerAndButton: {
        void* spinner = ReadPtrRaw(item, kMechaSpinnerOffset);
        if (spinner)
            CopyFStringRaw(reinterpret_cast<const uint8_t*>(spinner) + kSpinnerValueOffset,
                out->value, 256);
        break;
    }
    case kLineDropdown: {
        void* dropdown = ReadPtrRaw(item, kMechaDropdownOffset);
        if (dropdown)
            ReadDropdownSelectionRaw(dropdown, out->value, 256);
        break;
    }
    case kLineSlider: {
        void* slider = ReadPtrRaw(item, kMechaSliderOffset);
        if (slider)
            out->percent = static_cast<int>(ReadF32Raw(slider, kSliderPercentOffset));
        break;
    }
    case kLineCheckbox:
        break; // state already read above, from the widget rather than the type tag
    case kLineButton: {
        void* button = ReadPtrRaw(item, kMechaButtonOffset);
        if (button)
            CopyFStringRaw(reinterpret_cast<const uint8_t*>(button) + kButtonTextOffset,
                out->value, 256);
        break;
    }
    case kLineValue: {
        void* value = ReadPtrRaw(item, kMechaValueOffset);
        if (value) {
            if (CopyFStringRaw(reinterpret_cast<const uint8_t*>(value) + kScrollHtmlOffset,
                    out->value, 256) <= 0)
                CopyFStringRaw(reinterpret_cast<const uint8_t*>(value) + kScrollTextOffset,
                    out->value, 256);
        }
        break;
    }
    case kLineColorChip: {
        // The swatch keeps its colour on the UIBGBox (SetColor → BGColor). UpdateDataColorChip
        // HIDES the box for "0x00000000" — the weapon-colour default, drawn as no swatch at all —
        // so a hidden chip reads as no colour, exactly as it looks.
        void* chip = ReadPtrRaw(item, kMechaColorChipOffset);
        if (chip && (ReadU32Raw(chip, kPanelFlagsOffset) & kPanelVisible))
            CopyFStringRaw(reinterpret_cast<const uint8_t*>(chip) + kBGBoxColorOffset, out->value,
                256);
        break;
    }
    default:
        break;
    }
    return out->label[0] != L'\0';
}

OptionWidget WidgetOf(uint8_t type)
{
    switch (type) {
    case kLineSpinner:
    case kLineSpinnerAndButton: return OptionWidget::Spinner;
    case kLineDropdown: return OptionWidget::Dropdown;
    case kLineSlider: return OptionWidget::Slider;
    case kLineCheckbox: return OptionWidget::Checkbox;
    case kLineButton: return OptionWidget::Button;
    case kLineValue: return OptionWidget::Value;
    case kLineColorChip: return OptionWidget::ColorChip;
    default: return OptionWidget::Description;
    }
}

} // namespace

// One UIMechaListItem as an OptionRow. `index` is the row's position in its owning UIList (the
// SetSelectedIndex target), or -1 for rows parented to a plain panel. Returns false for a row
// that is hidden or has no label — such a row must not become a node.
bool MakeRow(void* item, int index, OptionRow* out)
{
    RawOptionRow raw;
    if (!ReadOptionRowRaw(item, &raw))
        return false;
    out->handle = item;
    out->index = index;
    out->widget = WidgetOf(raw.type);
    out->label = CleanText(WcharToUtf8(raw.label, static_cast<int>(wcslen(raw.label))));
    out->value = CleanText(WcharToUtf8(raw.value, static_cast<int>(wcslen(raw.value))));
    out->percent = raw.percent;
    out->checked = raw.checked;
    out->disabled = raw.disabled;
    out->focused = (ReadU32Raw(item, kPanelFlagsOffset) & kPanelFocused) != 0;
    return !out->label.empty();
}

// Every visible row of a UIList, indices preserved.
std::vector<OptionRow> ReadListRows(void* list)
{
    std::vector<OptionRow> rows;
    void* items[kMaxListRows] = {};
    int count = TakeListItemsRaw("Options: list rows", list, items, kMaxListRows);
    for (int i = 0; i < count; i++) {
        OptionRow row;
        if (MakeRow(items[i], i, &row))
            rows.push_back(std::move(row));
    }
    return rows;
}

// The same read sized from the list itself, for a list whose length the GAME decides and
// nothing on screen bounds: a character pool holds as many soldiers as the player has made, and
// a customization picker as many helmets as the DLC and mods installed. GetItem(i) IS
// ItemContainer.ChildPanels[i], so the container's array is the list.
std::vector<OptionRow> ReadListRowsAll(const char* what, void* list)
{
    std::vector<OptionRow> rows;
    if (!list)
        return rows;
    void* container = ReadPtrRaw(list, kListItemContainerOffset);
    if (!container)
        return rows;
    const std::vector<void*> items = ReadPtrArrayAllRaw(what, container, kPanelChildPanelsOffset);
    for (size_t i = 0; i < items.size(); i++) {
        OptionRow row;
        if (MakeRow(items[i], static_cast<int>(i), &row))
            rows.push_back(std::move(row));
    }
    return rows;
}

// A visible UIButton at `offset` on `screen` as a MenuItem; handle stays null when the button
// is absent, hidden, or unlabelled.
MenuItem ReadButtonField(void* screen, int32_t offset)
{
    MenuItem item;
    void* btn = ReadPtrRaw(screen, offset);
    if (!btn)
        return item;
    uint32_t flags = ReadU32Raw(btn, kPanelFlagsOffset);
    if (!(flags & kPanelVisible))
        return item;
    item.label = CleanText(ReadFStringField(btn, kButtonTextOffset));
    if (item.label.empty())
        return item;
    item.handle = btn;
    item.focused = (flags & kPanelFocused) != 0;
    item.disabled = (ReadU32Raw(btn, kButtonFlagsOffset) & kButtonDisabled) != 0;
    return item;
}

// UIList.SetSelectedIndex — the game's own row selection (fires the list's selection
// delegates, scrolls the view). -1 deselects.
bool SetListIndex(void* list, int index)
{
    if (!list)
        return false;
    struct {
        int32_t Index;
        uint32_t bForce;
        void* ReturnValue; // the function returns self
    } params{index, 0, nullptr};
    return CallNamedFunction(list, "SetSelectedIndex", &params);
}

// ---- public infra ----

void* FindScriptFunction(const char* className, const char* funcName)
{
    // Same lazy class resolution + retry discipline as CallStaticNative; both caches hold
    // objects that persist for the session (class-owned, never GC'd mid-run).
    static std::unordered_map<std::string, UObject*> sClasses;
    static std::unordered_map<std::string, UFunction*> sFuncs;
    static uint64_t sLastTryMs = 0;

    UObject*& cls = sClasses[className];
    if (!cls) {
        uint64_t now = GetTickCount64();
        if (now - sLastTryMs < 1000)
            return nullptr;
        sLastTryMs = now;
        cls = FindClassRaw(className);
        if (!cls) {
            LOG_WARN("GameUI: class \"%s\" not found (FindScriptFunction)", className);
            return nullptr;
        }
    }

    UFunction*& fn = sFuncs[std::string(className) + "." + funcName];
    if (!fn) {
        fn = FindFunctionInClassStructRaw(cls, funcName);
        if (!fn) {
            LOG_WARN("GameUI: function \"%s.%s\" NOT FOUND", className, funcName);
            return nullptr;
        }
        LOG_INFO("GameUI: resolved \"%s.%s\" at 0x%p", className, funcName, fn);
    }
    return fn;
}

// A UnrealScript `state` is a UStruct in its own right, hanging off the class's field chain, and
// the functions declared inside it hang off ITS chain — one level deeper than FindScriptFunction
// looks. The distinction is not academic: XComTacticalInput.Mouse_CheckForPathing is class-scope
// (which is why the finder above resolves it) while Mouse_CheckForFreeAim is declared once per
// targeting state, so a class-chain walk returns nothing for it at all. Verified live
// 2026-08-20 — at class scope: NOT FOUND; under UsingTargetingMethod: found, flags 0x820102,
// Func = ProcessInternal, i.e. byte-identical to the function the mouse gate already flips.
//
// Note there is no super walk here: states are not inherited into the child's own Children
// chain by name in a way that would help us, and every caller so far wants one exact
// (class, state, function) triple. Cached per triple; class resolution shares FindClassRaw's
// once-a-second retry through the same path as FindScriptFunction.
void* FindStateFunction(const char* className, const char* stateName, const char* funcName)
{
    static std::unordered_map<std::string, UObject*> sClasses;
    static std::unordered_map<std::string, UFunction*> sFuncs;
    static uint64_t sLastTryMs = 0;

    UObject*& cls = sClasses[className];
    if (!cls) {
        uint64_t now = GetTickCount64();
        if (now - sLastTryMs < 1000)
            return nullptr;
        sLastTryMs = now;
        cls = FindClassRaw(className);
        if (!cls) {
            LOG_WARN("GameUI: class \"%s\" not found (FindStateFunction)", className);
            return nullptr;
        }
    }

    std::string key
        = std::string(className) + "." + stateName + "." + funcName;
    UFunction*& fn = sFuncs[key];
    if (!fn) {
        UField* state = FindFieldInStructRaw(cls, stateName, "State");
        if (!state) {
            LOG_WARN("GameUI: state \"%s.%s\" NOT FOUND", className, stateName);
            return nullptr;
        }
        UField* found = FindFieldInStructRaw(state, funcName, "Function");
        if (!found) {
            LOG_WARN("GameUI: function \"%s\" NOT FOUND in state \"%s.%s\"", funcName,
                className, stateName);
            return nullptr;
        }
        fn = reinterpret_cast<UFunction*>(found);
        LOG_INFO("GameUI: resolved \"%s\" at 0x%p", key.c_str(), fn);
    }
    return fn;
}

bool IsScreenFocused(void* screen)
{
    if (!screen)
        return false;
    return IsFocusedRaw(screen); // see there for why bIsVisible is not part of this
}

bool IsScreenKindOf(void* screen, const char* baseClass)
{
    if (!screen || !baseClass || !baseClass[0])
        return false;
    // The pointer-compare form: the base class object is resolved once and the chain is then
    // walked as pointers, which is what makes this affordable on the tick.
    return ObjectClassNamedRaw(screen, baseClass, ClassMatch::Derived);
}

uint32_t ScreenStateBits(void* screen)
{
    return screen ? ReadU32Raw(screen, kPanelFlagsOffset) : 0;
}

bool ScreenShowsDuringCinematic(void* screen)
{
    if (!screen)
        return false;
    return (ReadU32Raw(screen, kScreenFlagsOffset) & kScreenShowDuringCinematic) != 0;
}

void HintScreenChange()
{
    gScanEpoch++;
}

namespace {

// The Engine singleton, through its own `native static final function Engine GetEngine()`. Not
// cached: it is one ProcessEvent on a static native, and the GC rule says a UObject pointer held
// across frames must be revalidated anyway — asking again IS the revalidation.
void* FindEngine()
{
    struct {
        void* ReturnValue;
    } params{};
    if (!CallStaticNative("Engine", "GetEngine", &params))
        return nullptr;
    return params.ReturnValue;
}

} // namespace

bool SubtitlesEnabled()
{
    void* engine = FindEngine();
    if (!engine)
        return true; // unknown — err toward speaking rather than toward silent omission
    return (ReadU32Raw(engine, kEngineFlagsOffset) & kEngineSubtitlesEnabled) != 0;
}

std::string LocalizeString(const char* section, const char* key, const char* package)
{
    if (!section || !key || !package || !key[0])
        return {};

    // Object.Localize(string SectionName, string KeyName, string PackageName) : string — the
    // game's OWN localization lookup, so the answer comes back in the player's language with no
    // table of ours to fall out of date. Layout from the SDK dump comments, PACK(4): three
    // FStrings then the return, 0x10 apart.
#pragma pack(push, 4)
    struct LocalizeParams {
        wchar_t* SectionData; // FString SectionName  @0x00
        int32_t SectionCount;
        int32_t SectionMax;
        wchar_t* KeyData; // FString KeyName          @0x10
        int32_t KeyCount;
        int32_t KeyMax;
        wchar_t* PackageData; // FString PackageName  @0x20
        int32_t PackageCount;
        int32_t PackageMax;
        wchar_t* RetData; // FString ReturnValue      @0x30
        int32_t RetCount;
        int32_t RetMax;
    };
#pragma pack(pop)
    static_assert(sizeof(LocalizeParams) == 0x40, "Object.Localize params must be 0x40 bytes");
    static_assert(offsetof(LocalizeParams, KeyData) == 0x10, "Localize KeyName at 0x10");
    static_assert(offsetof(LocalizeParams, PackageData) == 0x20, "Localize PackageName at 0x20");
    static_assert(offsetof(LocalizeParams, RetData) == 0x30, "Localize ReturnValue at 0x30");

    // Ours to keep alive across the call and nobody else's to free — ProcessEvent shallow-copies
    // the block and its destroy pass skips everything below ParmsSize (see CLAUDE.md).
    std::wstring wSection = Utf8ToWide(section);
    std::wstring wKey = Utf8ToWide(key);
    std::wstring wPackage = Utf8ToWide(package);

    LocalizeParams params{};
    params.SectionData = wSection.data();
    params.SectionCount = static_cast<int32_t>(wSection.size()) + 1; // FString counts the NUL
    params.SectionMax = params.SectionCount;
    params.KeyData = wKey.data();
    params.KeyCount = static_cast<int32_t>(wKey.size()) + 1;
    params.KeyMax = params.KeyCount;
    params.PackageData = wPackage.data();
    params.PackageCount = static_cast<int32_t>(wPackage.size()) + 1;
    params.PackageMax = params.PackageCount;

    if (!CallStaticNative("Object", "Localize", &params))
        return {};

    // ⛔ Sized from the DATA. A localized line is whatever the translator wrote, so this is
    // exactly the class of read the fixed-buffer hard rule is about — the count comes first and
    // the buffer is built to fit.
    const int32_t count = params.RetCount;
    if (!params.RetData || count <= 1)
        return {}; // no such key — the caller decides what that means
    if (count > kMaxPlausibleLocalizedChars) {
        LOG_WARN("GameUI: refusing implausible Localize length %d for %s.%s", count, package, key);
        return {};
    }
    std::vector<wchar_t> buf(static_cast<size_t>(count));
    const int len = CopyFStringRaw(&params.RetData, buf.data(), count);
    if (len <= 0)
        return {};
    // Engine-allocated and deliberately leaked, the same trade ConsoleCommand documents: freeing
    // it needs appFree, and this runs once per subtitle CUE rather than per frame.
    return WcharToUtf8(buf.data(), len);
}

bool MoviePlaying()
{
    // XComEngine.IsAnyMoviePlaying — the same question the game's own rulesets ask before doing
    // anything UI-ish, and the only one that sees a full-screen movie. A movie is NOT a UIScreen:
    // it plays over the stack, so every UI-side flag (input gate, ScreensVisible, cinematic mode)
    // stays exactly as it was and the screen underneath keeps reporting itself inited, visible,
    // focused and topmost. UIShellDifficulty starts this very movie itself via PlayLoadMapMovie.
    struct {
        uint32_t ReturnValue;
    } params{0};
    if (!CallStaticNative("XComEngine", "IsAnyMoviePlaying", &params))
        return false;
    return params.ReturnValue != 0;
}

StackState ReadStackState()
{
    StackState st;
    void* stack = gStackFinder.Find();
    if (!stack)
        return st;
    uint32_t flags = ReadU32Raw(stack, kStackFlagsOffset);
    st.valid = true;
    st.inputBlocked = (flags & kStackInputBlocked) != 0;
    st.screensHidden = (flags & kStackScreensVisible) == 0;
    st.cinematic = (flags & kStackCinematic) != 0;
    return st;
}

bool FocusButton(void* button)
{
    uint8_t params[16] = {}; // no parameters
    return CallNamedFunction(button, "SetSelectedNavigation", params);
}

bool PlayNavSound()
{
    void* mgr = FindSoundManager();
    if (!mgr)
        return false;
    // UE3 FString parameter: [wchar_t* data][count][max], count includes the terminator.
    // The callee only reads the key, and ProcessEvent destroys callee LOCALS only — parms
    // stay caller-owned — so pointing at our static buffer is the standard outbound pattern.
    static const wchar_t kKey[] = L"Play_Mouseover";
    constexpr int32_t kKeyCount = static_cast<int32_t>(sizeof(kKey) / sizeof(wchar_t));
    struct {
        const wchar_t* Data;
        int32_t Num;
        int32_t Max;
    } params{kKey, kKeyCount, kKeyCount};
    return CallNamedFunction(mgr, "PlaySoundEvent", &params);
}

bool ClickButton(void* button)
{
    struct {
        uint32_t ReturnValue;
    } params{};
    return CallNamedFunction(button, "Click", &params);
}

bool SendRowCommand(void* rowItem, int cmd, bool* handled)
{
    // Direction commands act on the press phase, everything else on release (see the
    // kActionPress/kActionRelease comment).
    bool isDirection = cmd == kCmdArrowLeft || cmd == kCmdArrowRight;
    struct {
        int32_t Cmd;
        int32_t Arg;
        uint32_t ReturnValue;
    } params{cmd, isDirection ? kActionPress : kActionRelease, 0};
    const bool called = CallNamedFunction(rowItem, "OnUnrealCommand", &params);
    if (handled)
        *handled = called && (params.ReturnValue & 1) != 0;
    return called;
}

bool SendScreenCommand(void* screen, int cmd)
{
    // A UIScreen's OnUnrealCommand has the row signature, so the row helper is the whole
    // implementation. Kept as its own name because the INTENT differs: for the popup family
    // the screen's own accept path is the only route there is — UIAlert's buttons carry no
    // retained label, no navigable focus and no reachable delegate of their own.
    return SendRowCommand(screen, cmd);
}

bool InvokeScreenVerb(void* screen, const char* funcName)
{
    if (!screen || !funcName)
        return false;
    uint8_t noParams[16] = {};
    return CallNamedFunction(screen, funcName, noParams);
}

// ---- tooltips ----

namespace {

// Title and body are two spoken phrases, so they take the kernel's join rule rather than a bare
// ". " — the game punctuates plenty of these itself ("НАГРАДА:" is a tooltip title) and a blind
// concatenation voiced the doubled stop at the player.
std::string JoinTooltip(const std::string& title, const std::string& body)
{
    std::string out = title;
    X2Access::Graph::AppendPhrase(out, body);
    return out;
}

// The tooltip manager that owns `panel`'s movie. Never a global scan: two managers can be alive
// at once (2D HUD + 3D strategy movie) with independently-counted ids.
void* TooltipMgrFor(void* panel)
{
    void* movie = panel ? ReadPtrRaw(panel, kPanelMovieOffset) : nullptr;
    void* pres = movie ? ReadPtrRaw(movie, kMoviePresOffset) : nullptr;
    return pres ? ReadPtrRaw(pres, kPresTooltipMgrOffset) : nullptr;
}

} // namespace

std::string ReadPanelTooltip(void* panel)
{
    if (!panel || !(ReadU32Raw(panel, kPanelFlagsOffset) & kPanelHasTooltip))
        return {};
    int32_t id = ReadI32Raw(panel, kPanelTooltipIdOffset);

    void* mgr = TooltipMgrFor(panel);
    if (!mgr)
        return {};

    // SetTooltipText path: a data element in the manager's shared TextTooltip.
    if (const uint8_t* el = FindTextTooltipDataRaw(mgr, id))
        return JoinTooltip(
            ReadFStringField(el, kTTDataTitleOffset), ReadFStringField(el, kTTDataBodyOffset));

    // Preformed path: a real UITextTooltip object in the Tooltips array. Only text tooltips
    // carry sTitle/sBody at these offsets; other subclasses (ability tooltips) would read
    // garbage, so the class check gates the read. Derived rather than exact because the check is
    // asking "does this object HAVE those two fields", which every descendant of the class that
    // declares them does — UITextTooltip has no vanilla descendants, so there is nothing else a
    // tolerant compare can reach.
    void* tip = FindTooltipByIdRaw(mgr, id);
    if (!tip || !ObjectClassNamedRaw(tip, "UITextTooltip", ClassMatch::Derived))
        return {};

    return JoinTooltip(ReadFStringField(tip, kTextTooltipTitleOffset),
        ReadFStringField(tip, kTextTooltipBodyOffset));
}

std::string ReadRowTooltip(void* rowItem)
{
    if (!rowItem)
        return {};
    // Row tooltips hang off the BG child panel (SetTooltipText targets it — mouse events
    // originate there); fall back to the row panel itself.
    std::string tip = ReadPanelTooltip(ReadPtrRaw(rowItem, kMechaBGOffset));
    if (tip.empty())
        tip = ReadPanelTooltip(rowItem);
    return tip;
}

std::string ReadListItemTooltip(void* rowItem)
{
    if (!rowItem)
        return {};
    // Same shape as ReadRowTooltip but a different child: UIListItemString forwards
    // SetTooltipText — and the why-disabled reason SetDisabled(bDisabled, Reason) parks there —
    // to ButtonBG. The fallback to the row itself covers a subclass that set one on itself.
    std::string tip = ReadPanelTooltip(ReadPtrRaw(rowItem, kListItemBGOffset));
    if (tip.empty())
        tip = ReadPanelTooltip(rowItem);
    return tip;
}

std::string ReadTooltipByPath(void* panel, const char* childPath)
{
    if (!panel || !childPath)
        return {};
    void* mgr = TooltipMgrFor(panel);
    if (!mgr)
        return {};

    // The panel's own path into the movie, which the game composed as ONE FName
    // (`name(ParentPanel.MCPath $ "." $ MCName)`).
    char base[256] = "";
    if (!FNameAtAnsiRaw(reinterpret_cast<const uint8_t*>(panel) + kPanelMCPathOffset, base,
            sizeof(base)))
        return {};
    if (!base[0] || strcmp(base, "None") == 0)
        return {}; // not inited, or a pure UnrealScript container — it has no path to match

    const uint8_t* el = FindTextTooltipDataByPathRaw(mgr, std::string(base) + childPath);
    if (!el)
        return {};
    return JoinTooltip(
        ReadFStringField(el, kTTDataTitleOffset), ReadFStringField(el, kTTDataBodyOffset));
}

std::string ReadCallbackTooltip(void* panel)
{
    if (!panel || !(ReadU32Raw(panel, kPanelFlagsOffset) & kPanelHasTooltip))
        return {};
    const int32_t id = ReadI32Raw(panel, kPanelTooltipIdOffset);
    void* mgr = TooltipMgrFor(panel);
    if (!mgr)
        return {};

    // Only the SetTooltipText family carries a per-element callback; a preformed tooltip has
    // none and the plain read is the whole answer.
    const uint8_t* el = FindTextTooltipDataRaw(mgr, id);
    if (!el)
        return ReadPanelTooltip(panel);
    void* target = ReadPtrRaw(el, kTTDataMouseInOffset + kDelegateObjectOffset);
    char fn[128] = "";
    if (!target
        || !FNameAtAnsiRaw(el + kTTDataMouseInOffset + kDelegateFunctionOffset, fn, sizeof(fn))
        || !fn[0] || strcmp(fn, "None") == 0)
        return ReadPanelTooltip(panel); // no callback registered — the text is already final

    // The game's own hover sequence, minus the showing. UITooltipMgr.OnMouse matches the shared
    // UITextTooltip by path, which lands in MatchesID/SetDataIndex — that is what points
    // DataIndex at THIS element and copies its delegate onto the object — and then fires
    // del_OnMouseIn(Tooltip). The callback's SetText writes TextTooltipData[DataIndex].sBody,
    // so selecting the element FIRST is load-bearing: without it the refreshed words would land
    // in whichever element the last hover left selected. The manager's own ActivateTooltip and
    // the UpdateData Flash push are deliberately not called — nothing appears on screen.
    void* shared = ReadPtrRaw(mgr, kMgrTextTooltipOffset);
    if (!shared)
        return ReadPanelTooltip(panel);
    struct {
        int32_t NewID;
        uint32_t ReturnValue;
    } select{id, 0};
    if (!CallNamedFunction(shared, "MatchesID", &select))
        return ReadPanelTooltip(panel);
    struct {
        void* Tooltip;
    } arg{shared};
    if (!CallNamedFunction(target, fn, &arg))
        LOG_WARN("ReadCallbackTooltip: %s could not be called for tooltip %d", fn, id);
    return ReadPanelTooltip(panel);
}

bool AdjustDropdown(void* rowItem, int delta)
{
    // The row's own OnUnrealCommand refuses input while disabled; calling into the widget
    // directly must honor the same gate.
    if (ReadU32Raw(rowItem, kMechaFlagsOffset) & kMechaDisabled)
        return false;
    void* dropdown = ReadPtrRaw(rowItem, kMechaDropdownOffset);
    if (!dropdown)
        return false;
    // The widget's own interaction, minus the modal wait: Open() marks the revert point,
    // Prev()/Next() move the highlighted selection (Prev INCREMENTS — the Flash column lists
    // upward), and Enter through the row confirms: Close + SetSelected + the screen's
    // selection-changed delegate, exactly as a sighted keyboard user lands it.
    struct {
        void* ReturnValue; // returns self
    } open{};
    if (!CallNamedFunction(dropdown, "Open", &open))
        return false;
    uint8_t none[16] = {};
    if (!CallNamedFunction(dropdown, delta > 0 ? "Prev" : "Next", none))
        return false;
    return SendRowCommand(rowItem, kCmdEnter);
}

// ---- shared screen lookup ----

namespace {

const char* const kTrackedClassNames[kTrackedScreenCount] = {
    "UIShellDifficulty",
    "UIChooseIronMan",
    "UIShellNarrativeContent",
    "UIShellSoundtrackPicker",
    "UIDialogueBox",
    "UIPauseMenu",
    "UILoadScreenAnimation",
    "UITacticalHUD",
    "UITutorialBox",
    "UIArmory_Loadout",
    "UISquadSelect",
    "UIAfterAction",
    "UILoadGame",
    "UISaveGame",
    "UIInputDialogue",
};

// Per-screen matching policy, positional against the table above. Per screen and not per table
// because these fifteen do not want the same answer, and three of them want the opposite of
// their neighbours. A short initializer leaves Exact, which is today's behaviour — so a forgotten
// entry is a screen that stays strict, never a screen that starts claiming things.
constexpr ClassMatch kTrackedMatch[kTrackedScreenCount] = {
    /*Difficulty*/ ClassMatch::Derived,
    /*IronMan*/ ClassMatch::Derived,
    /*Narrative*/ ClassMatch::Derived,
    /*Soundtrack*/ ClassMatch::Derived,
    /*Dialog*/ ClassMatch::Derived,
    // ⛔ EXACT: UIShellStrategy EXTENDS UIPauseMenu (UIShellStrategy.uc:1) and is a different
    // screen — the game itself tells them apart with IsA('UIShellStrategy') inside UIPauseMenu.
    // This table probes each entry independently, so it has no most-derived-first order to lean
    // on; tolerating here would hand the pause-menu recipe the developer strategy shell that
    // XComHQPresentationLayer pushes. A mod's UIPauseMenu override needs an ordered probe first.
    /*PauseMenu*/ ClassMatch::Exact,
    /*LoadScreen*/ ClassMatch::Derived,
    /*TacticalHUD*/ ClassMatch::Derived,
    /*TutorialBox*/ ClassMatch::Derived,
    // Deliberately accepted: the only vanilla descendant is UIArmory_Loadout_MP, which no recipe
    // owns, so tolerance adds a multiplayer screen nobody has exercised rather than stealing one.
    // It is the same widgets — refusing it would cost every mod loadout override instead.
    /*ArmoryLoadout*/ ClassMatch::Derived,
    // ⛔ EXACT, and this one is not about ambiguity: robojumper_UISquadSelect extends
    // UISquadSelect and NEVER populates the inherited m_kSlotList (it spawns its own
    // robojumper_UIList_SquadEditor). Finding it would make the recipe attach, TAKE THE ARROW
    // KEYS and narrate a squad with no soldiers in it — strictly worse than today's silent
    // non-attach, which at least leaves the keyboard to the game. Finding a screen is not the
    // same as reading it, and this entry is the rule's clearest case.
    /*SquadSelect*/ ClassMatch::Exact,
    // ⛔ EXACT for the same reason in its other shape: UIAfterAction_LW adds m_kSlotList2/3/4 for
    // a multi-row squad and ReadAfterAction reads only the inherited first list. Tolerating would
    // read six soldiers of twenty-four and say nothing about the rest — a dropped tail reads to a
    // blind player as "the screen has nothing more" and is therefore never reported as a bug.
    /*AfterAction*/ ClassMatch::Exact,
    /*LoadGame*/ ClassMatch::Derived,
    // In LOCKSTEP with SaveScreenIsSaving (game_ui_shell.cpp): that one boolean picks the title
    // offset, the busy flag and the whole semantics of the save/load recipe, so a tolerant finder
    // with an exact discriminator would read a mod's save screen as a LOAD screen — on the one
    // surface where the wrong-save-overwritten hazard is already documented.
    /*SaveGame*/ ClassMatch::Derived,
    /*TextInput*/ ClassMatch::Derived,
};

} // namespace

// The top entry of a dialog box's queue — m_arrData.Length - 1, which is where OnAccept /
// OnCancel and GetTopDialogBoxData look. Null when nothing is queued.
const uint8_t* TopDialogDataRaw(void* screen)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(screen) + kDialogDataOffset;
        auto* data = *reinterpret_cast<const uint8_t* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 64)
            return nullptr;
        return data + static_cast<size_t>(n - 1) * kDlgDataStride;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Is this box actually showing something? The box is PERMANENT — it exists from movie init
// and merely holds no data when idle — so this, not its presence, is the readiness test.
bool DialogIsShowingRaw(void* screen)
{
    // Same reason as IsFocusedRaw: the box's bIsVisible is false while it is on screen asking a
    // question. Its queue is the honest test — the game's own OnAccept/OnCancel read the same
    // top entry, so a box with something queued is a box the player is looking at.
    uint32_t flags = ReadU32Raw(screen, kPanelFlagsOffset);
    if (!(flags & kPanelInited) || (flags & kPanelRemoved))
        return false;
    return TopDialogDataRaw(screen) != nullptr;
}

namespace {

// Positional and one entry per tracked screen: a short initializer leaves a NULL in the table,
// and FindTrackedScreen calls this through the pointer without a guard, so the static_assert
// below is what keeps "added a screen, forgot its predicate" from being a crash on first scan.
// (IsShownRaw covers the loading overlay, a screen nobody focuses — it is shown over whatever
// is transitioning out, so being inited and visible is the whole test.)
constexpr bool (*kTrackedUsable[kTrackedScreenCount])(void*) = {
    /*Difficulty*/ IsFocusedRaw,
    /*IronMan*/ IsFocusedRaw,
    /*Narrative*/ IsFocusedRaw,
    /*Soundtrack*/ IsFocusedRaw,
    /*Dialog*/ DialogIsShowingRaw,
    /*PauseMenu*/ IsFocusedRaw,
    /*LoadScreen*/ IsShownRaw,
    /*TacticalHUD*/ IsFocusedRaw,
    /*TutorialBox*/ IsFocusedRaw,
    /*ArmoryLoadout*/ IsFocusedRaw,
    /*SquadSelect*/ IsFocusedRaw,
    /*AfterAction*/ IsFocusedRaw,
    /*LoadGame*/ IsFocusedRaw,
    /*SaveGame*/ IsFocusedRaw,
    /*TextInput*/ IsFocusedRaw,
};
static_assert(kTrackedUsable[kTrackedScreenCount - 1] != nullptr,
    "kTrackedUsable must have one predicate per TrackedScreen");

// Every transient screen behind ONE rate-limited GObjects pass. Independent ScreenFinders would
// each scan once a second whenever their screen is absent — which is almost always, since these
// exist only while starting or pausing a campaign. Cached instances still revalidate per call,
// so the scan runs only while a wanted screen is genuinely missing.
void* FindTrackedScreen(TrackedScreen which)
{
    static UObject* sClasses[kTrackedScreenCount] = {};
    static void* sCached[kTrackedScreenCount] = {};
    static uint64_t sLastScanMs = 0;
    static uint64_t sScanEpoch = 0;

    void* cached = sCached[which];
    bool live = cached
        && IsInstanceRaw(reinterpret_cast<UObject*>(cached), sClasses[which],
            kTrackedClassNames[which], kTrackedMatch[which]);
    if (live && kTrackedUsable[which](cached))
        return cached;
    if (!live)
        sCached[which] = cached = nullptr; // never hand out a GC'd pointer

    uint64_t now = GetTickCount64();
    if (sScanEpoch == gScanEpoch && now - sLastScanMs < 1000)
        return cached;
    sLastScanMs = now;
    sScanEpoch = gScanEpoch;

    // A class we never resolve is a screen we can never find, and that failure is otherwise
    // completely silent — so say it once, the first time each class comes back.
    for (int i = 0; i < kTrackedScreenCount; i++) {
        if (sClasses[i])
            continue;
        sClasses[i] = FindClassRaw(kTrackedClassNames[i]);
        if (sClasses[i])
            LOG_INFO("GameUI: %s class object at 0x%p", kTrackedClassNames[i], sClasses[i]);
    }

    // The scan itself takes the NEWEST instance of each class, unfiltered — that is exactly
    // what replaces a stale one after a world change.
    void* found[kTrackedScreenCount] = {};
    bool complete = ScanInstancesRaw(
        sClasses, kTrackedClassNames, kTrackedUsable, kTrackedMatch, found, kTrackedScreenCount);
    for (int i = 0; i < kTrackedScreenCount; i++) {
        if (found[i] != sCached[i] && found[i]) {
            LOG_INFO("GameUI: found %s instance at 0x%p", kTrackedClassNames[i], found[i]);
            if (kTrackedMatch[i] == ClassMatch::Derived)
                NoteOverrideRaw(kTrackedClassNames[i], found[i]);
        }
        // A partial walk proves nothing about what it did not reach. Clearing on one would
        // drop live screens for a second at exactly the moment they appear — during a world
        // change, which is when the walk is most likely to be cut short.
        if (found[i] || complete)
            sCached[i] = found[i];
    }
    if (!complete)
        LOG_WARN("GameUI: object scan cut short — keeping the screens it did not reach");
    return sCached[which];
}

} // namespace

// Same stack-first rule as PreferStack, for the screens behind the shared lookup.
void* FindScreen(TrackedScreen which)
{
    static bool sReported[kTrackedScreenCount] = {};
    // Both halves take the same per-screen policy, the PreferStack rule applied to the shared
    // lookup: a screen tolerated on the stack and refused by the scan would disappear exactly
    // during a world change, and UIDialogueBox — never pushed at all — would never be found.
    if (void* onStack = FindStackScreenRaw(kTrackedClassNames[which], kTrackedMatch[which]))
        return onStack;
    void* found = FindTrackedScreen(which);
    if (found && !sReported[which]) {
        sReported[which] = true;
        LOG_INFO("GameUI: %s is not on the screen stack — falling back to an object scan",
            kTrackedClassNames[which]);
    }
    return found;
}

std::string DescribeTrackedScreens()
{
    std::string out;
    for (int i = 0; i < kTrackedScreenCount; i++) {
        void* screen = FindScreen(static_cast<TrackedScreen>(i));
        char line[128];
        snprintf(line, sizeof(line), "%s%s %p/0x%X", out.empty() ? "" : ", ",
            kTrackedClassNames[i], screen, ScreenStateBits(screen));
        out += line;
    }
    return out;
}

std::string DescribeScreenStack()
{
    if (!gStackFinder.Find())
        return "  (no live UIScreenStack)\n";

    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    if (count <= 0)
        return "  (stack is empty)\n";

    std::string out;
    for (int i = 0; i < count; i++) {
        char line[192];
        if (!screens[i]) {
            snprintf(line, sizeof(line), "  %2d (null)\n", i);
            out += line;
            continue;
        }
        char name[64] = "?";
        ObjClassNameAnsiRaw(screens[i], name, sizeof(name));
        uint32_t flags = ReadU32Raw(screens[i], kPanelFlagsOffset);
        // Spelled out rather than left as a hex dword: bIsVisible is the bit that looks like it
        // means "on screen" and does not (a live pause menu reads 0x259, no visible bit), so a
        // dump that only prints 0x259 invites exactly the wrong conclusion.
        snprintf(line, sizeof(line), "  %2d %-28s %p 0x%03X %s%s%s%s\n", i, name, screens[i],
            flags,
            (flags & kPanelInited) ? "inited " : "",
            (flags & kPanelFocused) ? "focused " : "",
            (flags & kPanelVisible) ? "visible " : "",
            (flags & kPanelRemoved) ? "REMOVED " : "");
        out += line;
    }
    return out;
}

// ---- GFxMoviePlayer path-API calls ----

// The parameter block EVERY GFxMoviePlayer path function takes: an FString in at 0x00, the
// answer at 0x10 (pinned in sdk_layout_checks.cpp against the generated param structs). One
// shape covers ActionScriptString / ActionScriptInt / GetVariableString / GetVariableNumber,
// which is what makes the whole Scaleform surface reachable through one helper.
//
// The path buffer is OURS and must outlive the call — and only the call: parameters are
// shallow-memcpy'd into the engine's frame and its destroy pass skips everything below
// ParmsSize, so the engine never frees it and never keeps it.
bool FlashCallRaw(void* movie, const char* funcName, const std::string& path, uint8_t* block,
    size_t blockSize)
{
    if (!movie || blockSize < 32)
        return false;
    memset(block, 0, blockSize);
    static std::wstring sPath; // one live buffer at a time; the call is synchronous
    sPath = Utf8ToWide(path);
    const int32_t num = static_cast<int32_t>(sPath.size()) + 1; // FString Num counts the null
    *reinterpret_cast<const wchar_t**>(block) = sPath.c_str();
    *reinterpret_cast<int32_t*>(block + 8) = num;
    *reinterpret_cast<int32_t*>(block + 12) = num;
    return CallNamedFunction(movie, funcName, block);
}

// Invoke or read a path that answers with a string. Empty on any failure — including AS2's own
// "null", which is what Selection.getFocus() says when no field has focus.
std::wstring FlashString(void* movie, const char* funcName, const std::string& path)
{
    uint8_t block[64] = {};
    if (!FlashCallRaw(movie, funcName, path, block, sizeof(block)))
        return std::wstring();
    // Read back through the SEH-guarded FString primitive rather than dereferencing by hand; the
    // buffer is engine-allocated and deliberately leaked (freeing it would need appFree).
    wchar_t buf[512];
    int len = CopyFStringRaw(block + kFlashRetOffset, buf, 512);
    return len > 0 ? std::wstring(buf, static_cast<size_t>(len)) : std::wstring();
}

// ... and one that answers with an int. `fallback` is returned on a failed call; note that a
// path that does not resolve answers 0, while AS2's Selection accessors answer -1 when nothing
// is focused — which is how the two were told apart when this was first probed.
int FlashInt(void* movie, const char* funcName, const std::string& path, int fallback)
{
    uint8_t block[64] = {};
    if (!FlashCallRaw(movie, funcName, path, block, sizeof(block)))
        return fallback;
    return *reinterpret_cast<const int32_t*>(block + kFlashRetOffset);
}

// ---- outbound calls into companion-mod script ----

// The __try body lives in its own function with no C++ objects (C2712). One full pass, oldest
// first — a CDO is created once at package load, so there is exactly one and age is moot.
void* ScanForObjectNamedRaw(const char* objName)
{
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        int32_t count = objects ? objects->size() : 0;
        char name[96];
        for (int32_t i = 0; i < count; i++) {
            UObject* obj = objects->at(i);
            if (!obj)
                continue;
            if (ObjNameAnsiRaw(obj, name, sizeof(name)) && strcmp(name, objName) == 0)
                return obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

namespace {

// SEH-isolated revalidation of a cached CDO pointer (name re-read).
bool ObjectStillNamedRaw(void* obj, const char* objName)
{
    char name[96];
    return ObjNameAnsiRaw(reinterpret_cast<UObject*>(obj), name, sizeof(name))
        && strcmp(name, objName) == 0;
}

} // namespace

namespace {

// The companion's whole script package, by the object that IS it: the UPackage named after the
// mod, class "Package" so a stray object sharing the name cannot answer for it. Every companion
// class lives in this one package, so its absence answers for all of them at once.
bool CompanionPackagePresentRaw()
{
    void* pkg = ScanForObjectNamedRaw("X2AccessCompanion");
    if (!pkg)
        return false;
    char cls[64];
    return ObjClassNameAnsiRaw(pkg, cls, sizeof(cls)) && strcmp(cls, "Package") == 0;
}

// How long a "package not loaded" answer stands before it is re-checked. The retry exists so a
// wrong answer cannot stick forever, not because the package is expected to appear.
constexpr uint64_t kCompanionAbsentRetryMs = 30000;
uint64_t sCompanionAbsentUntilMs = 0;
uint64_t sCompanionNotYetLoggedUntilMs = 0;

// ⛔ An absence is only an ANSWER once the package has had its chance to load, and that is
// later than it looks: DLC enumeration logs its "Checking DLC installation" lines four seconds
// in, but the companion's script package is not in GObjects at ten seconds, and IS by the time
// the main menu attaches. The first live run of the negative cache proved it the expensive way:
// a healthy install was declared "NOT LOADED" during loading, and every companion lookup then
// answered null for 30 s — until twenty seconds AFTER the main menu, with the load-menu save
// names silent the whole while. So nothing is cached as absent before the settle moment, which
// is `CompanionPackageLoaded()` — the navigator asks it once, at the first main-menu attach.
bool sCompanionSettled = false;

} // namespace

bool CompanionPackageLoaded()
{
    sCompanionSettled = true;
    return CompanionPackagePresentRaw();
}

void* FindCompanionCDO(const char* className)
{
    static std::unordered_map<std::string, void*> sCache;
    static uint64_t sLastScanMs = 0;

    std::string expected = std::string("Default__") + className;

    auto it = sCache.find(expected);
    if (it != sCache.end() && it->second) {
        if (ObjectStillNamedRaw(it->second, expected.c_str()))
            return it->second;
        it->second = nullptr; // never hand out a pointer whose name no longer matches
    }

    uint64_t now = GetTickCount64();
    // The package is known absent: no scan and no log until the retry comes round. Before this
    // gate existed a session without the companion paid a full GObjects scan and a WARN line
    // once a second for as long as any surface polled — the tester's first log was one warning
    // per second for two minutes, and the one line that mattered was buried in them.
    if (now < sCompanionAbsentUntilMs)
        return nullptr;
    if (now - sLastScanMs < 1000)
        return nullptr;
    sLastScanMs = now;

    void* found = ScanForObjectNamedRaw(expected.c_str());
    if (found) {
        LOG_INFO("GameUI: companion CDO %s at 0x%p", expected.c_str(), found);
        sCache[expected] = found;
        return found;
    }
    // Before the settle moment a miss means nothing yet — the package loads with the shell —
    // so keep scanning once a second, say so at most once per interval, and cache nothing.
    if (!sCompanionSettled) {
        if (now >= sCompanionNotYetLoggedUntilMs) {
            sCompanionNotYetLoggedUntilMs = now + kCompanionAbsentRetryMs;
            LOG_INFO("GameUI: companion CDO %s not found yet — mod packages load with the "
                     "shell, so this is expected before the main menu",
                expected.c_str());
        }
        return nullptr;
    }
    // After it, a miss is one of two things, and the log has to say which: the whole package
    // is not loaded (an install problem — ActiveMods, ModRootDirs, the Mods folder), or the
    // package is there and this one class is not (a companion build older than the ASI). One
    // more scan tells them apart; the first answer is cached for the retry interval so it is
    // said ONCE.
    if (!CompanionPackagePresentRaw()) {
        sCompanionAbsentUntilMs = now + kCompanionAbsentRetryMs;
        LOG_WARN("GameUI: companion mod NOT LOADED — the X2AccessCompanion package is not in "
                 "GObjects, so %s and every other companion class is missing (first asked for by "
                 "%s); next check in %llu s",
            expected.c_str(), className,
            static_cast<unsigned long long>(kCompanionAbsentRetryMs / 1000));
    } else {
        LOG_WARN("GameUI: companion CDO %s NOT FOUND although the companion package is loaded "
                 "(companion build older than the ASI?)",
            expected.c_str());
    }
    return nullptr;
}

bool CallScriptFunction(void* obj, const char* funcName, void* params)
{
    return CallNamedFunction(obj, funcName, params);
}

bool CallCompanion(const char* className, const char* funcName, void* params,
    std::string* result)
{
    void* cdo = FindCompanionCDO(className);
    if (!cdo)
        return false; // FindCompanionCDO logs the miss (companion mod not loaded)

    // A no-parameter function still gets a zeroed block — ProcessEvent copies ParmsSize bytes.
    int64_t zeros[2] = {};
    if (!CallScriptFunction(cdo, funcName, params ? params : zeros)) {
        LOG_WARN("Companion: %s.%s failed", className, funcName);
        return false;
    }
    if (result)
        *result = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "NativeResult");
    return true;
}

std::string CleanText(const std::string& text)
{
    // Strip <...> markup tags, collapse runs of whitespace, trim.
    //
    // ⛔ A LINE BREAK TAG IS WHITESPACE, not nothing. The game writes "<br \>" (and "<br/>",
    // "<br>") between lines it means to keep apart — a covert-action slot's tooltip is the
    // soldier's name, a break, then the reward — and stripping it outright fused the two into
    // one word. Every other tag is presentation (<font>, <img>, <p>) and still vanishes.
    std::string out;
    out.reserve(text.size());
    bool inTag = false;
    bool pendingSpace = false;
    char tag[3] = {}; // the tag's first letters, lower-cased, enough to recognise "br"
    size_t tagLen = 0;
    for (char c : text) {
        if (c == '<') {
            inTag = true;
            tagLen = 0;
            continue;
        }
        if (inTag) {
            if (c == '>') {
                inTag = false;
                const bool isBreak = tagLen >= 2 && tag[0] == 'b' && tag[1] == 'r'
                    && (tagLen == 2 || tag[2] == ' ' || tag[2] == '/');
                if (isBreak)
                    pendingSpace = !out.empty();
            } else if (tagLen < sizeof(tag)) {
                tag[tagLen++] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        out += c;
    }
    return out;
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
