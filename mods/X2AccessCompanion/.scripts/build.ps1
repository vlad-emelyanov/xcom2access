Param(
    [string] $srcDirectory, # the path that contains the mod folder (mods/)
    [string] $sdkPath,      # the path to your SDK installation ending in "XCOM 2 War of the Chosen SDK"
    [string] $gamePath,     # the path to your XCOM 2 installation ending in "XCOM2-WaroftheChosen"
    [string] $config        # build configuration
)

$ScriptDirectory = Split-Path $MyInvocation.MyCommand.Path
$common = Resolve-Path (Join-Path $ScriptDirectory "..\..\..\deps\X2ModBuildCommon\build_common.ps1")
Write-Host "Sourcing $common"
. ($common)

$builder = [BuildProject]::new("X2AccessCompanion", $srcDirectory, $sdkPath, $gamePath)

# Include Community Highlander source so we can reference HL-added APIs
# (e.g. UIScreenStack.SubscribeToOnInputForScreen) at compile time.
#
# Per the Highlander README ("Building Against Highlander"), mods compile against
# the WORKSHOP-published Highlander sources — the released build that matches the
# runtime HL the game actually loads. Do NOT compile against the GitHub tree
# (deps/X2WOTCCommunityHighlander): its master is an unreleased beta whose sources
# both diverge from the runtime and have broken the SDK's mod-mode make commandlet
# (silent crash, diagnosed 2026-08-18). The WORKSHOP beta below is a different
# thing — it is a published build with its own Src, and it is fine to build on.
#
# ⛔ PROBE THE CANDIDATE LIST, never one hardcoded id. Which Highlander a machine
# has is a per-machine fact like the ASI loader's host DLL name, and the ids are not
# interchangeable: the stable and the beta ship the same PACKAGE name
# (X2WOTCCommunityHighlander) from different workshop folders, so a build that
# insisted on one id simply stopped working the day the author switched
# subscriptions (2026-09-16, mid-session: the stable's folder vanished between two
# builds and the failure named a subscribe step that was already done). Set
# XCOM2_HIGHLANDER_ID to pin one explicitly.
#
# ⚠ Both installed at once is REFUSED rather than guessed. They publish the same
# package name, so the game itself cannot load both, and picking one here by order
# would compile against sources that may not be the ones the runtime loads — the
# silent-wrongness this whole file is arranged to avoid.
$steamApps = Resolve-Path (Join-Path $gamePath "..\..\..")
$hlCandidates = [ordered]@{
    "1134256495" = "Community Highlander (stable)"
    "1796402257" = "Community Highlander (beta)"
}
if (-not [string]::IsNullOrEmpty($env:XCOM2_HIGHLANDER_ID)) {
    $hlCandidates = [ordered]@{ $env:XCOM2_HIGHLANDER_ID = "Community Highlander (XCOM2_HIGHLANDER_ID)" }
}
$hlFound = @()
foreach ($id in $hlCandidates.Keys) {
    $dir = Join-Path $steamApps "workshop\content\268500\$id"
    if (Test-Path (Join-Path $dir "Src\XComGame")) {
        $hlFound += [pscustomobject]@{ Id = $id; Dir = $dir; Label = $hlCandidates[$id] }
    }
}
if ($hlFound.Count -eq 0) {
    ThrowFailure ("No workshop Highlander sources found under {0}\workshop\content\268500 (looked for {1}). Subscribe to the Community Highlander, or set XCOM2_HIGHLANDER_ID to the workshop id you use." `
        -f $steamApps, ($hlCandidates.Keys -join ", "))
}
if ($hlFound.Count -gt 1) {
    ThrowFailure ("More than one Community Highlander is installed ({0}). They publish the same package name, so which one the runtime loads is ambiguous — unsubscribe from one, or set XCOM2_HIGHLANDER_ID to the workshop id to build against." `
        -f (($hlFound | ForEach-Object { "$($_.Id) $($_.Label)" }) -join "; "))
}
$hlDir = $hlFound[0].Dir
Write-Host ("Building against {0}, workshop id {1}" -f $hlFound[0].Label, $hlFound[0].Id)
$builder.IncludeSrc((Join-Path $hlDir "Src"))

# Workaround: X2ModBuildCommon v1.2.1 _SetupUtils populates modScriptPackages
# via Get-ChildItem -Directory, but the [string[]] typed field auto-stringifies
# DirectoryInfo objects to full paths. _CopyScriptPackages then builds invalid
# paths like "SDK\Script\E:\...\X2AccessCompanion.u".
#
# Fix: pre-make hook strips modScriptPackages to bare directory names.
# Pre-make hooks run after _SetupUtils (which populates the field) but before
# _CopyScriptPackages (which uses it). Capture $builder by value in closure.
$b = $builder
$builder.AddPreMakeHook({
    if ($null -ne $b.modScriptPackages -and $b.modScriptPackages.Length -gt 0) {
        [string[]] $fixed = @()
        foreach ($pkg in $b.modScriptPackages) {
            $fixed += [System.IO.Path]::GetFileName($pkg)
        }
        $b.modScriptPackages = $fixed
    }
}.GetNewClosure())

# Register the mod's script package with the SDK's make commandlet.
#
# `make -mods X2AccessCompanion <staging>` compiles ONLY the packages listed in
# [UnrealEd.EditorEngine] ModEditPackages in the SDK's DefaultEngine.ini (this is
# where the commandlet reads its mod-package list — confirmed by disassembly:
# GetArray(GConfig, "UnrealEd.EditorEngine", "ModEditPackages", ...)). If the entry
# is missing, make silently compiles only the base packages, produces NO
# X2AccessCompanion.u, and the build fails at the copy step with no useful error.
#
# Neither X2ModBuildCommon nor ModBuddy's own CompileScripts task writes this entry,
# so it must persist in the SDK config. A Steam "verify integrity" or a branch switch
# rewrites DefaultEngine.ini/XComEngine.ini and WIPES it (diagnosed 2026-08-19, after a
# full_content -> standard SDK branch switch). This pre-make hook re-adds it every build
# so the registration self-heals, and is idempotent when already present.
$builder.AddPreMakeHook({
    $iniPath = Join-Path $b.sdkPath "XComGame\Config\DefaultEngine.ini"
    $raw = Get-Content -Raw -LiteralPath $iniPath
    if ($raw -notmatch '(?m)^\+?ModEditPackages=X2AccessCompanion\s*$') {
        $new = [regex]::Replace(
            $raw,
            '(?m)^(\[UnrealEd\.EditorEngine\][^\r\n]*\r?\n)',
            ('${1}+ModEditPackages=X2AccessCompanion' + "`r`n"),
            1)
        if ($new -eq $raw) {
            ThrowFailure "Could not find [UnrealEd.EditorEngine] in $iniPath to register ModEditPackages"
        }
        Set-Content -NoNewline -LiteralPath $iniPath -Value $new
        Write-Host "Registered X2AccessCompanion in [UnrealEd.EditorEngine] ModEditPackages (was missing - likely reset by Steam verify/branch switch)."
    }
}.GetNewClosure())

# The game loads mod packages from ActiveMods in the USER config
# (Documents\My Games\XCOM2 War of the Chosen\XComGame\Config\XComModOptions.ini). The launcher
# normally writes it — and the game REWRITES it on exit with whatever was actually loaded, so
# one session launched while the Mods folder was empty wipes the section for every later
# direct XCom2.exe launch (diagnosed 2026-08-19: wiped 2026-08-18 during the C:->D: library
# migration; both mods silently absent since, surfacing as "companion CDO NOT FOUND" while the
# deployed files sat perfectly valid). Self-heal it every build, idempotently: our mod always,
# the workshop Community Highlander when it is installed. NOTE: if the game is RUNNING during
# the build, it may overwrite this again on exit — relaunch-after-build is the safe order.
$modOptionsPath = Join-Path ([Environment]::GetFolderPath('MyDocuments')) `
    "My Games\XCOM2 War of the Chosen\XComGame\Config\XComModOptions.ini"
$wantedMods = @("X2AccessCompanion")
# The same resolved folder the sources came from, so the ActiveMods entry can never name a
# Highlander other than the one just compiled against. The package name is identical for both
# workshop builds, which is exactly why the FOLDER is the thing that has to be resolved.
if (Test-Path (Join-Path $hlDir "X2WOTCCommunityHighlander.XComMod")) {
    $wantedMods += "X2WOTCCommunityHighlander"
}
if (Test-Path $modOptionsPath) {
    $optsRaw = Get-Content -Raw -LiteralPath $modOptionsPath
} else {
    $optsRaw = "[Engine.XComModOptions]`r`n`r`n"
}
if ($optsRaw -notmatch '(?m)^\[Engine\.XComModOptions\]') {
    $optsRaw = "[Engine.XComModOptions]`r`n`r`n" + $optsRaw
}
foreach ($mod in $wantedMods) {
    if ($optsRaw -notmatch ('(?m)^ActiveMods="?' + [regex]::Escape($mod) + '"?\s*$')) {
        $optsRaw = [regex]::Replace(
            $optsRaw,
            '(?m)^(\[Engine\.XComModOptions\][^\r\n]*\r?\n)',
            ('${1}ActiveMods="' + $mod + '"' + "`r`n"),
            1)
        Write-Host "Re-enabled $mod in ActiveMods ($modOptionsPath) - the game wipes the list when it exits with no mods loaded."
    }
}
# The whole Config DIRECTORY is absent until the game has been run once on this machine, and
# Set-Content does not create parents — so on a fresh PC the build died here with "Could not
# find a part of the path", after every compile step had already succeeded. Creating it is what
# the game itself would do on first launch, and it regenerates any other ini it needs.
$modOptionsDir = Split-Path -Parent $modOptionsPath
if (-not (Test-Path $modOptionsDir)) {
    New-Item -ItemType Directory -Force -Path $modOptionsDir | Out-Null
    Write-Host "Created $modOptionsDir - the game had not been run on this machine yet."
}
Set-Content -NoNewline -LiteralPath $modOptionsPath -Value $optsRaw

# ActiveMods is only half the rule: a mod is loaded when it is listed there AND its folder is
# under a ModRootDirs root. The game ships '+ModRootDirs=../../XcomGame/Mods/' in
# DefaultEngine.ini, but something on this setup rewrites the USER XComEngine.ini and leaves
# only the Steam workshop path (observed twice on 2026-08-22), which drops every locally
# deployed mod — ours — while every workshop mod, the Highlander included, keeps loading.
#
# That failure is silent and points the wrong way: the ASI loads, speaks its greeting and hooks
# ProcessEvent, then every companion call logs "companion CDO Default__X2Access*Reader NOT
# FOUND" and every script-sourced surface goes quiet. It reads as a broken bridge. The tell is
# in the game's own Launch.log - the ABSENCE of a line saying
# "Checking DLC installation for ../../XcomGame/Mods/X2AccessCompanion".
#
# ⛔ Repair only an EXISTING file. When the game has never run on this machine there is no user
# XComEngine.ini, and the one it generates from Default*.ini carries the Mods root already;
# writing a stub here would replace a full generated config with two lines.
$engineIniPath = Join-Path (Split-Path -Parent $modOptionsPath) "XComEngine.ini"
$modsRoot = "../../XcomGame/Mods/"
if (Test-Path $engineIniPath) {
    $engRaw = Get-Content -Raw -LiteralPath $engineIniPath
    # Slash direction and casing both vary between what the game writes and what it ships, so
    # match on shape rather than on the literal string - otherwise the repair appends a second,
    # differently-spelled copy of a root that is already there.
    if ($engRaw -notmatch '(?m)^ModRootDirs=\.\.[\\/]\.\.[\\/]XcomGame[\\/]Mods[\\/]?\s*$') {
        if ($engRaw -match '(?m)^\[Engine\.DownloadableContentEnumerator\]') {
            $engRaw = [regex]::Replace(
                $engRaw,
                '(?m)^(\[Engine\.DownloadableContentEnumerator\][^\r\n]*\r?\n)',
                ('${1}ModRootDirs=' + $modsRoot + "`r`n"),
                1)
        } else {
            $engRaw = $engRaw.TrimEnd() + "`r`n`r`n[Engine.DownloadableContentEnumerator]`r`nModRootDirs=$modsRoot`r`n"
        }
        Set-Content -NoNewline -LiteralPath $engineIniPath -Value $engRaw
        Write-Host "Restored ModRootDirs=$modsRoot in $engineIniPath - without it the game never enumerates locally deployed mods, whatever ActiveMods says."
    }
}

switch ($config)
{
    "debug" {
        $builder.EnableDebug()
    }
    "default" {
        # Nothing special
    }
    "" { ThrowFailure "Missing build configuration" }
    default { ThrowFailure "Unknown build configuration $config" }
}

# The sound package. Content/X2AccessSounds.upk is a BUILD PRODUCT (gitignored), generated from
# the tracked Sounds/*.wav by tools/make_sound_upk.py — the editor's Content Browser is the only
# other way to make one, and it is a sighted workflow. Generated here, before the build, because
# X2ModBuildCommon robocopies the whole mod root into staging, and because tools/release.ps1
# compiles the companion through this same script from a parked worktree. The generator skips
# itself when the package is newer than every WAV, which matters: X2ModBuildCommon keys its
# shader-precompile step on Content timestamps, and a package rewritten on every build would
# make every build pay for a precompile. ffmpeg on PATH is required (Vorbis encode).
$soundsDir = Join-Path $ScriptDirectory "..\Sounds"
$soundWavs = @(Get-ChildItem -LiteralPath $soundsDir -Filter *.wav -File -ErrorAction Ignore | Sort-Object Name)
if ($soundWavs.Count -gt 0) {
    $soundTool = Resolve-Path (Join-Path $ScriptDirectory "..\..\..\tools\make_sound_upk.py")
    $soundUpk = Join-Path $ScriptDirectory "..\Content\X2AccessSounds.upk"
    Write-Host ("Sound package: {0} WAV(s) -> {1}" -f $soundWavs.Count, $soundUpk)
    & python $soundTool $soundUpk @($soundWavs | ForEach-Object { $_.FullName })
    if ($LASTEXITCODE -ne 0) {
        ThrowFailure "make_sound_upk.py failed - the companion's earcons would be missing, refusing to build."
    }
}

$builder.InvokeBuild()
