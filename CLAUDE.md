# X2Access — screen-reader accessibility for XCOM 2 WOTC

X2Access makes XCOM 2: War of the Chosen playable for blind players. It is a
two-layer mod — the first known native (injected C++) mod for this game:

1. **C++ ASI plugin** (`src/`) — injected via Ultimate ASI Loader. Owns speech
   output, input, hooking, and UI navigation.
2. **UnrealScript companion mod** (`mods/X2AccessCompanion/`) — built with the
   official SDK. Owns game-state interpretation and narration triggers.

⛔ **This file is the RULEBOOK. It holds nothing that changes when a surface, an endpoint or a task
list changes** — a rule earns its place here by forbidding a mistake, never by recording what a past
session did, promising what a future one will, or describing something a reader can look up. **A
rule states itself in a line or two and points at the doc that holds its evidence**; the incident
that earned it lives there, not here. Everything that moves lives in `docs/`:

| Where | What |
|---|---|
| `docs/next-steps.md` | what is NOT done: first passes owed, unexercised paths, known defects, queued work |
| `docs/surfaces/README.md` | the registry of every surface we read — native TU, companion class, rules doc, recon |
| `docs/surfaces/*.md` | per-surface rules, each earned by a live bug |
| `docs/*-recon.md` | how a surface was measured BEFORE it was built |
| `docs/engine-notes.md` | the measured record of Firaxis' UE3 — dispatch, offsets, field maps, screen lifetime, what retains text |
| `docs/input-map.md` | what the game binds, what we claim, what is left |
| `docs/dev-harness.md` | the loopback server's endpoints and how to drive a live session without eyes or hands |
| `docs/build.md` | configure, build, deploy, the generator's patches, and what each dependency is pinned to |
| `docs/graph-kernel.md` | the a11y kernel's sheet design and `AppendPhrase` |
| `docs/localization.md` | the mod's own words on the vanilla loc system — the vocabulary class, tags for word order, the plural rule, what a mod loc file cannot do |
| `docs/ui-inventory.md` | the census of every `UIScreen` class in the game |

## Architecture: division of responsibility

Rule of thumb: **if the information is on screen (widgets, lists, focus), C++
reads it; if it is about the game simulation (units, cover, HP, turns,
objectives), UnrealScript reads it and pushes; input and speech are always C++.**

### C++ ASI owns

- **Event bus**: ProcessEvent hook (`src/process_event_hook.cpp`) patched into every UObject vtable
  at slot 72; `src/event_dispatch.cpp` routes watched function names to handlers.
- **Input**: `src/input_arbiter.cpp` subclasses the game window WNDPROC and swallows claimed key
  chords before the engine sees them. Handlers are queued and drained on the game tick. All new
  hotkeys and navigation input go through the arbiter — not GetAsyncKeyState polling, not
  Highlander input hooks. It also takes passive **observers** (`RegisterObserver`): every key-down
  and `WM_CHAR` it handles, claimed or not, fanned out to callbacks that change NOTHING about what
  the game receives — for the one job a claim cannot do, echoing what the player types into a
  Flash text field, where the keys must reach the field AND be spoken.
  - ⛔ An observer runs INSIDE the window procedure and may do nothing but RECORD: no engine
    memory, no speech, no outbound calls — the same rule a claim's `activeWhen` follows. Act on the
    game tick.
  - A claim may be **`passThrough`**: the handler runs AND the game receives the key, presses
    only, nothing latched. That is the form for a VANILLA key we add words to (Left Alt's target
    preview). ⛔ **Never swallow a modifier the game's own `bIgnore*` suppressors depend on** —
    `F4` is bound `bIgnoreAlt` so Alt+F4 reaches `ALT_F4_QUIT`, and an engine that never saw Alt go
    down fires the bare F4 binding instead (`docs/input-map.md`).
  - ⛔ **A GLOBAL claim — no `activeWhen`, live on every screen in every layer — has to earn the
    WHOLE keyboard, not just the screens it was tested on.** `src/input.cpp` holds exactly one
    (`?`, navigation help). F2-F4 are live Avenger keys and F1-F8 the shot HUD's target hotkeys,
    which is why the retired F-key scaffolding was never free (`docs/input-map.md`).
- **Speech**: `src/tts.cpp` wraps **Prism** (same library and pin as CyberAccess, so a speech bug
  reproduces across both mods). All speech goes through it. Speak the focused item only;
  navigation help on keypress; never dump all items on a screen. ⛔ A *backend instance* is not
  thread-safe: every `prism_backend_*` call stays under `tts.cpp`'s mutex and the poll-thread
  callback only raises an atomic flag.
- **Graph A11y Kernel** (`src/Graph/`): engine-neutral navigation core (Factorio Access → … →
  RTAccess → CyberAccess lineage). STL-only by contract — no UE3, SDK, or Windows includes;
  `tests/` compiles it standalone to enforce this. It consumes structured `GraphRender`
  snapshots; this is why UI reading belongs in C++ (feeding it through a string bridge cannot
  work). Design: `docs/graph-kernel.md`.
  - ⛔ **A TABLE IS NOT A LIST, and the kernel has a `GraphSheet` for the difference.** A list row
    flattens into a sentence losing nothing; a table row is several facts under headings drawn
    ONCE at the top, and reading it as a list loses what each cell MEANS, makes every arrow press
    a paragraph, and makes a COLUMN impossible to read down. **Reach for a sheet whenever a screen
    draws column headings** — it needs no announcer changes.
  - ⛔ **Never join two spoken phrases with a bare `", "`** — the GAME ends plenty of its own
    strings with `.`, `!`, `?`, `:` or `;`, and blind joining voices doubled full stops at the
    player. `X2Access::Graph::AppendPhrase` is the rule, and ⚠ **it tests the tail of the
    ACCUMULATED line, never the incoming part** — a reimplementation testing the part instead is a
    different function wearing this one's name. Recipes inherit it through `GraphAnnouncer`;
    **any surface that composes speech outside a recipe owes the same call**, and the companion
    mirrors the same set in `X2AccessHelper.AppendJoined`. Mechanics and the terminator set:
    `docs/graph-kernel.md`.
- **UI reading**: the `src/game_ui*.cpp` family — one core TU (`game_ui.cpp`: SEH primitives,
  stack walkers, call plumbing, behind `src/game_ui_internal.h`) plus one reader TU per surface
  family, mirrored by one recipe TU per family in `src/recipes/` (the dev half compiles only into
  a dev build) — reads `UIScreen`/`UIPanel`/`UIList` object trees through EXPLICIT field offsets
  (never the generated class layouts), and the recipes turn them into GraphRenders — one *recipe*
  per screen (probe → build → name), one attached at a time, listed in `kRecipes` TOPMOST SCREEN
  FIRST. Recipes see the navigator only through `src/recipes/recipe_context.h`;
  `src/menu_navigator.cpp` keeps the state machine — probe, settle, attach, differ, detach — and
  the `kRecipes` literal. ⛔ That literal stays an explicit array in that one file: cross-TU
  static initialization order is unspecified, so a self-registering roster would order itself by
  LINK order and attach to the wrong screen non-deterministically. Which TU reads which screen is
  `docs/surfaces/README.md`. Recipes replace per-screen UnrealScript listeners screen-by-screen;
  existing listeners stay until a recipe covers their screen. Every hand-transcribed offset is
  pinned by a `static_assert` in `src/sdk_layout_checks.cpp`, so a bad transcription or a
  regenerated SDK is a build error, not a silent wrong read.
- **Outbound script calls**: C++ may invoke any UnrealScript function via ProcessEvent. Always
  resolve the UFunction BY NAME at runtime and hand-roll the param struct
  (`GameUI::CallNamedFunction`; `CallStaticNative` for `static` functions, which take the CLASS
  object as `this`). The generated SDK's typed wrappers are unusable (the generator rule under
  **Hard rules**) and only `GameDefines.cpp` is compiled. Native functions need no special
  handling — `X2FiraxisLiveClient.IsLoggedIn` is `FUNC_Native` and has always worked through plain
  ProcessEvent.
  - ⛔ **Hand-rolling means EVERY parameter, `optional` ones included.** An optional argument
    still occupies its slot AHEAD of `ReturnValue`, so a block that omits it hands the engine a
    zero there and takes the return's copy-back past the struct's end — which is how the Commands
    stop read a Chosen as active ten minutes early and passed its first runs by accident
    (`docs/surfaces/tactical.md`). Pin each block against the generated `_Params` struct in
    `sdk_layout_checks.cpp`, as the Scaleform blocks are.

## Surfaces

⛔ **Check `docs/surfaces/README.md` before building anything that sounds like a screen we already
read** — a second reader for a covered surface is the failure mode that registry exists to prevent.
It maps every covered screen to its native TU, its companion class or recipe, its rules doc and its
recon.

⛔ **Read a surface's own rules before editing it.** `docs/surfaces/*.md` holds them, each earned by
a live bug: which widget retains the words, which handler activation must go through, what the game
withholds from a sighted player too. Rules general enough to bite the NEXT surface belong in **Hard
rules** below instead — that is the test for which file a new rule goes in.

## UnrealScript companion owns

- **Game-state narration**: event listeners on Community Highlander hooks
  (CHEventListenerTemplate, XComLWTuple events) for turn changes, ability results, damage, mission
  state. Resolution through `XCOMHISTORY` and localized strings is script-natural; never
  reimplement it via C++ reflection.
  - ⛔ **A state reached through another state is that state's version, not the present.**
    `FindComponentObject` and the component/owner walks resolve in the frame the object you hold
    lives in, and an `ObjectiveInfo` is written once, so the crate it hands back never ages —
    `bRemovedFromPlay` stayed false through twelve pickups. Re-fetch by ObjectID through
    `GetGameStateForObjectID` before reading anything off it (`docs/surfaces/tactical.md`, object
    scanner).
- **Description helpers**: `X2AccessHelper.uc` static functions (cover, hazards, units on tile),
  shared by listeners and the tile reader. `X2AccessTileReader.uc` is the worked example of the
  native-callable pattern: functions invoked on the CDO with simple params, result written to its
  `NativeResult` string property, reflected out by C++ right after the synchronous ProcessEvent
  returns. CDO rule: value-typed state only, never object references (GC).
  `X2AccessSaveReader.uc` is the smallest instance and the clearest statement of the rule behind
  it: it re-runs the row's own formatting over the struct the row RETAINED, and touches neither the
  history nor `ONLINEEVENTMGR.GetSaveGames` — the screen already asked, and asking again could
  answer differently (`docs/surfaces/shell.md`).
- **Combat and replay narration** (`X2AccessTacticalEvents.uc`, `X2AccessReplayNarration.uc`,
  `X2AccessTutorial.uc`) — the design is `docs/surfaces/tactical.md`. Three rules from it generalize:
  - ⛔ **Deferral is a per-event decision, not a default**, and for one action it is SPLIT across
    both visualization windows — the ACTION on `ELD_OnVisualizationBlockStarted`, its OUTCOME on
    `ELD_OnVisualizationBlockCompleted`, structural facts on `ELD_OnStateSubmitted`. **That split is
    what puts cause before consequence, and a shared window cannot**: priority orders listeners for
    the SAME event only, and between events the game's own trigger order is backwards for us.
  - ⛔ **When N indistinguishable callbacks would each speak the same line, claim an IDENTITY, never
    dedup on the text.** Two ADVENT Troopers differ by ObjectID where their words do not, so
    collapsing by text loses a kill.
  - Both paths run through the SAME builders, so a shot reads identically whether an event or a
    history frame produced it — a second narration path may never grow its own wording.
- **Speech vocabulary**: markup native has to speak is resolved to the game's OWN localized words
  (`X2AccessHelper.GetInputVocabulary` / `GetResultFlyovers`). ⛔ Our built-in table is English,
  which is wrong the moment the player is not — key any such table by markup token so a missing
  entry degrades to the English word instead of shifting a positional table.
- ⛔ **No English literal reaches a player from a migrated surface, and every word of OURS is a
  `localized` var on `X2AccessVocab` — one class, one loc section.** The game's own word is looked
  for FIRST (a template's `LocFriendlyName`, a unit's name); a number or a name goes into a
  template through vanilla's `<XGParam:…/>` tags, never concatenated; a counted noun is three keys
  chosen by `X2AccessVocab.Count`. A mod loc file cannot override a key vanilla ships
  (`docs/localization.md`).
- ⛔ **Every new or changed mod-owned player-facing string, in C++ or UnrealScript, must be added to
  `X2AccessVocab` and translated in both `.int` and `.rus`.** First reuse a game-localized word
  when it has the same meaning; keep developer-only diagnostics out of speech. Check all three
  key sets with `python tools/check_localization.py` (`docs/localization.md`).
- ⛔ **One spoken concept has one source.** If the game owns the label, read the screen's word
  and, for an empty read, its localized class default; do not copy that label into our loc files.
  `X2AccessVocab` is for mod-owned speech and generic emergency labels (`docs/localization.md`).
- **Earcons are game-native SoundCues, played through the game's sound manager** —
  `` `SOUNDMGR.PlaySoundEvent("<cue>") `` over the Highlander's SoundCue mapping — never WAVs
  played from the ASI: the game's mixer is where the player's volume settings live. The cues are
  built from `mods/X2AccessCompanion/Sounds/*.wav` by `tools/make_sound_upk.py` at every companion
  build (the SDK editor's drag-and-drop import is the only other way, and it is a sighted one) and
  registered per sound manager in `Config/XComGameData.ini` (`docs/build.md`, sounds). ⛔ A cue
  name must not end in `_<digits>` — FName parsing splits it and the config path never resolves.
  ⛔ A LOOPING cue is held in an `AudioComponent` on a spawned Actor (`X2AccessWallTones`), never
  played through `PlaySoundEvent` (nothing could stop it) and never held on a CDO (the GC rule) —
  and it idles ABOVE the device's cull threshold, never at volume 0: a silent wave instance is a
  stopped one (`docs/engine-notes.md`, audio).
- **Legacy screen listeners**: `UIScreenListener_*.uc` polling listeners with the
  `AccessibilityData` bridge. Working system; migrate to native UI reading incrementally, do not
  expand this pattern to new screens without reason.
- ⛔ **No `/*` and no backtick inside a `.uc` comment, line comments included.** The macro
  preprocessor runs before the lexer and tracks block comments on its own, so a `/*` in a `//`
  comment opens a block it never sees closed and every `` `MACRO `` after it stays unexpanded —
  the compile then fails on a `` `SCREENSTACK `` line with *"Bad or missing expression"*, naming
  nothing near the cause. A backtick in a comment is a macro invocation for the same reason
  (`docs/surfaces/strategy.md`, the Skyranger-arrived prompt).

## The bridge (script ↔ native)

- Script→native: listener writes its `AccessibilityData` string property, then fires the empty
  sentinel `X2Access_Speak()` via SetTimer; the ProcessEvent hook catches it, reflects the property
  out (`Hooks::ReadListenerProperty`), and speaks. This is the only script→native path — **DLLBind
  is conclusively unavailable in XCOM 2** (the SDK compiler rejects the keyword and the runtime
  support is compiled out of the exe).
- Native→script: outbound ProcessEvent calls.
- **Bridge v2** (`X2AccessNarrator.uc` + `src/narrator.cpp`) is the queued, channelled path:
  every channel queues in arrival order and the channel is a LABEL for the scrollback, not a
  priority — ⛔ **no channel clears the queue**, because a line that cleared it would eat the
  damage line explaining the kill it follows. Design and limits: `docs/surfaces/tactical.md`, the
  narrator bridge.
- ⛔ **The mod never interrupts speech and never drops a line for another. `TTS::Speak` has no
  interrupt flag and Prism is always handed `interrupt = false`** — whether a new line cuts off
  the one being read is the SCREEN READER's decision under the player's own settings, and a mod
  that forced it overrode them on every keypress. The Narrator orders lines; it never cuts or
  discards one (`DropPending` on a hard context change is the single deliberate exception).
- ⛔ `Narrator::Source` is not a channel: it records whether a line was read off a surface the game
  itself displayed (`Flyover`) or derived from the game state (`State`), and a `State` line is
  dropped when an identical `Flyover` line was said in the last 5 s. **One way only, and never
  State-against-State** — inside the tutorial both paths run at once and an untargeted ability comes
  out word-for-word twice, but identical text from two state events is load-bearing (the identity
  rule above).

## Engine facts

Heavily modified UE3, 64-bit only, Scaleform GFx UI. No anti-cheat; injection is tolerated.
ProcessEvent is UObject vtable slot 72 (measured on each build, not carried over);
`src/sdk_globals.cpp` validates the GNames/GObjects offsets with sanity heuristics before init
proceeds. ⛔ **There are TWO retail builds — the Steam image and the GOG one, the same source
relinked — so a module-relative address is per BUILD and belongs in the table in
`src/game_build.cpp`, never as a constant in the TU that uses it.** An image the table does not
know falls back to the reference row and says so, because the validation is what decides;
`tools/derive_offsets.py` adds a row from the two exes, and what confirms each number is
`docs/engine-notes.md`. **The measured record
behind every rule below — the dispatch decompilation, the retained-widget censuses, the Flash
path-API survey, the field maps, screen lifetime — is `docs/engine-notes.md`.** What it obliges:

- **Runtime `FString` is `TArray<wchar_t>` (UTF-16)** even though the generated SDK declares `char`.
  `ue3_reflection.cpp` owns the conversion; nothing else re-derives it.
- ⛔ **The ProcessEvent hook sees only NATIVE→script dispatch, so "watch a UI function by name" is
  not an available technique.** A script→script call never reaches the virtual slot we patch —
  which is why the obvious narration taps (`UIWorldMessageMgr.Message`, `UIEventNotices.Notify`,
  `UITacticalHUDMessageBanner.SetBanner`) cannot be hooked at all. Three routes exist instead:
  - ⚠️ **POLL what a manager RETAINS.** `UIWorldMessageMgr` keeps every message it is showing;
    `UIEventNotices` and `UITacticalHUDMessageBanner` retain nothing.
  - ✅ **Chain a NATIVE function's `UFunction::Func` (+0xF0).** A pointer write needing no hooking
    library; `mouse_gate.cpp` uses it to SUPPRESS and `flash_shadow.cpp` to OBSERVE. ⛔ Two
    limits: the thunk receives parameters STILL UNEVALUATED, so it only pays when the payload is
    reachable from `this` or from engine state; and it does not transfer to SCRIPT functions, whose
    `Func` is the single shared `ProcessInternal`.
  - Hooking `ProcessInternal` itself would hand us the whole script call graph and is the one thing
    that would change this calculus. It is not virtual, so it needs an inline hook — analysed in
    full, never built.
- ⛔ **A `localized` var can be declared on TWO classes and populated on only ONE — and the empty one
  fails silently.** **The tell is a DOUBLE SPACE**, since `label @ value` still inserts its
  separator when the label is empty — a `grep -P '  '` over the speech ring is part of the post-run
  audit. When sourcing a word from a class, verify the CDO actually holds it
  (`just dev-props "cdo=<Class>"`) rather than trusting that the `.uc` declares it and the loc file
  contains the key somewhere.
- ⛔ **A list row in this game retains no text — that is the house style, not a quirk of one screen.**
  Rows push their words straight into Flash, bypassing `SetText`, so the retained `Text` is EMPTY
  on a row visibly showing a name. What a row DOES keep is the data it was built from (`UnitRef`,
  `ItemComodity`, `StaffSlotRef`, a delegate), so the companion re-derives the words the way the
  row did. Check which kind a row is before planning the read, and **never read a delegate's FName
  as a label** — it would say *"OnShowObituaries"* to a Russian player.
  - ✅ **When the words cannot be re-derived, catch them IN FLIGHT.** `src/flash_shadow.cpp` chains
    `UIMovie.CopyCommandQueue` and decodes the movie-wide command queue at the one instant it is
    whole, keyed by cache index so a surface asks for its own panel's words (`just dev-flashqueue`).
    ⛔ POLLING that queue never worked and never will — by the time anything else looks, it is
    empty. It captures the PUSH, so it knows the words and nothing about selection, disabled state,
    or anything an `.as` computed after receiving them.
- ⛔ **The Flash path-string API is a VERIFICATION tool and must not become a reading strategy.**
  The game itself calls those getters *"very expensive, use sparingly"* and uses them twice. Flash
  holds only rendered TEXT, never semantics, and no field name in it can be `static_assert`ed.
  Keep it for caret, selection and focus — which the capture cannot see — and note it cannot read
  an `XComScrollingTextField` at all.
- ⛔ **A full-screen `.bk2` movie is engine-native all the way down — no UObject, no reflection, no
  script**, so do not go looking for a UIScreen, a Flash path or a companion hook for anything
  Bink-related. `src/movie_subtitles.cpp` dereferences a raw C++ object, and its rule is the one to
  copy if a second ever needs to: hard-code ONE address and validate it by its VTABLE before
  dereferencing anything, because that check is the entire pin (`docs/surfaces/strategy.md`).
- ⛔ **No gameplay event fires in the TUTORIAL — it replays history, it never submits it.** Two
  consequences: **anything built on the event bus must be tested on a normal mission**
  (Gatecrasher — a campaign with the tutorial checkbox off), and the tutorial reader's
  `UIWorldMessageMgr` polling is not a nicety there but the ONLY narration source.
  ✅ `X2AccessReplayNarration.uc` answers the gap: in a replay the history IS the event stream.
- ⛔ **The tactical HUD holds no text.** Every panel of `UITacticalHUD` is a write-only pipe into
  Flash, so the read-the-widget recipe pattern does NOT transfer: in tactical the widget tree
  supplies STRUCTURE and SELECTION while the companion supplies the WORDS — except the ability bar,
  the enemy refs and the objective list items, which are readable natively (field list in
  `docs/engine-notes.md`).

## Hard rules (each earned by a crash or a design dead end)

- **When a fact is on a screen, read it FROM that screen — even when the game state would also
  answer.** Parity is defined against what a sighted player sees, and the tutorial makes this bite:
  it replays history and never submits states, yet the UI on top is driven identically, so a
  readout sourced from `XCOMHISTORY`, a submitted context or the event bus is silently DEAD on the
  mission a new blind player starts with. Prefer, in order: the screen's own fields and functions,
  the state objects the screen itself cached to populate Flash, and only then a fresh history
  query. `X2AccessHackingReader.uc` is the worked example — not one `XCOMHISTORY` access in the
  whole class (`docs/surfaces/tactical.md`). The companion still owns the words; what this changes
  is WHERE it reads them from.
- **Outbound ProcessEvent calls: game thread only.** Trigger from the Tick drain (InputArbiter
  handlers already run there), never from the WNDPROC or the init thread.
- **Nothing in `DllMain(DLL_PROCESS_DETACH)` may join a thread or touch a library.** `ShutdownAll()`
  runs under the LOADER LOCK with the other threads already dead, so a join never returns. That is
  why `TTS::Shutdown` does NOT call `prism_shutdown()` or `prism_log_shutdown()` and LEAKS the
  context on purpose, and why the dev server's `Stop()` is bounded (`docs/build.md`, Prism).
- **Never cache UObject pointers across frames without revalidation** — GC moves on. Re-find via
  GObjects/SCREENSTACK or validate before use. **Type-revalidation is not enough**: a dead screen
  is still a well-formed instance of its class until GC collects it. A cache may short-circuit a
  rescan only while the instance is still USABLE (focused, showing) — otherwise rescan and take the
  newest (`docs/engine-notes.md`, screen lifetime).
- **A screen's index in GObjects says nothing about its age, and a removed screen keeps all its
  state bits.** Slots are reused and removal sets only `bIsRemoved` (0x80), so "take the last
  instance" latches onto a corpse that passes every readiness test. Screen scans FILTER by the
  usability predicate (which must reject `bIsRemoved`), never pick by position; the durable fix is
  walking `UIScreenStack.Screens` (0x6C, index 0 = the top). GObjects scanning survives only for
  screens the stack does not manage (`UIDialogueBox` is never pushed) and while the stack is being
  rebuilt, and the first fallback per class is logged.
- **Topmost is not the same as reachable — and "gated" is not one question but two.** Exactly
  three things take the WHOLE UI away: the stack's `IsInputBlocked` and `ScreensVisible` flags and
  a full-screen movie (`XComEngine.IsAnyMoviePlaying`). `MenuNavigator::OnTick` detaches and
  refuses to attach while any of them holds.
  - ⛔ **`bCinematicMode` is NOT one of them, and reading it as one cost a player three minutes of
    silence on the Chosen reveal.** The cinematic hide is per SCREEN — it skips every class
    declaring `UIScreen.bShowDuringCinematic`, which are exactly the ones a player can still act on
    mid-cutscene — so the refusal is per screen (`GameUI::ScreenShowsDuringCinematic`), never for
    the layer, and it cannot be inferred from `bIsVisible`, the bit the hide clears
    (`docs/engine-notes.md`).
- **Never gate screen readiness on `bIsVisible`** (UIPanel flag dword bit 0x20). The game leaves it
  FALSE on screens it is plainly showing — a live, focused pause menu reads 0x259 — and testing it
  meant silence at a modal the player then answered blind. Use `bIsInited && bIsFocused`
  (0x10 + 0x40) — `bIsFocused` is what the screen stack maintains for "top of the stack" — and for
  `UIDialogueBox`, whose queue is the real state, `bIsInited` plus a non-empty `m_arrData`. Child
  widgets are the exception: a BUTTON's `bIsVisible` is toggled deliberately and does mean hidden
  (the ant farm's grid is the one screen-level exception, because the GAME gates on it there).
- **Never store UIScreen references as class vars on UIScreenListeners** (listeners live on CDOs; a
  held reference blocks shell-world GC and crashes map transitions). Re-find via
  `` `SCREENSTACK.GetFirstInstanceOf() ``.
- **Three generator settings must stay FALSE** in
  `deps/CodeRed-Generator/Engine/XCOM2/Configuration.cpp` — `m_useConstants`,
  `m_removeNativeFlags`, `m_removeNativeIndex` — each having cost real debugging. What each does
  when on, the generator's other patches, and why there is no `SdkConstants.hpp` any more are in
  `docs/build.md`. `tools/fix_generated_sdk.py` repairs exactly one thing and doubles as the
  regression check: any other line in its output means a generator fix broke.
- **SEH-guard every read into engine memory** (`__try` around reflection and hook dispatch), and keep
  the ProcessEvent hot path cheap: bail on FName index before doing anything else.
- **A widget's declared `Type` does not tell you what the widget IS; the visible child does.** Read
  state from the widget POINTER, and take a row's kind from a widget that is VISIBLE
  (`SetWidgetType` hides the widgets it switches away from but leaves their pointers behind, so
  presence alone is not enough). A screen can hold both kinds at once, so "it works on the other
  rows" proves nothing — and the `Type` tag is a field any mod, ours included, may have rewritten
  under you. The Second Wave rows are the case (`docs/surfaces/shell.md`).
- **A readout that never sounds wrong can still always be wrong — audit its DISTRIBUTION, not its
  lines.** The tile cursor called every covered edge "high cover" for two months; every line was
  well-formed, and counting exposed it — **260 "High cover", zero "Low cover"** on a map made of
  cars and sandbags. A field whose values are enumerable and whose observed range collapses to one
  value is broken, whatever the lines sound like. So after any live run, shape-count the speech
  ring — `grep -oP 'TTS!?: "\K[^"]*' X2Access.log | sed 's/[0-9]\+/N/g' | sort | uniq -c | sort -rn`.
  A cheap corollary: **when two of our own surfaces answer the same question, make them agree or
  find out why they don't** — which is why `X2AccessHelper` exists as a shared oracle.
- ⛔ **Size the buffer from the data, never the data to the buffer — and a cap that drops the tail
  must be LOUD.** Truncating the tail of a screen reads to a blind player as *the screen has
  nothing more*, indistinguishable from the truth and therefore never reported as a bug.
  `UE3Reflect::ReadStringProperty` — the choke point every companion payload passes through — reads
  the length first and sizes to it; the remaining fixed buffers in the `game_ui*.cpp` family are
  per-WIDGET fields bounded by what fits on screen, and **any fixed-size read of something whose
  length the GAME decides is this bug waiting to happen** (`docs/engine-notes.md`).
- **When a control seems not to respond, prove which HALF is broken before fixing either.** Our own
  readout is never evidence about the game's state; when the two disagree, ask the sighted user
  what the screen shows (or read the widgets live) BEFORE theorizing about the call. One screenshot
  settled the Second Wave toggle after a full cycle spent on the write path
  (`docs/surfaces/shell.md`).
- **Sighted parity decides what gets a tab stop.** The stop list mirrors what is actually ON SCREEN;
  anything past that is a separately-labelled scan/aid surface and must never hand the player
  information a sighted player cannot get. Build tactical stops from `UITacticalHUD_MouseControls`
  — the game's own answer to "what does this HUD offer" — never from a sketch of what a HUD
  "should" have: the initiative order is cheat-gated and the standard HUD has no turn counter
  (`docs/surfaces/tactical.md`). Aids that genuinely compensate for lost vision (the tile cursor;
  distance and bearing on the enemy strip; a speech scrollback) stay — labelled as aids, not sold
  as parity.
  - ⛔ **The test for an aid is the FACT, never the RANGE or the effort.** An aid may deliver a fact
    far more conveniently than the game does; it may never deliver a fact the game draws NOWHERE,
    for anyone, at any range. **When the only existing implementation of a readout is the AI's,
    that is the tell** — the cover ring passes, RT's firing-position gear is refused
    (`docs/surfaces/tactical.md`).
- **A recipe is the RULE; game-native narrate-only is the EXCEPTION, and the test is
  destruction.** Recipes buy focus tracking, a stable reading order, and stops on panels the game
  makes unfocusable. *Narrate-only is justified only when taking the arrows would destroy
  information we cannot reproduce* — the Avenger ant farm passes, a room's interior does not
  (`docs/surfaces/avenger.md`). Answer this before writing either kind.
- **A gate is only as good as its other half.** When a surface has a native gate AND a script gate,
  changing one is changing half a rule: `ReadAvengerGate` learned to tolerate a screen while the
  companion's `GetGrid` did not, so native said *go ahead* ten times a second while script said
  *not on the grid* — silence on the exact screen the mode exists for. Change both, or neither.
- ⛔ **The screen's accept handler reads its OWN selection index and ignores the control it is
  handed — so set the selection FIRST, then send the key.** `UISaveGame.OnAccept`/`OnDelete`,
  `UIPersonnel.OnButtonCallback`, `UIChooseFacility.OnAccept` and `UIList.OnUnrealCommand` all do,
  and a click sent while the two disagreed would overwrite or delete the WRONG SAVE. Prefer the
  game's own path (`AcceptButton.Click()`, `SendRowCommand`), sent only once the selection has
  landed. **And the game's path means the INPUT path, not the handler at its end**: mods interpose
  by wrapping a list's `OnItemClicked` delegates, so send Enter to the LIST and let whatever chain
  is installed decide (`docs/surfaces/shell.md`, pause menu).
- ⛔ **An empty answer is indistinguishable from "the game is not showing this", so a wire format
  that drops empties must never carry positional data.** A field looked up in the wrong place
  answers `""`, the build skips an unlabelled node, and a whole control silently ceases to exist.
  Two corollaries: a REPEATING field (`util`, a table's `cell=`) is emitted **including its empty
  values**, because dropping one shifts every value after it; and screen-wide labels that are class
  defaults belong on the screen's own record, not asked of each row.
- **Join after cleaning, never before** — a nickname that `CleanText` reduces to nothing leaves its
  separator behind, so composed readouts stay as separate fields until each part is clean, then go
  through `AppendPhrase`.
- `src/Graph/` stays STL-only. UE3/host concerns go in adapters outside it.

## Build & deploy

**`just` is the interface** (`justfile` at the root; `just` alone lists every recipe) — `just go` is
the one-shot loop, `just go-full` includes the companion, `just test` runs the kernel tests with no
game needed. Recipes, paths, the SDK generator verbs and what each dependency is pinned to are in
**`docs/build.md`**. The rules:

- ⛔ **`-review` is mandatory when launching.** Without it the game boots the DEVELOPER shell
  (`UIShell`) instead of the retail main menu (`UIFinalShell`), so every `UIFinalShell` recipe finds
  nothing and the mod reads as dead. The justfile passes it; a hand-rolled or Steam launch must too.
- ⛔ **`build_uscript` HANGS FOREVER if the game is running** — the running game holds the companion's
  `.u` open and X2ModBuildCommon has no retry and no timeout, so it looks like a slow compile rather
  than a lock. `just kill` first. A FRESH `<SDK>/XComGame/Script/X2AccessCompanion.u` against a
  stale one under the game's `Mods/` means the compile succeeded and only the deploy is stuck.
- ⛔ The MSVC runtime is pinned **`MultiThreaded` (/MT), cache-`FORCE`d**, with `BUILD_SHARED_LIBS
  OFF` in the cache, both before `add_subdirectory(deps/prism)` — an unpinned cache silently links
  a `/MT` Prism into a `/MTd` X2Access.
- ⛔ **Keep every path in this tree overridable.** Documents is not `$HOME/Documents` under OneDrive
  and is not necessarily spelled in English — ask Windows via
  `SHGetKnownFolderPath(FOLDERID_Documents)`. Per-machine game and SDK paths live in an untracked
  `.env` beside the justfile (copy `env.example`); the justfile header documents the precedence.
- ⛔ **A build that ships is a build with the dev harness compiled OUT, proven on the binary.**
  `just release` cuts the rolling `dev` archive from a clean worktree parked at HEAD, configured
  `X2ACCESS_DEVSERVER=OFF`, and refuses to package an ASI that still contains a dev-server
  literal — the harness carries `/exec`, which runs any console command. Never hand a tester the
  everyday `build/` tree's ASI. ⛔ The Forgejo token goes in `.env.rc`, never `.env`: the justfile
  exports `.env` into every recipe's environment, the launched game's included (`docs/build.md`,
  Release).
- ⛔ **Never test for a particular Ultimate ASI Loader host DLL name** — which name it impersonates
  differs per machine, so a missing `version.dll` is not a broken install. Probe the whole candidate
  list and print which host was found.
- ⛔ **Regenerate the SDK from a game running the same mod set the players will run.** The Highlander
  ships replacement `XComGame`/`Engine`/`Core.upk`, so with it enabled every class layout comes from
  its build, and a field can exist in one build and not the other. A dump is Highlander-derived if
  `UIScreenStack` carries `OnInputSubscribers`.

## Dev harness (dev builds only)

A loopback HTTP server on **8774** for observing and driving the live game, plus scancode key
injection — sibling of the RTAccess (8772) and CyberAccess (8773) harnesses. `src/dev_server.cpp`;
how it is gated and armed, the endpoint manual and the `just dev-*` verbs are
**`docs/dev-harness.md`**. The rules that outlive any one endpoint:

- ⛔ **Shared-harness rule** (from RTAccess): do not drive the live game while another agent or
  session is driving it — give the user manual steps instead.
- **The accept thread never touches engine memory.** Engine-facing endpoints queue a job drained by
  `DevServer::Pump()` on the GAME thread — the outbound-ProcessEvent rule extended to reads. A
  game-thread endpoint timing out means *the game is not running frames*, not that the harness is
  broken.
- ⛔ Scripts must call curl as `$env:SystemRoot/System32/curl.exe`. Bare `curl` on PATH resolves to
  MSYS2's cygwin build, whose path translation mangles URL arguments into blanks.
- The harness is how the *our own readout is never evidence* rule gets satisfied: `/flash` and
  `/screenshot` answer what the GAME put on screen. ⚠ A blank `/flash` reading is AMBIGUOUS — an
  unresolved path answers exactly like an empty field.

## Reverse engineering workflow

- **Ghidra** (MCP) for static analysis — the source of the `0x143B50` ProcessEvent offset and the
  class layout. ImageBase is `0x140000000`, so a Ghidra VA maps to `base + (VA − 0x140000000)` at
  runtime.
- **Frida** for live probing — `tools/frida-run.py` (a direct-Python runner that deliberately does NOT
  use any `frida-mcp` server) plus the `frida` skill (`.claude/skills/frida/SKILL.md`), which carries
  the gotchas.
- **Precedence**: prefer the ASI log + Ghidra; reach for Frida only when the answer needs the running
  process. Anything needed more than once belongs in the ASI, not in a Frida script.
- **No hooking library**: both interception points are pointer swaps. If an inline hook is ever
  needed, it is MinHook (`docs/build.md`, dependencies).

## Adding a screen recipe

0. With the game on that screen and the harness armed, `just dev-gui` prints its widget tree and
   `just dev-props "screen=<Class>"` prints every field with its LIVE offset, type and value. That
   is steps 1-2 done from facts; read `SrcOrig` for the *why* (delegates, disabled navigation), not
   to guess layout.
1. Read the screen's UnrealScript in the SDK's `Development/SrcOrig/XComGame/Classes/` — ground truth
   for what the widgets are, which delegate activation must go through, and what a sighted player can
   actually reach. Watch for controls the game reaches only by mouse (its own keyboard path may be a
   dead end) and for `DisableNavigation()`, which means focus write-back there would be pushing focus
   somewhere the game refuses to keep it. ⛔ **First confirm the class is what the presentation layer
   SPAWNS** (`grep` the `XComHQPresentationLayer` / `XComPresentationLayer` function that opens it): a
   shipped class is not a reachable one — `UIFacilityUpgrade()` spawns `UIChooseUpgrade`, and the
   census had the dead class marked as the readable target.
2. Transcribe field offsets from the generated SDK's dump comments into the surface family's
   `game_ui_*.cpp` (generic widget offsets live in `game_ui_internal.h`), add a matching
   `static_assert` per offset to `sdk_layout_checks.cpp`, and expose a `Read<Screen>` view struct.
3. Add the recipe to its family's TU in `src/recipes/`, declare it in `recipes.h` (bumping
   `kRecipeCount`), and list it in `kRecipes` in `menu_navigator.cpp`, TOPMOST SCREEN FIRST —
   probe order is what resolves a stacked screen, and the count turns "declared but never
   listed" (a screen silently never probed) into a build error.
4. Prefer the game's own handlers over poking state: feed a row `OnUnrealCommand`, click a button
   through `Click()`, call the screen's own delegate target. That keeps every side effect the game
   expects.
5. Register it in `docs/surfaces/README.md`.

Screen lookup goes through the game's OWN screen stack (`FindStackScreenRaw` matches by class name
over `UIScreenStack::Screens`); GObjects scanning is the logged fallback for screens the stack does
not manage. Why, and what a corpse looks like: `docs/engine-notes.md`, screen lifetime.

## The tactical layer: input ownership

Three modes, and the layer must always know which one it is in: **World mode** (default — arrows
drive the tile cursor), **HUD mode** (the tactical recipe is FOCUSED; arrows navigate it), and
**Targeting mode** (`UITacticalHUD.m_isMenuRaised` — the GAME owns the arrows, Tab and F1-F8; we
narrate and read there and claim nothing that steers). ⚠ **Narrowed to targeting with a TARGET
LIST**: in free aim the arrows steer nothing, so there the rule inverts and the arrows are ours. The
principle is "do not take a key the game is using to steer", not "do not take arrows". The census
behind every claim below is **`docs/input-map.md`**.

So the tactical recipe is ATTACHED BUT UNFOCUSED while the cursor keeps the arrows — the
navigator's focus split (`Recipe::ambient`; `MenuNavigator::OwnsInput()` = attached AND focused).
⛔ **Gate on the right export**: the tile cursor and the legacy-listener mute stand down on
`OwnsInput()`, the tactical layer's own producers on `MenuAttached()` (a NON-ambient recipe took
the surface — the ambient recipe being attached is their normal state for a whole mission), and
`IsAttached()` is diagnostics plus surfaces the ambient recipe can never share a screen with.
Gating on mere attachment is right for menus and wrong here. An ambient recipe's probe must also
refuse BY ITS OWN HAND any overlay the screen stack cannot show it (`UIDialogueBox` pushes no
screen), because an attached recipe is the only one the navigator re-probes.

⛔ **UE3 matches modifiers LENIENTLY** — a bare vanilla binding fires under ANY modifier — so
**every modifier chord we advertise on a key carrying a vanilla bare binding MUST be claimed, or it
double-fires**, and, in the other direction, **taking a bare letter costs nothing**: the vanilla
action stays reachable at `Ctrl+key` with no config and no rebind. The mechanics and the proof for
each: `docs/input-map.md`.

⛔ **Soldier switching is `E` / `Q`, moved there by CONFIG, not by a claim.** The companion's
`Config/XComInput.ini` moves the game's own next-unit and previous-unit COMMANDS off `Tab` and
`LeftShift` onto `E` and `Q` in both input sections (they were camera rotation, useless to a
world-absolute cursor). Every handler keys on the command, so E and Q are Tab and LeftShift in
every meaning the game gives them — soldier, target, Armory — and the game's own key hints follow.
Two rules fall out. **Never re-provide the switch by claim**: it is narrated by POLLING the
controller, which also catches the mouse click, the turn start and the death-driven re-select.
**Shift is an ordinary modifier ONLY because that binding is gone**: `Key_Left_Shift`'s release
cycled the soldier unconditionally, and a Shift chord that cycles it again means the config did not
merge — check that first (`just dev-props`), never bring back the modifier swallow or the
mouse-gate thunk trick. Bare `Tab` is the HUD layer's entry key in both layers because that config
leaves the game nothing on it.

⛔ **Before claiming a key, check `docs/input-map.md`.** Free keys are nearly exhausted: exactly two
bare letters (`H`, `I`) and the punctuation the game never binds. ⛔ Do NOT poke
`PlayerInput.Bindings` at runtime to make room: the Controls screen reads the live array and bakes
a runtime edit into the player's profile the moment they press Save and Exit there.

## Current state / next steps

⛔ **The work list lives in `docs/next-steps.md`, not here.** First passes owed, unexercised paths,
known defects and queued work all belong there; this file keeps only what is already TRUE about the
mod. When one of those entries is finished, the rule it earned comes back here (or into the
surface's own `docs/surfaces/*.md`) and the entry is deleted there — a "done" mark is not the
product.

## Research scope

When researching for this project, focus on engine/native/UE3 technicals. Do not survey the
accessibility-mod landscape — the author knows it better than web research can capture (see memory).
