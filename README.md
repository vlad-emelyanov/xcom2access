# X2Access — XCOM 2 WOTC Accessibility Mod

Screen-reader accessibility for blind and visually impaired players of XCOM 2:
War of the Chosen. To our knowledge, the first native (injected C++) mod for
this game.

X2Access is two cooperating layers:

- **Native ASI plugin** (`src/`) — injected via
  [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
  Owns speech output through
  [SRAL](https://github.com/m1maker/SRAL) (screen readers / SAPI5), keyboard
  input via a window-procedure arbiter, a ProcessEvent hook that observes the
  game's script function dispatch, and the engine-neutral navigation kernel.
- **UnrealScript companion mod** (`mods/X2AccessCompanion/`) — built with the
  official SDK against the
  [X2WOTCCommunityHighlander](https://github.com/X2CommunityCore/X2WOTCCommunityHighlander).
  Owns game-state interpretation: screen listeners today, game-state narration
  on Highlander event hooks as the project grows.

The two halves talk over the engine's own dispatch: the companion writes a
string property and fires an empty sentinel function (`X2Access_Speak`), which
the native ProcessEvent hook catches, reads out via reflection, and speaks.
There is no DLLBind in XCOM 2's engine build, so this is the only
script-to-native path — and it is the same pattern proven by native modding
frameworks for other UE3 games.

The division of responsibility, engine facts, and development rules are
documented in [CLAUDE.md](CLAUDE.md).

## Building

### Prerequisites

- CMake 3.25+
- Visual Studio 2019+ with C++ desktop workload (MSVC x64)
- XCOM 2 War of the Chosen SDK (install from Steam)
- Git submodules initialized: `git submodule update --init --recursive`
  (this includes
  [X2WOTCCommunityHighlander](https://github.com/X2CommunityCore/X2WOTCCommunityHighlander)
  at `deps/X2WOTCCommunityHighlander`, pinned to `v1.31.0-release` — the same
  build as the workshop release the game loads; the companion mod compiles
  against its source)

### Build Steps

```bash
cmake -B build -A x64
cmake --build build --config Release
```

Output: `build/bin/Release/X2Access.asi`

### UnrealScript Companion Mod

The companion mod (X2AccessCompanion) is compiled using [X2ModBuildCommon](https://github.com/X2CommunityCore/X2ModBuildCommon) integrated into the CMake build:

```bash
# Compile UnrealScript mod only
cmake --build build --target build_uscript --config Release

# Build everything and deploy to game directory
cmake --build build --target deploy --config Release
```

The SDK and game paths default to this machine's Steam library (`D:/SteamLibrary/steamapps/common/`). Override at configure time if your library lives elsewhere:

```bash
cmake -B build -A x64 \
  -DXCOM2_SDK_PATH="<SteamLibrary>/steamapps/common/XCOM 2 War of the Chosen SDK" \
  -DXCOM2_GAME_PATH="<SteamLibrary>/steamapps/common/XCOM 2/XCom2-WarOfTheChosen"
```

## Installation

### Automated (deploy target)

```bash
cmake --build build --target deploy --config Release
```

This copies `X2Access.asi` to the game's `Binaries/Win64/scripts/` directory and deploys the companion mod to `XComGame/Mods/`. You still need to manually install the ASI loader and SRAL (see below).

### Manual

1. Download [Ultimate ASI Loader x64](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) — get `version-x64.zip`
2. Extract `version.dll` to your XCOM 2 WOTC binary directory:
   ```
   <Steam>/steamapps/common/XCOM 2/XCom2-WarOfTheChosen/Binaries/Win64/
   ```
3. Create a `scripts/` folder inside `Win64/`
4. Copy `X2Access.asi` into `scripts/` (SRAL is linked statically — no separate `SRAL.dll` needed)
5. For NVDA support, copy `nvdaControllerClient.dll` into `Win64/` (it must resolve against the game executable's directory)

### File Layout

```
XCom2-WarOfTheChosen/Binaries/Win64/
  version.dll                  ← Ultimate ASI Loader
  nvdaControllerClient.dll     ← NVDA support (optional)
  scripts/
    X2Access.asi               ← This mod (SRAL statically linked)
```

## Usage

Launch XCOM 2 WOTC normally. You should hear "X2 Access loaded" via your active screen reader or Windows SAPI5.

### Hotkeys

| Key              | Action                              |
|------------------|-------------------------------------|
| F1               | Speak status                        |
| F2               | Speak help / list hotkeys           |
| F3               | Speak current TTS engine info       |
| F4               | Toggle event discovery logging      |
| Shift+/ (?)      | Read navigation help                |
| Ctrl+Shift+S     | Toggle speech on/off                |

Hotkeys are claimed through the input arbiter: while the mod handles a chord, the game does not see it.

### Logging

Check `scripts/X2Access.log` for initialization details and diagnostics.

## Architecture

The rule of thumb: if the information is on screen (widgets, lists, focus),
the C++ side reads it; if it is about the game simulation (units, cover,
turns, objectives), UnrealScript reads it and pushes; input and speech are
always native. Full detail in [CLAUDE.md](CLAUDE.md).

### Native plugin (`src/`)

- **dllmain.cpp** — Entry point, deferred init thread to avoid loader lock
- **tts** — High-level speech API over statically linked SRAL: queue, interrupt, toggle
- **input_arbiter** — Window-procedure subclass: claims chords per (key, modifier-mask) and swallows them before the game's input pipeline
- **input** — Hotkey claim registrations
- **hooks** — SDK init, handler registration, the script↔native bridge endpoint
- **process_event_hook / event_dispatch** — ProcessEvent vtable hook (slot 72 on every UObject vtable) and a name-indexed event router with deferred registration for late-loading mod FNames
- **sdk_globals / ue3_reflection** — GNames/GObjects resolution with sanity validation, and SEH-guarded property reflection (including the runtime's UTF-16 FStrings)
- **Graph/** — Engine-neutral accessibility navigation kernel (graph-a11y-spec lineage), STL-only by contract, conformance-tested standalone via `tests/graph` (`ctest --test-dir build -C Release`, no game needed)
- **log** — File logger with timestamps and crash-resilient flushing

### Companion mod (`mods/X2AccessCompanion/`)

- **UIScreenListener_*.uc** — Per-screen listeners (main menu, difficulty,
  options, tactical HUD, …) that detect focus and value changes and hand the
  text to the native side over the bridge
- **X2AccessExplorePanel.uc** — Tactical explore-mode input via the
  Highlander's input subscription
- **X2AccessHelper.uc** — Static description helpers: UI widget values
  (checkbox, slider, spinner, dropdown), navigation help, and tactical tile
  facts (cover, hazards, units) resolved through the game-state history

The [X2WOTC Community Highlander](https://steamcommunity.com/workshops/filedetails/?id=1134256495)
is a **required** dependency of the companion mod, at compile time (the mod
builds against its source, pinned as a submodule to the same release the
workshop ships) and at runtime (subscribe to it on the Steam Workshop and
enable it alongside X2AccessCompanion).
