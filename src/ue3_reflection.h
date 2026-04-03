#pragma once

#include <string>

// Forward declarations
class UObject;

namespace UE3Reflect {

// Read a string (FString) property from a UObject by property name.
// Uses UE3 reflection: walks the class hierarchy's property chain,
// finds the UProperty with the matching name, reads FString at its offset.
// Returns empty string on failure. SEH-safe.
std::string ReadStringProperty(UObject* obj, const char* propName);

} // namespace UE3Reflect
