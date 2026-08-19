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
- Some screens are Flash-driven with no UIList (e.g. key bindings): neither
  layer can enumerate rows. Future option: call
  `GFxMoviePlayer.GetVariableString` outbound to read Flash state directly.
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
- **Escaping a UIList to sibling buttons** needs
  `Navigator.CheckForOwnerHandling` plus deferred (0.01 s timer) focus — never
  mutate Navigator state inside its own `Next()`.
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
- `deps/X2WOTCCommunityHighlander` — pure UnrealScript, no native code.
  Compile-time include for the companion mod and the source of narration event
  hooks. **Currently an untracked, unpinned checkout and undeclared at
  runtime — must become a pinned submodule with the requirement declared in
  the mod metadata/README before release.**
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

## Current state / next steps

The Graph kernel is wired and driving real screens (see **UI reading** above).
Remaining:

1. `ScriptCall` module (generic find-function-by-name + game-thread call).
   `game_ui.cpp` already hand-rolls this per call site — and now exports the
   first pieces (`FindCompanionCDO`, `CallScriptFunction`); the module is the
   generalization, not a prerequisite.
2. Continue migrating screens to native recipes (save/load, the strategy
   layer, tactical HUD).
3. Tile cursor IN-GAME VERIFICATION (built 2026-08-19, not yet run in a
   mission). Compile-time-unverifiable assumptions to check live:
   left/right mirroring of the camera-relative axes (single flip point:
   `X2AccessTileReader.GetScreenDirs`), the cover-flag bit order behind
   `GetCoverDirection` (`AppendCoverEdge` assumes N/S/E/W = bits 0-3, low at
   4-7), whether `CursorSetLocation` sticks while the cursor is chained to a
   unit, `ClickToPath` firing from an outbound call, and the shot-HUD
   stand-down. Then: object scanner (comma/period + PageUp/Dn are reserved for
   it), vantage read (V), interact-at-cursor.
4. Bridge v2 (channels/priorities, queued events).
5. The dialog recipe attaches wherever a `UIDialogueBox` shows, including in
   the strategy and tactical layers — deliberate (a modal must be readable),
   but it means the layer takes over input there before those screens have
   recipes of their own. Its screen lookup caches the newest box, which a
   presentation-layer change could leave stale; covering dialogs outside the
   shell should switch that to a screen-stack walk.

When researching for this project, focus on engine/native/UE3 technicals.
Do not survey the accessibility-mod landscape — the author knows it better
than web research can capture (see memory).
