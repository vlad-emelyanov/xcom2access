#include "event_dispatch.h"
#include "log.h"

#include "GameDefines.hpp"
#include "SdkConstants.hpp"
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

static std::atomic<bool> sVerboseLogging{false};
static std::atomic<uint64_t> sEventCount{0};
static std::atomic<uint64_t> sLoggedCount{0};

// Rate limit for verbose logging: log every Nth event
static constexpr uint64_t VERBOSE_LOG_INTERVAL = 500;

// SEH-safe verbose log helper — no C++ objects with destructors in SEH scope
static void VerboseLogEvent(UObject* obj, UFunction* func, uint64_t logNum) {
    char buf[512] = {};
    __try {
        const char* funcName = "?";
        const char* objName = "?";

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

        _snprintf_s(buf, sizeof(buf) - 1, "EventDispatch [verbose #%llu]: %s on %s",
                     logNum, funcName, objName);
        LOG_DEBUG("%s", buf);
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
    sLoggedCount.store(0);
    LOG_INFO("EventDispatch: Initialized");
}

void Shutdown() {
    sHandlers.clear();
    sWatchedIndices.clear();
    sNameToIndex.clear();
    LOG_INFO("EventDispatch: Shut down (processed %llu events, logged %llu)",
             sEventCount.load(), sLoggedCount.load());
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

void OnProcessEvent(UObject* obj, UFunction* func, void* params) {
    uint64_t count = sEventCount.fetch_add(1, std::memory_order_relaxed);

    // Verbose logging — rate-limited to avoid flooding
    if (sVerboseLogging.load(std::memory_order_relaxed)) {
        if ((count % VERBOSE_LOG_INTERVAL) == 0) {
            VerboseLogEvent(obj, func, sLoggedCount.fetch_add(1, std::memory_order_relaxed));
        }
    }

    // Fast path: read FName index via SEH-safe helper
    int32_t nameIndex = ReadFNameIndex(func);
    if (nameIndex < 0) return;

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

void SetVerboseLogging(bool enabled) {
    sVerboseLogging.store(enabled, std::memory_order_relaxed);
    LOG_INFO("EventDispatch: Verbose logging %s", enabled ? "enabled" : "disabled");
}

bool IsVerboseLogging() {
    return sVerboseLogging.load(std::memory_order_relaxed);
}

} // namespace EventDispatch
