#pragma once

// Tactical tile cursor — the RT TileExplorer port for XCOM 2's tactical map. Always active
// while the tactical HUD owns the screen: camera-relative arrow steps drive the game's OWN
// XCom3DCursor, Shift+comma/period step floors, C recenters on the active unit, Delete
// re-announces, Backspace is the two-press guarded move order.
//
// Division of labor: this module owns the keys (input arbiter claims) and speech; all
// game-state math and the spoken tile description live in the companion mod's
// X2AccessTileReader, called outbound per keypress (game thread, from the tick drain) with the
// result read back from its NativeResult property. Deliberately OUTSIDE the Graph kernel — the
// map is not a widget tree (spec: the exploration layer is its own subsystem).

namespace TileReader {

/// Register key claims. Call during init, BEFORE the game hooks go live.
void Init();

/// Game-tick pump: refresh the active predicate (tactical HUD focused, shot HUD down,
/// navigator detached). Throttled internally — safe to call many times per frame.
void OnTick();

/// The world-mode gate, as last computed by OnTick. The object scanner shares it rather than
/// recomputing: the two surfaces must stand down at exactly the same instants (targeting raised,
/// a recipe attached, tactical over) or one of them starts swallowing keys the other released.
bool IsActive();

/// Plant the cursor on the object scanner's current marker selection (companion
/// X2AccessTileReader.CursorGoToMarker) and speak the tile. Lives here rather than in the scanner
/// so the mouse gate is armed by the same code path as every other cursor move.
void GoToMarker();

} // namespace TileReader
