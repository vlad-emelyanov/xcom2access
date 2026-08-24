# Next steps — the running work list

`CLAUDE.md` holds the rules and the durable engine facts; this file holds what has NOT been done
yet. Four kinds of entry, in the order a session should care about them: **live-run debt** (code
that builds clean and has never been exercised against a running game), **unexercised paths** inside
surfaces that have run, **known defects**, and **queued work**. Ordering inside each section is by
value, not by dependency.

Doc references are relative to this file's own directory (`docs/`), so `geoscape-recon.md` and
`surfaces/avenger.md` resolve as written.

⚠ When one of these is finished, the RULE it earned goes back into `CLAUDE.md` (or into the
surface's own `surfaces/*.md`) — this file loses the entry rather than gaining a "done" mark.

## Live-run debt

⛔ **`movie_subtitles.cpp` has NEVER RUN and its first pass outranks the rest of this list**,
because it is the only surface in the mod that dereferences a raw C++ object rather than a
UObject: nothing in it is `static_assert`ed and the vtable check is the whole pin. The FIRST
thing to confirm is `just dev-log` carrying `MovieSubtitles: movie player at 0x… validated` —
if it says DISABLED instead, every offset below it is void and nothing else about this surface
is worth testing. Then a `.bk2` with a cue track (`CIN_XP_PreIntro` is the earliest, on a new
WOTC campaign; `CIN_XP_Stronghold_*` and the XP autopsies are the rest) checked three ways:
`just dev-speech` against what is on screen, the same movie with Show Subtitles OFF (must be
silent), and a grep for `no localized text for cue` — that line is the only place a cue whose
text we failed to resolve would show up, and its absence is what says the loc lookup found the
right package.

⛔ **The geoscape's `Enter` fix is UNVERIFIED and its predecessor was wrong twice**
(`geoscape-recon.md` §15). Focus write-back moved the game's navigator but not
`SelectedMapItem` — `UINavigator.SetSelected` fires no delegate — so `Enter` acted on tray icon
0 for the whole session, which read to the player as *"Enter just starts the scan where I am"*.
Activation no longer consults that state at all: it is now the mouse's own click on the ICON
(`OnMouseEvent(FXS_L_MOUSE_UP_DELAYED)` — 400, and 391 is a silent no-op), whose
`OnClickedDelegate` the game bound to that icon's own site. `SetTraySelection` also calls
`SetSelectedMapItem` now, but only for the 3D cursor and nav-help. The pass that closes this
must actually fly the Avenger to a POI.

**Not yet run live** (builds clean, unexercised): Skyranger briefing, modal/popup
recipe, commlink transient reader, armory loadout, personnel roster, commodity list, inside-a-room
recipe, selected-unit readout, objectives readout (`Ctrl+X`), research report
(`research-report-recon.md` §7 — the Flash capture's SECOND consumer, so the pass must also
check `just dev-flashqueue` against `just dev-speech`, and must cover the Shadow Chamber path,
which is the only one that puts this screen on the 2D movie), covert actions
(`covert-actions-recon.md` §9 — the checklist there is the pass, and its two live-only questions
are whether `LaunchButton.Click()` really reaches the confirm popup and whether the destructive
row selection behaves as the source says), the geoscape's **region actions** stop
(`geoscape-recon.md` §16 — make contact / install a radio relay, the two pin buttons vanilla
wires to no key; needs a save with `ResistanceRadio` researched, and its confirm alert is the
Flash capture's `Alert_MakeContact` panel, also unexercised), resistance orders
(`UIStrategyPolicy` — the pass must cover BOTH windows, the monthly report where cards can be
played and the read-only visit, and must actually place a card, swap two, and take one back out
with Backspace). Each needs a first pass against
`just dev-speech` and a shape-count of the speech ring afterwards. For the objectives readout the
OFFSETS are already confirmed live (2026-08-23, `LostAndAbandonedB`) and the `static_assert`s
pass — what is unexercised is the spoken COMPOSITION, plus the Avenger/dark-event and
counter-row branches, which need the ship and a Retaliation-style mission respectively.

⚠ The popup recipe has a NEW first-pass question on top of the usual one: `UIAlert`'s words now
come from the Flash command-queue capture, so the pass must check `just dev-flashqueue` against
`just dev-speech` for the same alert, and grep the log for `dropped asset-looking operand` —
that line is the asset filter reporting exactly what it removed, and it is the only place a
wrongly-dropped LINE would show up. Alerts worth hitting: an image-carrying splash
(`BuildGOpsAlert` / retaliation / supply raid, which push an image path mid-list) and any alert
whose confirm label should be matched out of the prose by the button filter.

⚠ The commlink reader's first pass now has a SECOND branch to cover: it obeys the game's
Show Subtitles setting (`surfaces/strategy.md`), so the same conversation must be heard once
with subtitles on and once with them off — off must be silent, and turning them back on
mid-conversation must announce the line still in progress rather than swallow it.

## Unexercised paths inside surfaces that HAVE run

Tactical narration's panic / shields / evac
events; the hacking screen's cancel line and any hack whose slot 0 is `bBadThing`; the ant farm's
countdown path (`Status` is empty unless something is building) and its `Delete` re-announce; the
SAVE screen (only LOAD was exercised — new-save row, overwrite confirmation, a save completing);
the text-entry reader's own announcements (`just dev-log` should carry `TextInput: dialog up (...
caret N, selection A..B)`); the Resistance Ring's covert-op status line, in BOTH branches — an
action running (name + days left) and none assigned.

The WOTC **first-time popups** now that `TutorialReader::OnTick` polls the modal outside the
tutorial (`surfaces/tactical.md`). This one needs no campaign luck: `just dev-exec
"ShowModalTutorialDialogBox 0 0"` spawns a `UITutorialBox` on demand. Check that the line carries
title, body and the dismiss hint, that `I` re-reads it on an ordinary mission, and that dismissing
it leaves `TutorialReader: inactive (modal only — not the tutorial mission)` in the log rather than
a repeating announcement.

## Known defects

- Two wording defects: the no-target readout doubles a full stop and repeats the same fact in
  two languages (the localized string already ends in a period and carries the meaning); recentering
  says the unit's name three times.
- ⚠ `TileReader::ComputeActive` still stands down on `MenuNavigator::IsAttached()` — right for
  menus, wrong for the tactical layer, where the recipe must be ATTACHED BUT UNFOCUSED while the
  cursor keeps the arrows (`CLAUDE.md`, *The tactical layer: input ownership*): the navigator needs
  an explicit focused state and the tile reader has to gate on that instead.
- The dialog recipe attaches wherever a `UIDialogueBox` shows, including in the strategy and tactical
  layers — deliberate (a modal must be readable), but it means the layer takes over input there
  before those screens have recipes of their own. Its screen lookup caches the newest box, which a
  presentation-layer change could leave stale; covering dialogs outside the shell should switch that
  to a screen-stack walk.

## Queued work

1. `ScriptCall` module (generic find-function-by-name + game-thread call). `game_ui.cpp` hand-rolls
   this per call site and exports the first pieces (`FindCompanionCDO`, `CallScriptFunction`); the
   module is the generalization, not a prerequisite.
2. `Narrator::Say` adoption: the menu navigator — `menu_navigator.cpp` plus the recipe TUs under
   `src/recipes/` — is the last surface still calling `TTS` directly,
   and finishing it is a design step rather than a swap — its `TTS::Speak(line, false)` sites mean
   "queued, non-interrupting, focus-owned", which no current channel expresses (`Focus`/`Aid`
   interrupt AND clear, `Alert` marks the batch interrupting, `Combat` is named for a producer this
   is not). Either a fifth neutral queued channel or a rename of `Combat`, then map `SpeakMove`'s
   `interrupt` flag onto the pair. Four boot lines stay on `TTS` on purpose and say why in place
   (`dllmain.cpp`, `Hooks::Init`): they run on the init thread, which `Narrator::Say` does not
   claim, and they report the mod's own state before any producer can have queued a line.
   ⚠ The old "harmless today" justification was wrong and is why the other surfaces went
   unmigrated for so long: it rested on `TacticalHUD::OnTick` dropping the queue, which fires only
   on the tactical layer's FALLING EDGE — nothing dropped it under the tile cursor or the object
   scanner, and the geoscape is not tactical at all.
3. Events NOT yet narrated, in rough value order: reinforcement drops, the Chosen arriving, objective
   completion, will/panic thresholds, loot. Each is one more `AddCHEvent` line; the cost is judgement
   about spam, not code. `UnitMoveFinished` was deliberately dropped — the shipped sources trigger it
   only from `DLC_2`'s Alien Ruler code, so it is not a base-game event.
4. Dev harness: `/tile` (the tile reader's state as structured data) and `/tactical` (mode, gate
   reasons, last narration lines) are the remaining endpoints. `just dev-speech` covers most of the
   latter already, since it marks interrupting lines with `!`.
5. Hacking phase 4: a pre-commit odds read via
   `XComGameState_Ability.GetUISummary_HackingBreakdown`, the spoken form of the mouse-hover-only
   `UITacticalHUD_HackingTooltip`, so a player can judge a hack before spending the action point.
   ⚠ Three voices can land on the hack outcome — the reveal line (everywhere), the `UIWorldMessageMgr`
   flyover poll (tutorial only today) and `X2AccessTacticalEvents`/`X2AccessReplayNarration` — and
   they do not all fire on the same mission, so check each where it actually fires.
6. Strategy-layer surfaces still unread, in value order: the staff drop-down (`UIPersonnel_DropDown`,
   a `UIPanel` and NOT part of the personnel family), `UIFacility.m_kResearchProgressBar` (the last
   of the room's Flash-only status panels — `m_kCovertOpsStatus` is done, `surfaces/avenger.md`),
   the resource strip, the to-do widget and the event queue (`avenger-view-recon.md` §2 —
   note §2.5 was CORRECTED: the event queue IS retained, `UIEventQueue_ListItem.Title` /
   `DaysValue`). Then the tactical HUD recipe itself.
