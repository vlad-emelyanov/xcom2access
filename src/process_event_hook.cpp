#include "process_event_hook.h"
#include "sdk_globals.h"
#include "event_dispatch.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "GameDefines.hpp"
#include "SdkConstants.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

#include <unordered_set>
#include <vector>

namespace ProcessEventHook {

static constexpr size_t PROCESS_EVENT_VTABLE_INDEX = 72;
// Known ProcessEvent offset from OffsetFinder + Ghidra
static constexpr uintptr_t PROCESS_EVENT_OFFSET = 0x143B50;

using ProcessEventFn = void(__fastcall*)(UObject* pThis, UFunction* pFunction, void* pParams);

static ProcessEventFn sOriginalProcessEvent = nullptr;
static bool sInstalled = false;

// Track all vtables we patched so we can restore them
struct PatchedVtable {
    void** vtable;
};
static std::vector<PatchedVtable> sPatchedVtables;

// SEH-safe dispatch wrapper — prevents our code from crashing the game
static void DispatchSafe(UObject* pThis, UFunction* pFunction, void* pParams) {
    __try {
        EventDispatch::OnProcessEvent(pThis, pFunction, pParams);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Silently swallow — never crash the game from our dispatch
    }
}

// The hook function — called for every ProcessEvent in the game.
static void __fastcall HookedProcessEvent(UObject* pThis, UFunction* pFunction, void* pParams) {
    // Dispatch to our event system (fast path — checks FName index)
    if (pFunction) {
        DispatchSafe(pThis, pFunction, pParams);
    }

    // Always call the original — never break game functionality
    if (sOriginalProcessEvent) {
        sOriginalProcessEvent(pThis, pFunction, pParams);
    }
}

// SEH-safe vtable read
static void** ReadVtableSEH(UObject* obj) {
    __try {
        return *reinterpret_cast<void***>(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// SEH-safe vtable slot read
static void* ReadVtableSlotSEH(void** vtable, size_t index) {
    __try {
        return vtable[index];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool PatchVtableSlot(void** vtable) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtable[PROCESS_EVENT_VTABLE_INDEX], sizeof(void*),
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    vtable[PROCESS_EVENT_VTABLE_INDEX] = reinterpret_cast<void*>(&HookedProcessEvent);

    DWORD dummy = 0;
    VirtualProtect(&vtable[PROCESS_EVENT_VTABLE_INDEX], sizeof(void*), oldProtect, &dummy);
    return true;
}

static bool RestoreVtableSlot(void** vtable) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtable[PROCESS_EVENT_VTABLE_INDEX], sizeof(void*),
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    vtable[PROCESS_EVENT_VTABLE_INDEX] = reinterpret_cast<void*>(sOriginalProcessEvent);

    DWORD dummy = 0;
    VirtualProtect(&vtable[PROCESS_EVENT_VTABLE_INDEX], sizeof(void*), oldProtect, &dummy);
    return true;
}

bool Install() {
    if (sInstalled) return true;

    if (!SDK::IsReady()) {
        LOG_ERROR("ProcessEventHook: SDK not ready");
        return false;
    }

    auto* objects = UObject::GObjObjects();
    if (!objects) {
        LOG_ERROR("ProcessEventHook: GObjObjects() returned null");
        return false;
    }

    // The known ProcessEvent address
    uintptr_t moduleBase = SDK::GetModuleBase();
    uintptr_t expectedPE = moduleBase + PROCESS_EVENT_OFFSET;
    sOriginalProcessEvent = reinterpret_cast<ProcessEventFn>(expectedPE);

    LOG_INFO("ProcessEventHook: Target ProcessEvent at 0x%llX (module+0x%llX)",
             expectedPE, PROCESS_EVENT_OFFSET);

    // Scan all objects and find unique vtables that have ProcessEvent at slot 72
    std::unordered_set<void**> seenVtables;
    int patchCount = 0;
    int objCount = objects->size();

    for (int32_t i = 0; i < objCount; i++) {
        UObject* obj = objects->at(i);
        if (!obj) continue;

        void** vtable = ReadVtableSEH(obj);
        if (!vtable) continue;

        // Skip if we've already processed this vtable
        if (!seenVtables.insert(vtable).second) continue;

        // Check if this vtable's slot 72 points to ProcessEvent
        void* slot72 = ReadVtableSlotSEH(vtable, PROCESS_EVENT_VTABLE_INDEX);
        if (reinterpret_cast<uintptr_t>(slot72) != expectedPE) continue;

        // Patch this vtable
        if (PatchVtableSlot(vtable)) {
            sPatchedVtables.push_back({vtable});
            patchCount++;
        }
    }

    if (patchCount == 0) {
        LOG_ERROR("ProcessEventHook: No vtables found with ProcessEvent at slot %zu", PROCESS_EVENT_VTABLE_INDEX);
        sOriginalProcessEvent = nullptr;
        return false;
    }

    sInstalled = true;
    LOG_INFO("ProcessEventHook: Patched %d unique vtables (scanned %d objects, %zu unique vtables)",
             patchCount, objCount, seenVtables.size());
    return true;
}

void Remove() {
    if (!sInstalled || !sOriginalProcessEvent) return;

    int restored = 0;
    for (auto& entry : sPatchedVtables) {
        if (RestoreVtableSlot(entry.vtable)) {
            restored++;
        }
    }

    LOG_INFO("ProcessEventHook: Restored %d/%zu vtables", restored, sPatchedVtables.size());
    sPatchedVtables.clear();
    sOriginalProcessEvent = nullptr;
    sInstalled = false;
}

bool IsInstalled() {
    return sInstalled;
}

} // namespace ProcessEventHook
