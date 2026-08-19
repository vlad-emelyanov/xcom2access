#include "gui_dump.h"

#ifdef X2ACCESS_DEVSERVER

#include "game_ui.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Core reflection types only, exactly as ue3_reflection.cpp and game_ui.cpp use them: UObject,
// UClass, UStruct/UField/UProperty and FName. These ARE safe to use as compiled layouts — they
// are the engine's own reflection spine, and the whole point of this file is to read every
// XComGame field THROUGH them rather than through the generated classes.
#include "GameDefines.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GuiDump {

namespace {

constexpr int kMaxPropsPerClass = 768;
constexpr int kMaxClassChain = 32;
constexpr int kMaxChildren = 192;
constexpr int kMaxNodes = 3000;   // a runaway tree must end in a truncation note, not a hang
constexpr int kMaxArrayElems = 8; // elements listed per array property
constexpr int kMaxStaticElems = 64; // elements printed for a static array (ArrayDim > 1)
constexpr int kMaxInstances = 32; // instances listed for an ambiguous ?instance=
constexpr int kMaxRegistry = 512; // a screen's ChildPanels registry, for the reachability check
constexpr int kValueChars = 160;  // per-value truncation

// ---- raw, SEH-safe primitives -------------------------------------------------------------
//
// Same contract as game_ui.cpp's: no C++ objects with destructors inside a __try body, so
// everything here traffics in PODs and the std::string building happens in the callers.

// The ANSI text of the FName stored at `addr` (entry id, then instance number). The engine
// prints the instance as "_N" with N one lower than the stored number, which is what makes a
// live screen read "UIFinalShell_0" rather than "UIFinalShell_1".
bool FNameTextRaw(const void* addr, char* buf, int bufLen)
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

bool ObjNameRaw(void* obj, char* buf, int bufLen)
{
    __try {
        return FNameTextRaw(&reinterpret_cast<UObject*>(obj)->Name, buf, bufLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ObjClassNameRaw(void* obj, char* buf, int bufLen)
{
    __try {
        UClass* cls = reinterpret_cast<UObject*>(obj)->Class;
        return cls && FNameTextRaw(&reinterpret_cast<UObject*>(cls)->Name, buf, bufLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// FString is TArray<wchar_t> at runtime despite the SDK declaring char:
// [wchar_t* data 0x00][int32 count 0x08][int32 max 0x0C].
int CopyFStringRaw(const void* fstr, wchar_t* buf, int bufWchars)
{
    __try {
        auto* raw = reinterpret_cast<const uint8_t*>(fstr);
        auto* data = *reinterpret_cast<wchar_t* const*>(raw);
        const int32_t count = *reinterpret_cast<const int32_t*>(raw + 8);
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

void* ReadPtrAt(const void* obj, int32_t offset)
{
    __try {
        return *reinterpret_cast<void* const*>(
            reinterpret_cast<const uint8_t*>(obj) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ---- reflection walks ----------------------------------------------------------------------

struct RawProp {
    void* prop;      // UProperty*
    char name[80];   // the property's own name
    char type[48];   // its CLASS name: StrProperty, ObjectProperty, ArrayProperty, ...
    int32_t offset;
    int32_t arrayDim;
    int32_t elementSize;
};

// The class chain from the object's class up to UObject, nearest first.
int ReadClassChainRaw(void* obj, void** outStructs, char (*outNames)[64], int maxOut)
{
    int n = 0;
    __try {
        UClass* cls = reinterpret_cast<UObject*>(obj)->Class;
        for (UStruct* s = reinterpret_cast<UStruct*>(cls); s && n < maxOut;
             s = reinterpret_cast<UStruct*>(s->SuperField)) {
            outStructs[n] = s;
            outNames[n][0] = '\0';
            FNameTextRaw(&reinterpret_cast<UObject*>(s)->Name, outNames[n], 64);
            n++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

// One class's OWN declared properties, in declaration order. Children also holds functions,
// enums, structs and states; only entries whose class name ends in "Property" carry an offset,
// so the rest are skipped rather than printed as fields that do not exist.
int ReadOwnPropsRaw(void* structObj, RawProp* out, int maxOut)
{
    int n = 0;
    __try {
        auto* st = reinterpret_cast<UStruct*>(structObj);
        for (UField* field = st->Children; field && n < maxOut; field = field->Next) {
            RawProp& p = out[n];
            p.name[0] = '\0';
            p.type[0] = '\0';
            if (!FNameTextRaw(&field->Name, p.name, sizeof(p.name)))
                continue;
            if (!ObjClassNameRaw(field, p.type, sizeof(p.type)))
                continue;
            const size_t len = strlen(p.type);
            if (len < 8 || strcmp(p.type + len - 8, "Property") != 0)
                continue;
            auto* prop = reinterpret_cast<UProperty*>(field);
            p.prop = field;
            p.offset = prop->Offset;
            p.arrayDim = prop->ArrayDim;
            p.elementSize = prop->ElementSize;
            n++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

// The offset of a named property on the object's class chain, or -1. Used for the one field the
// tree walk needs structurally (UIPanel::ParentPanel) — looked up rather than transcribed, so it
// survives a rebuild that moves it.
int32_t FindPropOffsetRaw(void* obj, const char* propName)
{
    void* chain[kMaxClassChain] = {};
    char names[kMaxClassChain][64] = {};
    const int depth = ReadClassChainRaw(obj, chain, names, kMaxClassChain);
    RawProp props[64] = {};
    for (int c = 0; c < depth; c++) {
        // Read this class's properties in slices: the scratch here is a stack buffer, and the
        // full per-class cap would be a large frame for a lookup that hits UIPanel immediately.
        const int count = ReadOwnPropsRaw(chain[c], props, 64);
        for (int i = 0; i < count; i++) {
            if (strcmp(props[i].name, propName) == 0)
                return props[i].offset;
        }
    }
    return -1;
}

// ---- property values -----------------------------------------------------------------------

enum class Kind {
    Unknown, Int, QWord, Float, Bool, Byte, Str, Name, Object, Array, Struct, Delegate
};

struct RawValue {
    Kind kind;
    int64_t i;
    double f;
    void* ptr;
    int32_t count;  // array element count / struct size in bytes
    uint32_t aux;   // BoolProperty bitmask, or an array's inner element size
    wchar_t wide[512];
    char text[96];  // object's class / struct name / enum literal / array inner type
    char decl[64];  // ObjectProperty's declared PropertyClass
};

Kind KindOf(const char* type)
{
    if (!strcmp(type, "IntProperty")) return Kind::Int;
    if (!strcmp(type, "QWordProperty")) return Kind::QWord;
    if (!strcmp(type, "FloatProperty")) return Kind::Float;
    if (!strcmp(type, "BoolProperty")) return Kind::Bool;
    if (!strcmp(type, "ByteProperty")) return Kind::Byte;
    if (!strcmp(type, "StrProperty")) return Kind::Str;
    if (!strcmp(type, "NameProperty")) return Kind::Name;
    if (!strcmp(type, "ArrayProperty")) return Kind::Array;
    if (!strcmp(type, "StructProperty")) return Kind::Struct;
    if (!strcmp(type, "DelegateProperty")) return Kind::Delegate;
    // ObjectProperty and everything deriving from it (Class, Component, Interface) is a pointer
    // at the property's offset, so one branch covers them all.
    if (strstr(type, "ObjectProperty") || strstr(type, "ClassProperty")
        || strstr(type, "ComponentProperty") || strstr(type, "InterfaceProperty"))
        return Kind::Object;
    return Kind::Unknown;
}

bool IsObjectArray(const char* innerType)
{
    return strstr(innerType, "ObjectProperty") || strstr(innerType, "ClassProperty")
        || strstr(innerType, "ComponentProperty");
}

// The enum literal for `value`, e.g. eUILineItemType_Checkbox — the readable half of a byte.
bool EnumLiteralRaw(void* byteProp, int64_t value, char* buf, int bufLen)
{
    __try {
        UEnum* e = reinterpret_cast<UByteProperty*>(byteProp)->Enum;
        if (!e || value < 0 || value >= e->Names.size())
            return false;
        return FNameTextRaw(&e->Names.at(static_cast<int32_t>(value)), buf, bufLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// `index` picks the element of a STATIC array (ArrayDim > 1); 0 for everything else.
void ReadValueRaw(void* obj, const RawProp& p, int index, RawValue& v)
{
    v.kind = KindOf(p.type);
    v.i = 0;
    v.f = 0;
    v.ptr = nullptr;
    v.count = 0;
    v.aux = 0;
    v.wide[0] = L'\0';
    v.text[0] = '\0';
    v.decl[0] = '\0';

    __try {
        auto* addr = reinterpret_cast<uint8_t*>(obj) + p.offset
            + static_cast<size_t>(index) * static_cast<size_t>(p.elementSize);
        switch (v.kind) {
        case Kind::Int:
            v.i = *reinterpret_cast<const int32_t*>(addr);
            break;
        case Kind::QWord:
            v.i = *reinterpret_cast<const int64_t*>(addr);
            break;
        case Kind::Float:
            v.f = *reinterpret_cast<const float*>(addr);
            break;
        case Kind::Bool:
            v.aux = reinterpret_cast<UBoolProperty*>(p.prop)->BitMask;
            v.i = (*reinterpret_cast<const uint32_t*>(addr) & v.aux) ? 1 : 0;
            break;
        case Kind::Byte:
            v.i = *addr;
            EnumLiteralRaw(p.prop, v.i, v.text, sizeof(v.text));
            break;
        case Kind::Str:
            CopyFStringRaw(addr, v.wide, static_cast<int>(sizeof(v.wide) / sizeof(wchar_t)));
            break;
        case Kind::Name:
            FNameTextRaw(addr, v.text, sizeof(v.text));
            break;
        case Kind::Object:
            v.ptr = *reinterpret_cast<void* const*>(addr);
            if (v.ptr)
                ObjClassNameRaw(v.ptr, v.text, sizeof(v.text));
            // The DECLARED type is worth printing even when the value is null — that is exactly
            // the case where a recipe author needs to know what belongs there.
            if (UClass* declared = reinterpret_cast<UObjectProperty*>(p.prop)->PropertyClass)
                FNameTextRaw(&reinterpret_cast<UObject*>(declared)->Name, v.decl, sizeof(v.decl));
            break;
        case Kind::Delegate:
            // A delegate's VALUE is an FScriptDelegate — { UObject* Object; FName FunctionName; }
            // — so the bound function name sits 8 bytes in. Its 0xB0 is a UFunction, NOT a
            // PropertyClass, which is why this cannot share the object branch.
            v.ptr = *reinterpret_cast<void* const*>(addr);
            if (v.ptr)
                ObjClassNameRaw(v.ptr, v.text, sizeof(v.text));
            FNameTextRaw(addr + 8, v.decl, sizeof(v.decl));
            break;
        case Kind::Array:
            v.ptr = *reinterpret_cast<void* const*>(addr);
            v.count = *reinterpret_cast<const int32_t*>(addr + 8);
            if (UProperty* inner = reinterpret_cast<UArrayProperty*>(p.prop)->Inner) {
                ObjClassNameRaw(inner, v.text, sizeof(v.text));
                v.aux = static_cast<uint32_t>(inner->ElementSize);
            }
            break;
        case Kind::Struct:
            if (UStruct* s = reinterpret_cast<UStructProperty*>(p.prop)->Struct) {
                FNameTextRaw(&reinterpret_cast<UObject*>(s)->Name, v.text, sizeof(v.text));
                v.count = s->PropertySize;
            }
            break;
        default:
            break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v.kind = Kind::Unknown;
    }
}

// One element of a dynamic array, for the element listings. Object arrays give class+address,
// FString arrays give the text; every other inner type only reports its count.
bool ArrayElemRaw(void* data, int32_t stride, int index, bool objects, RawValue& v)
{
    v.ptr = nullptr;
    v.text[0] = '\0';
    v.wide[0] = L'\0';
    __try {
        auto* addr = reinterpret_cast<uint8_t*>(data) + static_cast<size_t>(index) * stride;
        if (objects) {
            v.ptr = *reinterpret_cast<void* const*>(addr);
            if (v.ptr)
                ObjClassNameRaw(v.ptr, v.text, sizeof(v.text));
        } else {
            CopyFStringRaw(addr, v.wide, static_cast<int>(sizeof(v.wide) / sizeof(wchar_t)));
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---- formatting ----------------------------------------------------------------------------

std::string Utf8(const wchar_t* wide)
{
    if (!wide || !wide[0])
        return {};
    const int len = static_cast<int>(wcslen(wide));
    const int need = WideCharToMultiByte(CP_UTF8, 0, wide, len, nullptr, 0, nullptr, nullptr);
    if (need <= 0)
        return {};
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, len, &out[0], need, nullptr, nullptr);
    return out;
}

std::string Truncate(std::string s, size_t max)
{
    if (s.size() <= max)
        return s;
    s.resize(max);
    s += "...";
    return s;
}

bool ContainsNoCase(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > haystack.size())
        return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); i++) {
        size_t j = 0;
        for (; j < needle.size(); j++) {
            if (tolower(static_cast<unsigned char>(haystack[i + j]))
                != tolower(static_cast<unsigned char>(needle[j])))
                break;
        }
        if (j == needle.size())
            return true;
    }
    return false;
}

std::string FormatValue(const RawProp& p, const RawValue& v)
{
    char buf[256];
    switch (v.kind) {
    case Kind::Int:
    case Kind::QWord:
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.i));
        return buf;
    case Kind::Float:
        snprintf(buf, sizeof(buf), "%g", v.f);
        return buf;
    case Kind::Bool:
        // The mask is the point: several bools share one dword, and a recipe that wants the bit
        // needs both halves.
        snprintf(buf, sizeof(buf), "%-5s (mask 0x%08X)", v.i ? "true" : "false", v.aux);
        return buf;
    case Kind::Byte:
        if (v.text[0])
            snprintf(buf, sizeof(buf), "%lld (%s)", static_cast<long long>(v.i), v.text);
        else
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.i));
        return buf;
    case Kind::Str: {
        const std::string text = Utf8(v.wide);
        return text.empty() ? "\"\"" : "\"" + Truncate(GameUI::CleanText(text), kValueChars) + "\"";
    }
    case Kind::Name:
        return v.text[0] ? std::string("'") + v.text + "'" : std::string("'None'");
    case Kind::Object:
        if (!v.ptr)
            return v.decl[0] ? std::string("null (") + v.decl + ")" : std::string("null");
        snprintf(buf, sizeof(buf), "%s 0x%p", v.text[0] ? v.text : "?", v.ptr);
        return buf;
    case Kind::Delegate:
        if (!v.ptr && !v.decl[0])
            return "unbound";
        snprintf(buf, sizeof(buf), "%s 0x%p . %s", v.text[0] ? v.text : "?", v.ptr,
            v.decl[0] ? v.decl : "None");
        return buf;
    case Kind::Struct:
        snprintf(buf, sizeof(buf), "%s (%d bytes)", v.text[0] ? v.text : "?", v.count);
        return buf;
    case Kind::Array: {
        snprintf(buf, sizeof(buf), "%d x %s", v.count, v.text[0] ? v.text : "?");
        std::string out = buf;
        const bool objects = IsObjectArray(v.text);
        const bool strings = strcmp(v.text, "StrProperty") == 0;
        if (v.ptr && v.count > 0 && v.aux > 0 && (objects || strings)) {
            const int show = v.count < kMaxArrayElems ? v.count : kMaxArrayElems;
            for (int i = 0; i < show; i++) {
                RawValue elem{};
                if (!ArrayElemRaw(v.ptr, static_cast<int32_t>(v.aux), i, objects, elem))
                    break;
                out += "\n";
                if (objects) {
                    snprintf(buf, sizeof(buf), "        [%d] %s 0x%p", i,
                        elem.text[0] ? elem.text : "null", elem.ptr);
                    out += buf;
                } else {
                    out += "        [" + std::to_string(i) + "] \""
                        + Truncate(GameUI::CleanText(Utf8(elem.wide)), kValueChars) + "\"";
                }
            }
            if (show < v.count)
                out += "\n        ... " + std::to_string(v.count - show) + " more";
        }
        return out;
    }
    default:
        snprintf(buf, sizeof(buf), "(%s)", p.type);
        return buf;
    }
}

// ---- selection -----------------------------------------------------------------------------

std::string DescribeObject(void* obj);

void* Resolve(const Selector& sel, std::string& err, std::string& note)
{
    if (sel.ptr) {
        // A pointer copied out of an earlier dump can have been collected since. Walking a freed
        // one would fault inside a guard and report a plausible-looking empty tree, so
        // membership in GObjects is checked before anything is read through it.
        if (!GameUI::IsLiveObject(sel.ptr)) {
            char buf[96];
            snprintf(buf, sizeof(buf), "0x%p is not a live UObject (collected, or never was)\n",
                sel.ptr);
            err = buf;
            return nullptr;
        }
        return sel.ptr;
    }
    if (!sel.instance.empty()) {
        void* found[kMaxInstances] = {};
        const int count = GameUI::FindInstances(sel.instance.c_str(), found, kMaxInstances);
        if (count <= 0) {
            err = "no live instance of " + sel.instance + "\n";
            return nullptr;
        }
        // With several alive there is no way to tell which one the caller meant — a screen's
        // position in GObjects says nothing about its age, and corpses linger until GC — so the
        // dump names them all and lets the caller pick one with ?ptr=.
        if (count > 1) {
            note = std::to_string(count) + " live instances of " + sel.instance
                + " — dumping the first; ?ptr= picks another:\n";
            for (int i = 0; i < count; i++)
                note += "  " + DescribeObject(found[i]) + "\n";
            if (count == kMaxInstances)
                note += "  (listing capped at " + std::to_string(kMaxInstances) + ")\n";
        }
        return found[0];
    }
    void* screen = GameUI::FindStackScreen(sel.screen.empty() ? nullptr : sel.screen.c_str());
    if (!screen) {
        err = sel.screen.empty()
            ? std::string("the screen stack has no usable screen\n")
            : sel.screen + " is not on the screen stack — /status lists what is, and ?instance= "
                           "reaches objects the stack never holds\n";
    }
    return screen;
}

std::string DescribeObject(void* obj)
{
    char name[80] = "?";
    char cls[64] = "?";
    ObjNameRaw(obj, name, sizeof(name));
    ObjClassNameRaw(obj, cls, sizeof(cls));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s (%s) 0x%p", name, cls, obj);
    return buf;
}

// ---- the per-node summary the tree prints ----------------------------------------------------

// What one pass over a node's properties yields. Everything the tree line needs comes from that
// single walk: the state bits BY NAME (so the tree needs no flag offsets of its own — and reads
// correctly even if a rebuild moves them), the text the widget carries, and the two numeric
// fields that make a list row readable.
struct NodeInfo {
    bool haveFlags = false;
    bool inited = false, focused = false, visible = false, removed = false, navigable = false;
    bool haveSelected = false;
    int selectedIndex = -1;
    std::string lineType;                                  // UIMechaListItem::Type literal
    std::vector<std::pair<std::string, std::string>> text; // property name -> cleaned value
};

// Scratch for the property walk, reused across nodes: a tree is thousands of nodes and this
// buffer is ~100 KB. Game thread only, and CollectNode never re-enters itself.
std::vector<RawProp> gScratch;

// UIPanel::ParentPanel's offset, resolved once per dump from the root. -1 means the lookup
// failed, and the walk then falls back to taking ChildPanels at face value.
int32_t gParentOffset = -1;

void CollectNode(void* obj, NodeInfo& info)
{
    void* chain[kMaxClassChain] = {};
    char names[kMaxClassChain][64] = {};
    const int depth = ReadClassChainRaw(obj, chain, names, kMaxClassChain);

    if (gScratch.size() < kMaxPropsPerClass)
        gScratch.resize(kMaxPropsPerClass);

    for (int c = 0; c < depth; c++) {
        const int count = ReadOwnPropsRaw(chain[c], gScratch.data(), kMaxPropsPerClass);
        for (int i = 0; i < count; i++) {
            const RawProp& p = gScratch[static_cast<size_t>(i)];
            const Kind kind = KindOf(p.type);
            if (kind == Kind::Bool) {
                bool* slot = nullptr;
                if (!strcmp(p.name, "bIsInited")) slot = &info.inited;
                else if (!strcmp(p.name, "bIsFocused")) slot = &info.focused;
                else if (!strcmp(p.name, "bIsVisible")) slot = &info.visible;
                else if (!strcmp(p.name, "bIsRemoved")) slot = &info.removed;
                else if (!strcmp(p.name, "bIsNavigable")) slot = &info.navigable;
                if (!slot)
                    continue;
                RawValue v{};
                ReadValueRaw(obj, p, 0, v);
                *slot = v.i != 0;
                info.haveFlags = true;
            } else if (kind == Kind::Int && !strcmp(p.name, "SelectedIndex")) {
                RawValue v{};
                ReadValueRaw(obj, p, 0, v);
                info.selectedIndex = static_cast<int>(v.i);
                info.haveSelected = true;
            } else if (kind == Kind::Byte && !strcmp(p.name, "Type")) {
                RawValue v{};
                ReadValueRaw(obj, p, 0, v);
                info.lineType = v.text[0] ? std::string(v.text) : std::to_string(v.i);
            } else if (kind == Kind::Str && info.text.size() < 4) {
                RawValue v{};
                ReadValueRaw(obj, p, 0, v);
                const std::string text = GameUI::CleanText(Utf8(v.wide));
                if (!text.empty())
                    info.text.emplace_back(p.name, Truncate(text, 80));
            }
        }
    }
}

void WalkTree(void* panel, int depth, int maxDepth, int indexInParent, std::vector<void*>& seen,
    int& nodes, std::string& out)
{
    if (nodes >= kMaxNodes) {
        if (nodes == kMaxNodes) {
            out += "  ... node cap reached, tree truncated\n";
            nodes++;
        }
        return;
    }
    nodes++;

    char cls[64] = "?";
    ObjClassNameRaw(panel, cls, sizeof(cls));

    NodeInfo info;
    CollectNode(panel, info);

    std::string line(static_cast<size_t>(depth) * 2, ' ');
    char buf[192];
    snprintf(buf, sizeof(buf), "[%d] %s 0x%p", indexInParent, cls, panel);
    line += buf;
    if (info.haveFlags) {
        std::string bits;
        bits += info.inited ? "inited " : "";
        bits += info.focused ? "FOCUSED " : "";
        bits += info.visible ? "visible " : "";
        bits += info.navigable ? "nav " : "";
        bits += info.removed ? "REMOVED " : "";
        while (!bits.empty() && bits.back() == ' ')
            bits.pop_back();
        if (!bits.empty())
            line += " " + bits;
    }
    if (info.haveSelected)
        line += " sel=" + std::to_string(info.selectedIndex);
    if (!info.lineType.empty())
        line += " type=" + info.lineType;
    for (const auto& t : info.text)
        line += " " + t.first + "=\"" + t.second + "\"";
    out += line;
    out += "\n";

    if (depth >= maxDepth) {
        void* probe[1] = {};
        if (GameUI::ReadChildPanels(panel, probe, 1) > 0)
            out += std::string(static_cast<size_t>(depth + 1) * 2, ' ') + "... (depth cap)\n";
        return;
    }

    void* children[kMaxChildren] = {};
    const int count = GameUI::ReadChildPanels(panel, children, kMaxChildren);
    int printed = 0;
    for (int i = 0; i < count; i++) {
        if (!children[i] || children[i] == panel)
            continue;
        // A SCREEN's ChildPanels is not its children — it is a flat registry of every panel on
        // the screen, itself included (verified live: UITacticalHUD listed all 159, with a
        // UIStatList that really hangs four levels down inside the shot HUD). Deeper panels list
        // only their own children. ParentPanel is the link that means the same thing everywhere,
        // so the walk filters on it and the tree comes out actually nested.
        if (gParentOffset >= 0 && ReadPtrAt(children[i], gParentOffset) != panel)
            continue;
        bool repeat = false;
        for (void* v : seen) {
            if (v == children[i]) {
                repeat = true;
                break;
            }
        }
        if (repeat) {
            char cycle[96] = "?";
            ObjClassNameRaw(children[i], cycle, sizeof(cycle));
            out += std::string(static_cast<size_t>(depth + 1) * 2, ' ') + "(already shown: "
                + cycle + ")\n";
            continue;
        }
        seen.push_back(children[i]);
        WalkTree(children[i], depth + 1, maxDepth, printed++, seen, nodes, out);
    }
}

} // namespace

std::string Tree(const Selector& sel, int maxDepth)
{
    std::string err;
    std::string note;
    void* root = Resolve(sel, err, note);
    if (!root)
        return "[gui] " + err;

    if (maxDepth < 0)
        maxDepth = 0;
    if (maxDepth > 32)
        maxDepth = 32;

    gParentOffset = FindPropOffsetRaw(root, "ParentPanel");

    std::string out = note;
    out += "root: " + DescribeObject(root) + "\n";
    char head[128];
    snprintf(head, sizeof(head), "depth: %d, children via UIPanel.ParentPanel (+0x%04X)\n\n",
        maxDepth, static_cast<unsigned>(gParentOffset));
    out += gParentOffset >= 0
        ? head
        : "depth: " + std::to_string(maxDepth)
            + ", ParentPanel not found — showing ChildPanels raw, which on a SCREEN is a flat "
              "registry of every panel and will look wrongly nested\n\n";

    std::vector<void*> seen{root};
    int nodes = 0;
    WalkTree(root, 0, maxDepth, 0, seen, nodes, out);
    out += "\n" + std::to_string(nodes) + " panels\n";

    // No silent drops: anything in the root's own list that the walk never printed is named
    // here. On a screen that list is every panel it owns, so this is a complete accounting —
    // an entry showing up means its ParentPanel points somewhere outside this tree (or nowhere),
    // which is a fact about the screen worth seeing rather than hiding.
    if (gParentOffset >= 0) {
        void* registry[kMaxRegistry] = {};
        const int total = GameUI::ReadChildPanels(root, registry, kMaxRegistry);
        std::string orphans;
        int count = 0;
        for (int i = 0; i < total; i++) {
            if (!registry[i] || registry[i] == root)
                continue;
            bool shown = false;
            for (void* v : seen) {
                if (v == registry[i]) {
                    shown = true;
                    break;
                }
            }
            if (shown)
                continue;
            count++;
            if (count <= 24) {
                void* parent = ReadPtrAt(registry[i], gParentOffset);
                orphans += "  " + DescribeObject(registry[i]) + "  parent="
                    + (parent ? DescribeObject(parent) : std::string("null")) + "\n";
            }
        }
        if (count > 0) {
            out += "\n" + std::to_string(count)
                + " in the root's list but not under it (deeper than the depth cap, or parented "
                  "elsewhere):\n" + orphans;
            if (count > 24)
                out += "  ... " + std::to_string(count - 24) + " more\n";
        }
    }
    return out;
}

std::string Props(const Selector& sel, const std::string& filter)
{
    std::string err;
    std::string note;
    void* obj = Resolve(sel, err, note);
    if (!obj)
        return "[props] " + err;

    void* chain[kMaxClassChain] = {};
    char names[kMaxClassChain][64] = {};
    const int depth = ReadClassChainRaw(obj, chain, names, kMaxClassChain);
    if (depth <= 0)
        return "[props] could not read the class chain — is that really a UObject?\n";

    std::string out = note;
    out += "object: " + DescribeObject(obj) + "\n";
    out += "chain:";
    for (int c = 0; c < depth; c++)
        out += std::string(c ? " < " : " ") + names[c];
    out += "\n";
    if (!filter.empty())
        out += "filter: " + filter + "\n";
    // Saying where these numbers come from matters: they are the LIVE class's offsets, so they
    // already account for whatever mods recompiled the package — which the SDK dump does not.
    out += "offsets come from the running class, not from the SDK dump\n";

    int shown = 0;
    std::vector<RawProp> props(kMaxPropsPerClass);
    for (int c = 0; c < depth; c++) {
        const int count = ReadOwnPropsRaw(chain[c], props.data(), kMaxPropsPerClass);
        if (count <= 0)
            continue;
        const bool capped = count == kMaxPropsPerClass;

        std::string body;
        for (int i = 0; i < count; i++) {
            const RawProp& p = props[static_cast<size_t>(i)];
            if (!ContainsNoCase(p.name, filter))
                continue;
            // A faulted read leaves arrayDim at whatever was in the slot, so it is bounded
            // rather than trusted.
            int dim = p.arrayDim;
            if (dim < 1)
                dim = 1;
            const bool dimCapped = dim > kMaxStaticElems;
            if (dimCapped)
                dim = kMaxStaticElems;
            for (int e = 0; e < dim; e++) {
                RawValue v{};
                ReadValueRaw(obj, p, e, v);
                std::string name = p.name;
                if (p.arrayDim > 1)
                    name += "[" + std::to_string(e) + "]";
                char head[192];
                snprintf(head, sizeof(head), "  +0x%04X  %-18s %-30s = ",
                    static_cast<unsigned>(p.offset + e * p.elementSize), p.type, name.c_str());
                body += head;
                body += FormatValue(p, v);
                body += "\n";
                shown++;
            }
            if (dimCapped)
                body += "    ... " + std::to_string(p.arrayDim - kMaxStaticElems)
                    + " more elements not shown\n";
        }
        if (body.empty())
            continue;
        out += "\n-- " + std::string(names[c]) + " --\n";
        out += body;
        if (capped)
            out += "  (property list capped at " + std::to_string(kMaxPropsPerClass) + ")\n";
    }

    if (shown == 0)
        out += "\n(no properties matched)\n";
    return out;
}

} // namespace GuiDump

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif // X2ACCESS_DEVSERVER
