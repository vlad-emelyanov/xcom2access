<#
.SYNOPSIS
    Install this repository's git hooks from tools/hooks/ into the active hooks directory.

.DESCRIPTION
    Git never versions .git/hooks, so the canonical copies live in tools/hooks/ and this script
    puts them where git will actually run them. Re-run it after pulling a change to tools/hooks/.

    It installs into whatever `git rev-parse --git-path hooks` resolves to, which honours
    core.hooksPath. Pin that to the MAIN repo's .git/hooks by ABSOLUTE path
    (`git config core.hooksPath <repo>/.git/hooks`) and every worktree — and therefore every
    concurrent session — shares one installed copy. That is intended: the pre-push hook
    republishes the rolling dev release, and a guard that only some sessions run is most of the
    value gone.

.PARAMETER Uninstall
    Remove the hooks this script installs, restoring plain git behaviour.
#>
[CmdletBinding()]
param([switch]$Uninstall)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$SrcDir   = Join-Path $PSScriptRoot 'hooks'

Push-Location $RepoRoot
try {
    $hooksDir = (git rev-parse --path-format=absolute --git-path hooks).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $hooksDir) { throw "Could not resolve the hooks directory." }
    if (-not (Test-Path $hooksDir)) { New-Item -ItemType Directory -Path $hooksDir -Force | Out-Null }

    Write-Host "hooks directory: $hooksDir"
    $hooks = Get-ChildItem $SrcDir -File

    foreach ($h in $hooks) {
        $dest = Join-Path $hooksDir $h.Name
        if ($Uninstall) {
            if (Test-Path $dest) { Remove-Item $dest -Force; Write-Host "  removed  $($h.Name)" }
            else { Write-Host "  absent   $($h.Name)" }
            continue
        }
        Copy-Item $h.FullName $dest -Force
        # Git for Windows runs hooks through its bundled sh and ignores the execute bit, but this
        # repo is also driven from MSYS2, whose git DOES require it — and a hook that silently
        # never runs is exactly the failure this hook exists to prevent.
        $chmod = Get-Command chmod -ErrorAction SilentlyContinue
        if ($chmod) { & $chmod.Source '+x' $dest 2>$null }
        Write-Host "  installed $($h.Name)"
    }

    if (-not $Uninstall) {
        Write-Host ""
        Write-Host "pre-push now republishes the rolling dev release whenever main goes to origin."
        Write-Host "It BLOCKS the push while it builds, and refuses while XCom2.exe is running."
        Write-Host "To push without republishing:"
        Write-Host "    git push --no-verify"
    }
}
finally {
    Pop-Location
}
