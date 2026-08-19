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
# the WORKSHOP-published Highlander sources (workshop item 1134256495) — the
# released version that matches the runtime HL the game actually loads. Do NOT
# compile against the GitHub tree (deps/X2WOTCCommunityHighlander): its master is
# an unreleased beta whose sources both diverge from the runtime and have broken
# the SDK's mod-mode make commandlet (silent crash, diagnosed 2026-08-18).
$steamApps = Resolve-Path (Join-Path $gamePath "..\..\..")
$hlSrc = Join-Path $steamApps "workshop\content\268500\1134256495\Src"
if (-not (Test-Path (Join-Path $hlSrc "XComGame"))) {
    ThrowFailure "Workshop Highlander sources not found at $hlSrc — subscribe to the Community Highlander (workshop id 1134256495)"
}
$builder.IncludeSrc($hlSrc)

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
$hlWorkshop = Join-Path $steamApps "workshop\content\268500\1134256495\X2WOTCCommunityHighlander.XComMod"
if (Test-Path $hlWorkshop) {
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
Set-Content -NoNewline -LiteralPath $modOptionsPath -Value $optsRaw

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

$builder.InvokeBuild()
