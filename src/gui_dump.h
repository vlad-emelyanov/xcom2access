#pragma once

// Live widget-tree and property dumps for the dev harness (/gui and /props).
//
// This is the tool that replaces reading the SDK's SrcOrig and transcribing offsets out of the
// generated dump's comments: it reports what the RUNNING game actually has, at the offsets it
// actually has it, under whatever mod set is loaded. That distinction is not academic — the
// Community Highlander recompiles XComGame from SrcOrig and UIShellSoundtrackPicker loses a
// localized FString there, moving every field after 0x430 down 0x10.
//
// Everything here is reflection-driven (UStruct::Children walks), so it hand-transcribes no
// offsets of its own; the four raw primitives it cannot get that way come from GameUI.
//
// Dev builds only — the whole TU is out of a release .asi.

#ifdef X2ACCESS_DEVSERVER

#include <string>
#include <vector>

namespace GuiDump {

/// How the caller named the object to dump. Checked in this order: an explicit address from an
/// earlier dump, then a class default object, then the newest instance of a class, then the
/// screen stack.
struct Selector {
    void* ptr = nullptr;  // raw address (validated against GObjects before it is walked)
    std::string cdo;      // class default object (Default__<Class>) — the companion CDOs
    std::string instance; // newest live instance of this class, anywhere in GObjects
    std::string screen;   // class name on the screen stack; empty (with nothing else) = topmost
};

/// The widget tree under the selected panel: one line per UIPanel, with its class, address,
/// state bits and the text it carries. Game thread only.
std::string Tree(const Selector& sel, int maxDepth);

/// Every property the selected object's class chain declares — offset, type, name and live
/// value — grouped by declaring class. `filter` (optional, case-insensitive) keeps only
/// properties whose name contains it. Game thread only.
std::string Props(const Selector& sel, const std::string& filter);

/// Invoke a named script function on the selected object, and report what was found either way.
///
/// This exists because a recurring shape of accessibility bug in this game is not a missing
/// feature but an unreachable one: the game already has the function, and only calls it from a
/// branch a mouse+keyboard player never takes — `UIFacilityGrid.InitializeNavigationGrid` (the
/// whole ant-farm adjacency graph) sits behind `ISCONTROLLERACTIVE`, and the to-do widget's
/// list and notification screen behind mouse handlers. Being able to call one from outside
/// turns "would ungating this work?" into a measurement instead of a build-deploy-relaunch.
///
/// `args` supplies the function's arguments, in declaration order, as text — one per declared
/// argument or none at all. Supplying them is what makes the Flash bridge reachable from here:
/// `GFxMoviePlayer.GetVariableString(path)` and `ActionScriptInt(path)` are how anything about a
/// Scaleform movie gets read, and both take a path string. Passing one is SAFE for the reason
/// CLAUDE.md's ProcessEvent notes give: parameters are shallow-memcpy'd into the engine's frame
/// and the destroy pass skips every property below `ParmsSize`, so a buffer we own and keep
/// alive across the call is all that is required.
///
/// ⛔ Only value arguments a caller can actually write are accepted — string, int, float, bool,
/// byte. An OBJECT, struct, array, name or delegate argument is refused, because the only thing
/// this endpoint could put there is a zero, and a zeroed object pointer is a null dereference
/// inside script: a RedScreen or a crash in the very session the endpoint exists to observe.
///
/// A declared RETURN value is not an argument and never disqualifies a call: the engine writes
/// it into our parameter block and reads nothing from it, so the value is reported back.
/// Resolves only class-scope functions; a state-scoped one needs GameUI::FindStateFunction.
/// Game thread only, which is the same rule every outbound ProcessEvent call in this project
/// obeys.
std::string Call(const Selector& sel, const std::string& funcName,
    const std::vector<std::string>& args);

} // namespace GuiDump

#endif // X2ACCESS_DEVSERVER
