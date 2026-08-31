#pragma once

// Game-UI access, the DEV-HARNESS half: raw object access for the /gui and /props dumps, the
// /flash endpoint's path-API call, and the console bridge. Implemented in game_ui_dev.cpp,
// which — like dev_server.cpp — is only compiled into a dev build.

#include "game_ui_core.h"

#ifdef X2ACCESS_DEVSERVER

namespace GameUI {

// These are the four primitives the widget-tree dumper cannot get any other way; everything
// else it needs it reads through UE3 reflection, so it hand-transcribes no offsets of its own.

/// The screen stack's entry for `className`, or the TOPMOST usable screen when `className` is
/// null or empty. Same inited/not-removed filter the recipes use. Game thread only.
void* FindStackScreen(const char* className);

/// Every live instance of `className` in GObjects, in scan order, up to `maxItems`; returns the
/// count written. No usability filter, so it reaches objects the screen stack never sees
/// (XCom3DCursor, a cheat manager, a tooltip manager). Matching is by CLASS POINTER with class
/// default objects excluded by name — deliberately not by the object's own name, which the
/// recipes' finders can rely on only because screens happen to keep the default naming.
/// ALL of them are returned rather than one, because with several alive the dumper has no basis
/// for picking and the caller deserves to see the ambiguity. Game thread only.
int FindInstances(const char* className, void** out, int maxItems);

/// The CLASS DEFAULT OBJECT of `className` (`Default__<className>`), or null when the class is
/// not loaded. FindInstances deliberately skips CDOs so one cannot shadow a live screen, but for
/// the companion classes the CDO is the only instance there ever is — X2AccessObjectScanner,
/// X2AccessTileReader and X2AccessNarrator keep their entire model on it — so without this the
/// dumper cannot reach the objects this mod's own subsystems actually use. Game thread only.
void* FindDefaultObject(const char* className);

/// Is this pointer a live entry in GObjects? The dumpers take a `?ptr=` from a previous dump,
/// and a pointer that has since been collected must be refused rather than walked.
bool IsLiveObject(void* obj);

/// UIPanel::ChildPanels into `out`; returns the count written.
int ReadChildPanels(void* panel, void** out, int maxItems);

/// One call into GFxMoviePlayer's path API, for the harness's /flash endpoint.
///
/// Every one of those functions has the SAME parameter shape — an FString path in at 0x00, the
/// answer at 0x10 (pinned in sdk_layout_checks.cpp against the generated param structs). So the
/// only thing that varies is the return KIND, and it is decided by the name rather than guessed
/// at. That distinction matters: `GetVariable` answers with an FASValue struct and `Invoke`
/// takes an ARRAY of them, so reading either through this block would print a plausible-looking
/// number that is really half a pointer. Unknown names are refused.
///
/// ⚠ An unresolved path and an empty field answer ALIKE here (empty string, 0, false) — the
/// distinction the first live probe used was that the real `Selection` accessors answer -1 when
/// nothing is focused while an unresolved path answers 0. Ask for something you can predict.
struct FlashValue {
    enum class Kind { Void, Text, Number, Int, Bool };
    bool called = false;  // the UFunction was found and ProcessEvent ran
    std::string error;    // why not, when it did not
    Kind kind = Kind::Text;
    std::string text;  // Kind::Text
    double number = 0; // Kind::Number / Int / Bool (1 or 0)
};

/// `movie` is the UIMovie / GFxMoviePlayer to ask; `path` is an ActionScript path in it.
/// Costs one outbound call, and for a string answer leaks the engine-allocated FString it comes
/// back in — the same bargain ConsoleCommand makes, and the reason this is dev-only.
/// Game thread only.
FlashValue FlashCall(void* movie, const char* funcName, const char* path);

/// Run `command` through the game's own console (`Actor.ConsoleCommand` on the local player
/// controller) and return what it printed — empty for the many commands that say nothing, or a
/// `[exec] …` string when the call could not be made at all. This is the whole `exec function`
/// surface (1246 of them across the SDK, the cheat managers included) reachable by name, which
/// is why the dev harness needs no scripting bridge of its own.
///
/// The reply comes back in an engine-allocated FString that we read and deliberately leak; see
/// the implementation. Game thread only.
std::string ConsoleCommand(const std::string& command);

} // namespace GameUI

#endif // X2ACCESS_DEVSERVER
