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

**The pause menu's mod-row activation fix is UNVERIFIED** (2026-08-31): `ActivatePauseMenuItem`
now sends Enter to the LIST (`SendRowCommand`) instead of calling `OnChildClicked` by name, so a
mod-wrapped `OnItemClicked` delegate finally runs — found live on Remove Missing Mods Redux's
"Remove Missing Mods" row (6 of 8, tactical pause), where Enter did nothing. The pass: with that
workshop mod enabled, Enter on its row must raise its confirm dialog, and every VANILLA row
(return, save, load, options, exit) must still activate — the same call now also routes through
`UIPauseMenu.SetSelected`, which is new for those. `SetPauseMenuIndex` keeping
`m_iCurrentSelection` in sync is part of the same change.

**Not yet run live** (builds clean, unexercised): Skyranger briefing, modal/popup
recipe, commlink transient reader, armory loadout, personnel roster, commodity list, inside-a-room
recipe, selected-unit readout, objectives readout (`Ctrl+X`), research report
(`research-report-recon.md` §7 — the Flash capture's SECOND consumer, so the pass must also
check `just dev-flashqueue` against `just dev-speech`, and must cover the Shadow Chamber path,
which is the only one that puts this screen on the 2D movie), **research unlocked**
(`research-unlocked-recon.md` §5 — built 2026-08-31 with the game LIVE on the screen, so the
capture, both offsets and the two-card deck are already confirmed; what is unexercised is the
recipe itself: the flip announcement on Continue with ≥2 cards, the clean close into whatever
follows — `UIRewardsRecap` on the tutorial-objective path is UNCOVERED and is this chain's next
hole — and a shadow-project card, whose Flash styling nobody has seen beside the payload yet),
covert actions
(`covert-actions-recon.md` §9 — the checklist there is the pass, and its two live-only questions
are whether `LaunchButton.Click()` really reaches the confirm popup and whether the destructive
row selection behaves as the source says), the geoscape's **region actions** stop
(`geoscape-recon.md` §16 — make contact / install a radio relay, the two pin buttons vanilla
wires to no key; needs a save with `ResistanceRadio` researched, and its confirm alert is the
Flash capture's `Alert_MakeContact` panel, also unexercised), resistance orders
(`UIStrategyPolicy` — the PLAYABLE window ran live 2026-08-31, twice: attach with 11 targets
after the duplicate-id fix, board and hand walked, cards picked up, placed and removed,
Continue's orders-left dialog answered, and after the same-day second wave deployed, "drop
target" was heard on legal slots — including one that then ACCEPTED the drop — and "cannot
place here" on locked slots. Still owed: the READ-ONLY visit (`PolicyReadOnly` on the name,
verbs raising `UICantChangeOrders`), a direct swap of two slotted cards, Backspace-cancels-hold,
and the empty-hands refusals `PolicyCantPick`/`PolicyCantMove` — none heard live yet),
the **after-action walk-up** (`after-action-recon.md` — step 0 is DONE: every
§3 offset plus both §5 predictions were read off a live walk-up 2026-08-31, with the OLD ASI
deployed, so what remains is the recipe's own first pass — the promote → promotion screen →
back loop, the bond icon into `UISoldierBondScreen` (which has no recipe: expect Escape-only),
a KIA card's Memorialize into the photobooth (likewise uncovered), the wounded/days line
against the panel, and Continue through to loot recovered). Each needs a first pass against
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

**The mission blade family** (2026-08-31, built — `mission-blade-recon.md`; the `mission blade`
recipe + `X2AccessMissionReader`, covering all 16 `UIMission` subclasses through the base
skeleton). The first pass RAN the same day, live on a Russian campaign: a retaliation blade
end-to-end (attach → lines → Chosen strip → ПОДТВЕРДИТЬ → squad select), a locked alien
facility (locked text + Back only), and an unlocked facility with a reward and a second Chosen —
and it earned `PushValuePair` (recon §6: a caption whose value is absent is dropped whole; the
locked blade spoke a naked "НАГРАДА"). ⚠ That fix is SOURCE-ONLY until the next companion
compile, and re-checking the facility blade after it is part of closing this entry. Still
unexercised: a GOps blade with 2+ missions (the selector buttons are mouse-only in vanilla, and
clicking one must re-announce the switched mission via the MissionRef extra-key), the ambush
blade (its Chosen strip must stay correctly SILENT), and the Shadow Chamber / sitrep sections
(silent so far only because the campaign has neither — the gates themselves are untested in the
positive direction). Known v1 gaps, each a deliberate punt: `UIMission_GPIntelOptions` reads its
intel options (name + cost + totals) but cannot PURCHASE one yet (the rows are `UIMechaListItem`
checkboxes; the toggle path and the purchased-state read are owed together), and the sitrep
panel's mouse-only info button (opens `UISitRepInformation`, which the popup recipe already
reads) has no node yet — low value, since the reader already speaks each sitrep's name AND
description inline.

**The tactical HUD graph** (2026-08-31, built, never run — `surfaces/tactical.md` "Tactical HUD
graph" holds the design; this is the navigator's FIRST ambient recipe, so the focus split itself
is also unexercised; all four stops are in: Soldier, Abilities, Commands, Objectives). The pass,
on Gatecrasher with the harness armed: (a) mission start attaches
silently (`/health` navigator line reads `attached (tactical HUD, unfocused)`) and the tile
cursor still works — the split's whole point; (b) `Ctrl+Tab` speaks "HUD" + the Soldier stop's
first line (rank + name), arrows walk the facts (health, actions, aim, weapon + ammo, effects)
and Tab reaches Abilities, Commands and Objectives — the objectives stop must read word-for-word
what `Ctrl+X` reads, the soldier facts word-for-word what `;` says where they overlap, and the
abilities row Left/Right with cooldown/charges/hotkey per icon; (c) `Escape` speaks "Map" and the
arrows drive the cursor again; (d) Enter on an available ability raises targeting, the targeting
poll announces ability + default target, and the layer is UNFOCUSED afterwards (no arrow
swallowing — the game cycles targets); (e) Enter on an unavailable one says "unavailable" and
raises nothing; (f) a soldier switch while focused re-lands cleanly with the NEW soldier's
abilities (the stale-antenna trap — count the bar against the sighted icon count) AND the new
soldier's facts (the Soldier stop's ObjectID-keyed identities — watch for a stale weapon line);
(g) a pause menu and a modal dialog over tactical still attach their own recipes (the dialog is
the load-bearing one — the probe's own refusal is all that yields it); (h) `Shift+Tab` inside the
HUD does NOT cycle the soldier on release (the widened Shift swallow). Commands-stop specifics:
(i) the strip reads its five fixed buttons with the player's ACTUAL bound keys (the log carries
the one-time `TButtonItems stride 0x..` line — 0x21 vs 0x24 is the open layout question the
first pass settles); (j) on a mission with evac available, slot 6 appears ("Place evac zone"-
family label) and Enter on it raises free aim; (k) Enter on End Turn ends the turn — and note
whether staying HUD-focused through the enemy turn is livable or the stop should hand back to
Map mode on its own (a design question only a live run can answer); (l) Enter on Next/Prev
soldier moves the selection and the switch line speaks. Then shape-count the speech ring as
always.

## Unexercised paths inside surfaces that HAVE run

Tactical narration's panic / shields / evac
events; the hacking screen's cancel line and any hack whose slot 0 is `bBadThing`; the ant farm's
countdown path (`Status` is empty unless something is building) and its `Delete` re-announce; the
SAVE screen (only LOAD was exercised — new-save row, overwrite confirmation, a save completing);
the text-entry reader's own announcements (`just dev-log` should carry `TextInput: dialog up (...
caret N, selection A..B)`); the Resistance Ring's covert-op status line, in BOTH branches — an
action running (name + days left) and none assigned.

The tile cursor's **concealment readout** (2026-08-31, source-derived, never run):
"Detection zone" / "Tower detection" per tile and "breaks concealment, noise" on the move
preview (both concealment-gated — revealed, a window on the path is silent by design). The pass
is `concealment-recon.md` §6 — a Gatecrasher sweep across a visible pod's red paint, a
window-smashing move preview concealed AND revealed, a tower ring before and after its hack — and §4 lists the four parity assumptions the pass exists to check, the squad-visibility
gate on detectors first (if the game paints red for enemies the squad cannot see, widen ours to
match the overlay, not the other way round).

The tile cursor's **floor redesign** (2026-08-31, built from a live diagnosis but not yet run —
`surfaces/tactical.md` holds the rule): every cursor push now syncs the game's floor state, the
floor keys drive `AscendFloor`/`DescendFloor` instead of a per-column Z hop, and
`CursorFloorPoll` announces external floor changes. The pass: (a) with a soldier who can reach a
roof, cursor up to a tile that reads "Dash" and confirm the two-press move now COMMITS instead of
refusing "Out of range"; (b) `Ctrl+.`/`Ctrl+,` speak "Floor N of M" plus the landed tile, clamp
at "Top floor"/"Bottom floor", and reach a 2-story roof in two presses; (c) the mouse wheel and a
soldier switch across levels each speak the new floor ONCE and re-land the cursor — and neither
babbles at mission start, before the cursor is planted, or on same-level soldier switches (the
shape-count audit is the tool); (d) ground-level stepping is unchanged (every earlier test was
ground-level, which is what masked the clamp). And the same question for TARGETING mode,
deliberately left unchanged: `X2AccessTacticalReader`'s `PushAim`/`EnsureAimPlanted` push
position without floor state too — the pathing-pawn clamp does not apply there, but whether any
targeting method or the melee pathing pawn cares about `m_iRequestedFloor` across floors has
never been exercised (throw a grenade onto a roof from the ground and compare the spoken land
tile with `GetTargetLocations`).

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
- The dialog recipe attaches wherever a `UIDialogueBox` shows, including in the strategy and tactical
  layers — deliberate (a modal must be readable), but it means the layer takes over input there
  before those screens have recipes of their own. Its screen lookup caches the newest box, which a
  presentation-layer change could leave stale; covering dialogs outside the shell should switch that
  to a screen-stack walk.
- **Dev-server connections are intermittently RST mid-serve — the request still EXECUTES, only the
  response is lost.** Measured live 2026-08-31, mid-tactical: curl reports `(56) Recv failure:
  Connection was reset` after `upload completely sent off`, yet every such `/exec` command appears
  in Launch.log ("Command not recognized: X2AccessProbe1..6" for six requests of which five
  "failed"). So a `just dev-exec` that errors has usually still run the command — ⚠ blind-retrying a
  TOGGLE (TakeNoDamage, ToggleUnlimitedActions…) flips it again; verify state via `/props` instead
  of retrying. What is ruled out: not curl/just/pwsh quoting (identical bytes succeed and fail); not
  close-with-unread-data (the GET form resets too); not our `gClient` handling (single-threaded, one
  close per socket); not mere connection lifetime (a socket held open 5 s by an incomplete request
  survives and gets its response through the same SendResponse+close path). Incidence varies over
  time and eventually hit `/health` itself, so it is server-wide; `/exec`'s ~300 ms serve
  (`Sleep(250)`) just has the widest exposure window. Best theory: something in the game process
  sporadically closes a socket handle it no longer owns, and whether it collides with our live
  client handle is handle-recycling luck — `SendAll` then fails silently (`n<=0` return eats the
  error) and `closesocket` on the dead handle hides the rest. Next steps: log `WSAGetLastError` on
  `SendAll` failure so the server SEES the kill; Frida-hook `ws2_32!closesocket` logging caller
  module + handle to catch the strayer (needs a session where the game may be driven — not while
  the user is playing).

## Queued work

1. `Narrator::Say` adoption: the menu navigator — `menu_navigator.cpp` plus the recipe TUs under
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
2. Events NOT yet narrated, in rough value order: reinforcement drops, the Chosen arriving, objective
   completion, will/panic thresholds, loot. Each is one more `AddCHEvent` line; the cost is judgement
   about spam, not code. `UnitMoveFinished` was deliberately dropped — the shipped sources trigger it
   only from `DLC_2`'s Alien Ruler code, so it is not a base-game event.
3. Dev harness: `/tile` (the tile reader's state as structured data) and `/tactical` (mode, gate
   reasons, last narration lines) are the remaining endpoints. `just dev-speech` covers most of the
   latter already, since it marks interrupting lines with `!`.
5. Hacking phase 4: a pre-commit odds read via
   `XComGameState_Ability.GetUISummary_HackingBreakdown`, the spoken form of the mouse-hover-only
   `UITacticalHUD_HackingTooltip`, so a player can judge a hack before spending the action point.
   ⚠ Three voices can land on the hack outcome — the reveal line (everywhere), the `UIWorldMessageMgr`
   flyover poll (tutorial only today) and `X2AccessTacticalEvents`/`X2AccessReplayNarration` — and
   they do not all fire on the same mission, so check each where it actually fires.
6. The path-marker exact-parity read (`concealment-recon.md` §5): native, by-name reflection of
   `XComPathingPawn`'s five private marker arrays — no offsets or `static_assert`s needed for a
   by-name `TArray<TTile>` read. It is the audit tool for the recon's §4 assumptions (game markers
   vs our recompute on the same settled hover) and the only route to `LaserScopeMarkers` /
   `KillZoneMarkers`, two warnings the companion recompute does not attempt. Read it only where
   the path has settled — `CursorMoveTo`, or the queued `/tile` endpoint (item 4).
7. Strategy-layer surfaces still unread, in value order: the staff drop-down (`UIPersonnel_DropDown`,
   a `UIPanel` and NOT part of the personnel family), `UIFacility.m_kResearchProgressBar` (the last
   of the room's Flash-only status panels — `m_kCovertOpsStatus` is done, `surfaces/avenger.md`),
   the resource strip, the to-do widget and the event queue (`avenger-view-recon.md` §2 —
   note §2.5 was CORRECTED: the event queue IS retained, `UIEventQueue_ListItem.Title` /
   `DaysValue`).
8. The tactical HUD recipe's last stop — **Enemies** (the `m_arrTargets` strip, distance and
   bearing appended as a labelled aid, Enter → `DirectTargetObject`). Soldier (2026-08-31,
   `X2AccessUnitReader.BuildSoldierRecords` — the wire bridge went in that class, next to its
   composers, not in `X2AccessTacticalReader` as the plan sketched) and Commands (native —
   `GameUI::ReadMouseControls`) are built and ride the live-run-debt checklist above. Still owed:
   the spoken REASON on an unavailable ability (`AvailableCode` needs the companion vocabulary —
   the FName must never be spoken raw), and a decision on End Turn's focus hand-back (checklist
   item (k)).
