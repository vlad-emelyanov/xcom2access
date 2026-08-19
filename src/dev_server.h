#pragma once

// Loopback HTTP dev harness (phase 1: /health, /speech, /log, /status).
//
// Sibling of the RTAccess (port 8772) and CyberAccess (8773) harnesses; this one owns 8774, so
// all three can run side by side. The point is to make a live session observable and scriptable
// from outside the game: what the mod SPOKE, what the screen stack actually looks like, and
// whether the game thread is still pumping — none of which a screenshot or a log tail answers
// with a cursor you can poll against.
//
// Double-gated, deliberately:
//   compile — the CMake option X2ACCESS_DEVSERVER. OFF means dev_server.cpp is not built at
//             all, so a release .asi contains no socket code whatsoever; the entry points below
//             degrade to inline no-ops purely so the four call sites stay readable.
//   runtime — a marker FILE next to the .asi: Binaries/Win64/scripts/X2Access.devserver
//             (optional first line = port override). A file, not an environment variable: the
//             game is launched by Steam and by `just run`, and neither inherits our environment.
//
// Threading contract (the part that matters):
//   * The accept thread owns the socket and NEVER touches engine memory. Every engine-facing
//     endpoint queues a job that Pump() drains on the GAME thread — the hard rule in CLAUDE.md
//     about outbound ProcessEvent calls applies to reads through GameUI too.
//   * /health, /speech and /log answer ON the accept thread, from process-memory rings. That is
//     what makes them useful when the game thread is the thing that is broken: during a load
//     movie, a stall, or a hang, they still answer while /status times out.

#include <string>

namespace DevServer {

#ifdef X2ACCESS_DEVSERVER

/// Init thread, after Log::Init and the game hooks. `asiDir` is the directory holding the .asi
/// (and therefore the marker file). No-op unless the marker exists and the loopback bind
/// succeeds; every failure logs and degrades — the harness must never break the load path.
void Start(const std::string& asiDir);

/// Process detach. Releases callers blocked on jobs that will never run, closes the sockets and
/// waits BOUNDED for the accept thread (see the .cpp: we are under the loader lock here, so an
/// unbounded join is a deadlock). Idempotent.
void Stop();

/// Game thread, once per frame. Near-zero cost while no request is pending; also stamps the
/// liveness clock that /health reports.
void Pump();

/// Any thread. Feeds the /speech ring from the TTS::Speak chokepoint, so what the harness
/// reports is literally what was handed to the screen reader — not a re-derivation of it.
void RecordSpeech(const std::string& text, bool interrupt);

#else

inline void Start(const std::string&) {}
inline void Stop() {}
inline void Pump() {}
inline void RecordSpeech(const std::string&, bool) {}

#endif // X2ACCESS_DEVSERVER

} // namespace DevServer
