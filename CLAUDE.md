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
- **Speech**: `src/tts.cpp` wraps SRAL. All speech in the project goes through
  it. Speak the focused item only; navigation help on keypress; never dump all
  items on a screen.
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
  arbiter claims for camera-relative arrows, Shift+comma/period floors, C
  recenter, Delete re-announce, Backspace two-press move-to; unshifted
  comma/period and PageUp/Dn are RESERVED for the future object scanner. Each
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
  "unexplored"; HaveSeen → static layout only; Seen → full). Claims stand down
  while the shot HUD is raised (`m_isMenuRaised` — targeting owns the arrows),
  while a graph recipe is attached, or when `UITacticalHUD` loses focus.
  **Mouse gate** (`src/mouse_gate.cpp`): with the mouse the active input
  device, `XComTacticalInput.Mouse_CheckForPathing` re-pushes the cached mouse
  pick point through `CursorSetLocation` EVERY FRAME (last line of the input
  state's `PostProcessCheckGameLogic`), reverting any keyboard push one frame
  later. While the tile cursor has been used this mission AND owns world mode,
  the gate flips that one UFunction to `FUNC_Native` with `Func` → a thunk
  that consumes `EX_EndFunctionParms` and returns — script→script dispatch
  goes through exactly those two fields — and restores both exactly whenever
  targeting raises, a recipe attaches, tactical ends, or the ASI unloads.
  Clicks, free-aim (`Mouse_CheckForFreeAim`), and camera scroll are untouched;
  a mouse click while gated commits to the pathing pawn's preview of OUR tile,
  the same two-press semantics as Backspace.
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
  object references (GC).
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
- Planned bridge v2: structured payload (channel/priority + text, so combat
  narration queues while focus changes interrupt) and a queue property so
  rapid events are not lost.

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
  — cannot be built this way. Game-event narration belongs to companion
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
  layer can enumerate rows. Future option: call
  `GFxMoviePlayer.GetVariableString` outbound to read Flash state directly.
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

- **Outbound ProcessEvent calls: game thread only.** Trigger from the Tick
  drain (InputArbiter handlers already run there), never from the WNDPROC or
  the init thread.
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

- Configure: `cmake -B build -A x64` (game/SDK paths default to
  `D:/SteamLibrary/...`; override with `-DXCOM2_GAME_PATH=` /
  `-DXCOM2_SDK_PATH=`).
- C++ ASI: `cmake --build build --config Release` →
  `build/bin/Release/X2Access.asi`.
- Everything + deploy to game: `cmake --build build --config Release --target
  deploy` (copies the ASI to `Binaries/Win64/scripts/` and chains
  `build_uscript`, which compiles the mod via X2ModBuildCommon and
  self-deploys to `XComGame/Mods/`).
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

  Both take the same selector — `?screen=<Class>` (the screen stack, topmost
  when unnamed), `?instance=<Class>` (newest instance anywhere in GObjects, so
  it reaches objects the stack never holds — `XCom3DCursor`, a cheat manager,
  a tooltip manager), or `?ptr=0x<addr>` copied out of an earlier dump and
  re-validated against GObjects before it is walked. So the loop is: `/gui` to
  see the tree, `?ptr=` from a line of it, `/props` to read that widget out.

An unknown path returns the endpoint list. `/speech` and `/log` are
cursor-paged: poll with the returned `cursor:` value for exactly-once delivery.

Driving it: `just arm`, then `just dev-health` / `dev-status` / `dev-speech` /
`dev-log` / `dev-exec "<cmd>"` / `dev-input <keys> [kernel]` / `dev-shot` /
`dev-gui [depth] [select]` / `dev-props [select] [filter]`, or
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

- `deps/SRAL` (submodule) — speech output, linked static.
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
- `deps/CodeRed-Generator` — the SDK dumper. It produced `generated_sdk/X2SDK/`,
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
  steers.

So the recipe must be ATTACHED BUT UNFOCUSED while the cursor keeps the arrows.
`TileReader::ComputeActive` currently stands down on
`MenuNavigator::IsAttached()` — right for menus, wrong here: the navigator needs
an explicit focused state and the tile reader has to gate on that instead.

**Tab is taken, twice**: `eTBC_NextUnit` in world mode
(`UIUtilities_Input.uc:451`) and `NextTarget` while the shot HUD is raised
(`UITacticalHUD.uc:459`). **Ctrl+Tab is free** — UE3 matches modifiers strictly,
which is why `XComInput.ini` needs a separate `Shift=True` entry for R's error
report alongside plain R, and why F4 carries `bIgnoreAlt=True` — so a Ctrl chord
hits no tactical binding. Ctrl+Tab therefore enters/exits the HUD layer and
plain Tab is claimed ONLY while the layer is focused. We do NOT rebind the
player's config: a rebind outlives an uninstall, and re-exposing NextUnit from
our own chord would leave the game's own hint text lying.

## Current state / next steps

The Graph kernel is wired and driving real screens (see **UI reading** above).
Remaining:

1. `ScriptCall` module (generic find-function-by-name + game-thread call).
   `game_ui.cpp` already hand-rolls this per call site — and now exports the
   first pieces (`FindCompanionCDO`, `CallScriptFunction`); the module is the
   generalization, not a prerequisite.
2. Continue migrating screens to native recipes (save/load, the strategy
   layer, tactical HUD).
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
4. Bridge v2 (channels/priorities, queued events).
5. The dialog recipe attaches wherever a `UIDialogueBox` shows, including in
   the strategy and tactical layers — deliberate (a modal must be readable),
   but it means the layer takes over input there before those screens have
   recipes of their own. Its screen lookup caches the newest box, which a
   presentation-layer change could leave stale; covering dialogs outside the
   shell should switch that to a screen-stack walk.
6. Dev harness: `/gui` and `/props` are in (see **Dev harness** above) — built
   2026-08-19, NOT yet run against a live game. `/tile` (the tile reader's
   state as structured data) is the remainder of phase 3.

When researching for this project, focus on engine/native/UE3 technicals.
Do not survey the accessibility-mod landscape — the author knows it better
than web research can capture (see memory).
