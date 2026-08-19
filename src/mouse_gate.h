#pragma once

// While the tactical tile cursor owns world mode, the game's own per-frame mouse re-push
// (XComTacticalInput.Mouse_CheckForPathing) must not overwrite keyboard cursor pushes.
// This gate makes the script VM dispatch that one function to a native no-op while engaged,
// and restores the untouched original when released. See mouse_gate.cpp for the mechanism
// and the live measurements that led here.

namespace MouseGate {

/// Engage or release the swallow. Idempotent; resolves the UFunction lazily on first engage.
/// Game thread only (the same fields the VM reads are being written).
void SetSwallow(bool on);

/// Is the swallow currently engaged?
bool Swallowed();

/// Detach-time restore: release unconditionally so no game code is left dispatching into an
/// unloaded module. Safe to call if never engaged.
void ForceRestore();

} // namespace MouseGate
