#pragma once

// The game asks DirectInput for the system mouse at startup and never checks the answer, so a machine
// with no mouse connected dies about 12 s in on a null dereference (docs/engine-notes.md, a launch with
// no mouse). This hands the game a do-nothing mouse ONLY when the real request fails; a real mouse
// always takes the untouched path.
namespace NoMouse {

// Swap the exe's DirectInput8Create import for a wrapper. Idempotent; a failure logs and leaves the
// game exactly as it was. Called from the init thread, before the engine reaches its input init.
void Install();

// Put the import slot back (one pointer write into the exe's own import table).
void Shutdown();

} // namespace NoMouse
