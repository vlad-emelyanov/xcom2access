# Tactical surfaces

Implementation rules for the tactical layer — the map cursor, the scan ring, targeting, and what the game narrates during a mission. Each ⛔ was earned by a live bug; each ⚠ is a
known limit or an open decision. **Read the section for a surface before editing
it** — `CLAUDE.md`'s Surfaces table points here for exactly that reason.

Cross-cutting rules (buffer sizing, the screen-accept trap, the empty-field trap,
speech joining, parity) live in `CLAUDE.md`; only what is specific to these
screens is here.

## Tactical tile cursor

The RT TileExplorer port, deliberately OUTSIDE the Graph kernel (the map is not a
widget tree). C++ owns keys and speech — arrows (camera-relative), `Ctrl+,`/`Ctrl+.`
floors, `C` recenter, `Delete` re-announce, `Backspace` two-press move-to. Each
press makes one outbound call to the companion CDO (`GameUI::FindCompanionCDO` +
`CallScriptFunction`) and speaks the `NativeResult` reflected back. Script drives
the game's OWN `XCom3DCursor` (`CursorSetLocation`/`CursorSnapToFloor`/`MoveToUnit`
— pathing preview and camera follow come free; the cursor is NOT range-clamped,
only the pathing pawn's destination is) and commits through
`XComTacticalInput.ClickToPath`, guarded: commit only when the pawn's clamped
`LastDestinationTile` EQUALS the cursor tile, so a move can never land anywhere
other than the tile just spoken. Readout is fog-gated (`GetTileFOWValue`:
NeverSeen → "unexplored"; HaveSeen → static layout; Seen → full) and the
reachability clause is gated on the tile being enterable. Claims stand down while
the shot HUD is raised, while a recipe is attached, or when `UITacticalHUD` loses
focus.

- `SetCursorTile` passes `bSnapToValidPosition` EXPLICITLY false — snap ON slid a
  floor-up push two tiles sideways (`GetClosestValidCursorPosition`) where the
  commit guard needs byte-exact pawn/reader agreement. State-first stepping
  always: never read the cursor pawn back (it sits at body-center heights and is
  repositioned natively every frame). `XCom3DCursorMouse.CursorSnapToFloor` is
  overridden to a no-op, so floor keys must set exact Z themselves.
- **A LEDGE outranks "Impassable"**: a tile with no floor of its own but ground
  below is the railing a sighted player sees, and "Impassable" described it
  identically to a wall. Both halves of the test are the engine's: `IsFloorTile`
  false is why the cursor was left hanging there (`ResolveFloor` searches only one
  tile down, so stepping off a roof keeps its height — deliberately, so the edge
  can be FELT), and `GetFloorTileZ(tile, bUnlimitedSearch=true)` answers how deep,
  documenting its own miss by returning the source Z. A wall at ground level is
  not caught and must not be: buildings sit on the ground grid, so a tile inside a
  wall still HAS a floor. A ledge is open air, not merely something you cannot
  walk into.
- ⛔ **Per-edge cover: `COVER_<dir>` is PRESENCE, `COVER_<dir>Low` is a HEIGHT
  MODIFIER on it — never an alternative.** A low-cover edge carries BOTH bits, so
  `if (dir) … else if (dirLow)` reports every covered edge as high. The engine's
  own reading is `X2AbilityToHitCalc_StandardAim.GetCoverTypeForTile` (`:934`):
  *"if the cover flags in the specified direction don't have the low cover flag,
  then it is high cover"*. Test presence first, height second. Bit order is N/S/E/W
  (`0x01/0x02/0x04/0x08`, low bits four up).
- **Mouse gate** (`src/mouse_gate.cpp`): with the mouse active,
  `XComTacticalInput.Mouse_CheckForPathing` re-pushes the cached pick point through
  `CursorSetLocation` EVERY FRAME (last line of the input state's
  `PostProcessCheckGameLogic`), reverting a keyboard push one frame later. There is
  **one such function per input state** and the gate holds three:
  `Mouse_CheckForPathing` for world mode, `Mouse_CheckForFreeAim` for each of
  `UsingTargetingMethod` and `UsingSecondaryTargetingMethod` (measured at exactly
  **60.0 pushes/second**). They never contend, so each surface releases exactly what
  it took. ⚠ The free-aim pair is **state-scoped**, invisible to
  `FindScriptFunction`'s class-chain walk; `GameUI::FindStateFunction` walks the
  `UState`'s own `Children`. `TileReader::Call` engages the gate on first use per mission. While gated, the
  UFunction is flipped to `FUNC_Native`
  with `Func` (+0xF0) → a thunk that consumes `EX_EndFunctionParms` and returns —
  script→script dispatch goes through exactly those two fields — and both are
  restored when targeting raises, a recipe attaches, tactical ends, or the ASI
  unloads. `sdk_layout_checks.cpp` pins `FunctionFlags` (0xD0); the generated header
  mis-declares it uint64, so only that field can be `offsetof`'d. Clicks, free-aim
  and camera scroll are untouched; a click while gated commits to the pawn's preview
  of OUR tile. Preferred over `UIMovie.DeactivateMouse()`, which also stops the
  re-push but changes which HUD the game builds and stops
  `UITacticalHUD_Ability.UpdateData` refreshing `m_strAntennaText`.
- ⚠ `XComPathingPawn.Tick` hides everything while `UIReplay` is on the stack — and
  in some sessions it is, permanently.

## Object scanner

The RT `Scanner.cs` port, a PULL surface next to the tile cursor's push. ELEVEN
categories: `markers` (tutorial required-action marker + objective-carrying objects
— one group, because a waypoint and an objective glint are the same thing to a
player working out where to go), `units.enemies`, `units.squad`, `ground.loot`,
`ground.reinforcements`, `ground.evac`, `units.civilians`, `hazards`,
`objects.interact`, `objects.towers`, `world.connectors`. Each keeps
its OWN remembered position; the `CAT_*` ids are the one thing the two halves share
as a NUMBER rather than a name. The ring is what makes a further category cost one
`case` label instead of a key.

Keys, all gated on `TileReader::IsActive()` (shared, not recomputed — both surfaces
must stand down at the same instants): `B` next marker, `.` next enemy, `,` next
squad member, **`Shift`+those three for the PREVIOUS one**, `PageDown`/`PageUp`
within the category, `Ctrl+PageDown`/`Ctrl+PageUp` next/previous NON-EMPTY category
(announcing its name), `O` re-announce, `/` plant the tile cursor on it. `Shift+Tab`
is claimed here too — not a scanner verb, but the vanilla prev-soldier the Shift
swallow would otherwise cost.

- **NO REGISTRY.** RT needs a ticked `WorldModel` because Unity entities are mutable
  references; XCOM's `ObjectID` is a stable int over a versioned history, so the list
  is rebuilt per keypress and only the selection persists (an int key plus its tile),
  keyed by ID so a completed objective cannot silently shift what is selected. The
  ring cursor lives on the CDO, NOT in native: stepping it means knowing which
  categories are EMPTY, and only the model can answer that. `/` acts on the LAST
  ANNOUNCED entry — on what you just heard. Keys are stable per TILE (a linear index,
  not packed decimal, which would collide on a wide grid).
- ⛔ **Hazards group by TYPE, not by state object** — fire SPREADS, and every spread
  step writes another `XComGameState_WorldEffectTileData`, so per-object entries would
  grow a new "Fire" every turn for one fire. Each stored tile is re-checked against
  `TileContainsFire/Smoke/Poison/Acid` because a burned-out blob keeps its state
  object: the state is an INDEX, the world is the truth, and sharing that oracle with
  `X2AccessHelper.DescribeHazards` is why the ring and the tile cursor cannot
  disagree. Walk `StoredTileData` **by index**: its element type
  `WorldEffectTileDataContainer` is declared inside that class and referenced NOWHERE
  else in the SDK, so cross-class scoping of the name is untested ground.
- ⛔ **`ground.evac` speaks the origin CLAMPED into the zone bounds**, not
  `CenterLocation` — a rectangle's centre gives the wrong distance, the wrong bearing
  and a plant you may not be able to stand on.
- ⛔ **`objects.towers` gates on ANY XCom unit concealed**, not the player state's
  `bSquadIsConcealed`: a lone Phantom ranger ahead of a revealed squad is when a
  detection radius matters most. Its empty text names the GATE ("Towers shown only
  while concealed") rather than claiming the map has none.
- **The Lost ride the ENEMY ring**, as ONE entry (label "Lost swarm", count and nearest
  tile) however many there are. They are enemies, so the enemy key is where a player
  looks for them, and a category empty on most missions costs a ring step on all of
  them; the summary form is what makes sharing safe, since a swarm is 10-20 bodies
  with one shared name and a per-unit ring would bury the ADVENT pod standing behind
  them.
- **Enemies carry a sight clause**: the ring stays on squad visibility (what the unit
  FLAGS show), but `GetAllVisibleEnemyTargetsForUnit` +
  `GetAllSquadsightEnemiesForUnit` — the two lists `UITacticalHUD_Enemies` builds —
  decide between "via squadsight", "spotted by the squad" and silence, so the first
  unshootable target does not read as a bug in us. They reuse
  `X2AccessTacticalReader.DescribeTargetCover`/`DescribeTargetHealth` verbatim, so
  scanner and shot readout can never disagree about flanking. Ceiling on labels:
  `GetName(eNameType_Full)` gives a non-soldier its CHARACTER TEMPLATE, so every ADVENT
  trooper reads alike — parity, not a gap, because the game gives a sighted player no
  per-enemy identifier either.
- **`world.connectors` is SWEPT rather than enumerated.** The engine offers two handles
  on Z movement and only one is a list: `CollectLadderLandingTiles` is global (the same
  list `XGAIPlayer_Civilian` routes panicking civilians along, and both landings of one
  ladder are separate entries — "1 up" and "2 down" at one bearing IS the shape of a
  ladder), while ramps have only the per-tile `IsRampTile` predicate, so they come out
  of a bounded box (±12 tiles, ±1 Z). The bound is the right SCOPE, not just a budget —
  a ramp 40 tiles off is not the one you would take — which is why this category's empty
  text names the SCOPE where every other names the map. Clustering happens LAST, over
  already-distance-sorted candidates, so the survivor is the nearest end of the
  staircase. It exists because every other readout ends in "2 up" on a multi-storey map,
  and a blind player cannot act on that without knowing where the stairs are.
- **Both countdown categories gate on the game's OWN rule for drawing the mark**: loot
  uses `LootExpirationTurnsRemaining > 0 && HasAvailableLoot()`, verbatim from
  `XComGameState_LootDrop.BuildVisualizationForLootTicked`'s `SetVisible`, so an entry
  exists exactly when a waypoint is drawn; reinforcements use `Countdown > 0` (what
  `UITacticalHUD_Countdown.RefreshCounter` hides on) AND a `SpawnVisualizationType` that
  actually builds the flare — `BuildVisualizationForSpawnerCreation` is SKIPPED for `''`,
  `'TheLostSwarm'`, `'ChosenSpecialNoReveal'` and `'ChosenSpecialTopDownReveal'`. Speaking
  the reinforcement LOCATION (which the HUD counter never shows) is covered because that
  visualization leaves a persistent effect and a revealed area (`bStopEffect = false`,
  `bDestroyViewer = false`). Words come from the game's own
  `m_strReinforcementsTitle`/`m_strReinforcementsBody`, and both countdowns speak the
  NUMBER, because a number is what the sighted counter shows. Loot is labelled from
  `LastLootOwnerName`, never `GetLootingName()` — the latter re-enters the source object
  and, for an interactive-object source, walks into the same unguarded `GetVisualizer()`
  deref `InteractiveLabel` sidesteps.
- **Reachability is per-entry** (`bIsDestination`): spoken for markers and objectives,
  which you walk to; suppressed for enemies and the tutorial's attack marker, where "out
  of range" would read as "you cannot do this" about a shootable target.
- **The tutorial contributor** reads `XComTutorialMgr.NextPlayerAbilityContext`, NOT the
  six `StaticMeshComponent`s (routinely hidden at the origin while an action is still
  required), gated on `bIsTutorialAllowingControls`, and honours the parity guard:
  `AbilityTemplateName` / `SourceObject` / `PrimaryTarget` / `TargetLocations` /
  `MovementPaths[0]` only, NEVER `ResultContext`, which carries the recorded hit/miss of
  a scripted shot the player has not fired. Objective markers are deliberately NOT
  fog-gated — the game's own rule (`UIUnitFlag`/`UISimpleFlag` special-case an
  `ObjectiveInfo` component to keep the flag visible). Bearings come from
  `X2AccessTileReader.OffsetWords`, static and shared, so there is exactly ONE calibration
  of `NORTH_DX`/`EAST_DX`.

## Targeting / shot HUD

Watches only `m_isMenuRaised` — the flag `GameUI::ReadTacticalGate` already reports —
and calls `PollTargeting()` on the companion CDO every 150 ms while the HUD is usable;
the companion reads `m_iCurrentIndex` and the targeting method's target index and answers
only when the selection moved. **That is why this layer adds no offsets and no
`static_assert`s**: change detection sits next to the data, so a regenerated SDK cannot
silently break it. Resolution mirrors `UITacticalHUD_InfoPanel.Update` and
`UITacticalHUD_ShotWings.RefreshData` — the targeting method owns the target INDEX, into
the SELECTED ACTION's own target list, and `LookupShotBreakdown` gets an EXPLICIT
`UseAbility` because the empty-ability branch `RedScreen`s. Claims, live only while
raised and neither steering: `Delete` re-announces (never contends with the tile cursor's,
which stands down exactly then) and `H` reads the per-modifier breakdown — the spoken form
of the shot wings a mouse player opens by clicking.

The target line carries its **position in the cycle** (`Sectoid, 1 of 3, hit 73 percent…`)
beside the ability line's own index. Both are the same fact in the same words: the length of
a ring the player is walking with a key, not a survey of the map. The target ring is
`AvailableTargets` and the index is `X2TargetingMethod.GetTargetIndex()` — literally what
`GetTargetedObjectID()` reads and what `NextTarget()` advances — so it cannot drift from the
reticle. Spoken at `1 of 1` too: "there is nothing else to Tab to" is the same answer a
sighted player gets free from the reticles.

## Free aim / AoE preview

The third tactical input mode. An ability whose target is a POINT (`bFreeAim` set,
`AvailableTargets` EMPTY) reads its aim point off the game's own `XCom3DCursor` — grenades
and everything deriving from their method, cones, lines, the blaster launcher, the evac
zone. **Vanilla has no keyboard aiming path at all**:
`UITacticalHUD_AbilityContainer.AbilityRequiresTargetingActivation` returns false unless a
controller is active (`:155`), so the D-pad branch is dead code, and
`SetSelectionOnInputPress` (`:405`, on every PRESS) sends a bare arrow to
`CycleAbilitySelection` — which ABANDONS the throw. Claiming the arrows displaces nothing
(ability cycling stays on `1`-`9`). Aim keys are the tile cursor's: arrows step,
`Ctrl+,`/`Ctrl+.` change floor, `Delete` re-announces (ONE claim branching in script, never
two racing for a chord), and the scanner's `/` retargets the AIM point.
`Enter`/`Space`/`Escape` stay unclaimed. Detection and words live in the companion, which
reports the mode through `NativeState`, published by `TacticalHUD::OnTick`'s existing poll
— so no offsets and no extra outbound call.

⛔ Three traps:
- **Do NOT reuse `X2AccessTileReader.SetCursorTile`** — it zeroes
  `m_fMaxChainedDistance`, which under a targeting method IS the throw-range limit
  (`X2TargetingMethod_Grenade.Init:43` sets it from `GetAbilityCursorRangeMeters`; live
  960 units = 15 m = 10 tiles). Its comment calling that safe "because our keys stand down
  while the shot HUD is raised" is exactly what free aim falsifies.
- **Do NOT trust the pushed tile as the answer** — for a thrown object the blast centre is
  `GrenadePath.GetEndPosition()`, where it comes to REST, so a bounced grenade lands
  elsewhere. That resolves a frame late, which is why `PushAim` verifies the clamp
  synchronously (reading `GetCursorFeetLocation()`, the exact inverse of the offset
  `CursorSetLocation` adds, compared as a horizontal distance with half a tile of slack —
  the clamp lands on a SPHERE and owes the tile grid nothing) while the landing divergence
  is reported by the poll.
- **Blast contents come from `Ability.GatherAdditionalAbilityTargetsForLocation` asked
  about OUR tile**, not the method's `MarkedTargets` cache, which only refreshes inside its
  own `Update()`. It is the same list `ConfirmAbility` submits, fog-gated through
  `X2AccessTacticalEvents.IsNarratable` (the native gather does not care what the squad can
  see — a real leak guard). Friendly fire is read from that list, not the method's
  `bFriendlyFireAgainstUnits`, which is one frame stale; the game's own modal runs over a
  WIDER list (`CheckForFriendlyUnit` uses the visual `GetTargetedActors`) and stays the
  authority — ours is the early warning that keeps a blind player from reaching it.

⚠ The arc resolves a frame AFTER the push, so a single divergent reading is not evidence —
a pre-jump arc still points back at the shooter. A divergence must survive two consecutive
polls before it is spoken.

## Selected unit

An automatic line when the selection CHANGES (name + health/shield/armour + action points +
how many enemies THIS soldier sees) and `;` for the whole block on demand (adds CLASS,
concealment, named buffs and debuffs).

- The enemy count is the **enemy strip's** own number (`UITacticalHUD_Enemies`, *"currently
  visible aliens to the active soldier"*), from the two lists
  `XComGameState_Unit.GetUISummary_TargetableUnits` builds it from — what the unit sees, plus
  a sniper's squadsight, which `GetAllSquadsightEnemiesForUnit` already keeps disjoint. It is
  PER-SOLDIER on purpose: the squad's answer is already on the map as unit flags, while the
  strip answers the question the player pressed Tab to ask. ⛔ Filtered to living non-cosmetic
  UNITS — the strip's raw array also carries hackable objects, and counting it whole would
  call a door an enemy.
- ⛔ **Zero is SILENT on the switch line**, and explicit ("No enemies visible") only on `;`.
  Most switches on most missions happen with nothing in sight, so a spoken zero is the clause
  heard most often and wanted least, on the one line built to be short. This is not the
  empty-answer trap: that rule is about a readout whose silence cannot be told from its
  absence, and the switch line always names the soldier, so the player hears it fire either
  way. A pressed key is the case that does owe an answer, which is what `bSpeakZero` marks.

- ⛔ **Tab is NOT claimed and must not be.** The switch is found by POLLING
  `XComTacticalController` instead, which is the better instrument: selecting a soldier
  submits no game state and triggers no event (it is a VISUALIZER change), so there is
  nothing for `X2AccessTacticalEvents` to hear and nothing in the replay's frames for
  `X2AccessReplayNarration` to walk — and a poll catches every OTHER way the selection
  moves, which a Tab claim never could: a mouse click, the start of your turn, and the game
  re-selecting when the unit you were on goes down.
- Resolves through `X2AccessTileReader.ActiveUnitState()` rather than a resolver of its own,
  so the readout and the tile cursor can never name different units. Every value is parity:
  name and class from `UITacticalHUD_SoldierInfo.SetStats`, health/shield/armour from
  `UIUnitFlag.RealizeStats` via `X2AccessTacticalReader.DescribeTargetHealth` (the SAME
  oracle the shot HUD and object scanner use), action points from `RealizeMoves`' pips via
  `NumAllActionPoints()`, overwatch from `ReserveActionPoints.Find('Overwatch')`, and effect
  names from `GetUISummary_UnitEffectsByCategory` — which gets panic, disorientation,
  burning and stun spoken with no hand-written English word for any of them.
- ⛔ **ACTION POINTS ARE A FRIENDLY-ONLY READOUT.** `UIUnitFlag.RealizeMoves` draws the pips
  inside `if( m_bIsFriendly.GetValue() )` — *"Only show moves for friendly units"* is
  Firaxis' own comment — so how many actions an ALIEN has left is a fact the game shows
  nobody. That is why this reader is scoped to the SELECTED unit rather than to whatever the
  scanner or cursor points at. Do not generalize the companion's functions to an arbitrary
  unit without splitting that clause off first.
- The team test comes BEFORE the change check and deliberately does NOT record the id:
  `ControllingUnitVisualizer` can follow a unit the player does not command, and recording
  those would make every transient follow look like a change back to your own soldier. The
  forget-what-was-selected edge is the **MISSION**, which is what `GameUI::TacticalGate` grew
  `missionActive` for (a `UITacticalHUD` anywhere on the stack, where `hudOnStack` means "and
  nothing over it"). `IsUsable()` drops on every pause menu and cinematic, so resetting there
  would re-announce the same soldier each time one closed; never resetting is wrong too,
  because object IDs live in the campaign history and a soldier keeps hers across missions.

## Tutorial message reader

The tutorial is a REPLAY (`XComTutorialMgr extends XComReplayMgr`): it plays a recording
back, halts at every frame that needed input, and REJECTS anything that does not match —
with a sound and **no text at all**. Four surfaces carry its words and three are readable.
`UITutorialBox` keeps `Title`/`Desc`/`ButtonHelp` in real properties (deliberately NOT a
recipe: it holds nothing navigable — `OnInit` calls `DisableNavigation()` on container and
button — and a recipe would claim Enter/Space/Escape, which are how the box is dismissed).
The anchored blade and every other flyover come from polling `UIWorldMessageMgr`. The
bottom-right HUD blade goes straight into Flash and is recovered by the companion.

- **Markup is TRANSLATED, not stripped**: `%KEY:...%` already resolves to a bracketed,
  binding-aware key name, but mouse/arrow glyphs arrive as `<img src='rmb_icon'>` and
  `CleanText` would leave a hole exactly where the instruction is.
- Damage flyovers are skipped and plain world messages are read only inside the tutorial —
  `X2AccessTacticalEvents` narrates those from the game state, and both speaking would double
  every hit. A plain message is spoken only while `m_bVisible` (it is anchored in the WORLD,
  and `UpdateMessageLocation` clears that bit off screen) and stays PENDING rather than
  dropped so panning onto it still announces it; **blades are exempt and must be**, because
  they sit at the DESTINATION rather than the camera and read `vis=0` routinely.
- ⛔ **World messages are queued PER UNIT** (`X2Action_ConcealmentLost` queues one at each
  `Unit.GetLocation()`), so identical text from several slots is normal — a whole squad going
  concealed publishes the word once per soldier. Do NOT de-duplicate on text: two soldiers
  falling unconscious would collapse into one line. The fix is NAMING the anchor:
  `DescribeMessageAnchor` resolves `m_kFollowObject.ObjectID` through the history (gated on
  `CurrentHistoryFrame`, or the replay answers with the object as it WILL be) and reuses
  `X2AccessTacticalEvents.IsNarratable` / `UnitName`, so a flyover line and a combat line
  agree about who they name and obey one fog rule. Blades are excluded: an anchored blade is
  an instruction about a PLACE, so prefixing whoever stands there would be actively wrong.
- A MISS reaches `UIWorldMessageMgr` as an ordinary world message, not a damage one
  (`QueueWorldMessage` stamps `eHUDMessage_Damage` only when `m_iDamagePrimary > 0`), so
  flyovers matching the game's own result strings are dropped — the attributed line outranks
  the bare word.
- It announces that a submission was REFUSED (`InvalidSubmissionCnt` rose) but never where
  the tutorial wanted the player to go — that is the 3D marker's data and belongs to the
  object scanner. It leaves `X2AccessTileReader.CursorMoveTo` alone, which says "Moving" for
  a move the tutorial then vetoes: `ClickToPath` returns `true` before the submission is
  attempted, so "Moving" can only ever mean *submitted* — anywhere, not just here.

## Hacking screen

The purest case of "the widget tree holds no words" — `/gui` prints **`1 panels`**, the
screen and no children, because every string goes into Flash through `MC.FunctionOp` /
`ActionScriptVoid`. The companion supplies ALL of it and this module hand-transcribes **no
offsets at all** — correctness, not tidiness: the Highlander interleaves
`m_UsingSkulljackScreen` and `m_hackCanceled` into the bitfield at 0x450, so
`bWaitingForInput`'s **MASK** is 0x20 under the Highlander and 0x08 in vanilla, and
`static_assert`s pin offsets, not masks.

The screen's `OnUnrealCommand` handles only `FXS_DPAD_*`, so on mouse+keyboard the reward
choice is reachable **only by clicking a card on the 3D pad**. `Left`/`Right` move a cursor
across the (up to) three cards and hand the screen its own
`OnUnrealCommand(FXS_DPAD_LEFT/RIGHT, FXS_ACTION_PRESS)` — the int
`X2HackRewardTemplateManager.AcquireHackRewards` reads back off that very screen object, so
there is one source of truth; `Delete` re-announces. `Enter`/`Space`/`Escape` are NOT claimed
and every claim drops once the hack commits (the reveal ends with "press any key", and a
swallowed key would not dismiss it), reported through `NativeState`. Cancelling is detected
on the C++ falling edge — `OnCancel` closes the screen before anything can be read, and a pad
that goes away uncommitted was cancelled.

- ⛔ **The lit cards are NOT the rewards.** `AcquireHackRewards` only ever considers
  `i == 0 || i == SelectedHackRewardOption`, so an unselected middle card cannot be awarded
  whatever the meter did, while the meter lights panels by threshold and the thresholds
  DESCEND with the odds — passing card 3's lights cards 1 and 2 as well — and on a MISS
  `InitiateHack` further inflates the bar for drama
  (`Max(DisplayedScore, Rand(100 - SelectedHackChance - 1))`, whose own comment says it "does
  not affect the outcome of the hack at all"). Naming the lit prefix credits the player with
  rewards that were never in play. What `m_rewardsTaken` DOES answer is the only question
  that matters — a panel lights at `100 - GetHackChance(index)` and the inflation is bounded
  at `100 - SelectedHackChance - 2`, strictly under the selected card's own threshold, so
  `m_rewardsTaken > SelectedHackRewardOption` is an exact did-the-gamble-land test. The award
  is then rebuilt the way the game builds it: slot 0 always, UNLESS it is `bBadThing`, in
  which case it is the PENALTY and lands only when the gamble missed — and then it is named,
  so "missed" says what it cost. That also beats the banner Flash prints, which reads
  `HackRewards[RewardIndex].bBadThing` and so reports success even on a miss whenever slot 0
  is ordinary.
- ⛔ It must NOT gate on `StackState::Interactive()`: the stack reports **cinematic=1** while
  this screen is up, legitimately — `HideUIForCinematics` skips every screen with
  `bShowDuringCinematic`, which this screen sets. Detection reuses `ReadTacticalGate()`, whose
  `blocker` reads `UIHackingScreen` at exactly the instant the tile cursor stands down.
- Only the first `Min(len, 3)` rewards are spoken: the movie has three panels and
  `RefreshPreviewState` hard-loops `Index < 3`, so on the tutorial door (whose array is FIVE — object rewards plus
  `BattleData.TacticalHackRewards`) the tail is unreachable for everyone. Chances come from the screen's own `GetHackChance`
  (never re-derived — sitreps, Gremlins and Skullmining all move the inputs) and are
  truncated, matching Flash: 6.42 and 2.75 print as 6% and 2%.
