#pragma once

// The mission's objectives, on demand — "what am I actually supposed to be doing here".
//
// A sighted player has the objective list permanently in the corner of the screen and re-reads it
// with a glance. It is the one persistent HUD panel with no spoken equivalent: the mod announced
// a NEW objective when its alert popped (the Alert_NewObjective recipe) and the briefing read the
// list once on the Skyranger, and after that the information was gone. Ctrl+X puts it back.
//
// Native reads it end to end, with no companion call, because this panel is the exception to the
// tactical-HUD-holds-no-text rule: UIObjectiveListItem.RefreshDisplay ends every branch with
// `Data = NewData`, so the ObjectiveDisplayInfo behind each row survives in the widget. That is
// also what makes the readout work in the TUTORIAL, where nothing is submitted and a history- or
// event-sourced answer would be silently dead — the replay drives
// UIObjectiveList.OnVisualizationBlockComplete exactly as a live mission does.
//
// One surface, both layers: UITacticalHUD.m_kObjectivesControl in a mission and
// UIAvengerHUD.Objectives on the ship are the same UIObjectiveList class, so the Avenger's
// objectives and its dark events come out of the same reader.
//
// This is PARITY, not an aid: the stop list mirrors what is on screen, and every word spoken here
// is one the game is already drawing. Nothing is derived that a sighted player cannot see.

#include "game_ui.h"

#include <string>

namespace ObjectiveReader {

/// Register the Ctrl+X claim. Call during init, BEFORE the game hooks go live.
void Init();

/// Game-tick pump: refreshes the cached gate the claim's activeWhen reads. Throttled internally.
/// Game thread only.
void OnTick();

/// One objective row, spoken — and one stage counter. Exported for the tactical HUD recipe's
/// Objectives stop: two of our surfaces answer this question (Ctrl+X and the graph), and the
/// cheap corollary of the audit rule is to make them agree by construction — one composer,
/// however the row is reached. Pure functions of the line; no engine access.
std::string DescribeLine(const GameUI::ObjectiveLine& line);
std::string DescribeMissionCounter(const GameUI::MissionCounter& c);

} // namespace ObjectiveReader
