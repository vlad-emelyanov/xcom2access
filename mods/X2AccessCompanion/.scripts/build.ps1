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
