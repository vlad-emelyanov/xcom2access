#include "gui_dump.h"

#ifdef X2ACCESS_DEVSERVER

#include "game_ui.h"
#include "game_ui_internal.h"
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
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GuiDump {

// The raw primitives this dumper carried private copies of until the game_ui split exposed the
// shared set behind game_ui_internal.h. FNameAtAnsiRaw prints the engine's own "_N" instance
// suffix, which is exactly what the dumper wants a live panel's name to read as.
using GameUI::CopyFStringRaw;
using GameUI::FNameAtAnsiRaw;
using GameUI::ReadPtrRaw;

namespace {

constexpr int kMaxPropsPerClass = 768;
constexpr int kMaxClassChain = 32;
constexpr int kMaxChildren = 192;
constexpr int kMaxNodes = 3000;   // a runaway tree must end in a truncation note, not a hang
// Elements listed per array property. Raised from 8 on 2026-08-20: the anchored tutorial blade
// sat at index 8 of UIWorldMessageMgr.m_arrMsgs — the pool is 8 recycled slots plus one appended
// per NAMED message, so the truncation hid precisely the entries that carry an id and matter.
// Raised again to 160 on 2026-09-16: verifying the companion's XComInput.ini merge means reading
// the whole of PlayerInput.Bindings off a CDO (100 entries in the tactical section), and a
// dump that shows 32 of them cannot answer "is Tab still bound".
constexpr int kMaxArrayElems = 160;
constexpr int kMaxStaticElems = 64; // elements printed for a static array (ArrayDim > 1)
constexpr int kMaxInstances = 32; // instances listed for an ambiguous ?instance=
constexpr int kMaxRegistry = 512; // a screen's ChildPanels registry, for the reachability check
constexpr int kMaxStructMembers = 12; // members printed inline per struct
constexpr int kMaxStructDepth = 2;    // how far struct-inside-struct is expanded
constexpr int kValueChars = 160;  // per-value truncation

// ---- raw, SEH-safe helpers over the shared primitives ---------------------------------------
//
// Same contract as game_ui.cpp's: no C++ objects with destructors inside a __try body, so
// everything here traffics in PODs and the std::string building happens in the callers.
// Names come through FNameAtAnsiRaw so a live panel prints with the engine's own "_N" instance
// suffix ("UIFinalShell_0") — the dumper's whole vocabulary, unlike the finders' plain
// ObjNameAnsiRaw.

bool ObjNameRaw(void* obj, char* buf, int bufLen)
{
    __try {
        return FNameAtAnsiRaw(&reinterpret_cast<UObject*>(obj)->Name, buf, bufLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ObjClassNameRaw(void* obj, char* buf, int bufLen)
{
    __try {
        UClass* cls = reinterpret_cast<UObject*>(obj)->Class;
        return cls && FNameAtAnsiRaw(&reinterpret_cast<UObject*>(cls)->Name, buf, bufLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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
            FNameAtAnsiRaw(&reinterpret_cast<UObject*>(s)->Name, outNames[n], 64);
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
            if (!FNameAtAnsiRaw(&field->Name, p.name, sizeof(p.name)))
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

// ---- /call: finding a function and reading its signature ------------------------------------
//
// GameUI::CallScriptFunction resolves a function the same way, but it does not hand the
// UFunction back, and /call has to INSPECT one before it dares invoke it. Both walks are the
// same Children walk ReadOwnPropsRaw does — a class's Children holds its functions, states,
// enums and structs alongside its properties — so this adds no offset of its own either.

constexpr uint64_t kCpfParm = 0x80;        // UProperty::PropertyFlags — this is a parameter
constexpr uint64_t kCpfReturnParm = 0x400; // ... and specifically the return value
constexpr uint32_t kFuncNative = 0x400;    // UFunction::FunctionFlags — dispatches to a thunk

// A parameter carries everything a PROPERTY does — /call needs the offset and size to place the
// return value inside the block it hands ProcessEvent, and reusing RawProp means the return
// prints through the same FormatValue path /props uses rather than growing a second formatter.
struct RawParm {
    RawProp prop;
    bool isReturn;
};

// The UFunction of that name on the object's class chain, or null. `outOwner` receives the
// class that actually declares it, which is the interesting half of the answer when a call
// resolves somewhere other than where the caller expected.
void* FindFunctionOnChainRaw(void* obj, const char* funcName, char* outOwner, int ownerLen)
{
    void* chain[kMaxClassChain] = {};
    char names[kMaxClassChain][64] = {};
    const int depth = ReadClassChainRaw(obj, chain, names, kMaxClassChain);
    for (int c = 0; c < depth; c++) {
        void* found = nullptr;
        __try {
            auto* st = reinterpret_cast<UStruct*>(chain[c]);
            for (UField* field = st->Children; field; field = field->Next) {
                char name[64] = "";
                char cls[64] = "";
                if (!FNameAtAnsiRaw(&field->Name, name, sizeof(name)))
                    continue;
                if (strcmp(name, funcName) != 0)
                    continue;
                // Children mixes kinds; only a UFunction is callable. A state of the same name
                // would otherwise be handed to ProcessEvent as if it were one.
                if (!ObjClassNameRaw(field, cls, sizeof(cls)) || strcmp(cls, "Function") != 0)
                    continue;
                found = field;
                break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (found) {
            strncpy_s(outOwner, ownerLen, names[c], _TRUNCATE);
            return found;
        }
    }
    // Deliberately NOT searching states: a state-scoped function needs the UState's own chain
    // (GameUI::FindStateFunction), and calling one through ProcessEvent outside its state is a
    // different question than this endpoint is asking.
    outOwner[0] = '\0';
    return nullptr;
}

// A function's declared PARAMETERS, in order. A UFunction is itself a UStruct, so its Children
// are its parameters followed by its locals; CPF_Parm separates them, and the return value is a
// parameter that also carries CPF_ReturnParm. Reading this reflectively is what lets /call
// refuse a call it cannot make safely — ParmsSize would answer the same question, but the
// generated header buries it inside UFunction's padding, so it could not be `static_assert`ed
// and would be exactly the hand-transcribed offset this file exists to avoid.
//
// ⚠ CPF_ReturnParm is what separates "takes arguments" from "answers something". A function that
// declares only a return value takes NO arguments and is safe to call — see the guard in Call().
int ReadFunctionParmsRaw(void* fn, RawParm* out, int maxOut)
{
    int n = 0;
    __try {
        auto* st = reinterpret_cast<UStruct*>(fn);
        for (UField* field = st->Children; field && n < maxOut; field = field->Next) {
            RawProp& p = out[n].prop;
            p.name[0] = '\0';
            p.type[0] = '\0';
            if (!FNameAtAnsiRaw(&field->Name, p.name, sizeof(p.name)))
                continue;
            if (!ObjClassNameRaw(field, p.type, sizeof(p.type)))
                continue;
            const size_t len = strlen(p.type);
            if (len < 8 || strcmp(p.type + len - 8, "Property") != 0)
                continue;
            auto* prop = reinterpret_cast<UProperty*>(field);
            if (!(prop->PropertyFlags & kCpfParm))
                continue; // a local, not a parameter
            p.prop = field;
            p.offset = prop->Offset;
            p.arrayDim = prop->ArrayDim;
            p.elementSize = prop->ElementSize;
            out[n].isReturn = (prop->PropertyFlags & kCpfReturnParm) != 0;
            n++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

uint32_t ReadFunctionFlagsRaw(void* fn)
{
    __try {
        // The generated header declares FunctionFlags as uint64 (CLAUDE.md), so read the low
        // dword the engine actually stores. offsetof is still valid — that field alone is.
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<char*>(fn) + offsetof(UFunction, FunctionFlags));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// A STRUCT's own chain, starting at the struct itself — unlike ReadClassChainRaw, which starts
// at an object's class. UnrealScript structs can extend other structs, so the members are not
// necessarily all declared on the leaf.
int ReadStructChainRaw(void* structObj, void** out, int maxOut)
{
    int n = 0;
    __try {
        for (UStruct* s = reinterpret_cast<UStruct*>(structObj); s && n < maxOut;
             s = reinterpret_cast<UStruct*>(s->SuperField))
            out[n++] = s;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
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
    void* strct;    // UStruct* for a struct property, or an array's struct inner type
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
        return FNameAtAnsiRaw(&e->Names.at(static_cast<int32_t>(value)), buf, bufLen);
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
    v.strct = nullptr;

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
            FNameAtAnsiRaw(addr, v.text, sizeof(v.text));
            break;
        case Kind::Object:
            v.ptr = *reinterpret_cast<void* const*>(addr);
            if (v.ptr)
                ObjClassNameRaw(v.ptr, v.text, sizeof(v.text));
            // The DECLARED type is worth printing even when the value is null — that is exactly
            // the case where a recipe author needs to know what belongs there.
            if (UClass* declared = reinterpret_cast<UObjectProperty*>(p.prop)->PropertyClass)
                FNameAtAnsiRaw(&reinterpret_cast<UObject*>(declared)->Name, v.decl, sizeof(v.decl));
            break;
        case Kind::Delegate:
            // A delegate's VALUE is an FScriptDelegate — { UObject* Object; FName FunctionName; }
            // — so the bound function name sits 8 bytes in. Its 0xB0 is a UFunction, NOT a
            // PropertyClass, which is why this cannot share the object branch.
            v.ptr = *reinterpret_cast<void* const*>(addr);
            if (v.ptr)
                ObjClassNameRaw(v.ptr, v.text, sizeof(v.text));
            FNameAtAnsiRaw(addr + 8, v.decl, sizeof(v.decl));
            break;
        case Kind::Array:
            v.ptr = *reinterpret_cast<void* const*>(addr);
            v.count = *reinterpret_cast<const int32_t*>(addr + 8);
            if (UProperty* inner = reinterpret_cast<UArrayProperty*>(p.prop)->Inner) {
                ObjClassNameRaw(inner, v.text, sizeof(v.text));
                v.aux = static_cast<uint32_t>(inner->ElementSize);
                if (strcmp(v.text, "StructProperty") == 0)
                    v.strct = reinterpret_cast<UStructProperty*>(inner)->Struct;
            }
            break;
        case Kind::Struct:
            if (UStruct* s = reinterpret_cast<UStructProperty*>(p.prop)->Struct) {
                FNameAtAnsiRaw(&reinterpret_cast<UObject*>(s)->Name, v.text, sizeof(v.text));
                v.count = s->PropertySize;
                v.strct = s;
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
    return wide ? GameUI::WcharToUtf8(wide, static_cast<int>(wcslen(wide))) : std::string();
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

std::string FormatValue(void* obj, const RawProp& p, int elem, const RawValue& v, int depth);

// "{ X=24, Y=54, Z=9 }" — a struct's members read at their own offsets from `base`. This is the
// difference between a tile dump saying "TTile (12 bytes)" and saying where the cursor is.
std::string FormatStructInline(void* base, void* structObj, int depth)
{
    void* chain[8] = {};
    const int levels = ReadStructChainRaw(structObj, chain, 8);
    RawProp members[64] = {};

    std::string out = "{ ";
    int shown = 0;
    bool more = false;
    for (int c = 0; c < levels; c++) {
        const int count = ReadOwnPropsRaw(chain[c], members, 64);
        for (int i = 0; i < count; i++) {
            if (shown >= kMaxStructMembers) {
                more = true;
                break;
            }
            RawValue mv{};
            ReadValueRaw(base, members[i], 0, mv);
            if (shown)
                out += ", ";
            out += members[i].name;
            out += "=";
            out += FormatValue(base, members[i], 0, mv, depth);
            shown++;
        }
    }
    if (!shown)
        return "{}";
    if (more)
        out += ", ...";
    out += " }";
    return out;
}

std::string FormatValue(void* obj, const RawProp& p, int elem, const RawValue& v, int depth)
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
    case Kind::Struct: {
        snprintf(buf, sizeof(buf), "%s ", v.text[0] ? v.text : "?");
        std::string out = buf;
        if (v.strct && depth < kMaxStructDepth) {
            auto* base = reinterpret_cast<uint8_t*>(obj) + p.offset
                + static_cast<size_t>(elem) * static_cast<size_t>(p.elementSize);
            out += FormatStructInline(base, v.strct, depth + 1);
        } else {
            snprintf(buf, sizeof(buf), "(%d bytes)", v.count);
            out += buf;
        }
        return out;
    }
    case Kind::Array: {
        snprintf(buf, sizeof(buf), "%d x %s", v.count, v.text[0] ? v.text : "?");
        std::string out = buf;
        const bool objects = IsObjectArray(v.text);
        const bool strings = strcmp(v.text, "StrProperty") == 0;
        const bool structs = v.strct != nullptr && depth < kMaxStructDepth;
        if (v.ptr && v.count > 0 && v.aux > 0 && structs) {
            const int show = v.count < kMaxArrayElems ? v.count : kMaxArrayElems;
            for (int i = 0; i < show; i++) {
                auto* elemBase = reinterpret_cast<uint8_t*>(v.ptr) + static_cast<size_t>(i) * v.aux;
                out += "\n        [" + std::to_string(i) + "] "
                    + FormatStructInline(elemBase, v.strct, depth + 1);
            }
            if (show < v.count)
                out += "\n        ... " + std::to_string(v.count - show) + " more";
            return out;
        }
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
    if (!sel.cdo.empty()) {
        void* obj = GameUI::FindDefaultObject(sel.cdo.c_str());
        if (!obj) {
            err = "no class default object Default__" + sel.cdo
                + " — that class is not loaded (its package may not be in this build)\n";
            return nullptr;
        }
        return obj;
    }
    if (!sel.instance.empty()) {
        void* found[kMaxInstances] = {};
        const int count = GameUI::FindInstances(sel.instance.c_str(), found, kMaxInstances);
        if (count <= 0) {
            // Fall back to the class default object. Safe precisely BECAUSE nothing live
            // matched: the reason FindInstances skips CDOs is that one would otherwise shadow a
            // real screen, and here there is no real screen to shadow. This is what makes
            // ?instance=X2AccessObjectScanner do the obvious thing instead of reporting an
            // absence — those companion classes never have a non-default instance at all.
            void* def = GameUI::FindDefaultObject(sel.instance.c_str());
            if (def) {
                note = "no live instance of " + sel.instance
                    + " — showing its class default object (Default__" + sel.instance + ")\n";
                return def;
            }
            err = "no live instance of " + sel.instance
                + ", and no class default object of that name either\n";
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
        if (gParentOffset >= 0 && ReadPtrRaw(children[i], gParentOffset) != panel)
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
                void* parent = ReadPtrRaw(registry[i], gParentOffset);
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
                body += FormatValue(obj, p, e, v, 0);
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

// A bool property's bitmask, or 0 if it could not be read. Its own function because __try may
// not share a scope with objects that need destruction, and WriteArg returns a std::string.
uint32_t BoolBitMaskRaw(void* prop)
{
    __try {
        return reinterpret_cast<UBoolProperty*>(prop)->BitMask;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Write one text argument into the parameter block, by the declared property's kind. Returns an
// empty string on success, or why it refused.
//
// `owned` holds the wide buffers behind any FString arguments: ProcessEvent shallow-memcpy's the
// block, and the engine's destroy pass runs only on properties at or above ParmsSize — i.e. on
// locals, never on parameters — so a buffer WE own and keep alive for the duration of the call is
// exactly right, and freeing anything afterwards would be wrong.
std::string WriteArg(unsigned char* block, size_t blockSize, const RawProp& p,
    const std::string& text, std::vector<std::unique_ptr<std::wstring>>& owned)
{
    const Kind kind = KindOf(p.type);
    const size_t end = static_cast<size_t>(p.offset) + static_cast<size_t>(p.elementSize);
    if (p.offset < 0 || end > blockSize || p.arrayDim != 1)
        return std::string("argument \"") + p.name + "\" does not fit our parameter block";

    unsigned char* addr = block + p.offset;
    switch (kind) {
    case Kind::Int:
        *reinterpret_cast<int32_t*>(addr) = static_cast<int32_t>(strtol(text.c_str(), nullptr, 0));
        return {};
    case Kind::QWord:
        *reinterpret_cast<int64_t*>(addr) = _strtoi64(text.c_str(), nullptr, 0);
        return {};
    case Kind::Float:
        *reinterpret_cast<float*>(addr) = static_cast<float>(atof(text.c_str()));
        return {};
    case Kind::Byte:
        *addr = static_cast<unsigned char>(strtoul(text.c_str(), nullptr, 0));
        return {};
    case Kind::Bool: {
        const bool on = text == "1" || text == "true" || text == "TRUE" || text == "True";
        const uint32_t mask = BoolBitMaskRaw(p.prop);
        if (!mask)
            return std::string("could not read the bitmask of bool \"") + p.name + "\"";
        uint32_t& word = *reinterpret_cast<uint32_t*>(addr);
        word = on ? (word | mask) : (word & ~mask);
        return {};
    }
    case Kind::Str: {
        // TArray<wchar_t>: data pointer, Num, Max — and Num COUNTS THE TERMINATOR, which is what
        // CopyFStringRaw's `count - 1` on the way out is the mirror of.
        owned.push_back(std::make_unique<std::wstring>(GameUI::Utf8ToWide(text)));
        std::wstring& buf = *owned.back();
        const int32_t num = static_cast<int32_t>(buf.size()) + 1;
        *reinterpret_cast<const wchar_t**>(addr) = buf.c_str();
        *reinterpret_cast<int32_t*>(addr + 8) = num;
        *reinterpret_cast<int32_t*>(addr + 12) = num;
        return {};
    }
    default:
        break;
    }
    // Everything left is a pointer or a container, and the only value this endpoint could put
    // there is zero — which inside script is a null dereference, in the very session this exists
    // to observe.
    return std::string("argument \"") + p.name + "\" is a " + p.type
        + ", which /call will not synthesize";
}

std::string Call(const Selector& sel, const std::string& funcName,
    const std::vector<std::string>& args)
{
    if (funcName.empty())
        return "[call] no function named — /call takes &fn=<FunctionName>\n";

    std::string err;
    std::string note;
    void* obj = Resolve(sel, err, note);
    if (!obj)
        return "[call] " + err;

    char owner[64] = "";
    void* fn = FindFunctionOnChainRaw(obj, funcName.c_str(), owner, sizeof(owner));
    if (!fn) {
        return note + "[call] " + DescribeObject(obj) + "\n"
            + "no function \"" + funcName + "\" on its class chain.\n"
            + "/props lists the object's fields; a function declared inside a STATE is not "
              "reachable here (its home is the UState's own chain).\n";
    }

    const uint32_t flags = ReadFunctionFlagsRaw(fn);

    RawParm parms[32] = {};
    const int parmCount = ReadFunctionParmsRaw(fn, parms, 32);

    std::string head = note;
    head += "object:   " + DescribeObject(obj) + "\n";
    char line[256];
    snprintf(line, sizeof(line), "function: %s.%s at 0x%p (FunctionFlags 0x%08X%s)\n", owner,
        funcName.c_str(), fn, flags, (flags & kFuncNative) ? ", native" : "");
    head += line;

    // ARGUMENT-free only, on purpose — which is not the same as parameter-free. ProcessEvent
    // shallow-memcpy's ParmsSize bytes out of the block we hand it, so a function with real
    // arguments would read whatever we passed, and a zeroed object pointer is a null deref inside
    // script: a RedScreen or a crash in the very session this endpoint exists to observe. A
    // RETURN value is the opposite case — the engine WRITES it into our block at
    // `Parms + ReturnValueOffset` and reads nothing — so refusing those refused exactly the
    // ungates this endpoint exists for: `UIButton.Click()` answers a bool, and turning it down
    // meant the launch button on `UISquadSelect` (mouse-only in vanilla) could not be pressed
    // from here at all.
    const RawParm* ret = nullptr;
    int argCount = 0;
    for (int i = 0; i < parmCount; i++) {
        if (parms[i].isReturn)
            ret = &parms[i];
        else
            argCount++;
    }

    if (argCount != static_cast<int>(args.size())) {
        char count[160];
        snprintf(count, sizeof(count),
            "NOT CALLED — it declares %d argument(s) and %zu were supplied. Signature:\n",
            argCount, args.size());
        head += count;
        for (int i = 0; i < parmCount; i++) {
            head += std::string("  ") + parms[i].prop.type + " " + parms[i].prop.name
                + (parms[i].isReturn ? "   (return value)" : "") + "\n";
        }
        head += "supply them with &arg=<value>, once per argument, in declaration order.\n";
        return head;
    }

    // A valid, zeroed block rather than nullptr: with ParmsSize 0 the engine copies nothing, so
    // this is belt-and-braces against a signature that reads differently than we just measured —
    // and with a return value it is where that value lands.
    unsigned char params[256] = {};
    std::vector<std::unique_ptr<std::wstring>> owned; // must outlive the call; see WriteArg
    {
        size_t next = 0;
        for (int i = 0; i < parmCount && next < args.size(); i++) {
            if (parms[i].isReturn)
                continue;
            const std::string why
                = WriteArg(params, sizeof(params), parms[i].prop, args[next++], owned);
            if (!why.empty()) {
                head += "NOT CALLED — " + why + ".\n";
                return head;
            }
        }
    }
    if (ret) {
        const size_t end = static_cast<size_t>(ret->prop.offset)
            + static_cast<size_t>(ret->prop.elementSize) * static_cast<size_t>(ret->prop.arrayDim);
        if (ret->prop.offset < 0 || end > sizeof(params)) {
            snprintf(line, sizeof(line),
                "NOT CALLED — its %s return wants bytes [%d, %zu) and our parameter block is "
                "%zu.\n",
                ret->prop.type, ret->prop.offset, end, sizeof(params));
            head += line;
            return head;
        }
    }

    const bool ok = GameUI::CallScriptFunction(obj, funcName.c_str(), params);
    LOG_INFO("DevServer: /call %s.%s (%zu args) on 0x%p — %s", owner, funcName.c_str(),
        args.size(), obj, ok ? "returned" : "FAULTED");

    if (!ok) {
        head += "CALL FAULTED — the SEH guard caught it. The object may have been mid-GC, or the\n"
                "function may depend on state this screen does not have right now.\n";
        return head;
    }

    head += "called; it returned normally. /gui, /props and /status show what it changed.\n";
    if (ret) {
        RawValue v{};
        ReadValueRaw(params, ret->prop, 0, v);
        head += "returned: " + FormatValue(params, ret->prop, 0, v, 0) + "\n";
        // The buffer behind a returned FString is engine-allocated and deliberately LEAKED, the
        // same call the dev server's /exec makes: freeing it needs appFree, and a few dozen bytes
        // per dev-only call beats calling the wrong deallocator.
    }
    return head;
}

std::string Flash(const Selector& sel, const std::string& field, const std::string& funcName)
{
    std::string err;
    std::string note;
    void* obj = Resolve(sel, err, note);
    if (!obj)
        return "[flash] " + err;

    char line[256];
    std::string head = note;
    head += "object:   " + DescribeObject(obj) + "\n";

    // Both handles come out of REFLECTION, so this verb hand-transcribes no offsets of its own —
    // the same contract the tree and the property dump keep, and the reason it stays right
    // across a Highlander rebuild that moves fields around.
    const int32_t movieOff = FindPropOffsetRaw(obj, "Movie");
    void* movie = obj;
    if (movieOff >= 0) {
        movie = ReadPtrRaw(obj, movieOff);
        if (!movie) {
            snprintf(line, sizeof(line),
                "movie:    Movie is NULL at +0x%X — this panel has not been inited into a "
                "movie, so it has no Flash side yet.\n",
                movieOff);
            return head + line;
        }
        head += "movie:    " + DescribeObject(movie) + "\n";
    } else {
        // No Movie property: the caller selected the movie itself (?instance=UIMovie_2D), which
        // is the right way to reach an absolute path.
        head += "movie:    (the selected object itself — it declares no Movie property)\n";
    }

    // The panel's own path INTO that movie. MCPath is composed by the game as ONE FName
    // (`name(ParentPanel.MCPath $ "." $ MCName)`), so a path ending in _0 really is an FName
    // instance number and FNameAtAnsiRaw prints it back the way the game wrote it.
    std::string base;
    const int32_t mcOff = FindPropOffsetRaw(obj, "MCPath");
    if (mcOff >= 0) {
        char name[256] = "";
        if (FNameAtAnsiRaw(reinterpret_cast<const uint8_t*>(obj) + mcOff, name, sizeof(name)))
            base = name;
        if (base.empty() || base == "None") {
            snprintf(line, sizeof(line),
                "MCPath:   None at +0x%X — the panel has no path in the movie (not inited, or "
                "it is a pure UnrealScript container). Nothing to ask.\n",
                mcOff);
            return head + line;
        }
        head += "MCPath:   " + base + "\n";
    }

    std::string path = base;
    if (!field.empty()) {
        if (!path.empty())
            path += ".";
        path += field;
    }
    if (path.empty()) {
        return head
            + "nothing to ask: the object has no MCPath and no &field= was given. Pass a field, "
              "or use /call with an &arg= for an absolute path.\n";
    }

    const std::string fn = funcName.empty() ? std::string("GetVariableString") : funcName;
    head += "path:     " + path + "\n";
    head += "fn:       " + fn + "\n";

    const GameUI::FlashValue v = GameUI::FlashCall(movie, fn.c_str(), path.c_str());
    LOG_INFO("DevServer: /flash %s %s on 0x%p — %s", fn.c_str(), path.c_str(), movie,
        v.called ? "answered" : "refused");

    if (!v.called)
        return head + "NOT CALLED: " + v.error + "\n";

    switch (v.kind) {
    case GameUI::FlashValue::Kind::Text:
        head += "value:    \"" + Truncate(v.text, 2000) + "\" ("
            + std::to_string(v.text.size()) + " bytes)\n";
        break;
    case GameUI::FlashValue::Kind::Number:
        snprintf(line, sizeof(line), "value:    %g\n", v.number);
        head += line;
        break;
    case GameUI::FlashValue::Kind::Int:
        snprintf(line, sizeof(line), "value:    %lld\n", static_cast<long long>(v.number));
        head += line;
        break;
    case GameUI::FlashValue::Kind::Bool:
        head += std::string("value:    ") + (v.number != 0 ? "true" : "false") + "\n";
        break;
    case GameUI::FlashValue::Kind::Void:
        head += "value:    (void — the call ran and answers with nothing)\n";
        break;
    }

    // The one caveat that has to travel with every reading: Flash does not distinguish "no such
    // path" from "that field is empty". The first live probe told them apart only because the
    // real Selection accessors answer -1 where an unresolved path answers 0.
    const bool blank = (v.kind == GameUI::FlashValue::Kind::Text && v.text.empty())
        || (v.kind != GameUI::FlashValue::Kind::Text && v.kind != GameUI::FlashValue::Kind::Void
            && v.number == 0);
    if (blank) {
        head += "          ⚠ blank/zero is ambiguous here: an UNRESOLVED path answers exactly\n"
                "          the same as an empty field. Drop &field= to check the MCPath itself\n"
                "          resolves, then add one level at a time.\n";
    }
    return head;
}

} // namespace GuiDump

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif // X2ACCESS_DEVSERVER
