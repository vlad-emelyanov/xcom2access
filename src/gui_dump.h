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

namespace GuiDump {

/// How the caller named the object to dump. Checked in this order: an explicit address from an
/// earlier dump, then the newest instance of a class, then the screen stack.
struct Selector {
    void* ptr = nullptr;  // raw address (validated against GObjects before it is walked)
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

} // namespace GuiDump

#endif // X2ACCESS_DEVSERVER
