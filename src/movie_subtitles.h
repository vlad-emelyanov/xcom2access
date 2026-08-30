#pragma once

// The subtitles the game draws over a FULL-SCREEN MOVIE — the WOTC pre-intro, the Chosen
// strongholds, the XP autopsies, the final movie. Everything else in this mod that says
// "subtitle" means the narrative commlink (src/transient_reader.cpp); this is the other system
// entirely, and until now nothing read it, so a blind player sat through a subtitled cinematic
// in silence.
//
// ⛔ Not a UIScreen, not Flash, no companion. A `.bk2` is played by the engine's own Bink
// front end, which loads a cue track beside the video and draws the text with native canvas
// calls. Neither of this project's two usual layers can see any of it, so this module reads the
// player object directly — see movie_subtitles.cpp for what is read and how it is validated.
//
// The base game's TENTPOLE cinematics are NOT this: they are in-engine matinees over which
// UINarrativeMgr deliberately keeps the commlink alive, so the transient reader already has
// them. The two paths cannot both speak one line — they are different sources on different
// screens.

namespace MovieSubtitles {

/// Poll the movie player and speak the subtitle it is currently showing. Rate-limited
/// internally (100 ms). Makes an outbound ProcessEvent call on a cue change, so GAME THREAD
/// ONLY — drive it from the frame pump. One pointer read and a null check when no movie is up.
void OnTick();

} // namespace MovieSubtitles
