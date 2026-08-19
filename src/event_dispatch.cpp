#include "event_dispatch.h"
#include "log.h"

#include "GameDefines.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>

namespace EventDispatch {

// Handler storage — populated during Init, read-only on hot path
static std::unordered_map<int32_t, std::vector<EventHandler>> sHandlers;
static std::unordered_set<int32_t> sWatchedIndices;
static std::unordered_map<std::string, int32_t> sNameToIndex;

// Deferred registrations — handlers whose FNames weren't in GNames yet
struct PendingHandler {
    std::string functionName;
    EventHandler handler;
};
static std::vector<PendingHandler> sPending;

static std::atomic<bool> sDiscoveryLogging{false};
static std::atomic<bool> sDiscoveryResetRequested{false};
static std::atomic<uint64_t> sEventCount{0};

// The frame pump (see header). Game-thread only, like OnProcessEvent itself; the guard stops
// recursion when a pump-driven handler calls a game function that re-enters ProcessEvent.
static std::function<void()> sFramePump;
static bool sInFramePump = false;
static uint64_t sPumpFaults = 0;

// The pump call gets its OWN structured-exception guard. Without it, an access violation inside
// pump code unwinds past the caller's flag reset (SEH runs no C++ cleanup under /EHsc), latching
// sInFramePump true and silently killing the pump for the rest of the session.
static uint64_t sPumpCalls = 0;

static void CallFramePumpSEH() {
    sPumpCalls++;
    __try {
        sFramePump();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sPumpFaults++;
        if (sPumpFaults == 1 || sPumpFaults % 1000 == 0) {
            LOG_ERROR("EventDispatch: frame pump faulted (count=%llu)", sPumpFaults);
        }
    }
}

// Heartbeat: proves events and the pump are (or are not) flowing when nothing else logs.
static void HeartbeatMaybe() {
    static uint64_t sLastBeatMs = 0;
    uint64_t now = GetTickCount64();
    if (now - sLastBeatMs < 30000)
        return;
    bool first = sLastBeatMs == 0;
    sLastBeatMs = now;
    if (!first) // skip the boot call so the first beat lands 30 s in, with real numbers
        LOG_DEBUG("EventDispatch: heartbeat — %llu events, %llu pump calls, %llu pump faults",
                  sEventCount.load(std::memory_order_relaxed), sPumpCalls, sPumpFaults);
}

// Distinct UFunction FName indices already logged this discovery session.
// Touched only on the game thread (inside OnProcessEvent) — no lock needed.
static std::unordered_set<int32_t> sSeenDiscovery;

// SEH-safe discovery log helper — no C++ objects with destructors in SEH scope.
// Logs the calling function, the receiving object, and its class for context.
static void DiscoveryLogEvent(UObject* obj, UFunction* func) {
    char buf[640] = {};
    __try {
        const char* funcName = "?";
        const char* objName = "?";
        const char* className = "?";

        FName& fFuncName = func->Name;
        if (fFuncName.IsValid()) {
            FNameEntry* entry = FName::Names()->at(fFuncName.GetDisplayIndex());
            if (entry) funcName = entry->GetAnsiName();
        }

        FName& fObjName = obj->Name;
        if (fObjName.IsValid()) {
            FNameEntry* entry = FName::Names()->at(fObjName.GetDisplayIndex());
            if (entry) objName = entry->GetAnsiName();
        }

        UClass* cls = obj->Class;
        if (cls) {
            FName& fClassName = cls->Name;
            if (fClassName.IsValid()) {
                FNameEntry* entry = FName::Names()->at(fClassName.GetDisplayIndex());
                if (entry) className = entry->GetAnsiName();
            }
        }

        _snprintf_s(buf, sizeof(buf) - 1, "EventDispatch [discovery]: %s on %s (%s)",
                     funcName, objName, className);
        LOG_INFO("%s", buf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Silently skip
    }
}

// SEH-safe dispatch helper
static int ReadFNameIndex(UFunction* func) {
    __try {
        return func->Name.GetDisplayIndex();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void Init() {
    sHandlers.clear();
    sWatchedIndices.clear();
    sNameToIndex.clear();
    sEventCount.store(0);
    sSeenDiscovery.clear();
    LOG_INFO("EventDispatch: Initialized");
}

void Shutdown() {
    sHandlers.clear();
    sWatchedIndices.clear();
    sNameToIndex.clear();
    sSeenDiscovery.clear();
    LOG_INFO("EventDispatch: Shut down (processed %llu events)", sEventCount.load());
}

bool Register(const std::string& functionName, EventHandler handler) {
    // Resolve function name to FName index via GNames search
    FName fname(functionName.c_str());
    int32_t index = fname.GetDisplayIndex();

    if (index < 0) {
        // FName not in GNames yet — queue for deferred resolution
        LOG_INFO("EventDispatch: Deferring \"%s\" (not in GNames yet)", functionName.c_str());
        sPending.push_back({functionName, std::move(handler)});
        return false;
    }

    sHandlers[index].push_back(std::move(handler));
    sWatchedIndices.insert(index);
    sNameToIndex[functionName] = index;

    LOG_INFO("EventDispatch: Registered handler for \"%s\" (FName index %d)", functionName.c_str(), index);
    return true;
}

void RetryPending() {
    if (sPending.empty()) return;

    std::vector<PendingHandler> stillPending;

    for (auto& p : sPending) {
        FName fname(p.functionName.c_str());
        int32_t index = fname.GetDisplayIndex();

        if (index >= 0) {
            sHandlers[index].push_back(std::move(p.handler));
            sWatchedIndices.insert(index);
            sNameToIndex[p.functionName] = index;
            LOG_INFO("EventDispatch: Deferred handler resolved for \"%s\" (FName index %d)",
                     p.functionName.c_str(), index);
        } else {
            stillPending.push_back(std::move(p));
        }
    }

    sPending = std::move(stillPending);
}

void Unregister(const std::string& functionName) {
    auto it = sNameToIndex.find(functionName);
    if (it == sNameToIndex.end()) return;

    int32_t index = it->second;
    sHandlers.erase(index);
    sWatchedIndices.erase(index);
    sNameToIndex.erase(it);

    LOG_INFO("EventDispatch: Unregistered handlers for \"%s\"", functionName.c_str());
}

void SetFramePump(std::function<void()> pump) {
    sFramePump = std::move(pump);
    LOG_INFO("EventDispatch: frame pump %s", sFramePump ? "installed" : "cleared");
}

void OnProcessEvent(UObject* obj, UFunction* func, void* params) {
    sEventCount.fetch_add(1, std::memory_order_relaxed);
    HeartbeatMaybe();

    if (sFramePump && !sInFramePump) {
        sInFramePump = true;
        CallFramePumpSEH(); // throttles internally; faults are contained and counted
        sInFramePump = false;
    }

    // Fast path: read FName index via SEH-safe helper
    int32_t nameIndex = ReadFNameIndex(func);
    if (nameIndex < 0) return;

    // Discovery logging — record each distinct function once per session.
    if (sDiscoveryLogging.load(std::memory_order_relaxed)) {
        // Reset is requested from another thread; perform it here so the
        // seen-set is only ever mutated on the game thread.
        if (sDiscoveryResetRequested.exchange(false, std::memory_order_acq_rel)) {
            sSeenDiscovery.clear();
        }
        if (sSeenDiscovery.insert(nameIndex).second) {
            DiscoveryLogEvent(obj, func);
        }
    }

    if (sWatchedIndices.count(nameIndex) == 0) {
        return; // Not a watched event — bail fast
    }

    // Dispatch to registered handlers
    auto it = sHandlers.find(nameIndex);
    if (it != sHandlers.end()) {
        for (auto& handler : it->second) {
            handler(obj, func, params);
        }
    }
}

void SetDiscoveryLogging(bool enabled) {
    if (enabled) {
        // Ask the game thread to clear the seen-set at the next event, so the
        // set is never touched from this (input) thread.
        sDiscoveryResetRequested.store(true, std::memory_order_release);
    }
    sDiscoveryLogging.store(enabled, std::memory_order_relaxed);
    LOG_INFO("EventDispatch: Discovery logging %s", enabled ? "enabled" : "disabled");
}

bool IsDiscoveryLogging() {
    return sDiscoveryLogging.load(std::memory_order_relaxed);
}

} // namespace EventDispatch
