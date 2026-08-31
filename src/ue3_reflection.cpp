#include "ue3_reflection.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "GameDefines.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

#include <cstring>
#include <vector>

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace UE3Reflect {

// A companion payload is a whole screen and can run to several KB; anything past a megabyte is
// a torn read of a count field, not a string, and must not become an allocation.
constexpr int kMaxSaneStringChars = 1 << 20;

// SEH-safe: find the FString pointer for a named property on a UObject.
// No C++ objects with destructors — only raw pointers and primitives.
static FString* FindStringPropertyRaw(UObject* obj, const char* propName) {
    __try {
        UClass* cls = obj->Class;
        if (!cls)
            return nullptr;

        for (UStruct* curStruct = reinterpret_cast<UStruct*>(cls);
             curStruct;
             curStruct = reinterpret_cast<UStruct*>(curStruct->SuperField))
        {
            for (UField* field = curStruct->Children; field; field = field->Next)
            {
                FName& fname = field->Name;
                if (!fname.IsValid())
                    continue;

                FNameEntry* entry = FName::Names()->at(fname.GetDisplayIndex());
                if (!entry)
                    continue;

                const char* fieldName = entry->GetAnsiName();
                if (!fieldName)
                    continue;

                if (strcmp(fieldName, propName) != 0)
                    continue;

                UProperty* prop = reinterpret_cast<UProperty*>(field);
                int32_t offset = prop->Offset;
                if (offset <= 0)
                    return nullptr;

                return reinterpret_cast<FString*>(
                    reinterpret_cast<uint8_t*>(obj) + offset
                );
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Access violation — silently fail
    }

    return nullptr;
}

// SEH-safe: walk the class chain for a named UArrayProperty and return its Inner->ElementSize —
// the stride the ENGINE itself uses to index the array. Same walk as FindStringPropertyRaw; kept
// separate because that one returns a value pointer and this one returns metadata, and merging
// them would force both callers through an out-parameter for the other's answer.
static int32_t ArrayElementSizeRaw(UObject* obj, const char* propName) {
    __try {
        UClass* cls = obj->Class;
        if (!cls)
            return 0;

        for (UStruct* curStruct = reinterpret_cast<UStruct*>(cls);
             curStruct;
             curStruct = reinterpret_cast<UStruct*>(curStruct->SuperField))
        {
            for (UField* field = curStruct->Children; field; field = field->Next)
            {
                FName& fname = field->Name;
                if (!fname.IsValid())
                    continue;

                FNameEntry* entry = FName::Names()->at(fname.GetDisplayIndex());
                if (!entry)
                    continue;

                const char* fieldName = entry->GetAnsiName();
                if (!fieldName || strcmp(fieldName, propName) != 0)
                    continue;

                UProperty* inner = reinterpret_cast<UArrayProperty*>(field)->Inner;
                if (!inner)
                    return 0;
                return inner->ElementSize;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Access violation — silently fail
    }

    return 0;
}

int32_t ArrayElementSize(UObject* obj, const char* propName) {
    if (!obj || !propName || !propName[0])
        return 0;
    return ArrayElementSizeRaw(obj, propName);
}

// SEH-safe: the FString's length in wchars, EXCLUDING the null. 0 if it is empty or unreadable.
// Read before the copy so the buffer can be sized to the string rather than the other way round.
static int FStringLengthRaw(FString* fstr) {
    __try {
        auto* rawPtr = reinterpret_cast<uint8_t*>(fstr);
        auto* data = *reinterpret_cast<wchar_t**>(rawPtr);
        int32_t count = *reinterpret_cast<int32_t*>(rawPtr + 8);
        if (!data || count <= 1)
            return 0;
        return count - 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// SEH-safe: copy FString contents into a caller-provided wchar buffer.
// UE3 FString is actually TArray<wchar_t> on Windows, despite SDK declaring char.
// The memory layout is the same: pointer(8) + count(4) + max(4) = 16 bytes.
// Returns number of wchars copied (excluding null), or 0 on failure.
static int CopyFStringToWcharBuffer(FString* fstr, wchar_t* buf, int bufWchars) {
    __try {
        // Reinterpret FString as wchar_t-based TArray
        // Layout: [wchar_t* data (0x00)] [int32 count (0x08)] [int32 max (0x0C)]
        auto* rawPtr = reinterpret_cast<uint8_t*>(fstr);
        auto* data = *reinterpret_cast<wchar_t**>(rawPtr);
        int32_t count = *reinterpret_cast<int32_t*>(rawPtr + 8);

        if (!data || count <= 0)
            return 0;

        // count includes null terminator
        int len = count - 1;
        if (len <= 0)
            return 0;

        if (len >= bufWchars)
            len = bufWchars - 1;

        memcpy(buf, data, len * sizeof(wchar_t));
        buf[len] = L'\0';
        return len;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buf[0] = L'\0';
    }

    return 0;
}

std::string ReadStringProperty(UObject* obj, const char* propName) {
    if (!obj || !propName || !propName[0])
        return "";

    FString* fstr = FindStringPropertyRaw(obj, propName);
    if (!fstr)
        return "";

    // ⛔ SIZE THE BUFFER FROM THE STRING, never the string to the buffer. This was a fixed
    // wchar_t[2048] that clipped anything longer and said nothing about it — and the companion
    // readers push a WHOLE SCREEN through this one function, so the cap was a silent limit on
    // how much of a screen a blind player can be told about. It bit for real on the XCOM
    // archives: 39 entry titles came to 2051 characters against the 2047 that fit, and the four
    // characters over the line took the LAST record with them — the item card — so the layer
    // looked like it had simply never built that stop. Reading the rows and dropping the tail is
    // the worst possible failure here, because it reads to a player as "the screen has no more".
    const int len = FStringLengthRaw(fstr);
    if (len <= 0)
        return "";
    if (len > kMaxSaneStringChars) {
        // Not a long string — a torn or garbage count. Refuse rather than allocate on it.
        LOG_WARN("UE3Reflect: \"%s\" reports %d chars; refusing to read", propName, len);
        return "";
    }

    std::vector<wchar_t> wbuf(static_cast<size_t>(len) + 1, L'\0');
    const int copied = CopyFStringToWcharBuffer(fstr, wbuf.data(), len + 1);
    if (copied <= 0)
        return "";

    // Convert wchar_t (UTF-16) to UTF-8 std::string
    int needed
        = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), copied, nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return "";

    std::string result(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), copied, &result[0], needed, nullptr, nullptr);
    return result;
}

} // namespace UE3Reflect

#ifdef _MSC_VER
#pragma pack(pop)
#endif
