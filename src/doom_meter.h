#pragma once

// The Avatar Project meter — what MOVES on it, spoken when it moves.
//
// The meter itself is a Tab stop in the geoscape recipe (recipe_strategy.cpp, BuildDoomStop):
// title, "N out of M", or the completion countdown. This module is the other half: a doom change
// is a ten-second sequence the game runs IN FLIGHT MODE — screen tear, a camera pan to the
// facility, the game's own message written over the meter ("Alien Facility in West Africa",
// "SABOTAGE"), then the pips redrawn with the new ones blinking — and the tray recipe refuses to
// attach in flight mode, correctly, because nothing is navigable there. So the change cannot be
// an announcement on a node; it has to be watched from outside the recipe.
//
// WHY POLL THE PIPS AND NOT LISTEN FOR AN EVENT. `ClearPendingDoom` fires the pending entry's
// event name when it has one — 'OnFacilityAddsDoom', 'OnFortressAddsDoom', five golden-path
// removal names — but the covert action's sabotage and a destroyed facility's removal queue their
// pending doom with NO event at all, and the first facility's entry is cleared on a later pass
// than the one that names it. The pips move for every one of them, in one place, and that place
// is what a sighted player is looking at. `UIStrategyMap_HUD.CachedDoom` is the number the pips
// were last drawn from, rewritten by every UpdateData, so an edge on it IS the redraw.
//
// THE WORDS come from the same panel's Flash push: `SetDoomMessage` / `SetDoomMessageRemove` is
// an `MC.FunctionString` on the HUD, so the capture (flash_shadow.cpp) records it against the
// HUD's cache index one frame before the pips change. Each recorded call carries a sequence
// stamp, and this module remembers the last one it spoke — the per-panel history keeps old
// pushes around, and a stale "Alien Facility in X" attached to a change from somewhere else
// would name the wrong place. An edge with no NEW message waits briefly for one, then speaks the
// numbers alone: some sequences (the hidden fortress with no label, the first facility's second
// pass) legitimately push nothing.
//
// Parity: everything spoken is on screen — the message the game wrote, the count of blocks that
// blinked, the title and the total the pips show. Nothing about the alien's next timer, which the
// game draws nowhere.

#include <string>

namespace GameUI {
struct DoomMeterView;
}

namespace DoomMeter {

/// Game-tick pump: watch the meter for a change and speak it. Throttled internally. Game thread
/// only (one outbound companion call per HUD instance, for the maximum).
void OnTick();

/// The meter as words: the panel's own title (or the fallback) and "N out of M" — or the bare
/// count when the maximum is unknown, which is only the companion-missing case. Shared with the
/// recipe's stop so a change and the stop cannot word the same fact two ways.
std::string MeterLine(const GameUI::DoomMeterView& view, int max);

/// The completion countdown as words: the panel's own label and its four unit words around the
/// four values the companion reads off `GetTimerDisplayValues`. Makes ONE outbound call. Empty
/// when the companion cannot answer.
std::string CountdownLine(const GameUI::DoomMeterView& view);

/// The meter's maximum, from the companion (`GetMaxDoom`, which Second Wave scales). 0 when the
/// companion cannot answer. One outbound call.
int FetchMax();

} // namespace DoomMeter
