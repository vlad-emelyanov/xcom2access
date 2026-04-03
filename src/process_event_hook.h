#pragma once

namespace ProcessEventHook {

// Install the vtable hook on UObject ProcessEvent (index 72).
// Requires SDK::IsReady() to be true.
// Returns true if the hook was installed successfully.
bool Install();

// Remove the hook and restore the original vtable entry.
void Remove();

// True if the hook is currently installed.
bool IsInstalled();

} // namespace ProcessEventHook
