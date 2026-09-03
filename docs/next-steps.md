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

⛔ **TWO COMPANION COMMITS POSTDATE THE DEPLOYED `.u`.** The build in the game is the ASI of
2026-08-31 17:00 beside the companion of 16:57, and the whole live-run debt below it was
retired against THAT pair — but `04a264c` (16:58, `X2AccessAfterActionReader` /
`X2AccessHelper` / `X2AccessSquadReader`: the soldier reading identically on both bookends) and
`169019d` (`X2AccessTileReader`) both landed after it. So the after-action card wording, squad
select's card wording and the tile readout in the game are the versions BEFORE those changes,
and whatever was verified about them does not carry. `just go-full` first, then re-walk those
three; the unification commit is the one that matters, because its whole claim is that two
surfaces now say the same thing and only a live pass can show that they do.

**The Chosen dossier and the bond screens** (2026-09-03, built, never run —
`chosen-bond-recon.md` §8 is the checklist; the `chosen dossier`, `soldier bonds` and `bond
confirm` recipes + `X2AccessChosenReader` / `X2AccessBondReader`, and two new popup-family
branches for `UIChosenEncountered` / `UISoldierCaptured`). Needs a campaign with at least one
Chosen met and a soldier or two with cohesion. The load-bearing items: (a) on the dossier, Enter
on the second Chosen's tab must make the `dossier` stop re-read for THAT Chosen — the wire cache
is keyed on the screen pointer and discriminated on `CurrentChosenIndex`, and if the words do not
change it is that discriminator that broke; (b) on the bond screen opened from the armory's BONDS
entry, Left/Right on the soldier node must cycle soldiers with the roster respawning under it,
Right onto `CREATE BOND` + Enter must reach the confirm screen, and Enter on a row that offers no
bond must switch the page to that soldier; (c) opened from after action (`bSquadOnly`) the
soldier node must be plain text and a plain row's Enter must do nothing but log the refused verb
— vanilla gates `OnSelectSoldier` there; (d) the two continue-only popups must offer ONE button
and no `<XGParam` markup may reach the ring. Then the audit: no double space, no asset path, and
the cohesion column must not collapse to one value across the roster. ⚠ The confirm screen's
Cancel EMPTIES the Training Center's bond slots when it was opened from one — that is vanilla,
and Escape there must be tried once from the slot path to hear what the room says afterwards.

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

**Staffing a BUILT facility, and the upgrade screen** (2026-09-03, built and deployed, never
run — `surfaces/avenger.md` "Inside a room" and "Facility upgrades", recon §18). Reported as
"I can't assign engineers to the Power Relay or soldiers to the Training Center": the room
recipe built no nodes for `UIFacility`'s staff slots and had no picker shape. Now the slots sit
in the crew stop and the picker is the debris room's, through one shared companion class
(`X2AccessStaffSlots`) — so the debris-room path is ALSO re-deployed and must be re-walked. The
pass, on a campaign with a built facility: (a) in the Power Relay, Down from "ENGINEERS: N"
must land on the first slot, Enter must open the picker and the screen name must gain "choose
who to assign", Enter on a name must assign and the slot must re-read with the occupant WITHOUT
leaving the room (the words re-ask while the picker is open); (b) in the GTS, Enter on the
empty slot → picker of rookies → Enter → the game's train dialog (dialog recipe) → Yes →
`UIChooseClass` (commodity recipe), and Enter on the FILLED slot must raise the stop-training
confirmation, never a picker; (c) a padlocked slot must say "locked" plus the game's unlock
tooltip and Enter must open `UIChooseUpgrade`; (d) on that screen the rows read with
"unavailable" where greyed, the card's COMPLETED line appears only on an installed upgrade, the
power line carries its sign, and Enter installs and pops back to the room; (e) a debris room
still clears by keyboard (the shared builder replaced its own). Then shape-count the ring, and
`grep -n 'MISSING DISPLAY INFO'` over it — that literal is `XComGameState_StaffSlot`'s own
fallback for a template with no display function, and would mean a slot kind the words never
covered.

**The black market** (2026-09-03, built from source, never run — `surfaces/avenger.md` "Black
market", recon §19; the `black market` and `black market sell` recipes plus
`X2AccessBlackMarketReader`). The companion compiled clean SDK-side at 18:20 that day but the
game was running, so the `.u` under `Mods/` is the OLD one. ⛔ `just go-full` with the game
stopped before anything else: the trading post's words and the entry menu's interest sentence are
companion-side, so without the fresh `.u` every row reads as "Item" and the interest node is the
bare caption. The pass, from a geoscape with the market scanned: (a) the entry menu must attach
as "The Black Market" and read the welcome line, "INTERESTS THIS MONTH:" with its sentence, then
BUY and SELL INVENTORY as buttons — a greyed one must carry its reason (SOLD OUT THIS MONTH /
EMPTY INVENTORY) — and Enter on BUY must land in the commodity recipe on "This Month's Goods";
(b) on the trading post the FIRST thing to settle is the heading-to-column pairing, `just
dev-shot` beside `just dev-speech`: the sheet assumes QTY over the owned count, COST over the
unit value, SELL over the number selling and TOTAL over the line total, read off
`UpdateSellInfo`'s push order and the loc strings and never off the Flash layout, and a swap
there miscaptions every cell; (c) `+` on a row must click and say "SELL 1, TOTAL §N", `-` back
to zero must say "SELL 0" with no total, Right from the name must read "QTY, n" and the further
cells under their headings, and the CONFIRM node must lose "unavailable" the moment something
is marked; (d) Enter must sell, respawn the rows and leave focus on a row rather than detaching,
and the resource stop must show the new supplies; (e) `Escape` must close both screens
(unclaimed). Then shape-count the ring and `grep -P '  '` it: an empty heading leaves a double
space, which is the localized-var trap.

## Unexercised paths inside surfaces that HAVE run

⚠ Everything in this section belongs to a surface whose MAIN PATH has been walked live. What is
listed is the branch that has not: a state the campaign has not reached, a mod that has to be
installed, a setting that has to be off. Treat a line here as "nobody has heard this yet", not
as "this is broken".

The branches carried over when the 2026-08-31 build's live-run debt was retired (main paths all
confirmed in play):

- ⛔ **The resource strip has NEVER RUN** (built 2026-09-03, `surfaces/avenger.md`). It is
  first-pass debt rather than a branch, and it is cheap to walk because the strip is on screen
  for most of the strategy layer. Four things to confirm, in order:
  1. `just dev-log` carrying `ResourceStrip: N resource(s) from cache index …, showing`. **N
     must be 9 on the ant farm** — anything less means the repaint delimiter or the raised
     `kMaxCallsPerPanel` is not doing its job, and 8 specifically would mean the cap change did
     not take.
  2. `R` on the ant farm reads all nine; Tab in a commodity list, in a room and on the geoscape
     reaches a **Resources** stop with the subset `avenger-view-recon.md` §2.2 predicts for that
     screen. ⚠ The per-screen subsets are the thing most likely to be wrong, and the cheapest
     check is the Power Generator (power ONLY) against `UIChooseResearch` (five, no supplies).
  3. **The colour states.** `grep -c critical` / `low` over `just dev-speech` — a run in which
     NOTHING is ever `critical` is the distribution failure CLAUDE.md's audit rule describes,
     and the easy provocation is `UIFacility_ResistanceComms` with contacts at capacity.
  4. The strip must be **SILENT before `T0_M4_ReturnToAvenger`** and while the Avenger is in
     flight — no stop, and `R` unclaimed. This is the parity half and the only one that fails
     in the direction a player would not report.
- ⛔ **The Avenger HUD layer has NEVER RUN** (built 2026-09-03, `surfaces/avenger.md` §Avenger
  HUD layer). First-pass debt on the ant farm, and the checklist is short because four of its
  five stops reuse readers that have: (1) `Tab` on the grid must say "Avenger HUD" then land on
  the resources stop, and `Escape` must say "Rooms" and give the arrows back — the room name
  must still speak on the next arrow, which is the `OwnsInput()` gate in `avenger.cpp` doing its
  job; (2) `Tab` must cycle Resources → Objectives → To do → Events → Room shortcuts, and
  `just dev-log` must show the recipe attached "(ambient, unfocused)" BEFORE the first Tab —
  a refusal ("Avenger HUD unavailable.") on the first press means the probe is not passing;
  (3) Enter on a shortcut category must open that room and land in the room recipe; Enter on a
  to-do message must open its hotlink (the research picker is the easy one); (4) the event
  queue's expand button must appear only with two or more events, and the date node must read
  a real date — an empty date with the queue showing means `RefreshDateTime` was not captured
  on that cache index (check `just dev-flashqueue`). ⚠ The to-do strip is the one panel with a
  real staleness question (`UpdateCategories` runs on the grid only) — read it, open a room,
  come back, and confirm it refreshed.
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
- **After action**: a KIA card's Memorialize into the photobooth (no recipe — expect
  Escape-only) and the bond icon into `UISoldierBondScreen`, which now has the `soldier bonds`
  recipe (its own first pass is under live-run debt above) — from here it opens in `bSquadOnly`
  mode, the branch where vanilla gates the soldier switch and the cycle off.
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
7. Strategy-layer surfaces still unread, in value order: `UIFacility.m_kResearchProgressBar` (the last
   of the room's Flash-only status panels — `m_kCovertOpsStatus` is done, `surfaces/avenger.md`),
   and on the Avenger HUD the two panels the ant farm's Tab layer left out on purpose:
   `UIAvengerHUD.StaffIcons` (one icon per unstaffed ENGINEER — the scientist loop is commented
   out in shipped source — clicking opens the engineer list; likely a duplicate of the to-do
   strip's Staffing category, so check that before building) and the crew status indicators
   (`UIAvengerHUD_SoldierStatusIndicatorContainer`, bond/trait icons anchored to walking pawns
   in 3D — they go off-camera for a sighted player too, so a stop for them is an AID and must be
   labelled as one). ⚠ The resource strip, the to-do widget and the event queue all came off this
   list on 2026-09-03 (`surfaces/avenger.md`) and are **unexercised live** — see the entries
   above.
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
