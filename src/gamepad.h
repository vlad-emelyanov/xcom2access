#pragma once

// Controller support for the shell's menus.
//
// The game stays in its mouse-and-keyboard mode (the 2D movie's mouse-active flag is what
// ISCONTROLLERACTIVE reads, and flipping it rebuilds whole HUDs — docs/input-map.md). So a pad is
// not a mode here, it is a second keyboard: each mapped button resolves to the (vk, mods) of an
// existing claim and runs that claim's handler, which means every recipe works on a pad without
// knowing one exists.
//
// The game reads the pad too, so a mapped button must also stop reaching it, or a press would act
// twice. XCom2.exe imports XInputGetState from XINPUT1_3.dll by ordinal; one pointer swap in its
// IAT filters what the engine sees, button by button. What stays unmapped — B above all — goes
// through to the game's own handlers, the same deal Escape gets on the keyboard.

#ifdef X2ACCESS_DEVSERVER
#include <string>
#endif

namespace Gamepad {

/// Game-tick pump: installs the import filter once, polls the pads, dispatches presses. Run it
/// right after InputArbiter::OnTick. Game thread only.
void OnTick();

/// Put the original import back. Plain pointer write, legal under the loader lock.
void Shutdown();

#ifdef X2ACCESS_DEVSERVER
/// Dev harness: queue a press of each named input, read by the mod AND the game exactly as a
/// physical press would be, so a test needs no hands on the pad. Names: A B X Y LB RB LT RT Start
/// Back L3 R3 DUp DDown DLeft DRight LUp LDown LLeft LRight. Any thread. Empty on success, else
/// the name that was not recognised.
std::string DevQueuePresses(const std::string& commaSeparatedNames, unsigned holdMs, unsigned gapMs,
    bool engineEvents = true);

/// Tell the ENGINE no pad is connected (the mod still reads the real and virtual pad). A pad that
/// shows activity flips the game out of mouse mode on its own, so a mouse-mode test needs this.
/// Any thread.
void DevSetHideFromEngine(bool hide);

/// One-paragraph state dump. Game thread only.
std::string DevStatus();
#endif

} // namespace Gamepad
