# X2Access — screen-reader accessibility for XCOM 2 WOTC

X2Access makes XCOM 2: War of the Chosen playable for blind players. It is a
two-layer mod — the first known native (injected C++) mod for this game:

1. **C++ ASI plugin** (`src/`) — injected via Ultimate ASI Loader. Owns speech
   output, input, hooking, and UI navigation.
2. **UnrealScript companion mod** (`mods/X2AccessCompanion/`) — built with the
   official SDK. Owns game-state interpretation and narration triggers.

## Architecture: division of responsibility

Rule of thumb: **if the information is on screen (widgets, lists, focus), C++
reads it; if it is about the game simulation (units, cover, HP, turns,
objectives), UnrealScript reads it and pushes; input and speech are always C++.**

### C++ ASI owns
- **Event bus**: ProcessEvent hook (`src/process_event_hook.cpp`) patched into
  every UObject vtable at slot 72; `src/event_dispatch.cpp` routes watched
  function names to handlers. FNames defined by the mod resolve late — the
  pending-registration retry on Tick handles that.
- **Input**: `src/input_arbiter.cpp` subclasses the game window WNDPROC and
  swallows claimed key chords before the engine sees them. Handlers are queued
  and drained on the game tick. All new hotkeys and navigation input go through
  the arbiter — not GetAsyncKeyState polling, not Highlander input hooks.
  Since 2026-08-21 it also takes passive **observers** (`RegisterObserver`):
  every key-down and every `WM_CHAR` it handles, claimed or not, fanned out to
  callbacks that change NOTHING about what the game receives. That exists for
  the one job a claim cannot do — echoing what the player types into a Flash
  text field, where the keys must reach the field AND be spoken (see
  **Text-entry dialog**). ⛔ An observer runs INSIDE the window procedure and
  may do nothing but RECORD: no engine memory, no speech, no outbound calls —
  the same rule a claim's `activeWhen` follows. Act on the game tick.
- **Speech**: `src/tts.cpp` wraps **Prism** (was SRAL until 2026-08-20 — same
  library and same pin as CyberAccess, so a speech bug reproduces across both
  mods). All speech in the project goes through it. Speak the focused item only;
  navigation help on keypress; never dump all items on a screen. Prism gives us
  three things SRAL could not: twelve backends instead of three, a *debounced
  availability poll thread* in place of SRAL's per-utterance process-table walk,
  and an error return from `speak` — so a reader that DIED under us (NVDA exited;
  its RPC endpoint is gone and Prism never reconnects an existing instance) costs
  one line instead of the rest of the session. Both the availability callback and
  a failed `speak` route to the same `RebindBackend()`; a *backend instance* is
  explicitly not thread-safe, so every `prism_backend_*` call stays under
  `tts.cpp`'s mutex and the poll-thread callback only raises an atomic flag.
- **Graph A11y Kernel** (`src/Graph/`): engine-neutral navigation core
  (Factorio Access → … → RTAccess → CyberAccess lineage). STL-only by contract
  — no UE3, SDK, or Windows includes; `tests/` compiles it standalone to
  enforce this. It consumes structured `GraphRender` snapshots; this is why UI
  reading belongs in C++ (feeding it through a string bridge cannot work).
- **UI reading**: `src/game_ui.cpp` reads `UIScreen`/`UIPanel`/`UIList` object
  trees through EXPLICIT field offsets (never the generated class layouts) and
  `src/menu_navigator.cpp` turns them into GraphRenders — one *recipe* per
  screen (probe → build → name), one attached at a time. This replaces
  per-screen UnrealScript listeners screen-by-screen; existing listeners stay
  until a recipe covers their screen. Every hand-transcribed offset is pinned
  by a `static_assert` in `src/sdk_layout_checks.cpp`, so a bad transcription
  or a regenerated SDK is a build error, not a silent wrong read.
  Covered so far: main menu, settings, the whole new-campaign flow
  (difficulty + Second Wave, Ironman choice, optional narrative content,
  soundtrack picker, and the modal `UIDialogueBox` that overlays them), and the
  in-game pause menu (`UIPauseMenu`, both layers) — which is what lets a player
  leave a campaign at all.
- **Tactical tile cursor** (`src/tile_reader.cpp` + companion
  `X2AccessTileReader.uc`): the RT TileExplorer port, deliberately OUTSIDE the
  Graph kernel (the map is not a widget tree). C++ owns the keys and speech —
  arbiter claims for camera-relative arrows, Ctrl+comma/period floors, C
  recenter, Delete re-announce, Backspace two-press move-to; unshifted
  comma/period and PageUp/Dn belong to the object scanner, and SHIFT+them is now
  its reverse cycle — which is why the floor keys are on Ctrl and not, as they
  shipped for two months, on Shift (see the tactical input section: that pair
  fired `PrevUnit()` on every release). Each
  press makes one outbound call to the companion CDO
  (`GameUI::FindCompanionCDO` + `CallScriptFunction`) and speaks the
  `NativeResult` string reflected back. The script side drives the game's OWN
  `XCom3DCursor` (`CursorSetLocation`/`CursorSnapToFloor`/`MoveToUnit` — the
  pathing-pawn preview and camera follow come for free; the cursor itself is
  NOT range-clamped, only the pathing pawn's destination is) and commits moves
  through the game's own click path (`XComTacticalInput.ClickToPath`), guarded:
  commit only when the pathing pawn's clamped `LastDestinationTile` EQUALS the
  cursor tile, so a move can never land anywhere other than the tile just
  spoken. Readout is fog-parity gated (`GetTileFOWValue`: NeverSeen →
  "unexplored"; HaveSeen → static layout only; Seen → full).
  **A LEDGE outranks "Impassable"** (2026-08-21, ported from WrathAccess's
  `grid.edge_drop`): a tile with no floor of its own but ground below it is the
  railing a sighted player is looking at, and calling it "Impassable" described it
  identically to a wall — the one distinction a blind player most needs. Both
  halves of the test are the engine's: `IsFloorTile` false is the very reason the
  cursor was left hanging there (`ResolveFloor` searches only one tile down, so
  stepping off a roof keeps its height instead of falling — deliberately, so the
  edge can be FELT), and `GetFloorTileZ(tile, bUnlimitedSearch=true)` answers how
  deep, documenting its own miss by returning the source Z. A wall at ground level
  is not caught and must not be: buildings sit on the ground grid, so a tile inside
  a wall still HAS a floor and never reaches the test. That is what keeps the word
  honest — a ledge is open air, not merely something you cannot walk into.
  ⛔ **Per-edge cover: `COVER_<dir>` is PRESENCE, `COVER_<dir>Low` is a HEIGHT
  MODIFIER on it — never an alternative.** A low-cover edge carries BOTH bits, so
  the `if (dir) … else if (dirLow)` that shipped reported every covered edge as
  high: 260 *"High cover"* and not one *"Low cover"* across a whole Gatecrasher
  run (live 2026-08-21), on a map that is mostly cars and sandbags. The engine's
  own reading is `X2AbilityToHitCalc_StandardAim.GetCoverTypeForTile` (`:934`),
  comment included: *"if the cover flags in the specified direction don't have
  the low cover flag, then it is high cover"*. Test presence first, height
  second. Bit order is N/S/E/W (`0x01/0x02/0x04/0x08`, low bits four up), which
  `DIR_NORTH..DIR_WEST` already matched — the directions were never wrong, only
  the heights, and that is why a whole run of plausible-sounding readouts hid it.
  The shot HUD's `DescribeTargetCover` was right throughout (it read *"in half
  cover"* in the same run), so the two surfaces DISAGREED — a mismatch between
  two of our own readouts is the cheapest bug signal this project has. Claims stand down
  while the shot HUD is raised (`m_isMenuRaised` — targeting owns the arrows),
  while a graph recipe is attached, or when `UITacticalHUD` loses focus.
  **Mouse gate** (`src/mouse_gate.cpp`): with the mouse the active input
  device, `XComTacticalInput.Mouse_CheckForPathing` re-pushes the cached mouse
  pick point through `CursorSetLocation` EVERY FRAME (last line of the input
  state's `PostProcessCheckGameLogic`), reverting any keyboard push one frame
  later. There is **one such function per input state** and the gate now holds
  three: `Mouse_CheckForPathing` for world mode, and `Mouse_CheckForFreeAim` for
  each of `UsingTargetingMethod` and `UsingSecondaryTargetingMethod` (free aim,
  measured live 2026-08-20 at exactly **60.0 pushes/second** on the cursor with a
  stationary mouse). They never contend — the pathing function is not called from
  a targeting state and vice versa — so each surface releases exactly what it
  took. ⚠ The free-aim pair is **state-scoped**, so `FindScriptFunction`'s
  class-chain walk cannot see it at all (verified live: at class scope NOT FOUND);
  `GameUI::FindStateFunction` walks the `UState`'s own `Children`, which
  `docs/object-scanner-recon.md` §4.6 had already flagged as needed for the
  `Key_Left_Shift` fix. While the tile cursor has been used this mission AND owns world mode,
  the gate flips that one UFunction to `FUNC_Native` with `Func` → a thunk
  that consumes `EX_EndFunctionParms` and returns — script→script dispatch
  goes through exactly those two fields — and restores both exactly whenever
  targeting raises, a recipe attaches, tactical ends, or the ASI unloads.
  Clicks, free-aim (`Mouse_CheckForFreeAim`), and camera scroll are untouched;
  a mouse click while gated commits to the pathing pawn's preview of OUR tile,
  the same two-press semantics as Backspace.
- **Object scanner** (`src/object_scanner.cpp` + companion `X2AccessObjectScanner.uc`, markers
  AND `units.enemies` both VERIFIED LIVE 2026-08-20, `Tutorial_01`; `units.squad`, `ground.loot`,
  `ground.reinforcements`, six more categories and the category ring built 2026-08-20 and **not yet
  run live**): the RT `Scanner.cs` port, a PULL surface next to the
  tile cursor's push. Categories are the ring's dimension, TWELVE of them: `markers` (the tutorial's
  required-action marker and objective-carrying objects — one group, because a tutorial waypoint
  and an objective glint are the same thing to a player working out where to go), `units.enemies`,
  `units.squad`, `ground.loot`, `ground.reinforcements`, `ground.evac`, `units.civilians`,
  `hazards`, `objects.interact`, `units.lost`, `objects.towers` and `world.connectors`. Each keeps its OWN
  remembered position, so checking an enemy and then a marker loses your place in neither; the
  `CAT_*` ids are the one thing the two halves share as a NUMBER rather than a name.
  Six of the eleven needed **no native change at all** — the ring is what makes a category cost one
  `case` label instead of a key. `world.connectors` (2026-08-21) is the twelfth and the only one
  that is **SWEPT rather than enumerated**, because the engine offers exactly two handles on
  getting between Z levels and only one of them is a list: `CollectLadderLandingTiles` is global
  (the same list `XGAIPlayer_Civilian` routes panicking civilians along, and both landings of one
  ladder are separate entries — "1 up" and "2 down" at one bearing IS the shape of a ladder),
  while ramps have only the per-tile `IsRampTile` predicate, so they come out of a bounded box
  (±12 tiles, ±1 Z) instead. The bound is the right SCOPE and not just a budget: a ramp 40 tiles
  off is not the one you would take, which is why this category's empty text names the SCOPE
  ("No ramps or ladders nearby") where every other one names the map. Clustering happens LAST,
  over the already-distance-sorted candidates, so the tile that survives is always the nearest end
  of the staircase rather than whichever the sweep reached first. Keys are stable per TILE (a
  linear index, not packed decimal, which would collide on a wide grid). Why it exists at all:
  every other readout ends in "2 up" on a multi-storey map, and a blind player cannot act on that
  without knowing where the stairs are. WrathAccess needs a whole watershed segmentation of the
  navmesh to make each staircase its own room; XCOM keeps the fact in the tile grid. Four rules
  the earlier ones earned:
  **hazards group by TYPE, not by state object** (fire SPREADS, and every spread step writes another
  `XComGameState_WorldEffectTileData`, so per-object entries would grow a new "Fire" every turn for
  one fire), and each stored tile is re-checked against `TileContainsFire/Smoke/Poison/Acid` because
  a burned-out blob keeps its state object — the state is an INDEX, the world is the truth, and
  sharing that oracle with `X2AccessHelper.DescribeHazards` is why the ring and the tile cursor
  cannot disagree. ⛔ Walk `StoredTileData` **by index**: its element type
  `WorldEffectTileDataContainer` is declared inside that class and referenced NOWHERE else in the
  SDK, so cross-class scoping of the name is untested ground. **`ground.evac` speaks the origin
  CLAMPED into the zone bounds**, not `CenterLocation` — a rectangle's centre gives the wrong
  distance, the wrong bearing and a plant you may not be able to stand on. **`objects.towers` gates
  on ANY XCom unit concealed**, not the player state's `bSquadIsConcealed`, because a lone Phantom
  ranger ahead of a revealed squad is when a detection radius matters most; its empty text names the
  GATE ("Towers shown only while concealed") rather than claiming the map has none. And **enemies
  carry a sight clause**: the ring stays on squad visibility (what the unit FLAGS show), but
  `GetAllVisibleEnemyTargetsForUnit` + `GetAllSquadsightEnemiesForUnit` — the two lists
  `UITacticalHUD_Enemies` builds — decide between "via squadsight", "spotted by the squad" and
  silence, so the first unshootable target does not read as a bug in us. Keys, all
  gated on `TileReader::IsActive()` (shared, not recomputed — the two surfaces must stand down at
  the same instants): `B` next marker, `.` next enemy, `,` next squad member, **`Shift`+any of
  those three for the PREVIOUS one**,
  `PageDown`/`PageUp` next/previous within the current category, `Ctrl+PageDown`/`Ctrl+PageUp`
  next/previous NON-EMPTY category (announcing its name), `O` re-announce whatever was last said,
  `/` plant the tile cursor on it. Only three categories get a dedicated key — the ones pressed
  every turn — because the ring is what stops each further category costing a key tactical does not
  have, and it leaves `;` and `'` free for the vantage read and the inspect verb. Shift-reverses
  landed 2026-08-21 with the Shift swallow that made them safe (see the tactical input section);
  before them the only way back was `PageUp`, which walks the RING's category and therefore could
  not step one enemy back while the ring sat on markers. `Shift+Tab` is claimed by the same module
  — not a scanner verb at all, but the vanilla prev-soldier the swallow would otherwise have cost. The ring cursor
  lives on the CDO, NOT in native: stepping it means knowing which categories are EMPTY, and only
  the model can answer that. `/` acting on the LAST ANNOUNCED entry rather than on a "current
  category" is deliberate — it acts on what you just heard.
  **The two countdown categories are where the sighted/blind gap was widest**, and both gate on
  the game's OWN rule for drawing the mark rather than on a rule of ours: loot uses
  `LootExpirationTurnsRemaining > 0 && HasAvailableLoot()`, copied verbatim from
  `XComGameState_LootDrop.BuildVisualizationForLootTicked`'s `SetVisible`, so an entry exists
  exactly when a waypoint is drawn there; reinforcements use `Countdown > 0` (what
  `UITacticalHUD_Countdown.RefreshCounter` hides on) AND a `SpawnVisualizationType` that actually
  builds the flare — `BuildVisualizationForSpawnerCreation` is SKIPPED for `''`, `'TheLostSwarm'`,
  `'ChosenSpecialNoReveal'` and `'ChosenSpecialTopDownReveal'`, so those spawners leave nothing on
  the ground and get no ring entry. Speaking the reinforcement LOCATION (which the HUD counter
  never shows) is covered because that visualization leaves a persistent effect and a revealed
  area — `bStopEffect = false`, `bDestroyViewer = false`. Its words come from the game's own
  `m_strReinforcementsTitle`/`m_strReinforcementsBody`, and both countdowns speak the NUMBER,
  because a number is what the sighted counter shows. Loot is labelled from `LastLootOwnerName`,
  never `GetLootingName()` — the latter re-enters the source object and, for an interactive-object
  source, walks into the same unguarded `GetVisualizer()` deref `InteractiveLabel` sidesteps. Reachability is per-entry
  (`bIsDestination`): spoken for markers and objectives, which you walk to; suppressed for enemies
  and the tutorial's attack marker, where "out of range" would read as "you cannot do this" about
  a perfectly shootable target. Enemies reuse
  `X2AccessTacticalReader.DescribeTargetCover`/`DescribeTargetHealth` verbatim, so the scanner and
  the shot readout can never disagree about whether something is flanked. Note the ceiling on
  enemy labels: `GetName(eNameType_Full)` gives a non-soldier its CHARACTER TEMPLATE, so every
  ADVENT trooper reads alike and only distance/bearing/index tell them apart — which is parity,
  not a gap, because the game gives a sighted player no per-enemy identifier either.
  **Dev-harness note:** the companion CDOs are now inspectable — `/props?cdo=<Class>`, and
  `?instance=` falls back to the CDO when nothing live matches (`GameUI::FindDefaultObject`).
  That is the only way to see this subsystem's model; before it, the speech ring was the sole
  evidence. **NO REGISTRY** — RT needs a ticked `WorldModel` because Unity entities are mutable
  references; XCOM's `ObjectID` is a stable int over a versioned history, so the list is rebuilt
  per keypress and only the selection persists (an int key plus its tile), keyed by ID so a
  completed objective cannot silently shift what is selected. The tutorial contributor reads
  `XComTutorialMgr.NextPlayerAbilityContext`, NOT the six `StaticMeshComponent`s (routinely hidden
  at the origin while an action is still required), gated on `bIsTutorialAllowingControls` — the
  replay's own "I have stopped and handed over control" flag — and honours the parity guard:
  `AbilityTemplateName` / `SourceObject` / `PrimaryTarget` / `TargetLocations` /
  `MovementPaths[0]` only, NEVER `ResultContext`, which carries the recorded hit/miss of a
  scripted shot the player has not fired. Objective markers are deliberately NOT fog-gated —
  that is the game's own rule (`UIUnitFlag`/`UISimpleFlag` special-case an `ObjectiveInfo`
  component to keep the flag visible). Compass bearings come from
  `X2AccessTileReader.OffsetWords`, now static and shared, so there is exactly ONE calibration of
  `NORTH_DX`/`EAST_DX` and the two surfaces cannot drift apart about which way north is.
- **Tutorial message reader** (`src/tutorial_reader.cpp` + companion
  `X2AccessTutorial.uc`, built AND VERIFIED LIVE 2026-08-20 — all three text
  surfaces spoke on the first run, `Tutorial_01` frame 150): the tutorial is a
  REPLAY (`XComTutorialMgr extends XComReplayMgr`), so it plays a recording back,
  halts at every frame that needed input, and REJECTS anything that does not
  match — with a sound and **no text at all**. Four surfaces carry its words and
  three are readable. The modal `UITutorialBox` keeps `Title`/`Desc`/`ButtonHelp`
  in real script properties (it is deliberately NOT a graph recipe: it holds
  nothing navigable — `OnInit` calls `DisableNavigation()` on both the container
  and its button — and a recipe would claim Enter/Space/Escape, which are exactly
  how the box is dismissed). The anchored blade and every other flyover come from
  polling `UIWorldMessageMgr` (see the ProcessEvent bullet below). The
  bottom-right HUD blade goes straight into Flash and its words are recovered by
  the companion, walking the history back from `CurrentHistoryFrame` for the
  newest un-removed `XComGameStateContext_TutorialBox`. **Markup is TRANSLATED,
  not stripped**: `%KEY:...%` already resolves to a bracketed, binding-aware key
  name, but the mouse/arrow glyphs arrive as `<img src='rmb_icon'>` and
  `CleanText` would leave a hole exactly where the instruction is. Damage
  flyovers are skipped and plain world messages are read only inside the tutorial
  — `X2AccessTacticalEvents` already narrates those from the game state, and both
  speaking would double every hit. Two more parity rules earned live 2026-08-20:
  a plain world message is spoken only while `m_bVisible` (it is anchored in the
  WORLD, and `UpdateMessageLocation` clears that bit off screen — an off-camera
  "Unconscious" was read by nobody and named nothing), and it stays PENDING
  rather than dropped so panning onto it still announces it; **blades are exempt
  and must be**, because they sit at the DESTINATION rather than at the camera
  and the tutorial's own instruction blade reads `vis=0` routinely.
  ⛔ **World messages are queued PER UNIT** (`X2Action_ConcealmentLost` queues one
  at each `Unit.GetLocation()`), so identical text from several slots is normal
  and expected — a whole squad going concealed publishes the word once per
  soldier. Do NOT "fix" that by de-duplicating on text: two soldiers falling
  unconscious would collapse into one line — the fix is NAMING the anchor, and
  that is what `DescribeMessageAnchor` does: it resolves
  `m_kFollowObject.ObjectID` through the history (gated on `CurrentHistoryFrame`,
  or the replay answers with the object as it WILL be) and reuses
  `X2AccessTacticalEvents.IsNarratable` / `UnitName`, so a flyover line and a
  combat-event line agree about who they name and obey one fog rule. Blades are
  excluded on purpose: an anchored blade is an instruction about a PLACE, so
  prefixing whoever stands there would be actively wrong.
- **Hacking screen** (`src/hacking_screen.cpp` + companion
  `X2AccessHackingReader.uc`, phases 1-2 built AND VERIFIED LIVE 2026-08-20,
  phase 3 built the same day but **not yet run live**;
  recon in `docs/hacking-screen-recon.md`): `UIHackingScreen` is the purest case
  of "the widget tree holds no words" — `/gui` on it prints **`1 panels`**, the
  screen and no children, because every string goes into Flash through
  `MC.FunctionOp` / `ActionScriptVoid` and is retained nowhere. So the companion
  supplies ALL of it and this module hand-transcribes **no offsets at all** —
  which here is correctness, not tidiness: the Highlander interleaves
  `m_UsingSkulljackScreen` and `m_hackCanceled` into the bitfield at 0x450, so
  `bWaitingForInput`'s **MASK** is 0x20 under the Highlander and 0x08 in vanilla,
  and `static_assert`s pin offsets, not masks. Phase 2 fixes more than narration:
  the screen's `OnUnrealCommand` handles only the controller's `FXS_DPAD_*`, so
  on mouse+keyboard the reward choice is reachable **only by clicking a card on
  the 3D pad in the soldier's hands** — verified live 2026-08-20, two synthetic
  Right presses left `SelectedHackRewardOption` at 1. `Left`/`Right` move a
  cursor across the (up to) three cards and, on cards 1-2, hand the screen its
  own `OnUnrealCommand(FXS_DPAD_LEFT/RIGHT, FXS_ACTION_PRESS)` — which is the
  int `X2HackRewardTemplateManager.AcquireHackRewards` reads back off that very
  screen object, so there is one source of truth; `Delete` re-announces.
  `Enter`/`Space`/`Escape` are deliberately NOT claimed, and every claim drops
  once the hack is committed (the reveal ends with "press any key", and a key we
  swallowed would not dismiss it) — the companion reports that through a second
  reflected-out property, `NativeState`. **Phase 3 speaks the outcome from the
  pad, not the simulation**: `bWaitingForInput` (set from
  `OnCommand("HackEnded")`) for WHEN, and for WHAT the award set the game itself
  would build — because ⛔ **the lit cards are NOT the rewards**.
  `AcquireHackRewards` only ever considers `i == 0 || i == SelectedHackRewardOption`,
  so an unselected middle card cannot be awarded whatever the meter did, while
  the meter lights panels by threshold and the thresholds DESCEND with the odds
  — passing card 3's lights cards 1 and 2 as well, and on a MISS `InitiateHack`
  further inflates the bar for drama
  (`Max(DisplayedScore, Rand(100 - SelectedHackChance - 1))`, whose own comment
  says it "does not affect the outcome of the hack at all"). Naming the lit
  prefix therefore credits the player with rewards that were never in play: that
  is what shipped 2026-08-20 and was corrected 2026-08-21, the first live reveal
  having read correctly only because the player had selected card 2, where
  prefix and award coincide. What `m_rewardsTaken` DOES answer is the only
  question that matters — a panel lights at `100 - GetHackChance(index)` and the
  inflation is bounded at `100 - SelectedHackChance - 2`, strictly under the
  selected card's own threshold, so `m_rewardsTaken > SelectedHackRewardOption`
  is an exact did-the-gamble-land test. The award is then rebuilt the way the
  game builds it: slot 0 always, UNLESS it is `bBadThing`, in which case it is
  the PENALTY and lands only when the gamble missed — and then it is named, so
  "missed" says what it cost rather than only that there was a cost. That also
  beats the banner Flash prints, which reads `HackRewards[RewardIndex].bBadThing` and so says
  **Успех!** even on a miss whenever slot 0 is ordinary. Cancelling is detected
  on the C++ falling edge (`OnCancel` closes the screen before anything can be
  read, and a pad that goes away uncommitted was cancelled). Detection reuses `ReadTacticalGate()`,
  whose `blocker` reads `UIHackingScreen` at exactly the instant the tile cursor
  stands down, so the two can never disagree. ⛔ It must NOT gate on
  `StackState::Interactive()`: the stack reports **cinematic=1** while this
  screen is up, and legitimately so — `HideUIForCinematics` skips every screen
  with `bShowDuringCinematic`, which this screen sets. Only the first
  `Min(len, 3)` rewards are ever spoken: the movie has three panels and
  `RefreshPreviewState` hard-loops `Index < 3`, so on the tutorial door (whose
  array is FIVE, object rewards plus `BattleData.TacticalHackRewards`) the tail
  is unreachable for everyone. Chances come from the screen's own
  `GetHackChance` (never re-derived — sitreps, Gremlins and Skullmining all move
  the inputs) and are truncated, matching Flash: 6.42 and 2.75 print as 6% and
  2%.
- **Free aim / AoE preview** (`src/free_aim.cpp` + the free-aim section of companion
  `X2AccessTacticalReader.uc`, built AND VERIFIED LIVE 2026-08-20 — the tutorial's grenade step
  was completed with it: `/` aimed at the object scanner's own *"Бросить гранату сюда"* marker,
  the arrows stepped from there, and the throw landed; recon in `docs/free-aim-recon.md`): the
  third tactical input mode. An ability whose target is a POINT
  (`bFreeAim` set, `AvailableTargets` EMPTY) reads its aim point off the game's own
  `XCom3DCursor` — grenades and everything deriving from their method, cones, lines, the blaster
  launcher, the evac zone. **Vanilla has no keyboard aiming path at all**:
  `UITacticalHUD_AbilityContainer.AbilityRequiresTargetingActivation` returns false unless a
  controller is active (`:155`), so the D-pad branch is dead code, and `SetSelectionOnInputPress`
  (`:405`, on every PRESS) sends a bare arrow to `CycleAbilitySelection` — which ABANDONS the
  throw. Claiming the arrows therefore displaces nothing (ability cycling stays on `1`-`9`), and
  this is where the old "targeting mode: we claim nothing that steers" rule is narrowed rather
  than broken — see the tactical input section. Aim keys are the tile cursor's, deliberately:
  arrows step, `Ctrl+,`/`Ctrl+.` change floor, `Delete` re-announces (ONE claim, branching in
  script, never two racing for the chord), and the scanner's `/` retargets the AIM point instead
  of planting the walk cursor. `Enter`/`Space`/`Escape` stay unclaimed — the game's own confirm
  and cancel already work. Detection and every word live in the companion, which reports the mode
  through a second reflected-out property (`NativeState`), published by `TacticalHUD::OnTick`'s
  existing 150 ms poll — so the layer adds **no offsets, no `static_assert`s and no extra
  outbound call**. ⛔ Three traps, each earned in recon: **do NOT reuse
  `X2AccessTileReader.SetCursorTile`** — it zeroes `m_fMaxChainedDistance`, which under a
  targeting method IS the throw-range limit (`X2TargetingMethod_Grenade.Init:43` sets it from
  `GetAbilityCursorRangeMeters`; live 960 units = 15 m = 10 tiles), and its comment saying that
  is safe "because our keys stand down while the shot HUD is raised" is exactly what free aim
  falsifies. **Do NOT trust the pushed tile as the answer** — for a thrown object the blast
  centre is `GrenadePath.GetEndPosition()`, where it comes to REST, so a bounced grenade lands
  elsewhere; that resolves a frame late, which is why `PushAim` verifies the clamp synchronously
  (reading `GetCursorFeetLocation()`, the exact inverse of the offset `CursorSetLocation` adds,
  compared as a horizontal distance with half a tile of slack — the clamp lands on a SPHERE and
  owes the tile grid nothing) while the landing divergence is reported by the poll. And **blast
  contents come from `Ability.GatherAdditionalAbilityTargetsForLocation` asked about OUR tile**,
  not from the method's `MarkedTargets` cache, because that cache only refreshes inside the
  method's own `Update()`; it is the same list `ConfirmAbility` submits, fog-gated through
  `X2AccessTacticalEvents.IsNarratable` (the native gather does not care what the squad can see,
  so that filter is a real leak guard). Friendly fire is read from that list rather than from the
  method's `bFriendlyFireAgainstUnits`, which is one frame stale; the game's own modal runs over
  a WIDER list (`CheckForFriendlyUnit` uses the visual `GetTargetedActors`) and stays the
  authority — ours is the early warning that keeps a blind player from ever reaching it.
  ⚠ The first live run found the one thing 150 ms of poll interval does NOT buy: the arc resolves
  a frame AFTER the push, so a single divergent reading is not evidence. It announced *"Blocked,
  lands 6 north, 1 west"* about a throw that then landed perfectly — the reading was the pre-jump
  arc, still pointing back at the shooter. A divergence now has to survive two consecutive polls
  before it is spoken: a stale frame cannot, a real obstruction always does.

- **Avenger ant farm** (`src/avenger.cpp` + companion `X2AccessAvengerReader.uc`, phase 1 built
  2026-08-21 and **not yet run live**; recon in `docs/avenger-view-recon.md`, §11 is what was
  built): the first surface where the game already HAS the keyboard navigation and merely never
  turns it on. `UIFacilityGrid.InitScreen` builds its whole eight-way room adjacency graph inside
  `` `if( `ISCONTROLLERACTIVE) `` — and that macro is **not a hardware check**, it is the 2D
  movie's mouse-active flag (`Core/Globals.uci:207`). ⛔ Do NOT flip the flag: mouse-inactive mode
  changes which HUD the game builds (the same reason the tile cursor uses a swallow instead of
  `DeactivateMouse`), and here it would make the keyboard WORSE — `UIAvengerShortcuts` routes
  `2`-`6` through a highlight-then-confirm path in the controller branch instead of jumping
  straight to the room. The surgical fix is to call the gated function ourselves, from script,
  once per grid instance. So this layer is **narrate-only and claims no arrows**: the game's own
  `UINavigator` does the moving and brings the camera swoop, the focus delivery and the highlight
  box with it. CONFIRMED LIVE 2026-08-20 before any of it was built (via the dev harness's
  `/call`): six injected arrows walked the hardcoded graph exactly, `Left`/`Right` moving at all
  being the proof — the un-ungated fallback is `UINavigator`'s linear Prev/Next with
  `HorizontalNavigation` false, so before the call they did nothing. The 51 DIAGONAL links are
  `FXS_VIRTUAL_LSTICK_DIAG_*` only and have no keyboard chord; parsing all 152 `AddNavTarget*`
  calls shows the cardinal-only graph is fully strongly connected across all 19 rooms, so losing
  them costs reachability of nothing. Re-arming keys off the GRAPH'S OWN EMPTINESS
  (`FacilityOverlays[0].Navigator.NavigationTargets.Length == 0`) rather than off screen
  identity: idempotent (the game's function clears every overlay first), self-healing across a
  map transition, a no-op on a controller, and it needs no `UIScreen` reference on the CDO.
  Every word comes off the OVERLAY, never the game state — `Label`/`Status`/`Queue`/`bLocked`/
  `bUnderConstruction`/`m_bClearing`/`IconTypes`/`BGColor` are each written by a setter under a
  dirty check immediately before the matching Flash op, so the retained value IS what Flash was
  last told, and the class holds not one `XCOMHISTORY` access. That is what buys parity for free:
  the room NAME is set under `if( bIsFocused )`, so it appears exactly when the game shows it.
  Two parity rulings from the live pass, and they go OPPOSITE ways. ⛔ A tutorial-locked room's
  name must NOT be spoken — `UpdateTutorialLocked()` is the one update branch that sets no title
  in any case, focused or not, and a screenshot confirmed a sighted player selecting that room is
  told nothing either; speaking a name we looked up ourselves would hand a blind player what the
  game withholds from everyone. ✅ A locked room's REFUSAL must be spoken — `OnConfirm` opens with
  a bare `if( bLocked ) return;`, no sound and no popup, but a sighted player SEES the room dimmed
  (`eUIState_Disabled`, `iAlpha` 0) and never presses it twice; saying *locked* on arrival is
  parity with the dimming, and it needs no `Enter` claim (`FXS_KEY_ENTER`/`FXS_KEY_SPACEBAR` are
  handled unconditionally by the overlay already). POSITION is spoken always, because the
  highlight box's place in the ship is visible even when no title is drawn and, in the tutorial,
  is the only thing distinguishing one arrow press from the next — core slots as
  `row R, column C` (indices 3-14, row-major, 3 wide x 4 tall), the seven fixed rooms as a
  spatial phrase, and ⚠ POSITIONS ONLY, never facility names. The gate is the game's OWN test:
  `GameUI::ReadAvengerGate` asks whether `UIFacilityGrid` is the FIRST stack entry, matching
  `GetCurrentScreen()`'s definition (first non-none, no state-bit filter), because that is what
  `UIFacilityGrid.OnUnrealCommand` checks before handling anything on mouse+keyboard. The
  companion additionally honours the grid's own `bIsVisible` — the documented exception to "never
  gate readiness on `bIsVisible`", since here the GAME gates on it. One claim: `Delete`
  re-announces (never contends with the tile cursor's or the shot HUD's `Delete` — those gate on
  tactical), displacing only `[Engine.PlayerInput]`'s `Toggledebugcamera`, which stays reachable
  at `Ctrl+Delete` for free.

- **Save / load** (`src/game_ui.cpp` + the save/load recipe in
  `menu_navigator.cpp` + companion `X2AccessSaveReader.uc`, built AND VERIFIED
  LIVE 2026-08-21 — the load screen read its whole list on the first run and
  Backspace deletions rebuilt and re-announced it each time; recon in
  `docs/save-load-recon.md`): `UILoadGame` and
  `UISaveGame` are one screen twice over — the same three fields at the same
  offsets (`m_iCurrentSelection` 0x410, `m_arrListItems` 0x424) and the same row
  widget — and they are the SHELL's first case of the tactical HUD's problem:
  `UISaveLoadGameListItem.UpdateData` resolves the date, name and mission into
  LOCALS, pushes them into Flash and retains only the `OnlineSaveGame` struct it
  built them from. So native reads structure and the companion re-derives the
  words from that struct, mirroring `UpdateData` line for line — with no
  `XCOMHISTORY` and no `GetSaveGames` re-query, because the screen already asked.
  The 12-hour clock conversion is a NATIVE function
  (`FormatTimeStampFor12HourClock`), which is the whole reason the formatting is
  not done in C++: reproducing it would say *14:22* to a player whose screen says
  *2:22 PM*. Unlike the ant farm, nothing here is un-ungated —
  `OnUnrealCommand` already walks the list with the arrows on mouse+keyboard and
  `RawInputListener` already deletes on Delete/Backspace — so this is a reader,
  not a rescue, and it is deliberately the thinnest recipe in the file.
  **ONE NODE PER ROW**: the row shows two buttons, but a second column would
  replace the one number a blind player cannot get any other way — *save 4 of
  12* — with *1 of 2*, because `GraphBuilder` stamps positions WITHIN a row
  holding more than one item. ⛔ The layer must NOT claim `Delete` here: every
  other Delete claim in the project (tile cursor, shot HUD, hacking pad, ant
  farm) stands down while this screen is up, so the key is free and it is the
  screen's own delete verb — named once on attach in the row's own localized
  word, since the game's nav help offers nothing but Back on mouse+keyboard.
  Activation goes through the row's own `AcceptButton.Click()`, which dispatches
  new-save-rename versus overwrite for free, **after** setting the selection —
  and that order is load-bearing rather than tidy: `UISaveGame.OnAccept` and
  `OnDelete` ignore the control they are handed and act on
  `m_iCurrentSelection`, so a click sent while the two disagreed would overwrite
  or delete the WRONG SAVE. Words are cached on the ROW POINTER VECTOR (the
  navigator re-renders ten times a second and the save screen's new-save row
  re-runs `FillInHeaderForSave`, which walks the history): a save list changes
  only through `BuildMenu()`, which respawns every row panel, so a changed
  pointer list is an exact, self-invalidating cache key. **Reading ORDER is not
  the row's order**: the row draws the wall-clock timestamp at the head of its
  first line, and speech puts it LAST, after the save description, the game type
  and the mission. That is a speech decision rather than a parity one — a
  sighted player takes the row in at a glance, while speech is serial, and a
  list of autosaves differs in its description, not in the minute it was
  written, so a leading timestamp is the part you sit through before reaching
  the part you wanted. The companion emits `time` and `desc` as separate fields
  for exactly this; `UpdateData` concatenates them.
- **Text-entry dialog** (`src/text_input.cpp`, built AND VERIFIED LIVE
  2026-08-21 on the save-name box): `UIInputDialogue` — naming a new save, and
  the error reporter. The player types into a **Flash TextField**, which no
  screen reader can see, so nothing echoes their own keystrokes back. ⛔ It is
  NOT a graph recipe and it claims NOTHING: a recipe attaching turns on the
  navigator's arrows, Enter, Space, Tab, Home and End, every one of which the
  player is using to type or to answer — and there is no spare chord for a
  read-back key either (`F1`-`F4`, `?` and Ctrl+Shift+S are claimed globally in
  `src/input.cpp`; everything else is a text-editing key). It **observes and
  reads**: `InputArbiter::RegisterObserver` (a passive fan-out added for this)
  records that a key happened, and the field itself answers what it did.
  **The Flash side is READABLE — measured live** (see
  `docs/save-load-recon.md` §7.1 for the table): AS2's global `Selection` class
  resolves through `ActionScriptInt` **with or without the `_global.` prefix**,
  so `Selection.getCaretIndex()` / `getBeginIndex()` / `getEndIndex()` give an
  EXACT caret and selection, `Selection.getFocus()` names the focused field as a
  full AS path, and `GetVariableString("<path>.text")` reads the contents. ⚠ The
  probe's own trick is worth keeping: a path that does not resolve answers **0**
  while the real accessors answer **-1** when nothing is focused, which is how
  "it worked" was told from "it silently did nothing".
  `GameUI::ReadFocusedTextField` is all four in one call. It costs two leaked
  `FString`s per read — the `ConsoleCommand` bargain — so it runs behind a
  keystroke and on a 500 ms heartbeat (which catches a mouse click placing the
  caret), never free-running. Spoken: the prompt plus the starting text on
  arrival, and *selected* with it when the whole prefill is selected — which it
  IS (`begin 0, end 12, caret 12` live), meaning the next keystroke replaces the
  name and no caret model could ever have known. Then per keystroke: the
  inserted run, or what Backspace removed, or the newly selected text, or the
  character the caret landed on. A key the field REFUSED changes nothing and
  stays silent. `getFocus` earns its place beyond the caret — it says WHICH
  field, which the crash-report dialog (a title box and a body box with Tab
  between them) needs and "the text" alone cannot answer.
- **Outbound script calls**: C++ may invoke any UnrealScript function via
  ProcessEvent. Always resolve the UFunction BY NAME at runtime and hand-roll
  the param struct (`GameUI::CallNamedFunction`; `CallStaticNative` for
  `static` functions, which take the CLASS object as `this`). The generated
  SDK's typed wrappers are unusable — see **The generated SDK's function
  wrappers are broken** below — and only `GameDefines.cpp` is compiled.
  Native functions need no special handling: call them exactly like script
  functions.

### UnrealScript companion owns
- **Game-state narration**: event listeners on Community Highlander hooks
  (CHEventListenerTemplate, XComLWTuple events) for turn changes, ability
  results, damage, mission state. Resolution through `XCOMHISTORY` and
  localized strings is script-natural; never reimplement it via C++ reflection.
- **Description helpers**: `X2AccessHelper.uc` static functions (cover,
  hazards, units on tile), shared by listeners and the tile reader.
  `X2AccessTileReader.uc` is the worked example of the native-callable pattern:
  functions invoked on the CDO with simple params, result written to its
  `NativeResult` string property, reflected out by C++ right after the
  synchronous ProcessEvent returns. CDO rule: value-typed state only, never
  object references (GC). `X2AccessSaveReader.uc` is the smallest instance of
  the same pattern and the clearest statement of the rule behind it: it re-runs
  `UISaveLoadGameListItem.UpdateData`'s own formatting over the struct that row
  RETAINED, and touches neither the history nor `ONLINEEVENTMGR.GetSaveGames` —
  the screen already asked, and asking again could answer differently.
  ⛔ It also uses `Chr(10)` rather than `"\n"` for the separator the engine
  wrote into `Header.Description`, for the same reason `X2AccessNarrator`'s
  separators are printable.
- **Tactical combat narration** (`X2AccessTacticalEvents.uc`, built 2026-08-20,
  **VERIFIED LIVE 2026-08-21 on Gatecrasher** — the normal mission the tutorial
  could never be): one `CHEventListenerTemplate`, tactical only, nine events. The
  Gatecrasher run exercised ability, damage, death, turn handover and both
  concealment events end to end, attributed and localized —
  `Ряд. Jane Kelly: Выпад at Солдат "Адвента", хит.` /
  `Сектоид: Пси-реанимация at Офицер "Адвента".` /
  `Ряд. Jane Kelly, 3 damage, 4 of 7 health.` — with `Your turn.`/`Enemy turn.`
  arriving on the Alert channel and marked interrupting in the speech ring. That
  also settles the script half of Bridge v2, which the tutorial could not:
  `X2AccessNarrator`'s `default.`-backed push and delimited drain really do carry
  a burst. (An earlier 2026-08-20 tutorial run proved only registration and the
  NATIVE half; ⛔ do not read its `Промах!` as evidence of this listener — that
  was a world-message flyover `TutorialReader` hands straight to
  `Narrator::Say`.) Still unexercised: panic, shields, evac.
  This is what T1 forced — the flyover/notice/banner tap is not buildable, so
  the game's own event bus is the path. **Deferral is a per-event decision, not
  a default**, and for one action it is SPLIT ACROSS BOTH visualization windows:
  `ELD_OnVisualizationBlockStarted` for the ACTION (`AbilityActivated`) and
  `ELD_OnVisualizationBlockCompleted` for its OUTCOME (`UnitTakeEffectDamage`,
  `UnitDied`), because a listener otherwise fires at state submission, seconds
  ahead of the animation; `ELD_OnStateSubmitted` for structural facts
  (`PlayerTurnBegun`, concealment, panic, shields, evac), which are tied to no
  animation and would be dropped entirely by a state that never builds a
  visualization block. ⛔ **The split is what puts cause before consequence, and
  a shared window cannot.** Priority orders listeners for the SAME event only;
  between events the order is the GAME's trigger order, and that is backwards
  for us — `XComGameState_Unit` triggers `UnitTakeEffectDamage` (`:5874`) and
  `UnitDied` (`:6022`) while effects are applied, and
  `XComGameStateContext_Ability` only reaches `AbilityActivated` afterwards under
  its own *"Trigger post-activation events"* comment (`:191`). All three on one
  window spoke the outcome before the attack (live 2026-08-21: *"ADVENT Soldier
  killed."* then *"Jane Kelly: Slash at ADVENT Soldier, hit."*, and worse for
  damage — you learned you had been shot before you learned by whom), and they
  landed in the SAME 100 ms drain, so the narrator could not reorder what it
  could not tell apart. Blocks run sequentially, so the split also stops two
  abilities resolving in one burst from interleaving; it is what the game's own
  HUD does (`UIUnitFlagManager:55`, `UITacticalHUD_Enemies:68`). Note the replay
  narrator needs none of this: walking frames in order gets the human order for
  free. Three details
  that needed the game's own source: damage is SUMMED over the entries whose
  `DamageResult.Context` matches this state (`DamageResults` accumulates for the
  whole mission and one grenade lands as several entries); only abilities that
  actually roll get a result word (`X2AbilityToHitCalc_DeadEye` always hits, so
  the word would be noise), and it is the game's own localized
  `m_aAbilityHitResultStrings`; and the interrupt pass is skipped, because
  `AbilityActivated` re-triggers per interrupt step. Fog parity via
  `CanXComSquadSeeTarget` with ONE documented exception — a unit stops being
  visible the moment it dies, so a kill the squad CAUSED is always announced.
- **Replay narration** (`X2AccessReplayNarration.uc`, built 2026-08-20, not yet
  run live): the tutorial's stand-in for the dead event bus. Polled from
  `Narrator::OnTick` (100 ms, next to the drain), it walks history frames and
  emits ability / damage / death / concealment / turn lines through
  `X2AccessTacticalEvents`'s shared builders — `BuildAbilityLine`,
  `BuildDamageLine`, `NarrateDeath` — so a shot reads identically whether an
  event or a frame produced it. ⛔ **Never read past what the player was SHOWN**:
  the whole tutorial script is in the history at mission start, so the cursor is
  `XComGameStateVisualizationMgr.LastStateHistoryVisualized` (the game's own
  "which frame is on screen", what `UIUnitFlag` uses), clamped again to the
  replay's `CurrentHistoryFrame`. That also syncs narration to the animation,
  which is what `ELD_OnVisualizationBlockStarted` buys the live listeners. ⛔ The
  catch-up bound is a RUNAWAY guard, not a pacing one: normal playback advances
  that cursor in steps of **25-50 frames**, because most history frames carry no
  visualization of their own and `LastStateHistoryVisualized` jumps to the end of
  a block when the block completes. A 24-frame bound therefore read as "seek" on
  ordinary play and dropped 597 frames across one tutorial run — among them an
  overwatch shot the player watched and never heard. It is 512 now, and the only
  real seek is `StartReplay`'s own `JumpReplayToFrame`, which the first-poll
  branch already adopts. A skip is always logged, never silently swallowed. Two
  consequences elsewhere: a MISS reaches `UIWorldMessageMgr` as an ordinary world
  message, not a damage one (`QueueWorldMessage` stamps `eHUDMessage_Damage` only
  when `m_iDamagePrimary > 0`), so `tutorial_reader.cpp` now drops flyovers
  matching the game's own result strings — the attributed line outranks the bare
  word; and it resolves the GRI by hand rather than through `` `REPLAY `` /
  `` `XCOMVISUALIZATIONMGR ``, because those dereference a null GRI in the shell
  ten times a second.
- **Speech vocabulary** (`X2AccessHelper.GetInputVocabulary` /
  `GetResultFlyovers`): the game's OWN localized words for markup native has to
  speak. Our built-in table is English, which is wrong the moment the player is
  not — "Нажмите right mouse button" sat three words from the game's own
  `[ПРАВАЯ КНОПКА МЫШИ]` live. Keyed by markup token (an `<img>` `src`, or
  `KEY:NAME`) so a missing entry degrades to the English word instead of shifting
  a positional table. ⚠️ `%KEY:LMB%` is the one that MUST be there:
  `X2MissionNarrativeTemplate.KeyBindingMarkups` resolves only RMB/ENTER/TAB/Q/E/P
  and `%KEY:LMB%` is substituted solely by `UIUtilities_Input.InsertPCIcons`,
  which the blade path never calls — so it reached speech raw.
- **Tutorial blade recovery** (`X2AccessTutorial.uc`): the one tutorial surface
  native cannot read. Walks the history BACKWARDS from
  `XComTutorialMgr.CurrentHistoryFrame` (never past it — the whole tutorial
  script is already in the history at mission start, so the raw history end is
  the future) collecting removed `MessageID`s as it goes, and resolves the newest
  surviving UNANCHORED blade through the mission's own
  `X2MissionNarrativeTemplate.GetObjectiveText`, exactly as
  `X2Action_ShowTutorialPopup.GetTutorialBoxText` does. A removal context carries
  only `MessageID` + `Remove` — `BoxType` reads as its default — so removals must
  be matched by id, never filtered by type.
- **Legacy screen listeners**: `UIScreenListener_*.uc` polling listeners with
  the `AccessibilityData` bridge. Working system; migrate to native UI reading
  incrementally, do not expand this pattern to new screens without reason.

### The bridge (script ↔ native)
- Script→native: listener writes its `AccessibilityData` string property, then
  fires the empty sentinel function `X2Access_Speak()` via SetTimer; the
  ProcessEvent hook catches it, reflects the property out
  (`Hooks::ReadListenerProperty`), and speaks. This is the only script→native
  path — **DLLBind is conclusively unavailable in XCOM 2** (the SDK compiler
  rejects the keyword and the runtime support is compiled out of the exe).
- Native→script: outbound ProcessEvent calls (see ScriptCall above).
- **Bridge v2 — BUILT 2026-08-20** (`X2AccessNarrator.uc` + `src/narrator.cpp`),
  the queued/channelled path the v1 sentinel cannot be: producers call
  `Alert()` / `Combat()`, which append to an `ENTRY_SEP`-joined string on the
  CDO, and C++ drains the whole burst in ONE `DrainNarration()` per 100 ms (the
  outbound call is the cost, not the payload). Four channels on the native side
  — `Focus`/`Aid` interrupt AND clear the queue (the player pressed a key;
  what they asked for outranks what happened), `Alert` queues in order but marks
  the BATCH as interrupting, `Combat` queues. That `Alert` rule is the subtle
  one: an alert that simply cleared the queue would eat the damage line
  explaining the kill it follows. Flush is on the frame pump, so the queue
  orders *within* a frame and adds no latency; past the flush the screen
  reader's own queue takes over, which is the honest limit (a burst can be
  cancelled wholesale by an interrupt, never trimmed — hence the 24-line cap,
  logged on overflow at both ends). A 128-line ring with channel, SOURCE and timestamp
  backs the future speech-scrollback key. ⛔ `Narrator::Source` is not a channel:
  it records whether a line was read off a surface the game itself displayed
  (`Flyover`) or derived from the game state (`State`), and a `State` line is
  dropped when an identical `Flyover` line was said in the last 5 s. **One way
  only, and never State-against-State** — inside the tutorial both paths run at
  once and an untargeted ability comes out word-for-word twice ("Osei:
  Overwatch"), with the flyover ~150 ms ahead; but identical text from two state
  events is load-bearing, because two ADVENT Troopers really are both called
  "ADVENT Trooper" and collapsing a double kill loses a kill. Same reasoning as
  the per-unit world-message rule, applied to the other side of the bridge. ⛔ **`default.X = ...` does not
  compile unless X is a `config` property** — the UnrealScript compiler says
  *"Can't change default values of non-config properties"*, and a static
  producer has no other route to per-class storage, so the queue vars are
  `config(Game)` with no ini section shipped (nothing is ever written back;
  `SaveConfig` is never called). The payload is a delimited STRING rather than a
  dynamic array because mutating one through `default.` is a pattern the shipped
  SDK sources use nowhere.

## Engine facts

- Heavily modified UE3, 64-bit only, Scaleform GFx UI. No anti-cheat; Firaxis
  ships EasyHook64.dll themselves — injection is tolerated.
- ProcessEvent: UObject vtable slot 72, address `moduleBase + 0x143B50`
  (found via Ghidra + `tools/OffsetFinder`). GNames/GObjects offsets live in
  the generated SDK constants; `src/sdk_globals.cpp` validates them with
  sanity heuristics before init proceeds.
- Runtime `FString` is `TArray<wchar_t>` (UTF-16) even though the generated
  SDK declares `char` — `ue3_reflection.cpp` handles the conversion.
- **The ProcessEvent hook sees only NATIVE→script dispatch.** Verified live
  2026-08-19 with F4 discovery during a mission: a full HUD refresh (Tab
  switched soldier, `m_arrAbilities` went 4 → 5, every panel repainted)
  produced not ONE UI script function — no `UpdateAbilitiesArray`, no
  `SetStats`, no `OnUnrealCommand`, no `RealizeTargets`. What does arrive is the
  engine calling into script: `Tick` / `PostRender` / `HandleInputKey` /
  `BeginState` on `XComTacticalInput`, `FilterButtonInput` on `UIMovie_2D`, and
  the simulation surface the native visibility and pathing systems call —
  `CallAbilityMeetsCondition`, `GetVisibleFlankingEnemiesOfLocation`,
  `NumActionPointsForMoving`, `TargetIsEnemy`, `UpdateGameplayVisibility`,
  `GetEnvironmentDamagePreview` (our own outbound calls show up as traffic too,
  which is how `IsAnyMoviePlaying` appears). That is UE3 behaving as documented:
  a script→script call runs through `CallFunction` → `Function->Func`
  (`ProcessInternal`), never the virtual slot 72 we patch. **So "watch a UI
  function by name" is not an available technique.** The obvious narration tap
  — `UIWorldMessageMgr.Message` (every flyover, with damage and crit label),
  `UIEventNotices.Notify` / `NotifyBanner`, `UITacticalHUDMessageBanner.SetBanner`
  — cannot be HOOKED this way. ⚠️ But `UIWorldMessageMgr` can be **POLLED**
  (found 2026-08-20, `src/tutorial_reader.cpp`): the manager RETAINS every
  message it is showing in `m_arrMsgs` (0x450, stride 0x78, text at +0x10) with
  `m_arrActiveMessages` (0x460) indexing the ones on screen — so damage numbers,
  crit labels, "Unconscious", the `UITacticalTutorialMgr` cursor-help strings
  and the tutorial's anchored blades are all readable after all. Only the
  *unanchored* HUD blade is genuinely lost to Flash. The rest of the paragraph
  stands: `UIEventNotices` and `UITacticalHUDMessageBanner` retain nothing, and
  game-event narration belongs to companion
  `X2EventListener`s; reaching script→script traffic would need a second hook on
  `ProcessInternal`, which would hand us the whole script call graph and is the
  one thing that would change this calculus. Ghidra pass 2026-08-20 costed it:
  `ProcessInternal` is **not virtual** — the UObject vtable block around slot 72
  matches UE3's declaration order exactly with no gaps (72 ProcessEvent,
  73 ProcessDelegate, 74 ProcessState, 75 ProcessRemoteFunction,
  76 InitExecution, 77 GotoLabel, 78 GotoState,
  79 CallFunctionByNameWithArguments), so it needs an INLINE hook (the first
  real use for MinHook, not vendored yet). Its address needs no signature scan:
  `UFunction::Func` (+0xF0) of any SCRIPT function is the dispatch target, and
  ProcessEvent calls exactly that at `0x140143E40` — read it once at init and
  the hook survives a game patch. The frame it receives carries the params
  ALREADY EVALUATED, so existing reflection reads apply unchanged; layout under
  pack(4) is `Node` +0x14, `Object` +0x1C, `Code` +0x24, `Locals` +0x2C (the
  `Code` offset cross-checks with what `mouse_gate.cpp` derived independently
  from `execSetLocation`). Native calls would NOT appear in such a tap (a
  native's `Func` is its exec thunk), and script calls made through ProcessEvent
  would appear TWICE.
- Some screens are Flash-driven with no UIList (e.g. key bindings): neither
  layer can enumerate rows, and the same is true of anything a screen wrote into
  Flash and did not retain — a text field's caret and selection above all. The
  route in is `GFxMoviePlayer`'s own path-string API, ALL of which is script-
  callable and therefore reachable by outbound ProcessEvent:
  `GetVariableString` / `GetVariableNumber` / `GetVariableBool` / `Invoke`, plus
  the `ActionScriptInt` / `ActionScriptString` / `ActionScriptObject` wrappers
  that INVOKE a method at a path (which is how `UIInputDialogue.AS_GetInputText`
  itself works — `Movie.ActionScriptString(MCPath $ ".GetInputText")`). Since
  2026-08-21 `/call` takes arguments, so probing a path costs one HTTP call
  (`just dev-flash`). ✅ **PROVEN LIVE 2026-08-21**: GFx's path resolution DOES
  reach AS2's global `Selection` accessors, prefix optional, and TextField
  properties (`.text`, `.length`, `.maxChars`, `.caretIndex`) read straight
  through `GetVariableString`. `docs/save-load-recon.md` §7.1 has the measured
  table. A path that does not resolve answers 0 or an empty string, so a probe
  needs a value it can tell apart from silence.
- ⛔ **No gameplay event fires in the TUTORIAL — it replays history, it never
  submits it** (proven live 2026-08-20: a full tutorial run produced zero lines
  from `X2AccessTacticalEvents`, with the listener provably registered).
  `X2GameRuleset.SubmitGameStateContext` (`:292`, `:401`) routes every context to
  `XComTutorialMgr.HandleSubmittedGameStateContext` instead of
  `SubmitGameStateContext_Internal` while `` `REPLAY.bInTutorial ``; on a match
  that handler calls `ResumeReplay()`/`StepReplayForward()` and returns `true`,
  DISCARDING the player's context, and `XComReplayMgr.StepReplayForward` merely
  advances `CurrentHistoryFrame` and calls `BuildVisualizationFrame` on a frame
  that was already in the history at mission start. So `ContextBuildGameState`
  never runs and `X2EventManager.TriggerEvent` is never reached — for our
  listeners or the game's own. Two consequences: **anything built on the event
  bus must be tested on a normal mission** (Gatecrasher — campaign with the
  tutorial checkbox off), and the tutorial reader's `UIWorldMessageMgr` polling
  is not a nicety there but the ONLY narration source, which is exactly why
  plain world messages are read inside the tutorial and nowhere else.
  ✅ **Answered by `X2AccessReplayNarration.uc`** (built 2026-08-20): in a
  replay the history IS the event stream, so it walks the frames instead of
  waiting for events, through the SAME `X2AccessTacticalEvents` builders — one
  place decides wording. Its gate is `bInReplay`, i.e. exactly when the
  listeners cannot fire, so the two paths can never both narrate one action.

- **The tactical HUD holds no text.** Unlike the shell screens, every panel of
  `UITacticalHUD` is a write-only pipe into Flash: `SetStats` resolves name,
  rank, class, aim and effects out of the game state and hands them straight to
  `MC.BeginFunctionOp` / `QueueString`, retaining nothing
  (`SrcOrig/.../UITacticalHUD_SoldierInfo.uc:199`) — and the shot HUD, info
  panel, enemy list, countdown and banner all do the same. So `/gui` on
  tactical prints 159 panels and almost no words, and the read-the-widget
  recipe pattern does NOT transfer. What IS readable: the ability bar
  (`UITacticalHUD_AbilityContainer.m_arrAbilities` + `m_iCurrentIndex`, and per
  icon `m_strAntennaText` / `m_strCooldown` / `m_strCharge` / `IsAvailable` —
  the antenna text only while `Movie.IsMouseActive()` — verified live
  2026-08-19: populated and localized, e.g. slot 0 "ВЫСТРЕЛ" with
  `m_strHotkeyLabel` "1", so the whole ability bar including each entry's
  `AvailableCode` reason needs NO companion call), the enemy refs
  (`UITacticalHUD_Enemies.m_arrTargets`), `UITacticalHUD.CurrentTargetID` /
  `m_isMenuRaised` / `ConcealmentMode`, and `UIObjectiveListItem.Data`
  (`ObjectiveDisplayInfo.DisplayLabel`, plus its timer). Everything else — HP,
  armour, action points, effects, ammo, hit chance — exists only in
  `XCOMHISTORY`. In tactical the widget tree therefore supplies STRUCTURE and
  SELECTION while the companion supplies the WORDS: the content provider is a
  companion CDO, not offset transcription in `game_ui.cpp`, which is also what
  makes the structured bridge (v2) a prerequisite rather than a nicety.
- Best public documentation of Firaxis' UE3 internals: robojumper's
  "Too Real" series (https://robojumper.github.io/too-real/). Highlander event
  reference: https://x2communitycore.github.io/X2WOTCCommunityHighlander/

## Hard rules (each earned by a crash or a design dead end)

- **When a fact is on a screen, read it FROM that screen — even when the game
  state would also answer.** Parity is defined against what a sighted player
  sees, and the tutorial is the case that makes this bite: it replays history
  and never submits states (`XComTutorialMgr.HandleSubmittedGameStateContext`
  discards the player's context, so `ContextBuildGameState` never runs), yet the
  UI on top is driven identically — the hacking pad animates, the panels light,
  the flyovers appear. A sighted player cannot tell the two missions apart. So a
  readout sourced from `XCOMHISTORY`, a submitted context or the event bus is
  silently DEAD on the mission a new blind player starts with, while one sourced
  from the screen's own properties works in both for free — with no
  tutorial-shaped special case. Prefer, in order: the screen's own fields and
  functions (`UIHackingScreen.GetHackChance`, `SelectedHackRewardOption`,
  `m_rewardsTaken`), the state objects the screen itself cached to populate
  Flash (`UnitState`/`ObjectState` — same objects, same values, no re-lookup),
  and only then a fresh history query. `X2AccessHackingReader.uc` is the worked
  example: not one `XCOMHISTORY` access in the whole class. This does NOT
  contradict the rule of thumb above — the companion still owns the words; what
  it changes is WHERE the companion reads them from.
- **Outbound ProcessEvent calls: game thread only.** Trigger from the Tick
  drain (InputArbiter handlers already run there), never from the WNDPROC or
  the init thread.
- **Nothing in `DllMain(DLL_PROCESS_DETACH)` may join a thread or touch a
  library.** `ShutdownAll()` runs under the LOADER LOCK, and at process exit the
  other threads are already dead, so a join never returns. This is why
  `TTS::Shutdown` deliberately does NOT call `prism_shutdown()` or
  `prism_log_shutdown()`: the first deletes the context, which joins the
  `BackendEnumerator` jthread, then calls `CoUninitialize` and a run of
  `__FUnloadDelayLoadedDLL2` (i.e. `FreeLibrary`); the second joins the logging
  drain. Both are textbook loader-lock deadlocks. What Shutdown does instead is
  the safe subset: clear the log handler so nothing can re-enter this module
  after `Log::Shutdown`, `prism_backend_stop` so a player quitting does not hear
  the rest of the sentence, and then LEAK the context on purpose — the process is
  going away. Same reasoning as the dev server's bounded `Stop()`.
- **Never cache UObject pointers across frames without revalidation** — GC
  moves on. Re-find via GObjects/SCREENSTACK or validate before use.
  **Type-revalidation is not enough**: after a world change the old screen is
  still a well-formed instance of its class for as long as GC leaves it
  allocated, so "is it still a UIFinalShell?" happily says yes about a dead one.
  A cache may short-circuit a rescan only while the instance is still USABLE
  (focused, showing) — otherwise rescan and take the newest. Getting this wrong
  cost 63 seconds of dead silence after exiting a campaign to the main menu,
  and it looked like a hang rather than a stale pointer.
- **A screen's index in GObjects says nothing about its age, and a removed
  screen keeps all its state bits.** UE3 reuses freed slots, so a freshly
  spawned screen routinely sits at a LOWER index than corpses of its own class
  (observed: live `UIPauseMenu` at 383831, removed ones at 383887 and 383932).
  Removal sets `bIsRemoved` (0x80) and clears nothing else, so a corpse still
  reads inited/visible/focused. "Take the last instance" therefore latched onto
  a dead screen that passed every readiness test, read as empty, and left the
  layer silently detached with the menu open. Screen scans must FILTER by the
  usability predicate (which must reject `bIsRemoved`), never pick by position.
  The durable fix is walking `UIScreenStack.Screens` (0x6C) instead of GObjects.
- **Topmost is not the same as reachable.** Starting a campaign pops the
  narrative screen and leaves `UIShellDifficulty` underneath inited, focused and
  top of the stack for the ~12 s the loading movie plays, so the layer narrated
  a screen that had visibly gone. The stack carries the game's own answer in its
  flag dword at 0x60: `IsInputBlocked` (0x1), `ScreensVisible` (0x2, so hidden
  when CLEAR) and `bCinematicMode` (0x8) — the same gates the game consults
  before opening its pause menu. Any of them set means nothing is navigable;
  `MenuNavigator::OnTick` detaches and refuses to attach while gated.
- **Never gate screen readiness on `bIsVisible`** (UIPanel flag dword bit
  0x20). The game leaves it FALSE on screens it is plainly showing: a live,
  focused pause menu reads 0x259 and so does the exit-to-main-menu confirmation
  dialog over it — `bIsNavigable|bIsFocused|bIsInited`, no visible bit. Testing
  it meant silence at a modal the player then answered blind. Use
  `bIsInited && bIsFocused` (bit 0x10 + 0x40) — `bIsFocused` is what the screen
  stack maintains for "top of the stack" — and for `UIDialogueBox`, whose queue
  is the real state, `bIsInited` plus a non-empty `m_arrData`. Child widgets are
  the exception: a BUTTON's `bIsVisible` is toggled deliberately and does mean
  hidden.
- **Never store UIScreen references as class vars on UIScreenListeners**
  (listeners live on CDOs; a held reference blocks shell-world GC and crashes
  map transitions). Re-find via `` `SCREENSTACK.GetFirstInstanceOf() ``.
- **Three generator settings must stay FALSE** in
  `deps/CodeRed-Generator/Engine/XCOM2/Configuration.cpp`, and each cost real
  debugging: `m_useConstants` (with it on, all 29388 wrappers resolved their
  `UFunction*` from the owning CLASS's index — `GObjObjects()->at(IDX_CLASS_…)`,
  the same constant `StaticClass()` uses — so every one handed `ProcessEvent` a
  `UClass`; off, they emit `UFunction::FindFunction("Function Pkg.Class.Name")`,
  which also survives a game patch), `m_removeNativeFlags` (clearing
  `FUNC_Native` around the call is actively wrong here: a native's `Script` array
  is a 2-byte STUB, not a body — `XComEngine.IsAnyMoviePlaying` has
  `Script.Count` 2 against 1663 for `UIPauseMenu.OnChildClicked` — so clearing it
  runs the stub and returns a ZEROED param block indistinguishable from a real
  answer), and `m_removeNativeIndex` (same family). Natives dispatch correctly
  through plain `ProcessEvent` with the flags left alone;
  `X2FiraxisLiveClient.IsLoggedIn` is `FUNC_Native` and has always worked that
  way here. With those off there is no `SdkConstants.hpp` any more — it existed
  only to feed the index-based wrappers, and the GNames/GObjects offsets we
  actually use live in `GameDefines.hpp`.
  The generator is also patched to emit valid C++ rather than leaving it to a
  repair pass: `.` `<` `>` `~` and space added to `m_unsafeChars` (UnrealScript
  names really do carry dots — `OverriddenTangentVector.X`),
  `UniquifyFunctionNames()` applied in all three emission passes (one class can
  declare the same function once per STATE — `XComInputBase` declares `Key` ten
  times — which collapsed into ten identical declarations and ten identical
  `_Params` structs), and the `GameDefines.hpp` pack region emitted
  automatically. `tools/fix_generated_sdk.py` now repairs exactly one thing
  (undefined suffixed enum references) and doubles as the regression check: any
  other line in its output means a generator fix broke.
- **Anything in `GameDefines.hpp` must be included inside `#pragma pack(4)`.**
  Every `SDK_HEADERS/*.hpp` opens its own pack region; GameDefines does not, so
  its types (FNameEntry, TArray, FName, FString) silently take the including
  TU's pack. Mixed packs across TUs is an ODR violation that moved
  `FNameEntry::Name` from 0x14 to 0x18 and broke GNames lookup entirely. The
  The generator now emits the region itself, so a regeneration keeps it;
  `sdk_layout_checks.cpp` pins the result either way, so a lost pack region is a
  build error rather than a silent runtime failure.
- **SEH-guard every read into engine memory** (`__try` around reflection and
  hook dispatch), and keep the ProcessEvent hot path cheap: bail on FName
  index before doing anything else.
- **A widget's declared `Type` does not tell you what the widget IS; the
  visible child does.** Every Second Wave (Advanced Options) row reads
  `UIMechaListItem.Type` = `Description` while showing a live, visible
  `UICheckbox` — the same object whose `bChecked` the campaign start reads in
  `GetSelectedSecondWaveOptionNames`. `ReadOptionRowRaw` only looked at the
  checkbox inside its `Type == Checkbox` branch, so those rows reported
  "unchecked" forever. Read state from the widget POINTER, and take a row's
  kind from a widget that is VISIBLE (`SetWidgetType` hides the widgets it
  switches away from but leaves their pointers behind, so presence alone is not
  enough — visibility is). The difficulty screen's own bottom rows do read
  `Type` = Checkbox, so a screen can hold both kinds at once and "it works on
  the other rows" proves nothing.
- **A readout that never sounds wrong can still always be wrong — audit its
  DISTRIBUTION, not its lines.** The tile cursor called every covered edge "high
  cover" for two months. Every individual line was well-formed, correctly
  directed and fog-gated; nothing in a log read as a defect. What exposed it was
  counting: **260 "High cover", zero "Low cover"** in one Gatecrasher run, on a
  map made of cars and sandbags. A field whose values are enumerable and whose
  observed range collapses to one value is broken, whatever the lines sound like.
  So after any live run, shape-count the speech ring — `grep -oP 'TTS!?: "\K[^"]*'
  X2Access.log | sed 's/[0-9]\+/N/g' | sort | uniq -c | sort -rn` — which in the
  same pass named the three noisiest wording defects in the build by frequency.
  A cheap corollary: **when two of our own surfaces answer the same question,
  make them agree or find out why they don't.** The shot HUD's
  `DescribeTargetCover` said "in half cover" in that very run while the tile
  cursor said "High cover"; the two disagreeing was the whole diagnosis, and it
  is the reason `X2AccessHelper` exists as a shared oracle.
- **When a control seems not to respond, prove which HALF is broken before
  fixing either.** The above cost a full debugging cycle spent on the write
  path — reading the game's source for `OnSecondWaveOptionClicked`, ruling out
  the param layout against the SDK dump, regenerating the whole SDK, adding a
  verify-and-fall-back that set the checkbox itself — because the readback said
  "unchecked" and the readback was the broken thing. The toggle had been
  landing correctly the entire time. **One screenshot settled it in seconds**:
  the boxes were visibly ticked, exactly the rows the player had pressed. Our
  own readout is never evidence about the game's state; when the two disagree,
  ask the sighted user what the screen shows (or read the widgets live with
  Frida) BEFORE theorizing about the call.
- **Escaping a UIList to sibling buttons** needs
  `Navigator.CheckForOwnerHandling` plus deferred (0.01 s timer) focus — never
  mutate Navigator state inside its own `Next()`.
- **Sighted parity decides what gets a tab stop.** The stop list mirrors what is
  actually ON SCREEN; anything past that is a separately-labelled scan/aid
  surface and must never hand the player information a sighted player cannot
  get. Both halves have already bitten during tactical design: the WOTC
  initiative order looks like an obvious stop and is CHEAT-GATED in retail
  (`UITurnOverlay.ShowOrHideInitiative` shows it only when
  `CHEATMGR.bShowInitiative`), so narrating it would leak enemy turn order;
  and the standard HUD carries no turn counter — the only counters are
  `UISpecialMissionHUD`'s pair, and that screen (always pushed) shows them only
  while a `UiTimer.ShouldShow`, so an untimed mission reads `Timers = 0` and
  displays nothing. The game's own answer to "what does this HUD offer" is
  `UITacticalHUD_MouseControls` — prev/next soldier, end turn, rotate camera,
  call Skyranger, Chosen info, cancel shot, shot info, soldier info — and on
  mouse/keyboard that panel IS the command surface: `CharInfoButton` /
  `EndTurnButton` / `SkyrangerButton` are spawned only in the CONTROLLER branch
  of `UITacticalHUD.OnInit` and read null live. Build stops from that list,
  never from a sketch of what a HUD "should" have. Aids that genuinely
  compensate for lost vision (the tile cursor; distance and bearing on the
  enemy strip; a speech scrollback,
  because speech is serial where flyovers are simultaneous and persist ~5 s)
  stay — labelled as aids, not sold as parity.
- `src/Graph/` stays STL-only. UE3/host concerns go in adapters outside it.

## Build & deploy

- ⛔ The MSVC runtime is pinned **`MultiThreaded` (/MT), cache-`FORCE`d**, before
  `add_subdirectory(deps/prism)`. Not optional and not cosmetic: Prism sets
  `CMAKE_MSVC_RUNTIME_LIBRARY` itself off `CMAKE_BUILD_TYPE`, which is EMPTY
  under the Visual Studio multi-config generator we configure with, so an
  unpinned cache silently links a `/MT` Prism into a `/MTd` X2Access in Debug.
  `BUILD_SHARED_LIBS OFF` must also be in the cache before that line — the static
  path is what makes Prism define `PRISM_STATIC`, so `<prism.h>` stops saying
  `dllimport`. The upside of /MT is that the `.asi` imports OS libraries only:
  no VC++ redistributable on the player's machine.
- Configure: `cmake -B build -A x64` (game/SDK paths default to
  `D:/SteamLibrary/...`; override with `-DXCOM2_GAME_PATH=` /
  `-DXCOM2_SDK_PATH=`).
- C++ ASI: `cmake --build build --config Release` →
  `build/bin/Release/X2Access.asi`.
- Everything + deploy to game: `cmake --build build --config Release --target
  deploy` (copies the ASI to `Binaries/Win64/scripts/` and chains
  `build_uscript`, which compiles the mod via X2ModBuildCommon and
  self-deploys to `XComGame/Mods/`).
- ⛔ **`build_uscript` HANGS FOREVER if the game is running** (measured
  2026-08-20: >10 min against 15 s normally). The running game holds
  `XComGame/Mods/X2AccessCompanion/Script/X2AccessCompanion.u` open, the copy
  step gets a sharing violation, and X2ModBuildCommon has no retry and no
  timeout — it spins in `Start-Sleep` and never returns, so it looks like a slow
  compile rather than a lock. Diagnose it by the SDK-side artifact: if
  `<SDK>/XComGame/Script/X2AccessCompanion.u` has a FRESH timestamp while the
  copy under the game's `Mods/` still has the old one, the compile succeeded and
  only the deploy is stuck. `just kill` first, then rebuild. The `.asi` is locked
  by the running game too, but that half FAILS FAST with a plain sharing
  violation — it is only `build_uscript` that turns the same lock into a
  silent hang, which is what makes it worth a rule.
- Kernel tests (no game needed): `ctest --test-dir build -C Release`.
- Loading: Ultimate ASI Loader's `version-x64.dll` sits in
  `Binaries/Win64/`; the ASI logs to `X2Access.log` next to itself, rotating
  the previous three sessions to `X2Access.1/2/3.log` on every start — so a
  relaunch no longer destroys the evidence for whatever you were chasing.
  Timestamps carry milliseconds; screen-transition latency is unreadable
  without them.

## Dev harness (dev builds only)

Loopback HTTP server for observing and driving the live game — sibling of the
RTAccess (8772) and CyberAccess (8773) harnesses; this one owns **8774**, so
all three can be armed at once. Source: `src/dev_server.cpp`.

**Double-gated.** Compile: `-DX2ACCESS_DEVSERVER=ON` (the justfile's
`devserver` variable, ON for the local tree; OFF means the TU is not built and
a release `.asi` holds no socket code at all). Runtime: a marker FILE,
`Binaries/Win64/scripts/X2Access.devserver` (optional first line = port
override) — a file rather than an env var because neither Steam nor
`just run` inherits our environment, and the marker survives a relaunch.
`just arm` / `just disarm` write it.

**Threading.** The accept thread owns the socket and never touches engine
memory. Engine-facing endpoints queue a job that `DevServer::Pump()` drains on
the GAME thread, first statement of the `EventDispatch::SetFramePump` lambda —
the same rule as outbound ProcessEvent calls, extended to reads. `/health`,
`/speech` and `/log` answer **off-thread** from process-memory rings, which is
what makes them useful when the game thread is the broken thing. That is not
hypothetical: a launch that fell into a `GetQueryData - Timed out while waiting
for GPU to catch up` loop 14 s after boot left every game-thread endpoint
timing out for minutes while `/health` kept answering with a rising
`pump-age-ms` and `/log` + the game's own `Launch.log` named the cause. A
game-thread timeout therefore means "the game is not running frames", NOT "the
harness is broken" — check `pump-age-ms` before suspecting anything else, and
remember the pump rides ProcessEvent traffic, so it is only as alive as the
game is. `Stop()` runs from
`DllMain(DLL_PROCESS_DETACH)`, i.e. under the loader lock, so it does a
**bounded** 1 s wait on the accept thread and detaches rather than `join()`ing
— an unbounded join there can deadlock the game's shutdown.

Endpoints:

- `/health` — build stamp and `pump-age-ms`.
- `/speech?since=N` — every line `TTS::Speak` was handed, cursor-paged.
- `/log?since=N&tail=N` — the ASI log ring, cursor-paged.
- `/status` — movie + load screen, the screen stack's gate flags, the attached
  recipe, the tile-reader gate, and a top-first dump of `UIScreenStack.Screens`
  with each entry's flags decoded (the not-inited and removed entries the
  finders skip are shown too — the point is to answer "what is on screen" for a
  screen that has no recipe yet).
- `/tutorial` — the tutorial reader's state: replay frame, whether the player or
  the replay owns controls, the on-rails rejection count, the modal, and every
  active world message with its RAW text beside what the reader would SPEAK.
  That last pair is the point — markup translation is the half of this feature
  a screenshot cannot check.
- `/exec` — a console command (POST body, or `?cmd=`). See below.
- `/input?keys=Down,Down,Enter` — synthetic keys. Default tier is **SendInput
  scancodes** after raising the game window, which drives the WHOLE game (UE3
  reads Raw Input, so a VK-only synthetic press is ignored — scancodes are not
  optional). `&mode=kernel` instead injects into the arbiter's own queue via
  `InputArbiter::InjectSynthetic`, driving ONLY our layer, working with the
  game backgrounded, and reporting per key whether a claim took it. `&hold=`
  and `&gap=` are milliseconds.
- `/screenshot[?path=&foreground=0&output=N]` — PNG of the monitor the game is
  on, via DXGI Desktop Duplication (`src/screen_capture.cpp`).
- `/gui[?depth=N]` — the widget tree under a screen: one line per `UIPanel`
  with its class, address, state bits and the text it carries.
- `/props[?filter=<substring>]` — every property the object's class chain
  declares: **offset, type, name and live value**, grouped by declaring class.
- `/call?fn=<Function>` — invoke a **zero-parameter** script function on the
  selected object. Built for the recurring shape of accessibility bug in this
  game: not a missing feature but an UNREACHABLE one — the game already has the
  function and only calls it from a branch a mouse+keyboard player never takes.
  `UIFacilityGrid.InitializeNavigationGrid` (the ant farm's whole eight-way room
  adjacency graph) sits behind `ISCONTROLLERACTIVE`; `UIToDoWidget`'s list and
  notification screen sit behind mouse handlers. This turns "would ungating that
  work?" into one HTTP call instead of a build-deploy-relaunch per hypothesis.
  Arguments go in as `&arg=<value>`, repeated once per declared argument in
  declaration order, and a count mismatch is refused with the signature printed.
  Passing them is safe for the reason ProcessEvent's parameter semantics give
  below: parameters are shallow-`memcpy`'d and the destroy pass skips everything
  under `ParmsSize`, so a buffer the harness owns and keeps alive across the
  call is all that is required. ⛔ Only values it can actually WRITE are
  accepted — string, int, float, bool, byte. An object, struct, array, name or
  delegate argument is refused, because the only thing the endpoint could put
  there is a zero, and a zeroed object pointer is a null deref inside script in
  the very session it exists to observe. A declared RETURN value was never a
  disqualifier: the engine writes it into our block and reads nothing from it.
  **Arguments are what make the Scaleform side reachable**, and that is the
  point of them: everything about a live movie is read through
  `GFxMoviePlayer.GetVariableString` / `GetVariableNumber` / `ActionScriptInt` /
  `ActionScriptString`, all of which take a path string — so
  `just dev-flash ActionScriptString "_global.Selection.getFocus"` is now a
  question one can ask. That is the only route to the half of the UI that is
  Flash-only: a text field's caret, the key-bindings rows, anything a screen
  wrote out and did not retain. Parameters are counted REFLECTIVELY
  (`UFunction`'s own `Children`, filtered on `CPF_Parm`) rather than from
  `ParmsSize`, which the generated header buries inside `UFunction`'s padding
  where it could not be `static_assert`ed. Class-scope functions only — a
  state-scoped one needs `GameUI::FindStateFunction`.

  All three take the same selector — `?screen=<Class>` (the screen stack,
  topmost when unnamed), `?instance=<Class>` (newest instance anywhere in
  GObjects, so it reaches objects the stack never holds — `XCom3DCursor`, a
  cheat manager, a tooltip manager), or `?ptr=0x<addr>` copied out of an earlier
  dump and re-validated against GObjects before it is walked. So the loop is:
  `/gui` to see the tree, `?ptr=` from a line of it, `/props` to read that
  widget out, `/call` to make it do something.

An unknown path returns the endpoint list. `/speech` and `/log` are
cursor-paged: poll with the returned `cursor:` value for exactly-once delivery.

Driving it: `just arm`, then `just dev-health` / `dev-status` / `dev-speech` /
`dev-log` / `dev-exec "<cmd>"` / `dev-input <keys> [kernel]` / `dev-shot` /
`dev-gui [depth] [select]` / `dev-props [select] [filter]` /
`dev-call <fn> [select]` / `dev-flash <fn> <arg> [select]`, or
`just dev '<endpoint>?<query>'` for anything else.
⛔ Scripts must call curl as `$env:SystemRoot/System32/curl.exe` — bare `curl`
on PATH resolves to MSYS2's cygwin build, whose path translation mangles URL
arguments into blanks. The justfile's `curl` variable already does this.
⛔ **Shared-harness rule** (from RTAccess): do not drive the live game while
another agent or session is driving it — give the user manual steps instead.

### `/exec` and the console — three facts established live

1. **The retail binary has a CheatManager.** `XComPlayerController.AddCheats`
   and `XComGameInfo.AllowCheats` both sit inside
   `` `if(`notdefined(FINAL_RELEASE)) `` blocks, so this was an open question.
   `obj list class=XComTacticalCheatManager` in a live mission returns
   `XComTacticalController_0.XComTacticalCheatManager_0` — so retail is NOT a
   FINAL_RELEASE build and all **1246 `exec function`s** in the SDK sources are
   reachable by name, cheat managers included. This is the one place XCOM 2 is
   *easier* than both sibling projects: no CET-Lua bridge, no `Mono.CSharp`.
2. **`ConsoleCommand`'s return value is useless here** — it comes back empty
   even for commands that clearly ran (`obj list …` returns nothing and prints
   its table; an unknown command returns nothing and logs "Command not
   recognized"). Output goes to the game's `Launch.log`. So `/exec` snapshots
   that file's size, runs the command, and returns what got appended; an
   endpoint reporting only the return value would look broken for every command
   worth running.
3. **Route: `WorldInfo.GetWorldInfo()` (static native) →
   `GetALocalPlayerController()` (on Actor) → `ConsoleCommand(cmd, bWriteToLog)`.**
   No object is cached across frames, which keeps the GC rule intact.
   The param struct is `#pragma pack(4)`: `ReturnValue` sits at **0x14**,
   directly after the bool bitfield with no padding, and natural alignment
   would put it at 0x18 and read garbage. Pinned by `static_assert`.

### ProcessEvent's parameter semantics (decompiled, moduleBase+0x143B50)

Needed before any call that passes or returns an `FString`, and true of every
such call:

- Parameters are **shallow-`memcpy`'d** into ProcessEvent's alloca'd frame —
  not per-property deep-copied. So a buffer we own is passed by pointer.
- The `ConstructorLink` destroy pass only runs `DestroyValue` on properties
  whose `Offset >= ParmsSize` — true locals, **never parameters**. So the
  engine never frees an `FString` buffer we passed in; ours merely has to
  outlive the call.
- The return value is written **straight into our block**: ProcessEvent passes
  `Parms + ReturnValueOffset` as the native's result pointer. It arrives in an
  ENGINE-allocated buffer, so reading it is free and freeing it would need
  `appFree`. `ConsoleCommand` deliberately leaks it — a few dozen bytes per
  dev-only call beats calling the wrong deallocator.

### `/gui` and `/props`: reflection, not transcription

`src/gui_dump.cpp` reads everything through the engine's own reflection spine
(`UStruct::Children` → `UProperty::Offset`/`ArrayDim`/`ElementSize`, the
property's CLASS name for its type, `UBoolProperty::BitMask`,
`UByteProperty::Enum` for enum literals), so it hand-transcribes **no offsets
of its own** — the four raw primitives it cannot get that way (`FindStackScreen`,
`FindAnyInstance`, `IsLiveObject`, `ReadChildPanels`) are dev-only exports from
`game_ui.cpp` built on offsets that are already `static_assert`-pinned.

That is what makes it authoritative where the SDK dump is not: the numbers come
from the class the running process actually has, under whatever mod set is
loaded. The Highlander's rebuild of `UIShellSoundtrackPicker` (a localized
`FString` that `SrcOrig` never declared, moving everything after 0x430 down
0x10) is exactly the case a dump cannot answer and this can. It also means the
tree prints state bits **by name** — `bIsInited`/`bIsFocused`/`bIsVisible`/
`bIsRemoved`/`bIsNavigable` — rather than decoding a flag dword at a fixed
offset, so it stays right across a rebuild that moves them.

**A screen's `ChildPanels` is not its children.** It is a flat REGISTRY of every
panel on that screen, itself included at index 0 — verified live 2026-08-19:
`UITacticalHUD` listed all 159 of its panels, among them a `UIStatList` that
really hangs four levels down inside `UITacticalHUD_ShotHUD`. Panels below the
screen do list only their own children, so a naive recursive walk *looks* right
and quietly reparents everything the recursion happens to reach last. The link
that means the same thing at every level is `UIPanel.ParentPanel` (+0x2E0), and
`/gui` filters on it; whatever the walk does not reach is then listed with its
actual parent rather than dropped.

First live run (2026-08-19, mid-mission) also confirmed every hand-transcribed
`UIPanel` offset against the running class: `bIsInited` 0x10, `bIsVisible` 0x20,
`bIsFocused` 0x40, `bIsRemoved` 0x80, `bHasTooltip` 0x100, `bIsNavigable` 0x200
in the dword at +0x284, `CachedTooltipId` +0x2AC, `Movie` +0x2B8,
`ChildPanels` +0x2E8. Dumping a screen we already have a recipe for is the
calibration check worth running after any SDK regeneration.

Steps 1-2 of *Adding a screen recipe* are what this replaces. `/tile` (the tile
reader's state as structured data) is still open.

## Live testing without eyes or hands in the game

The dev server (`src/dev_server.cpp`, loopback :8774, armed by the
`X2Access.devserver` marker) makes a session drivable end to end from outside:
`/speech` is exactly what the screen reader was handed (poll with the cursor),
`/status` is the gate + screen stack, and `tools/send-game-keys.ps1` injects
scancode-level keys (SendInput with KEYEVENTF_SCANCODE — WScript SendKeys
silently drops arrow keys for this game, and the INPUT struct must be 40 bytes
or SendInput injects nothing). Screenshot via PowerShell CopyFromScreen, then
read the image. This loop found and fixed the compass 180° rotation in one
session without the player touching the keyboard.

## Reverse engineering workflow

Two complementary tools; the ASI itself is the third and durable one.

- **Ghidra** (MCP) for static analysis — the source of the `0x143B50`
  ProcessEvent offset and the class layout. ImageBase is `0x140000000`, so a
  Ghidra VA maps to `base + (VA − 0x140000000)` at runtime.
- **Frida** for live probing — `tools/frida-run.py` (a direct-Python runner
  that deliberately does NOT use any `frida-mcp` server) plus the `frida` skill
  (`.claude/skills/frida/SKILL.md`), which carries the hard-won gotchas: a hook
  only fires while the script is loaded (arm with `--stay`), never hook the
  Windows message-pump primitives (we subclass the WNDPROC — hook that entry
  instead), a zero-hit hook usually means a cold target, and bound every
  `Memory.scanSync`. The project's UE3 offsets were originally discovered this
  way; use it for offset confirmation and recipe-testing, then fold durable
  findings into the ASI (a handler, a log line, an `event_dispatch` watch).
- **Precedence**: prefer the ASI log + Ghidra; reach for Frida only when the
  answer needs the running process. Anything needed more than once belongs in
  the ASI, not in a Frida script.

### Hooking libraries

We ship two interception points and neither needs a hooking library: the
ProcessEvent hook is a vtable pointer swap and input is a WNDPROC subclass.
If we ever need an inline hook on a non-virtual native function, use
**MinHook** (small, CMake-friendly, the game-modding standard) — not MS
Detours. We deliberately do **not** vendor a runtime assembler (AsmJit /
Xbyak): MinHook generates its own trampolines, so an assembler is only
warranted for a bespoke register-capturing stub MinHook can't express, which
we have no need for. The game ships Firaxis' own `EasyHook64.dll` but nothing
loads it — it is vestigial; do not use it.

## Dependencies

- `deps/prism` (submodule, pinned to `v0.17.3`) — speech output, linked static,
  ⛔ **MPL-2.0, not MIT** (file-level copyleft: we vendor it unmodified and the
  README says where the source is). Its deps are bundled under its own
  `third_party/`, so the build is offline; it needs the Windows SDK's `midl.exe`
  to generate the NVDA RPC client stubs, which is why no `nvdaControllerClient.dll`
  ships beside the game any more. Replaced `deps/SRAL` on 2026-08-20.
- `deps/X2ModBuildCommon` (submodule) — UnrealScript build pipeline.
- `deps/X2WOTCCommunityHighlander` (submodule, pinned to `v1.31.0-release`) —
  pure UnrealScript, no native code. Compile-time include for the companion mod
  and the source of narration event hooks; declared as a runtime requirement in
  the README, the mod's ReadMe.txt and the .XComMod description. The pin was
  chosen by DIFFING, not by trusting a tag: the workshop build installed here
  (1134256495, what the game actually loads) ships its full `Src/`, and
  `v1.31.0-release` is the tag whose tree matches it with ZERO differing files
  (the checkout had silently drifted to `v1.31.1-beta`, 35 files ahead of the
  runtime). When bumping the pin, re-run that check: workshop stable lags
  master by months, and compiling against a newer CHL than the game loads
  means layouts and hooks that don't exist at runtime.
  Note what "pure UnrealScript" means for us: the mod ships **replacement
  `XComGame.upk`, `Engine.upk` and `Core.upk`** (9.8 MB of recompiled script),
  so with it enabled every class layout comes from the Highlander's build, not
  the stock one. **Regenerate the SDK from a game running the same mod set the
  players will run**; a dump is Highlander-derived if `UIScreenStack` carries
  `OnInputSubscribers` (issue #198) and vanilla if it does not.
  Two ways that build differs, both found by regenerating on 2026-08-19:
  - Its added vars are APPENDED, so `UIScreenStack.Screens` (0x6C) and the gate
    flags (0x60) sit where the vanilla dump put them — the subscriber arrays
    land at 0x8C and after.
  - It recompiles the whole package from the SDK's `SrcOrig`, and **`SrcOrig`
    is not the retail package**: `UIShellSoundtrackPicker` there never declared
    the localized `m_chooseSoundtrack`, so under the Highlander that FString is
    gone and everything after 0x430 moves down 0x10. A field can therefore
    exist in one build and not the other — the layout asserts are what turns
    that into a build error, and they caught exactly this one.
- `deps/CodeRed-Generator` (submodule of the `alex19EP/CodeRed-Generator`
  fork, branch `xcom2`) — the SDK dumper. The fork branch carries the XCOM2
  adaptation on top of upstream: `Engine/XCOM2/` config plus the generator
  fixes documented above (`m_unsafeChars`, `UniquifyFunctionNames`, the
  `GameDefines.hpp` pack region). The generic fixes are separable from the
  XCOM2 config and are upstream-PR candidates. It produced `generated_sdk/X2SDK/`,
  which IS a build input but is **not in git** (113 MB, `.gitignore`d): if that
  folder is lost the build dies and the only recovery is a regeneration run.
  `just sdk-build` / `sdk-gen` / `sdk-adopt` / `sdk-revert` drive that; the
  generator dumps from a LIVE process, so `sdk-gen` injects it into the running
  game with Frida. Do NOT drop it into `Binaries/Win64/scripts/` as a second
  `.asi` — the ASI loader scans subdirectories, so two plugins load and two
  copies of X2Access patching 1610 vtables each kills the game on launch.

## Adding a screen recipe

The pattern, end to end (see the new-game recipes for a worked set):

0. With the game on that screen and the harness armed, `just dev-gui` prints
   its widget tree and `just dev-props "screen=<Class>"` prints every field
   with its LIVE offset, type and value. That is steps 1-2 below done from
   facts; read `SrcOrig` for the *why* (delegates, disabled navigation), not to
   guess layout.
1. Read the screen's UnrealScript in the SDK's `Development/SrcOrig/XComGame/
   Classes/` — it is the ground truth for what the widgets are, which delegate
   activation must go through, and what a sighted player can actually reach.
   Watch for controls the game reaches only by mouse (its own keyboard path may
   be a dead end) and for `DisableNavigation()`, which means focus write-back
   there would be pushing focus somewhere the game refuses to keep it.
2. Transcribe field offsets from the generated SDK's dump comments into
   `game_ui.cpp`, add a matching `static_assert` per offset to
   `sdk_layout_checks.cpp`, and expose a `Read<Screen>` view struct.
3. Add the recipe to `menu_navigator.cpp` and list it in `kRecipes`, TOPMOST
   SCREEN FIRST — probe order is what resolves a stacked screen.
4. Prefer the game's own handlers over poking state: feed a row
   `OnUnrealCommand`, click a button through `Click()`, call the screen's own
   delegate target. That keeps every side effect the game expects.

Screen lookup goes through the game's OWN screen stack
(`UIScreenStack::Screens` at 0x6C, index 0 = the top: Push inserts at 0).
`FindStackScreenRaw` matches by class name over a few dozen entries. A popped
screen leaves the stack immediately, whereas as an OBJECT it lingers with every
state bit intact until GC — which is exactly how a corpse got cached as the live
pause menu. GObjects scanning survives only as a fallback: for screens the stack
does not manage (`UIDialogueBox` is permanent and never pushed) and for the
window while the stack itself is being rebuilt. The first fallback per class is
logged, so "not stack-managed" is a fact in the log rather than an assumption.

## The tactical layer: input ownership

Three modes, and the layer must always know which one it is in:

- **World mode** (default) — arrows drive the tile cursor (`src/tile_reader.cpp`).
- **HUD mode** — the tactical recipe is FOCUSED; arrows navigate it.
- **Targeting mode** (`UITacticalHUD.m_isMenuRaised`) — the GAME owns the
  arrows, Tab and F1-F8. We narrate and read there; we claim nothing that
  steers. ⚠ **Narrowed 2026-08-20 to targeting with a TARGET LIST.** In free aim
  (`docs/free-aim-recon.md`) the arrows steer nothing: the controller aiming
  branch is dead code on mouse+keyboard, so a bare arrow lands in
  `CycleAbilitySelection` and abandons the ability the player just chose. There
  the rule inverts and the arrows are ours — the principle was "do not take a key
  the game is using to steer", and here it is not.

So the recipe must be ATTACHED BUT UNFOCUSED while the cursor keeps the arrows.
`TileReader::ComputeActive` currently stands down on
`MenuNavigator::IsAttached()` — right for menus, wrong here: the navigator needs
an explicit focused state and the tile reader has to gate on that instead.

**Tab is taken, twice**: `eTBC_NextUnit` in world mode
(`UIUtilities_Input.uc:451`) and `NextTarget` while the shot HUD is raised
(`UITacticalHUD.uc:459`). **Ctrl+Tab works only because WE claim it** — UE3
matches modifiers **LENIENTLY** (verified live 2026-08-20, and this reverses what
this file said before). A `KeyBind`'s `Control`/`Shift`/`Alt` are modifiers the
chord REQUIRES; modifiers the player merely happens to hold are NOT checked.
`bIgnoreCtrl`/`bIgnoreShift`/`bIgnoreAlt` are the opt-in SUPPRESSORS — `Input.uc`:
*"if true, the bind will not be activated if the corresponding key is held down"*
— and the entire game config uses them exactly twice: on `R`, so `Shift+R` reaches
ErrorReport without also reloading, and on `F4`, so Alt+F4 does not also fire the
game action. (Those two entries are what the old "strict" reading got backwards:
they exist to SUPPRESS a bare bind, which is only necessary because matching is
lenient.) So a bare binding fires under ANY modifier, and `Ctrl+Tab` reaches
`eTBC_NextUnit` unless the arbiter swallows it first. **Every modifier chord we
advertise on a key that carries a vanilla bare binding MUST be claimed, or it
double-fires.** Ctrl+Tab therefore enters/exits the HUD layer and plain Tab is
claimed ONLY while the layer is focused.

**The accessibility verbs outrank the vanilla bindings** (decided 2026-08-20) —
and taking a bare letter turns out to cost **nothing: no config snippet, no
rebind at all** (settled live 2026-08-20; the planned
`mods/X2AccessCompanion/Config/XComInput.ini` was deleted unused). Because
matching is lenient, every vanilla bare-key action is ALREADY reachable at
`Ctrl+key`: the arbiter claims the bare key and swallows it, the Ctrl chord is
not claimed so it passes through untouched, and the engine matches the vanilla
binding with Ctrl held. Proven end to end in a live mission — bare `Left` drove
the tile cursor with no camera movement, `Ctrl+Left` scrolled the camera: same
key, one binding, no config file in play. The object scanner needs the bare
letters, and of RT's keymap only `, . / ' ; H I` are free in XCOM tactical
(`V M N P L K O U X Y Z T G J B` all carry vanilla actions) — all of those vacate
onto Ctrl+letter for free. This LEAVES the old "we do NOT rebind the player's
config" rule intact instead of superseding it. ⛔ Still do NOT poke
`PlayerInput.Bindings` at runtime: `UIKeybindingsPCScreen.ReadBindings()` reads
the live array, so a runtime edit shows up in the Controls screen and is baked
into the player's profile permanently the moment they press Save and Exit there.
⚠ The one thing leniency does NOT hand us is a free `Shift` — **so we take it,
2026-08-21**. `LeftShift` is bound in `[XComGame.XComTacticalInput]` and
`Key_Left_Shift`'s RELEASE calls `PrevUnit()` UNCONDITIONALLY
(`XComTacticalInput.uc:2147`, state `ActiveUnit_Moving`), so every `Shift+` chord
cycled the soldier when the player let go — including the tile reader's
already-shipped `Shift+,`/`Shift+.` floor keys, for the whole time they shipped.
It never showed up in testing because the tutorial runs `InReplayPlayback`, whose
copy of the same function (`:587`) is a bare `return false` — the one place being
a replay HID a bug instead of causing one.
**The fix is a SWALLOW of the modifier itself**, not of the function: the arbiter
now accepts a claim on a modifier key (`{VK_LSHIFT, ModNone}`, side-resolved from
lParam's scan code because WM_KEYDOWN reports the generic `VK_SHIFT`), so while
our tactical layer owns input the engine never sees LeftShift go down or up and
the bound command never runs. **Our own chord matching is untouched**, and that is
the linchpin: `GetKeyState` reflects the state the OS updated when the message
was pulled off the queue, not what the window procedure then did with it. `Shift`
is therefore the reverse-cycle modifier the scanner keymap always wanted, matching
every sibling scanner in this lineage.
⛔ **`docs/object-scanner-recon.md` §4.6's Option B is WRONG and must not be
tried**: the mouse-gate trick does not apply verbatim to `Key_Left_Shift`, which
takes an `int ActionMask` and returns a bool. `MouseGate`'s thunk consumes a lone
`EX_EndFunctionParms`; here it would meet a parameter expression instead, leave
the VM's `Code` pointer mid-expression, and hand the caller an unwritten return
value. Doing it properly needs `UStruct::Script`, an offset this project has never
measured — and the swallow needs no engine offset at all.
Two consequences to keep straight. `RightShift` is deliberately NOT claimed (it
carries no binding in the tactical section, so it was never the problem). And the
swallow costs `PressedKeys` its `LeftShift` entry, which is what `Key_Tab`'s
prev-soldier branch reads — so `Shift+Tab` is claimed and re-provided by
`X2AccessObjectScanner.PrevSoldier`, reproducing
`XComTacticalInput.IsManualUnitSwitchAllowed`'s guard rule for rule. `Ctrl`
remains the safe modifier for anything that does NOT want to displace a vanilla
key: `Key_Left_Control` merely toggles the pathing pawn's waypoint mode on press
and untoggles it on release.

**Exactly two letter keys are free: H and I.** Measured across every input
section of `DefaultInput.ini` 2026-08-20 — and it must be measured across ALL of
them, not just `[XComGame.XComTacticalInput]`. Two traps. First, most letter
bindings are generic `X_Key_Press | onrelease X_Key_Release` forwarders into
`XComInputBase.InputEvent`: they look empty and are not (`Y` is Overwatch's
default ability hotkey via `Template.DefaultKeyBinding`, and `T`, `V`, `B`, `N`
are live the same way). Second, **`U` is bound in `[Engine.PlayerInput]`, not in
the tactical block** (`ClearAllWaypoints`), so a tactical-section-only grep
reports it free and is wrong. Claimed so far: `H` = shot breakdown (targeting
only), `I` = re-read the tutorial message (tutorial only). That is the whole
supply of bare letters; anything further needs a modifier — and the vanilla
action it displaces stays reachable at Ctrl+letter for free, no config required
(see the leniency paragraph above).

**Targeting mode is covered** (`src/tactical_hud.cpp` + companion
`X2AccessTacticalReader.uc`, built AND VERIFIED LIVE 2026-08-20 — a live shot read
`Выстрел, 1 of 5. Офицер "Адвента", hit 49 percent, damage 3 to 5, in half cover, 6 of 6 health`,
so ability slot, localized name, target, odds, damage band, cover and health all resolved). It
watches
only `m_isMenuRaised` — the flag `GameUI::ReadTacticalGate` already reports for
the tile cursor — and calls `PollTargeting()` on the companion CDO every 150 ms
while the tactical HUD is usable; the companion reads `m_iCurrentIndex` and the
targeting method's target index from script and answers with text ONLY when the
selection moved. **That is why Phase 1 added no offsets and no
`static_assert` entries**: change detection sits next to the data, so a
regenerated SDK or a Highlander bump cannot silently break it. Resolution
mirrors `UITacticalHUD_InfoPanel.Update` and
`UITacticalHUD_ShotWings.RefreshData` exactly — the targeting method owns the
target INDEX and the index is into the SELECTED ACTION's own target list, and
`LookupShotBreakdown` gets an EXPLICIT `UseAbility` because the empty-ability
branch is the one that `RedScreen`s. Claims, live only while raised and neither
of them steering: `Delete` re-announces the shot (it never contends with the
tile cursor's `Delete` — that stands down exactly when the shot HUD is up) and
`H` reads the per-modifier breakdown, the spoken form of the shot wings a mouse
player opens by clicking. `H` was chosen because the
`[XComGame.XComTacticalInput]` block of `DefaultInput.ini` binds no command to
it at all.

## Current state / next steps

The Graph kernel is wired and driving real screens (see **UI reading** above).
Remaining:

1. `ScriptCall` module (generic find-function-by-name + game-thread call).
   `game_ui.cpp` already hand-rolls this per call site — and now exports the
   first pieces (`FindCompanionCDO`, `CallScriptFunction`); the module is the
   generalization, not a prerequisite.
2. Continue migrating screens to native recipes. Save/load is DONE and verified
   (2026-08-21, see **Save / load** above); the strategy layer and the tactical
   HUD remain. What the save/load run did NOT cover, and the next session
   should: the SAVE screen itself (only the LOAD screen was exercised — its
   new-save row, the overwrite confirmation, and a save actually completing),
   and the text-entry reader end to end (the probe proved every read it makes,
   but the module's own announcements have not been heard yet — `just dev-log`
   should carry `TextInput: dialog up (... caret N, selection A..B)`).
3. Tile cursor: VERIFIED LIVE 2026-08-19 (autonomously: `tools/send-game-keys.ps1`
   scancode injection + dev-server speech ring + screenshots): compass steps
   (world-absolute, engine north = +Y, east = -X, calibrated against two crate
   faces — cover flags are the oracle: a tile adjacent to an impassable crate
   names its crate-facing edge), offsets exact per step, per-edge cover words,
   fog parity, reachability, floor jumps, shot-HUD/pause stand-down, lazy
   plant, state-first stepping (never read the cursor pawn back — it sits at
   body-center heights and is repositioned natively every frame).
   ROOT CAUSE FOUND LIVE 2026-08-19 — the physical cursor does not follow our
   pushes because **the game re-drives it from the mouse EVERY FRAME**:
   `XComTacticalInput`'s active-state `PostProcessCheckGameLogic` (`:2978`)
   unconditionally calls `Mouse_CheckForPathing` (`:3204`), which — while
   `Get2DMovie().IsMouseActive()` — deprojects the mouse and calls
   `CursorSetLocation(kPickPoint, false)`. So our push LANDS and is overwritten
   in the same frame. Proof: after three arrow steps the reader's own state
   advanced correctly (it spoke three different tiles, naming the right allies)
   while `XCom3DCursorMouse_0.Location` stayed bit-identical — and
   `m_bPathingNeedsUpdate` read TRUE on that same object, which only
   `CursorSetLocation` sets, so the call reached the right instance and did run.
   This RETIRES the earlier Ghidra conclusion that "mouse tracking is
   event-driven and a landed push sticks": the re-trace is not in the pawn
   physics (`physFlying`/`CalcVelocity` really are plain integration) — it is in
   SCRIPT, one layer up, which is why decompiling the pawn found nothing. The
   rest of that pass still holds: `ProcessChainedDistance` (VA 0x140b616e0)
   clamps only while `ChainedPawn != none` AND a chain distance is > 0, and the
   live cursor reads `m_fMaxChainedDistance`/`m_fMinChainedDistance` = 0 with
   `ChainedPawn` kept, exactly as `SetCursorTile` intends — that part of the fix
   landed and is not the problem. `CursorSetLocation` is
   `(newLoc, bResetSpeedCurveRampUp=true, bSnapToValidPosition=true)`, so our
   `false` is the RAMP-UP flag and snap is already defaulting on.
   The fix is `src/mouse_gate.cpp` — RESOLVED, VERIFIED LIVE 2026-08-20: it
   swallows that ONE script function by setting `FUNC_Native` on its
   `UFunction` and pointing `Func` (+0xF0) at a thunk that consumes the
   `EX_EndFunctionParms` byte and returns, restoring both fields exactly on
   release (targeting raised, recipe attached, tactical over, DLL detach).
   Fully wired: CMake, `TileReader::Call` engages it on first use per mission,
   `OnTick` tracks the active gate, `DllMain` force-restores,
   `sdk_layout_checks.cpp` pins `FunctionFlags` (0xD0 — the generated header
   mis-declares it uint64, so only that one field can be offsetof'd; Func 0xF0
   is dump-comment + live-verified). The full chain ran live: plant landed and
   STUCK at the tile center, each arrow moved the pawn exactly 96 units on the
   right axis, `LastDestinationTile` previewed OUR tile, and the two-press
   Backspace move-to committed through `ClickToPath` — unit walked to the
   spoken tile (screenshot-verified). The swallow was preferred over the blunt
   lever — `UIMovie.DeactivateMouse()` also stops `Mouse_CheckForPathing`, but
   mouse-inactive mode changes which HUD the game builds (`UITacticalHUD.OnInit`
   spawns `UITacticalHUD_MouseControls` in the mouse branch and the
   `CharInfoButton`/`EndTurnButton`/`SkyrangerButton` hotlinks in the other) and
   stops `UITacticalHUD_Ability.UpdateData` refreshing `m_strAntennaText`, which
   is where ability names come from. The swallow keeps clicks, free-aim, camera
   scroll and every other mouse feature untouched; a mouse click while gated
   commits to the pawn's preview of OUR tile — the same two-press semantics as
   Backspace. With the gate in, `SetCursorTile` passes bSnapToValidPosition
   EXPLICITLY false: every historical "push never lands" was the mouse re-push
   (snap was never the culprit), and snap ON slid a floor-up push two tiles
   sideways (`GetClosestValidCursorPosition`, measured 2026-08-20) where the
   guard needs byte-exact pawn/reader agreement. Also noted:
   `XCom3DCursorMouse.CursorSnapToFloor` is overridden to a no-op (floor keys
   must keep setting exact Z, which they do), and `XComPathingPawn.Tick` hides
   everything while `UIReplay` is on the stack (it is, permanently, in some
   sessions — though the 2026-08-19 mission's stack was
   UISpecialMissionHUD/UITurnOverlay/UITacticalHUD/UIUnitFlagManager/
   UIEventNotices, with no UIReplay). Then: object scanner (comma/period +
   PageUp/Dn reserved), vantage read (V), interact-at-cursor.
4. Tactical narration and bridge v2 are BUILT (2026-08-20) and **VERIFIED LIVE
   2026-08-21 on Gatecrasher**, together with the targeting layer, the tile
   cursor, the object scanner and the shot HUD — see **Bridge v2** and
   **Tactical combat narration** above. The run also found the ordering defect
   that fix split across the two visualization windows. Three follow-ups still
   open: three of the nine events (panic, shields, evac) went unexercised; the
   menu navigator, the tile reader and the `X2Access_Speak` handler still call
   `TTS` directly rather than `Narrator::Say`, so the scrollback ring holds
   narration only (harmless today — a recipe attaching drops the tactical queue
   via `TacticalHUD::OnTick`); and `/tactical`, the dev endpoint that would show
   mode, gate reasons and the last narration lines, is not built. `just
   dev-speech` covers most of it already, since it marks interrupting lines
   with `!`.
   Three wording defects the same run exposed, all visible by shape-counting the
   speech ring (`grep -oP 'TTS!?: "\K[^"]*' | sed 's/[0-9]\+/N/g' | sort |
   uniq -c` — the cheapest audit this project has). ✅ The noisiest is FIXED: an
   impassable tile reported reachability too, *"Impassable. Out of range. 3 south,
   1 west"*, meaningless about a tile nobody can stand on (~99×, against 23 actual
   moves); the reachability clause is now gated on the tile being enterable, and
   2026-08-21 the word itself got better — an open-air tile reads *"Ledge, 3 down"*
   rather than describing a railing exactly like a wall (see the tile-cursor
   section). Still open: the no-target readout says *"Выстрел, 1 of 5. Нет
   доступных целей.. No target"* — a doubled full stop and the same fact in two
   languages, because the localized string already ends in a period and carries
   the meaning (12×); and recentering says the unit's name three times, *"Shreya
   Bose. Ally: Shreya Bose. High cover south. Your tile. At Shreya Bose"*.
5. Events NOT yet narrated, in rough value order: reinforcement drops, the
   Chosen arriving, objective completion, will/panic thresholds, loot. Each is
   one more `AddCHEvent` line; the cost is judgement about spam, not code.
   `UnitMoveFinished` was deliberately dropped — the shipped sources trigger it
   only from `DLC_2`'s Alien Ruler code, so it is not a base-game event.
6. The dialog recipe attaches wherever a `UIDialogueBox` shows, including in
   the strategy and tactical layers — deliberate (a modal must be readable),
   but it means the layer takes over input there before those screens have
   recipes of their own. Its screen lookup caches the newest box, which a
   presentation-layer change could leave stale; covering dialogs outside the
   shell should switch that to a screen-stack walk.
7. Dev harness: `/gui` and `/props` are in (see **Dev harness** above) — built
   2026-08-19, NOT yet run against a live game. `/tutorial` joined them
   2026-08-20. `/tile` (the tile reader's state as structured data) is the
   remainder of phase 3.
8. The tutorial reader is BUILT and VERIFIED LIVE (2026-08-20, see **Tutorial
   message reader** above). First run, `Tutorial_01` frame 150, all three text
   surfaces in one burst: the world flyover "Без сознания", the anchored blade
   `IWBTest` read out of `m_arrMsgs` **slot 8** (the element the old
   `kMaxArrayElems = 8` truncated away) with `<img src='rmb_icon' …>` spoken as
   "right mouse button", and the Flash-only HUD blade recovered through the
   companion's history walk carrying `[ПРАВАЯ КНОПКА МЫШИ]` — the binding-aware
   name `%KEY:RMB%` resolves to. Still unobserved: the modal `UITutorialBox`
   (none had fired yet) and a rejection announcement. Three things it
   deliberately does not do. It announces that a submission was REFUSED (`InvalidSubmissionCnt` rose)
   but never where the tutorial wanted the player to go: that reads
   `XComTutorialMgr.NextPlayerAbilityContext`, which is the 3D marker's data and
   belongs to the object scanner — two surfaces speaking the same thing would be
   worse than one. It leaves `X2AccessTileReader.CursorMoveTo` alone, which still
   says "Moving" for a move the tutorial then vetoes: `ClickToPath` returns
   `true` before the submission is even attempted (`GameStateMoveUnit` ends in a
   bare `return true` and `LatentSubmitGameStateContext` is asynchronous), so
   "Moving" can only ever mean *submitted* — anywhere, not just in the tutorial.
   And it reads plain world messages only INSIDE the tutorial; turning that on
   generally needs the overlap with `X2AccessTacticalEvents` measured live first
   (`/tutorial` prints exactly what would be spoken, which is how to measure it).
9. Hacking screen phases 1-3 are BUILT and VERIFIED LIVE (recon and phasing in
   `docs/hacking-screen-recon.md`): phases 1-2 on the tutorial door 2026-08-20 —
   card 3 reading back "Selected" proves the D-pad write landed on the game's
   own field — and phase 3 on a REAL mission 2026-08-21, which spoke
   *"РАЗБЛОКИРОВАНО: Обезвредить детонатор, Вражеский протокол. Нажмите любую
   клавишу"* after a 4 percent gamble landed. That run also retired the open
   inference, in the direction that made the shipped code wrong:
   `m_rewardsTaken` does not index the awarded cards, it counts LIT panels, and
   the two coincide only when the player selected card 2 — which is what the run
   happened to do. See **Hacking screen** above for the rule that replaced it.
   Still unexercised: the cancel line (`Escape` out of the pad) and any hack
   whose slot 0 is `bBadThing`, where a miss has to name the penalty. ⚠ Three
   voices can land on the hack outcome — the reveal line
   (everywhere), the `UIWorldMessageMgr` flyover poll (tutorial only today) and
   `X2AccessTacticalEvents` / `X2AccessReplayNarration` — and they do not all
   fire on the same mission, so check each where it actually fires. Phase 4 is
   the remaining work: a pre-commit odds read via
   `XComGameState_Ability.GetUISummary_HackingBreakdown`, the spoken form of the
   mouse-hover-only `UITacticalHUD_HackingTooltip`, so a player can judge a hack
   before spending the action point.
10. The Avenger's ant farm is BUILT (2026-08-21, see **Avenger ant farm** above;
    recon and the phase-1 write-up in `docs/avenger-view-recon.md`, §11) and
    awaits its first live run — the ungate itself was already confirmed live
    2026-08-20 through `/call`, so what is unproven is the narration around it.
    Four things the first run has to check: that the announcement fires at all
    (`just dev-log` should carry `Avenger: ant farm active` and
    `Avenger navigation grid installed`); that a RESTING MOUSE does not steal the
    selection back (nothing re-pushes per frame the way `Mouse_CheckForPathing`
    does in tactical, but a pointer sitting over a room while we navigate is the
    case to try); that leaving and re-entering the grid re-announces (the C++
    falling-edge poll is what lets the companion see the screen go); and — the
    one thing only a screenshot settles — whether the seven fixed rooms' POSITION
    phrases match where those rooms actually are. The buildable core's
    `row R, column C` is exact (indices 3-14, row-major, from the graph itself);
    the outer ring's wording is the graph's relations put into words and is the
    part to improve. Post-tutorial, re-measure whether `Label` populates on
    keyboard focus as predicted — that is what makes rooms speak their names, and
    it cannot be observed while the whole ship is `bTutorialLocked`. Then the
    rest of the layer: inside a room (`UIFacility` + 22 subclasses, staff slots),
    the resource strip, the to-do widget and the event queue — §2 and §4 of the
    recon, all of which are retained-widget reads rather than new offsets.

When researching for this project, focus on engine/native/UE3 technicals.
Do not survey the accessibility-mod landscape — the author knows it better
than web research can capture (see memory).
