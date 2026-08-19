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
- **UI reading** (direction of travel): a native adapter walks
  `UIScreen`/`UIPanel`/`UIList` object trees via `src/ue3_reflection.cpp` and
  builds GraphRenders. This replaces per-screen UnrealScript listeners
  screen-by-screen; existing listeners stay until the native path covers their
  screen.
- **Outbound script calls** (planned `ScriptCall` module): C++ may invoke any
  UnrealScript function via ProcessEvent. The generated SDK ships typed
  wrappers for every base-game function (`generated_sdk/X2SDK/SDK_HEADERS/
  *_classes.cpp`); only `GameDefines.cpp` is compiled today — add wrapper
  packages selectively (XComGame_classes.cpp is 28 MB; prefer extracting what
  is needed). Functions in our own mod have no generated wrappers: resolve the
  UFunction by full name at runtime and hand-roll the param struct. Static
  script functions are called on the class default object.

### UnrealScript companion owns
- **Game-state narration**: event listeners on Community Highlander hooks
  (CHEventListenerTemplate, XComLWTuple events) for turn changes, ability
  results, damage, mission state. Resolution through `XCOMHISTORY` and
  localized strings is script-natural; never reimplement it via C++ reflection.
- **Description helpers**: `X2AccessHelper.uc` static functions (tile
  coordinates, cover, hazards, units on tile). Direction of travel: make these
  callable from C++ (simple params in, result written to a string property the
  C++ side reflects out) so native features like the explore cursor can query
  them per move.
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
- **Never store UIScreen references as class vars on UIScreenListeners**
  (listeners live on CDOs; a held reference blocks shell-world GC and crashes
  map transitions). Re-find via `` `SCREENSTACK.GetFirstInstanceOf() ``.
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
  `Binaries/Win64/`; the ASI logs to `X2Access.log` next to itself.

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
- `deps/CodeRed-Generator` — offline SDK dumper (not part of the build);
  produced `generated_sdk/X2SDK/`, which is committed and is a build input.

## Current state / next steps

The Graph kernel is compiled and tested but **unwired** — nothing builds
GraphRenders or routes arbiter input into it yet. The build-out order:

1. `ScriptCall` module (generic find-function-by-name + game-thread call).
2. First native UI adapter on one simple screen (main menu) driving the
   kernel through InputArbiter, then migrate screens incrementally.
3. Explore cursor moves to C++ (cursor state + input native; per-move tile
   descriptions fetched from `X2AccessHelper` via ScriptCall), retiring
   `X2AccessExplorePanel` and its Highlander input subscription.
4. Bridge v2 (channels/priorities, queued events).

When researching for this project, focus on engine/native/UE3 technicals.
Do not survey the accessibility-mod landscape — the author knows it better
than web research can capture (see memory).
