#include "sdk_globals.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// SDK headers — provides TArray, FNameEntry, FName, UObject, extern GObjects/GNames
#include "GameDefines.hpp"
#include "SdkConstants.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

namespace SDK {

static bool sReady = false;
static uintptr_t sModuleBase = 0;
static uintptr_t sModuleSize = 0;

bool IsReady() { return sReady; }
uintptr_t GetModuleBase() { return sModuleBase; }
uintptr_t GetModuleSize() { return sModuleSize; }

// Resolve module base and image size for the main executable.
static bool ResolveModule() {
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) {
        LOG_ERROR("SDK: GetModuleHandleA(nullptr) failed");
        return false;
    }
    sModuleBase = reinterpret_cast<uintptr_t>(exe);

    // Read PE header to get image size
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(sModuleBase);
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(sModuleBase + dos->e_lfanew);
    sModuleSize = nt->OptionalHeader.SizeOfImage;

    LOG_INFO("SDK: Module base = 0x%llX, size = 0x%llX", sModuleBase, sModuleSize);
    return true;
}

// SEH validation helpers — must be in separate functions from C++ objects with destructors.
// Returns 0 on success, negative on failure.
static int ValidateGNamesSEH(TArray<FNameEntry*>* pGNames, int32_t* outCount, const char** outFirstName) {
    __try {
        int32_t nameCount = pGNames->size();
        // Require at least 30000 names — ensures game scripts are loaded
        // (early init has ~5K names, fully loaded has ~46K)
        if (nameCount < 30000 || nameCount > 500000) return -1;

        FNameEntry* first = pGNames->at(0);
        if (!first) return -2;

        const char* firstName = first->GetAnsiName();
        if (!firstName || firstName[0] == '\0') return -3;

        *outCount = nameCount;
        *outFirstName = firstName;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -99;
    }
}

static int ValidateGObjectsSEH(TArray<UObject*>* pGObjects, int32_t* outCount, int32_t* outFirstNameId) {
    __try {
        int32_t objCount = pGObjects->size();
        if (objCount < 1 || objCount > 2000000) return -1;

        for (int32_t i = 0; i < objCount && i < 100; i++) {
            UObject* obj = pGObjects->at(i);
            if (obj) {
                *outCount = objCount;
                *outFirstNameId = obj->Name.GetDisplayIndex();
                return 0;
            }
        }
        return -2; // No non-null objects
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -99;
    }
}

bool Init() {
    if (sReady) return true;

    if (sModuleBase == 0 && !ResolveModule()) {
        return false;
    }

    // Resolve GNames
    auto* pGNames = reinterpret_cast<TArray<FNameEntry*>*>(sModuleBase + GNames_Offset);
    int32_t nameCount = 0;
    const char* firstName = nullptr;

    int result = ValidateGNamesSEH(pGNames, &nameCount, &firstName);
    if (result != 0) {
        if (result == -99) {
            LOG_ERROR("SDK: Exception validating GNames at 0x%llX", sModuleBase + GNames_Offset);
        } else {
            LOG_WARN("SDK: GNames validation failed (code %d)", result);
        }
        return false;
    }

    LOG_INFO("SDK: GNames validated — count=%d, [0]=\"%s\"", nameCount, firstName);
    GNames = pGNames;

    // Resolve GObjects
    auto* pGObjects = reinterpret_cast<TArray<UObject*>*>(sModuleBase + GObjects_Offset);
    int32_t objCount = 0;
    int32_t firstNameId = -1;

    result = ValidateGObjectsSEH(pGObjects, &objCount, &firstNameId);
    if (result != 0) {
        if (result == -99) {
            LOG_ERROR("SDK: Exception validating GObjects at 0x%llX", sModuleBase + GObjects_Offset);
        } else {
            LOG_WARN("SDK: GObjects validation failed (code %d)", result);
        }
        GNames = nullptr;
        return false;
    }

    LOG_INFO("SDK: GObjects validated — count=%d, first obj FName index=%d", objCount, firstNameId);
    GObjects = pGObjects;
    sReady = true;
    LOG_INFO("SDK: Initialization complete");
    return true;
}

void Shutdown() {
    GObjects = nullptr;
    GNames = nullptr;
    sReady = false;
    sModuleBase = 0;
    sModuleSize = 0;
    LOG_INFO("SDK: Shut down");
}

} // namespace SDK
