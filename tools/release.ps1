<#
.SYNOPSIS
    Build, package and publish the rolling `dev` test release to git.alex19ep.me.

.DESCRIPTION
    Cuts an archive testers extract at the WOTC game folder and publishes it as a ROLLING
    release: the `dev` tag is force-moved to the current HEAD every run and the release's assets
    are replaced, so there is exactly one "current dev build" at any time. Nothing is versioned per
    run — that is the point (the CyberAccess decision of 2026-08-12, carried over unchanged), and
    it is why the notes carry the commit SHA: a tester's bug report has to name a commit, because
    the tag alone never will.

    The archive holds BOTH halves — the ASI (with its PDB) and the compiled UnrealScript companion —
    because neither is a mod on its own, and a tester who gets one without the other reports
    "silent" bugs that are nothing but the missing half.

    ⛔ The ASI is built from a SEPARATE build tree configured with X2ACCESS_DEVSERVER=OFF. The
    everyday build/ tree has it ON, and that DLL must never ship: it carries the loopback HTTP
    server and the /exec console bridge, which runs any console command — cheat managers included.
    The gate is compile-time, so OFF genuinely removes the code rather than disarming it —
    Assert-NoDevServer below proves that on the built artifact instead of trusting the flag.

    ⛔ The companion is compiled through the SDK's make commandlet, which HANGS FOREVER while the
    game holds the package open (CLAUDE.md, build_uscript). The script refuses to start while
    XCom2.exe is running rather than let a release look like a slow compile.

    Credentials come from .env.rc (gitignored) — NOT from .env. The justfile loads .env into the
    environment of every recipe, `just run` included, so a token there would be inherited by the
    game process. The repo is PUBLIC; never inline a token here.

.PARAMETER BuildDir
    CMake build tree to use. Defaults to release-stage/build. Reused incrementally between runs.

.PARAMETER SkipBuild
    Reuse whatever is already in BuildDir. Only safe when that tree was configured devserver-OFF;
    the devserver assertion still runs regardless, so a mistake here fails loudly. The companion
    is still recompiled: its build is fast and its output lives in the SDK, not in BuildDir.

.PARAMETER FromWorkingTree
    Build from the live checkout instead of a parked clean worktree at HEAD. Refused when any
    build input is dirty — see the note at the build step.

.PARAMETER SkipMainPush
    Publish the tag and the release, but leave pushing main to the caller. The pre-push hook
    passes this; see tools/hooks/pre-push for why it is load-bearing there.

.PARAMETER DryRun
    Do everything local — build, stage, zip, assert — and stop before touching the remote.
    Prints exactly what would be pushed and published.
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [switch]$SkipBuild,
    [switch]$FromWorkingTree,
    [switch]$SkipMainPush,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Owner    = 'alex19ep'
$Repo     = 'xcom2access'
$ApiBase  = "https://git.alex19ep.me/api/v1"
$Tag      = 'dev'
$ModName  = 'X2AccessCompanion'

if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot 'release-stage/build' }
$StageDir = Join-Path $RepoRoot 'release-stage/stage'
$OutDir   = Join-Path $RepoRoot 'release-stage'

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Warn($msg) { Write-Host "!!  $msg" -ForegroundColor Yellow }

# ---------------------------------------------------------------- env files

# KEY=value lines, `#` comments, optional quotes — the same shape the justfile's dotenv loader
# reads, so .env and .env.rc parse identically.
function Read-EnvFile([string]$path) {
    $vals = @{}
    if (-not (Test-Path $path)) { return $vals }
    foreach ($line in Get-Content $path) {
        if ($line -match '^\s*#') { continue }
        if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$') {
            $vals[$Matches[1]] = $Matches[2].Trim('"').Trim("'")
        }
    }
    return $vals
}

function Read-EnvRc {
    $path = Join-Path $RepoRoot '.env.rc'
    if (-not (Test-Path $path)) {
        throw ".env.rc not found at $path. Generate a Forgejo token at " +
              "https://git.alex19ep.me/user/settings/applications with the repository " +
              "permission set to Read and Write, then put it in .env.rc as FORGE_TOKEN=<token>. " +
              "(Not in .env: the justfile exports that file into every recipe's environment, " +
              "the launched game's included.)"
    }
    $vals = Read-EnvFile $path
    if (-not $vals.ContainsKey('FORGE_TOKEN') -or -not $vals['FORGE_TOKEN'] -or
        $vals['FORGE_TOKEN'] -eq 'paste_your_token_here') {
        throw "FORGE_TOKEN is not set in .env.rc."
    }
    return $vals
}

# The game and SDK paths, with the justfile's precedence: a real environment variable, then
# .env, then the generic Steam layout. The companion compile needs both (the SDK to run make,
# the game to find the workshop Highlander sources it compiles against).
function Resolve-GamePaths {
    $dotenv = Read-EnvFile (Join-Path $RepoRoot '.env')
    $defaults = @{
        XCOM2_GAME_PATH = 'C:/Program Files (x86)/Steam/steamapps/common/XCOM 2/XCom2-WarOfTheChosen'
        XCOM2_SDK_PATH  = 'C:/Program Files (x86)/Steam/steamapps/common/XCOM 2 War of the Chosen SDK'
    }
    $out = @{}
    foreach ($k in $defaults.Keys) {
        $v = [Environment]::GetEnvironmentVariable($k)
        if (-not $v -and $dotenv.ContainsKey($k)) { $v = $dotenv[$k] }
        if (-not $v) { $v = $defaults[$k] }
        $out[$k] = $v
    }
    return $out
}

# ------------------------------------------------------------------ API calls

function Invoke-Forge {
    param(
        [Parameter(Mandatory)][string]$Method,
        [Parameter(Mandatory)][string]$Path,
        $Body,
        [switch]$AllowNotFound
    )
    $headers = @{ Authorization = "token $($script:Token)"; Accept = 'application/json' }
    # NOT $args - that is an automatic variable in PowerShell and assigning to it inside a
    # function shadows the caller's argument array.
    $req = @{ Method = $Method; Uri = "$ApiBase$Path"; Headers = $headers }
    if ($null -ne $Body) {
        $req.Body        = ($Body | ConvertTo-Json -Depth 5 -Compress)
        $req.ContentType = 'application/json'
    }
    try {
        return Invoke-RestMethod @req
    } catch {
        $code = $null
        if ($_.Exception.PSObject.Properties.Name -contains 'Response' -and $_.Exception.Response) {
            $code = [int]$_.Exception.Response.StatusCode
        }
        if ($AllowNotFound -and $code -eq 404) { return $null }
        throw "Forge API $Method $Path failed ($code): $($_.Exception.Message)"
    }
}

# ------------------------------------------------------------------ integrity

# The one check that must never be skipped. String presence is the honest test: dev_server.cpp
# is not compiled at all when X2ACCESS_DEVSERVER=OFF (CMakeLists.txt adds the TU under the
# option), so these literals — the runtime marker's file name, one endpoint path and one header
# the /health answer prints — cannot survive a correct release build. They live in that one TU
# and nowhere else (grep src/ before changing the list). If any appear, the wrong tree was
# packaged.
function Assert-NoDevServer([string]$Dll) {
    $markers = @('X2Access.devserver', '/flashqueue', 'pump-age-ms')
    $bytes   = [System.IO.File]::ReadAllBytes($Dll)
    # Latin1 keeps one byte == one char, so an ASCII literal in the binary matches verbatim.
    $text    = [System.Text.Encoding]::Latin1.GetString($bytes)
    $hits    = @($markers | Where-Object { $text.Contains($_) })
    if ($hits.Count -gt 0) {
        throw "REFUSING TO PUBLISH: $Dll contains dev-harness markers [$($hits -join ', ')]. " +
              "That build was configured with X2ACCESS_DEVSERVER=ON and carries the loopback " +
              "server and the /exec console bridge. Rebuild with -DX2ACCESS_DEVSERVER=OFF."
    }
    Write-Host "    devserver markers absent - safe to ship" -ForegroundColor Green
}

# --------------------------------------------------------------------- build

Push-Location $RepoRoot
try {
    # ⛔ Before anything else. The companion's deploy step robocopies into the game's Mods folder
    # with a retry count of a million and no timeout, and the running game holds the .u open —
    # so a release started with the game up sits in Start-Sleep forever and reads as a slow build.
    if (Get-Process XCom2 -ErrorAction Ignore) {
        throw "XCom2.exe is running. The companion compile cannot deploy while the game holds its " +
              "package open (it hangs, it does not fail). Close the game (`just kill`) and run again."
    }

    $sha      = (git rev-parse HEAD).Trim()
    $shaShort = (git rev-parse --short HEAD).Trim()
    # ⛔ Several Claude sessions work this repository SIMULTANEOUSLY, in ONE working tree. That is
    # normal here, not an accident — so at any moment src/ or mods/ may hold another session's
    # half-finished feature, and a build from the live tree would compile it into an archive whose
    # notes claim a commit that does not contain it. Publishing someone's unreviewed
    # work-in-progress to testers under a published commit's name is not a warning-level mistake.
    #
    # So by default we do not build from the live tree at all: a detached worktree is parked at the
    # exact commit being published, and the archive is built from that. It is reused between runs
    # (and the build tree lives OUTSIDE it) so the incremental build still works. That makes "this
    # archive is commit $shaShort" true by construction rather than by everyone remembering to
    # check.
    if ($FromWorkingTree) {
        $SrcDir = $RepoRoot
        # Only the paths that actually FEED the archive. tools/, docs/ and the generated SDK are
        # routinely dirty or untracked and cannot reach it, so blocking on those would just teach
        # everyone to reach for the escape hatch.
        $dirty = @(git status --porcelain -- 'src' 'mods' 'tests' 'deps' 'CMakeLists.txt' 'README.md' 'LICENSE')
        if ($dirty.Count -gt 0) {
            Write-Warn "Uncommitted changes to build inputs:"
            $dirty | ForEach-Object { Write-Warn "    $_" }
            throw "REFUSING TO PACKAGE: -FromWorkingTree would build the archive from these " +
                  "changes and publish it as commit $shaShort, which does not contain them. " +
                  "Commit them, or drop -FromWorkingTree to build from a clean checkout."
        }
    } else {
        $SrcDir = Join-Path $RepoRoot 'release-stage/src'
        Write-Step "Parking a clean checkout of $shaShort at release-stage/src"
        if (Test-Path (Join-Path $SrcDir '.git')) {
            git -C $SrcDir checkout --detach --force $sha 2>&1 | Out-Host
        } else {
            git worktree add --detach --force $SrcDir $sha 2>&1 | Out-Host
        }
        if ($LASTEXITCODE -ne 0) { throw "Could not park a worktree at $sha." }
        # Only the submodules the ARCHIVE is built from: prism links into the ASI, and the
        # companion's build.ps1 sources X2ModBuildCommon by relative path. The Highlander is a
        # reference tree (the companion compiles against the WORKSHOP sources, see build.ps1) and
        # the SDK generator is a dev tool; neither reaches the archive. Their objects are already
        # in this repo's .git, so this is a local checkout, not a fetch — and NOT --recursive:
        # prism's own submodule is godot-cpp, which PRISM_ENABLE_GDEXTENSION=OFF never builds, and
        # pulling it would turn an offline checkout into a network clone.
        git -C $SrcDir submodule update --init deps/prism deps/X2ModBuildCommon 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Submodule checkout failed in $SrcDir." }
    }

    # The generated SDK is a build input that is NOT in git (CMakeLists.txt), so a clean checkout
    # has none and the ASI target would silently not be declared. The parked tree borrows the
    # main tree's — the one every hand-transcribed offset was static_asserted against.
    $SdkDir = Join-Path $RepoRoot 'generated_sdk/X2SDK'
    if (-not (Test-Path (Join-Path $SdkDir 'GameDefines.cpp'))) {
        throw "No generated SDK at $SdkDir - the ASI cannot be built. Regenerate it first " +
              "(just sdk-build, just run, just sdk-gen, just sdk-adopt)."
    }

    $paths    = Resolve-GamePaths
    $gamePath = $paths['XCOM2_GAME_PATH']
    $sdkPath  = $paths['XCOM2_SDK_PATH']
    if (-not (Test-Path $sdkPath)) { throw "SDK not found at $sdkPath (XCOM2_SDK_PATH in .env)." }
    if (-not (Test-Path $gamePath)) { throw "Game not found at $gamePath (XCOM2_GAME_PATH in .env)." }

    if (-not $SkipBuild) {
        Write-Step "Configuring release tree (devserver OFF) at $BuildDir"
        # Same generator as the everyday tree (`cmake -B build -A x64`), so the /MT pin and the
        # per-config PDB rules in CMakeLists.txt apply unchanged. X2ACCESS_DEVSERVER is passed OFF
        # explicitly rather than left to its default: this tree is reused between runs, so a value
        # set in it once would otherwise stick forever.
        cmake -S $SrcDir -B $BuildDir -A x64 -DX2ACCESS_DEVSERVER=OFF `
              -DX2ACCESS_SDK_DIR="$SdkDir" `
              -DXCOM2_GAME_PATH="$gamePath" -DXCOM2_SDK_PATH="$sdkPath" | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed." }

        Write-Step "Building the ASI (Release)"
        cmake --build $BuildDir --config Release --target X2Access | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed." }

        Write-Step "Running the Graph kernel conformance suite"
        cmake --build $BuildDir --config Release --target GraphTests | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "GraphTests build failed." }
        ctest --test-dir $BuildDir -C Release --output-on-failure | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Kernel tests failed - refusing to package a build that fails them." }
    }

    # The companion, compiled from the SOURCE BEING SHIPPED (the parked tree's mods/), through the
    # same X2ModBuildCommon path `just uscript` uses. Its output lands in the SDK's staging folder
    # and self-deploys to the game's Mods/ — the latter is a side effect on this machine's install,
    # the same one every `just go-full` has.
    Write-Step "Compiling the UnrealScript companion from $SrcDir/mods"
    $pwsh = (Get-Command pwsh -ErrorAction SilentlyContinue)
    if (-not $pwsh) { $pwsh = Get-Command powershell.exe }
    & $pwsh.Source -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $SrcDir "mods/$ModName/.scripts/build.ps1") `
        -srcDirectory (Join-Path $SrcDir 'mods') -sdkPath $sdkPath -gamePath $gamePath -config default | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Companion compile failed - refusing to package broken scripts." }

    Write-Step "Staging the deploy layout"
    if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
    $asiDir = Join-Path $StageDir 'Binaries/Win64/scripts'
    $modDst = Join-Path $StageDir "XComGame/Mods/$ModName"
    New-Item -ItemType Directory -Force -Path $asiDir, $modDst | Out-Null

    $dll = Join-Path $BuildDir 'bin/Release/X2Access.asi'
    if (-not (Test-Path $dll)) { throw "No ASI at $dll after build." }
    Copy-Item $dll $asiDir

    Write-Step "Checking the packaged ASI for dev-harness leftovers"
    Assert-NoDevServer (Join-Path $asiDir 'X2Access.asi')

    # The PDB ships INSIDE the archive, beside its own ASI — not as a separate release asset.
    # What it buys is the PAIRING, which a rolling tag otherwise destroys: symbols published as a
    # release asset are REPLACED on the next publish, so a dump from a tester still on last week's
    # build becomes unsymbolizable the moment we cut a new one. Inside the archive, every tester
    # permanently holds the PDB matching the binary they actually ran.
    # Fatal, not a warning: CMakeLists.txt emits it unconditionally (/Zi + /DEBUG on the target),
    # so a missing PDB means the build tree is not what it should be.
    $pdbSrc = Join-Path $BuildDir 'bin/Release/X2Access.pdb'
    if (-not (Test-Path $pdbSrc)) {
        throw "No PDB at $pdbSrc. /Zi and /DEBUG are unconditional on the X2Access target, so a " +
              "missing PDB means the build tree is not what it should be. Crash dumps from this " +
              "build could never be symbolized - refusing to package it."
    }
    Write-Step "Bundling the matching PDB beside the ASI"
    Copy-Item $pdbSrc $asiDir

    # The compiled mod is the SDK's staging copy — what X2ModBuildCommon mirrors into the game.
    # `.scripts/` is build tooling the mirror carries along; nothing in the game reads it.
    $modSrc = Join-Path $sdkPath "XComGame/Mods/$ModName"
    foreach ($must in "$ModName.XComMod", "Script/$ModName.u", 'Config', 'Localization') {
        if (-not (Test-Path (Join-Path $modSrc $must))) {
            throw "Compiled companion is missing $must under $modSrc - the SDK build did not produce a whole mod."
        }
    }
    Write-Step "Staging the companion from $modSrc"
    robocopy $modSrc $modDst /E /NP /NJH /NJS /NFL /NDL /XD '.scripts' | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy of the companion failed ($LASTEXITCODE)." }

    # The README lists the keys and carries the Prism MPL-2.0 notice, which has to reach whoever
    # receives the binary; the LICENSE is this project's own. Both from the tree being shipped.
    foreach ($doc in 'README.md', 'LICENSE') {
        $src = Join-Path $SrcDir $doc
        if (-not (Test-Path $src)) { throw "No $doc in $SrcDir; refusing to package a build without it." }
        Copy-Item $src $StageDir
    }

    $zip = Join-Path $OutDir "X2Access-dev-$shaShort.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Write-Step "Packing $zip"
    Compress-Archive -Path (Join-Path $StageDir '*') -DestinationPath $zip -CompressionLevel Optimal

    $zipMB = [math]::Round((Get-Item $zip).Length / 1MB, 2)
    Write-Host "`n    archive : $zip ($zipMB MB)"
    foreach ($f in (Get-ChildItem $StageDir -Recurse -File)) {
        Write-Host "              $($f.FullName.Substring($StageDir.Length + 1) -replace '\\','/')"
    }

    # ------------------------------------------------------------------ prune
    # Every run drops an archive named after its commit; a few are worth keeping rather than none,
    # because the tag MOVES and the only way to hand a tester the exact bits they are running — and
    # the PDB paired with them — is to still have their archive on disk. Sorted by write time, NOT
    # by name: the names are commit SHAs and sort meaninglessly. The archive just written is
    # necessarily the newest, so it is never the one pruned.
    $keepArchives = 4
    $stale = @(Get-ChildItem $OutDir -Filter 'X2Access-dev-*.zip' -File |
               Sort-Object LastWriteTime -Descending | Select-Object -Skip $keepArchives)
    if ($stale.Count -gt 0) {
        $freedMB = [math]::Round((($stale | Measure-Object Length -Sum).Sum) / 1MB, 1)
        Write-Step "Pruning $($stale.Count) old archive(s), keeping the newest $keepArchives ($freedMB MB)"
        foreach ($old in $stale) {
            Write-Host "    removing $($old.Name)"
            Remove-Item $old.FullName -Force
        }
    }

    # ------------------------------------------------------------------ notes

    $date    = (git log -1 --format=%cd --date=short).Trim()
    # -join is not optional: git returns string[], and interpolating an array into the here-string
    # below flattens it with SPACES, collapsing the whole changelog onto one unreadable line.
    $recent  = ((git log -12 --format='- %s') -join "`n")
    $notes   = @"
Rolling pre-alpha test build of X2Access, a screen-reader accessibility mod for XCOM 2: War of the Chosen.

**This tag moves.** ``$Tag`` always points at the newest test build, so re-downloading the same tag
later gives you different bits. When reporting anything, quote the commit below.

| | |
|---|---|
| Commit | ``$shaShort`` |
| Built | $date |

### Requirements

- XCOM 2: War of the Chosen on PC (Windows), current Steam build. The base game without the
  expansion is not supported.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) — download
  ``version-x64.zip`` and put its ``version.dll`` in ``XCom2-WarOfTheChosen\Binaries\Win64\``. This
  is what loads the mod; without it nothing speaks.
- The [X2WOTC Community Highlander](https://steamcommunity.com/workshop/filedetails/?id=1134256495)
  from the Steam Workshop, enabled. The companion mod is compiled against it and will not load
  without it.
- A screen reader (NVDA, JAWS, ZDSR, ZoomText, SenseReader, PC-Talker, BoyPCReader), or SAPI /
  Windows OneCore as the fallback.

### Installing

Extract the archive at the **War of the Chosen game folder** — ``XCom2-WarOfTheChosen``, the one
holding ``Binaries`` and ``XComGame`` (under ``steamapps\common\XCOM 2``). It contains:

``````
README.md, LICENSE                  (at the root — the README lists the keys)
Binaries/Win64/scripts/             X2Access.asi, X2Access.pdb
XComGame/Mods/X2AccessCompanion/    the companion mod (.XComMod, Config, Localization, Script)
``````

Both halves must land. The ASI reads screens; the companion supplies the words for combat and for
every screen whose text the game keeps only in Flash — one without the other is a mod that goes
quiet in places, not a mod that fails. Then, in the game's launcher (or the Alternative Mod
Launcher), enable **X2AccessCompanion** and the **Community Highlander**.

Launch through Steam or the mod launcher as usual. If you start ``XCom2.exe`` directly instead, add
``-review`` to its arguments: the launchers pass it for you, and without it the game boots its
developer shell instead of the retail main menu, where nothing reads.

### Expect rough edges

Pre-alpha, under active development. Coverage grows screen by screen, keys can change without
notice, and screens that already work may break as neighbouring ones land. **Back up your saves.**

### Reporting

Say what you were doing, what was spoken, and what you expected instead. Quote the commit above.

Attach the mod's own log, ``Binaries\Win64\scripts\X2Access.log`` — the previous three sessions are
kept beside it as ``X2Access.1.log`` to ``X2Access.3.log``, so a relaunch does not destroy the
evidence. The game's own ``Launch.log`` (under ``Documents\My Games\XCOM2 War of the Chosen\XComGame\Logs``)
is worth attaching for anything that looks like the game misbehaving rather than the mod.

If it crashed and you have a minidump, send that too: ``X2Access.pdb`` is bundled beside the ASI in
this archive precisely so a dump can be read. Symbols only resolve against the exact build that
produced them, and because this tag moves, the symbols for the build you are running now will not
exist here later. Keep the archive you installed from, or send the ``.pdb`` along with the dump.

### Recent changes

$recent

---

MIT. Bundles [Prism](https://github.com/ethindp/prism) (MPL-2.0) statically; its source is the
upstream repository at the pinned tag. Unofficial fan project, not affiliated with Firaxis Games,
2K or Take-Two Interactive.
"@

    $notesPath = Join-Path $OutDir 'release-notes.md'
    Set-Content -Path $notesPath -Value $notes -Encoding UTF8
    Write-Host "    notes   : $notesPath`n"

    if ($DryRun) {
        Write-Step "DRY RUN - stopping before the remote"
        Write-Host "    would push  : main -> origin ($shaShort)"
        Write-Host "    would move  : tag '$Tag' -> $shaShort (force)"
        Write-Host "    would publish: prerelease '$Tag' with the assets above"
        return
    }

    # ----------------------------------------------------------- remote work

    $env_ = Read-EnvRc
    $script:Token = $env_['FORGE_TOKEN']
    $user  = if ($env_.ContainsKey('FORGE_USER')) { $env_['FORGE_USER'] } else { $Owner }

    # ⛔ Do NOT probe GET /user here. Forgejo's tokens are SCOPED, and /user needs a `user` scope
    # this release has no other reason to hold — a repository-only token answers it 403 while being
    # perfectly able to do everything below. Check the thing we actually need instead: the repo,
    # which also reports whether the token may WRITE, so a read-only token fails here with a
    # readable reason rather than three steps later mid-publish.
    Write-Step "Checking the token"
    $repoInfo = Invoke-Forge GET "/repos/$Owner/$Repo"
    $perms = if ($repoInfo.PSObject.Properties.Name -contains 'permissions') { $repoInfo.permissions } else { $null }
    if (-not $perms -or -not $perms.push) {
        throw "The token in .env.rc cannot write to $Owner/$Repo. Regenerate it at " +
              "https://git.alex19ep.me/user/settings/applications with the repository " +
              "permission set to Read and Write."
    }
    Write-Host "    $($repoInfo.full_name) - push:$($perms.push) admin:$($perms.admin)"

    # The Releases unit may ship DISABLED on this repo; the releases endpoints 404 until it is on.
    if (-not $repoInfo.has_releases) {
        Write-Step "Enabling the repository's Releases unit (currently off)"
        try {
            Invoke-Forge PATCH "/repos/$Owner/$Repo" @{ has_releases = $true } | Out-Null
        } catch {
            # Editing repository settings needs ADMIN, which a write-scoped token may not carry.
            # Say exactly which switch to flip rather than failing on a 404 further down.
            throw "Could not enable the Releases unit ($($_.Exception.Message)). Turn it on by " +
                  "hand at https://git.alex19ep.me/$Owner/$Repo/settings -> Advanced Settings -> " +
                  "check 'Releases', then run this again."
        }
    }

    # ⛔ Both guards are deliberate. tools/hooks/pre-push runs THIS script when main is pushed, so
    # the two pushes below would each re-enter it and rebuild the project forever. --no-verify
    # skips the hook; the env var makes the hook bail even if it is reached some other way (a
    # different hook path, a nested git invocation, someone running the hook by hand).
    $env:X2ACCESS_RELEASING = '1'
    try {
        $pushUrl = "https://$([uri]::EscapeDataString($user)):$([uri]::EscapeDataString($script:Token))@git.alex19ep.me/$Owner/$Repo.git"

        # ⛔ In hook mode we must NOT push main, and the reason is not obvious. git decides what to
        # send BEFORE running pre-push, including the ref's expected OLD value. If we move main
        # here, git's own push then arrives claiming an old value that is no longer current and the
        # server rejects it — "incorrect old value provided" — so a completely successful release
        # ends in what looks exactly like a failed push. Pushing only the TAG avoids it: the tag
        # carries the very same commit objects, so git's subsequent push of main is an ordinary
        # fast-forward with nothing left to transfer.
        if ($SkipMainPush) {
            Write-Step "Skipping the main push - the caller's own push will do it"
        } else {
            Write-Step "Pushing main"
            git push --no-verify $pushUrl HEAD:main 2>&1 | ForEach-Object { $_ -replace [regex]::Escape($script:Token), '***' } | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "git push failed." }
        }

        Write-Step "Moving the '$Tag' tag to $shaShort"
        git tag -f $Tag $sha | Out-Host
        git push --no-verify -f $pushUrl "refs/tags/${Tag}:refs/tags/${Tag}" 2>&1 | ForEach-Object { $_ -replace [regex]::Escape($script:Token), '***' } | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "git tag push failed." }
    }
    finally {
        Remove-Item Env:\X2ACCESS_RELEASING -ErrorAction SilentlyContinue
    }

    Write-Step "Publishing the release"
    $existing = Invoke-Forge GET "/repos/$Owner/$Repo/releases/tags/$Tag" -AllowNotFound
    $payload  = @{
        tag_name         = $Tag
        target_commitish = $sha
        name             = "dev build ($shaShort)"
        body             = $notes
        draft            = $false
        prerelease       = $true
    }
    if ($existing) {
        $release = Invoke-Forge PATCH "/repos/$Owner/$Repo/releases/$($existing.id)" $payload
        # Assets are not versioned, so a stale one would sit next to the new build forever.
        foreach ($a in @($existing.assets)) {
            Write-Host "    removing stale asset $($a.name)"
            Invoke-Forge DELETE "/repos/$Owner/$Repo/releases/$($existing.id)/assets/$($a.id)" | Out-Null
        }
    } else {
        $release = Invoke-Forge POST "/repos/$Owner/$Repo/releases" $payload
    }

    foreach ($asset in @($zip)) {
        $name = Split-Path $asset -Leaf
        Write-Step "Uploading $name"
        Invoke-RestMethod -Method POST `
            -Uri "$ApiBase/repos/$Owner/$Repo/releases/$($release.id)/assets?name=$([uri]::EscapeDataString($name))" `
            -Headers @{ Authorization = "token $($script:Token)" } `
            -Form @{ attachment = Get-Item $asset } | Out-Null
    }

    Write-Host "`nPublished: https://git.alex19ep.me/$Owner/$Repo/releases/tag/$Tag" -ForegroundColor Green
}
finally {
    Pop-Location
}
