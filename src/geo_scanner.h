#pragma once

// Geoscape ring — the strategy layer's object scanner, and the sibling of src/object_scanner.cpp.
// A PULL surface beside the tray recipe's push, exactly as the tactical scanner sits beside the
// tile cursor: the player asks "what is out there and where", rather than being told.
//
// ⚠ IT IS AN AID, NOT PARITY. The tray recipe reads the game's own list of what is actionable
// right now; this reads the whole globe, including the fourteen regions the tray never holds
// (CanBeScanned() keeps all but the two or three currently scannable out of it). That is
// information a sighted player has to go looking for by dragging the map around — so by the
// project's stop-list rule the ring must NOT be a Tab stop. It lives on its own keys and stays out
// of the recipe's stop cycle entirely.
//
// THE SORT ORIGIN IS THE GEOSCAPE'S CURSOR. The globe has none for a mouse+keyboard player at all
// (pin selection by proximity is per-frame and `ISCONTROLLERACTIVE-gated), so a flat list of sites
// would strip it of place — what exists, never where. The ring sorts by distance from a movable
// point: the Avenger by default, Home to move it onto whatever you just heard, C to put it back.
//
// Division of labor, same as every other scanner in this project: this module owns the keys and
// the speech; the whole model — enumerate, filter, sort, format — lives in the companion's
// X2AccessGeoScanner and is called outbound per keypress (game thread, from the tick drain) with
// the result read back from its NativeResult property. No offsets, no static_asserts.

namespace GeoScanner {

/// Register key claims. Call during init, BEFORE the game hooks go live.
void Init();

/// Is the ring live — the geoscape up and nothing over it? Shared with the menu navigator so the
/// tray recipe and the ring stand down at the same instants rather than each deciding separately.
bool IsActive();

} // namespace GeoScanner
