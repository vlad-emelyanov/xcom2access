#pragma once

#include <string>

// Forward declaration
class UObject;

namespace Hooks {

void Init();
void Shutdown();

// Read a named string property from the cached accessibility listener.
// Used by Input module for on-demand reads (e.g., ? key for nav help).
// Returns empty string if no listener is cached or property not found.
std::string ReadListenerProperty(const char* propName);

} // namespace Hooks
