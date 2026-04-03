# X2Access — XCOM 2 WOTC Accessibility Mod (POC)

Native C++ plugin that integrates [SRAL](https://github.com/m1maker/SRAL) (Screen Reader Abstraction Library) for blind/visually impaired XCOM 2: War of the Chosen players.

This is the proof-of-concept DLL side of a hybrid mod architecture. It proves the pipeline: **Ultimate ASI Loader → ASI plugin → SRAL → Screen Reader / SAPI5 TTS**.

## Building

### Prerequisites

- CMake 3.25+
- Visual Studio 2019+ with C++ desktop workload (MSVC x64)
- XCOM 2 War of the Chosen SDK (install from Steam)
- Git submodules initialized: `git submodule update --init --recursive`

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

The SDK and game paths default to standard Steam install locations. Override at configure time if needed:

```bash
cmake -B build -A x64 \
  -DXCOM2_SDK_PATH="D:/Steam/steamapps/common/XCOM 2 War of the Chosen SDK" \
  -DXCOM2_GAME_PATH="D:/Steam/steamapps/common/XCOM 2/XCom2-WarOfTheChosen"
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
4. Copy `X2Access.asi` into `scripts/`
5. Download [SRAL x64](https://github.com/m1maker/SRAL/releases) and copy `SRAL.dll` (and any dependencies like `nvdaControllerClient64.dll`) into `scripts/`

### File Layout

```
XCom2-WarOfTheChosen/Binaries/Win64/
  version.dll                  ← Ultimate ASI Loader
  scripts/
    X2Access.asi               ← This mod
    SRAL.dll                   ← SRAL library
    nvdaControllerClient64.dll ← NVDA support (optional)
```

## Usage

Launch XCOM 2 WOTC normally. You should hear "X2 Access loaded" via your active screen reader or Windows SAPI5.

### Hotkeys

| Key              | Action                              |
|------------------|-------------------------------------|
| F1               | Speak status                        |
| F2               | Speak help / list hotkeys           |
| F3               | Speak current TTS engine info       |
| Ctrl+Shift+S     | Toggle speech on/off                |

### Logging

Check `scripts/X2Access.log` for initialization details and diagnostics.

## Architecture

- **dllmain.cpp** — Entry point, deferred init thread to avoid loader lock
- **sral_bridge** — Dynamic loading of SRAL.dll via LoadLibrary/GetProcAddress
- **tts** — High-level speech API with queue, interrupt, and toggle
- **input** — Keyboard polling thread (50ms) with debounce
- **hooks** — Stub for future game memory hooks
- **log** — File logger with timestamps and crash-resilient flushing
