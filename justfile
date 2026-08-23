# X2Access — build / deploy / run helpers.
# `just` on its own lists every recipe. The one-shot dev loop is `just go`.

set windows-shell := ["pwsh", "-NoProfile", "-NonInteractive", "-Command"]

# Per-machine paths come from an untracked `.env` beside this file (copy `env.example`), so
# the tree carries the generic Steam layout as its default and nobody's D: drive gets baked
# in. Precedence, lowest first: the literal below, XCOM2_*_PATH from `.env` or the real
# environment, then an explicit `just game_path=... <recipe>`.
set dotenv-load := true

# Override any of these per-invocation, e.g. `just config=Debug build`
game_path := env_var_or_default("XCOM2_GAME_PATH", "C:/Program Files (x86)/Steam/steamapps/common/XCOM 2/XCom2-WarOfTheChosen")
sdk_path := env_var_or_default("XCOM2_SDK_PATH", "C:/Program Files (x86)/Steam/steamapps/common/XCOM 2 War of the Chosen SDK")
build_dir := "build"
config := "Release"

# The loopback HTTP dev harness. ON for the local tree (it is still inert without the marker
# file — see `just arm`); pass devserver=OFF when configuring a build meant for players.
devserver := "ON"

# -review is mandatory: without it the game boots the developer shell (UIShell)
# instead of the retail main menu (UIFinalShell) and every UIFinalShell recipe finds nothing.
launch_flags := "-review -noRedScreens -noStartUpMovies"

win64 := game_path / "Binaries/Win64"
asi_log := win64 / "scripts/X2Access.log"
# Launch.log sits under the REAL Documents folder, which is not "$HOME/Documents" on a machine
# where OneDrive has redirected it (and is not even spelled in English there — this one resolves
# to C:\Users\<user>\OneDrive\Документы). So `gamelog` asks Windows for the folder instead of
# carrying a path baked in on whatever PC last edited this file; src/dev_server.cpp's /exec
# already resolves it the same way, through SHGetKnownFolderPath(FOLDERID_Documents).
game_log_rel := "My Games/XCOM2 War of the Chosen/XComGame/Logs/Launch.log"

_default:
    @just --list --unsorted

# Fast dev loop: stop the game, rebuild + install the ASI, relaunch. No SDK needed.
go: kill install run

# Full dev loop, including the UnrealScript companion (needs a working SDK mod-compile).
go-full: kill deploy run

# Configure the CMake build tree (x64, forwards the game/SDK paths).
configure:
    cmake -B {{ build_dir }} -A x64 -DXCOM2_GAME_PATH="{{ game_path }}" -DXCOM2_SDK_PATH="{{ sdk_path }}" -DX2ACCESS_DEVSERVER={{ devserver }}

[private]
configured:
    @if (-not (Test-Path "{{ build_dir }}/CMakeCache.txt")) { {{ just_executable() }} configure }

# Build the C++ ASI plugin only.
build: configured
    cmake --build {{ build_dir }} --config {{ config }} --target X2Access

# Compile the UnrealScript companion mod (self-deploys to XComGame/Mods).
uscript: configured
    cmake --build {{ build_dir }} --config {{ config }} --target build_uscript

# Install the freshly built ASI into the game. ASI-only changes never need the SDK.
install: build
    @if (-not (Test-Path "{{ win64 }}/scripts")) { New-Item -ItemType Directory -Force -Path "{{ win64 }}/scripts" | Out-Null; Write-Output "created {{ win64 }}/scripts" }
    @Copy-Item -Path "{{ build_dir }}/bin/{{ config }}/X2Access.asi" -Destination "{{ win64 }}/scripts/X2Access.asi" -Force
    @Write-Output "installed X2Access.asi -> {{ win64 }}/scripts"

# The UnrealScript half of this currently fails: the WOTC SDK's `make -mods` crashes on
# 'MetaData WinDrv.MetaData'. Use `install` for ASI-only work until that is fixed.
[doc("Build and install BOTH layers (ASI + companion mod). UScript half is broken, see comment.")]
deploy: configured
    cmake --build {{ build_dir }} --config {{ config }} --target deploy

# Run the Graph kernel conformance suite (no game needed).
test: configured
    cmake --build {{ build_dir }} --config {{ config }} --target GraphTests
    ctest --test-dir {{ build_dir }} -C {{ config }} --output-on-failure

# Launch XCOM 2 WOTC with the flags the mod needs.
run:
    @if (-not (Test-Path "{{ win64 }}/XCom2.exe")) { throw "XCom2.exe not found under {{ win64 }}" }
    @Start-Process -FilePath "{{ win64 }}/XCom2.exe" -ArgumentList "{{ launch_flags }}" -WorkingDirectory "{{ win64 }}"
    @Write-Output "Launched XCom2.exe {{ launch_flags }}"

# Stop a running XCOM 2 (needed before deploy — the game locks X2Access.asi).
kill:
    @if (Get-Process XCom2 -ErrorAction Ignore) { Stop-Process -Name XCom2 -Force; Write-Output "XCom2 stopped" } else { Write-Output "XCom2 not running" }

# Last N lines of the ASI log (default 50).
log lines="50":
    @if (Test-Path "{{ asi_log }}") { Get-Content -Path "{{ asi_log }}" -Tail {{ lines }} } else { Write-Output "no ASI log yet at {{ asi_log }}" }

# Follow the ASI log live (Ctrl+C to stop).
logf:
    @Get-Content -Path "{{ asi_log }}" -Tail 20 -Wait

# Last N lines of the game's own Launch.log (default 50).
gamelog lines="50":
    @$log = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "{{ game_log_rel }}"; if (Test-Path $log) { Get-Content -Path $log -Tail {{ lines }} } else { Write-Output "no Launch.log yet at $log" }

# Show what is currently installed in the game and whether it is running.
status:
    @$items = @{ "ASI" = "{{ win64 }}/scripts/X2Access.asi"; "Companion mod" = "{{ game_path }}/XComGame/Mods/X2AccessCompanion"; "Generated SDK" = "{{ justfile_directory() }}/generated_sdk/X2SDK/GameDefines.cpp" }; foreach ($k in $items.Keys) { $p = $items[$k]; if (Test-Path $p) { Write-Output ("{0}: installed, {1}" -f $k, (Get-Item $p).LastWriteTime) } else { Write-Output ("{0}: MISSING ({1})" -f $k, $p) } }; $loaderHosts = @("version.dll","version-x64.dll","dinput8.dll","winmm.dll","dsound.dll","d3d9.dll","d3d11.dll","dxgi.dll","xinput1_3.dll","wininet.dll"); $found = @(); foreach ($h in $loaderHosts) { $p = Join-Path "{{ win64 }}" $h; if (Test-Path $p) { $found += ("{0} ({1})" -f $h, (Get-Item $p).LastWriteTime) } }; if ($found.Count -gt 0) { Write-Output ("ASI loader: found " + ($found -join ", ")) } else { Write-Output "ASI loader: MISSING (no known Ultimate ASI Loader host DLL in {{ win64 }})" }; if (Get-Process XCom2 -ErrorAction Ignore) { Write-Output "XCom2: running" } else { Write-Output "XCom2: not running" }

# ---- dev harness (loopback HTTP, port 8774) ---------------------------------
#
# Two gates. The build gate is `devserver` above (compiled in by default here, out of a
# player build). The RUNTIME gate is a marker file next to the .asi, so an armed build is
# still silent until you say so — and so arming survives a Steam relaunch, which an
# environment variable would not.
#
#   just arm && just go      # arm, rebuild, relaunch — the harness comes up with the game
#   just dev-status          # what is on screen right now
#   just dev-gui             # the topmost screen's widget tree
#   just dev-speech          # everything the mod has spoken this session
#
# /health, /speech and /log answer off the game thread on purpose: when the game is wedged
# they still respond, and /health's pump age tells you it is wedged.

devserver_port := "8774"
marker := win64 / "scripts/X2Access.devserver"
# NOT bare `curl`: PATH here resolves to MSYS2's cygwin curl, whose path translation mangles
# URL and @file arguments into blanks. Same trap the CyberAccess harness scripts hit.
curl := env_var_or_default("SystemRoot", "C:/Windows") / "System32/curl.exe"

# Arm the dev harness (marker file next to the .asi). Takes effect on the next game start.
arm:
    @if (-not (Test-Path "{{ win64 }}/scripts")) { throw "no scripts dir at {{ win64 }}/scripts - run 'just install' first" }
    @Set-Content -Path "{{ marker }}" -Value "" -NoNewline
    @Write-Output "armed: {{ marker }} (restart the game to pick it up)"

# Disarm the dev harness.
disarm:
    @if (Test-Path "{{ marker }}") { Remove-Item "{{ marker }}" -Force; Write-Output "disarmed: {{ marker }}" } else { Write-Output "already disarmed" }

# GET any harness endpoint: `just dev status`, `just dev 'speech?since=12'`, `just dev health`.
dev endpoint="health":
    @& "{{ curl }}" -s --max-time 20 "http://127.0.0.1:{{ devserver_port }}/{{ endpoint }}"

# Liveness + build stamp + how long ago the game thread last pumped.
dev-health: (dev "health")

# Everything the mod has spoken this session, cursor-paged.
dev-speech since="0": (dev "speech?since=" + since)

# The ASI log ring over HTTP (cursor-paged, unlike `just log` which tails the file).
dev-log tail="50": (dev "log?tail=" + tail)

# Screen stack, gates, navigator and tile-reader state. Runs on the game thread.
dev-status: (dev "status")

# Run a console command and get back whatever it wrote to the game's Launch.log.
# All 1246 SDK exec functions are reachable, cheat managers included: `just dev-exec "StartLadder 0 1"`.
dev-exec cmd:
    @& "{{ curl }}" -s --max-time 30 --data-binary "{{ cmd }}" "http://127.0.0.1:{{ devserver_port }}/exec"

# Synthetic keys. Default tier is SendInput (drives the WHOLE game, needs the window foreground);
# mode=kernel injects into our own arbiter queue instead, which drives ONLY our layer and works
# with the game backgrounded: `just dev-input Left,Left,Delete kernel`.
dev-input keys mode="send":
    @& "{{ curl }}" -s --max-time 30 "http://127.0.0.1:{{ devserver_port }}/input?keys={{ keys }}&mode={{ mode }}"

# The widget tree of a screen: class, address, state bits and the text each panel carries.
# Default target is the topmost screen; `just dev-gui 10 "screen=UIPauseMenu"` picks another,
# and `instance=` / `ptr=0x...` reach objects the screen stack never holds.
dev-gui depth="8" select="":
    @& "{{ curl }}" -s --max-time 30 "http://127.0.0.1:{{ devserver_port }}/gui?depth={{ depth }}&{{ select }}"

# One object's properties with their LIVE offsets, types and values — the replacement for
# reading SrcOrig and transcribing offsets out of the SDK dump. Same selector as dev-gui:
# `just dev-props "screen=UIShellDifficulty" SecondWave`, `just dev-props "ptr=0x000002..."`.
dev-props select="" filter="":
    @& "{{ curl }}" -s --max-time 30 "http://127.0.0.1:{{ devserver_port }}/props?{{ select }}&filter={{ filter }}"

# Invoke a script function on a live object — the third verb on the dev-gui selector. Built for
# the controller-gated function: code the game already has and only calls from a branch a
# mouse+keyboard player never takes, e.g.
# `just dev-call InitializeNavigationGrid "screen=UIFacilityGrid"` to ungate the ant farm's
# room adjacency graph. Refuses object/struct/array arguments (the only value it could pass is a
# zero, i.e. a null deref inside script) and prints the signature it refused, so a wrong guess
# costs a line instead of the session.
dev-call fn select="":
    @& "{{ curl }}" -s --max-time 30 "http://127.0.0.1:{{ devserver_port }}/call?fn={{ fn }}&{{ select }}"

# The same verb WITH arguments, url-encoded — which is how the Scaleform side is read, since
# everything about a live movie goes through GFxMoviePlayer's path-string API:
#   just dev-flash GetVariableString "theInputDialogueScreen.inputField.text"
#   just dev-flash ActionScriptInt "_global.Selection.getCaretIndex"
# The movie is the default selector because UIMovie_2D is where the 2D screens live; override it
# to reach another one.
dev-flash fn arg select="instance=UIMovie_2D":
    @& "{{ curl }}" -s --max-time 30 -G --data-urlencode "arg={{ arg }}" "http://127.0.0.1:{{ devserver_port }}/call?fn={{ fn }}&{{ select }}"

# The same read, PANEL-RELATIVE — the harness verb for "what did the game actually put on
# screen?". It resolves the selected panel's own MCPath and reads <MCPath>.<field>, so a screen
# that pipes its text into Flash and retains none of it (the tactical HUD, the save rows, the
# squad cards — everything /gui prints as wordless) can still be checked against our readout:
#   just dev-field "screen=UITacticalHUD"                     # does its MCPath resolve at all?
#   just dev-field "ptr=0x000002..." "hitChanceText.text"     # a child field, one level down
#   just dev-field "screen=UISaveGame" "someFlag" GetVariableBool
# ⛔ Verification only. UIMCController.uc calls these getters "an immediate and blocking
# operation … very expensive, use sparingly" and the shipped game uses one exactly twice; nothing
# in the speech path may read this way.
#
# Read a Flash field of the selected panel: what the game actually put on screen.
dev-field select="" field="" fn="":
    @& "{{ curl }}" -s --max-time 30 -G --data-urlencode "field={{ field }}" "http://127.0.0.1:{{ devserver_port }}/flash?fn={{ fn }}&{{ select }}"

# What panels PUSHED into Scaleform, captured in flight — the other half of dev-field, and the
# one that is not verification-only. dev-field asks Flash what it currently HOLDS (blocking, and
# blind to any XComScrollingTextField, whose text sits behind an AS2 getter GFx cannot run); this
# reads the words on their way OUT, at UIMovie.CopyCommandQueue, before the queue is cleared.
#
# For every screen in the retains-nothing family — UIAlert's 118 variants, every list row, all of
# UITacticalHUD — this is where the text actually is:
#   just dev-flashqueue           # every panel that has pushed words, with its cache index
#   just dev-flashqueue 42        # just that cache index, all recorded calls in order
# Answers off the game thread, so it still works when the game thread is the broken thing.
dev-flashqueue index="":
    @& "{{ curl }}" -s --max-time 30 "http://127.0.0.1:{{ devserver_port }}/flashqueue?index={{ index }}"

# PNG of the monitor the game is on (Desktop Duplication). Defaults beside the .asi.
dev-shot path="":
    @& "{{ curl }}" -s --max-time 60 "http://127.0.0.1:{{ devserver_port }}/screenshot?path={{ path }}"

# ---- SDK regeneration -------------------------------------------------------
#
# The generated SDK is NOT in git (114 MB, .gitignore'd), so regenerating is the only way to
# recover it and the only way to pick up a game patch. Three steps, because the generator is a
# DLL that dumps from a LIVE process:
#
#   just sdk-build      # only after editing the generator's Configuration.cpp
#   just run            # start the game, sit at the main menu
#   just sdk-gen        # inject; wait for the generator's message box, then quit the game
#   just sdk-adopt      # repair, swap in, and prove it with the layout asserts
#
# Injection goes through Frida rather than dropping a second .asi into scripts/: the ASI loader
# scans SUBDIRECTORIES too, so an extra plugin loads alongside ours, and two copies of X2Access
# patching 1610 vtables each takes the game down on launch.
#
# In the generator's Engine/XCOM2/Configuration.cpp, m_useConstants, m_removeNativeFlags and
# m_removeNativeIndex must all stay FALSE — see the hard rule in CLAUDE.md for what each one
# breaks.

gen_dir := justfile_directory() / "deps/CodeRed-Generator"
gen_dll := gen_dir / "x64/Release/CodeRedGenerator.dll"
# MSBuild and the C++ toolset are LOOKED UP rather than written down. Two things move between
# machines and a hardcoded MSBuild path sees neither: which Visual Studio is installed (this
# tree has been built under VS 2022 and under VS Build Tools 2026, whose MSBuild lives under
# .../Microsoft Visual Studio/18/BuildTools), and which PlatformToolset ships with it. The
# generator's .vcxproj asks for v143, and MSBuild does not fall back when only a newer toolset
# is present — it fails with "the build tools for v143 cannot be found", which reads like a
# broken project rather than a missing component. So: use the project's own toolset when it IS
# installed, else the newest one that is. vswhere's location is fixed by Microsoft's contract,
# which is what makes it the one path here worth hardcoding.
vswhere := "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"

# Compile the SDK generator DLL (needed only after changing its Configuration.cpp).
sdk-build:
    @$ErrorActionPreference = 'Stop'; if (-not (Test-Path "{{ vswhere }}")) { throw "vswhere not found at {{ vswhere }} - install Visual Studio Build Tools with the C++ workload" }; $vs = & "{{ vswhere }}" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath; if (-not $vs) { throw "no Visual Studio install carries the x64 C++ toolset" }; $msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'; if (-not (Test-Path $msbuild)) { throw "MSBuild not found at $msbuild" }; $want = (Select-String -Path "{{ gen_dir }}/CodeRedGenerator.vcxproj" -Pattern '<PlatformToolset>(v\d+)</PlatformToolset>' | Select-Object -First 1).Matches[0].Groups[1].Value; $have = @(Get-ChildItem (Join-Path $vs 'MSBuild/Microsoft/VC/*/Platforms/x64/PlatformToolsets/*') -Directory -ErrorAction SilentlyContinue | ForEach-Object Name | Where-Object { $_ -match '^v\d+$' } | Sort-Object -Descending -Unique); if (-not $have) { throw "no v1NN platform toolsets under $vs" }; $ts = if ($have -contains $want) { $want } else { $have[0] }; Write-Output "MSBuild: $msbuild"; Write-Output "toolset: $ts (project asks for $want; installed: $($have -join ', '))"; & $msbuild "{{ gen_dir }}/CodeRedGenerator.sln" -p:Configuration=Release -p:Platform=x64 -p:PlatformToolset=$ts -v:minimal -nologo; if ($LASTEXITCODE -ne 0) { throw "generator build failed (exit $LASTEXITCODE)" }
    @Write-Output "generator built: {{ gen_dll }}"

# Inject the generator into the RUNNING game; it writes generated_sdk_new/ over a few minutes.
sdk-gen:
    @if (-not (Get-Process XCom2 -ErrorAction Ignore)) { throw "XCom2 is not running - use 'just run' first" }
    @if (-not (Test-Path "{{ gen_dll }}")) { throw "generator not built - run 'just sdk-build'" }
    @if (Test-Path "generated_sdk_new") { throw "generated_sdk_new already exists - move or delete it first" }
    @python tools/frida-run.py --script tools/inject-sdk-generator.js -a "{{ gen_dll }}"
    @Write-Output "Injected. Generation starts ~10s later; wait for the message box, then quit the game."

# Repair generated_sdk_new, swap it in (old kept), and verify via the layout asserts.
# Pass allow_vanilla=1 to adopt a dump taken with no Community Highlander loaded (you almost
# certainly do not want this - see the check below).
sdk-adopt allow_vanilla="0":
    @if (-not (Test-Path "generated_sdk_new/X2SDK")) { throw "no generated_sdk_new/X2SDK - run 'just sdk-gen' first" }
    # ⛔ REFUSE A VANILLA DUMP. The Highlander ships replacement XComGame/Engine/Core .upk files,
    # so with it loaded EVERY class layout comes from its recompile - and the project's offsets
    # are calibrated against that build, not the stock one. A dump taken with no mods active is
    # therefore wrong in ways nothing downstream reports: the layout asserts pass, because the
    # one documented divergence (UIShellSoundtrackPicker's localized m_chooseSoundtrack, absent
    # under the Highlander, shifting everything after 0x430 down 0x10) is precisely where
    # sdk_layout_checks.cpp deliberately asserts NOTHING. So a green build proves nothing here
    # and the tell has to be read off the dump itself. UIScreenStack.OnInputSubscribers is that
    # tell (Highlander issue #198): present in their build, absent from stock.
    @$hdr = "generated_sdk_new/X2SDK/SDK_HEADERS/XComGame_classes.hpp"; if (-not (Test-Path $hdr)) { throw "no $hdr - the generation run did not finish" }; $body = (Get-Content -Raw $hdr); $i = $body.IndexOf("class UUIScreenStack : public UObject"); if ($i -lt 0) { throw "UUIScreenStack not found in the dump - it is not a usable XComGame header" }; $cls = $body.Substring($i, [Math]::Min(2000, $body.Length - $i)); if ($cls -notmatch 'OnInputSubscribers') { if ("{{ allow_vanilla }}" -ne "1") { throw "VANILLA DUMP REFUSED: UIScreenStack carries no OnInputSubscribers, so this was generated with no Community Highlander loaded. Every layout in it is the stock package's. Fix the mod list (just uscript repopulates ActiveMods), relaunch, confirm the mods loaded, and regenerate. Override with 'just allow_vanilla=1 sdk-adopt' only if you know why." } else { Write-Output "WARNING: adopting a VANILLA dump (allow_vanilla=1). Offsets are calibrated for the Highlander build." } } else { Write-Output "Highlander-derived dump confirmed (UIScreenStack.OnInputSubscribers present)." }
    @python tools/fix_generated_sdk.py generated_sdk_new/X2SDK
    @if (Test-Path "generated_sdk_old") { Remove-Item -Recurse -Force "generated_sdk_old" }
    @if (Test-Path "generated_sdk") { Move-Item "generated_sdk" "generated_sdk_old" }
    @Move-Item "generated_sdk_new" "generated_sdk"
    @Write-Output "swapped in; previous SDK kept as generated_sdk_old"
    # RECONFIGURE, not just build. Whether the X2Access target EXISTS is decided at configure
    # time by X2ACCESS_SDK_PRESENT (see CMakeLists.txt), and this recipe is the one command in
    # the tree that changes that answer — a fresh clone configures with no SDK, generates one,
    # and lands here with a cache that never declared the target. `build` alone would reuse it
    # and fail with MSBuild's "project file does not exist", which names neither the SDK nor the
    # cache. The guard in `configured` cannot help: CMakeCache.txt is present, just stale.
    @{{ just_executable() }} configure
    @{{ just_executable() }} build
    @Write-Output "layout asserts passed - the new SDK agrees with every hand-transcribed offset"

# Put the previous SDK back if sdk-adopt produced a build that disagrees with our offsets.
sdk-revert:
    @if (-not (Test-Path "generated_sdk_old")) { throw "no generated_sdk_old to revert to" }
    @if (Test-Path "generated_sdk") { Remove-Item -Recurse -Force "generated_sdk" }
    @Move-Item "generated_sdk_old" "generated_sdk"
    @Write-Output "reverted to the previous SDK"

# Delete the CMake build tree.
clean:
    @if (Test-Path "{{ build_dir }}") { Remove-Item -Recurse -Force "{{ build_dir }}"; Write-Output "removed {{ build_dir }}" } else { Write-Output "{{ build_dir }} already clean" }

# Clean, reconfigure, and build + install the ASI from scratch.
rebuild: clean configure install
