#pragma once

#include <cstdint>
#include <string>

// Forward declarations
class UObject;

namespace UE3Reflect {

// Read a string (FString) property from a UObject by property name.
// Uses UE3 reflection: walks the class hierarchy's property chain,
// finds the UProperty with the matching name, reads FString at its offset.
// Returns empty string on failure. SEH-safe.
std::string ReadStringProperty(UObject* obj, const char* propName);

// The engine's OWN element stride for a dynamic-array property, read off the UArrayProperty's
// Inner->ElementSize — for striding over a TArray of script structs whose runtime padding the
// compiled SDK cannot promise (a ScriptStruct's PropertiesSize is unaligned in the dump, and
// whether UStructProperty rounds it is the engine's call, not ours). 0 on failure. SEH-safe.
int32_t ArrayElementSize(UObject* obj, const char* propName);

} // namespace UE3Reflect
