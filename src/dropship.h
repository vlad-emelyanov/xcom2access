#pragma once

// The Skyranger load screen — UIDropShipBriefing_MissionStart on the way to a mission,
// UIDropShipBriefing_MissionEnd on the way home.
//
// This is the longest stretch of the game in which X2Access said nothing at all: from the moment
// the squad is confirmed until the tactical HUD builds, a blind player got silence, and then MORE
// silence once the map had loaded and the game was sitting there waiting for a keypress. The
// generic ten-second "Loading" cue in menu_navigator.cpp cannot cover it — that keys off
// UILoadScreenAnimation, and UILoadAnimation(true) is called only by UINarrativeMgr and
// XComWaitCondition_NarrativesLoading, never on the dropship path.
//
// NARRATE-ONLY, and unusually for this project the argument is the INPUT MODEL rather than the
// content (recon §7). Two facts settle it:
//
//   1. The screen stack is in CINEMATIC MODE here. The dropship plays a narrative moment on
//      arrival (StartDropshipNarrative / PlayPostMissionNarrative), UINarrativeMgr:1408 calls
//      HideUIForCinematics(), and that is exactly why both briefing classes set
//      bShowDuringCinematic. MenuNavigator refuses to attach while the stack reports cinematic,
//      so a recipe would need a second documented exception to that gate — and there is nothing
//      navigable to attach TO: LaunchButton.DisableNavigation() in both variants, and no list.
//
//   2. Once the map has loaded, ANY key launches — and every key we claim stops doing so.
//      XComInputBase:470 intercepts above the screen stack: while the destination is still
//      loading it returns outright (so every key is dead, and a claim there costs the player
//      nothing), and once loaded any key RELEASE calls CleanupDropshipStart(). A recipe would
//      claim Enter and Space, the two keys a player actually reaches for, and would then have to
//      re-provide the launch itself.
//
// ⛔ Do NOT build against UIDropshipHUD. It looks like this screen's input handler — a proper
// OnUnrealCommand gated on bWaitingForInput — and it is dead code: NotifyDestinationLoaded(), its
// only setter, has no caller anywhere in the sources, and neither does the pres-layer function
// that would spawn the screen.
//
// Recon: docs/dropship-briefing-recon.md.

namespace Dropship {

/// Register the re-announce claim. Call during init, BEFORE the game hooks go live.
void Init();

/// Game-tick pump: track the screen, speak the arrival block once and the launch prompt on the
/// map-loaded edge. Throttled internally. Game thread only (it makes outbound ProcessEvent
/// calls).
void OnTick();

} // namespace Dropship
