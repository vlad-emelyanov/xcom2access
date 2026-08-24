# Build, deploy and dependencies

How the two layers are configured, built, deployed and loaded, and what the tree depends on. The
rules extracted from all this — the mandatory `-review` launch flag, the `/MT` runtime pin, the
`build_uscript` hang, the three generator settings, keep-every-path-overridable — stay in
`CLAUDE.md`; this file is the operational detail behind them.

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

