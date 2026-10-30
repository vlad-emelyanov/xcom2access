# Accessibility audit, 2026-10-30

Where X2Access works for a blind player, and where it falls short. Taken at `01b9dd4` on
`controller-shell-menus`. `docs/next-steps.md` remains the work list; this is a snapshot of the
state of the mod and is not kept up to date.

- **Tier 1** is access: a fact or action a sighted player has that a blind player cannot get, or
  gets wrong.
- **Tier 2** is speed: the fact is reachable, but costs many keypresses, a long line or a manual
  sweep where a sighted player needs one glance.

**Benchmark.** The Civ VI mod: a "what is next" key, a messages history that jumps to its subject,
one-line status, ranked browsers, group jumps, per-device key names, details on demand, and every
key answering.

**Evidence.** Each finding is tagged:
- **LIVE**: seen in today's run, through `tools/xctl.ps1`, on 22 old campaign saves.
- **CODE**: read in the source. Spot-checked, not every reference was re-verified.
- **DOCS**: stated by a doc only.

The live pass reached the shell, one mission, the ant farm, the Avenger HUD layer, the first screen
of the Geoscape and one alert. It did not fire a shot, run an enemy turn, or open a room, the
armory or squad select. `;` and `?` could not be tested, because `/input` has no key name for them.

## 1. The main finding: the game is not in the mode the mod assumes

`docs/input-map.md` says "The game stays in mouse mode". On this machine it does not. **LIVE**:
`UIMovie_2D.MouseActive` read `false` at the main menu and stayed false, and the pause menu listed
*View Controller Map*. So `ISCONTROLLERACTIVE` is true, and the game runs the branches almost all
of our recon says a mouse+keyboard player never takes. **The cause is the saved Input Device
option**, not the connected pad: `UIMovie.uc:118` seeds the flag from the profile, and live
`XComOnlineProfileSettingsDataBlob.m_bActivateMouse` reads false. `UIOptionsPCScreen.uc:2355-2360`
flips it from one press on the Input Device spinner. So the two modes are a player's setting, and a
controller player is in this mode by choice, which is why both must work.

Proven consequences in this mode:

| What happens | Why | Evidence |
|---|---|---|
| The HUD layer has no Commands stop, so no End Turn or other command there | `UITacticalHUD.uc:317` spawns `UITacticalHUD_MouseControls` only with the mouse active | LIVE: stops were Objectives, Score, Soldier, Abilities |
| The HUD layer's abilities read "Ability 1", "Ability 2" | The name comes from `m_strAntennaText`, which `game_ui_tactical.cpp:47` notes is PC only | LIVE |
| Every save row reached by arrow says "unavailable", then loads | The row hides its accept button unless focused, `UISaveLoadGameListItem.uc:235-243`. A hidden button reads as absent, `game_ui.cpp:1847`, and `recipe_shell.cpp:873` turns that into disabled | LIVE, also in the 16:23 log |

**The keyboard does not reach the game's own handlers in this mode.** Confirmed by physical presses
from the player: the ant farm's arrows, Escape, `E` and `Q` all did nothing, and the mod logged
passing each one to the game. Keys our own claims take still work. The pad's equivalents all worked:
the stick walked the rooms, Start opened the pause menu, and the bumpers switched soldiers with full
speech. This is what the "Injected Escape closes nothing" defect in `next-steps.md` really was. The
bindings themselves merged (`E` and `Q` at `[98]`/`[99]` of the tactical array), so the config is
not at fault. The modes therefore split cleanly: mouse mode's keyboard paths are not available to a
controller player, and each surface needs a working route in each mode.

Two more things in this mode need a look. Pad B and injected Escape both left `UIShellDifficulty`
open after Enter on a difficulty row; B had worked on Load Game earlier today. The cause was not
found. And "SCORE, 0" was a HUD stop on a campaign mission, reached through `AddTLEHudStop`; if the
screen draws no score in a campaign, that is a parity leak.

## 2. Controller coverage

| Layer | State |
|---|---|
| Shell menus | Our layer owns the pad. Walked earlier on `/pad` |
| Pause menu, strategy, tactical | Pad is vanilla, by design (`LayerOwnsPad`, `gamepad.cpp`) |

Vanilla pad plus our polling already goes further than the docs say. **LIVE**: Start opened the
pause menu and it read. D-pad walked the ant farm with room lines. A on the Bridge opened the
Geoscape, which read. An alert read. **But** on the Geoscape D-pad said nothing and A passed time
with no speech. No recipe's Tab stops are reachable on a pad outside the shell, so the tactical
HUD, Avenger HUD and every list stop are closed to it.

Inside the shell the pad has no route to Home/End, `+`/`-`, `?`, or deleting a save: X is hidden
from the game while the layer owns the pad, and save rows do not claim Backspace, so vanilla's
`FXS_BUTTON_X` delete dies. **CODE**. There are 73 keyboard claim sites against 15 mapped inputs.
The pad needs the Civ mod's answer, a ring and context-dependent buttons, not a longer table.

## 3. What works

**Newly heard today (retires debt in `next-steps.md`):**
- The cover ring `J` answers: `Half cover west, 3 tiles wide, 0 tiles, 1 action, 1 of 16`. This was
  the entry saying nothing had ever been heard.
- The exits ring `V`, the markers ring `B`, and the squad ring `,`, at 6 entries.
- The Avenger HUD layer: all five stops, entry and Escape. Its resources, objectives, to-do, events
  with date, and shortcuts all read.
- The resource strip `R`: nine figures with `low` markers varying.
- The soldier's move-readiness on the tile: `Cannot stand here`, height changes, `1 action`.

**Solid by code and earlier hearing:** the shot line and `H` breakdown; free aim with blast contents;
the tile cursor's fog gating; combat narration with cause before consequence on both live and
replay paths; popups, commlink and toasts; the mission blade; the Avatar meter; personnel,
Avenger Report and bonds as `GraphSheet`s; all five Settings tabs; 594 localization keys matching.

**The reach is wide but much is untested.** No P1 screen is unbuilt, yet the monthly report, Chosen
screens, armory hub, promotion trees, black market, recruits, Skyranger prompt and the whole
Legacy Pack remain "built, never run". They are written for mouse mode, so section 1 may invalidate
any of them.

## 4. Tier 1 gaps, most serious first

### Across the game
1. **Controller mode mismatch** (section 1). Blocks a pad player and possibly a keyboard player.
2. **`Selected` is missing, so screens land on the wrong row.** LIVE: "SELECT DIFFICULTY. Veteran
   selected", then focus on Rookie. CODE: the SDK holds the name lowercase, and `strcmp` at
   `ue3_reflection.cpp:50` misses it (same at `:95`). It breaks the landing in `KeyGraph.cpp:508-520`
   across 35 references in 9 recipe files. The one word of 289 this hits, and the fix is two lines.
3. **`?` help is wrong or empty almost everywhere.** CODE, `input.cpp:47-71`. Three listeners write
   it and nothing clears it, so it can recite the main menu mid-mission. No spoken string names
   Space, PageUp, Home or Tab, so the README is the only source.
4. **Key Bindings cannot rebind or be read, and Enter saves and exits with no warning.** CODE,
   `UIKeybindingsPCScreen.uc:424-429`, `:610-622`. `ui-inventory.md` wrongly marks it done.

### Tactical
5. **Reinforcement warning, concealment entered, objective completed or failed, and will
   thresholds are not narrated.** CODE, `X2AccessTacticalEvents.uc:101-123`. Loading into a mission
   says only the selected soldier: no mission, no turn, no concealment. LIVE.
6. **Path hazards.** The route and its markers are not spoken. Hazards are not checked along the
   path, and path risks are reported only while concealed. CODE, `X2AccessTileReader.uc:962`.
7. **Enemy overwatch is not spoken**, though the flag draws it. CODE, `X2AccessObjectScanner.uc:489`.
   The Flash side of the icon is unverified.
8. **The Reaper's Shadow reveal chance** has no reader at all. CODE, `UITacticalHUD.uc:1182-1209`.
9. **Enemy status flyovers are dropped outside the tutorial.** The comment at
   `tutorial_reader.cpp:475` says the listeners cover them; no listener does.
10. **Enemies walking into view outside a pod reveal, and all enemy movement, are silent.**
    CODE, `X2AccessTacticalEvents.uc:208`.
11. **The Lost and Chosen turn banners are missing**, on the false premise that the game draws none.
    CODE, `XGPlayer.uc:874-881`.
12. **Smaller:** will is absent from `;` and the Soldier stop; no sit-rep panel reader; no reason on
    an unavailable ability, although `X2AccessTacticalReader.uc:380-382` already speaks one.

### Geoscape
13. **Dark Events is empty and its verbs unreachable: the worst single screen.** It reads title, help
    and a bare "status". Reveal and the Active/Pending flip are mouse or pad only. It is a mandatory
    page of every month. CODE, `X2AccessPopupReader.uc:605-613` (confirmed).
14. **Nothing says whether time is running**, or what is being scanned. Stopping makes no sound, and
    Space gives no answer when scanning is refused. CODE, `UIStrategyMap.uc:1751-1795`.
15. **Not marked:** locked missions (padlock), about-to-expire sites, Chosen territory. CODE.
16. **The ring omits drawn pins:** running covert action, visible UFO, Chosen. Continent bonus effect
    and the "no contact" reason are also missing. CODE.

### Avenger and soldiers
17. **The loadout tooltip is two-thirds missing.** Attached upgrades, what armour grants and what an
    item does are unread. CODE, `X2AccessLoadoutReader.uc:179-193`.
18. **The staff picker loses the hover:** current location, status, and the bonus preview. You cannot
    tell which room you are pulling an engineer from. CODE, `X2AccessStaffSlots.uc:178-201`.
19. **Squad select offers verbs the game does not.** Boost and Build Items are ungated, so a live
    "Boost" can do nothing. CODE, `game_ui_strategy.cpp:332-346`.
20. **The soldier header drops** maximum will, missions and kills. CODE, `X2AccessHelper.uc:303-318`.
    **Personnel rows drop** bond level and availability. **The lab's research progress panel** and the
    **weapon colour selector** have no reader.

### Shell
21. **Save rows** falsely read "unavailable" (section 1), and the opening says "DELETE: Backspace",
    which is a dead key on the pad.
22. **Unbuilt:** My2K date-of-birth dropdowns, `UICredits`, `UISaveExplanationScreen`, Multiplayer.

## 5. Tier 2 gaps, by how much time they save

### Across the game
1. **No repeat, no review, no jump to the subject.** `Narrator::History()` exists with zero callers,
   and 47 `TTS::Speak` calls in the navigator and recipes bypass it. A dismissed alert or missed
   commlink is gone, and no line carries a tile. This is Civ's Control+M, and the largest single
   difference in everyday play.
2. **Lists have arrows only.** No type-ahead (`SearchText` is declared and nothing reads it), no
   group jump (`MoveRegion` has no caller). Saves are unbounded, Challenge Archives holds 107 rows,
   and a roster 30 soldiers. Civ's PageUp/PageDown with counts is the model.
3. **Silent refusals.** List edges, Enter on a disabled row, Space with no tooltip and an empty
   adjust all give nothing, so "refused" sounds like "broken". LIVE at several points today.
4. **No "where am I" or repeat in menus**, and re-announce differs by surface: `Delete`, `O`, `;`.
5. **Inconsistent verbs.** Backspace means seven different things. Space scans on the Geoscape.
6. **Literal key names** at 16 call sites, with no per-device wording. Each extension of pad support
   adds misleading hints.
7. **No mod settings:** verbosity, cue toggles and cursor options need an ini edit or do not exist.

### Tactical
8. **Who can still act?** Pips sit on every friendly flag; the ring omits them on purpose, and the
   comment's reasoning is wrong. Today it means cycling every soldier.
9. **Identical targets during targeting.** The target line has no distance or bearing, and the
   scanner stands down, so two troopers differ only by index. The keys fall through without a refusal.
10. **No turn-start brief or enemy-turn summary.** "Your turn" says nothing else; timer and
    reinforcements cost `Ctrl+X` or a ring walk.
11. **Assessing cover** costs `J`, `/`, `Delete` per candidate, because entries carry no sight or
    flank clause.
12. **Lead with what differs.** Enemy lines start with the shared name; the tile step puts the
    bearing last after up to a dozen clauses, and the cursor has no multi-tile jump.
13. **After a move:** "Moving", then no arrival, actions left or cover.
14. **Selecting from the ring:** `,` only announces. The `.` ring has no hit chance.

### Geoscape
15. **No status line and no "what next".** The date, event queue, scan state and doom are one glance
    for a sighted player. The event stop exists only on the ant farm, and the `;` status the recon
    designed was never built.
16. **The to-do strip is absent from the Geoscape**, though the game draws it there.
17. **The ring is read-only and distance-sorted:** no Enter on an entry, no needs-attention filter.
18. **No digest of what changed** after time stops; a burst of popups leaves no summary.

### Avenger and soldiers
19. **Commodity rows say nothing decisive.** Name and state only; cost and time need Tab, arrows and
    Shift+Tab per row, and "disabled" gives no reason. The largest gap against Civ's production list.
20. **No ready-soldiers view.** Disabled picker rows give no reason, and status is the fourth field.
21. **No base digest.** Attention means the To Do stop or walking 19 rooms. The Avenger Report has no
    hotkey.
22. **No weapon comparison:** a swap says nothing on the row and nothing about what it replaces.

## 6. Parity limits on the fixes

Checked against "the test is the fact": refuse **ranking cover** ("Best: 1 tile west") and **"who can
I flank from where"** as the AI's planner; **any readout of expiry hours**, next doom tick, or a
dark event's secret text; and **best soldier for a slot** or derived damage-per-turn scores.
Allowed: a sight clause on cover entries, enemy-turn summaries limited to narrated visible events,
filtered views of drawn facts, and the game's own bonus preview.

## 7. Priorities

**Decided 2026-10-30: the mod supports BOTH mouse+keyboard and controller mode.** Neither may break
the other, so every fix below is checked in both modes.

**The goal of Priority 1 is one journey, played with screen reader output only and on a controller:**
load a game, run the Avenger, launch from the Geoscape, and win a mission. Priority 2 makes that
journey faster but is not needed to complete it.

### Priority 1: complete the journey

1. **Section 1 is settled** (done 2026-10-30). The cause is the profile's Input Device option, and
   physical presses confirmed the keyboard does not reach the game's handlers in controller mode.
2. **Make the tactical HUD work in controller mode.** Commands (End Turn above all) and ability
   names need a source that does not depend on the mouse-only widgets.
3. **Small fixes:** `_stricmp` at `ue3_reflection.cpp:50,95`; treat a hidden accept button on a save
   row as "not focused", not disabled; give Key Bindings a warning.
4. **Tactical narration and Tier 1 reads:** a spoken orientation on load, concealment,
   reinforcements, objectives, turn banners, overwatch, path hazards, and enemies coming into view.
5. **Pad past the shell, minimum set:** the buttons the journey needs, in tactical (tile cursor,
   soldier, HUD stops, targeting, confirm, End Turn), on the Avenger and on the Geoscape, including
   speech for what A does there. Plain button mapping only; the ring and wheel are Priority 2.
6. **Dark Events reader** and verbs. Escape closes the screen, so it does not stop the journey, but
   it is a mandatory monthly page with nothing to read.
7. **Walk the whole journey on the pad and fix what breaks.** Armory, squad select, the mission blade,
   the Skyranger prompt, after action and the post-mission chain are "built, never run" or untested
   in controller mode, so this pass is the acceptance test, not a formality.

Item 4 can safely precede the shared layer. Companion narration already goes through
`X2AccessNarrator`, so a later history feature picks those lines up without rework. Lines will gain a
tile when jump-to-subject is built.

### Priority 2: make it fast

8. **The shared layer:** migrate speech to the Narrator, then repeat, messages list and jump;
   context help from localized per-claim text, with a test failing on a claim with no entry;
   spoken refusals; type-ahead and group jumps in the kernel.
9. **The one-key overviews:** tactical squad readiness plus turn brief, Geoscape status plus
   "what next", Avenger digest. Tactical has `[`, `]`, `\` free by reading the bindings (unmeasured
   live); the Geoscape has eleven free letters.
10. **Row wording:** commodity rows, target bearing, and status first on picker rows.
11. **The pad's ring and wheel,** with per-device key names.

## 8. Doc corrections this audit found

Still owed (`input-map.md` and `next-steps.md` are already corrected):

- `gamepad.h`: "the game stays in its mouse-and-keyboard mode" is not true here.
- `ui-inventory.md:86`: Key Bindings is not done.
- `ui-inventory.md:142`: `UIAbilityInfoScreen` is not reached from the armory in vanilla source.
- `ui-inventory.md:208`, `:177`: `UINarrativePopup` is dead code, and `UINotificationMenu` opens only
  from a pad's L3.
- `surfaces/avenger.md:359`: `UIFacility_Infirmary` is dead code.
- `CLAUDE.md`: `UIEventNotices` does retain what `X2AccessTransientReader.uc:160` polls.
- `narrator.h`: the harness does not read `History()`.
