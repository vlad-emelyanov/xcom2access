#include "ue3_reflection.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "GameDefines.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

#include <cstring>

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace UE3Reflect {

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

    wchar_t wbuf[2048] = {};
    int len = CopyFStringToWcharBuffer(fstr, wbuf, 2048);
    if (len <= 0)
        return "";

    // Convert wchar_t (UTF-16) to UTF-8 std::string
    int needed = WideCharToMultiByte(CP_UTF8, 0, wbuf, len, nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return "";

    std::string result(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf, len, &result[0], needed, nullptr, nullptr);
    return result;
}

} // namespace UE3Reflect

#ifdef _MSC_VER
#pragma pack(pop)
#endif
