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
- **Sounds (earcons) are game-native SoundCues, and the package that carries them is a BUILD
  PRODUCT.** The companion's `build.ps1` runs `tools/make_sound_upk.py` over the tracked
  `mods/X2AccessCompanion/Sounds/*.wav` and writes `Content/X2AccessSounds.upk` (gitignored) before
  X2ModBuildCommon stages the mod; `tools/release.ps1` inherits the step because it compiles the
  companion through the same script. ffmpeg must be on PATH — it normalises each WAV to 16-bit PCM
  and encodes the Ogg Vorbis copy the PC audio device actually plays. Each `Foo.wav` becomes
  SoundCue `X2AccessSounds.Foo`, registered under `+SoundCuePaths` in `Config/XComGameData.ini` for
  the sound manager of the layer that plays it, and script plays it as
  `` `SOUNDMGR.PlaySoundEvent("Foo") `` through the Community Highlander's SoundCue mapping (Issue
  #10). Why a writer and not the editor: the SDK's only documented import path is the Content
  Browser's drag-and-drop dialog (`Documentation/Content/XCOM2Mods_Audio.pdf`), a mouse workflow
  with no commandlet behind it, so the package format was decoded from an editor-saved reference
  (MusicModdingSystem's `SoundDropshipIntroCues.upk`) and the writer's docstring is the record of
  it — every constant, the five bulk-data blocks a wave carries, and the `PackageSource`
  checksum (`appStrCrcCaps` of the package name, verified against two packages). ⛔ Cue names
  must not end in `_<digits>`: FName parsing splits such a string when the config path is resolved,
  and the literal name stored in the package never matches; the writer refuses them. ⚠ The
  first build after a WAV changes is slow: X2ModBuildCommon keys its shader-precompile step on
  Content timestamps, which is why the writer leaves an up-to-date package untouched.
  - **A WAV's file name carries its build TAGS** between the stem and the extension
    (`X2Access_Wall_East.loop.right.wav` → cue `X2Access_Wall_East`), so the file says how it is
    built and no manifest can drift from it. `loop` puts a `SoundNodeLooping` between the cue and
    the wave, for a bed script holds in an `AudioComponent` and steers with `AdjustVolume` — never
    for `PlaySoundEvent`, which could not stop it. `left` / `right` / `center` BAKE the pan into the
    samples (mono-summed, then one channel of a silent stereo pair, or mono): a cue played
    unspatialised keeps its channels, and that is the only way a tone gets a world-absolute
    placement, since spatialisation pans relative to the camera, which rotates. The wall tones
    (`X2AccessWallTones`) are the worked example of both.
- Kernel tests (no game needed): `ctest --test-dir build -C Release` (`just test`).
- Loading: Ultimate ASI Loader sits in `Binaries/Win64/` under one of the several host DLL names it
  can impersonate, and **which name it uses differs per machine** — so never test for a
  particular one: a missing `version.dll` is not a broken install (`just status` checked exactly
  that one name and reported the loader MISSING on a perfectly working setup; it now probes the
  whole candidate list and PRINTS which host it found, which is also how you learn what this
  machine is using). The ASI logs to
  `X2Access.log` next to itself, rotating the previous three sessions to `X2Access.1/2/3.log` on
  every start. Timestamps carry milliseconds; screen-transition latency is unreadable without them.

## Release

The rolling `dev` test release on git.alex19ep.me, ported from CyberAccess's `tools/release.ps1`
with the same design and the same guards. `just release` publishes, `just release-dry` does
everything except touch the remote, `just hooks` installs the pre-push hook that runs the release
on every push of main.

- **Rolling, not versioned.** The `dev` tag is force-moved to HEAD every run and the release's
  assets are replaced, so there is exactly one current test build. The notes carry the commit
  SHA and every bug report has to quote it — the tag alone never identifies a build.
- **One archive, both halves.** `X2Access-dev-<sha>.zip` extracts at the WOTC game folder:
  `Binaries/Win64/scripts/X2Access.asi` + `.pdb`, `XComGame/Mods/X2AccessCompanion/` (the SDK's
  compiled staging copy minus `.scripts/`), `README.md`, `LICENSE` and the install check
  `X2Access-Check.cmd` + `.ps1` (from `tools/dist/`) at the root. The Ultimate ASI Loader and the
  workshop Highlander are requirements the notes point at, not bundled.
- **The install check is the tester's first step, not ours.** `tools/dist/X2Access-Check.ps1`
  (Windows PowerShell 5.1, plain ASCII, one finding per line for a screen reader) probes the
  loader host list, the ASI, the companion's four must-have pieces and the Highlander's `.XComMod`
  under every mod root — by mod NAME, because the release and beta listings share it — then
  repeats the companion build script's two self-heals on the user
  config: `ActiveMods` in `XComModOptions.ini` and the local `ModRootDirs` root in
  `XComEngine.ini`, preserving the file's encoding. It refuses to write while `XCom2.exe` runs
  (the game rewrites the list on exit) and with `-CheckOnly`.
  - **A mod is known to the game by the NAME OF ITS `.XComMod` FILE, and a mod root is read
    exactly ONE level down.** The exe carries the literal `/*.XComMod` — a glob run inside a
    candidate folder, which is why the FILE name and not the folder's is the mod's name
    (`ActiveMods=ModConfigMenu` against a workshop folder called `667104300`, whose `Title=` is
    the unrelated "Mod Config Menu"). A workshop folder is a numeric id and a hand-installed one
    keeps whatever name its zip had, so `Find-ModDir` searches every root for the file; a copy
    found two levels down is REPORTED, not accepted, because unzipping an archive that carries
    its own top folder is how a hand install goes quiet. The roots are the local `Mods` folder, every
    Steam library's workshop folder, and every `ModRootDirs` entry in the user `XComEngine.ini`
    and the shipped `DefaultEngine.ini` (which spells it `+ModRootDirs`, and relative to
    `Binaries\Win64`).
  - **Nothing in the check is Steam-specific except where a subscribed mod sits.** The game
    folder is found from the script's own location first, then Steam's libraries, then the GOG
    registry and the Epic manifests; the config path comes from Windows' Documents folder either
    way. A copy from any store installs the mod the same.
  - ⛔ **`Join-Path` resolves the PATH PROVIDER, so it FAILS on a drive that does not exist, and
    under `$ErrorActionPreference = 'Stop'` that ends the script.** The first GOG install the
    check met had a Steam library registered on an `E:` that was gone, and the run died mid-way
    through a Russian-language PowerShell dump — every path this script composes comes from a
    registry key, a `.vdf`, an `.ini` or a user parameter, so ANY of them can name a dead drive.
    `Join-PathText` is the pure-string replacement and there is no bare `Join-Path` left in the
    file. `Test-Path` and `Split-Path` are safe (False, and plain text); `Get-ChildItem
    -LiteralPath` is NOT, and its failure is a parameter-binding one that `-ErrorAction
    SilentlyContinue` does not suppress, so every call is behind a `Test-Path` guard. A
    script-scope `trap` turns whatever gets past all that into three plain sentences and exit
    code 3, because the reader of this output is using a screen reader and a stack dump is not
    an error message.
  - **The Highlander's DLC plugin is a two-way check.** `DLC2CommunityHighlander` is required
    when Alien Hunters is installed (`XComGame\DLC\DLC_2`, which the store sells as the Alien
    Rulers: the DLC's script package is built against the unmodified game the Highlander
    replaces) and its authors call it gamebreaking without that DLC — so the check enables it
    with the Highlander in the first case and reports it as a problem when it is switched on in
    the second. It never switches a mod OFF: the list is the player's. `$DlcHighlanders` is the
    table to extend if a second DLC ever gets a plugin; today that one is the only one. The first field report was the
  companion silently not loading; its output settles that in one line. The ASI points at it too:
  at the first main-menu attach `MenuNavigator` asks `GameUI::CompanionPackageLoaded` once and,
  when the package is absent, speaks `Vocab::CompanionNotLoaded` after the menu's own line —
  and from that moment on `FindCompanionCDO` caches an absence for 30 s, so a session without
  the companion logs one warning per interval instead of one per second per polling surface.
  ⛔ Not before it: the package loads with the shell, later than DLC enumeration, and the first
  cut cached a "not loaded" seen at ten seconds on a HEALTHY install, muting every companion
  lookup until twenty seconds after the main menu.
- **Built from a parked worktree, never the live tree.** Several sessions work this tree at once,
  so the archive is built from `release-stage/src`, a detached worktree at the published commit
  (submodules `deps/prism` and `deps/X2ModBuildCommon` only — the Highlander and the SDK generator
  never reach the archive). `-FromWorkingTree` exists and refuses when any build input is dirty.
- **The generated SDK is borrowed.** A clean checkout has no `generated_sdk/`, so the release tree
  configures with `-DX2ACCESS_SDK_DIR=<main tree>/generated_sdk/X2SDK` — the same SDK the layout
  asserts pass against.
- ⛔ **Devserver OFF, and proven on the binary.** The release tree is configured
  `-DX2ACCESS_DEVSERVER=OFF`; `Assert-NoDevServer` then scans the built ASI for three literals
  that live only in `src/dev_server.cpp` (`X2Access.devserver`, `/flashqueue`, `pump-age-ms`) and
  refuses to package on a hit. Move one of those strings out of that TU and the check goes blind —
  update the list with it.
- **The PDB ships inside the archive.** `CMakeLists.txt` emits it in every config (`/Zi`, `/DEBUG`,
  with `/OPT:REF /OPT:ICF` restored outside Debug), and the script fails without it: a rolling tag
  replaces assets, so a PDB published separately is gone the moment a tester still on last week's
  build sends a dump.
- ⛔ **Refuses while `XCom2.exe` runs** — the companion deploy is the `build_uscript` hang above,
  and a release that looks like a slow compile is worse than one that says why it stopped.
- **Kernel tests gate it.** `GraphTests` is built and `ctest` run in the release tree before
  packaging.
- **Credentials**: `FORGE_TOKEN` in `.env.rc` (untracked, separate from `.env` because the
  justfile exports `.env` into every recipe's environment, the launched game's included). The token
  needs the repository permission at Read and Write; the script checks `/repos/<owner>/<repo>`
  for `permissions.push` rather than `/user`, which a repository-scoped token answers 403.
- **The hook.** `tools/hooks/pre-push` runs the script with `-SkipMainPush` whenever main goes to
  origin, and BLOCKS the push until the release succeeds. Re-entry is guarded twice
  (`X2ACCESS_RELEASING` plus `--no-verify` on the script's own pushes). It pushes only the tag and
  lets git push main itself, because git fixed main's expected old value before the hook ran and
  would reject its own push otherwise. `git push --no-verify` lands commits without a release.
- The last four archives are kept under `release-stage/` (gitignored) so a tester's exact bits and
  matching PDB can still be produced after the tag has moved on.

## Dependencies

- `deps/prism` (submodule, pinned to `v0.18.2`) — speech output, linked static, ⛔ **MPL-2.0, not
  MIT** (file-level copyleft: we vendor it unmodified and the README says where the source is). Its
  deps are bundled under its own `third_party/`, so the build is offline; it needs the Windows SDK's
  `midl.exe` to generate the NVDA RPC client stubs, which is why no `nvdaControllerClient.dll` ships
  beside the game. Prism's error return from `speak` and its debounced availability poll both route
  to `TTS::RebindBackend()`, so a reader that DIED under us (NVDA exited; its RPC endpoint is gone
  and Prism never reconnects an existing instance) costs one line instead of the session.
  ⛔ **`TTS::Shutdown` deliberately does NOT call `prism_shutdown()` or `prism_log_shutdown()`.**
  `ShutdownAll()` runs from `DllMain(DLL_PROCESS_DETACH)` under the LOADER LOCK, and at process exit
  the other threads are already dead, so a join never returns. The first call deletes the context,
  which joins the `BackendEnumerator` jthread, then calls `CoUninitialize` and a run of
  `__FUnloadDelayLoadedDLL2` (i.e. `FreeLibrary`); the second joins the logging drain. Both are
  textbook loader-lock deadlocks. Shutdown does the safe subset instead: clear the log handler so
  nothing can re-enter this module after `Log::Shutdown`, `prism_backend_stop` so a player quitting
  does not hear the rest of the sentence, then LEAK the context on purpose. The dev server's bounded
  `Stop()` (`dev-harness.md`) follows the same reasoning.
- `deps/X2ModBuildCommon` (submodule) — UnrealScript build pipeline.
- **No hooking library, on purpose.** The two interception points ship without one — the
  ProcessEvent hook is a vtable pointer swap and input is a WNDPROC subclass. If an inline hook on a
  non-virtual native is ever needed (`ProcessInternal` is the candidate, `engine-notes.md`), use
  **MinHook** (small, CMake-friendly, the game-modding standard), not MS Detours, and do not vendor a
  runtime assembler (AsmJit / Xbyak) — MinHook generates its own trampolines. The game ships
  Firaxis' own `EasyHook64.dll` but nothing loads it; it is vestigial and not for us.
- `deps/X2WOTCCommunityHighlander` (submodule, pinned to `v1.31.2-beta`) — pure UnrealScript, no
  native code. Compile-time include for the companion and the source of narration event hooks;
  declared as a runtime requirement in the README, the mod's ReadMe.txt and the .XComMod description.
  The pin is chosen by DIFFING, not by trusting a tag: the workshop build installed here ships its
  full `Src/`, and the pin is the tag whose tree matches it with ZERO differing files. When bumping
  the pin, re-run that check — a workshop build can lag master by months, and compiling against a
  newer CHL than the game loads means layouts and hooks that don't exist at runtime:
  ```sh
  git -C deps/X2WOTCCommunityHighlander archive <tag> X2WOTCCommunityHighlander/Src | tar -x -C <tmp>
  diff -r --strip-trailing-cr <tmp>/X2WOTCCommunityHighlander/Src \
      "<steamapps>/workshop/content/268500/<id>/Src"
  ```
  ⛔ `--strip-trailing-cr` is not optional: the workshop files are CRLF and git's blobs are LF, so a
  plain `diff` reports every shared file as differing and the check silently proves nothing.
  - ⛔ **WHICH workshop id is a per-machine fact — probe, never hardcode.** The stable (1134256495)
    and the beta (1796402257) publish the SAME package name from different folders, so
    `mods/X2AccessCompanion/.scripts/build.ps1` probes the candidate list, prints which it found,
    and REFUSES if both are installed (the game cannot load both either). `XCOM2_HIGHLANDER_ID`
    pins one. Hardcoding an id is what broke the build the day the author switched subscriptions
    (2026-09-16, mid-session: the stable's folder vanished between two builds and the error named a
    subscribe step that had already been done).
  - **Switched stable → beta on 2026-09-16** (`v1.31.0-release` → `v1.31.2-beta`, workshop
    1796402257; the beta tree matches that workshop item with zero differing files). The offset
    surface was checked before the switch was accepted, since the Highlander's replacement packages
    are what every class layout comes from: of the 139 classes `sdk_layout_checks.cpp` pins, **47
    are Highlander-patched, and NOT ONE has a different `var` block between the two builds.**
    `UIEventQueue` is newly patched by the beta but its var block is identical to vanilla's. Across
    everything the beta adds, exactly one real layout change exists — `XComShell` gains
    `bIsMusicPlaying` — and `XComShell` is not read by offset anywhere in `src/`. The two
    photobooth cameras look changed and are not: the beta only drops their `private` keyword, same
    types in the same order.
    ⚠ That is a SOURCE-level comparison and it is not the authoritative one. The authority is a
    regenerated dump under the beta plus a native rebuild, where the layout asserts turn any miss
    into a build error — the asserts pin against the CURRENT generated SDK, which is still the
    stable's dump, so rebuilding native today proves nothing about the beta (`docs/next-steps.md`).
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
  generator fixes below. The generic fixes are separable and are upstream-PR candidates.
  - **Three settings in `Engine/XCOM2/Configuration.cpp` must stay FALSE**, each having cost real
    debugging. `m_useConstants`: with it on, all 29388 wrappers resolved their `UFunction*` from the
    owning CLASS's index — the same constant `StaticClass()` uses — so every one handed
    `ProcessEvent` a `UClass`; off, they emit `UFunction::FindFunction("Function Pkg.Class.Name")`,
    which also survives a game patch. `m_removeNativeFlags`: clearing `FUNC_Native` around the call
    is actively wrong here, because a native's `Script` array is a 2-byte STUB, not a body
    (`XComEngine.IsAnyMoviePlaying` has `Script.Count` 2 against 1663 for
    `UIPauseMenu.OnChildClicked`), so clearing it runs the stub and returns a ZEROED param block
    indistinguishable from a real answer. `m_removeNativeIndex`: same family. With those off there
    is no `SdkConstants.hpp` any more — it existed only to feed the index-based wrappers — and the
    GNames/GObjects offsets we use live in `GameDefines.hpp`.
  - **The generator is patched to emit valid C++ rather than leaving it to a repair pass**: `.` `<`
    `>` `~` and space added to `m_unsafeChars` (UnrealScript names really do carry dots —
    `OverriddenTangentVector.X`); `UniquifyFunctionNames()` applied in all three emission passes
    (one class can declare the same function once per STATE — `XComInputBase` declares `Key` ten
    times — which collapsed into ten identical declarations and ten identical `_Params` structs);
    and the `GameDefines.hpp` `#pragma pack(4)` region emitted automatically. That last one matters:
    every `SDK_HEADERS/*.hpp` opens its own pack region and GameDefines did not, so its types
    (FNameEntry, TArray, FName, FString) silently took the including TU's pack, and mixed packs
    across TUs is an ODR violation that moved `FNameEntry::Name` from 0x14 to 0x18 and broke GNames
    lookup entirely. `sdk_layout_checks.cpp` pins the result either way.
  - `tools/fix_generated_sdk.py` repairs exactly one thing (undefined suffixed enum references) and
    doubles as the regression check: any other line in its output means a generator fix broke.
  It produced
  `generated_sdk/X2SDK/`, which IS a build input but is **not in git** (113 MB, `.gitignore`d): if
  that folder is lost the build dies and the only recovery is a regeneration run. `just sdk-build` /
  `sdk-gen` / `sdk-adopt` / `sdk-revert` drive it; the generator dumps from a LIVE process, so
  `sdk-gen` injects into the running game with Frida. ⛔ Do NOT drop it into
  `Binaries/Win64/scripts/` as a second `.asi` — the ASI loader scans subdirectories, so two plugins
  load and two copies of X2Access patching 1610 vtables each kills the game on launch.

