#pragma once

#include <cstdint>

namespace SDK {

// Initialize the UE3 SDK by resolving GNames and GObjects from the game process.
// Returns true if both globals were found and validated.
// Safe to call multiple times — returns immediately if already initialized.
bool Init();

// Shut down SDK globals (nulls out pointers).
void Shutdown();

// True after a successful Init().
bool IsReady();

// Base address of the main executable module (XCom2.exe).
uintptr_t GetModuleBase();

// Image size of the main executable module.
uintptr_t GetModuleSize();

} // namespace SDK
