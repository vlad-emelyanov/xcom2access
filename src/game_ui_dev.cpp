// The DEV-HARNESS half of GameUI: raw object access for the /gui and /props dumps, the /flash
// endpoint's path-API call, and the console bridge. Compiled only into a dev build, exactly like
// dev_server.cpp — a release .asi contains none of this.

#include "game_ui_dev.h"

#ifdef X2ACCESS_DEVSERVER

#include "game_ui_internal.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Core reflection types only (UObject, UClass, FName, TArray) — the same proven set game_ui.cpp
// uses; see its include comment for why the full generated SDK stays out.
#include "GameDefines.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

#include <cstring>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GameUI {

namespace {

// Classes derived from one named class, for the dev dumps' ?instance= lookup.
constexpr int kMaxDerivedClasses = 64;

} // namespace

// ---- console commands -------------------------------------------------------------------------

std::string ConsoleCommand(const std::string& command)
{
    if (command.empty())
        return "[exec] empty command";

    // WorldInfo.GetWorldInfo() is `native final static` — the one way into the world that needs
    // no object cached across frames, which is exactly the rule about not holding UObject
    // pointers. GetALocalPlayerController is declared on Actor, and WorldInfo is an Actor.
    struct {
        void* ReturnValue;
    } world{};
    if (!CallStaticNative("WorldInfo", "GetWorldInfo", &world) || !world.ReturnValue)
        return "[exec] no WorldInfo (world not up yet?)";

    struct {
        void* ReturnValue;
    } pc{};
    if (!CallNamedFunction(world.ReturnValue, "GetALocalPlayerController", &pc) || !pc.ReturnValue)
        return "[exec] no local PlayerController";

    // Actor.ConsoleCommand(string Command, optional bool bWriteToLog) : string, FUNC_Native.
    // Layout is transcribed from the SDK dump comments and is PACK(4): ReturnValue sits at 0x14,
    // immediately after the bool bitfield with no padding, so the FString pointer is 4-byte
    // aligned. Natural alignment would put it at 0x18 and every call would read garbage.
#pragma pack(push, 4)
    struct ConsoleParams {
        wchar_t* CmdData;  // FString Command    @0x00
        int32_t CmdCount;
        int32_t CmdMax;
        uint32_t bWriteToLog; // bitfield        @0x10
        wchar_t* RetData;  // FString ReturnValue @0x14
        int32_t RetCount;
        int32_t RetMax;
    };
#pragma pack(pop)
    static_assert(sizeof(ConsoleParams) == 0x24, "Actor.ConsoleCommand params must be 0x24 bytes");
    static_assert(offsetof(ConsoleParams, bWriteToLog) == 0x10, "bWriteToLog must sit at 0x10");
    static_assert(offsetof(ConsoleParams, RetData) == 0x14, "ReturnValue FString must sit at 0x14");

    // Safe to hand the engine our own buffer: ProcessEvent (moduleBase+0x143B50, decompiled)
    // shallow-memcpy's the whole parameter block into its alloca'd frame and only ever calls
    // DestroyValue on properties whose Offset is >= ParmsSize — true locals, never parameters.
    // So nothing frees this, and it must merely outlive the call.
    std::wstring wide = Utf8ToWide(command);

    ConsoleParams params{};
    params.CmdData = wide.data();
    params.CmdCount = static_cast<int32_t>(wide.size()) + 1; // FString counts the terminator
    params.CmdMax = params.CmdCount;
    params.bWriteToLog = 1; // also lands in the game's Launch.log, where `just gamelog` finds it

    if (!CallNamedFunction(pc.ReturnValue, "ConsoleCommand", &params))
        return "[exec] ConsoleCommand did not resolve or faulted";

    // The native writes the return FString straight into our block (ProcessEvent passes
    // Parms + ReturnValueOffset as the result pointer), allocated with the ENGINE's allocator.
    // We deliberately leak it: freeing it needs appFree, and a dev-only endpoint leaking a few
    // dozen bytes per call is a far better trade than calling the wrong deallocator.
    std::vector<wchar_t> buf(64 * 1024);
    int len = CopyFStringRaw(&params.RetData, buf.data(), static_cast<int>(buf.size()));
    if (len <= 0)
        return {}; // ran fine, said nothing — most commands are silent
    return WcharToUtf8(buf.data(), len);
}

void* FindStackScreen(const char* className)
{
    if (className && className[0])
        return FindStackScreenRaw(className);

    // No class asked for: the topmost screen the recipes would consider. Index 0 is the top
    // (Push inserts there), but the entry above a live screen can be a corpse the stack has
    // not dropped yet, so skip on the same predicate the finders use rather than taking [0]
    // blind.
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count; i++) {
        if (screens[i] && StackEntryLiveRaw(screens[i]))
            return screens[i];
    }
    return nullptr;
}

int FindInstances(const char* className, void** out, int maxItems)
{
    if (!className || !className[0] || maxItems <= 0)
        return 0;
    UObject* classObj = FindClassRaw(className);
    if (!classObj)
        return 0;

    // Two passes, because matching only the exact class is a trap: asking for XCom3DCursor in a
    // live mission found NOTHING, since the cursor the game spawns is an XCom3DCursorMouse. The
    // script side takes subclasses for granted (`local XCom3DCursor Cursor` binds the subclass
    // fine), so an inspector that does not is wrong in the way that looks like an absent object.
    //
    // Pass 1 collects the class objects that DERIVE from the named one — a few thousand class
    // objects walked, not four hundred thousand instances — so pass 2 stays a pointer compare.
    void* derived[kMaxDerivedClasses] = {};
    int classCount = 0;
    int found = 0;
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return 0;

        derived[classCount++] = classObj;
        char clsName[64];
        for (int32_t i = 0; i < objects->size() && classCount < kMaxDerivedClasses; i++) {
            UObject* obj = objects->at(i);
            if (!obj || obj == classObj)
                continue;
            UClass* meta = obj->Class;
            if (!meta || !ObjNameAnsiRaw(reinterpret_cast<UObject*>(meta), clsName, sizeof(clsName))
                || strcmp(clsName, "Class") != 0)
                continue; // not a class object
            // Bounded: a UE3 class chain is a dozen deep, and a corrupt SuperField must not spin.
            UStruct* super = reinterpret_cast<UStruct*>(obj)->SuperField
                ? reinterpret_cast<UStruct*>(reinterpret_cast<UStruct*>(obj)->SuperField)
                : nullptr;
            for (int step = 0; super && step < 32; step++) {
                if (reinterpret_cast<UObject*>(super) == classObj) {
                    derived[classCount++] = obj;
                    break;
                }
                super = reinterpret_cast<UStruct*>(super->SuperField);
            }
        }

        char name[80];
        for (int32_t i = 0; i < objects->size() && found < maxItems; i++) {
            UObject* obj = objects->at(i);
            if (!obj)
                continue;
            bool isKind = false;
            for (int c = 0; c < classCount; c++) {
                if (reinterpret_cast<UObject*>(obj->Class) == derived[c]) {
                    isKind = true;
                    break;
                }
            }
            if (!isKind)
                continue;
            // The class default object is an instance of its own class and would otherwise be
            // the first hit for every lookup; its name is the one thing that distinguishes it.
            if (ObjNameAnsiRaw(obj, name, sizeof(name)) && strncmp(name, "Default__", 9) == 0)
                continue;
            out[found++] = obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return found;
}

void* FindDefaultObject(const char* className)
{
    if (!className || !className[0])
        return nullptr;
    // A CDO is named Default__<Class>, and that name is exactly what FindInstances excludes it
    // by — so the name IS the lookup here. One pass, oldest first; there is precisely one per
    // class, created at package load.
    std::string expected = std::string("Default__") + className;
    return ScanForObjectNamedRaw(expected.c_str());
}

bool IsLiveObject(void* obj)
{
    if (!obj)
        return false;
    __try {
        TArray<UObject*>* objects = UObject::GObjObjects();
        if (!objects)
            return false;
        for (int32_t i = objects->size() - 1; i >= 0; i--) {
            if (objects->at(i) == reinterpret_cast<UObject*>(obj))
                return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

int ReadChildPanels(void* panel, void** out, int maxItems)
{
    // The cap is the dumper's own buffer, and a /gui listing that silently stops short would
    // send someone hunting for a widget the screen does have.
    return TakeArrayRaw("Dev: ChildPanels", panel, kPanelChildPanelsOffset, out, maxItems);
}

FlashValue FlashCall(void* movie, const char* funcName, const char* path)
{
    FlashValue v;
    if (!funcName || !funcName[0]) {
        v.error = "no function named";
        return v;
    }
    if (!movie) {
        v.error = "no movie to ask (the panel's Movie is null — not inited yet?)";
        return v;
    }

    // Whitelist, because the block below is a LAYOUT and not a guess. The two deliberately
    // excluded neighbours are the ones whose shape differs: GetVariable returns an FASValue
    // struct, and Invoke takes an array of them as a second parameter.
    const std::string fn = funcName;
    if (fn == "GetVariableString" || fn == "ActionScriptString")
        v.kind = FlashValue::Kind::Text;
    else if (fn == "GetVariableNumber" || fn == "ActionScriptFloat")
        v.kind = FlashValue::Kind::Number;
    else if (fn == "ActionScriptInt")
        v.kind = FlashValue::Kind::Int;
    else if (fn == "GetVariableBool")
        v.kind = FlashValue::Kind::Bool;
    else if (fn == "ActionScriptVoid")
        v.kind = FlashValue::Kind::Void;
    else {
        v.error = fn
            + " is not one of the path functions this endpoint knows the return shape of. "
              "Use GetVariableString / GetVariableNumber / GetVariableBool / ActionScriptString "
              "/ ActionScriptInt / ActionScriptFloat / ActionScriptVoid — GetVariable (an "
              "FASValue struct) and Invoke (an array of them) have a different parameter block "
              "and would be misread here";
        return v;
    }

    uint8_t block[64] = {};
    if (!FlashCallRaw(movie, funcName, path ? path : "", block, sizeof(block))) {
        v.error = "GFxMoviePlayer." + fn
            + " could not be resolved or called on that object (is it really a movie?)";
        return v;
    }
    v.called = true;

    switch (v.kind) {
    case FlashValue::Kind::Text: {
        wchar_t buf[4096];
        const int len = CopyFStringRaw(block + kFlashRetOffset, buf, 4096);
        if (len > 0)
            v.text = WcharToUtf8(buf, len);
        break;
    }
    case FlashValue::Kind::Number:
        v.number = *reinterpret_cast<const float*>(block + kFlashRetOffset);
        break;
    case FlashValue::Kind::Int:
        v.number = *reinterpret_cast<const int32_t*>(block + kFlashRetOffset);
        break;
    case FlashValue::Kind::Bool:
        // UBOOL returns arrive as bit 0 of the dword, which is how the generated param struct
        // declares it (`bool ReturnValue : 1`).
        v.number = (*reinterpret_cast<const uint32_t*>(block + kFlashRetOffset) & 1u) ? 1 : 0;
        break;
    case FlashValue::Kind::Void:
        break;
    }
    return v;
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif // X2ACCESS_DEVSERVER
