#pragma once

#include <cstdint>

// Which retail build of XCom2.exe we are inside, and where its globals are.
//
// There is more than one. Everything this mod pins was measured against the STEAM image; the GOG
// release is the same source RELINKED (PE timestamp 2018-09-13 against Steam's 2022-02-22, entry
// points 0x90 apart), so every one of those addresses moved. The measured table and the method
// that produced it are `docs/engine-notes.md`; `tools/derive_offsets.py` re-runs it against any
// third build, given that build's exe.
//
// ⛔ An unknown image is NOT refused. It gets the reference build's offsets and a loud log line,
// because refusing would turn a re-signed or repacked copy of a build we do support into a mod
// that does not load — and the offsets are validated anyway: `SDK::Init` reads GNames and GObjects
// through sanity heuristics before anything else proceeds, so a wrong guess fails as a named
// validation error rather than a wild read.
namespace GameBuild {

struct Build {
    const char* name;
    uint32_t timestamp;   // PE FileHeader.TimeDateStamp — the build's identity
    uint32_t sizeOfImage; // OptionalHeader.SizeOfImage — second half of the fingerprint
    uintptr_t gnames;
    uintptr_t gobjects;
    uintptr_t processEvent;
    uintptr_t moviePlayerGlobal;
    uintptr_t moviePlayerVtable;
};

// The offsets to use. Never null; falls back to the reference build (see above).
const Build& Current();

// True when the running image's fingerprint actually appears in the table.
bool IsKnown();

// "GOG (timestamp 0x5B9A17C4, image 0x20CA000)" — for the log, and for a bug report that has to
// identify the build without the reporter knowing what a build is.
const char* Fingerprint();

} // namespace GameBuild
