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
// ⚠ The base game's story cinematics (`CIN_TP_*`, `CIN_Autopsy_*`) ARE Bink movies too — their
// `.bk2` files ship in the BASE game's `XComGame/Movies`, not WOTC's, which holds only their cue
// tracks. Whether one is spoken depends on whether the engine loaded its cue track, which it does
// only on some play paths; docs/surfaces/strategy.md has the measured rule.

namespace MovieSubtitles {

/// Poll the movie player and speak the subtitle it is currently showing. Rate-limited
/// internally (100 ms). GAME THREAD ONLY — drive it from the frame pump. While no movie is up it
/// resolves the cue text table through the engine's `Localize` a batch at a time; while one is,
/// it makes no outbound call that touches the config cache (movie_subtitles.cpp says why).
void OnTick();

} // namespace MovieSubtitles
