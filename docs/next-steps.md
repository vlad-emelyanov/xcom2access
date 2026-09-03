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

⛔ **TWO COMPANION COMMITS POSTDATE THE DEPLOYED `.u`.** The build in the game is the ASI of
2026-08-31 17:00 beside the companion of 16:57, and the whole live-run debt below it was
retired against THAT pair — but `04a264c` (16:58, `X2AccessAfterActionReader` /
`X2AccessHelper` / `X2AccessSquadReader`: the soldier reading identically on both bookends) and
`169019d` (`X2AccessTileReader`) both landed after it. So the after-action card wording, squad
select's card wording and the tile readout in the game are the versions BEFORE those changes,
and whatever was verified about them does not carry. `just go-full` first, then re-walk those
three; the unification commit is the one that matters, because its whole claim is that two
surfaces now say the same thing and only a live pass can show that they do.

**The mission bookends** (2026-09-03, built, never run — `mission-summary-recon.md`; the
`mission summary` and `chosen summary` recipes, both Flash-capture-only, no companion). This is
the last hole in the post-mission chain, so the pass is one mission end walked all the way
through. It needs a **tutorial-off** campaign: `X2TacticalGameRuleset` skips both screens on the
tutorial, the final mission and Lost and Abandoned, and a LOSS that offers a restart goes to
`UICombatLose` (already the popup recipe's) instead. The full checklist is recon §7; its three
load-bearing items are (a) **does the navigator attach at all** — nothing yet says what
`IsInputBlocked` / `ScreensVisible` / `bCinematicMode` read during the end-of-mission fade, and
if the stack is gated here the way the hacking screen's is, the fix is the hacking screen's and
everything below it is moot; (b) `just dev-flashqueue` must show `SetMissionInfo` with **21
operands** on the screen's cache index — an empty capture is the LRU question, not an offset
question; (c) Enter on Continue must run the auto-photo and hand over to `UIAfterAction` with no
second press and no stall on `bWaitingForMissionSummary`. Then: the objective label switching to
"CIVILIANS RESCUED:" on a Terror or Chosen-retaliation mission, the photobooth node appearing on
a normal win and being ABSENT after a total squad wipe, and nothing anywhere speaking an asset
path (`grep -n 'img:///\|UILibrary\|\.tga'` over the speech ring — the Chosen screen pushes six
of them). ⚠ The Chosen screen's three outcome branches are three separate missions — a plain
encounter (its `SetChosenReward` is pushed with three EMPTY strings and must stay silent), a
knowledge extraction, and a soldier kidnapped — and the reward branch needs a Chosen actually
killed, which is the rarest of the four.

## Unexercised paths inside surfaces that HAVE run

⚠ Everything in this section belongs to a surface whose MAIN PATH has been walked live. What is
listed is the branch that has not: a state the campaign has not reached, a mod that has to be
installed, a setting that has to be off. Treat a line here as "nobody has heard this yet", not
as "this is broken".

The branches carried over when the 2026-08-31 build's live-run debt was retired (main paths all
confirmed in play):

- **Pause menu, mod rows.** `ActivatePauseMenuItem` sends Enter to the LIST (`SendRowCommand`)
  rather than calling `OnChildClicked` by name, so a mod-wrapped `OnItemClicked` delegate runs.
  The vanilla rows are confirmed; the MOD row that motivated it is not — with Remove Missing
  Mods Redux enabled, Enter on its row (6 of 8, tactical pause) must raise its confirm dialog.
- **Popup recipe, image-carrying alerts.** `UIAlert`'s words come from the Flash capture, and
  the asset filter reports what it removed as `dropped asset-looking operand`. Unheard: a splash
  that pushes an image path MID-LIST (`BuildGOpsAlert` / retaliation / supply raid), and an
  alert whose confirm label has to be matched out of the prose by the button filter. Grep the
  log for that line and check `just dev-flashqueue` against `just dev-speech`.
- **Commlink with subtitles OFF.** The reader obeys the game's Show Subtitles setting
  (`surfaces/strategy.md`): off must be SILENT, and turning it back on mid-conversation must
  announce the line still in progress rather than swallow it.
- **Research report from the Shadow Chamber** — the only path that puts that screen on the 2D
  movie (`research-report-recon.md` §7).
- **Research available**: a deck of ≥2 cards (the flip announcement on Continue), a
  shadow-project card, and the close into `UIRewardsRecap` on the tutorial-objective path —
  which is UNCOVERED and is that chain's next hole (`research-unlocked-recon.md` §5).
- **Geoscape region actions** (`geoscape-recon.md` §16): make contact and install a radio relay,
  the two pin buttons vanilla wires to no key. Needs a save with `ResistanceRadio` researched;
  its confirm alert is the Flash capture's `Alert_MakeContact` panel.
- **Resistance orders**: the READ-ONLY visit (`PolicyReadOnly` on the name, verbs raising
  `UICantChangeOrders`), a direct swap of two slotted cards, Backspace-cancels-hold, and the
  empty-hands refusals `PolicyCantPick` / `PolicyCantMove`.
- **After action**: a KIA card's Memorialize into the photobooth and the bond icon into
  `UISoldierBondScreen` — neither of those two screens has a recipe, so expect Escape-only.
- **Objectives readout** (`Ctrl+X`): the Avenger / dark-event branch and the counter-row branch,
  which need the ship and a Retaliation-style mission respectively.
- **Mission blade**: a GOps blade with 2+ missions (the selector buttons are mouse-only in
  vanilla, and clicking one must re-announce the switched mission via the MissionRef extra-key),
  the ambush blade (its Chosen strip must stay correctly SILENT), and the Shadow Chamber /
  sitrep sections — silent so far only because the campaign has neither, so the gates are
  untested in the POSITIVE direction.
- **Tactical HUD graph**: on a mission with evac available, Commands slot 6 appears ("Place evac
  zone"-family label) and Enter on it must raise free aim. And one design question a live run
  has to answer rather than a check: whether staying HUD-focused through the enemy turn is
  livable, or whether End Turn should hand back to Map mode on its own.
- **Promotion screens**: the classic tree is walked, the other two are not. `_PromotionHero`
  needs a faction hero (or any class after the Training Center retrofit) and its verb is buying
  an ability with AP, including the refusal when AP is short — ⛔ and it is the one screen where
  our layer is the ONLY keyboard route, since vanilla handles DPAD/LStick only and
  `InitPromotion` calls `DisableNavigation()`. `_PromotionPsiOp` needs a Psi Lab and a trained
  operative. On any of the three, a locked rank's ability names must stay UNSPOKEN — the reader
  resolves names in script precisely so `m_strAbilityLockedTitle` substitutes, and a regression
  there leaks what no sighted player can read.

Tactical narration's panic / shields / evac
events; the hacking screen's cancel line and any hack whose slot 0 is `bBadThing`; the ant farm's
countdown path (`Status` is empty unless something is building) and its `Delete` re-announce; the
SAVE screen (only LOAD was exercised — new-save row, overwrite confirmation, a save completing);
the text-entry reader's own announcements (`just dev-log` should carry `TextInput: dialog up (...
caret N, selection A..B)`); the Resistance Ring's covert-op status line, in BOTH branches — an
action running (name + days left) and none assigned.

The tile cursor **across floors, in TARGETING mode** — the half of the floor redesign that was
deliberately left unchanged. World mode now syncs the game's floor state on every cursor push
and has run; `X2AccessTacticalReader`'s `PushAim` / `EnsureAimPlanted` still push position
WITHOUT floor state. The pathing-pawn clamp that motivated the redesign does not apply there, so
this is an open question rather than a known defect: whether any targeting method, or the melee
pathing pawn, cares about `m_iRequestedFloor` across floors. Throw a grenade onto a roof from
the ground and compare the spoken land tile against `GetTargetLocations`.

The tile cursor's **detection readout on a hacked tower** — the one branch of the concealment
work a normal Gatecrasher sweep does not produce. "Tower detection" is confirmed; what nobody
has heard is the ring BEFORE and AFTER the hack, i.e. that hacking the tower actually silences
it. (The wider parity audit behind that readout — `concealment-recon.md` §4's four assumptions,
our recompute against the game's own markers — is queued work item 6, not a test.)

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
   `GameUI::ReadMouseControls`) are built and have run. Still owed: the spoken REASON on an
   unavailable ability (`AvailableCode` needs the companion vocabulary — the FName must never be
   spoken raw), and a decision on End Turn's focus hand-back.
9. The mission blade's two v1 punts, each a deliberate gap rather than test debt:
   `UIMission_GPIntelOptions` reads its intel options (name + cost + totals) but cannot PURCHASE
   one — the rows are `UIMechaListItem` checkboxes, so the toggle path and the purchased-state
   read are owed together — and the sitrep panel's mouse-only info button (it opens
   `UISitRepInformation`, which the popup recipe already reads) has no node. The second is low
   value: the reader already speaks each sitrep's name AND description inline.
