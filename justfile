# X2Access — build / deploy / run helpers.
# `just` on its own lists every recipe. The one-shot dev loop is `just go`.

set windows-shell := ["pwsh", "-NoProfile", "-NonInteractive", "-Command"]

# Override any of these per-invocation, e.g. `just config=Debug build`
game_path := "D:/SteamLibrary/steamapps/common/XCOM 2/XCom2-WarOfTheChosen"
sdk_path := "D:/SteamLibrary/steamapps/common/XCOM 2 War of the Chosen SDK"
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
game_log := "C:/Users/alex/Documents/My Games/XCOM2 War of the Chosen/XComGame/Logs/Launch.log"

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
    @if (Test-Path "{{ game_log }}") { Get-Content -Path "{{ game_log }}" -Tail {{ lines }} } else { Write-Output "no Launch.log yet at {{ game_log }}" }

# Show what is currently installed in the game and whether it is running.
status:
    @$items = @{ "ASI" = "{{ win64 }}/scripts/X2Access.asi"; "ASI loader" = "{{ win64 }}/version.dll"; "NVDA client" = "{{ win64 }}/nvdaControllerClient.dll"; "Companion mod" = "{{ game_path }}/XComGame/Mods/X2AccessCompanion" }; foreach ($k in $items.Keys) { $p = $items[$k]; if (Test-Path $p) { Write-Output ("{0}: installed, {1}" -f $k, (Get-Item $p).LastWriteTime) } else { Write-Output ("{0}: MISSING ({1})" -f $k, $p) } }; if (Get-Process XCom2 -ErrorAction Ignore) { Write-Output "XCom2: running" } else { Write-Output "XCom2: not running" }

# ---- dev harness (loopback HTTP, port 8774) ---------------------------------
#
# Two gates. The build gate is `devserver` above (compiled in by default here, out of a
# player build). The RUNTIME gate is a marker file next to the .asi, so an armed build is
# still silent until you say so — and so arming survives a Steam relaunch, which an
# environment variable would not.
#
#   just arm && just go      # arm, rebuild, relaunch — the harness comes up with the game
#   just dev-status          # what is on screen right now
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
msbuild := "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe"

# Compile the SDK generator DLL (needed only after changing its Configuration.cpp).
sdk-build:
    @if (-not (Test-Path "{{ msbuild }}")) { throw "MSBuild not found at {{ msbuild }}" }
    @& "{{ msbuild }}" "{{ gen_dir }}/CodeRedGenerator.sln" -p:Configuration=Release -p:Platform=x64 -v:minimal -nologo
    @Write-Output "generator built: {{ gen_dll }}"

# Inject the generator into the RUNNING game; it writes generated_sdk_new/ over a few minutes.
sdk-gen:
    @if (-not (Get-Process XCom2 -ErrorAction Ignore)) { throw "XCom2 is not running - use 'just run' first" }
    @if (-not (Test-Path "{{ gen_dll }}")) { throw "generator not built - run 'just sdk-build'" }
    @if (Test-Path "generated_sdk_new") { throw "generated_sdk_new already exists - move or delete it first" }
    @python tools/frida-run.py --script tools/inject-sdk-generator.js
    @Write-Output "Injected. Generation starts ~10s later; wait for the message box, then quit the game."

# Repair generated_sdk_new, swap it in (old kept), and verify via the layout asserts.
sdk-adopt:
    @if (-not (Test-Path "generated_sdk_new/X2SDK")) { throw "no generated_sdk_new/X2SDK - run 'just sdk-gen' first" }
    @python tools/fix_generated_sdk.py generated_sdk_new/X2SDK
    @if (Test-Path "generated_sdk_old") { Remove-Item -Recurse -Force "generated_sdk_old" }
    @if (Test-Path "generated_sdk") { Move-Item "generated_sdk" "generated_sdk_old" }
    @Move-Item "generated_sdk_new" "generated_sdk"
    @Write-Output "swapped in; previous SDK kept as generated_sdk_old"
    @just build
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
