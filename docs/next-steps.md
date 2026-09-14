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

**The Skyranger-arrived prompt** (2026-09-14, `skyranger arrives` recipe in
`recipes/recipe_strategy.cpp`, `GameUI::FindSkyrangerArrivesScreen`,
`X2AccessSkyrangerReader.DescribeSkyrangerArrives`; rules in `surfaces/strategy.md` under
"Skyranger-arrived prompt") — built from `SrcOrig` after a live screenshot of the silent screen,
compiled clean on both halves, **never heard**. The mission blade's sequel: any squad launch
reaches it, so the first pass is one flight. What to listen for:

1. Attach names the screen from the left blade — Russian game: `"РЕЙНДЖЕР", Начало процедуры
   посадки` — then lands on the mission stop: the operation name (`Операция "Паучий огонь"`) and
   the body line; Tab reaches the two buttons.
2. Enter on the first button must start the load (the Skyranger briefing follows); Escape or the
   second button must recall the Skyranger with the `Avenger_Skyranger_Recalled` narrative. Both
   go through the screen's own `OnUnrealCommand`, so a silent Enter means the recipe did not own
   input, not that the call is wrong.
3. On the tutorial's first mission (and any `bCannotBackOutSquadSelect` source) the buttons stop
   must hold ONE button — the return button is gated on `Button2.bIsVisible`.
4. The screen deliberately outlives the launch click; the recipe must detach when the stack is
   torn down for the mission, not speak the prompt again over the briefing.

**The blast on a listed target** (2026-09-14, `X2AccessTacticalReader.DescribeTargetBlast` /
`TargetBlastTiles` / `UnitsInTiles`; rules in `surfaces/tactical.md` under "Targeting / shot
HUD") — built from `SrcOrig`, compiled clean, **never heard**. Three situations exercise it, and
Gatecrasher's street offers the first two on its first turn:

1. A standard shot with the reticle on a CAR (Tab past the troopers; the enemy strip puts
   destructibles last). The target line must gain `Blast: <units>` naming every unit standing in
   the drawn box, or `Blast: nothing`, and `WARNING: friendly fire, <name> in blast` when a
   squadmate is in it. `Delete` must add `Blast area N by N tiles`; the box is the
   `DamageTileRadius` square, so a car should read `3 by 3` or `5 by 5`, never `1 by 1`.
2. The same reticle on an ADVENT Trooper must NOT gain a blast clause at all — a unit target
   draws no area unless it wears a Homing Mine.
3. A Reaper's Remote Start (any campaign with a Reaper, or `GiveAbility RemoteStart` via
   `just dev-exec`), whose targets are only explosives: the same clause, from the target style's
   own tiles, with the multiplied radius — expect a wider footprint than the shot on the same car.

The suspects if a run disagrees: an empty `Blast:` beside a visibly marked unit means
`GetUnitsOnTile` wants a different Z than the box carries (the box spans a Z range and units are
keyed by their floor tile — check whether the marked unit's `TileLocation.Z` is inside
`GetBlastExtents`); a clause on every unit target means `GetAOETiles` answers for an effect that
draws nothing.

**The cover ring — `J` / `Shift+J`** (2026-09-07, `X2AccessObjectScanner.AddCover` /
`ClaimCoverCell` + `CAT_COVER`, `X2AccessTileReader.CoverEdgeWords` / `CoverSignature`, the two
`'J'` claims in `object_scanner.cpp`; rules in `surfaces/tactical.md` under the object scanner) —
both halves compile clean and **NOTHING HAS EVER BEEN HEARD**. Every fact in it is inferred from
the SDK headers and from RT's `CoverModel.cs`; not one has been read off a running map. What to
listen for, in the order a single Gatecrasher run settles them:

1. **Does `GetCoverPoints` answer at all, and with what?** The whole surface rests on one native
   call this project has never made. `Radius` XY / `Height` Z in world units is read off
   `XComTacticalCheatManager.TestCoverPoints` (`5 * 96`, `5 * 64`), not measured. If `J` says "No
   cover nearby on this floor" while the cursor's own `Delete` readout names cover on the very tile
   under it, the call or its units are the suspect — and those two surfaces disagreeing is itself
   the diagnosis, exactly as the tile cursor and the shot HUD disagreeing was.
2. ⛔ **Shape-count the ring, do not listen to it.** This is the readout the distribution rule was
   written for: the 260-"High cover"/zero-"Low cover" bug lived for two months in well-formed
   lines. `grep -oP 'TTS!?: "\K[^"]*' X2Access.log | sed 's/[0-9]\+/N/g' | sort | uniq -c | sort -rn`
   must show **both** "Full cover" and "Half cover" leading entries, and a spread of `N tiles wide`
   values rather than every entry at one width. All-one-value means the signature or the flood fill
   collapsed, whatever the lines sound like.
3. **Does the clustering match the map?** A sandbag line or a long wall must read as ONE entry with
   a plausible width; a car or a crate must give up to four, one per face, at four different
   bearings. Every entry at "1 tile wide" means the flood fill never joined anything (suspect
   `CoverSignature` — PEEK bits leaking in would do exactly this); one entry spanning half the map
   means it joined too much.
4. **Does the selection survive stepping the cursor?** Press `J` twice, arrow the cursor two tiles,
   press `J` again: it must continue from where it was, not restart at 1 of N. That is the whole
   job of keying on the cluster's lowest linear tile index instead of its nearest member, and it is
   unverifiable any other way.
5. **Does `/` plant on a tile you could actually stand on**, and does `Backspace` then accept it?
   The tile the ring speaks is the nearest cluster member; if the plant lands somewhere the pathing
   pawn refuses, `IsFloorTileAndValidDestination` is being asked with the wrong unit or the ±1 Z
   band is picking a cell off a ledge.
6. **`J` must not double-fire.** It displaces a live vanilla ability-hotkey forwarder bound in TWO
   input sections; the log should show the claim swallowing it, and no ability should trigger.
   `Ctrl+J` must still reach the vanilla action. Check `Shift+J` walks BACKWARD and does not cycle
   the soldier (the `LeftShift` swallow covers it, but this is a new chord on that swallow).

⚠ Two design calls this run should raise rather than settle: whether `COVER_MAX = 16` entries is a
ring or a list on a dense urban map, and whether the label wants the sentence join it inherits from
the tile cursor ("Half cover north. Full cover east") mid-way through a comma-joined line.

**The Avatar project meter — its Tab stop and its change watch** (2026-09-14, `BuildDoomStop` in
`recipes/recipe_strategy.cpp`, `doom_meter.cpp`, `GameUI::ReadDoomMeter`,
`X2AccessGeoScanner.DescribeDoom` / `DescribeDoomTimer`; rules in `surfaces/strategy.md` under
"Avatar project meter") — the stop and the removal edge were HEARD on 2026-09-14 (Russian game:
`ПРОЕКТ "АВАТАР", 6 out of 12` on Tab; `Cheat Removal, down 1, ПРОЕКТ "АВАТАР" 5 out of 12` after
`RemoveFortressDoom 1`; all six `m_strDoom*` strings populated; `watching HUD, doom 6 of 12` on
every entry; no double space in the ring). One fact from that run: the game plays a queued doom
change only from the alien HQ's update, i.e. while the map is up, not in flight, and time is
running — a cheat fired under a popup waits until the player is back on the map. Still owed:

The fortress's generic addition was also heard the same evening, spoken while the tray was
detached in flight: `Проект "Аватар", up 1, ПРОЕКТ "АВАТАР" 6 out of 12` — the title twice, since
that message IS the title; folded since (rule in `surfaces/strategy.md`). A facility's addition
was heard too, driven by `FastForwardFacilityDoomTimer` through `just dev-exec` (the lever for
that edge — it ends the interval, and the next alien HQ update on the map adds the doom):
`Станция пришельцев в регионе Западная Азия, up 1, ПРОЕКТ "АВАТАР" 7 out of 12`, followed by
Central's commlink line from the transient reader. Still owed:

The countdown was reached too, with `GiveResource Doom 5` (five permanent pips; the alien AI
enters Lose mode on the next update, pushes `eAlert_AlienVictoryImminent`, and the timer replaces
the pips) — the Tab stop read `ПРОЕКТ "АВАТАР" ЗАВЕРШИТСЯ ЧЕРЕЗ: 21 ДН. 23 Ч. 57 МИН. 33 СЕК.`,
but the watch said NOTHING for the jump or the flip: the stack went blank during the popup push
and the watch reset (rule in `surfaces/strategy.md`). The reversal (`GiveResource Doom -5`) WAS
heard on that binary, as the predicted double: `Cheat Doom, down 5, ПРОЕКТ "АВАТАР" 7 out of 12`
and then the meter again for the timer leaving. Both fixed, and the replay on the new build was
heard: one line on the popup's close — `Cheat Doom, up 6, ПРОЕКТ "АВАТАР" ЗАВЕРШИТСЯ ЧЕРЕЗ: 24
ДН. 0 Ч. 0 МИН. 0 СЕК.` — and one on the reversal — `Cheat Doom, down 6, ПРОЕКТ "АВАТАР", 6 out
of 12`; the watch also held through a mission blade without re-baselining. Still owed:

1. Leave the geoscape and come back with doom pending from a mission — the entry-path animation
   must produce an ordinary edge, not silence and not a double line.
2. Unheard: a title-as-message addition folded to `Проект "Аватар", up 1, 6 out of 12` (needs the
   fortress timer: `FastForwardFortressDoomTimer`).
3. The ring's facility entry carries the pin's doom level — HEARD: four facilities at 1 and the
   fortress at 2 summed to the meter's 6. That hearing found the difficulty label voicing its
   `<font>` tag and four facilities reading alike; both fixed (region in the label, `bNoColor`,
   and the scanner's native half cleaning every payload), compiled, unheard: expect `Устроить
   диверсию на объекте пришельцев, Западная Азия, Сложно, ПРОЕКТ "АВАТАР" 1, 2450 miles west,
   1 of 5`.

**Unit conditions on the squad ring, the soldier sheet and the Targets stop** (2026-09-04,
`X2AccessHelper.UnlistedPenaltyWords` / `StunWords`, `X2AccessObjectScanner.StatusWords` and its
`bIsCosmetic` gate, `X2AccessUnitReader.EffectNames`; rules in `surfaces/tactical.md` under the
object scanner) — compiled clean, NEVER HEARD. Both halves were found in one 2026-09-04 log and
neither fix has reached a running game: the deploy could not replace the locked `.u`. What to
listen for: (1) the squad ring counts **6 of 6** on a six-soldier squad, not 7, and no nameless
"1 of 1 health, 0 tiles" entry follows the Specialist — the GREMLIN is the phantom and the tell is
that shape; (2) a stunned soldier reads "Оглушение: 2" on `,`, on `;` **and** on the Targets stop,
with the same words in all three — this is the first thing the shared oracle exists to guarantee,
so a disagreement between them is the regression to report; (3) a disoriented soldier reads the
game's own word, not silence, which is what the whole turn after the Assassin's attack sounded
like before; (4) ⚠ the ring entry is now a longer line than it was — if it reads as a paragraph
rather than a breath, the penalty list is the part to trim, NOT the pair. A shape-count of the
speech ring should show "Оглушение"/"Дезориентация" appearing at all, which it never has.

**Sight, flank and the target preview on the tile cursor** (2026-09-04, `X2AccessTileReader.
DescribeSight` / `DescribeTargetPreview` / `CursorTargetPreview`, the `passThrough` claim kind in
`input_arbiter.cpp`, the Left Alt claim in `tile_reader.cpp`; the parity case is in
`surfaces/tactical.md`) — ran live once (2026-09-04, a facility mission with a turret, an Archon
and a Purifier up). What that run settled: the sight clause and the flank clause both fire and both
vary (74 "Seen by 1", 20 "Seen by 1, flanked", 15 "Seen by 2", 6 "Seen by 2, flanked"; the flank
word tracks the cover side against the enemy's bearing), Alt fires through the pass-through claim
(eight presses, "passed modifier vk 0xA4 through", every answer agreeing with the step readout of
the same tile, "No targets" where the step carried no preview clause), and with "Show Target
Preview" ON the preview rides every step (155 lines). Still owed: (1) the game's OWN strip must
rise under the same Alt press — the log cannot see Flash, only the player can; (2) Alt+F4 in world
mode must still quit; (3) with "Show Target Preview" OFF, only Alt answers. ⚠ Two design calls that
run raised rather than settled: the per-step form speaks the game's heading ("ВИДИМЫЕ ЦЕЛИ:", the
loc string is upper-case) on every arrow press, which may want dropping on the per-step form and
keeping on Alt; and both clauses answer about the CURSOR tile even when it is out of range, where
the game's own preview answers about the pawn's clamped destination.

**The HUD graph's Targets stop** (2026-09-04, `X2AccessTacticalReader.BuildEnemyRecords` /
`SelectEnemyHead`, the stop in `recipe_tactical.cpp`; rules in `surfaces/tactical.md` under the
HUD graph) — reached once live (2026-09-04), on a ONE-head strip: "Targets, Тяжелая турель,
button, ХИТ 79 percent, 6 of 6 health, armour 2, 17 tiles, 7 north, 13 west, 4 up" — the name,
the shot HUD's own hit caption, health, armour, distance and bearing all read; no position, because
the kernel omits "1 of 1" on purpose, and Left/Right were silent on a single item as they are at
the ends of Commands. Everything else is still owed, on a mission with a pod up: Left/Right must
walk several heads in the strip's order with the best shot FIRST; a head the selected ability
cannot reach must say "not targetable" and a squadsight-only one "via squadsight"; Enter must
raise the shot HUD on that target (the poll then speaks ability and target) and the layer must
yield focus silently — `SelectEnemyHead` has never been called. Two things only that pass can
settle: whether the Selected part lands the stop on the targeted head without stranding the heads
before it (the kernel's own warning), and whether ten uncached `GetHitChanceForObjectRef` sweeps a
second cost a visible frame on a twelve-head strip — if they do, the cache key is `m_arrTargets`
plus the selected ability, not the pointers.

**The Commands stop's Chosen gate** (2026-09-04, `ChosenActivatedRawCall` in
`game_ui_tactical.cpp`; the pin is in `sdk_layout_checks.cpp`) — fixed from the 2026-09-04 log,
not yet rebuilt or run. The stop read "Button 7, button, 7 of 7" (the Chosen Info slot with no
label and no key) ten minutes before the Assassin revealed: the block handed to
`GetActivatedChosen` omitted its optional `HistoryIndex`, so the engine read index 0 — the OLDEST
frame — and answered about it, while the game's own `UpdateControls` asks about the latest. The
live check needs a mission with a Chosen: before the reveal the Commands stop must count SIX
buttons (five fixed plus the Skyranger call; no "Button 7"), and after it the seventh must read
with the game's own label and key. ⚠ If a seventh unlabelled button survives the fix, the
suspect is no longer the frame but the copy-back: log the returned pointer beside
`ReadMouseControls`' count and set both against what the screen shows (`/screenshot`).

**Four `AppendPhrase` forks removed** (2026-09-04, `recipe_shell.cpp` ×3 + `text_input.cpp` ×2) —
found by the mandated post-run shape-count of the 2026-09-04 ring, not reported by anyone, which is
the point of that audit. Two shapes were live: the dialog said **"Выйти в главное меню?. Все
несохраненные…"** (title + body joined with a bare `". "`, three times) and the save-name box said
**"ВВЕДИТЕ НАЗВАНИЕ:, edit box"** (prompt + role joined with a bare `", "` by a hand-rolled lambda).
Both left sides are the GAME's own strings and both end in a terminator. Builds clean, kernel tests
pass, never run. The check is two keypresses: Escape → exit to main menu must read "…меню? Все
несохраненные…" with ONE terminator, and Save Game must read "ВВЕДИТЕ НАЗВАНИЕ: edit box". Then
re-run the audit — `grep -aoP 'TTS!?: "\K[^"]*' X2Access.log | grep -P '[.!?;:](\.\s|,\s)' | grep -v
'\.\.\.'` must come back empty, where it returned four lines before.

⛔ **TWO COMPANION COMMITS POSTDATE THE DEPLOYED `.u`.** The build in the game is the ASI of
2026-08-31 17:00 beside the companion of 16:57, and the whole live-run debt below it was
retired against THAT pair — but `04a264c` (16:58, `X2AccessAfterActionReader` /
`X2AccessHelper` / `X2AccessSquadReader`: the soldier reading identically on both bookends) and
`169019d` (`X2AccessTileReader`) both landed after it. So the after-action card wording, squad
select's card wording and the tile readout in the game are the versions BEFORE those changes,
and whatever was verified about them does not carry. `just go-full` first, then re-walk those
three; the unification commit is the one that matters, because its whole claim is that two
surfaces now say the same thing and only a live pass can show that they do.

**The object scanner's one-key-one-entry guard** (2026-09-04, `X2AccessObjectScanner.
InsertByDistance`) compiles clean and has not run: it was written mid-mission with the game up, so
only `just uscript` ran and the deploy robocopy was killed. Diagnosed live on
`Neutralize_Vehicle` — the carried VIP owned two `XComGameState_ObjectiveInfo` components, the `B`
ring said "Objective: David Watson" at 1 of 3 then 2 of 3 forever, and the evac zone at 3 of 3 was
unreachable forward (`Shift`+`B` reached it, which is what proved `AddEvac` right). The check is
one press: on a mission with an evac zone, `B` must reach it without wrapping backwards, and the
ring must count 2, not 3. Rule filed in `surfaces/tactical.md`.

**The evac zone in the tile readout** (2026-09-04, `X2AccessHelper.DescribeEvacZone` +
`X2AccessTileReader.DescribeTile`) — same deploy debt, same one-press check: sweep the cursor into
the zone and every tile inside the game's own `GetEvacMinMax` box must say "Evac zone". Two things
the first pass has to settle. **Does the Z band read right?** The box is three levels tall (the
2D footprint ±1 in Z), so the tile a level BELOW the painted square is inside it and must say so —
that is the game's rule, not ours, and it is the half a player would never guess. **Does
"no clearance" ever fire, and does it fire anywhere sensible?** `ValidateEvacTile` is a qualifier
rather than a gate on purpose (a silent tile inside a painted zone reads as "the zone ends here"),
but if that phrase turns up on ordinary standable ground the qualifier is wrong, not the tile.
Shape-count the ring afterwards: "Evac zone" and "Evac zone, no clearance" should partition the
footprint, and an all-or-nothing split is the distribution tell.

**The rank out of the name column** (2026-09-04, `X2AccessPersonnelReader.SoldierCells` and
`X2AccessBondReader.AppendRows`, plus the two native joins in `recipe_avenger.cpp` /
`recipe_strategy.cpp`) — reported live: the roster read the rank twice, once in the name and once
in the column beside it, because `eNameType_FullNick` prefixes `GET_RANK_ABBRV`. Now
`eNameType_Full` plus a separate `nick` field joined native-side. The check is one arrow press per
screen: on the personnel roster and on the bond screen's roster, column 0 must be the name and the
nickname with NO rank, and column 1 must still be the rank. The regression to watch for is the
other half of the fix — a soldier with no nickname must not read a trailing separator (the wire
sends `" "`, and only native's `CleanText`-before-`JoinParts` order makes it vanish), so find a
rookie and listen to the end of the line. Untouched on purpose, and not a bug either: the MEMORIAL
tab's name (the game hands that row `eNameType_FullNick` itself and draws no rank column) and the
bond screen's HEADER (the game says the rank twice up there).

**Screens raised inside a cutscene** (2026-09-04, reported live: the Chosen reveal was three
minutes of silence and the player pressed Enter on a screen nobody had read). Cause was the
navigator refusing the whole layer on `bCinematicMode`; the refusal is now per screen on
`UIScreen.bShowDuringCinematic` (`engine-notes.md` has the measurement, `CLAUDE.md`'s
topmost-is-not-reachable rule the statement). `X2AccessPopupReader.DescribeChosenReveal` also
gained the nickname, the joined rival-faction line and the screen's own single CONFIRM button.
The live pass, in one Gatecrasher-style mission with a Chosen: (a) the reveal must announce
itself, and Tab must step class+name+nickname, the STRENGTHS heading and its traits, the
WEAKNESSES heading and its traits, the rival faction, then one button — Enter closes; (b) an
UNREVEALED trait must read as the game's "unknown" wording, NOT as the real trait; (c) the log
must show `cinematic mode on` WITHOUT a layer-wide `UI gated`, and the tactical HUD must NOT
attach while the cutscene runs (it declares no `bShowDuringCinematic`, so the per-screen refusal
has to keep doing what the old gate did); (d) the same fix opens the pause menu, `UILoadGame`,
`UIOptionsPCScreen`, `UIDialogueBox` and the hacking pad during a cutscene — press Escape mid-
cinematic once and confirm the pause menu now reads. ⚠ Chosen-reveal weaknesses are drawn
DISABLED when a Dark Event set `bIgnoreWeaknesses`; the game gives that state no words, so we say
nothing about it either — a known parity gap, not a defect to fix by inventing an English label.

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

**The monthly report and its Chosen page** (2026-09-03, built from source after a live screenshot
of the Chosen page the mod was silent on, never run — `resistance-report-recon.md` §6; the
`monthly report` and `chosen activity` recipes, `X2AccessReportReader` and
`X2AccessChosenReader.DescribeChosenEvents`). Needs a month end. The load-bearing items: (a) the
supply line must carry the card's own signed figure and the activity lists must read as sentences
with no `<font` markup left in the ring; (b) Enter on the report's Continue must land on the Chosen
page with one stop per column and the unmet column saying only `UNKNOWN CHOSEN`; (c) on the FIRST
knowledge gain of a campaign the warning stop must lead, its ACCEPT must dismiss the popup and the
Continue must appear only after — the wire cache is discriminated on `bShowChosenWarningPopup`,
and if the Continue never appears that discriminator is the suspect; (d) when a Chosen levelled up
the level-up popups come first and the columns must still read once they are dismissed. The page
after this one, `UIAdventOperations`, is the popup recipe's and reads its headings only — its
dark-event rows are the next hole in the chain, with `UIRewardsRecap`.

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

**The P1 sweep** (2026-09-03, built from source in one pass, never run — seven screens, each with
its own checklist in its recon doc; the companion compiled clean but the `.u` under `Mods/` is
whatever the last `just go-full` left, so **`just go-full` with the game stopped comes first**,
and every screen below that carries a `X2Access*Reader` reads as bare fallbacks until then).
- **The armory hub** (`armory`, `armory-recon.md` §1.5). The one most likely to be wrong: it is
  written against the Highlander's rewrite, so the FIRST check is `just dev-gui` showing the eight
  `ArmoryMainMenu_*Button` MCNames — a list of unnamed rows means a vanilla `XComGame.upk` and the
  whole row join is void. Then the soldier spinner (Left/Right must cycle with the header
  re-reading), a greyed PCS row's reason, and the abilities/traits toggle on a soldier with a
  trait. ⚠ The loadout's header composers moved into `X2AccessHelper` in the same change, so the
  loadout's `who` and `stats` lines are re-deployed and must be re-heard.
- **Recruits** (`recruits`, `recruit-recap-recon.md` §1.6): a row must read `name, country`, the
  details stop the bio, and Enter with supplies short must raise the game's dialog and land back
  on the list afterwards.
- **The rewards recap** — ✅ heard 2026-09-04: attached as the operation, landed on ЗАДАНИЕ
  ЗАВЕРШЕНО, read the region, the paragraph and two rewards, and Continue handed over cleanly.
  Still unheard: a mission with global effects (the GLOBAL EFFECTS: context) and a failed one.
- **The covert action result** (`covert report`, `covert-actions-recon.md` §8b): needs an action
  to complete. RESULTS must carry one node per soldier with the XP and cohesion lines, and a risk
  that fired must be the LAST value on that soldier's node.
- **The loot receipt** (`loot`, `tactical-modals-recon.md` §1.5): walk onto a corpse; the HUD
  layer must detach, the receipt attach under the corpse's name, and Enter on OK must release the
  visualizer. ⚠ The screen appears unprompted at the end of a move, so a silent stall on a
  mission is the symptom of this recipe not attaching.
- **The soldier sheet** — ◐ heard 2026-09-04: opened from the HUD stop, read the seven stats
  with `none` for a zero, Escape brought the HUD back. Fixed from that run: the class label
  (written with `SetHTMLText`, so the html field is the read). Still unheard: the EFFECTS stop —
  Tab past the stats on a soldier with a passive and confirm the PASSIVES / BONUSES / PENALTIES
  contexts and one node per effect with its description.
- **Weapon upgrades** (`weapon upgrades`, `armory-recon.md` §2.6): the pin on
  `WeaponViewOffsets` is the Highlander detector, so a build that fails there is a vanilla dump.
  Enter on a slot must open the picker with the slots re-read as `current slots`; arrowing the
  picker must re-read the stat block and the description box per upgrade with no `Accessed None`
  from the companion's change-state preview; Escape must back out without leaving. The pattern
  picker must land on the current pattern and Escape there must revert. ⚠ The colour row's
  `UIColorSelector` has no reader.
- **The PCS picker's two additions** (`armory-recon.md` §3): on `UIInventory_Implants` a red row
  must say `cannot equip` (not `cannot afford`), and the card must end in a stat line
  (`AIM +5`) paired with the RIGHT stat — the game's own header shows it under the wrong label.

**The P2 pass: customization, the character pool, manage equipment** (2026-09-04, built from
source in one pass, never run — `customize-recon.md` holds the three checklists, §1.5, §2.6 and
§3.4; the `customize`, `manage equipment`, `character pool` and `pool files` recipes plus
`X2AccessCustomizeReader`). ⛔ **Neither layer is in the game.** The ASI built clean (kernel
tests green) and the companion compiled clean SDK-side, but the game was started mid-build and
holds both files: `just install` was refused on the `.asi`, and the `.u` under `Mods/` is still
the 02:24 one (the deploy robocopy stalled and was killed, per the memory note). So **`just
go-full` with the game stopped comes first**, and until then every screen below reads as the OLD
build — the customize screens silent, the pool silent. The load-bearing items, in the order they
are cheapest to reach:
- **The pool, from the main menu, needs no campaign**: attach as the header, the rows as
  checkboxes with EDIT CHARACTER advertised, Backspace on a row into the editor (`bInArmory`
  false — no soldier spinner), Escape back with the row re-read. IMPORT into a file and Escape
  OUT of it to the file list rather than closing — that is `OnCancel`'s own two-step and the
  recipe re-attaching on the same screen with new rows.
- **The colour grid is where this pass is most likely wrong**: nothing in the layer has driven
  a `UINavigator` through `SetSelectedNavigation` on a grid child before. Enter on HAIR COLOR
  must land on the chip the game highlighted, arrows must move by row and column WITH the pawn
  previewing (`just dev-shot` beside `just dev-speech`), Enter must commit and re-read the row's
  hue, Escape must revert. If the write-back does not preview, the fallback is the chip's own
  `OnReceiveFocus` by name.
- **The picker's name** is the opener's selected row, read through the stack past the mouse
  guard; a picker attaching as *Options* means that walk found no family screen beneath it.
- **The menu screen's ALLOW checkboxes** toggle through `SimulateMouseClickOnCheckbox` — the
  one activation in the family that bypasses the input path — and TIME ADDED must read
  `unavailable` with its timestamp.
- Then the distribution audit for the hues (every soldier the same hue is the collapse), `grep
  -n '0x'` over the ring for a swatch that escaped `ColourName`, and `grep -P '  '`.

## Unexercised paths inside surfaces that HAVE run

⚠ Everything in this section belongs to a surface whose MAIN PATH has been walked live. What is
listed is the branch that has not: a state the campaign has not reached, a mod that has to be
installed, a setting that has to be off. Treat a line here as "nobody has heard this yet", not
as "this is broken".

The branches carried over when the 2026-08-31 build's live-run debt was retired (main paths all
confirmed in play):

- **Movie subtitles with Show Subtitles OFF** — the surface's main path ran on 2026-09-04 (see
  `surfaces/strategy.md`) but the option gate did not. `PumpMovie` blanks the string unless
  `UEngine.bSubtitlesEnabled` OR the movie's flag `0x80`, and we mirror both; the branch that has
  never been heard is the OFF one, which must be SILENT — our speech IS the subtitle, and a mod
  that keeps reading after the player turned subtitles off is the failure. The 0x80 half wants a
  legal/attract screen, which stays readable with the option off; that is the branch that proves
  we mirror the flag rather than just the setting.

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
- **Popup recipe, the multi-batch fix (2026-09-03; heard 2026-09-04 on eleven alerts).**
  `ReadAlertCaptured` reads EVERY batch on the alert's panel rather than the newest —
  `popup-inventory.md` §10 has the measurement. The first pass found the leak that reading
  everything invites: `spawnChildPanel`'s two name operands led every popup (`surfaces/
  strategy.md`, fixed — `IsStructuralBatch`), and the headshot mechanism read right
  (`eAlert_NewStaffAvailableSmall`'s trailing `UpdateImage` dropped, the words kept). Still owed:
  (a) `eAlert_StaffInfo` itself — ВНИМАНИЕ!, the title, the recruit's name, the bonus label and
  BOTH facility rows, in that order; (b) ~~the four- and five-batch alerts checked for
  over-read~~ — heard 2026-09-04 afternoon, and both over-read exactly as predicted: the
  covert-action confirmation spoke its slot indices ("0", "1", "2") and Retribution its level
  badge ("2"), fixed by `IsPositionalOperand` (`surfaces/strategy.md`); still unheard AFTER the
  fix. Same session, same rule: `addTweenBetween` leaked "_alpha, time, delay" onto
  `eAlert_FacilityComplete` as a STALE batch on a recycled cache index — `IsStructuralBatch` now
  names the tween ops, and the reader logs `skipped structural batch(es) … on cache index N`
  whenever it meets one, which is the line that would show a second recycled index; (c) the
  `UpdateFacilityList` deferral reading the facility rows AFTER the name. The `read N line(s)
  from the Flash capture (A+B+C, …)` log line names the contributing batches, and the reader now
  WARNS on any contributor that is not an `Update*`/`Set*` push — grep `not an Update*/Set* push`
  after a run.
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

- ⛔ **An in-engine MATINEE cutscene reads NOTHING, and nothing in the mod currently owns that
  path.** Found 2026-09-04 in the same log that confirmed movie subtitles working:
  `CIN_TP_OldGods_pt1` played for **2 minutes 13 seconds in total silence** (13:37:03 → 13:39:16,
  28 log lines, not one of them speech), and `CIN_TP_ShadowChamberReveal` and `CIN_TP_BuildOutpost`
  the same. This is NOT a `movie_subtitles.cpp` defect — that surface behaved exactly right, and
  the reason is on disk: **`CIN_XP_PreIntro` has both a `.bk2` and a `.txt` cue track and read all
  35 of its cues; the `CIN_TP_*` three have a `.txt` cue track and NO `.bk2` at all.** So the Bink
  player is handed the NAME (which is why `MovieSubtitles: movie '…' opened` is logged, and why
  `IsAnyMoviePlaying` gates the navigator with `movie 1`) but never a cue table, and the reader
  correctly stays quiet. The text exists — 21 cues under `CIN_TP_OldGods_pt1_*` in
  `XComGame/Localization/<lang>/Subtitles.<lang>`, the same file the Bink path reads — drawn by
  `RenderExtraSubtitles` timed against the MATINEE rather than a `BINKHANDLE`. ⚠ The per-cutscene
  `CIN_TP_OldGods_pt1_ART.<lang>` package is a red herring: its `SpokenText` entries are all `""`.
  `surfaces/strategy.md` assumed `UINarrativeCommLink` covers these (`HideUIForCinematics` spares
  it); this run says something does not, so the first job is to find out WHICH path draws them —
  one live pass with `just dev-props` during such a cutscene, looking for who holds the cue index,
  before any code. The gap is worth the trouble: these are the game's tentpole story beats, they
  run minutes long, and silence over one is indistinguishable from a hang.
- **Raw `". "` joins survive in the LEGACY screen listeners** — `UIScreenListener_Difficulty` (six),
  `UIScreenListener_NarrativeContent` (three), `UIScreenListener_KeyBindings` (two) and
  `X2AccessGeoScanner` (one) each build `Label $ ". " $ Value` instead of calling
  `X2AccessHelper.AppendPart`. Same defect as the four fixed on 2026-09-04, and every left side is a
  game-shipped label, so a language whose labels end in a colon voices a doubled terminator on every
  row. None showed in the 2026-09-04 ring because those screens were not walked; do not read that as
  evidence they are fine. Cheap to fix, and the natural moment is when each listener is retired for a
  recipe rather than as a sweep of its own — a listener that is about to be deleted does not need a
  patch. ⚠ `X2AccessGeoScanner` is NOT in that category (no recipe is queued for it), so it is the
  one worth fixing on its own.
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
8. The tactical HUD recipe: Soldier (2026-08-31, `X2AccessUnitReader.BuildSoldierRecords`),
   Commands (native — `GameUI::ReadMouseControls`) and now Targets (2026-09-04,
   `X2AccessTacticalReader.BuildEnemyRecords` / `SelectEnemyHead` — the live-run debt entry
   above) are built. Still owed: the spoken REASON on an unavailable ability (`AvailableCode`
   needs the companion vocabulary — the FName must never be spoken raw), and a decision on End
   Turn's focus hand-back.
9. The mission blade's two v1 punts, each a deliberate gap rather than test debt:
   `UIMission_GPIntelOptions` reads its intel options (name + cost + totals) but cannot PURCHASE
   one — the rows are `UIMechaListItem` checkboxes, so the toggle path and the purchased-state
   read are owed together — and the sitrep panel's mouse-only info button (it opens
   `UISitRepInformation`, which the popup recipe already reads) has no node. The second is low
   value: the reader already speaks each sitrep's name AND description inline.
