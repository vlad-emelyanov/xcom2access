#pragma once

// Controller mode drops every keyboard and mouse event at XComInputBase.InputEvent. This keeps
// bAllowAllInput set on the live input object while the mouse is inactive, so the keyboard the
// mod's claims and the game's own bindings rely on keeps working. Never written in mouse mode.

namespace KeyboardGate {

/// Game-tick poll, throttled inside. Game thread only.
void OnTick();

/// Is the game in controller mode (the 2D movie's MouseActive false)? As of the last poll, false
/// until the first one; an atomic read, so a claim's activeWhen may call it from the window
/// procedure.
bool ControllerMode();

/// Hand the live input object one game command (a UIUtilities_Input FXS_* value) as a press
/// followed by its release, the pair a pad button gives it. False when there is no input object.
/// Game thread only.
bool SendCommandTap(int cmd);

} // namespace KeyboardGate
