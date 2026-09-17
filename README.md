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

## Status

**Pre-alpha.** Under active development and not ready for general use: coverage
grows screen by screen, keys can change without notice, and screens that already
work may break as neighbouring ones land. Expect rough edges, and back up your
saves.

Test builds are published as a **rolling `dev` release** — the tag always
points at the newest build, so the same download link gives you the current
one. Bug reports should name the commit the release notes quote, since the tag
alone never will.

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
edge, hazards, height changes, fog parity, reachability, and a two-press move-to that
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

## Requirements

- XCOM 2: **War of the Chosen** on PC (Windows), the current Steam build. The
  base game without the expansion is not supported: the companion is a WOTC
  mod and the native side is measured against the WOTC executable.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)
  — the `version-x64.zip` build. This is what loads the mod into the game;
  without it nothing speaks, and nothing in the game changes at all.
- The [X2WOTC Community Highlander](https://steamcommunity.com/workshop/filedetails/?id=1134256495)
  from the Steam Workshop, subscribed and enabled. The companion mod is
  compiled against it and does not load without it. The native side runs
  either way, so with the Highlander missing the menus still read but
  **combat narration and every screen whose words come from the companion go
  quiet** — it looks like a partial mod, not a broken one.
- A screen reader (NVDA, JAWS, ZDSR, ZoomText, SenseReader, PC-Talker,
  BoyPCReader), or SAPI / Windows OneCore as the fallback. Prism is linked
  statically, so there is no speech DLL to place beside the game — including
  for NVDA: Prism generates the NVDA RPC client stubs itself, so
  `nvdaControllerClient.dll` is neither needed nor looked for.

## Installing

Download the archive from the
[`dev` release](https://git.alex19ep.me/alex19ep/xcom2access/releases) and
extract it at the **War of the Chosen game folder** — `XCom2-WarOfTheChosen`,
the one holding `Binaries/` and `XComGame/`, under `steamapps/common/XCOM 2`.
It contains:

```
README.md
LICENSE
X2Access-Check.cmd, X2Access-Check.ps1     (the install check: run it after extracting)
Binaries/Win64/scripts/X2Access.asi        (the native plugin, Prism statically linked)
Binaries/Win64/scripts/X2Access.pdb        (symbols, so a crash dump can be read)
XComGame/Mods/X2AccessCompanion/           (the companion mod: .XComMod, Config, Localization, Script)
```

`Binaries/Win64/scripts/` is where the ASI loader looks for plugins, and
`XComGame/Mods/` is where the game's launcher looks for local mods; neither
is a choice. Both halves must land — the ASI reads screens, the companion
supplies the words for combat and for every screen whose text the game keeps
only in Flash, and one without the other is a mod that goes quiet in places,
not a mod that fails.

Then:

1. Put the ASI loader's `version.dll` in `Binaries/Win64/`, next to
   `XCom2.exe`. The loader can impersonate several system DLL names;
   `version.dll` is the one its x64 package ships as, and any of the others
   works too.
2. With the game closed, run `X2Access-Check.cmd` from the game folder. It
   checks that the loader, the ASI, the companion and the Highlander are all
   in place, names anything missing and how to fix it, and enables
   **X2AccessCompanion** and the **Community Highlander** in the game's mod
   settings the way the launcher does (`Documents\My Games\XCOM2 War of the
   Chosen\XComGame\Config\XComModOptions.ini`). Its output is plain text, one
   finding per line, each starting with OK, FIXED, PROBLEM or NOTE. If you
   launch through the Alternative Mod Launcher, tick both mods there as well:
   it rewrites that list from its own selection on every launch. The script
   lives at `tools/dist/` in this repository.
3. Launch through Steam or the mod launcher as usual. If you start
   `XCom2.exe` directly instead, add `-review` to its arguments: the launchers
   pass it for you, and without it the game boots its developer shell instead
   of the retail main menu, where nothing reads.

## Usage

You should hear "X2 Access loaded" through your screen reader or SAPI as the
game comes up.

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
| Alt             | Targets from the cursor tile — the game's target preview, spoken (the game keeps the key too) |
| ;               | Read the selected soldier in full                                  |
| E / Q           | Next / previous soldier (the game's own switch, moved here from Tab / Shift; also next / previous target while aiming) |

Every step also says who would see a soldier on that tile ("Seen by 2") and whether its cover
holds against them ("flanked"), the way the game draws the eye markers and the red cover shield
on every hover. With the game's own "Show Target Preview" option on, the Alt readout rides every
step as well.

**Tactical — the HUD layer** — Tab moves the arrows from the map onto the HUD's panels

| Key             | Action                                                                                   |
|-----------------|------------------------------------------------------------------------------------------|
| Tab             | Enter the HUD layer (from the map)                                                       |
| Tab / Shift+Tab | Next / previous panel: Soldier, Abilities, Commands, Targets, Objectives                 |
| Arrow keys      | Walk a panel (Left/Right along the ability bar, the command strip and the target strip)  |
| Enter           | Use the ability, press the command, or aim at the target                                 |
| Escape          | Back to the map                                                                          |

The Targets panel is the row of enemy heads: best shot first, each with its hit chance, cover or
flanked, health, squadsight, whether the selected ability can reach it, its buffs and debuffs,
and how far away it is.

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
| Tab    | Enter the Avenger HUD layer (Escape leaves) |
| Delete | Re-announce the focused room / the briefing |
| E / Q  | Next / previous soldier on the Armory, Customize and Bond screens (the game's own cycle, moved here from Tab / Shift) |

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

**About the keys we take.** The engine matches a binding leniently — a bare key
fires whatever modifiers you happen to be holding — so every vanilla action on a
key we claim stays reachable at **Ctrl+key**: the arbiter swallows the bare key,
the Ctrl chord passes through untouched, and the game matches its own binding
with Ctrl held. Bare `Left` drives the tile cursor; `Ctrl+Left` still scrolls the
camera.

**The one binding change we ship.** The companion mod carries an `XComInput.ini`
that moves the game's own *next unit* and *previous unit* commands from Tab and
Left Shift onto **E** and **Q**, in tactical and on the Avenger alike. Q and E
were camera rotation, which a cursor that always steps north has no use for.
Everything the game did on Tab it now does on E — next soldier, next target while
aiming, next soldier in the Armory — and its own key hints say so. Switching is
narrated by watching the game rather than by taking the key, which also catches
the mouse click, the turn start and the re-select after a soldier goes down.
Because Left Shift no longer carries a binding, Shift is a plain modifier for
every reverse-cycle chord above, and bare Tab is free to be the HUD layer's key.
The game's Controls screen shows the new bindings and will save them into your
profile if you press Save there; they then persist even without the mod, and the
Controls screen is where to put them back.

### Logging

Check `scripts/X2Access.log` for initialization details and diagnostics. The
previous three sessions rotate to `X2Access.1/2/3.log`, so relaunching does not
destroy the evidence for whatever you were chasing.

## Building from source

### Prerequisites

- CMake 3.25+
- Visual Studio 2019+ with C++ desktop workload (MSVC x64)
- XCOM 2 War of the Chosen SDK (install from Steam)
- Git submodules initialized: `git submodule update --init --recursive`
  (this includes
  [X2WOTCCommunityHighlander](https://github.com/X2CommunityCore/X2WOTCCommunityHighlander)
  at `deps/X2WOTCCommunityHighlander`, pinned to `v1.31.0-release` — the same
  build as the workshop release the game loads; the companion mod compiles
  against the workshop copy's source)
- The generated SDK under `generated_sdk/`, which is not in git: it is dumped
  from the live game, and `docs/build.md` has the four-step recipe.

### Build steps

```bash
cmake -B build -A x64
cmake --build build --config Release
```

Output: `build/bin/Release/X2Access.asi`

### UnrealScript companion mod

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

The `deploy` target copies `X2Access.asi` to the game's `Binaries/Win64/scripts/`
and the companion to `XComGame/Mods/`; the ASI loader is still installed by
hand, as above.

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
file to arm; see [`docs/dev-harness.md`](docs/dev-harness.md).

### Cutting a test release

`just release` builds both halves from a clean checkout of HEAD with the dev
harness compiled out, proves that on the binary, and publishes the rolling
`dev` release; `just release-dry` stops before the upload. `just hooks`
installs the pre-push hook that does this on every push of main. Details and
the credentials it needs are in [`docs/build.md`](docs/build.md).

## License

MIT — see [LICENSE](LICENSE).

[Prism](https://github.com/ethindp/prism) is **Mozilla Public License 2.0** and is statically
linked into `X2Access.asi`. MPL-2.0 permits that combination and leaves this project's own
sources under MIT, but it is file-level copyleft: the source for Prism, including any change
we make to its files, must stay available to anyone who receives the binary. We vendor it
unmodified as the `deps/prism` submodule pinned to a published tag (`v0.17.3`), so the upstream
repository is that source. Prism in turn bundles its own third-party code — see its `NOTICE`
file.

The companion mod is built with
[X2ModBuildCommon](https://github.com/X2CommunityCore/X2ModBuildCommon) (MIT) against the
[X2WOTC Community Highlander](https://github.com/X2CommunityCore/X2WOTCCommunityHighlander)
(MIT); neither is bundled in the archive.

XCOM 2 is a trademark of Take-Two Interactive. This is an unofficial fan project, not
affiliated with or endorsed by Firaxis Games or 2K.
