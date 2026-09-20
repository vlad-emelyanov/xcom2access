#include "game_build.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

// The generated SDK still carries the reference build's GNames/GObjects as macros. They are the
// same two numbers as the first table row below, and the `static_assert` under the table is what
// keeps them so: regenerating the SDK against a different dump becomes a build error here instead
// of a silent disagreement between the table and the header everything else includes.
#include "GameDefines.hpp"

namespace GameBuild {

namespace {

// Measured 2026-09-20, each anchor twice and by methods sharing no assumptions
// (`docs/engine-notes.md`, the Steam/GOG table). ⛔ Row 0 is the REFERENCE: it is what an
// unrecognised image falls back to, and what `tools/derive_offsets.py` derives a new row FROM.
constexpr Build kBuilds[] = {
    // name     timestamp    SizeOfImage  GNames       GObjects     ProcessEvent  movie global  movie vtable
    {  "Steam", 0x621562FA,  0x020C7000,  0x01DAF450,  0x01DAF4A8,  0x00143B50,   0x01DB8900,   0x013E9B28 },
    {  "GOG",   0x5B9A17C4,  0x020CA000,  0x01DB2490,  0x01DB24E8,  0x00144310,   0x01DBB950,   0x013EA7B8 },
};
constexpr int kReference = 0;

static_assert(kBuilds[kReference].gnames == GNames_Offset,
              "The reference row's GNames no longer matches the generated SDK. If the SDK was "
              "regenerated from a new dump, re-derive every row (tools/derive_offsets.py) rather "
              "than editing one number.");
static_assert(kBuilds[kReference].gobjects == GObjects_Offset,
              "The reference row's GObjects no longer matches the generated SDK — see above.");
static_assert(kBuilds[0].timestamp != kBuilds[1].timestamp,
              "Two rows with the same fingerprint: the first would always win.");

bool sResolved = false;
bool sKnown = false;
const Build* sCurrent = &kBuilds[kReference];
char sFingerprint[128] = "unread";

// Reads the loaded image's own PE header. The module is mapped by definition, so this needs no
// SEH — but the signatures are checked anyway, because a header that is not a header would
// otherwise hand us two arbitrary dwords to match the table against.
void Resolve()
{
    if (sResolved)
        return;
    sResolved = true;

    const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!exe) {
        LOG_ERROR("GameBuild: GetModuleHandleA(nullptr) failed — assuming the %s build",
            kBuilds[kReference].name);
        return;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        LOG_ERROR("GameBuild: no DOS header at the module base — assuming the %s build",
            kBuilds[kReference].name);
        return;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        LOG_ERROR("GameBuild: no NT header — assuming the %s build", kBuilds[kReference].name);
        return;
    }

    const uint32_t stamp = nt->FileHeader.TimeDateStamp;
    const uint32_t image = nt->OptionalHeader.SizeOfImage;

    for (const Build& b : kBuilds) {
        if (b.timestamp == stamp && b.sizeOfImage == image) {
            sCurrent = &b;
            sKnown = true;
            break;
        }
    }

    std::snprintf(sFingerprint, sizeof(sFingerprint), "%s (timestamp 0x%08lX, image 0x%lX)",
        sKnown ? sCurrent->name : "UNKNOWN BUILD", static_cast<unsigned long>(stamp),
        static_cast<unsigned long>(image));

    // Nothing to say on the ordinary path: the startup banner prints the fingerprint once, and
    // two identical lines are two lines a tester has to listen to.
    if (!sKnown) {
        // Not fatal on its own: SDK::Init validates what this hands it. Say everything a report
        // needs, because the person reading this log cannot look at the screen to add context.
        LOG_WARN("GameBuild: %s — this executable is not in the offset table, so the %s build's "
                 "addresses are being tried. If the mod then goes silent, that is why: send this "
                 "line to the mod author with the exe, and tools/derive_offsets.py adds a row.",
            sFingerprint, kBuilds[kReference].name);
    }
}

} // namespace

const Build& Current()
{
    Resolve();
    return *sCurrent;
}

bool IsKnown()
{
    Resolve();
    return sKnown;
}

const char* Fingerprint()
{
    Resolve();
    return sFingerprint;
}

} // namespace GameBuild
