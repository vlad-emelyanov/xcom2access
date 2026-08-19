#include "game_ui.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Core reflection types only (UObject, UClass, FName, TArray) — the same proven set the other
// game-facing TUs use. Deliberately NOT the full generated SDK: its class headers are emitted
// under pack(0x8) and their COMPILED layouts drift from the engine's real ones, so all XComGame
// field access below goes through explicit offsets taken from the generator's reflection-dump
// comments and verified against a live process (see kOffsets below).
#include "GameDefines.hpp"
#include "SdkConstants.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

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
// against a running WOTC shell, 2026-08-18): flags dword read 0x279/0xA7B with expected bits;
// MAINMENU held 6 buttons; Text read the localized "НОВАЯ ИГРА". The shell-extras set (ticker,
// My2K, tooltip, attention mask) comes from the same dump; every read through it is SEH-guarded
// and gated on the target's panel-visible bit.
constexpr int32_t kPanelFlagsOffset = 0x284;    // UIPanel flag dword
constexpr uint32_t kPanelInited = 0x10;
constexpr uint32_t kPanelVisible = 0x20;
constexpr uint32_t kPanelFocused = 0x40;
constexpr int32_t kButtonTextOffset = 0x3D8;    // UIButton::Text (FString)
constexpr int32_t kButtonFlagsOffset = 0x400;   // UIButton flag dword
constexpr uint32_t kButtonDisabled = 0x01;      // IsDisabled
constexpr uint32_t kButtonAttention = 0x80;     // bNeedsAttention (the "!" badge)
constexpr int32_t kShellMainMenuOffset = 0x4CC; // UIShell::MAINMENU (TArray<UIButton*>)
constexpr int32_t kShellTickerOffset = 0x4F4;   // UIShell::TickerText (UIScrollingText*)
constexpr int32_t kShellMy2KOffset = 0x5C8;     // UIFinalShell::m_my2KButton (UIButton*)
constexpr int32_t kShellLiveClient = 0x608;     // UIFinalShell::FiraxisLiveClient
constexpr int32_t kScrollTextOffset = 0x3D8;    // UIScrollingText::Text (FString)
constexpr int32_t kScrollHtmlOffset = 0x3E8;    // UIScrollingText::HtmlText (FString)

// UIOptionsPCScreen (the settings screen) and its row/widget classes, same reflection-dump
// provenance. The tab-name strings are NOT in tab-index order in the class: the enum is
// Audio(0), Video(1), Graphics(2), Gameplay(3), Interface(4) while the strings declare
// Video, Graphics, Audio, Gameplay, Interface — kOptsTabNameOffsets below is in ENUM order.
constexpr int32_t kOptsTitleOffset = 0x42C;       // m_strTitle (FString)
constexpr int32_t kOptsTabNameOffsets[5] = {0x4DC, 0x4BC, 0x4CC, 0x4EC, 0x4FC};
constexpr int32_t kOptsListOffset = 0x140C;       // List (UIList*)
constexpr int32_t kOptsMechaItemsOffset = 0x1414; // m_arrMechaItems (TArray<UIMechaListItem*>)
constexpr int32_t kOptsCurrentTabOffset = 0x1424; // m_iCurrentTab (int)
constexpr int32_t kOptsGPUButtonOffset = 0x1434;  // GPUAutoDetectButton (UIButton*)
constexpr int32_t kOptsSaveButtonOffset = 0x1444; // SaveAndExitButton (UIButton*)
constexpr int32_t kOptsExitButtonOffset = 0x144C; // ExitWitoutChangesButton (UIButton*)
constexpr int32_t kOptsCreditsButtonOffset = 0x1454; // CreditsButton (UIButton*)
constexpr int32_t kListSelectedIndexOffset = 0x404;  // UIList::SelectedIndex (int)
constexpr int32_t kMechaDescOffset = 0x404;       // UIMechaListItem::Desc (UIScrollingText*)
constexpr int32_t kMechaFlagsOffset = 0x414;      // UIMechaListItem flag dword
constexpr uint32_t kMechaDisabled = 0x01;         // bDisabled
constexpr int32_t kMechaValueOffset = 0x418;      // UIMechaListItem::Value (UIScrollingText*)
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
constexpr int32_t kPanelMovieOffset = 0x2B8;      // UIPanel::Movie (UIMovie*)
constexpr int32_t kMoviePresOffset = 0x204;       // UIMovie::PRES (XComPresentationLayerBase*)
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
constexpr int32_t kMechaBGOffset = 0x40C;         // UIMechaListItem::BG (UIPanel*)

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

// ---- raw, SEH-safe primitives (no C++ objects with destructors inside __try) ----

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

// A live INSTANCE: class matches by POINTER; its name is the class's display name, which
// excludes the class default object ("Default__<name>") for free.
bool IsInstanceRaw(UObject* obj, UObject* classObj, const char* className)
{
    __try {
        if (!obj || !classObj || reinterpret_cast<UObject*>(obj->Class) != classObj)
            return false;
        char name[64];
        return ObjNameAnsiRaw(obj, name, sizeof(name)) && strcmp(name, className) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// LAST live instance in GObjects (newest allocation): during world transitions a stale,
// not-yet-collected instance of the same class can precede the current world's one.
UObject* ScanLastInstanceRaw(UObject* classObj, const char* className)
{
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return nullptr;
        for (int32_t i = objects->size() - 1; i >= 0; i--) {
            UObject* obj = objects->at(i);
            if (obj && IsInstanceRaw(obj, classObj, className))
                return obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// The cached find-a-screen pattern shared by every screen recipe: resolve the class object once,
// keep the newest live instance, revalidate it cheaply per call (class pointer + name under SEH
// reject a GC'd-and-reused allocation), and rescan GObjects at most once a second.
struct ScreenFinder {
    const char* className;
    UObject* classObj = nullptr;
    void* cached = nullptr;
    uint64_t lastScanMs = 0;
    int emptyScans = 0;

    void* Find()
    {
        if (cached && IsInstanceRaw(reinterpret_cast<UObject*>(cached), classObj, className))
            return cached;
        cached = nullptr;

        uint64_t now = GetTickCount64();
        if (now - lastScanMs < 1000)
            return nullptr;
        lastScanMs = now;

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

        cached = ScanLastInstanceRaw(classObj, className);
        if (cached)
            LOG_INFO("GameUI: found %s instance at 0x%p", className, cached);
        else if (++emptyScans % 30 == 1)
            LOG_DEBUG("GameUI: no %s instance yet (scan #%d)", className, emptyScans);
        return cached;
    }
};

struct RawMenuItem {
    void* handle;
    wchar_t label[256];
    bool focused;
    bool disabled;
    bool attention;
};

int ReadMainMenuRaw(void* shell, RawMenuItem* out, int maxItems)
{
    int count = 0;
    __try {
        // MAINMENU is TArray<UIButton*>: [data ptr (0x00)] [count (0x08)] [max (0x0C)].
        auto* arr = reinterpret_cast<const uint8_t*>(shell) + kShellMainMenuOffset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0 || n > 64)
            return 0;
        for (int32_t i = 0; i < n && count < maxItems; i++) {
            void* btn = data[i];
            if (!btn)
                continue;
            uint32_t panelFlags = ReadU32Raw(btn, kPanelFlagsOffset);
            if (!(panelFlags & kPanelVisible))
                continue;
            RawMenuItem& item = out[count];
            item.handle = btn;
            item.focused = (panelFlags & kPanelFocused) != 0;
            uint32_t buttonFlags = ReadU32Raw(btn, kButtonFlagsOffset);
            item.disabled = (buttonFlags & kButtonDisabled) != 0;
            item.attention = (buttonFlags & kButtonAttention) != 0;
            if (CopyFStringRaw(reinterpret_cast<const uint8_t*>(btn) + kButtonTextOffset,
                    item.label, 256) <= 0)
                continue; // a button with no text is scenery, not a control
            count++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return count;
}

// ---- settings screen raw readers ----

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

// The m_arrMechaItems pointer array, indices preserved (row index IS the List selection index —
// rows are spawned into the list in array order).
int ReadOptionRowPtrsRaw(void* screen, void** out, int maxItems)
{
    __try {
        auto* arr = reinterpret_cast<const uint8_t*>(screen) + kOptsMechaItemsOffset;
        auto* data = *reinterpret_cast<void* const* const*>(arr);
        int32_t n = *reinterpret_cast<const int32_t*>(arr + 8);
        if (!data || n <= 0)
            return 0;
        if (n > maxItems)
            n = maxItems;
        for (int32_t i = 0; i < n; i++)
            out[i] = data[i];
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

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

void* ReadObjectClassRaw(UObject* obj); // defined with the resolution cache below

// The object's class is named exactly `className` (no inheritance walk — used where the
// concrete class is known).
bool ObjectClassNamedRaw(void* obj, const char* className)
{
    void* cls = ReadObjectClassRaw(reinterpret_cast<UObject*>(obj));
    char name[64];
    return cls && ObjNameAnsiRaw(reinterpret_cast<UObject*>(cls), name, sizeof(name))
        && strcmp(name, className) == 0;
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
    case kLineCheckbox: {
        void* checkbox = ReadPtrRaw(item, kMechaCheckboxOffset);
        out->checked = checkbox && (ReadU32Raw(checkbox, kCheckboxFlagsOffset) & kCheckboxChecked);
        break;
    }
    case kLineButton: {
        void* button = ReadPtrRaw(item, kMechaButtonOffset);
        if (button)
            CopyFStringRaw(reinterpret_cast<const uint8_t*>(button) + kButtonTextOffset,
                out->value, 256);
        break;
    }
    case kLineValue:
    case kLineColorChip: {
        void* value = ReadPtrRaw(item, kMechaValueOffset);
        if (value) {
            if (CopyFStringRaw(reinterpret_cast<const uint8_t*>(value) + kScrollHtmlOffset,
                    out->value, 256) <= 0)
                CopyFStringRaw(reinterpret_cast<const uint8_t*>(value) + kScrollTextOffset,
                    out->value, 256);
        }
        break;
    }
    default:
        break;
    }
    return out->label[0] != L'\0';
}

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

void* ReadObjectClassRaw(UObject* obj)
{
    __try {
        return obj->Class;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
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

bool CallNamedFunction(void* obj, const char* funcName, void* params)
{
    if (!obj)
        return false;
    UFunction* fn = ResolveFunctionCached(obj, funcName);
    return fn && CallFunctionRaw(obj, fn, params);
}

// The current world's sound manager instance. The GRI spawns it as a SUBCLASS: XComShellGRI
// extends XComTacticalGRI, so the shell (main menu) world's manager is an
// XComTacticalSoundManager too; the Avenger uses XComStrategySoundManager. Both register the
// UI cues (DefaultGameData [XComGame.XCom*SoundManager] includes SoundGlobalUI.Play_Mouseover).
void* FindSoundManager()
{
    static const char* kClassNames[2] = {"XComTacticalSoundManager", "XComStrategySoundManager"};
    static UObject* sClasses[2] = {nullptr, nullptr};
    static void* sCached = nullptr;
    static int sCachedIdx = -1;
    static uint64_t sLastScanMs = 0;

    if (sCached
        && IsInstanceRaw(reinterpret_cast<UObject*>(sCached), sClasses[sCachedIdx],
            kClassNames[sCachedIdx]))
        return sCached;
    sCached = nullptr;

    uint64_t now = GetTickCount64();
    if (now - sLastScanMs < 1000)
        return nullptr;
    sLastScanMs = now;

    for (int i = 0; i < 2; i++) {
        if (!sClasses[i])
            sClasses[i] = FindClassRaw(kClassNames[i]);
        if (!sClasses[i])
            continue;
        void* inst = ScanLastInstanceRaw(sClasses[i], kClassNames[i]);
        if (inst) {
            sCached = inst;
            sCachedIdx = i;
            LOG_INFO("GameUI: sound manager (%s) at 0x%p", kClassNames[i], inst);
            return sCached;
        }
    }
    return nullptr;
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

// The My2K link state, queried the way the game's own UpdateMy2KButtonStatus does (offline /
// link / unlink), through the shell's FiraxisLive client. TTL-cached at the 1 s cadence the
// game's own status timer runs at, so the 10 Hz graph rebuild stays cheap. Game thread only.
My2KStatus QueryMy2KStatusCached(void* shell)
{
    static void* sClient = nullptr;
    static My2KStatus sStatus = My2KStatus::Unknown;
    static uint64_t sLastMs = 0;

    void* client = ReadPtrRaw(shell, kShellLiveClient);
    if (!client)
        return My2KStatus::Unknown;
    uint64_t now = GetTickCount64();
    if (client == sClient && now - sLastMs < 1000)
        return sStatus;
    sClient = client;
    sLastMs = now;

    struct {
        uint32_t ReturnValue;
    } params{};
    if (!CallNamedFunction(client, "IsLoggedIn", &params)) {
        sStatus = My2KStatus::Unknown;
    } else if (!params.ReturnValue) {
        sStatus = My2KStatus::Offline;
    } else {
        params.ReturnValue = 0;
        if (!CallNamedFunction(client, "IsAccountFull", &params))
            sStatus = My2KStatus::Unknown;
        else
            sStatus = params.ReturnValue ? My2KStatus::Linked : My2KStatus::NotLinked;
    }
    return sStatus;
}

} // namespace

void* FindMainMenuShell()
{
    static ScreenFinder sFinder{"UIFinalShell"};
    return sFinder.Find();
}

bool IsScreenFocused(void* screen)
{
    if (!screen)
        return false;
    uint32_t flags = ReadU32Raw(screen, kPanelFlagsOffset);
    return (flags & kPanelInited) && (flags & kPanelVisible) && (flags & kPanelFocused);
}

std::vector<MenuItem> ReadMainMenu(void* shell)
{
    std::vector<MenuItem> items;
    if (!shell)
        return items;

    RawMenuItem raw[32];
    int count = ReadMainMenuRaw(shell, raw, 32);

    for (int i = 0; i < count; i++) {
        MenuItem item;
        item.handle = raw[i].handle;
        item.label = CleanText(WcharToUtf8(raw[i].label, static_cast<int>(wcslen(raw[i].label))));
        item.focused = raw[i].focused;
        item.disabled = raw[i].disabled;
        item.attention = raw[i].attention;
        if (!item.label.empty())
            items.push_back(std::move(item));
    }
    return items;
}

ShellExtras ReadShellExtras(void* shell)
{
    ShellExtras ex;
    if (!shell)
        return ex;

    void* my2k = ReadPtrRaw(shell, kShellMy2KOffset);
    if (my2k && (ReadU32Raw(my2k, kPanelFlagsOffset) & kPanelVisible)) {
        ex.my2kHandle = my2k;
        ex.my2kStatus = QueryMy2KStatusCached(shell);
    }

    // The ticker holds the MOTD news line; hidden (SetTicker("")) when there is none. Its
    // content lives in HtmlText (SetHtmlText path) — Text stays empty on this screen.
    void* ticker = ReadPtrRaw(shell, kShellTickerOffset);
    if (ticker && (ReadU32Raw(ticker, kPanelFlagsOffset) & kPanelVisible)) {
        ex.tickerText = ReadFStringField(ticker, kScrollHtmlOffset);
        if (ex.tickerText.empty())
            ex.tickerText = ReadFStringField(ticker, kScrollTextOffset);
        if (!ex.tickerText.empty())
            ex.tickerHandle = ticker;
    }
    return ex;
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

// ---- settings screen (UIOptionsPCScreen) ----

void* FindOptionsScreen()
{
    static ScreenFinder sFinder{"UIOptionsPCScreen"};
    return sFinder.Find();
}

namespace {

OptionWidget WidgetOf(uint8_t type)
{
    switch (type) {
    case kLineSpinner:
    case kLineSpinnerAndButton: return OptionWidget::Spinner;
    case kLineDropdown: return OptionWidget::Dropdown;
    case kLineSlider: return OptionWidget::Slider;
    case kLineCheckbox: return OptionWidget::Checkbox;
    case kLineButton: return OptionWidget::Button;
    case kLineValue:
    case kLineColorChip: return OptionWidget::Value;
    default: return OptionWidget::Description;
    }
}

} // namespace

OptionsView ReadOptions(void* screen)
{
    OptionsView v;
    if (!screen)
        return v;

    v.title = ReadFStringField(screen, kOptsTitleOffset);
    v.currentTab = ReadI32Raw(screen, kOptsCurrentTabOffset);
    for (int i = 0; i < kOptionsTabCount; i++)
        v.tabNames[i] = ReadFStringField(screen, kOptsTabNameOffsets[i]);

    void* rows[64] = {};
    int rowCount = ReadOptionRowPtrsRaw(screen, rows, 64);
    for (int i = 0; i < rowCount; i++) {
        RawOptionRow raw;
        if (!ReadOptionRowRaw(rows[i], &raw))
            continue;
        OptionRow row;
        row.handle = rows[i];
        row.index = i;
        row.widget = WidgetOf(raw.type);
        row.label = CleanText(WcharToUtf8(raw.label, static_cast<int>(wcslen(raw.label))));
        row.value = CleanText(WcharToUtf8(raw.value, static_cast<int>(wcslen(raw.value))));
        row.percent = raw.percent;
        row.checked = raw.checked;
        row.disabled = raw.disabled;
        if (!row.label.empty())
            v.rows.push_back(std::move(row));
    }

    void* list = ReadPtrRaw(screen, kOptsListOffset);
    v.selectedRow = list ? ReadI32Raw(list, kListSelectedIndexOffset) : -1;

    // Bottom buttons in on-screen order; only visible ones become nodes (GPU auto-detect only
    // exists on the Graphics tab, Credits only where the screen allows it).
    constexpr int32_t kButtonOffsets[4] = {kOptsSaveButtonOffset, kOptsExitButtonOffset,
        kOptsCreditsButtonOffset, kOptsGPUButtonOffset};
    for (int32_t offset : kButtonOffsets) {
        void* btn = ReadPtrRaw(screen, offset);
        if (!btn)
            continue;
        uint32_t flags = ReadU32Raw(btn, kPanelFlagsOffset);
        if (!(flags & kPanelVisible))
            continue;
        MenuItem item;
        item.handle = btn;
        item.label = ReadFStringField(btn, kButtonTextOffset);
        item.focused = (flags & kPanelFocused) != 0;
        item.disabled = (ReadU32Raw(btn, kButtonFlagsOffset) & kButtonDisabled) != 0;
        if (!item.label.empty())
            v.buttons.push_back(std::move(item));
    }
    return v;
}

int GetOptionsTab(void* screen)
{
    return screen ? ReadI32Raw(screen, kOptsCurrentTabOffset) : 0;
}

std::string GetOptionsTabName(void* screen, int tab)
{
    if (!screen || tab < 0 || tab >= kOptionsTabCount)
        return {};
    return ReadFStringField(screen, kOptsTabNameOffsets[tab]);
}

bool SetOptionsTab(void* screen, int tab)
{
    struct {
        int32_t iSelect;
        uint32_t bForce;
    } params{tab, 0};
    return CallNamedFunction(screen, "SetSelectedTab", &params);
}

bool SetOptionsListIndex(void* screen, int index)
{
    void* list = ReadPtrRaw(screen, kOptsListOffset);
    if (!list)
        return false;
    struct {
        int32_t Index;
        uint32_t bForce;
        void* ReturnValue; // the function returns self
    } params{index, 0, nullptr};
    return CallNamedFunction(list, "SetSelectedIndex", &params);
}

bool SendRowCommand(void* rowItem, int cmd)
{
    // Direction commands act on the press phase, everything else on release (see the
    // kActionPress/kActionRelease comment).
    bool isDirection = cmd == kCmdArrowLeft || cmd == kCmdArrowRight;
    struct {
        int32_t Cmd;
        int32_t Arg;
        uint32_t ReturnValue;
    } params{cmd, isDirection ? kActionPress : kActionRelease, 0};
    return CallNamedFunction(rowItem, "OnUnrealCommand", &params);
}

std::string ReadPanelTooltip(void* panel)
{
    if (!panel || !(ReadU32Raw(panel, kPanelFlagsOffset) & kPanelHasTooltip))
        return {};
    int32_t id = ReadI32Raw(panel, kPanelTooltipIdOffset);

    void* movie = ReadPtrRaw(panel, kPanelMovieOffset);
    void* pres = movie ? ReadPtrRaw(movie, kMoviePresOffset) : nullptr;
    void* mgr = pres ? ReadPtrRaw(pres, kPresTooltipMgrOffset) : nullptr;
    if (!mgr)
        return {};

    // SetTooltipText path: a data element in the manager's shared TextTooltip.
    if (const uint8_t* el = FindTextTooltipDataRaw(mgr, id)) {
        std::string body = ReadFStringField(el, kTTDataBodyOffset);
        std::string title = ReadFStringField(el, kTTDataTitleOffset);
        if (title.empty())
            return body;
        if (body.empty())
            return title;
        return title + ". " + body;
    }

    // Preformed path: a real UITextTooltip object in the Tooltips array. Only text tooltips
    // carry sTitle/sBody at these offsets; other subclasses (ability tooltips) would read
    // garbage, so the class check gates the read.
    void* tip = FindTooltipByIdRaw(mgr, id);
    if (!tip || !ObjectClassNamedRaw(tip, "UITextTooltip"))
        return {};

    std::string body = ReadFStringField(tip, kTextTooltipBodyOffset);
    std::string title = ReadFStringField(tip, kTextTooltipTitleOffset);
    if (title.empty())
        return body;
    if (body.empty())
        return title;
    return title + ". " + body;
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

std::string CleanText(const std::string& text)
{
    // Strip <...> markup tags, collapse runs of whitespace, trim.
    std::string out;
    out.reserve(text.size());
    bool inTag = false;
    bool pendingSpace = false;
    for (char c : text) {
        if (c == '<') {
            inTag = true;
            continue;
        }
        if (inTag) {
            if (c == '>')
                inTag = false;
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
