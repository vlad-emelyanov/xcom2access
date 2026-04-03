#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Forward declarations — avoid including full SDK headers
class UObject;
class UFunction;

namespace EventDispatch {

// Callback signature: (object that received the event, function being called, parameter block)
using EventHandler = std::function<void(UObject* obj, UFunction* func, void* params)>;

// Lifecycle
void Init();
void Shutdown();

// Register a handler for a specific UE3 function name (e.g., "OnInit", "Tick").
// The name is resolved to an FName index at registration time.
// If the name isn't in GNames yet, it's queued for deferred resolution.
// Returns true if the name was resolved immediately.
bool Register(const std::string& functionName, EventHandler handler);

// Retry resolving any pending (deferred) handler registrations.
// Call periodically (e.g., on Tick) until all handlers are resolved.
void RetryPending();

// Remove all handlers for a function name.
void Unregister(const std::string& functionName);

// Called from the ProcessEvent hook on every event — must be fast.
void OnProcessEvent(UObject* obj, UFunction* func, void* params);

// Toggle verbose logging of ALL events (rate-limited).
void SetVerboseLogging(bool enabled);
bool IsVerboseLogging();

} // namespace EventDispatch
