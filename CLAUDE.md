# X2Access — screen-reader accessibility for XCOM 2 WOTC

X2Access makes XCOM 2: War of the Chosen playable for blind players. It is a
two-layer mod — the first known native (injected C++) mod for this game:

1. **C++ ASI plugin** (`src/`) — injected via Ultimate ASI Loader. Owns speech
   output, input, hooking, and UI navigation.
2. **UnrealScript companion mod** (`mods/X2AccessCompanion/`) — built with the
   official SDK. Owns game-state interpretation and narration triggers.

This file holds the RULES and the durable engine facts. Per-surface
reconnaissance — measurements, live-run transcripts, what a first pass must
answer — lives in `docs/*-recon.md`; the surface table maps each screen to its
document. Keep this file rule-first: a fact earns its place by changing what the
next session does, not by recording what a past one did.

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
  the arbiter — not GetAsyncKeyState polling, not Highlander input hooks. It also
  takes passive **observers** (`RegisterObserver`): every key-down and `WM_CHAR`
  it handles, claimed or not, fanned out to callbacks that change NOTHING about
  what the game receives — for the one job a claim cannot do, echoing what the
  player types into a Flash text field, where the keys must reach the field AND
  be spoken.
  - ⛔ An observer runs INSIDE the window procedure and may do nothing but
    RECORD: no engine memory, no speech, no outbound calls — the same rule a
    claim's `activeWhen` follows. Act on the game tick.
  - ⛔ **A GLOBAL claim — no `activeWhen`, live on every screen in every layer —
    has to earn the WHOLE keyboard, not just the screens it was tested on.**
    `src/input.cpp` holds exactly one (`?`, navigation help). The retired
    F1-F4/Ctrl+Shift+S scaffolding was never free: **F2/F3/F4 are live game keys
    in the Avenger** (`UIFacilityStaffContainer.OnUnrealCommand` maps
    `FXS_KEY_F2..F4` to "activate staff slot 0..2" with no `` `ISCONTROLLERACTIVE ``
    gate), and F1-F8 are the shot HUD's target hotkeys (`UITacticalHUD.uc:432`).
    The dev harness answers what they answered (`/health`, `/speech`, `/log`).
- **Speech**: `src/tts.cpp` wraps **Prism** (same library and pin as CyberAccess,
  so a speech bug reproduces across both mods). All speech goes through it. Speak
  the focused item only; navigation help on keypress; never dump all items on a
  screen. Prism's error return from `speak` and its debounced availability poll
  both route to `RebindBackend()`, so a reader that DIED under us (NVDA exited;
  its RPC endpoint is gone and Prism never reconnects an existing instance) costs
  one line instead of the session. ⛔ A *backend instance* is not thread-safe:
  every `prism_backend_*` call stays under `tts.cpp`'s mutex and the poll-thread
  callback only raises an atomic flag.
- **Graph A11y Kernel** (`src/Graph/`): engine-neutral navigation core (Factorio
  Access → … → RTAccess → CyberAccess lineage). STL-only by contract — no UE3,
  SDK, or Windows includes; `tests/` compiles it standalone to enforce this. It
  consumes structured `GraphRender` snapshots; this is why UI reading belongs in
  C++ (feeding it through a string bridge cannot work).
  - ⛔ **A TABLE IS NOT A LIST, and the kernel has a `GraphSheet` for the
    difference** (host vocabulary lifted into a `SheetConfig` so it lives inside
    the kernel rather than beside it). A list row flattens into a sentence losing
    nothing. A table row is several facts under headings drawn ONCE at the top,
    and speech has three problems a sighted reader does not: a cell read alone is
    a value with no idea what it means; reading the whole row every time makes
    every arrow press a paragraph on a twenty-soldier roster; and focus snapping
    back to the first cell makes a COLUMN impossible to read down. The sheet
    answers all three with graph primitives and no announcer changes — the column
    header is the **Left/Right edge label**, the row name is the **Up/Down edge
    label** into a non-primary cell, and the whole-row readout is the **primary's
    own extra announcement parts**, so column 0 IS the flat list and the cells to
    its right are the same facts made addressable. Two fields carry it:
    `NodeVtable::Column` plus `KeyGraph::SlideToColumn`, which restores your
    column after a NON-directional jump (a row vanishing under you — ordinary
    arrow moves never consult it), and `NodeVtable::LandGroup`, the general
    row-vanish rule: land on the nearest survivor of the SAME group, never on a
    header or a lead line. Sparse rows are first-class — a null cell keeps its
    column NUMBER while being unlandable, which is what lets Up/Down match columns
    across ragged rows. **Reach for a sheet whenever a screen draws column
    headings.**
  - ⛔ **Never join two spoken phrases with a bare `", "`** — the GAME ends plenty
    of its own strings with `.`, `!`, `?`, `:` or `;`, and blind joining voices
    doubled full stops at the player. `X2Access::Graph::AppendPhrase` is the rule,
    and ⚠ **it tests the tail of the ACCUMULATED line, never the incoming part**:
    the separator goes BETWEEN them, so it is the left-hand side that decides
    whether one is wanted at all, and a reimplementation testing the part instead
    is a different function wearing this one's name. A line already ending in one
    of `kPhraseTerminators` (that five-character set, no more and no fewer) takes a
    plain space; everything else takes the separator — `", "` to build a phrase
    list, `". "` to build the sentence sequences the dropship briefing and the
    tutorial modal are made of. Both of `GraphAnnouncer`'s join sites route through
    it, so recipes inherit it. **Any surface that composes speech outside a recipe
    owes the same call** — the ant farm is narrate-only and had to be given it by
    hand; the companion mirrors the same set in `X2AccessHelper.AppendJoined`.
    Four kernel tests pin it.
- **UI reading**: `src/game_ui.cpp` reads `UIScreen`/`UIPanel`/`UIList` object
  trees through EXPLICIT field offsets (never the generated class layouts) and
  `src/recipes/` turns them into GraphRenders — one *recipe* per screen
  (probe → build → name), one attached at a time, listed in `kRecipes` TOPMOST
  SCREEN FIRST. The recipes sit one TU per surface family
  (`recipe_shell/strategy/avenger.cpp`) and see the navigator only through
  `src/recipes/recipe_context.h`; `src/menu_navigator.cpp` keeps the state
  machine — probe, settle, attach, differ, detach — and the `kRecipes` literal.
  ⛔ That literal stays an explicit array in that one file: cross-TU static
  initialization order is unspecified, so a self-registering roster would order
  itself by LINK order and attach to the wrong screen non-deterministically.
  This replaces per-screen UnrealScript listeners screen-by-screen;
  existing listeners stay until a recipe covers their screen. Every hand-
  transcribed offset is pinned by a `static_assert` in `src/sdk_layout_checks.cpp`,
  so a bad transcription or a regenerated SDK is a build error, not a silent wrong
  read.
- **Outbound script calls**: C++ may invoke any UnrealScript function via
  ProcessEvent. Always resolve the UFunction BY NAME at runtime and hand-roll the
  param struct (`GameUI::CallNamedFunction`; `CallStaticNative` for `static`
  functions, which take the CLASS object as `this`). The generated SDK's typed
  wrappers are unusable (see the generator rule under **Hard rules**) and only
  `GameDefines.cpp` is compiled. Native functions need no special handling —
  `X2FiraxisLiveClient.IsLoggedIn` is `FUNC_Native` and has always worked through
  plain ProcessEvent.

## Surfaces

One row per screen or subsystem the mod reads. **Check this table before building
anything that sounds like one of these** — a second reader for a covered screen is
the failure mode it exists to prevent. The full census of every `UIScreen` class in
the game — covered, TODO with priority, or out of scope — is `docs/ui-inventory.md`.

⛔ **Read a surface's own rules before editing it.** `docs/surfaces/*.md` holds them,
each earned by a live bug: which widget retains the words, which handler activation
must go through, what the game withholds from a sighted player too. The Rules column
names the file; the section heading inside it matches the Surface column. Recon
(`docs/*-recon.md`) is how a surface was measured *before* it was built; the surface
doc is what the build settled on. Rules general enough to bite the NEXT surface are
in **Hard rules** below instead.

| Surface | Native | Companion / recipe | Rules | Recon |
|---|---|---|---|---|
| Main menu, settings, new-campaign flow, pause menu | `game_ui.cpp` | `recipes/recipe_shell.cpp` | — | — |
| Save / load | `game_ui.cpp` | `X2AccessSaveReader.uc`, save/load recipe | surfaces/shell.md | `save-load-recon.md` |
| Text-entry dialog | `text_input.cpp` | — (observer, no recipe) | surfaces/shell.md | `save-load-recon.md` §7.1 |
| Tactical tile cursor | `tile_reader.cpp`, `mouse_gate.cpp` | `X2AccessTileReader.uc` | surfaces/tactical.md | — |
| Object scanner | `object_scanner.cpp` | `X2AccessObjectScanner.uc` | surfaces/tactical.md | `object-scanner-recon.md` |
| Targeting / shot HUD | `tactical_hud.cpp` | `X2AccessTacticalReader.uc` | surfaces/tactical.md | — |
| Free aim / AoE | `free_aim.cpp` | `X2AccessTacticalReader.uc` | surfaces/tactical.md | `free-aim-recon.md` |
| Selected unit | `unit_info.cpp` | `X2AccessUnitReader.uc` | surfaces/tactical.md | — |
| Objectives list | `objective_reader.cpp` | — (native reads the widget) | surfaces/tactical.md | — |
| Tutorial messages | `tutorial_reader.cpp` | `X2AccessTutorial.uc` | surfaces/tactical.md | — |
| Hacking screen | `hacking_screen.cpp` | `X2AccessHackingReader.uc` | surfaces/tactical.md | `hacking-screen-recon.md` |
| Avenger ant farm | `avenger.cpp` | `X2AccessAvengerReader.uc` | surfaces/avenger.md | `avenger-view-recon.md` §11 |
| Inside a room | `game_ui.cpp` | `X2AccessFacilityReader.uc`, `avenger room` | surfaces/avenger.md | `avenger-view-recon.md` §13 |
| Debris room | `game_ui.cpp` | `X2AccessRoomReader.uc`, `avenger debris room` | surfaces/avenger.md | `avenger-view-recon.md` §13 |
| Commodity list (13 screens) | `game_ui.cpp` | `X2AccessCommodityReader.uc`, `commodity list` | surfaces/avenger.md | `avenger-view-recon.md` §14 |
| Personnel roster | `game_ui.cpp` | `X2AccessPersonnelReader.uc`, `personnel roster` | surfaces/avenger.md | `avenger-view-recon.md` §15 |
| Choose facility | `game_ui.cpp` | `X2AccessBuildFacilityReader.uc`, `choose facility` | surfaces/avenger.md | `avenger-view-recon.md` §16 |
| Research report | `game_ui.cpp` (Flash capture) | — (no companion), `research report` | — | `research-report-recon.md` |
| Geoscape tray | `geo_scanner.cpp` | `X2AccessGeoScanner.uc`, `geoscape tray` | surfaces/strategy.md | `geoscape-recon.md` |
| Region actions (contact, radio relay) | — | `X2AccessGeoScanner.uc`, `geoscape tray` stop 2 | surfaces/strategy.md | `geoscape-recon.md` §16 |
| Skyranger briefing | `dropship.cpp` | `X2AccessDropshipReader.uc` | surfaces/strategy.md | `dropship-briefing-recon.md` |
| Modals / alerts | `flash_shadow.cpp` (UIAlert words) | `X2AccessPopupReader.uc`, `popup` | surfaces/strategy.md | `popup-inventory.md` |
| Commlink / toasts | `transient_reader.cpp` | `X2AccessTransientReader.uc` | surfaces/strategy.md | `popup-inventory.md` |
| Movie subtitles (`.bk2`) | `movie_subtitles.cpp` | — (no script surface exists) | surfaces/strategy.md | — |
| Squad select | — | `X2AccessSquadReader.uc`, `squad select` | surfaces/strategy.md | `squad-select-recon.md` |
| Armory loadout | — | `X2AccessLoadoutReader.uc`, `armory loadout` | surfaces/strategy.md | `loadout-recon.md` |
| Covert actions | `game_ui.cpp` | `X2AccessCovertActionReader.uc`, `covert actions` | surfaces/strategy.md | `covert-actions-recon.md` |
| Resistance orders | `game_ui.cpp` | `X2AccessPolicyReader.uc`, `resistance orders` | surfaces/strategy.md | — |

## UnrealScript companion owns

- **Game-state narration**: event listeners on Community Highlander hooks
  (CHEventListenerTemplate, XComLWTuple events) for turn changes, ability results, damage, mission
  state. Resolution through `XCOMHISTORY` and localized strings is script-natural; never
  reimplement it via C++ reflection.
- **Description helpers**: `X2AccessHelper.uc` static functions (cover, hazards, units on tile),
  shared by listeners and the tile reader. `X2AccessTileReader.uc` is the worked example of the
  native-callable pattern: functions invoked on the CDO with simple params, result written to its
  `NativeResult` string property, reflected out by C++ right after the synchronous ProcessEvent
  returns. CDO rule: value-typed state only, never object references (GC).
  `X2AccessSaveReader.uc` is the smallest instance and the clearest statement of the rule behind
  it: it re-runs `UISaveLoadGameListItem.UpdateData`'s own formatting over the struct that row
  RETAINED, and touches neither the history nor `ONLINEEVENTMGR.GetSaveGames` — the screen already
  asked, and asking again could answer differently. ⛔ It also uses `Chr(10)` rather than `"\n"`
  for the separator the engine wrote into `Header.Description`, for the same reason
  `X2AccessNarrator`'s separators are printable.
- **Tactical combat narration** (`X2AccessTacticalEvents.uc`): one `CHEventListenerTemplate`,
  tactical only, nine events — the path forced by the flyover/notice/banner tap not being
  buildable. **Deferral is a per-event decision, not a default**, and for one action it is SPLIT
  ACROSS BOTH visualization windows: `ELD_OnVisualizationBlockStarted` for the ACTION
  (`AbilityActivated`) and `ELD_OnVisualizationBlockCompleted` for its OUTCOME
  (`UnitTakeEffectDamage`, `UnitDied`), because a listener otherwise fires at state submission,
  seconds ahead of the animation; `ELD_OnStateSubmitted` for structural facts (`PlayerTurnBegun`,
  concealment, panic, shields, evac), which are tied to no animation and would be dropped entirely
  by a state that never builds a visualization block.
  - ⛔ **The split is what puts cause before consequence, and a shared window cannot.** Priority
    orders listeners for the SAME event only; between events the order is the GAME's trigger order,
    and that is backwards for us — `XComGameState_Unit` triggers `UnitTakeEffectDamage` (`:5874`)
    and `UnitDied` (`:6022`) while effects are applied, and `XComGameStateContext_Ability` only
    reaches `AbilityActivated` afterwards under its own *"Trigger post-activation events"* comment
    (`:191`). All three on one window speak the outcome before the attack — you learn you have been
    shot before you learn by whom — and they land in the SAME 100 ms drain, so the narrator cannot
    reorder what it cannot tell apart. Blocks run sequentially, so the split also stops two
    abilities resolving in one burst from interleaving; it is what the game's own HUD does
    (`UIUnitFlagManager:55`, `UITacticalHUD_Enemies:68`). The replay narrator needs none of this:
    walking frames in order gets the human order for free.
  - ⛔ **One grenade landing as several `DamageResults` entries is TWO bugs, and summing is only
    the first.** `DamageResults` accumulates for the whole mission, so damage is SUMMED over the
    entries whose `DamageResult.Context` matches this state — that fixes what the line SAYS.
    `XComGameState_Unit.TakeDamage` also fires `UnitTakeEffectDamage` once per entry, and deferred
    to `ELD_OnVisualizationBlockCompleted` those N callbacks are IDENTICAL in every argument (same
    unit state, array already complete), so each re-sums the same total into the same sentence and
    all N land in one 100 ms drain — that is how many times it is said, and summing does nothing
    about it. Nothing stateless can pick one of N indistinguishable calls, so
    `OnUnitTakeEffectDamage` claims `(GameState.HistoryIndex, Unit.ObjectID)` through
    `ClaimDamageNarration` before it speaks. **The key is identity, never the text** — that is what
    keeps it compatible with the narrator's no-text-dedup rule, since two ADVENT Troopers differ by
    ObjectID where their words do not. The replay narrator needs no such guard: it visits each unit
    of a frame once by construction.
  - Two more details that needed the game's own source: only abilities that actually roll get a
    result word (`X2AbilityToHitCalc_DeadEye` always hits, so the word would be noise), and it is the game's own
    localized `m_aAbilityHitResultStrings`; and the interrupt pass is skipped, because
    `AbilityActivated` re-triggers per interrupt step. Fog parity via `CanXComSquadSeeTarget` with
    ONE documented exception — a unit stops being visible the moment it dies, so a kill the squad
    CAUSED is always announced.
- **Replay narration** (`X2AccessReplayNarration.uc`): the tutorial's stand-in for the dead event
  bus. Polled from `Narrator::OnTick` (100 ms, next to the drain), it walks history frames and emits
  ability / damage / death / concealment / turn lines through `X2AccessTacticalEvents`'s shared
  builders — `BuildAbilityLine`, `BuildDamageLine`, `NarrateDeath` — so a shot reads identically
  whether an event or a frame produced it. Its gate is `bInReplay`, i.e. exactly when the listeners
  cannot fire, so the two paths can never both narrate one action.
  - ⛔ **Never read past what the player was SHOWN**: the whole tutorial script is in the history at
    mission start, so the cursor is `XComGameStateVisualizationMgr.LastStateHistoryVisualized` (the
    game's own "which frame is on screen", what `UIUnitFlag` uses), clamped again to the replay's
    `CurrentHistoryFrame`. That also syncs narration to the animation.
  - ⛔ The catch-up bound is a RUNAWAY guard, not a pacing one: normal playback advances that cursor
    in steps of **25-50 frames**, because most history frames carry no visualization of their own
    and `LastStateHistoryVisualized` jumps to the end of a block when the block completes. A
    24-frame bound read as "seek" on ordinary play and dropped 597 frames across one tutorial run —
    among them an overwatch shot the player watched and never heard. It is 512 now, and the only
    real seek is `StartReplay`'s own `JumpReplayToFrame`, which the first-poll branch already
    adopts. A skip is always logged, never silently swallowed. It resolves the GRI by hand rather
    than through `` `REPLAY `` / `` `XCOMVISUALIZATIONMGR ``, because those dereference a null GRI
    in the shell ten times a second.
- **Speech vocabulary** (`X2AccessHelper.GetInputVocabulary` / `GetResultFlyovers`): the game's OWN
  localized words for markup native has to speak. Our built-in table is English, which is wrong the
  moment the player is not. Keyed by markup token (an `<img>` `src`, or `KEY:NAME`) so a missing
  entry degrades to the English word instead of shifting a positional table. ⚠️ `%KEY:LMB%` is the
  one that MUST be there: `X2MissionNarrativeTemplate.KeyBindingMarkups` resolves only
  RMB/ENTER/TAB/Q/E/P and `%KEY:LMB%` is substituted solely by `UIUtilities_Input.InsertPCIcons`,
  which the blade path never calls — so it reaches speech raw.
- **Tutorial blade recovery** (`X2AccessTutorial.uc`): the one tutorial surface native cannot read.
  Walks the history BACKWARDS from `XComTutorialMgr.CurrentHistoryFrame` (never past it) collecting
  removed `MessageID`s, and resolves the newest surviving UNANCHORED blade through the mission's own
  `X2MissionNarrativeTemplate.GetObjectiveText`, exactly as
  `X2Action_ShowTutorialPopup.GetTutorialBoxText` does. A removal context carries only `MessageID` +
  `Remove` — `BoxType` reads as its default — so removals must be matched by id, never filtered by
  type.
- **Legacy screen listeners**: `UIScreenListener_*.uc` polling listeners with the
  `AccessibilityData` bridge. Working system; migrate to native UI reading incrementally, do not
  expand this pattern to new screens without reason.

## The bridge (script ↔ native)

- Script→native: listener writes its `AccessibilityData` string property, then fires the empty
  sentinel `X2Access_Speak()` via SetTimer; the ProcessEvent hook catches it, reflects the property
  out (`Hooks::ReadListenerProperty`), and speaks. This is the only script→native path — **DLLBind
  is conclusively unavailable in XCOM 2** (the SDK compiler rejects the keyword and the runtime
  support is compiled out of the exe).
- Native→script: outbound ProcessEvent calls.
- **Bridge v2** (`X2AccessNarrator.uc` + `src/narrator.cpp`), the queued/channelled path the v1
  sentinel cannot be: producers call `Alert()` / `Combat()`, which append to an `ENTRY_SEP`-joined
  string on the CDO, and C++ drains the whole burst in ONE `DrainNarration()` per 100 ms (the
  outbound call is the cost, not the payload). Four channels natively — `Focus`/`Aid` interrupt AND
  clear the queue (the player pressed a key; what they asked for outranks what happened), `Alert`
  queues in order but marks the BATCH as interrupting, `Combat` queues. That `Alert` rule is the
  subtle one: an alert that simply cleared the queue would eat the damage line explaining the kill
  it follows. Flush is on the frame pump, so the queue orders *within* a frame and adds no latency;
  past it the screen reader's own queue takes over, which is the honest limit (a burst can be
  cancelled wholesale by an interrupt, never trimmed — hence the 24-line cap, logged on overflow at
  both ends). A 128-line ring with channel, SOURCE and timestamp backs the future speech-scrollback
  key.
- ⛔ `Narrator::Source` is not a channel: it records whether a line was read off a surface the game
  itself displayed (`Flyover`) or derived from the game state (`State`), and a `State` line is
  dropped when an identical `Flyover` line was said in the last 5 s. **One way only, and never
  State-against-State** — inside the tutorial both paths run at once and an untargeted ability comes
  out word-for-word twice, with the flyover ~150 ms ahead; but identical text from two state events
  is load-bearing, because two ADVENT Troopers really are both called "ADVENT Trooper" and
  collapsing a double kill loses a kill.
- ⛔ **`default.X = ...` does not compile unless X is a `config` property** — the UnrealScript
  compiler says *"Can't change default values of non-config properties"*, and a static producer has
  no other route to per-class storage, so the queue vars are `config(Game)` with no ini section
  shipped (nothing is ever written back; `SaveConfig` is never called). The payload is a delimited
  STRING rather than a dynamic array because mutating one through `default.` is a pattern the
  shipped SDK sources use nowhere.

## Engine facts

- Heavily modified UE3, 64-bit only, Scaleform GFx UI. No anti-cheat; Firaxis ships EasyHook64.dll
  themselves — injection is tolerated.
- ProcessEvent: UObject vtable slot 72, address `moduleBase + 0x143B50` (found via Ghidra +
  `tools/OffsetFinder`). GNames/GObjects offsets live in the generated SDK constants;
  `src/sdk_globals.cpp` validates them with sanity heuristics before init proceeds.
- Runtime `FString` is `TArray<wchar_t>` (UTF-16) even though the generated SDK declares `char` —
  `ue3_reflection.cpp` handles the conversion.
- **The ProcessEvent hook sees only NATIVE→script dispatch.** A full HUD refresh (Tab switched
  soldier, `m_arrAbilities` 4 → 5, every panel repainted) produces not ONE UI script function — no
  `UpdateAbilitiesArray`, no `SetStats`, no `OnUnrealCommand`, no `RealizeTargets`. What arrives is
  the engine calling into script: `Tick` / `PostRender` / `HandleInputKey` / `BeginState` on
  `XComTacticalInput`, `FilterButtonInput` on `UIMovie_2D`, and the simulation surface the native
  visibility and pathing systems call — `CallAbilityMeetsCondition`,
  `GetVisibleFlankingEnemiesOfLocation`, `NumActionPointsForMoving`, `TargetIsEnemy`,
  `UpdateGameplayVisibility`, `GetEnvironmentDamagePreview`. That is UE3 as documented: a
  script→script call runs through `CallFunction` → `Function->Func` (`ProcessInternal`), never the
  virtual slot 72 we patch. **So "watch a UI function by name" is not an available technique**, and
  the obvious narration tap — `UIWorldMessageMgr.Message`, `UIEventNotices.Notify`/`NotifyBanner`,
  `UITacticalHUDMessageBanner.SetBanner` — cannot be HOOKED this way.
  - ⚠️ But `UIWorldMessageMgr` can be **POLLED**: it RETAINS every message it is showing in
    `m_arrMsgs` (0x450, stride 0x78, text at +0x10) with `m_arrActiveMessages` (0x460) indexing the
    ones on screen — so damage numbers, crit labels, "Unconscious", the `UITacticalTutorialMgr`
    cursor-help strings and the tutorial's anchored blades are readable after all. Only the
    *unanchored* HUD blade is genuinely lost to Flash. `UIEventNotices` and
    `UITacticalHUDMessageBanner` retain nothing.
  - ✅ **A NATIVE function CAN be hooked individually, and it needs no hooking library**: its
    `UFunction::Func` (+0xF0) already holds its exec thunk, and that one field is what BOTH
    dispatch paths call — `CallFunction` for a `FUNC_Native` callee, and ProcessEvent itself
    (`pcVar3 = *(code **)(param_2 + 0xf0)`, decompiled at moduleBase+0x143B50). So chaining one is
    a POINTER WRITE — save the original, write ours, call theirs — the same mechanism
    `mouse_gate.cpp` already uses to SUPPRESS, used instead to OBSERVE. `src/flash_shadow.cpp` is
    the worked example. ⛔ Two limits keep this from being the general answer: the thunk receives
    the frame with parameters STILL UNEVALUATED (`Code` points at bytecode, so reading an argument
    means replaying the VM's param pass — the trap `docs/object-scanner-recon.md` §4.6 names), so
    it is only worth it when the payload is reachable from `this` or from engine state; and the
    per-function trick does not transfer to SCRIPT functions, whose `Func` is the single shared
    `ProcessInternal` (pointing that at a thunk is the whole-call-graph hook below, not a targeted
    one).
  - Reaching script→script traffic would need a second hook on `ProcessInternal`, which would hand
    us the whole script call graph and is the one thing that would change this calculus. It is **not
    virtual** — the UObject vtable block around slot 72 matches UE3's declaration order exactly with
    no gaps (72 ProcessEvent, 73 ProcessDelegate, 74 ProcessState, 75 ProcessRemoteFunction,
    76 InitExecution, 77 GotoLabel, 78 GotoState, 79 CallFunctionByNameWithArguments), so it needs
    an INLINE hook (the first real use for MinHook, not vendored yet). Its address needs no
    signature scan: `UFunction::Func` (+0xF0) of any SCRIPT function is the dispatch target, and
    ProcessEvent calls exactly that at `0x140143E40` — read it once at init and the hook survives a
    game patch. The frame it receives carries the params ALREADY EVALUATED, so existing reflection
    reads apply unchanged; layout under pack(4) is `Node` +0x14, `Object` +0x1C, `Code` +0x24,
    `Locals` +0x2C (the `Code` offset cross-checks with what `mouse_gate.cpp` derived independently
    from `execSetLocation`). Native calls would NOT appear in such a tap (a native's `Func` is its
    exec thunk), and script calls made through ProcessEvent would appear TWICE.
- ⛔ **A `localized` var can be declared on TWO classes and populated on only ONE — and the empty one
  fails silently.** `m_sShotChanceLabel` / `m_sCritChanceLabel` are declared by both
  `UITacticalHUD_InfoPanel` (`:43`) and `UITacticalHUD_ShotHUD` (`:40`), which use them identically;
  but `XComGame.<loc>` ships the values under **`[UITacticalHUD_ShotHUD]` only**, so the InfoPanel's
  copies are `""` at runtime — as is EVERY localized string on that class (`m_sUnavailable`,
  `m_sNoTargetsHelp`, `m_sFreeAimingTitle`) while its plain defaults (`HitMCPath`) hold their
  values. Reading the empty pair costs more than a missing caption: with nothing to tell them apart,
  a sniper shot whose hit and crit both sit at 60 speaks the same number twice, meaning two
  different things. **The tell is a DOUBLE SPACE**, because `label @ value` still inserts its
  separator when the label is empty; a `grep -P '  '` over the speech ring finds it, and that is
  part of the post-run audit. When sourcing a word from a class, verify the CDO actually holds it
  (`just dev-props "cdo=<Class>"`) rather than trusting that the `.uc` declares it and the loc file
  contains the key somewhere.
- ⛔ **A list row in this game retains no text — that is the house style, not a quirk of one
  screen.** `UIPersonnel_ListItem`, `UIInventory_ListItem`, the debris room's picker rows and
  `UIFacility_RoomFunc` all take their words as parameters and push them through
  `MC.BeginFunctionOp` / `AS_UpdateData` / `MC.FunctionString`, BYPASSING `UIButton.SetText` /
  `UIListItemString.SetText` — so the retained `Text` is EMPTY on a row that is visibly showing a
  name. What a row DOES keep is the data it was built from (`UnitRef`, `ItemComodity`,
  `StaffSlotRef`, a delegate), so the companion re-derives the words the way the row did. Two
  screens break the pattern and are the ones native can simply read:
  `UIChooseFacility.PopulateList` and `UIButton.SetText`-based upgrade buttons. Check which kind a
  row is before planning the read; do not read a delegate's FName as a label — it would say
  *"OnShowObituaries"* to a Russian player.
  - ✅ **THERE IS NOW A THIRD OPTION, and for a screen whose words cannot be re-derived it is the
    right one: catch them IN FLIGHT.** `src/flash_shadow.cpp` chains `UIMovie.CopyCommandQueue`
    (native `Func` swap, see the hooking fact above) and decodes the movie-wide
    `UIMovie.CommandQueue` (0x270, `TArray<FASValue>`) at the one instant it is whole — inside
    that call, which `ProcessQueuedCommands` makes once a frame immediately BEFORE clearing the
    array and invoking into Flash. ⛔ This is why POLLING that queue never worked and the failed
    attempt is recorded in `game_ui.cpp`: by the time anything else looks, it is empty. Batches
    are keyed by `UIMCController.CacheIndex` (via `UIPanel.MC`, 0x2D0), so a surface asks for its
    own panel's words by cache index; `just dev-flashqueue` prints the lot. Three rules it was
    built with: only batches carrying a non-empty STRING are kept (the per-frame stream is mostly
    `_x`/`_y`/`_alpha` churn, and dropping it is what makes an always-on capture affordable);
    `CLEANUP_OP` evicts, which is both the recycled-cache-index guard and — because
    `UIMCController.Remove` queues it with NO `EndOp` and therefore NO null delimiter — a batch
    that MUST be special-cased or the decoder swallows the next panel's text whole; and it
    captures the PUSH, so it knows the words and nothing about selection, disabled state, or
    anything an `.as` computed after receiving them. `UIAlert` is the first consumer.
- Some screens are Flash-driven with no UIList (e.g. key bindings): neither layer can enumerate
  rows, and the same is true of anything a screen wrote into Flash and did not retain — a text
  field's caret and selection above all. The route in is `GFxMoviePlayer`'s own path-string API, ALL
  of it script-callable and therefore reachable by outbound ProcessEvent: `GetVariableString` /
  `GetVariableNumber` / `GetVariableBool` / `Invoke`, plus the `ActionScriptInt` /
  `ActionScriptString` / `ActionScriptObject` wrappers that INVOKE a method at a path (how
  `UIInputDialogue.AS_GetInputText` itself works — `Movie.ActionScriptString(MCPath $
  ".GetInputText")`). ✅ GFx's path resolution DOES reach AS2's global `Selection` accessors, prefix
  optional, and TextField properties (`.text`, `.length`, `.maxChars`, `.caretIndex`) read straight
  through `GetVariableString`. A path that does not resolve answers 0 or an empty string, so a probe
  needs a value it can tell apart from silence.
  ⛔ That is a VERIFICATION tool and must not become a reading strategy — `UIMCController.uc` labels
  those getters *"an immediate and blocking operation … very expensive, use sparingly"*, and the
  shipped game takes its own advice: two call sites in the whole of `XComGame`. Flash also holds
  only the rendered TEXT, never the semantics (which row is selected, what a number means, whether a
  button is disabled), and no field name in it can be `static_assert`ed — so the companion CDO stays
  the way words are read at runtime. It also cannot read an `XComScrollingTextField` AT ALL: its
  text is exposed through an AS2 `addProperty` getter, which `GetVariableString` cannot run, and
  the backing storage is not reachable by path either (measured live on `descriptionText`). **When
  the question is "what words did this panel get", the capture above answers it and this does
  not**; keep the path API for caret, selection and focus, which the capture cannot see.
- ⛔ **A full-screen `.bk2` movie is engine-native all the way down — no UObject, no reflection, no
  script.** `FFullScreenMovieBink` is a plain C++ object behind the global `GFullScreenMovie`
  (RVA `0x1DB8900`); it owns the Bink handle, the frame counter and the parsed
  `Movies/<name>.txt` cue track, and it draws its subtitles with native canvas calls
  (`XComEngine::RenderExtraSubtitles`). Script sees exactly two things about it, both booleans in
  disguise: `XComEngine.PlayMovie` starts one and `IsAnyMoviePlaying` says whether one is up. So
  **do not go looking for a UIScreen, a Flash path or a companion hook for anything Bink-related**
  — `src/movie_subtitles.cpp` is the only surface in this mod that dereferences a raw C++ object,
  and the rule it established is the one to copy if a second ever needs to: hard-code ONE address
  (the global), and validate it by its VTABLE before dereferencing anything through it, because no
  offset on a non-UObject can be `static_assert`ed and that check is therefore the entire pin.
- ⛔ **No gameplay event fires in the TUTORIAL — it replays history, it never submits it.**
  `X2GameRuleset.SubmitGameStateContext` (`:292`, `:401`) routes every context to
  `XComTutorialMgr.HandleSubmittedGameStateContext` instead of `SubmitGameStateContext_Internal`
  while `` `REPLAY.bInTutorial ``; on a match that handler calls `ResumeReplay()` /
  `StepReplayForward()` and returns `true`, DISCARDING the player's context, and
  `XComReplayMgr.StepReplayForward` merely advances `CurrentHistoryFrame` and calls
  `BuildVisualizationFrame` on a frame already in the history at mission start. So
  `ContextBuildGameState` never runs and `X2EventManager.TriggerEvent` is never reached — for our
  listeners or the game's own. Two consequences: **anything built on the event bus must be tested on
  a normal mission** (Gatecrasher — campaign with the tutorial checkbox off), and the tutorial
  reader's `UIWorldMessageMgr` polling is not a nicety there but the ONLY narration source.
  ✅ `X2AccessReplayNarration.uc` answers the gap: in a replay the history IS the event stream, so it
  walks frames instead of waiting for events, through the SAME builders.
- **The tactical HUD holds no text.** Unlike the shell screens, every panel of `UITacticalHUD` is a
  write-only pipe into Flash: `SetStats` resolves name, rank, class, aim and effects out of the game
  state and hands them straight to `MC.BeginFunctionOp` / `QueueString`, retaining nothing
  (`SrcOrig/.../UITacticalHUD_SoldierInfo.uc:199`) — as do the shot HUD, info panel, enemy list,
  countdown and banner. So `/gui` on tactical prints 159 panels and almost no words, and the
  read-the-widget recipe pattern does NOT transfer. What IS readable: the ability bar
  (`UITacticalHUD_AbilityContainer.m_arrAbilities` + `m_iCurrentIndex`, and per icon
  `m_strAntennaText` / `m_strCooldown` / `m_strCharge` / `IsAvailable` — antenna text only while
  `Movie.IsMouseActive()` — so the whole bar including each entry's `AvailableCode` reason needs NO
  companion call), the enemy refs (`UITacticalHUD_Enemies.m_arrTargets`),
  `UITacticalHUD.CurrentTargetID` / `m_isMenuRaised` / `ConcealmentMode`, and
  `UIObjectiveListItem.Data` (`ObjectiveDisplayInfo.DisplayLabel`, plus its timer). Everything else
  — HP, armour, action points, effects, ammo, hit chance — exists only in `XCOMHISTORY`. In tactical
  the widget tree supplies STRUCTURE and SELECTION while the companion supplies the WORDS.
- Best public documentation of Firaxis' UE3 internals: robojumper's "Too Real" series
  (https://robojumper.github.io/too-real/). Highlander event reference:
  https://x2communitycore.github.io/X2WOTCCommunityHighlander/

## Hard rules (each earned by a crash or a design dead end)

- **When a fact is on a screen, read it FROM that screen — even when the game state would also
  answer.** Parity is defined against what a sighted player sees, and the tutorial makes this bite:
  it replays history and never submits states, yet the UI on top is driven identically — the hacking
  pad animates, the panels light, the flyovers appear. A sighted player cannot tell the two missions
  apart. So a readout sourced from `XCOMHISTORY`, a submitted context or the event bus is silently
  DEAD on the mission a new blind player starts with, while one sourced from the screen's own
  properties works in both for free. Prefer, in order: the screen's own fields and functions
  (`UIHackingScreen.GetHackChance`, `SelectedHackRewardOption`, `m_rewardsTaken`), the state objects
  the screen itself cached to populate Flash (`UnitState`/`ObjectState` — same objects, same values,
  no re-lookup), and only then a fresh history query. `X2AccessHackingReader.uc` is the worked
  example: not one `XCOMHISTORY` access in the whole class. The companion still owns the words; what
  this changes is WHERE it reads them from.
- **Outbound ProcessEvent calls: game thread only.** Trigger from the Tick drain (InputArbiter
  handlers already run there), never from the WNDPROC or the init thread.
- **Nothing in `DllMain(DLL_PROCESS_DETACH)` may join a thread or touch a library.** `ShutdownAll()`
  runs under the LOADER LOCK, and at process exit the other threads are already dead, so a join
  never returns. This is why `TTS::Shutdown` deliberately does NOT call `prism_shutdown()` or
  `prism_log_shutdown()`: the first deletes the context, which joins the `BackendEnumerator` jthread,
  then calls `CoUninitialize` and a run of `__FUnloadDelayLoadedDLL2` (i.e. `FreeLibrary`); the
  second joins the logging drain. Both are textbook loader-lock deadlocks. Shutdown does the safe
  subset instead: clear the log handler so nothing can re-enter this module after `Log::Shutdown`,
  `prism_backend_stop` so a player quitting does not hear the rest of the sentence, then LEAK the
  context on purpose. Same reasoning as the dev server's bounded `Stop()`.
- **Never cache UObject pointers across frames without revalidation** — GC moves on. Re-find via
  GObjects/SCREENSTACK or validate before use. **Type-revalidation is not enough**: after a world
  change the old screen is still a well-formed instance of its class for as long as GC leaves it
  allocated, so "is it still a UIFinalShell?" happily says yes about a dead one. A cache may
  short-circuit a rescan only while the instance is still USABLE (focused, showing) — otherwise
  rescan and take the newest. Getting this wrong cost 63 seconds of dead silence after exiting a
  campaign, and it looked like a hang rather than a stale pointer.
- **A screen's index in GObjects says nothing about its age, and a removed screen keeps all its
  state bits.** UE3 reuses freed slots, so a freshly spawned screen routinely sits at a LOWER index
  than corpses of its own class. Removal sets `bIsRemoved` (0x80) and clears nothing else, so a
  corpse still reads inited/visible/focused. "Take the last instance" therefore latches onto a dead
  screen that passes every readiness test, reads as empty, and leaves the layer silently detached
  with the menu open. Screen scans must FILTER by the usability predicate (which must reject
  `bIsRemoved`), never pick by position. The durable fix is walking `UIScreenStack.Screens` (0x6C).
- **Topmost is not the same as reachable.** Starting a campaign pops the narrative screen and leaves
  `UIShellDifficulty` underneath inited, focused and top of the stack for the ~12 s the loading movie
  plays. The stack carries the game's own answer in its flag dword at 0x60: `IsInputBlocked` (0x1),
  `ScreensVisible` (0x2, so hidden when CLEAR) and `bCinematicMode` (0x8) — the same gates the game
  consults before opening its pause menu. Any of them set means nothing is navigable;
  `MenuNavigator::OnTick` detaches and refuses to attach while gated.
- **Never gate screen readiness on `bIsVisible`** (UIPanel flag dword bit 0x20). The game leaves it
  FALSE on screens it is plainly showing: a live, focused pause menu reads 0x259 and so does the
  exit-to-main-menu confirmation dialog over it — `bIsNavigable|bIsFocused|bIsInited`, no visible
  bit. Testing it meant silence at a modal the player then answered blind. Use
  `bIsInited && bIsFocused` (0x10 + 0x40) — `bIsFocused` is what the screen stack maintains for "top
  of the stack" — and for `UIDialogueBox`, whose queue is the real state, `bIsInited` plus a
  non-empty `m_arrData`. Child widgets are the exception: a BUTTON's `bIsVisible` is toggled
  deliberately and does mean hidden. (The ant farm's grid is the one documented exception on a
  screen, because the GAME gates on it there.)
- **Never store UIScreen references as class vars on UIScreenListeners** (listeners live on CDOs; a
  held reference blocks shell-world GC and crashes map transitions). Re-find via
  `` `SCREENSTACK.GetFirstInstanceOf() ``.
- **Three generator settings must stay FALSE** in
  `deps/CodeRed-Generator/Engine/XCOM2/Configuration.cpp`, each having cost real debugging:
  `m_useConstants` (with it on, all 29388 wrappers resolved their `UFunction*` from the owning
  CLASS's index — the same constant `StaticClass()` uses — so every one handed `ProcessEvent` a
  `UClass`; off, they emit `UFunction::FindFunction("Function Pkg.Class.Name")`, which also survives
  a game patch), `m_removeNativeFlags` (clearing `FUNC_Native` around the call is actively wrong
  here: a native's `Script` array is a 2-byte STUB, not a body — `XComEngine.IsAnyMoviePlaying` has
  `Script.Count` 2 against 1663 for `UIPauseMenu.OnChildClicked` — so clearing it runs the stub and
  returns a ZEROED param block indistinguishable from a real answer), and `m_removeNativeIndex`
  (same family). With those off there is no `SdkConstants.hpp` any more — it existed only to feed the
  index-based wrappers, and the GNames/GObjects offsets we use live in `GameDefines.hpp`.
  The generator is also patched to emit valid C++ rather than leaving it to a repair pass: `.` `<`
  `>` `~` and space added to `m_unsafeChars` (UnrealScript names really do carry dots —
  `OverriddenTangentVector.X`), `UniquifyFunctionNames()` applied in all three emission passes (one
  class can declare the same function once per STATE — `XComInputBase` declares `Key` ten times —
  which collapsed into ten identical declarations and ten identical `_Params` structs), and the
  `GameDefines.hpp` pack region emitted automatically. `tools/fix_generated_sdk.py` now repairs
  exactly one thing (undefined suffixed enum references) and doubles as the regression check: any
  other line in its output means a generator fix broke.
- **Anything in `GameDefines.hpp` must be included inside `#pragma pack(4)`.** Every
  `SDK_HEADERS/*.hpp` opens its own pack region; GameDefines does not, so its types (FNameEntry,
  TArray, FName, FString) silently take the including TU's pack. Mixed packs across TUs is an ODR
  violation that moved `FNameEntry::Name` from 0x14 to 0x18 and broke GNames lookup entirely. The
  generator now emits the region itself; `sdk_layout_checks.cpp` pins the result either way.
- **SEH-guard every read into engine memory** (`__try` around reflection and hook dispatch), and keep
  the ProcessEvent hot path cheap: bail on FName index before doing anything else.
- **A widget's declared `Type` does not tell you what the widget IS; the visible child does.** Every
  Second Wave row reads `UIMechaListItem.Type` = `Description` while showing a live, visible
  `UICheckbox` — the same object whose `bChecked` the campaign start reads in
  `GetSelectedSecondWaveOptionNames`. `ReadOptionRowRaw` only looked at the checkbox inside its
  `Type == Checkbox` branch, so those rows reported "unchecked" forever. ⚠ **That `Description` is
  OUR OWN companion's write, not the engine's** — the correction matters because the rule was
  filed as an engine fact and is not one. The GAME types those rows `Checkbox`
  (`UIShellDifficulty.uc:472` sets it, `UIMechaListItem.UpdateDataCheckbox:549` sets it again);
  `UIScreenListener_Difficulty.FixSecondWaveItems` retypes them so Enter stops being eaten by the
  game's own `bReadOnly` checkbox (`UIShellDifficulty.uc:482`, `UIMechaListItem.uc:186-190`). The
  rows that listener does not loop over — the difficulty screen's own bottom rows — still read
  `Type` = Checkbox, and that asymmetry was the tell all along. **The durable half is the game's
  and survives the correction:** read state from the widget POINTER, and take a row's kind from a
  widget that is VISIBLE (`SetWidgetType` hides the widgets it switches away from but leaves their
  pointers behind, so presence alone is not enough). A screen can hold both kinds at once, so "it
  works on the other rows" proves nothing — and the `Type` tag is a field any mod, ours included,
  may have rewritten under you.
- **A readout that never sounds wrong can still always be wrong — audit its DISTRIBUTION, not its
  lines.** The tile cursor called every covered edge "high cover" for two months. Every line was
  well-formed, correctly directed and fog-gated; nothing in a log read as a defect. Counting exposed
  it: **260 "High cover", zero "Low cover"** in one Gatecrasher run, on a map made of cars and
  sandbags. A field whose values are enumerable and whose observed range collapses to one value is
  broken, whatever the lines sound like. So after any live run, shape-count the speech ring —
  `grep -oP 'TTS!?: "\K[^"]*' X2Access.log | sed 's/[0-9]\+/N/g' | sort | uniq -c | sort -rn` — which
  in the same pass named the three noisiest wording defects by frequency. A cheap corollary: **when
  two of our own surfaces answer the same question, make them agree or find out why they don't.** The
  shot HUD said "in half cover" in that very run while the tile cursor said "High cover"; the two
  disagreeing was the whole diagnosis, and it is why `X2AccessHelper` exists as a shared oracle.
- ⛔ **Size the buffer from the data, never the data to the buffer — and a cap that drops the tail
  must be LOUD.** `UE3Reflect::ReadStringProperty` is the single choke point every companion payload
  passes through — tile cursor, shot HUD, object scanner, hacking pad, every recipe's wire records,
  and `X2AccessNarrator`'s 24-line drain — and it read all of them into a fixed `wchar_t[2048]`,
  clipping anything longer and saying nothing. The XCOM archives' 39 entry titles came to **2051
  characters** against the 2047 that fit, and those four characters took the LAST record with them —
  the item card. The rows all read perfectly and Tab found no card stop, so the bug presented as *the
  recipe never built that stop*. Note the failure MODE, which is why this is a hard rule: truncating
  the tail of a screen reads to a blind player as *the screen has nothing more*, indistinguishable
  from the truth and therefore never reported as a bug. The length is now read first
  (`FStringLengthRaw`), the buffer sized to it, and an implausible count refused with a log line.
  **Any fixed-size read of something whose length the GAME decides is this bug waiting to happen** —
  the remaining `wchar_t[256]`/`[512]` buffers in `game_ui.cpp` are per-WIDGET fields bounded by what
  fits on screen; the payload paths are not, and must never go back to a fixed buffer.
- **When a control seems not to respond, prove which HALF is broken before fixing either.** The
  Second Wave bug cost a full cycle spent on the write path — reading the game's source for
  `OnSecondWaveOptionClicked`, ruling out the param layout, regenerating the whole SDK, adding a
  verify-and-fall-back — because the readback said "unchecked" and the readback was the broken thing.
  The toggle had been landing correctly all along. **One screenshot settled it in seconds.** Our own
  readout is never evidence about the game's state; when the two disagree, ask the sighted user what
  the screen shows (or read the widgets live) BEFORE theorizing about the call.
- **Escaping a UIList to sibling buttons** needs `Navigator.CheckForOwnerHandling` plus deferred
  (0.01 s timer) focus — never mutate Navigator state inside its own `Next()`.
- **Sighted parity decides what gets a tab stop.** The stop list mirrors what is actually ON SCREEN;
  anything past that is a separately-labelled scan/aid surface and must never hand the player
  information a sighted player cannot get. Both halves have bitten: the WOTC initiative order looks
  like an obvious stop and is CHEAT-GATED in retail (`UITurnOverlay.ShowOrHideInitiative` shows it
  only when `CHEATMGR.bShowInitiative`), so narrating it would leak enemy turn order; and the
  standard HUD carries no turn counter — the only counters are `UISpecialMissionHUD`'s pair, and that
  screen shows them only while a `UiTimer.ShouldShow`, so an untimed mission reads `Timers = 0`. The
  game's own answer to "what does this HUD offer" is `UITacticalHUD_MouseControls` — prev/next
  soldier, end turn, rotate camera, call Skyranger, Chosen info, cancel shot, shot info, soldier info
  — and on mouse/keyboard that panel IS the command surface: `CharInfoButton` / `EndTurnButton` /
  `SkyrangerButton` are spawned only in the CONTROLLER branch of `UITacticalHUD.OnInit` and read null
  live. Build stops from that list, never from a sketch of what a HUD "should" have. Aids that
  genuinely compensate for lost vision (the tile cursor; distance and bearing on the enemy strip; a
  speech scrollback, because speech is serial where flyovers are simultaneous and persist ~5 s) stay
  — labelled as aids, not sold as parity.
- **A recipe is the RULE; game-native narrate-only is the EXCEPTION, and the test is
  destruction.** Recipes buy focus tracking, a stable reading order, and stops on panels the game
  makes unfocusable. *Narrate-only is justified only when taking the arrows would destroy
  information we cannot reproduce* — the Avenger ant farm passes (the room NAME is set inside
  `if( bIsFocused )`, so our own focus model would leave every room nameless, plus the camera swoop
  and the highlight box); a room's interior does not, and taking the arrows there bought a stop on
  a crew line no keyboard in the game can reach. Answer this before writing either kind.
- **A gate is only as good as its other half.** When a surface has a native gate AND a script gate,
  changing one is changing half a rule: `ReadAvengerGate` learned to tolerate `UIBuildFacilities`
  while the companion's own `GetGrid` did not, so native said *go ahead* ten times a second while
  script said *not on the grid* — silence on the exact screen the mode exists for. Change both, or
  neither.
- ⛔ **The screen's accept handler reads its OWN selection index and ignores the control it is
  handed — so set the selection FIRST, then send the key.** `UISaveGame.OnAccept`/`OnDelete` act on
  `m_iCurrentSelection`, `UIPersonnel.OnButtonCallback` on `m_kList.selectedIndex`,
  `UIChooseFacility.OnAccept` on the screen's own `SelectedIndex`, and `UIList.OnUnrealCommand`
  fires `OnItemClicked(self, SelectedIndex)` whatever row the caller had in mind. A click sent
  while the two disagreed would overwrite or delete the WRONG SAVE. Prefer the game's own path
  (`AcceptButton.Click()`, `SendRowCommand`) over poking state, and send it only once the selection
  has landed.
- ⛔ **An empty answer is indistinguishable from "the game is not showing this", so a wire format
  that drops empties must never carry positional data.** A field looked up in the wrong place
  answers `""`, the build skips an unlabelled node, and a whole control silently ceases to exist —
  four of squad select's five first-run defects were that one bug in different clothes. Two
  corollaries: a REPEATING field (`util`, a table's `cell=`) is emitted **including its empty
  values**, because dropping one shifts every value after it; and screen-wide labels that are class
  defaults belong on the screen's own record, not asked of each row.
- **Join after cleaning, never before** — a nickname that `CleanText` reduces to nothing leaves its
  separator behind, so composed readouts stay as separate fields until each part is clean, then go
  through `AppendPhrase`. Same family as the never-join-with-a-bare-comma rule in the kernel.
- **An icon in its unlit state gets no words.** A sighted eye passes straight over an unlit bond
  icon; speaking its title because the widget still holds one makes every card end in a phrase that
  means nothing. Read the state the icon is IN, not the string it was built with.
- `src/Graph/` stays STL-only. UE3/host concerns go in adapters outside it.

## Build & deploy

- **`just` is the interface** (`justfile` at the root; `just` alone lists every recipe). One-shot
  loops: `just go` = kill + rebuild + install the ASI + relaunch (no SDK needed); `just go-full` =
  the same including the companion. Individually: `configure`, `build`, `uscript`, `install`,
  `deploy`, `test`, `run`, `kill`, `log`, `gamelog`, `status`.
- ⛔ **`-review` is mandatory when launching.** Without it the game boots the DEVELOPER shell
  (`UIShell`) instead of the retail main menu (`UIFinalShell`), so every `UIFinalShell` recipe finds
  nothing and the mod reads as dead. It is in the justfile's `launch_flags` (with `-noRedScreens
  -noStartUpMovies`); a hand-rolled or Steam launch must add it too.
- ⛔ The MSVC runtime is pinned **`MultiThreaded` (/MT), cache-`FORCE`d**, before
  `add_subdirectory(deps/prism)`. Not cosmetic: Prism sets `CMAKE_MSVC_RUNTIME_LIBRARY` itself off
  `CMAKE_BUILD_TYPE`, which is EMPTY under the Visual Studio multi-config generator we configure
  with, so an unpinned cache silently links a `/MT` Prism into a `/MTd` X2Access in Debug.
  `BUILD_SHARED_LIBS OFF` must also be in the cache before that line — the static path is what makes
  Prism define `PRISM_STATIC`, so `<prism.h>` stops saying `dllimport`. The upside of /MT is that the
  `.asi` imports OS libraries only: no VC++ redistributable on the player's machine.
- Configure: `just configure` (or `cmake -B build -A x64`). Game/SDK paths default to
  `C:/Program Files (x86)/Steam/steamapps/common/...`; override with `-DXCOM2_GAME_PATH=` /
  `-DXCOM2_SDK_PATH=`, or `just game_path=... configure`. ⛔ Keep every path in this tree
  overridable — hardcoding the current machine's layout had to be undone across CMakeLists, the
  justfile and the dev server (Documents is not `$HOME/Documents` under OneDrive and is not
  necessarily spelled in English; ask Windows via `SHGetKnownFolderPath(FOLDERID_Documents)`).
- C++ ASI: `cmake --build build --config Release` → `build/bin/Release/X2Access.asi`. Everything +
  deploy: `--target deploy` (copies the ASI to `Binaries/Win64/scripts/` and chains `build_uscript`,
  which compiles the mod via X2ModBuildCommon and self-deploys to `XComGame/Mods/`).
- ⛔ **`build_uscript` HANGS FOREVER if the game is running** (>10 min against 15 s normally). The
  running game holds `XComGame/Mods/X2AccessCompanion/Script/X2AccessCompanion.u` open, the copy step
  gets a sharing violation, and X2ModBuildCommon has no retry and no timeout — it spins in
  `Start-Sleep` and never returns, so it looks like a slow compile rather than a lock. Diagnose by
  the SDK-side artifact: if `<SDK>/XComGame/Script/X2AccessCompanion.u` is FRESH while the copy under
  the game's `Mods/` is not, the compile succeeded and only the deploy is stuck. `just kill` first.
  The `.asi` is locked by the running game too, but that half FAILS FAST.
- Kernel tests (no game needed): `ctest --test-dir build -C Release` (`just test`).
- Loading: Ultimate ASI Loader sits in `Binaries/Win64/` under one of the several host DLL names it
  can impersonate, and **which name it uses differs per machine** — so never test for a
  particular one: a missing `version.dll` is not a broken install (`just status` checked exactly
  that one name and reported the loader MISSING on a perfectly working setup; it now probes the
  whole candidate list and PRINTS which host it found, which is also how you learn what this
  machine is using). The ASI logs to
  `X2Access.log` next to itself, rotating the previous three sessions to `X2Access.1/2/3.log` on
  every start. Timestamps carry milliseconds; screen-transition latency is unreadable without them.

## Dev harness (dev builds only)

Loopback HTTP server for observing and driving the live game — sibling of the RTAccess (8772) and
CyberAccess (8773) harnesses; this one owns **8774**. Source: `src/dev_server.cpp`.

**Double-gated.** Compile: `-DX2ACCESS_DEVSERVER=ON` (the justfile's `devserver` variable, ON for the
local tree; OFF means the TU is not built and a release `.asi` holds no socket code). Runtime: a
marker FILE, `Binaries/Win64/scripts/X2Access.devserver` (optional first line = port override) — a
file rather than an env var because neither Steam nor `just run` inherits our environment, and it
survives a relaunch. `just arm` / `just disarm` write it.

**Threading.** The accept thread owns the socket and never touches engine memory. Engine-facing
endpoints queue a job that `DevServer::Pump()` drains on the GAME thread, first statement of the
`EventDispatch::SetFramePump` lambda — the same rule as outbound ProcessEvent calls, extended to
reads. `/health`, `/speech` and `/log` answer **off-thread** from process-memory rings, which is what
makes them useful when the game thread is the broken thing: a launch that fell into a `GetQueryData -
Timed out while waiting for GPU to catch up` loop left every game-thread endpoint timing out for
minutes while `/health` kept answering with a rising `pump-age-ms` and `/log` named the cause. A
game-thread timeout therefore means "the game is not running frames", NOT "the harness is broken" —
check `pump-age-ms` first, and remember the pump rides ProcessEvent traffic, so it is only as alive
as the game is. `Stop()` runs under the loader lock, so it does a **bounded** 1 s wait on the accept
thread and detaches rather than `join()`ing.

Endpoints:

- `/health` — build stamp and `pump-age-ms`.
- `/speech?since=N` — every line `TTS::Speak` was handed, cursor-paged.
- `/log?since=N&tail=N` — the ASI log ring, cursor-paged.
- `/status` — movie + load screen, the screen stack's gate flags, the attached recipe, the
  tile-reader gate, the dropship line, and a top-first dump of `UIScreenStack.Screens` with each
  entry's flags decoded (not-inited and removed entries shown too — the point is to answer "what is
  on screen" for a screen that has no recipe yet).
- `/tutorial` — replay frame, whether the player or the replay owns controls, the on-rails rejection
  count, the modal, and every active world message with its RAW text beside what the reader would
  SPEAK. That pair is the point — markup translation is the half a screenshot cannot check.
- `/exec` — a console command (POST body, or `?cmd=`).
- `/input?keys=Down,Down,Enter` — synthetic keys. Default tier is **SendInput scancodes** after
  raising the game window, which drives the WHOLE game (UE3 reads Raw Input, so a VK-only synthetic
  press is ignored). `&mode=kernel` injects into the arbiter's own queue via
  `InputArbiter::InjectSynthetic`, driving ONLY our layer, working with the game backgrounded, and
  reporting per key whether a claim took it. `&hold=` and `&gap=` are milliseconds.
- `/flashqueue[?index=N]` — what panels PUSHED into Scaleform, captured in flight by
  `src/flash_shadow.cpp`. For the whole retains-nothing family this is the only place the words
  ever existed, so it is the endpoint that answers "what does this screen actually say" for a
  screen `/gui` prints as wordless. Bare it lists every tracked panel with its cache index; with
  `?index=` it dumps one panel's recorded calls, operands typed and in order. Answers **off the
  game thread** (it is our own map, not engine memory), so it still reports when the game thread
  is the broken thing.
- `/screenshot[?path=&foreground=0&output=N]` — PNG of the monitor the game is on, via DXGI Desktop
  Duplication (`src/screen_capture.cpp`).
- `/gui[?depth=N]` — the widget tree under a screen: one line per `UIPanel` with class, address,
  state bits and the text it carries.
- `/props[?filter=<substring>]` — every property the object's class chain declares: **offset, type,
  name and live value**, grouped by declaring class. `?cdo=<Class>` reaches a class default object
  (`GameUI::FindDefaultObject`), the only way to see the companion subsystems' model.
- `/call?fn=<Function>` — invoke a script function on the selected object. Built for the recurring
  shape of accessibility bug in this game: not a missing feature but an UNREACHABLE one — the game
  already has the function and only calls it from a branch a mouse+keyboard player never takes
  (`UIFacilityGrid.InitializeNavigationGrid` behind `ISCONTROLLERACTIVE`; `UIToDoWidget`'s list behind
  mouse handlers). Turns "would ungating that work?" into one HTTP call instead of a
  build-deploy-relaunch per hypothesis. Arguments go in as `&arg=<value>`, once per declared argument
  in declaration order; a count mismatch is refused with the signature printed. Passing them is safe
  for the reason the parameter semantics below give: parameters are shallow-`memcpy`'d and the
  destroy pass skips everything under `ParmsSize`, so a buffer the harness owns and keeps alive
  across the call is all that is required. ⛔ Only values it can actually WRITE are accepted — string,
  int, float, bool, byte. An object, struct, array, name or delegate argument is refused, because the
  only thing the endpoint could put there is a zero, and a zeroed object pointer is a null deref
  inside script in the very session it exists to observe. A declared RETURN value was never a
  disqualifier: the engine writes it into our block and reads nothing from it. **Arguments are what
  make the Scaleform side reachable** — every fact about a live movie is read through path-string
  getters, so `just dev-flash ActionScriptString "_global.Selection.getFocus"` is a question one can
  ask. Parameters are counted REFLECTIVELY (`UFunction`'s own `Children`, filtered on `CPF_Parm`)
  rather than from `ParmsSize`, which the generated header buries inside `UFunction`'s padding.
  Class-scope functions only — a state-scoped one needs `GameUI::FindStateFunction`.
- `/flash?field=<name>` — the same read, but **panel-relative**: it resolves the SELECTED panel's own
  `MCPath` and reads `<MCPath>.<field>`. That is Firaxis' own composition —
  `UIMCController.GetString(Field)` is literally
  `Movie.GetVariableString(Panel.MCPath $ "." $ Field)` — so a child field nests the way their comment
  says. Both handles come out of reflection, so this verb transcribes no offsets of its own. `&fn=`
  picks the getter and therefore the return type — `GetVariableString` (default) / `Number` / `Bool`,
  or `ActionScriptString` / `Int` / `Float` / `Void`; `GetVariable` and `Invoke` are REFUSED by name
  rather than guessed at, because their parameter blocks differ (an `FASValue` struct, and an array of
  them) and reading either through the shared block would print half a pointer as a number. Omitting
  `&field=` asks for the MCPath itself, the cheapest test that the panel resolves in the movie at all.
  **Why it exists**: this is the answer to the hard rule that *our own readout is never evidence about
  the game's state*. For every screen that pipes its text into Flash and retains none of it, "what
  does the screen actually say?" had no answer short of asking a sighted person. ⚠ Blank is
  AMBIGUOUS: an unresolved path answers exactly like an empty field, so the endpoint says so on every
  blank reading.

  All four take the same selector — `?screen=<Class>` (the screen stack, topmost when unnamed),
  `?instance=<Class>` (newest instance anywhere in GObjects, reaching objects the stack never holds —
  `XCom3DCursor`, a cheat manager, a tooltip manager; falls back to the CDO when nothing live
  matches), or `?ptr=0x<addr>` copied out of an earlier dump and re-validated against GObjects before
  it is walked. So the loop is: `/gui` to see the tree, `?ptr=` from a line of it, `/props` to read
  that widget out, `/call` to make it do something — and `/flash` to check what it actually put on
  screen.

An unknown path returns the endpoint list. `/speech` and `/log` are cursor-paged: poll with the
returned `cursor:` value for exactly-once delivery.

Driving it: `just arm`, then `just dev-health` / `dev-status` / `dev-speech` / `dev-log` /
`dev-exec "<cmd>"` / `dev-input <keys> [kernel]` / `dev-shot` / `dev-gui [depth] [select]` /
`dev-props [select] [filter]` / `dev-call <fn> [select]` / `dev-flash <fn> <arg> [select]` /
`dev-field [select] [field] [fn]` / `dev-flashqueue [index]`, or `just dev '<endpoint>?<query>'`
for anything else.
⛔ Scripts must call curl as `$env:SystemRoot/System32/curl.exe` — bare `curl` on PATH resolves to
MSYS2's cygwin build, whose path translation mangles URL arguments into blanks. The justfile's `curl`
variable already does this.
⛔ **Shared-harness rule** (from RTAccess): do not drive the live game while another agent or session
is driving it — give the user manual steps instead.

### `/exec` and the console — three facts established live

1. **The retail binary has a CheatManager.** `XComPlayerController.AddCheats` and
   `XComGameInfo.AllowCheats` both sit inside `` `if(`notdefined(FINAL_RELEASE)) `` blocks, so this
   was an open question. `obj list class=XComTacticalCheatManager` in a live mission returns
   `XComTacticalController_0.XComTacticalCheatManager_0` — so retail is NOT a FINAL_RELEASE build and
   all **1246 `exec function`s** in the SDK sources are reachable by name. This is the one place
   XCOM 2 is *easier* than both sibling projects: no CET-Lua bridge, no `Mono.CSharp`.
2. **`ConsoleCommand`'s return value is useless here** — empty even for commands that clearly ran.
   Output goes to the game's `Launch.log`, so `/exec` snapshots that file's size, runs the command,
   and returns what got appended.
3. **Route: `WorldInfo.GetWorldInfo()` (static native) → `GetALocalPlayerController()` (on Actor) →
   `ConsoleCommand(cmd, bWriteToLog)`.** No object cached across frames, so the GC rule holds. The
   param struct is `#pragma pack(4)`: `ReturnValue` sits at **0x14**, directly after the bool
   bitfield with no padding; natural alignment would put it at 0x18 and read garbage. Pinned by
   `static_assert`.

### ProcessEvent's parameter semantics (decompiled, moduleBase+0x143B50)

Needed before any call that passes or returns an `FString`:

- Parameters are **shallow-`memcpy`'d** into ProcessEvent's alloca'd frame — not per-property
  deep-copied. So a buffer we own is passed by pointer.
- The `ConstructorLink` destroy pass only runs `DestroyValue` on properties whose
  `Offset >= ParmsSize` — true locals, **never parameters**. So the engine never frees an `FString`
  we passed in; ours merely has to outlive the call.
- The return value is written **straight into our block**: ProcessEvent passes
  `Parms + ReturnValueOffset` as the native's result pointer. It arrives in an ENGINE-allocated
  buffer, so reading it is free and freeing it would need `appFree`. `ConsoleCommand` deliberately
  leaks it — a few dozen bytes per dev-only call beats calling the wrong deallocator.

### `/gui` and `/props`: reflection, not transcription

`src/gui_dump.cpp` reads everything through the engine's own reflection spine (`UStruct::Children` →
`UProperty::Offset`/`ArrayDim`/`ElementSize`, the property's CLASS name for its type,
`UBoolProperty::BitMask`, `UByteProperty::Enum` for enum literals), so it hand-transcribes **no
offsets of its own** — the four raw primitives it cannot get that way (`FindStackScreen`,
`FindAnyInstance`, `IsLiveObject`, `ReadChildPanels`) are dev-only exports from `game_ui.cpp` built on
offsets already `static_assert`-pinned. That is what makes it authoritative where the SDK dump is not:
the numbers come from the class the running process actually has, under whatever mod set is loaded.
The Highlander's rebuild of `UIShellSoundtrackPicker` is exactly the case a dump cannot answer and
this can. It also prints state bits **by name** rather than decoding a flag dword at a fixed offset.

**A screen's `ChildPanels` is not its children.** It is a flat REGISTRY of every panel on that screen,
itself included at index 0 — `UITacticalHUD` lists all 159 of its panels, among them a `UIStatList`
that really hangs four levels down inside `UITacticalHUD_ShotHUD`. Panels below the screen do list
only their own children, so a naive recursive walk *looks* right and quietly reparents everything the
recursion happens to reach last. The link that means the same thing at every level is
`UIPanel.ParentPanel` (+0x2E0), and `/gui` filters on it; whatever the walk does not reach is listed
with its actual parent rather than dropped.

Confirmed `UIPanel` offsets against the running class: `bIsInited` 0x10, `bIsVisible` 0x20,
`bIsFocused` 0x40, `bIsRemoved` 0x80, `bHasTooltip` 0x100, `bIsNavigable` 0x200 in the dword at
+0x284, `CachedTooltipId` +0x2AC, `Movie` +0x2B8, `ChildPanels` +0x2E8. Dumping a screen we already
have a recipe for is the calibration check worth running after any SDK regeneration.

## Live testing without eyes or hands in the game

The dev server makes a session drivable end to end from outside: `/speech` is exactly what the screen
reader was handed (poll with the cursor), `/status` is the gate + screen stack, and
`tools/send-game-keys.ps1` injects scancode-level keys (SendInput with KEYEVENTF_SCANCODE — WScript
SendKeys silently drops arrow keys for this game, and the INPUT struct must be 40 bytes or SendInput
injects nothing). Screenshot via `/screenshot`, then read the image. This loop found and fixed the
compass 180° rotation in one session without the player touching the keyboard.

## Reverse engineering workflow

- **Ghidra** (MCP) for static analysis — the source of the `0x143B50` ProcessEvent offset and the
  class layout. ImageBase is `0x140000000`, so a Ghidra VA maps to `base + (VA − 0x140000000)` at
  runtime.
- **Frida** for live probing — `tools/frida-run.py` (a direct-Python runner that deliberately does NOT
  use any `frida-mcp` server) plus the `frida` skill (`.claude/skills/frida/SKILL.md`), which carries
  the gotchas: a hook only fires while the script is loaded (arm with `--stay`), never hook the
  Windows message-pump primitives (we subclass the WNDPROC — hook that entry instead), a zero-hit hook
  usually means a cold target, and bound every `Memory.scanSync`.
- **Precedence**: prefer the ASI log + Ghidra; reach for Frida only when the answer needs the running
  process. Anything needed more than once belongs in the ASI, not in a Frida script.
- **Hooking libraries**: we ship two interception points and neither needs one — the ProcessEvent hook
  is a vtable pointer swap and input is a WNDPROC subclass. If an inline hook on a non-virtual native
  is ever needed, use **MinHook** (small, CMake-friendly, the game-modding standard), not MS Detours.
  We deliberately do **not** vendor a runtime assembler (AsmJit / Xbyak): MinHook generates its own
  trampolines. The game ships Firaxis' own `EasyHook64.dll` but nothing loads it — vestigial; do not
  use it.

## Dependencies

- `deps/prism` (submodule, pinned to `v0.17.3`) — speech output, linked static, ⛔ **MPL-2.0, not
  MIT** (file-level copyleft: we vendor it unmodified and the README says where the source is). Its
  deps are bundled under its own `third_party/`, so the build is offline; it needs the Windows SDK's
  `midl.exe` to generate the NVDA RPC client stubs, which is why no `nvdaControllerClient.dll` ships
  beside the game.
- `deps/X2ModBuildCommon` (submodule) — UnrealScript build pipeline.
- `deps/X2WOTCCommunityHighlander` (submodule, pinned to `v1.31.0-release`) — pure UnrealScript, no
  native code. Compile-time include for the companion and the source of narration event hooks;
  declared as a runtime requirement in the README, the mod's ReadMe.txt and the .XComMod description.
  The pin was chosen by DIFFING, not by trusting a tag: the workshop build installed here
  (1134256495, what the game actually loads) ships its full `Src/`, and `v1.31.0-release` is the tag
  whose tree matches it with ZERO differing files (the checkout had silently drifted 35 files ahead of
  the runtime). When bumping the pin, re-run that check: workshop stable lags master by months, and
  compiling against a newer CHL than the game loads means layouts and hooks that don't exist at
  runtime.
  Note what "pure UnrealScript" means for us: the mod ships **replacement `XComGame.upk`, `Engine.upk`
  and `Core.upk`** (9.8 MB of recompiled script), so with it enabled every class layout comes from the
  Highlander's build. **Regenerate the SDK from a game running the same mod set the players will
  run**; a dump is Highlander-derived if `UIScreenStack` carries `OnInputSubscribers` (issue #198) and
  vanilla if it does not. Two ways that build differs:
  - Its added vars are APPENDED, so `UIScreenStack.Screens` (0x6C) and the gate flags (0x60) sit where
    the vanilla dump put them — the subscriber arrays land at 0x8C and after.
  - It recompiles the whole package from the SDK's `SrcOrig`, and **`SrcOrig` is not the retail
    package**: `UIShellSoundtrackPicker` there never declared the localized `m_chooseSoundtrack`, so
    under the Highlander that FString is gone and everything after 0x430 moves down 0x10. A field can
    exist in one build and not the other — the layout asserts turn that into a build error, and they
    caught exactly this one.
- `deps/CodeRed-Generator` (submodule of the `alex19EP/CodeRed-Generator` fork, branch `xcom2`) — the
  SDK dumper, carrying the XCOM2 adaptation on top of upstream: `Engine/XCOM2/` config plus the
  generator fixes above. The generic fixes are separable and are upstream-PR candidates. It produced
  `generated_sdk/X2SDK/`, which IS a build input but is **not in git** (113 MB, `.gitignore`d): if
  that folder is lost the build dies and the only recovery is a regeneration run. `just sdk-build` /
  `sdk-gen` / `sdk-adopt` / `sdk-revert` drive it; the generator dumps from a LIVE process, so
  `sdk-gen` injects into the running game with Frida. ⛔ Do NOT drop it into
  `Binaries/Win64/scripts/` as a second `.asi` — the ASI loader scans subdirectories, so two plugins
  load and two copies of X2Access patching 1610 vtables each kills the game on launch.

## Adding a screen recipe

0. With the game on that screen and the harness armed, `just dev-gui` prints its widget tree and
   `just dev-props "screen=<Class>"` prints every field with its LIVE offset, type and value. That is
   steps 1-2 done from facts; read `SrcOrig` for the *why* (delegates, disabled navigation), not to
   guess layout.
1. Read the screen's UnrealScript in the SDK's `Development/SrcOrig/XComGame/Classes/` — ground truth
   for what the widgets are, which delegate activation must go through, and what a sighted player can
   actually reach. Watch for controls the game reaches only by mouse (its own keyboard path may be a
   dead end) and for `DisableNavigation()`, which means focus write-back there would be pushing focus
   somewhere the game refuses to keep it.
2. Transcribe field offsets from the generated SDK's dump comments into `game_ui.cpp`, add a matching
   `static_assert` per offset to `sdk_layout_checks.cpp`, and expose a `Read<Screen>` view struct.
3. Add the recipe to its family's TU in `src/recipes/`, declare it in `recipes.h` (bumping
   `kRecipeCount`), and list it in `kRecipes` in `menu_navigator.cpp`, TOPMOST SCREEN FIRST —
   probe order is what resolves a stacked screen, and the count turns "declared but never
   listed" (a screen silently never probed) into a build error.
4. Prefer the game's own handlers over poking state: feed a row `OnUnrealCommand`, click a button
   through `Click()`, call the screen's own delegate target. That keeps every side effect the game
   expects.

Screen lookup goes through the game's OWN screen stack (`UIScreenStack::Screens` at 0x6C, index 0 =
the top: Push inserts at 0). `FindStackScreenRaw` matches by class name over a few dozen entries. A
popped screen leaves the stack immediately, whereas as an OBJECT it lingers with every state bit
intact until GC — which is how a corpse got cached as the live pause menu. GObjects scanning survives
only as a fallback: for screens the stack does not manage (`UIDialogueBox` is permanent and never
pushed) and for the window while the stack itself is being rebuilt. The first fallback per class is
logged, so "not stack-managed" is a fact in the log rather than an assumption.

## The tactical layer: input ownership

Three modes, and the layer must always know which one it is in:

- **World mode** (default) — arrows drive the tile cursor.
- **HUD mode** — the tactical recipe is FOCUSED; arrows navigate it.
- **Targeting mode** (`UITacticalHUD.m_isMenuRaised`) — the GAME owns the arrows, Tab and F1-F8. We
  narrate and read there; we claim nothing that steers. ⚠ **Narrowed to targeting with a TARGET
  LIST.** In free aim the arrows steer nothing: the controller aiming branch is dead code on
  mouse+keyboard, so a bare arrow lands in `CycleAbilitySelection` and abandons the ability the player
  just chose. There the rule inverts and the arrows are ours — the principle was "do not take a key
  the game is using to steer", and here it is not.

So the recipe must be ATTACHED BUT UNFOCUSED while the cursor keeps the arrows. ⚠
`TileReader::ComputeActive` still stands down on `MenuNavigator::IsAttached()` — right for menus,
wrong here: the navigator needs an explicit focused state and the tile reader has to gate on that
instead.

**UE3 matches modifiers LENIENTLY.** A `KeyBind`'s `Control`/`Shift`/`Alt` are modifiers the chord
REQUIRES; modifiers the player merely happens to hold are NOT checked.
`bIgnoreCtrl`/`bIgnoreShift`/`bIgnoreAlt` are the opt-in SUPPRESSORS — `Input.uc`: *"if true, the bind
will not be activated if the corresponding key is held down"* — and the entire game config uses them
exactly twice: on `R`, so `Shift+R` reaches ErrorReport without also reloading, and on `F4`, so Alt+F4
does not also fire the game action. (Those exist to SUPPRESS a bare bind, which is only necessary
because matching is lenient.) So a bare binding fires under ANY modifier. **Every modifier chord we
advertise on a key that carries a vanilla bare binding MUST be claimed, or it double-fires.** Ctrl+Tab
therefore enters/exits the HUD layer and plain Tab is claimed ONLY while the layer is focused.

**The accessibility verbs outrank the vanilla bindings**, and taking a bare letter costs **nothing: no
config snippet, no rebind at all**. Because matching is lenient, every vanilla bare-key action is
ALREADY reachable at `Ctrl+key`: the arbiter claims the bare key and swallows it, the Ctrl chord is
not claimed so it passes through, and the engine matches the vanilla binding with Ctrl held. Proven
live — bare `Left` drove the tile cursor with no camera movement, `Ctrl+Left` scrolled the camera. Of
RT's keymap only `, . / ' ; H I` are free in XCOM tactical (`V M N P L K O U X Y Z T G J B` all carry
vanilla actions), and all of those vacate onto Ctrl+letter for free. ⛔ Still do NOT poke
`PlayerInput.Bindings` at runtime: `UIKeybindingsPCScreen.ReadBindings()` reads the live array, so a
runtime edit shows up in the Controls screen and is baked into the player's profile permanently the
moment they press Save and Exit there.

⚠ **The one thing leniency does NOT hand us is a free `Shift` — so we take it.** `LeftShift` is bound
in `[XComGame.XComTacticalInput]` and `Key_Left_Shift`'s RELEASE calls `PrevUnit()` UNCONDITIONALLY
(`XComTacticalInput.uc:2147`, state `ActiveUnit_Moving`), so every `Shift+` chord cycled the soldier
when the player let go. It never showed up in testing because the tutorial runs `InReplayPlayback`,
whose copy of the same function (`:587`) is a bare `return false` — the one place being a replay HID a
bug instead of causing one. **The fix is a SWALLOW of the modifier itself**, not of the function: the
arbiter accepts a claim on a modifier key (`{VK_LSHIFT, ModNone}`, side-resolved from lParam's scan
code because WM_KEYDOWN reports the generic `VK_SHIFT`), so while our tactical layer owns input the
engine never sees LeftShift go down or up. **Our own chord matching is untouched**, and that is the
linchpin: `GetKeyState` reflects the state the OS updated when the message was pulled off the queue,
not what the window procedure then did with it.
⛔ **`docs/object-scanner-recon.md` §4.6's Option B is WRONG and must not be tried**: the mouse-gate
trick does not apply verbatim to `Key_Left_Shift`, which takes an `int ActionMask` and returns a bool.
`MouseGate`'s thunk consumes a lone `EX_EndFunctionParms`; here it would meet a parameter expression
instead, leave the VM's `Code` pointer mid-expression, and hand the caller an unwritten return value.
Doing it properly needs `UStruct::Script`, an offset this project has never measured — and the swallow
needs no engine offset at all.
Two consequences: `RightShift` is deliberately NOT claimed (it carries no binding in the tactical
section). And the swallow costs `PressedKeys` its `LeftShift` entry, which is what `Key_Tab`'s
prev-soldier branch reads — so `Shift+Tab` is claimed and re-provided by
`X2AccessObjectScanner.PrevSoldier`, reproducing `XComTacticalInput.IsManualUnitSwitchAllowed`'s guard
rule for rule. `Ctrl` remains the safe modifier for anything that does NOT want to displace a vanilla
key: `Key_Left_Control` merely toggles the pathing pawn's waypoint mode on press and untoggles it on
release.

⛔ **Tab stays the game's, in both of its meanings.** `eTBC_NextUnit` in world mode
(`UIUtilities_Input.uc:451`) and `NextTarget` while the shot HUD is raised (`UITacticalHUD.uc:459`)
are the player's own bindings and the layer does not take them: switching soldiers is narrated by
POLLING the controller instead, which leaves the binding untouched AND catches the mouse click, the
turn start and the death-driven re-select that a key claim never could. The one exception is
`Shift+Tab`, claimed and re-provided only because the Shift swallow costs `Key_Tab`'s prev-soldier
branch the `PressedKeys` entry it reads — a debt the swallow created, not a verb the layer wanted.

**Exactly two letter keys are free: H and I.** Measured across every input section of
`DefaultInput.ini` — and it must be measured across ALL of them. Two traps. First, most letter
bindings are generic `X_Key_Press | onrelease X_Key_Release` forwarders into
`XComInputBase.InputEvent`: they look empty and are not (`Y` is Overwatch's default ability hotkey via
`Template.DefaultKeyBinding`, and `T`, `V`, `B`, `N` are live the same way). Second, **`U` is bound in
`[Engine.PlayerInput]`, not in the tactical block** (`ClearAllWaypoints`), so a tactical-section-only
grep reports it free and is wrong. Claimed: `H` = shot breakdown (targeting only), `I` = re-read the
tutorial message (tutorial only). That is the whole supply of bare letters.

**Punctuation is where the budget actually is**, and it costs even less: `;` and `'` carry NO
`Bindings=` entry in any input section, so they displace nothing and need no leniency argument (the
only two lines in `DefaultInput.ini` mentioning `Semicolon` are `WidgetInputAliases` character lists
for the editor's edit boxes, which bind no command). `;` is the selected-unit readout; `'` remains
free, earmarked for the vantage read.

## Current state / next steps

1. `ScriptCall` module (generic find-function-by-name + game-thread call). `game_ui.cpp` hand-rolls
   this per call site and exports the first pieces (`FindCompanionCDO`, `CallScriptFunction`); the
   module is the generalization, not a prerequisite.
2. ⛔ **The geoscape's `Enter` fix is UNVERIFIED and its predecessor was wrong twice**
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
3. Unexercised paths inside surfaces that HAVE run: tactical narration's panic / shields / evac
   events; the hacking screen's cancel line and any hack whose slot 0 is `bBadThing`; the ant farm's
   countdown path (`Status` is empty unless something is building) and its `Delete` re-announce; the
   SAVE screen (only LOAD was exercised — new-save row, overwrite confirmation, a save completing);
   the text-entry reader's own announcements (`just dev-log` should carry `TextInput: dialog up (...
   caret N, selection A..B)`); the Resistance Ring's covert-op status line, in BOTH branches — an
   action running (name + days left) and none assigned.
4. Two known wording defects: the no-target readout doubles a full stop and repeats the same fact in
   two languages (the localized string already ends in a period and carries the meaning); recentering
   says the unit's name three times.
5. `Narrator::Say` adoption: the menu navigator — `menu_navigator.cpp` plus the recipe TUs under
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
6. Events NOT yet narrated, in rough value order: reinforcement drops, the Chosen arriving, objective
   completion, will/panic thresholds, loot. Each is one more `AddCHEvent` line; the cost is judgement
   about spam, not code. `UnitMoveFinished` was deliberately dropped — the shipped sources trigger it
   only from `DLC_2`'s Alien Ruler code, so it is not a base-game event.
7. The dialog recipe attaches wherever a `UIDialogueBox` shows, including in the strategy and tactical
   layers — deliberate (a modal must be readable), but it means the layer takes over input there
   before those screens have recipes of their own. Its screen lookup caches the newest box, which a
   presentation-layer change could leave stale; covering dialogs outside the shell should switch that
   to a screen-stack walk.
8. Dev harness: `/tile` (the tile reader's state as structured data) and `/tactical` (mode, gate
   reasons, last narration lines) are the remaining endpoints. `just dev-speech` covers most of the
   latter already, since it marks interrupting lines with `!`.
9. Hacking phase 4: a pre-commit odds read via
   `XComGameState_Ability.GetUISummary_HackingBreakdown`, the spoken form of the mouse-hover-only
   `UITacticalHUD_HackingTooltip`, so a player can judge a hack before spending the action point.
   ⚠ Three voices can land on the hack outcome — the reveal line (everywhere), the `UIWorldMessageMgr`
   flyover poll (tutorial only today) and `X2AccessTacticalEvents`/`X2AccessReplayNarration` — and
   they do not all fire on the same mission, so check each where it actually fires.
10. Strategy-layer surfaces still unread, in value order: the staff drop-down (`UIPersonnel_DropDown`,
    a `UIPanel` and NOT part of the personnel family), `UIFacility.m_kResearchProgressBar` (the last
    of the room's Flash-only status panels — `m_kCovertOpsStatus` is done, `surfaces/avenger.md`),
    the resource strip, the to-do widget and the event queue (`avenger-view-recon.md` §2 —
    note §2.5 was CORRECTED: the event queue IS retained, `UIEventQueue_ListItem.Title` /
    `DaysValue`). Then the tactical HUD recipe itself.

When researching for this project, focus on engine/native/UE3 technicals. Do not survey the
accessibility-mod landscape — the author knows it better than web research can capture (see memory).
