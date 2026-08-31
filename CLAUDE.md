# X2Access — screen-reader accessibility for XCOM 2 WOTC

X2Access makes XCOM 2: War of the Chosen playable for blind players. It is a
two-layer mod — the first known native (injected C++) mod for this game:

1. **C++ ASI plugin** (`src/`) — injected via Ultimate ASI Loader. Owns speech
   output, input, hooking, and UI navigation.
2. **UnrealScript companion mod** (`mods/X2AccessCompanion/`) — built with the
   official SDK. Owns game-state interpretation and narration triggers.

⛔ **This file is the RULEBOOK. It holds nothing that changes when a surface, an endpoint or a task
list changes** — a rule earns its place here by forbidding a mistake, never by recording what a past
session did, promising what a future one will, or describing something a reader can look up. When a
rule needs evidence, state the rule and point at the doc that holds it. Everything that moves lives
in `docs/`:

| Where | What |
|---|---|
| `docs/next-steps.md` | what is NOT done: first passes owed, unexercised paths, known defects, queued work |
| `docs/surfaces/README.md` | the registry of every surface we read — native TU, companion class, rules doc, recon |
| `docs/surfaces/*.md` | per-surface rules, each earned by a live bug |
| `docs/*-recon.md` | how a surface was measured BEFORE it was built |
| `docs/engine-notes.md` | the measured record of Firaxis' UE3 — dispatch, offsets, field maps, what retains text |
| `docs/input-map.md` | what the game binds, what we claim, what is left |
| `docs/dev-harness.md` | the loopback server's endpoints and how to drive a live session without eyes or hands |
| `docs/build.md` | configure, build, deploy, and what each dependency is pinned to |
| `docs/graph-kernel.md` | the a11y kernel's sheet design |
| `docs/ui-inventory.md` | the census of every `UIScreen` class in the game |

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
    difference.** A list row flattens into a sentence losing nothing; a table row
    is several facts under headings drawn ONCE at the top, and reading it as a
    list loses what each cell MEANS, makes every arrow press a paragraph, and
    makes a COLUMN impossible to read down. **Reach for a sheet whenever a screen
    draws column headings** — the mechanism (`NodeVtable::Column`,
    `KeyGraph::SlideToColumn`, `NodeVtable::LandGroup`, sparse rows) is
    `docs/graph-kernel.md`, and it needs no announcer changes.
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
  which is wrong the moment the player is not — key any such table by markup token so a missing entry
  degrades to the English word instead of shifting a positional table.
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

Heavily modified UE3, 64-bit only, Scaleform GFx UI. No anti-cheat; Firaxis ships EasyHook64.dll
themselves — injection is tolerated. ProcessEvent is UObject vtable slot 72 at
`moduleBase + 0x143B50`; GNames/GObjects offsets live in the generated SDK constants, and
`src/sdk_globals.cpp` validates them with sanity heuristics before init proceeds. The measured
record behind everything below — the dispatch decompilation, the retained-widget censuses, the Flash
path-API survey, the field maps — is **`docs/engine-notes.md`**. What it obliges:

- **Runtime `FString` is `TArray<wchar_t>` (UTF-16)** even though the generated SDK declares `char`.
  `ue3_reflection.cpp` owns the conversion; nothing else re-derives it.
- ⛔ **The ProcessEvent hook sees only NATIVE→script dispatch, so "watch a UI function by name" is
  not an available technique.** A script→script call runs through `CallFunction` → `ProcessInternal`,
  never the virtual slot we patch — which is why the obvious narration taps
  (`UIWorldMessageMgr.Message`, `UIEventNotices.Notify`/`NotifyBanner`,
  `UITacticalHUDMessageBanner.SetBanner`) cannot be hooked at all. Three routes exist instead:
  - ⚠️ **POLL what a manager RETAINS.** `UIWorldMessageMgr` keeps every message it is showing, so
    damage numbers, crit labels, the `UITacticalTutorialMgr` cursor-help strings and the tutorial's
    anchored blades are readable after all; only the *unanchored* HUD blade is lost to Flash.
    `UIEventNotices` and `UITacticalHUDMessageBanner` retain nothing.
  - ✅ **Chain a NATIVE function's `UFunction::Func` (+0xF0).** A pointer write — save the original,
    write ours, call theirs — needing no hooking library; `mouse_gate.cpp` uses it to SUPPRESS and
    `flash_shadow.cpp` to OBSERVE. ⛔ Two limits: the thunk receives parameters STILL UNEVALUATED, so
    it only pays when the payload is reachable from `this` or from engine state; and it does not
    transfer to SCRIPT functions, whose `Func` is the single shared `ProcessInternal`.
  - Hooking `ProcessInternal` itself would hand us the whole script call graph and is the one thing
    that would change this calculus. It is not virtual, so it needs an inline hook — analysed in
    full, never built.
- ⛔ **A `localized` var can be declared on TWO classes and populated on only ONE — and the empty one
  fails silently.** `UITacticalHUD_InfoPanel` declares the shot/crit labels that `XComGame.<loc>`
  ships under `[UITacticalHUD_ShotHUD]` only, so its copies are `""` at runtime while its plain
  defaults hold their values. Reading the empty pair costs more than a caption: a shot whose hit and
  crit both sit at 60 speaks the same number twice, meaning two different things. **The tell is a
  DOUBLE SPACE**, since `label @ value` still inserts its separator when the label is empty — a
  `grep -P '  '` over the speech ring is part of the post-run audit. When sourcing a word from a
  class, verify the CDO actually holds it (`just dev-props "cdo=<Class>"`) rather than trusting that
  the `.uc` declares it and the loc file contains the key somewhere.
- ⛔ **A list row in this game retains no text — that is the house style, not a quirk of one screen.**
  Rows take their words as parameters and push them straight into Flash, bypassing
  `UIButton.SetText` / `UIListItemString.SetText`, so the retained `Text` is EMPTY on a row that is
  visibly showing a name. What a row DOES keep is the data it was built from (`UnitRef`,
  `ItemComodity`, `StaffSlotRef`, a delegate), so the companion re-derives the words the way the row
  did. Check which kind a row is before planning the read, and **never read a delegate's FName as a
  label** — it would say *"OnShowObituaries"* to a Russian player.
  - ✅ **When the words cannot be re-derived, catch them IN FLIGHT.** `src/flash_shadow.cpp` chains
    `UIMovie.CopyCommandQueue` and decodes the movie-wide command queue at the one instant it is
    whole, keyed by cache index so a surface asks for its own panel's words (`just dev-flashqueue`).
    ⛔ POLLING that queue never worked and never will — by the time anything else looks, it is
    empty. It captures the PUSH, so it knows the words and nothing about selection, disabled state,
    or anything an `.as` computed after receiving them.
- ⛔ **The Flash path-string API is a VERIFICATION tool and must not become a reading strategy.**
  `UIMCController.uc` calls those getters *"an immediate and blocking operation … very expensive,
  use sparingly"* and the shipped game takes its own advice: two call sites in all of `XComGame`.
  Flash holds only rendered TEXT, never semantics, and no field name in it can be `static_assert`ed.
  Keep it for caret, selection and focus — which the capture cannot see — and note it cannot read an
  `XComScrollingTextField` at all.
- ⛔ **A full-screen `.bk2` movie is engine-native all the way down — no UObject, no reflection, no
  script**, so do not go looking for a UIScreen, a Flash path or a companion hook for anything
  Bink-related. `src/movie_subtitles.cpp` is the only surface in this mod that dereferences a raw C++
  object, and the rule it established is the one to copy if a second ever needs to: hard-code ONE
  address (the global) and validate it by its VTABLE before dereferencing anything through it,
  because no offset on a non-UObject can be `static_assert`ed and that check is therefore the entire
  pin.
- ⛔ **No gameplay event fires in the TUTORIAL — it replays history, it never submits it.**
  `ContextBuildGameState` never runs and `X2EventManager.TriggerEvent` is never reached, for our
  listeners or the game's own. Two consequences: **anything built on the event bus must be tested on
  a normal mission** (Gatecrasher — a campaign with the tutorial checkbox off), and the tutorial
  reader's `UIWorldMessageMgr` polling is not a nicety there but the ONLY narration source.
  ✅ `X2AccessReplayNarration.uc` answers the gap: in a replay the history IS the event stream.
- ⛔ **The tactical HUD holds no text.** Every panel of `UITacticalHUD` is a write-only pipe into
  Flash, so `/gui` there prints 159 panels and almost no words and the read-the-widget recipe pattern
  does NOT transfer. In tactical the widget tree supplies STRUCTURE and SELECTION while the companion
  supplies the WORDS — except the ability bar, the enemy refs and the objective list items, which are
  readable natively (field list in `docs/engine-notes.md`).

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

**`just` is the interface** (`justfile` at the root; `just` alone lists every recipe) — `just go` is
the one-shot loop, `just go-full` includes the companion, `just test` runs the kernel tests with no
game needed. Recipes, paths, the SDK generator verbs and what each dependency is pinned to are in
**`docs/build.md`**. The rules:

- ⛔ **`-review` is mandatory when launching.** Without it the game boots the DEVELOPER shell
  (`UIShell`) instead of the retail main menu (`UIFinalShell`), so every `UIFinalShell` recipe finds
  nothing and the mod reads as dead. The justfile passes it; a hand-rolled or Steam launch must too.
- ⛔ **`build_uscript` HANGS FOREVER if the game is running** — the running game holds the companion's
  `.u` open, and X2ModBuildCommon has no retry and no timeout, so it looks like a slow compile rather
  than a lock. `just kill` first. Diagnose by the SDK-side artifact: a FRESH
  `<SDK>/XComGame/Script/X2AccessCompanion.u` against a stale one under the game's `Mods/` means the
  compile succeeded and only the deploy is stuck.
- ⛔ The MSVC runtime is pinned **`MultiThreaded` (/MT), cache-`FORCE`d**, before
  `add_subdirectory(deps/prism)`, with `BUILD_SHARED_LIBS OFF` in the cache before that same line.
  Prism sets `CMAKE_MSVC_RUNTIME_LIBRARY` itself off `CMAKE_BUILD_TYPE`, which is EMPTY under the
  multi-config generator, so an unpinned cache silently links a `/MT` Prism into a `/MTd` X2Access.
- ⛔ **Keep every path in this tree overridable.** Hardcoding this machine's layout had to be undone
  across CMakeLists, the justfile and the dev server: Documents is not `$HOME/Documents` under
  OneDrive and is not necessarily spelled in English — ask Windows via
  `SHGetKnownFolderPath(FOLDERID_Documents)`.
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
the endpoint manual and the `just dev-*` verbs are **`docs/dev-harness.md`**. The rules that outlive
any one endpoint:

- **Double-gated**, and both gates matter: compile with `-DX2ACCESS_DEVSERVER=ON`, arm with a marker
  FILE (`Binaries/Win64/scripts/X2Access.devserver`, written by `just arm` / `just disarm`). A file
  rather than an env var because neither Steam nor `just run` inherits our environment. A release
  `.asi` holds no socket code at all.
- **The accept thread never touches engine memory.** Engine-facing endpoints queue a job drained by
  `DevServer::Pump()` on the GAME thread — the outbound-ProcessEvent rule extended to reads.
- `/health`, `/speech` and `/log` answer OFF-thread from process-memory rings, which is what makes
  them useful when the game thread is the broken thing. So a game-thread endpoint timing out means
  *the game is not running frames*, NOT that the harness is broken — check `pump-age-ms` first.
- `Stop()` runs under the loader lock: a bounded 1 s wait on the accept thread, then detach, never
  `join()`. Same reasoning as the `DLL_PROCESS_DETACH` rule under **Hard rules**.
- ⛔ **Shared-harness rule** (from RTAccess): do not drive the live game while another agent or
  session is driving it — give the user manual steps instead.
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
  LIST**: in free aim the arrows steer nothing, so there the rule inverts and the arrows are ours.
  The principle is "do not take a key the game is using to steer", not "do not take arrows".

So the tactical recipe is ATTACHED BUT UNFOCUSED while the cursor keeps the arrows — the
navigator's focus split (`Recipe::ambient`; `MenuNavigator::OwnsInput()` = attached AND focused).
⛔ **Gate on the right export**: the tile cursor and the legacy-listener mute stand down on
`OwnsInput()`, the tactical layer's own producers on `MenuAttached()` (a NON-ambient recipe took
the surface — the ambient recipe being attached is their normal state for a whole mission), and
`IsAttached()` is diagnostics plus surfaces the ambient recipe can never share a screen with.
Gating on mere attachment is right for menus and wrong here. An ambient recipe's probe must also
refuse BY ITS OWN HAND any overlay the screen stack cannot show it (`UIDialogueBox` pushes no
screen), because an attached recipe is the only one the navigator re-probes.

⛔ **UE3 matches modifiers LENIENTLY**: a `KeyBind`'s `Control`/`Shift`/`Alt` are modifiers the chord
REQUIRES, while modifiers the player merely happens to hold are NOT checked, so **a bare vanilla
binding fires under ANY modifier**. Two consequences run in opposite directions and both are rules:

- **Every modifier chord we advertise on a key carrying a vanilla bare binding MUST be claimed, or
  it double-fires.** Ctrl+Tab enters/exits the HUD layer, so plain Tab is claimed only while the
  layer is focused.
- **Taking a bare letter costs nothing — no config snippet, no rebind.** Leniency already leaves
  every vanilla bare-key action reachable at `Ctrl+key`: we claim and swallow the bare key, the Ctrl
  chord passes through unclaimed, and the engine matches the vanilla binding with Ctrl held. So the
  accessibility verbs outrank the vanilla bindings, and a displaced action is not lost.

⚠ **Leniency does NOT hand us a free `Shift`, so we SWALLOW the modifier itself** — `LeftShift`'s
RELEASE calls `PrevUnit()` unconditionally in the tactical input state, which cycled the soldier at
the end of every `Shift+` chord. The arbiter accepts a claim on a modifier key, side-resolved from
lParam's scan code because `WM_KEYDOWN` reports the generic `VK_SHIFT`. **Our own chord matching is
untouched, and that is the linchpin**: `GetKeyState` reflects what the OS recorded when the message
was pulled off the queue, not what the window procedure then did with it. ⛔ Do NOT try to solve this
with the mouse-gate thunk trick — it does not apply to a function taking a parameter and returning a
bool, and the swallow needs no engine offset at all.

⛔ **Tab stays the game's, in both of its meanings** — next unit in world mode, next target while the
shot HUD is raised. Soldier switching is narrated by POLLING the controller, which leaves the binding
untouched AND catches the mouse click, the turn start and the death-driven re-select that a key claim
never could. The one exception is `Shift+Tab`, claimed and re-provided only because the Shift swallow
costs `Key_Tab`'s prev-soldier branch the `PressedKeys` entry it reads — a debt the swallow created,
not a verb the layer wanted.

⛔ **Before claiming a key, check `docs/input-map.md`** — the census of what the game binds, what we
hold and what is left. Free keys are nearly exhausted: exactly two bare letters (`H`, `I`) and the
punctuation the game never binds. ⛔ Do NOT poke `PlayerInput.Bindings` at runtime to make room:
`UIKeybindingsPCScreen.ReadBindings()` reads the live array, so a runtime edit shows up in the
Controls screen and is baked into the player's profile the moment they press Save and Exit there.

## Current state / next steps

⛔ **The work list lives in `docs/next-steps.md`, not here.** First passes owed, unexercised paths,
known defects and queued work all belong there; this file keeps only what is already TRUE about the
mod. When one of those entries is finished, the rule it earned comes back here (or into the
surface's own `docs/surfaces/*.md`) and the entry is deleted there — a "done" mark is not the
product.

## Research scope

When researching for this project, focus on engine/native/UE3 technicals. Do not survey the
accessibility-mod landscape — the author knows it better than web research can capture (see memory).
