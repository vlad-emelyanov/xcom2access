#pragma once

// Hacking screen — the hack / skulljack overlay (plan phases 1-3, see
// docs/hacking-screen-recon.md).
//
// UIHackingScreen is the purest case of the tactical-HUD problem: /gui on it prints "1 panels" —
// the screen and no children — because every word goes into Flash and is retained nowhere. So
// the words come from the companion CDO X2AccessHackingReader, re-resolved from the screen's own
// script properties and the reward templates, and C++ owns detection, the keys and the speech.
// Nothing here transcribes a field offset, which on this screen is a correctness argument: the
// Highlander interleaves two bools into the bitfield at +0x450, so bWaitingForInput's MASK
// differs between a Highlander and a vanilla install, and static_asserts pin offsets, not masks.
//
// What phase 2 fixes is not merely narration. The screen's OnUnrealCommand handles only the
// controller's D-pad commands, so on mouse+keyboard the reward choice is reachable ONLY by
// clicking a card on a small 3D quad floating in the world (proved live 2026-08-20: two
// synthetic Right presses left SelectedHackRewardOption at 1). Left/Right here feed the screen
// its own D-pad command, which is the value X2HackRewardTemplateManager.AcquireHackRewards reads
// back off this very screen object when the hack resolves.
//
// Phase 3 (the reveal) is the same discipline pushed one step further. The obvious source for
// "what did I win" is the submitted FinalizeHack context or the post-hack game state — and both
// are never written in the tutorial, which replays recorded frames while the pad animates and
// lights up exactly as it does on a real mission. So the outcome comes from the pad too:
// bWaitingForInput for WHEN (Flash's own "meter finished" signal) and m_rewardsTaken for WHAT
// (Flash's own count of cards that lit).
//
// Deliberately NOT a Graph recipe, for two reasons: there is no widget tree to graph, and
// MenuNavigator refuses to attach while the screen stack reports cinematic mode — which this
// screen runs under, legitimately, because it sets bShowDuringCinematic.

namespace HackingScreen {

/// Register key claims. Call during init, BEFORE the game hooks go live.
void Init();

/// Game-tick pump: track the screen and run the announce-once poll. Throttled internally — safe
/// to call many times per frame. Game thread only (it makes outbound ProcessEvent calls).
void OnTick();

} // namespace HackingScreen
