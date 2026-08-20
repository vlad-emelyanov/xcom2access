#pragma once

// Tutorial message reader — the text half of making Tutorial_01 playable blind.
//
// The tutorial is a REPLAY (XComTutorialMgr extends XComReplayMgr): it plays a recorded session
// back and halts at every frame that needed player input. Everything the tutorial says arrives on
// one of four surfaces, and this reader owns the three that carry WORDS:
//
//   1. the modal popup       UITutorialBox        — real script properties, read directly
//   2. the anchored blade    UIWorldMessageMgr    — retained in m_arrMsgs, read by POLLING
//   3. the bottom-right HUD blade — Flash-only text; presence detected here, words recovered by
//      the companion (X2AccessTutorial) out of the history that produced it
//
// The fourth surface is the 3D world markers, and it belongs to the object scanner: they are
// world-space things with a location, which is exactly what the scanner enumerates.
//
// ⚠️ Surface 2 corrects a documented conclusion. CLAUDE.md says the flyover tap
// (UIWorldMessageMgr.Message) "cannot be built this way" — true of HOOKING, because those calls
// are script→script and never reach our ProcessEvent vtable slot. But the manager RETAINS every
// message it is showing, so polling reaches what hooking cannot.
//
// Nothing here steers. The reader claims exactly one key (re-read), and the modal is dismissed
// with the game's own Enter / Space / Escape — deliberately NOT claimed, because swallowing them
// would leave a blind player inside a modal they cannot close.

#include <string>

namespace TutorialReader {

/// Register the re-read claim. Call during init, BEFORE the game hooks go live.
void Init();

/// Game-tick pump: poll the tutorial's text surfaces and announce what changed. Throttled
/// internally. Game thread only (it makes outbound ProcessEvent calls).
void OnTick();

#ifdef X2ACCESS_DEVSERVER
/// The reader's whole state as text: replay frame, gate, rejection count, the modal, and every
/// active world message with its type and id. Diagnostics only. Game thread only.
std::string Describe();
#endif

} // namespace TutorialReader
