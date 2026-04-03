/*
 * UE3 Offset Finder v2 for XCOM 2: War of the Chosen (x64)
 *
 * Injected as an ASI via Ultimate ASI Loader. Scans the running game process
 * for GNames, GObjects, UObject field layout, and ProcessEvent vtable index.
 * Dumps results to OffsetFinder.log.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <share.h>

static HMODULE gModule = nullptr;
static FILE* gLog = nullptr;
static uint8_t* gModuleSnapshot = nullptr; // Bulk copy of module for fast scanning
static size_t gModuleSnapshotSize = 0;

static void Log(const char* fmt, ...) {
    if (!gLog) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(gLog, fmt, args);
    va_end(args);
    fprintf(gLog, "\n");
    fflush(gLog);
}

// ============================================================================
// Memory helpers
// ============================================================================

static bool IsReadable(const void* addr, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    return true;
}

static bool SafeRead(const void* addr, void* buf, size_t size) {
    if (!IsReadable(addr, size)) return false;
    __try {
        memcpy(buf, addr, size);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadString(const void* addr, char* buf, size_t maxLen) {
    if (!IsReadable(addr, 1)) return false;
    __try {
        for (size_t i = 0; i < maxLen - 1; i++) {
            char c = ((const char*)addr)[i];
            buf[i] = c;
            if (c == '\0') return true;
        }
        buf[maxLen - 1] = '\0';
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct ModuleInfo { uintptr_t base; size_t size; };

static ModuleInfo GetMainModule() {
    ModuleInfo info = {};
    HMODULE hMod = GetModuleHandleA(nullptr);
    if (!hMod) return info;
    MODULEINFO mi;
    if (GetModuleInformation(GetCurrentProcess(), hMod, &mi, sizeof(mi))) {
        info.base = (uintptr_t)mi.lpBaseOfDll;
        info.size = mi.SizeOfImage;
    }
    return info;
}

// ============================================================================
// GNames
// ============================================================================

static const int NAME_OFFSETS_TO_TRY[] = { 0x10, 0x14, 0x18, 0x0C, 0x08 };

struct GNamesResult {
    uintptr_t address;      // Address of the TArray
    uintptr_t data;         // TArray.Data pointer
    int32_t count;           // TArray.Count
    int nameFieldOffset;    // Offset of Name[] within FNameEntry
};

static bool CheckNameEntry(uintptr_t entry, int nameOffset, const char* expected) {
    if (!IsReadable((void*)entry, nameOffset + 8)) return false;
    char buf[64] = {};
    if (!SafeReadString((void*)(entry + nameOffset), buf, sizeof(buf))) return false;
    return strcmp(buf, expected) == 0;
}

static std::string ReadFName(const GNamesResult& gn, int32_t nameIndex) {
    if (nameIndex < 0 || nameIndex >= gn.count) return "<invalid>";
    uintptr_t entryPtr = 0;
    if (!SafeRead((void*)(gn.data + nameIndex * 8), &entryPtr, 8) || entryPtr == 0)
        return "<null>";
    char buf[256] = {};
    SafeReadString((void*)(entryPtr + gn.nameFieldOffset), buf, sizeof(buf));
    return buf;
}

static GNamesResult FindGNames(uintptr_t moduleBase, size_t moduleSize) {
    GNamesResult result = {};
    Log("Scanning for GNames...");

    for (size_t offset = 0; offset + 16 <= moduleSize; offset += 8) {
        // Read TArray fields from snapshot (fast, no VirtualQuery)
        uintptr_t data = 0;
        int32_t count = 0;
        if (gModuleSnapshot) {
            memcpy(&data, gModuleSnapshot + offset, 8);
            memcpy(&count, gModuleSnapshot + offset + 8, 4);
        } else {
            if (!SafeRead((void*)(moduleBase + offset), &data, 8)) continue;
            if (!SafeRead((void*)(moduleBase + offset + 8), &count, 4)) continue;
        }
        if (data == 0 || count < 100 || count > 500000) continue;
        if (!IsReadable((void*)data, 8 * 3)) continue;

        uintptr_t entries[3] = {};
        if (!SafeRead((void*)data, entries, sizeof(entries))) continue;
        if (entries[0] == 0 || entries[1] == 0) continue;

        for (int nameOff : NAME_OFFSETS_TO_TRY) {
            if (CheckNameEntry(entries[0], nameOff, "None") &&
                CheckNameEntry(entries[1], nameOff, "ByteProperty")) {
                result.address = moduleBase + offset;
                result.data = data;
                result.count = count;
                result.nameFieldOffset = nameOff;
                Log("  GNames at offset 0x%llX, Count=%d, FNameEntry.Name at +0x%X",
                    (uint64_t)offset, count, nameOff);

                for (int i = 0; i < 16 && i < count; i++) {
                    Log("    [%d] = \"%s\"", i, ReadFName(result, i).c_str());
                }
                return result;
            }
        }
    }
    Log("  GNames NOT FOUND");
    return result;
}

// ============================================================================
// GObjects + UObject layout discovery
// ============================================================================

struct UObjectLayout {
    int internalIndexOffset;  // Offset of ObjectInternalInteger within UObject
    int nameOffset;           // Offset of FName within UObject
    int classOffset;          // Offset of UClass* within UObject
    int outerOffset;          // Offset of UObject* Outer within UObject
};

struct GObjectsResult {
    uintptr_t address;
    uintptr_t data;
    int32_t count;
    UObjectLayout layout;
};

static GObjectsResult FindGObjects(uintptr_t moduleBase, size_t moduleSize, const GNamesResult& gn) {
    GObjectsResult result = {};
    if (!gn.address) return result;
    Log("Scanning for GObjects...");

    for (size_t offset = 0; offset + 16 <= moduleSize; offset += 8) {
        if (moduleBase + offset == gn.address) continue;

        uintptr_t data = 0;
        int32_t count = 0;
        if (gModuleSnapshot) {
            memcpy(&data, gModuleSnapshot + offset, 8);
            memcpy(&count, gModuleSnapshot + offset + 8, 4);
        } else {
            if (!SafeRead((void*)(moduleBase + offset), &data, 8)) continue;
            if (!SafeRead((void*)(moduleBase + offset + 8), &count, 4)) continue;
        }
        if (data == 0 || count < 1000 || count > 2000000) continue;
        if (!IsReadable((void*)data, 8 * 10)) continue;

        // Check entries 1-4 for UObjects with InternalIndex matching array position
        int validCount = 0;
        int foundIdxOff = -1;
        for (int i = 1; i < 5; i++) {
            uintptr_t obj = 0;
            if (!SafeRead((void*)(data + i * 8), &obj, 8) || obj == 0) continue;
            if (!IsReadable((void*)obj, 0x80)) continue;
            uintptr_t vtable = 0;
            if (!SafeRead((void*)obj, &vtable, 8) || !IsReadable((void*)vtable, 64)) continue;

            for (int off = 0x08; off <= 0x40; off += 4) {
                int32_t idx = -1;
                if (SafeRead((void*)(obj + off), &idx, 4) && idx == i) {
                    validCount++;
                    foundIdxOff = off;
                    break;
                }
            }
        }

        if (validCount >= 2) {
            result.address = moduleBase + offset;
            result.data = data;
            result.count = count;
            result.layout.internalIndexOffset = foundIdxOff;
            Log("  GObjects at offset 0x%llX, Count=%d", (uint64_t)offset, count);
            Log("  UObject.InternalIndex at +0x%X", foundIdxOff);

            // Now discover FName offset within UObject.
            // The first non-null object at GObjects[1] should have FName index corresponding
            // to one of the early GNames entries. Scan UObject fields for a plausible FName
            // (int32 FNameEntryId that resolves to a known name, followed by int32 InstanceNumber=0).
            uintptr_t obj1 = 0;
            SafeRead((void*)(data + 1 * 8), &obj1, 8);
            if (obj1 != 0) {
                Log("  Probing UObject at 0x%llX for FName/Class/Outer fields...", (uint64_t)obj1);
                for (int off = 0x08; off <= 0x70; off += 4) {
                    int32_t nameId = -1, instNum = -1;
                    SafeRead((void*)(obj1 + off), &nameId, 4);
                    SafeRead((void*)(obj1 + off + 4), &instNum, 4);
                    if (nameId >= 0 && nameId < gn.count && instNum == 0) {
                        std::string name = ReadFName(gn, nameId);
                        if (!name.empty() && name != "<null>" && name != "<invalid>" && name.length() > 2) {
                            Log("    +0x%02X: FName? id=%d inst=%d -> \"%s\"", off, nameId, instNum, name.c_str());
                        }
                    }
                }
                // Also look for pointer fields that point to other GObjects entries (Class, Outer)
                for (int off = 0x08; off <= 0x70; off += 8) {
                    uintptr_t ptr = 0;
                    SafeRead((void*)(obj1 + off), &ptr, 8);
                    if (ptr == 0) continue;
                    // Check if this pointer is itself a UObject (has a vtable and InternalIndex)
                    if (!IsReadable((void*)ptr, 0x40)) continue;
                    uintptr_t vt = 0;
                    SafeRead((void*)ptr, &vt, 8);
                    if (!IsReadable((void*)vt, 64)) continue;
                    int32_t ptrIdx = -1;
                    SafeRead((void*)(ptr + foundIdxOff), &ptrIdx, 4);
                    if (ptrIdx >= 0 && ptrIdx < count) {
                        // Verify it's actually in GObjects at that index
                        uintptr_t verify = 0;
                        SafeRead((void*)(data + ptrIdx * 8), &verify, 8);
                        if (verify == ptr) {
                            Log("    +0x%02X: UObject* -> GObjects[%d] (0x%llX)", off, ptrIdx, (uint64_t)ptr);
                        }
                    }
                }
            }
            return result;
        }
    }
    Log("  GObjects NOT FOUND");
    return result;
}

// ============================================================================
// ProcessEvent finder
// ============================================================================

// Strategy: Find the address of the "CallFunction" error string in the binary.
// Then find the function that references it (CallFunction/FUN_140f5df50 equivalent).
// Then search vtables for functions that CALL that CallFunction address.
// The vtable entry index = ProcessEvent index.

static uintptr_t FindStringInModule(uintptr_t base, size_t /*size*/, const char* needle) {
    if (!gModuleSnapshot) return 0;
    size_t needleLen = strlen(needle);
    if (needleLen >= gModuleSnapshotSize) return 0;
    for (size_t i = 0; i <= gModuleSnapshotSize - needleLen; i++) {
        if (memcmp(gModuleSnapshot + i, needle, needleLen) == 0) return base + i;
    }
    return 0;
}

// Check if a function at 'funcAddr' contains a CALL or JMP to 'targetAddr' within its first 'maxBytes' bytes.
static bool FunctionCallsTarget(uintptr_t funcAddr, uintptr_t targetAddr, size_t maxBytes = 4096) {
    if (!IsReadable((void*)funcAddr, maxBytes)) return false;
    const uint8_t* p = (const uint8_t*)funcAddr;
    for (size_t i = 0; i + 5 <= maxBytes; i++) {
        if (p[i] == 0xE8 || p[i] == 0xE9) { // Relative CALL or JMP
            int32_t rel;
            memcpy(&rel, p + i + 1, 4);
            uintptr_t callTarget = funcAddr + i + 5 + (int64_t)rel;
            if (callTarget == targetAddr) return true;
        }
    }
    return false;
}

// Find the address of CallFunction by searching for the error string xref.
static uintptr_t FindCallFunction(uintptr_t moduleBase, size_t moduleSize) {
    // Search for the string "Error: CallFunction"
    uintptr_t strAddr = FindStringInModule(moduleBase, moduleSize,
        "Error: CallFunction - attempt to call invalid function");
    if (strAddr == 0) {
        Log("  Could not find CallFunction error string");
        return 0;
    }
    Log("  CallFunction error string at 0x%llX", (uint64_t)strAddr);

    // Now scan .text for a LEA instruction referencing this string using snapshot buffer.
    uintptr_t textBase = moduleBase + 0x1000; // Skip PE header
    size_t textSize = moduleSize - 0x1000;
    size_t textSnapOff = 0x1000; // offset into snapshot

    Log("  Scanning for LEA referencing CallFunction string...");
    for (size_t i = 0; i + 7 <= textSize; i++) {
        uint8_t b0 = gModuleSnapshot[textSnapOff + i];
        if (b0 != 0x48 && b0 != 0x4C) continue;
        uint8_t b1 = gModuleSnapshot[textSnapOff + i + 1];
        if (b1 != 0x8D) continue;
        uint8_t b2 = gModuleSnapshot[textSnapOff + i + 2];
        if ((b2 & 0x07) != 0x05) continue;
        if ((b2 & 0xC0) != 0x00) continue;

        int32_t rel;
        memcpy(&rel, gModuleSnapshot + textSnapOff + i + 3, 4);
        uintptr_t target = textBase + i + 7 + (int64_t)rel;
        if (target == strAddr) {
            // Found the instruction that loads the CallFunction error string.
            // The containing function is CallFunction.
            // Walk backwards to find the function start (look for common prologue patterns).
            uintptr_t instrAddr = textBase + i;
            Log("  LEA referencing CallFunction string at 0x%llX", (uint64_t)instrAddr);

            // Simple heuristic: search backwards for function prologue
            // Common x64 prologues: 40 55 (push rbp), 48 89 5C (mov [rsp+x], rbx),
            // 48 83 EC (sub rsp, imm8), 4C 89 44 (mov [rsp+x], r8)
            // Or just look for 0xCC (int3 padding) then the next instruction
            size_t snapPos = textSnapOff + i;
            for (size_t back = 1; back < 0x2000 && back <= snapPos; back++) {
                uint8_t prev = gModuleSnapshot[snapPos - back];
                if (prev == 0xCC || prev == 0xC3) { // int3 or ret = end of previous function
                    uintptr_t funcStart = moduleBase + snapPos - back + 1;
                    Log("  CallFunction likely starts at 0x%llX", (uint64_t)funcStart);
                    return funcStart;
                }
            }
            // Fallback: couldn't find start
            Log("  WARNING: Could not determine CallFunction start");
            return 0;
        }
    }
    Log("  Could not find xref to CallFunction string");
    return 0;
}

static void FindProcessEvent(const GObjectsResult& go, uintptr_t moduleBase, size_t moduleSize) {
    if (!go.address) {
        Log("Cannot find ProcessEvent without GObjects");
        return;
    }

    Log("--- Finding ProcessEvent ---");

    // Step 1: Find CallFunction address
    uintptr_t callFuncAddr = FindCallFunction(moduleBase, moduleSize);
    if (callFuncAddr == 0) {
        Log("  Failed to find CallFunction");
        return;
    }

    // Step 2: Get multiple UObject vtables from GObjects and find which vtable entry
    //         contains a function that calls CallFunction.
    Log("  Scanning vtable entries for functions that call CallFunction...");

    // Collect many different vtables to find the common ProcessEvent index
    const int MAX_VTABLES = 50;
    struct VTableInfo { uintptr_t vtable; int objIdx; };
    VTableInfo vtables[MAX_VTABLES] = {};
    int vtableCount = 0;

    // Sample objects spread across the whole GObjects array for diversity
    int step = go.count > 5000 ? go.count / 500 : 1;
    for (int i = 1; i < go.count && vtableCount < MAX_VTABLES; i += step) {
        uintptr_t obj = 0;
        SafeRead((void*)(go.data + i * 8), &obj, 8);
        if (obj == 0 || !IsReadable((void*)obj, 8)) continue;
        uintptr_t vt = 0;
        SafeRead((void*)obj, &vt, 8);
        if (!IsReadable((void*)vt, 8 * 130)) continue;

        // Avoid duplicates
        bool dup = false;
        for (int j = 0; j < vtableCount; j++) {
            if (vtables[j].vtable == vt) { dup = true; break; }
        }
        if (dup) continue;

        vtables[vtableCount++] = {vt, i};
    }

    Log("  Found %d unique vtables to scan", vtableCount);

    // For each vtable, check entries 0-120 for a function that calls CallFunction
    const int MAX_VTABLE_IDX = 128;
    int peIndexVotes[MAX_VTABLE_IDX] = {};
    for (int v = 0; v < vtableCount; v++) {
        for (int idx = 0; idx < MAX_VTABLE_IDX; idx++) {
            uintptr_t fn = 0;
            SafeRead((void*)(vtables[v].vtable + idx * 8), &fn, 8);
            if (fn == 0 || !IsReadable((void*)fn, 64)) continue;

            if (FunctionCallsTarget(fn, callFuncAddr)) {
                Log("    VTable[obj=%d] index %d (fn=0x%llX) calls CallFunction!",
                    vtables[v].objIdx, idx, (uint64_t)fn);
                peIndexVotes[idx]++;
            }
        }
    }

    // Find the most common index
    int bestIdx = -1, bestVotes = 0;
    for (int i = 0; i < 128; i++) {
        if (peIndexVotes[i] > bestVotes) {
            bestVotes = peIndexVotes[i];
            bestIdx = i;
        }
    }

    if (bestIdx >= 0) {
        Log("  ProcessEvent vtable index: %d (found in %d/%d vtables)", bestIdx, bestVotes, vtableCount);
    } else {
        Log("  ProcessEvent index NOT FOUND via CallFunction xref");
        Log("  Trying indirect: searching for functions that call functions that call CallFunction...");

        // CallFunction might be called via a wrapper. Search for functions that call CallFunction,
        // then check vtables for callers of THOSE functions.
        // Scan .text for CALL instructions targeting callFuncAddr
        uintptr_t textBase = moduleBase + 0x1000;
        size_t textSize = moduleSize - 0x1000;
        uintptr_t callers[32] = {};
        int callerCount = 0;

        Log("  Scanning .text for CALL instructions targeting CallFunction...");
        size_t textSnapOff2 = 0x1000;
        for (size_t i = 0; i + 5 <= textSize && callerCount < 32; i++) {
            if (gModuleSnapshot[textSnapOff2 + i] == 0xE8) { // Relative CALL
                int32_t rel;
                memcpy(&rel, gModuleSnapshot + textSnapOff2 + i + 1, 4);
                uintptr_t target = textBase + i + 5 + (int64_t)rel;
                if (target == callFuncAddr) {
                    // Find containing function start by walking backwards in snapshot
                    size_t snapPos = textSnapOff2 + i;
                    uintptr_t funcStart = 0;
                    for (size_t back = 1; back < 0x2000 && back <= snapPos; back++) {
                        uint8_t prev = gModuleSnapshot[snapPos - back];
                        if (prev == 0xCC || prev == 0xC3) {
                            funcStart = moduleBase + snapPos - back + 1;
                            break;
                        }
                    }
                    if (funcStart && funcStart != callFuncAddr) {
                        bool exists = false;
                        for (int c = 0; c < callerCount; c++)
                            if (callers[c] == funcStart) { exists = true; break; }
                        if (!exists) {
                            callers[callerCount++] = funcStart;
                            Log("    Function at 0x%llX calls CallFunction", (uint64_t)funcStart);
                        }
                    }
                }
            }
        }

        // Now check vtables for entries pointing to any of these callers
        memset(peIndexVotes, 0, sizeof(peIndexVotes));
        for (int v = 0; v < vtableCount; v++) {
            for (int idx = 0; idx < MAX_VTABLE_IDX; idx++) {
                uintptr_t fn = 0;
                SafeRead((void*)(vtables[v].vtable + idx * 8), &fn, 8);
                for (int c = 0; c < callerCount; c++) {
                    if (fn == callers[c]) {
                        Log("    VTable[obj=%d] index %d directly IS a CallFunction caller!", vtables[v].objIdx, idx);
                        peIndexVotes[idx]++;
                    }
                }
                // Also check if fn calls any of the callers
                if (fn != 0 && IsReadable((void*)fn, 64)) {
                    for (int c = 0; c < callerCount; c++) {
                        if (FunctionCallsTarget(fn, callers[c], 2048)) {
                            Log("    VTable[obj=%d] index %d calls wrapper of CallFunction", vtables[v].objIdx, idx);
                            peIndexVotes[idx]++;
                        }
                    }
                }
            }
        }

        bestIdx = -1;
        bestVotes = 0;
        for (int i = 0; i < 128; i++) {
            if (peIndexVotes[i] > bestVotes) {
                bestVotes = peIndexVotes[i];
                bestIdx = i;
            }
        }
        if (bestIdx >= 0) {
            Log("  ProcessEvent vtable index: %d (found in %d/%d vtables, indirect)", bestIdx, bestVotes, vtableCount);
        } else {
            Log("  ProcessEvent index could NOT be determined automatically.");
        }
    }

    // Dump first 3 vtables for manual analysis
    Log("\n  --- VTable dump (first 3, entries 0-%d) ---", MAX_VTABLE_IDX - 1);
    int dumpCount = vtableCount < 3 ? vtableCount : 3;
    for (int v = 0; v < dumpCount; v++) {
        Log("  VTable[obj=%d] at 0x%llX:", vtables[v].objIdx, (uint64_t)vtables[v].vtable);
        for (int idx = 0; idx < MAX_VTABLE_IDX; idx++) {
            uintptr_t fn = 0;
            SafeRead((void*)(vtables[v].vtable + idx * 8), &fn, 8);
            if (fn != 0) {
                Log("    [%3d] 0x%llX (offset 0x%llX)", idx, (uint64_t)fn,
                    (fn >= moduleBase && fn < moduleBase + moduleSize) ? (uint64_t)(fn - moduleBase) : 0ull);
            }
        }
    }
}

// ============================================================================
// Main
// ============================================================================

static void CreateModuleSnapshot(uintptr_t base, size_t size) {
    gModuleSnapshot = (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!gModuleSnapshot) {
        Log("  WARNING: Could not allocate snapshot buffer, scans will be slow");
        return;
    }
    // Copy page by page, zeroing unreadable pages
    gModuleSnapshotSize = size;
    for (size_t off = 0; off < size; off += 4096) {
        size_t chunk = (off + 4096 <= size) ? 4096 : size - off;
        if (IsReadable((void*)(base + off), chunk)) {
            __try { memcpy(gModuleSnapshot + off, (void*)(base + off), chunk); }
            __except(EXCEPTION_EXECUTE_HANDLER) { memset(gModuleSnapshot + off, 0, chunk); }
        } else {
            memset(gModuleSnapshot + off, 0, chunk);
        }
    }
    Log("  Snapshot created: %llu MB", size / (1024 * 1024));
}

static void FinderThread() {
    Sleep(8000); // Wait for game to load

    char dllPath[MAX_PATH];
    GetModuleFileNameA(gModule, dllPath, MAX_PATH);
    std::string logPath(dllPath);
    auto pos = logPath.find_last_of("\\/");
    if (pos != std::string::npos) logPath = logPath.substr(0, pos);
    logPath += "\\OffsetFinder.log";

    gLog = _fsopen(logPath.c_str(), "w", _SH_DENYWR); // Allow external reads
    if (!gLog) return;

    Log("=== UE3 Offset Finder v2 for XCOM 2 WOTC ===");

    ModuleInfo mod = GetMainModule();
    Log("Module: base=0x%llX size=0x%llX (%llu MB)",
        (uint64_t)mod.base, (uint64_t)mod.size, mod.size / (1024 * 1024));

    // Snapshot entire module into local buffer for fast scanning
    Log("Creating module snapshot for fast scanning...");
    CreateModuleSnapshot(mod.base, mod.size);

    // Step 1: GNames
    Log("\n--- Step 1: GNames ---");
    GNamesResult gnames = FindGNames(mod.base, mod.size);
    if (gnames.address)
        Log("  GNames offset: 0x%llX", (uint64_t)(gnames.address - mod.base));

    // Step 2: GObjects + UObject layout
    Log("\n--- Step 2: GObjects + UObject layout ---");
    GObjectsResult gobjects = FindGObjects(mod.base, mod.size, gnames);
    if (gobjects.address)
        Log("  GObjects offset: 0x%llX", (uint64_t)(gobjects.address - mod.base));

    // Step 3: ProcessEvent
    Log("\n--- Step 3: ProcessEvent ---");
    FindProcessEvent(gobjects, mod.base, mod.size);

    // Summary
    Log("\n=== SUMMARY ===");
    Log("Module base: 0x%llX", (uint64_t)mod.base);
    if (gnames.address)
        Log("GNames: offset=0x%llX, FNameEntry.Name at +0x%X, UTF-8",
            (uint64_t)(gnames.address - mod.base), gnames.nameFieldOffset);
    if (gobjects.address) {
        Log("GObjects: offset=0x%llX", (uint64_t)(gobjects.address - mod.base));
        Log("UObject.InternalIndex: +0x%X", gobjects.layout.internalIndexOffset);
    }

    if (gModuleSnapshot) {
        VirtualFree(gModuleSnapshot, 0, MEM_RELEASE);
        gModuleSnapshot = nullptr;
    }

    fclose(gLog);
    gLog = nullptr;

    MessageBoxA(nullptr, "Offset scan v2 complete!\nCheck OffsetFinder.log", "UE3 Offset Finder", MB_OK);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        gModule = hModule;
        DisableThreadLibraryCalls(hModule);
        std::thread(FinderThread).detach();
    }
    return TRUE;
}
