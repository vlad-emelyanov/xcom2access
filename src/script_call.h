#pragma once

// The companion bridge: calling UnrealScript from native, and cleaning what it hands back.
//
// This is the half of `game_ui.h` that has nothing to do with widgets. A tactical reader talks
// to SCRIPT — it asks `X2AccessTileReader` what is on a tile, it asks `X2AccessUnitReader` about
// the selected soldier — and it has no business reaching for `StrategyMapView` or `TrayIcon`.
// Six TUs (free_aim, narrator, object_scanner, unit_info, mouse_gate, transient_reader) used
// between two and three of the declarations below and were including all ~1460 lines of the UI
// interface to get them; now the include graph says what each layer is allowed to know.
//
// This header is the script-bridge module's interface; the implementation lives in game_ui.cpp
// (the GameUI core TU), next to the SEH-guarded reflection primitives it is built on, which the
// family split extracted behind game_ui_internal.h.
//
// ⛔ EVERYTHING HERE IS GAME THREAD ONLY — outbound ProcessEvent from the Tick drain, never from
// the WNDPROC or the init thread (CLAUDE.md, Hard rules).

#include <string>

namespace GameUI {

// ---- outbound calls into companion-mod script ----

/// The class default object of a script class, found by its "Default__<ClassName>" object
/// name. Cached; revalidated by re-reading the object's name each call; a full GObjects scan
/// runs at most once a second while the object is genuinely missing — and NOT AT ALL while the
/// companion package is known absent: a miss is followed by one package check, logged once, and
/// every caller answers null quietly for the next 30 s. Game thread only.
void* FindCompanionCDO(const char* className);

/// Whether the companion's script package (the UPackage named X2AccessCompanion) is loaded at
/// all — a fresh GObjects scan each call, so ask once, at a moment that settles the question:
/// the first main-menu attach. The package loads with the shell, later than DLC enumeration, so
/// this call is ALSO the settle moment for FindCompanionCDO: before it a miss is "not yet" and
/// nothing is cached; after it a miss is an absence, cached for 30 s. This is the startup
/// announcement's question; readers keep asking FindCompanionCDO. Game thread only.
bool CompanionPackageLoaded();

/// Resolve `funcName` on the object's class chain (cached per class) and ProcessEvent it.
/// `params` must match the UFunction's parameter layout. Game thread only.
bool CallScriptFunction(void* obj, const char* funcName, void* params);

/// The whole CDO-reader pattern in one call: find the companion CDO, ProcessEvent the entry
/// point, read the payload back — the sequence every narrate-only surface used to hand-roll.
/// `params` may be null for a no-parameter function (a zeroed block is supplied, because
/// ProcessEvent copies ParmsSize bytes regardless). False when the CDO is missing
/// (FindCompanionCDO logs that) or the call faulted or failed to resolve (logged here, with the
/// class and function). On success `result`, when non-null, receives the RAW NativeResult
/// payload — cleaning stays the CALLER's, deliberately: the tutorial reader must translate its
/// <img> markup rather than strip it, and the wire parser takes CleanText as a function pointer.
/// ⛔ Not for a consumer that must act BETWEEN the call and the read — the tile reader and the
/// free-aim reader engage the mouse gate there and keep their own sequence. Game thread only.
bool CallCompanion(const char* className, const char* funcName, void* params,
    std::string* result);

/// The UFunction object for `className.funcName`, resolved through the class object's field
/// chain (supers included) — for callers that need the function OBJECT rather than a call
/// (the mouse gate rewrites its dispatch fields). Cached; a missing class is retried at most
/// once a second. Returns null until resolvable. Game thread only.
void* FindScriptFunction(const char* className, const char* funcName);

/// The UFunction object for a function declared inside a `state`, which lives one level deeper
/// than FindScriptFunction looks: the state is itself a UStruct on the class's field chain, and
/// its functions hang off that. Needed because the same function name can be declared once per
/// state (XComTacticalInput declares Mouse_CheckForFreeAim in both targeting states), so the
/// state must be named to pick one. Cached per triple; returns null until resolvable.
/// Game thread only.
void* FindStateFunction(const char* className, const char* stateName, const char* funcName);

// ---- outbound calls into the GAME's own script (not the companion's) ----

/// `Object.Localize(SectionName, KeyName, PackageName)` — the game's own localization lookup.
///
/// Empty when the key is absent, which callers must treat as "the game has no words for this"
/// rather than as an error: the engine's own consumers fall back to the raw key, and a caller
/// that would rather stay silent than speak an asset name is making a different call. Use this
/// wherever the words are in a loc FILE rather than on a widget — a built-in English table would
/// be wrong the moment the player is not.
std::string LocalizeString(const char* section, const char* key, const char* package);

/// The game's Show Subtitles option, read off the ENGINE (`UEngine.bSubtitlesEnabled`) rather
/// than off the profile, because the engine's copy is what the game itself tests before drawing
/// a subtitle — the profile can be one toggle ahead of it. True when it cannot be determined:
/// erring toward speaking beats a surface that goes silently missing.
bool SubtitlesEnabled();

// ---- and the cleaning every payload that comes back needs ----

/// Minimal P10 text cleaning: strip markup tags, collapse whitespace.
///
/// It rides in this header rather than the UI one because that is where its callers are: every
/// string a companion function returns is a GAME string, coloured and marked up exactly like the
/// ones on a widget, and all three script-bridge consumers outside game_ui.cpp (avenger.cpp,
/// dropship.cpp, transient_reader.cpp) apply it to a payload rather than to a widget read. The
/// wire parser takes it as a function POINTER for the same reason — one implementation of "strip
/// the markup", kept outside the parser so the parser stays engine-free.
std::string CleanText(const std::string& text);

} // namespace GameUI
