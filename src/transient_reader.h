#pragma once

// The transient surfaces — docs/popup-inventory.md §4: the narrative commlink subtitle (which is
// how nearly all of XCOM 2's story dialogue is delivered), the event-notice toast stack, and the
// strategy tutorial help blade. None is focusable or dismissable, so none gets a graph recipe;
// this is a poll that speaks each line once. See transient_reader.cpp for why the change
// detection lives in the companion rather than here.

namespace TransientReader {

/// Poll the companion and speak anything new. Rate-limited internally (250 ms). Makes an outbound
/// ProcessEvent call, so GAME THREAD ONLY — drive it from the frame pump.
void OnTick();

} // namespace TransientReader
