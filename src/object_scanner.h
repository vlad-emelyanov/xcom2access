#pragma once

// Object scanner — the RT Scanner port for XCOM 2's tactical map. A PULL surface: the player asks
// "what is out there and where", rather than being told. Sibling of the tile cursor, sharing its
// gate and its scan origin so both describe one world from one place.
//
// SLICE 1a ships the `markers` category only — the world-space things the game points the player
// at (the tutorial's required-action marker, objective-carrying objects, the evac zone). The
// remaining categories from docs/object-scanner-recon.md §3.2 land on the same ring later.
//
// Division of labor, same as the tile cursor: this module owns the keys and the speech; the whole
// model — enumerate, filter, classify, sort, format — lives in the companion mod's
// X2AccessObjectScanner and is called outbound per keypress (game thread, from the tick drain)
// with the result read back from its NativeResult property. No offsets, no static_asserts: change
// detection and layout knowledge sit next to the data, where an SDK regeneration cannot break it.

namespace ObjectScanner {

/// Register key claims. Call during init, BEFORE the game hooks go live.
void Init();

} // namespace ObjectScanner
