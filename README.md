# X2Access — XCOM 2 WOTC Accessibility Mod

Screen-reader accessibility for blind and visually impaired players of XCOM 2:
War of the Chosen. To our knowledge, the first native (injected C++) mod for
this game.

X2Access is two cooperating layers:

- **Native ASI plugin** (`src/`) — injected via
  [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
  Owns speech output through
  [Prism](https://github.com/ethindp/prism) (screen readers / SAPI5), keyboard
  input via a window-procedure arbiter, a ProcessEvent hook that observes the
  game's script function dispatch, UI reading straight off the widget tree, and
  the engine-neutral navigation kernel.
- **UnrealScript companion mod** (`mods/X2AccessCompanion/`) — built with the
  official SDK against the
  [X2WOTCCommunityHighlander](https://github.com/X2CommunityCore/X2WOTCCommunityHighlander).
  Owns game-state interpretation: combat narration on Highlander event hooks,
  and the words for every screen whose text goes into Flash and is retained
  nowhere.

The rule of thumb: **if the information is on screen (widgets, lists, focus),
the C++ side reads it; if it is about the game simulation (units, cover, HP,
turns, objectives), UnrealScript reads it and pushes; input and speech are
always native.**

The two halves talk over the engine's own dispatch. Script to native: the
companion writes a string property and fires an empty sentinel function
(`X2Access_Speak`), which the native ProcessEvent hook catches, reads out via
reflection, and speaks — there is no DLLBind in XCOM 2's engine build, so this
is the only script-to-native path. Native to script: outbound ProcessEvent
calls onto the companion's class default objects, whose answers are reflected
straight back out.

The division of responsibility, engine facts, and development rules are
documented in [CLAUDE.md](CLAUDE.md); per-screen reverse-engineering notes live
in [`docs/`](docs/).

## What is covered

X2Access is under active development and does not yet cover the whole game.
What works today:

**Shell and menus** — main menu, settings, the whole new-campaign flow
(difficulty, Second Wave, Ironman, narrative content, soundtrack), save and
load, the in-game pause menu, modal dialogs, and the text-entry box that names
a save — the one surface where your own keystrokes are echoed back, because
they go into a Flash text field no screen reader can see.

**The Avenger** — the room grid ("ant farm"), with the game's own eight-way
room navigation turned on; inside a room; choosing what to build; clearing
debris; the personnel roster, read as a proper table rather than a flattened
list; and the thirteen-screen commodity family — choose research, build items,
storage, implants, the black market, post-mission loot and the rest — item card
included.

**The geoscape** — a category ring over regions, missions, scan sites, alien
facilities, faction headquarters and continents, each with distance and bearing
from a sort origin you choose.

**Tactical combat** — a tile cursor you walk around the map with (cover per
edge, hazards, ledges, fog parity, reachability, and a two-press move-to that
commits through the game's own click path); an object scanner ring over eleven
categories (markers, enemies — the Lost included, as one swarm entry — your squad,
loot, reinforcements, evac, civilians, hazards, interactables, towers, and
ramps/ladders); the shot HUD with
odds, damage band, target cover and health; free aim for grenades and other
point-targeted abilities, which vanilla offers no keyboard path to at all; the
hacking screen, likewise mouse-only in vanilla; a selected-soldier readout; and
running narration of abilities, damage, deaths, turn handover and concealment.

**Story and transitions** — the tutorial's message surfaces, the Skyranger
briefing on the way to and from a mission, the radio commlink that carries
essentially all of XCOM 2's story dialogue, and the toast and notice stacks.

Several of these turn on keyboard navigation the game already has and simply
never enables for mouse-and-keyboard players; a few others provide keyboard
access the game never had at all.

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

Close the game before either of these. The running game holds the compiled
package open, and the UnrealScript deploy step waits on that lock forever
rather than failing — it looks like a slow compile and is not.

The SDK and game paths default to the development machine's Steam library (`C:/Program Files (x86)/Steam/steamapps/common/`). Override at configure time if your library lives elsewhere:

```bash
cmake -B build -A x64 \
  -DXCOM2_SDK_PATH="<SteamLibrary>/steamapps/common/XCOM 2 War of the Chosen SDK" \
  -DXCOM2_GAME_PATH="<SteamLibrary>/steamapps/common/XCOM 2/XCom2-WarOfTheChosen"
```

### Tests

The navigation kernel is STL-only by contract and compiles standalone, so its
conformance suite needs neither the game nor the SDK:

```bash
ctest --test-dir build -C Release
```

### Development shortcuts

A [`justfile`](justfile) wraps the common loops — `just build`, `just install`,
`just deploy`, `just test`, `just run`, `just kill`, `just log`. Dev builds also
carry a loopback HTTP harness for observing and driving a live session
(`just arm`, then `just dev-status` / `dev-speech` / `dev-gui` / `dev-props`).
It is compiled out of release builds entirely and additionally needs a marker
file to arm; see the dev-harness section of [CLAUDE.md](CLAUDE.md).

## Installation

### Automated (deploy target)

```bash
cmake --build build --target deploy --config Release
```

This copies `X2Access.asi` to the game's `Binaries/Win64/scripts/` directory and deploys the companion mod to `XComGame/Mods/`. You still need to manually install the ASI loader (see below).

### Manual

1. Download [Ultimate ASI Loader x64](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) — get `version-x64.zip`
2. Extract `version.dll` to your XCOM 2 WOTC binary directory:
   ```
   <Steam>/steamapps/common/XCOM 2/XCom2-WarOfTheChosen/Binaries/Win64/
   ```
3. Create a `scripts/` folder inside `Win64/`
4. Copy `X2Access.asi` into `scripts/`

That is the whole install. Prism is linked statically, so there is no speech DLL to place
beside the game — including for NVDA: Prism generates the NVDA RPC client stubs itself and
talks to the reader over RPC, so `nvdaControllerClient.dll` is neither needed nor looked for.

The [X2WOTC Community Highlander](https://steamcommunity.com/workshops/filedetails/?id=1134256495)
is a **required** dependency of the companion mod, at compile time (the mod
builds against its source, pinned as a submodule to the same release the
workshop ships) and at runtime (subscribe to it on the Steam Workshop and
enable it alongside X2AccessCompanion).

### File Layout

```
XCom2-WarOfTheChosen/Binaries/Win64/
  version.dll                  ← Ultimate ASI Loader
  scripts/
    X2Access.asi               ← This mod (Prism statically linked)
```

## Usage

Launch XCOM 2 WOTC normally. You should hear "X2 Access loaded" via your active screen reader or Windows SAPI5.

### Hotkeys

Keys are claimed through the input arbiter, per (key, modifier) chord and per
context: while the mod handles a chord, the game does not see it. Every key
below is live only in the context it is listed under — the tile cursor's arrows
stand down the moment the shot HUD goes up, and so on.

**Everywhere**

| Key         | Action               |
|-------------|----------------------|
| Shift+/ (?) | Read navigation help |

**Menus and screens** — while a screen the mod reads is open

| Key             | Action                                                                     |
|-----------------|----------------------------------------------------------------------------|
| Arrow keys      | Move between items; Left/Right also adjust sliders, spinners and dropdowns |
| Tab / Shift+Tab | Next / previous group of controls on the screen                            |
| Enter, Space    | Activate the focused item                                                  |
| Home / End      | First / last item                                                          |
| PageUp / PageDn | Previous / next tab (settings)                                             |

**Tactical — moving around the map**

| Key             | Action                                                             |
|-----------------|--------------------------------------------------------------------|
| Arrow keys      | Step the tile cursor one tile (world-absolute: Up is always north) |
| Ctrl+, / Ctrl+. | Cursor down / up one floor                                         |
| C               | Recenter the cursor on the selected soldier                        |
| Delete          | Re-announce the current tile                                       |
| Backspace ×2    | Move the selected soldier to the cursor tile                       |
| ;               | Read the selected soldier in full                                  |

**Tactical — the object scanner**

| Key                       | Action                                        |
|---------------------------|-----------------------------------------------|
| B / Shift+B               | Next / previous objective marker              |
| . / Shift+.               | Next / previous enemy                         |
| , / Shift+,               | Next / previous squad member                  |
| PageDn / PageUp           | Next / previous entry in the current category |
| Ctrl+PageDn / Ctrl+PageUp | Next / previous non-empty category            |
| O                         | Re-announce the last entry                    |
| /                         | Put the tile cursor on it                     |
| Shift+Tab                 | Previous soldier (re-provided; see below)     |

**Tactical — aiming a shot** — while the shot HUD is up

| Key    | Action                        |
|--------|-------------------------------|
| Delete | Re-announce the shot          |
| H      | Read the hit-chance breakdown |

**Tactical — free aim** — grenades, cones, lines, evac placement

| Key             | Action                         |
|-----------------|--------------------------------|
| Arrow keys      | Move the aim point             |
| Ctrl+, / Ctrl+. | Aim point down / up one floor  |
| Delete          | Re-announce the aim point      |
| /               | Aim at the scanner's selection |

**Hacking screen**

| Key          | Action               |
|--------------|----------------------|
| Left / Right | Choose a reward card |
| Delete       | Re-announce          |

**The Avenger and the Skyranger**

| Key    | Action                                      |
|--------|---------------------------------------------|
| Delete | Re-announce the focused room / the briefing |

**Geoscape**

| Key                       | Action                                |
|---------------------------|---------------------------------------|
| PageDn / PageUp           | Next / previous entry in the category |
| Ctrl+PageDn / Ctrl+PageUp | Next / previous non-empty category    |
| O                         | Re-announce the selection             |
| Home                      | Measure distances from the selection  |
| C                         | Measure distances from the Avenger    |
| /                         | Point the camera at the selection     |

**Tutorial**

| Key | Action                       |
|-----|------------------------------|
| I   | Re-read the tutorial message |

**About the keys we take.** X2Access never edits your key bindings, and it
never needs to. The engine matches a binding leniently — a bare key fires
whatever modifiers you happen to be holding — so every vanilla action on a key
we claim stays reachable at **Ctrl+key**: the arbiter swallows the bare key, the
Ctrl chord passes through untouched, and the game matches its own binding with
Ctrl held. Bare `Left` drives the tile cursor; `Ctrl+Left` still scrolls the
camera. Tab stays yours in both of its meanings — switching soldiers is narrated
by watching the game rather than by taking the key, which also catches the mouse
click, the turn start and the re-select after a soldier goes down. The one debt
is `Shift+Tab`, which the scanner claims and re-provides, because holding Shift
for a reverse cycle would otherwise trip the game's own prev-soldier binding.

### Logging

Check `scripts/X2Access.log` for initialization details and diagnostics. The
previous three sessions rotate to `X2Access.1/2/3.log`, so relaunching does not
destroy the evidence for whatever you were chasing.

## Third-party licenses

[Prism](https://github.com/ethindp/prism) is **Mozilla Public License 2.0** and is statically
linked into `X2Access.asi`. MPL-2.0 permits that combination and leaves this project's own
sources untouched, but it is file-level copyleft: the source for Prism, including any change
we make to its files, must stay available to anyone who receives the binary. We vendor it
unmodified as the `deps/prism` submodule pinned to a published tag (`v0.17.3`), so the upstream
repository is that source. Prism in turn bundles its own third-party code — see its `NOTICE`
file.

XCOM 2 is a trademark of Take-Two Interactive. This is an unofficial fan project, not
affiliated with or endorsed by Firaxis Games or 2K.
