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
