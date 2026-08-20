#pragma once

// While one of our surfaces owns the 3D cursor, the game's own per-frame mouse re-push must not
// overwrite keyboard cursor pushes. There is one such re-push per input state and they are gated
// independently:
//
//   world mode      XComTacticalInput.Mouse_CheckForPathing        -> SetSwallow
//   free aim        UsingTargetingMethod.Mouse_CheckForFreeAim     -> SetFreeAimSwallow
//                   UsingSecondaryTargetingMethod.Mouse_CheckForFreeAim
//
// The gate makes the script VM dispatch those functions to a native no-op while engaged, and
// restores the untouched originals when released. See mouse_gate.cpp for the mechanism and the
// live measurements that led here.

namespace MouseGate {

/// Engage or release the WORLD-MODE swallow (the tile cursor's). Idempotent; resolves the
/// UFunction lazily on first engage. Game thread only (the same fields the VM reads are being
/// written).
void SetSwallow(bool on);

/// Engage or release the FREE-AIM swallow, covering both targeting states. Same contract.
/// Never contends with SetSwallow: the pathing function is not called from a targeting state
/// and the free-aim ones are not called from the active-unit state.
void SetFreeAimSwallow(bool on);

/// Is any swallow currently engaged?
bool Swallowed();

/// Detach-time restore: release everything unconditionally so no game code is left dispatching
/// into an unloaded module. Safe to call if never engaged.
void ForceRestore();

} // namespace MouseGate
