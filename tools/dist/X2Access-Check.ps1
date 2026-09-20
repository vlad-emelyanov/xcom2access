<#
.SYNOPSIS
    Checks an X2Access install and switches on the two mods the game has to load.

.DESCRIPTION
    Ships at the root of the X2Access release archive, beside README.md. Run it after
    extracting the archive at the War of the Chosen game folder, or any time speech
    goes quiet in places (combat narration, the tile reader, save names) while the
    menus still read: that is the shape of the companion mod not loading.

    What it does, in order:

      1. Finds the game folder (this script's own folder, or a Steam, GOG or Epic
         install; the mod does not care which store the copy came from).
      2. Checks the pieces the mod needs: the Ultimate ASI Loader host DLL,
         X2Access.asi, the companion mod's files, and the Community Highlander --
         subscribed or hand-installed, either counts.
      3. Checks the DLC that needs a Highlander plugin of its own: with Alien
         Hunters (the Alien Rulers) installed, DLC2CommunityHighlander has to be
         there too, and without that DLC it has to stay off.
      4. Enables X2AccessCompanion, the Highlander and any DLC plugin it needs in
         the game's user config (XComModOptions.ini under Documents), exactly the
         way the game's launcher does when you tick a mod, and restores the local
         Mods root in XComEngine.ini if something removed it.

    Every line of output starts with one of four words:
      OK       the check passed
      FIXED    something was wrong and this script corrected it
      PROBLEM  something is wrong that you have to fix; the line says how
      NOTE     advice, nothing to act on right now

    The output is plain text with one finding per line, written for a screen reader.

.PARAMETER GamePath
    The War of the Chosen game folder (the one holding Binaries and XComGame). Only
    needed when the script is not running from inside it and the Steam lookup fails.

.PARAMETER CheckOnly
    Report everything but change nothing.

.NOTES
    Windows PowerShell 5.1 or newer. Double-click X2Access-Check.cmd to run it without
    touching an execution policy.
#>
[CmdletBinding()]
param(
    [string]$GamePath,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'

$ModName             = 'X2AccessCompanion'
$HighlanderName      = 'X2WOTCCommunityHighlander'
$HighlanderUrl       = 'https://steamcommunity.com/workshop/filedetails/?id=1134256495'
$HighlanderGitUrl    = 'https://github.com/X2CommunityCore/X2WOTCCommunityHighlander/releases'
$AsiLoaderUrl        = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases'
$XcomAppId           = '268500'
# The Highlander replaces the game's script packages, and a DLC that ships one of its own needs
# the matching plugin or that DLC misbehaves with the Highlander on. Exactly one exists today:
# Alien Hunters (the Alien Rulers), which the game installs as XComGame\DLC\DLC_2. Its plugin is
# required WITH that DLC and harmful without it, so both directions are checked.
$DlcHighlanders = @(
    @{ Dlc = 'DLC_2'
       Title = 'Alien Hunters'
       Mod = 'DLC2CommunityHighlander'
       Url = 'https://steamcommunity.com/sharedfiles/filedetails/?id=2534737016' }
)
# Ultimate ASI Loader impersonates one of these; which one differs per machine, so a missing
# version.dll is not a broken install. Probe the whole list and say which one was found.
$LoaderHosts = @('version.dll', 'version-x64.dll', 'dinput8.dll', 'winmm.dll', 'dsound.dll',
                 'd3d9.dll', 'd3d11.dll', 'dxgi.dll', 'xinput1_3.dll', 'wininet.dll')

$script:Passed = 0
$script:Fixed  = 0
$script:Failed = 0

function Write-Ok      ([string]$Message) { $script:Passed++; Write-Host "OK: $Message" }
function Write-Fixed   ([string]$Message) { $script:Fixed++;  Write-Host "FIXED: $Message" }
function Write-Problem ([string]$Message) { $script:Failed++; Write-Host "PROBLEM: $Message" }
function Write-Note    ([string]$Message) { Write-Host "NOTE: $Message" }

function Test-GameFolder ([string]$Dir) {
    if (-not $Dir) { return $false }
    return (Test-Path -LiteralPath (Join-Path $Dir 'Binaries\Win64\XCom2.exe')) -and
           (Test-Path -LiteralPath (Join-Path $Dir 'XComGame'))
}

# Every Steam library on this machine: the install folder from the registry, plus every
# "path" entry in its libraryfolders.vdf.
function Get-SteamLibraries {
    $libs = @()
    $steam = $null
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
        try {
            $props = Get-ItemProperty -Path $key -ErrorAction Stop
            foreach ($name in 'SteamPath', 'InstallPath') {
                $value = $props.PSObject.Properties[$name]
                if ($value -and $value.Value) { $steam = [string]$value.Value; break }
            }
        } catch { }
        if ($steam) { break }
    }
    if (-not $steam) { return @() }
    $steam = $steam -replace '/', '\'
    $libs += $steam
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        $text = Get-Content -Raw -LiteralPath $vdf
        foreach ($m in [regex]::Matches($text, '"path"\s+"([^"]+)"')) {
            $libs += ($m.Groups[1].Value -replace '\\\\', '\')
        }
    }
    return @($libs | Select-Object -Unique)
}

# Where a non-Steam copy lives: the launchers that keep a registry key or a manifest are asked
# where their games are, and every answer still goes through Test-GameFolder, so a wrong guess
# costs nothing. Both the folder itself and its XCom2-WarOfTheChosen subfolder are offered,
# because a store may install the two campaigns side by side the way Steam does.
function Get-OtherGameFolders {
    $paths = @()
    foreach ($key in 'HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games', 'HKLM:\SOFTWARE\GOG.com\Games') {
        try {
            foreach ($entry in (Get-ChildItem -Path $key -ErrorAction Stop)) {
                $p = (Get-ItemProperty -Path $entry.PSPath -ErrorAction SilentlyContinue).path
                if ($p) { $paths += [string]$p }
            }
        } catch { }
    }
    $manifests = Join-Path $env:ProgramData 'Epic\EpicGamesLauncher\Data\Manifests'
    if (Test-Path -LiteralPath $manifests) {
        foreach ($f in (Get-ChildItem -LiteralPath $manifests -Filter '*.item' -ErrorAction SilentlyContinue)) {
            try { $text = Get-Content -Raw -LiteralPath $f.FullName } catch { continue }
            foreach ($m in [regex]::Matches($text, '"InstallLocation"\s*:\s*"([^"]+)"')) {
                $paths += ($m.Groups[1].Value -replace '\\\\', '\')
            }
        }
    }
    $out = @()
    foreach ($p in $paths) { $out += $p; $out += (Join-Path $p 'XCom2-WarOfTheChosen') }
    return @($out | Select-Object -Unique)
}

# Every folder the game looks in for mods: the local Mods folder the archive installs into, the
# Steam workshop folder of every library on this machine, and each ModRootDirs entry the config
# names -- the shipped default with a '+', whatever a launcher rewrote it to, and whatever a hand
# install added. A relative root is resolved the way the game resolves it: against Binaries\Win64,
# its working folder.
function Get-ModRoots ([string]$LocalMods, [string]$Win64Dir, [string]$GameDir, [string[]]$ConfigFiles) {
    $roots = @($LocalMods)
    # The game's own library may not be the one the registry names first, so take the workshop
    # folder that sits beside the install as well as the ones the registry knows about. A game
    # folder too near the root of a drive to have a steamapps above it is not an error: an
    # install from any other store looks exactly like that.
    try {
        $steamappsOfGame = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $GameDir))
        if ($steamappsOfGame) { $roots += (Join-Path $steamappsOfGame "workshop\content\$XcomAppId") }
    } catch { }
    foreach ($lib in Get-SteamLibraries) {
        $roots += (Join-Path $lib "steamapps\workshop\content\$XcomAppId")
    }
    foreach ($ini in $ConfigFiles) {
        if (-not $ini -or -not (Test-Path -LiteralPath $ini)) { continue }
        try { $text = (Read-ConfigText $ini).Text } catch { continue }
        foreach ($m in [regex]::Matches($text, '(?m)^\s*[+.]?ModRootDirs=(.+?)\s*$')) {
            $p = $m.Groups[1].Value
            if ($p -notmatch '^([A-Za-z]:|\\\\)') { $p = Join-Path $Win64Dir $p }
            try { $roots += [System.IO.Path]::GetFullPath($p) } catch { }
        }
    }
    return @($roots | Where-Object { $_ } | Select-Object -Unique)
}

# Finds the folder holding <ModId>.XComMod under any of the given roots, and says whether the
# game can actually load it from there. A mod is known to the game by the NAME OF ITS .XComMod
# FILE, never by the folder around it: a workshop folder is a numeric id and a hand-installed one
# keeps whatever name its zip had, so the file is what gets searched for. The game reads exactly
# one level down from a mod root, so a copy found deeper is reported, not accepted -- unzipping
# an archive that carries its own top folder is the usual way a hand install goes quiet.
function Find-ModDir ([string]$ModId, [string[]]$Roots) {
    $file = "$ModId.XComMod"
    $nested = $null
    foreach ($root in $Roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        foreach ($d in (Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue)) {
            if (Test-Path -LiteralPath (Join-Path $d.FullName $file)) {
                return [pscustomobject]@{ Path = $d.FullName; Loadable = $true }
            }
            if ($nested) { continue }
            foreach ($s in (Get-ChildItem -LiteralPath $d.FullName -Directory -ErrorAction SilentlyContinue)) {
                if (Test-Path -LiteralPath (Join-Path $s.FullName $file)) { $nested = $s.FullName; break }
            }
        }
    }
    if ($nested) { return [pscustomobject]@{ Path = $nested; Loadable = $false } }
    return $null
}

# Reads a config file keeping track of its encoding, so the write-back does not silently
# convert a UTF-16 file the game wrote into ANSI or the other way round.
function Read-ConfigText ([string]$Path) {
    $reader = New-Object System.IO.StreamReader($Path, [System.Text.Encoding]::Default, $true)
    try {
        $text = $reader.ReadToEnd()
        return @{ Text = $text; Encoding = $reader.CurrentEncoding }
    } finally {
        $reader.Close()
    }
}

function Write-ConfigText ([string]$Path, [string]$Text, [System.Text.Encoding]$Encoding) {
    [System.IO.File]::WriteAllText($Path, $Text, $Encoding)
}

Write-Host 'X2Access install check'
Write-Host ''

# ---- 1. the game folder ---------------------------------------------------------------------

$game = $null
if ($GamePath) {
    if (Test-GameFolder $GamePath) {
        $game = (Resolve-Path -LiteralPath $GamePath).Path
    } else {
        Write-Problem "The folder given as -GamePath is not the War of the Chosen game folder (no Binaries\Win64\XCom2.exe under it): $GamePath"
        exit 2
    }
}
if (-not $game) {
    # The archive extracts at the game folder, so this script normally sits right in it.
    # Walk up a few levels too, in case it was left inside a subfolder.
    $probe = $PSScriptRoot
    for ($i = 0; $i -lt 4 -and $probe; $i++) {
        if (Test-GameFolder $probe) { $game = $probe; break }
        $probe = Split-Path -Parent $probe
    }
}
if (-not $game) {
    foreach ($lib in Get-SteamLibraries) {
        $candidate = Join-Path $lib 'steamapps\common\XCOM 2\XCom2-WarOfTheChosen'
        if (Test-GameFolder $candidate) { $game = $candidate; break }
    }
}
if (-not $game) {
    # Nothing here is Steam-specific: the mod works the same on a copy from any store.
    foreach ($candidate in Get-OtherGameFolders) {
        if (Test-GameFolder $candidate) { $game = $candidate; break }
    }
}
if (-not $game) {
    Write-Problem 'Could not find the War of the Chosen game folder. Extract the archive at XCom2-WarOfTheChosen (the folder holding Binaries and XComGame; on Steam it is under steamapps\common\XCOM 2) and run this script from there, or pass -GamePath.'
    exit 2
}
Write-Ok "Game folder: $game"

$win64   = Join-Path $game 'Binaries\Win64'
$modsDir = Join-Path $game 'XComGame\Mods'

# Ask Windows where Documents is; it is not always under the profile and not always spelled
# in English. The game's own folder name under it is fixed, and is the same whatever store the
# copy came from.
$documents      = [Environment]::GetFolderPath('MyDocuments')
$configDir      = Join-Path $documents 'My Games\XCOM2 War of the Chosen\XComGame\Config'
$modOptionsPath = Join-Path $configDir 'XComModOptions.ini'
$engineIniPath  = Join-Path $configDir 'XComEngine.ini'

# ---- 2. is the game running? ------------------------------------------------------------------

# The game rewrites the active-mod list when it exits, with whatever it actually loaded. A fix
# written while it runs is undone the moment the player quits, so refuse to write and say why.
$gameRunning = $null -ne (Get-Process -Name 'XCom2' -ErrorAction SilentlyContinue)
if ($gameRunning) {
    Write-Problem 'XCOM 2 is running. Close it, then run this check again: the game overwrites its mod settings when it exits, which would undo any fix made now.'
}
$mayWrite = (-not $CheckOnly) -and (-not $gameRunning)

# ---- 3. the native half -----------------------------------------------------------------------

$foundHosts = @()
foreach ($h in $LoaderHosts) {
    if (Test-Path -LiteralPath (Join-Path $win64 $h)) { $foundHosts += $h }
}
if ($foundHosts.Count -gt 0) {
    Write-Ok "ASI loader host DLL present in Binaries\Win64: $($foundHosts -join ', ')"
} else {
    Write-Problem "No Ultimate ASI Loader in $win64. Download version-x64.zip from $AsiLoaderUrl and put its version.dll next to XCom2.exe. Without it the mod never loads and nothing speaks."
}

$asi = Join-Path $win64 'scripts\X2Access.asi'
if (Test-Path -LiteralPath $asi) {
    $stamp = (Get-Item -LiteralPath $asi).LastWriteTime.ToString('yyyy-MM-dd HH:mm')
    Write-Ok "X2Access.asi present (dated $stamp)"
} else {
    Write-Problem "X2Access.asi is missing from $win64\scripts. The archive was not extracted at the game folder, or only part of it was."
}
if (-not (Test-Path -LiteralPath (Join-Path $win64 'scripts\X2Access.pdb'))) {
    Write-Note 'X2Access.pdb is not beside the ASI. The mod runs without it, but a crash dump from this install could not be read; keep the archive you installed from.'
}

# ---- 4. the companion mod ---------------------------------------------------------------------

$modDir = Join-Path $modsDir $ModName
$missing = @()
foreach ($must in "$ModName.XComMod", "Script\$ModName.u", 'Config', 'Localization') {
    if (-not (Test-Path -LiteralPath (Join-Path $modDir $must))) { $missing += $must }
}
$companionInstalled = ($missing.Count -eq 0)
if ($companionInstalled) {
    Write-Ok "Companion mod complete at $modDir"
} else {
    Write-Problem "Companion mod incomplete at $modDir (missing: $($missing -join ', ')). Extract the whole archive at the game folder so XComGame\Mods\$ModName lands with the ASI."
}

# ---- 5. the Community Highlander --------------------------------------------------------------

# Subscribed mods live under <any Steam library>\steamapps\workshop\content\268500\<id>\, a
# hand-installed one under a mod root -- normally the game's own XComGame\Mods. Both count, and
# on a copy that came from anywhere but Steam the second is the only one there is. The Highlander
# also has more than one workshop listing (the release and its beta branch) with the SAME mod
# name, which is the name the game loads it by, so search for the .XComMod file rather than
# trusting one id or one folder name.
$modRoots = Get-ModRoots $modsDir $win64 $game @($engineIniPath, (Join-Path $game 'XComGame\Config\DefaultEngine.ini'))

$hl = Find-ModDir $HighlanderName $modRoots
if ($hl -and $hl.Loadable) {
    Write-Ok "Community Highlander found at $($hl.Path)"
} elseif ($hl) {
    Write-Problem "The Community Highlander is at $($hl.Path), one folder too deep for the game to find it: a mod root is read exactly one level down. Move the folder holding $HighlanderName.XComMod straight into $modsDir, so the .XComMod file ends up two levels under Mods, not three."
} else {
    Write-Problem "The X2WOTC Community Highlander is not installed. On Steam, subscribe to it ($HighlanderUrl) and let Steam download it; on any other copy of the game, download it from $HighlanderGitUrl and unzip it into $modsDir so that $HighlanderName.XComMod sits one folder down. Then run this check again. Without it the companion mod does not load: menus still read, but combat narration and every companion-sourced screen stay silent."
}
$highlanderReady = [bool]($hl -and $hl.Loadable)

# ---- 6. the DLC plugins the Highlander needs ---------------------------------------------------

# A DLC is installed when its folder is there -- XComGame\DLC\DLC_2 for Alien Hunters, which the
# store sells as the Alien Rulers. The plugin goes both ways: with the DLC it is required, and
# without it the plugin's own authors call it gamebreaking, so an enabled plugin on a copy that
# has no such DLC is reported too. Enabling one is left to section 7 along with everything else,
# and only when the Highlander it plugs into is loadable in the first place.
$dlcPlugins = @()
$strayPlugins = @()
foreach ($dlc in $DlcHighlanders) {
    $dlcDir = Join-Path $game "XComGame\DLC\$($dlc.Dlc)"
    $installed = (Test-Path -LiteralPath (Join-Path $dlcDir "$($dlc.Dlc).XComDLC")) -or
                 (Test-Path -LiteralPath (Join-Path $dlcDir "Script\$($dlc.Dlc).u"))
    $plugin = Find-ModDir $dlc.Mod $modRoots
    if (-not $installed) {
        # No DLC and no plugin is the ordinary case and says nothing. A plugin without its DLC is
        # left to section 7, which knows whether it is switched on: installed is harmless, enabled
        # is not.
        if ($plugin) { $strayPlugins += $dlc }
        continue
    }
    if ($plugin -and $plugin.Loadable) {
        Write-Ok "$($dlc.Title) DLC is installed, and its Highlander plugin $($dlc.Mod) is at $($plugin.Path)"
        if ($highlanderReady) { $dlcPlugins += $dlc.Mod }
    } elseif ($plugin) {
        Write-Problem "$($dlc.Title) DLC is installed and its Highlander plugin is at $($plugin.Path), one folder too deep for the game to find it: a mod root is read exactly one level down. Move the folder holding $($dlc.Mod).XComMod straight into $modsDir."
    } else {
        Write-Problem "$($dlc.Title) DLC is installed, so the Community Highlander needs its $($dlc.Title) plugin as well. On Steam, subscribe to $($dlc.Url); on any other copy, download it from $HighlanderGitUrl and unzip it into $modsDir so that $($dlc.Mod).XComMod sits one folder down. The DLC ships a script package built against the unmodified game, which the Highlander has replaced; the plugin is the rebuilt one, and its authors call it required whenever the DLC is there."
    }
}

# ---- 7. the user config: which mods the game loads --------------------------------------------

if (-not (Test-Path -LiteralPath $configDir)) {
    if ($mayWrite) {
        New-Item -ItemType Directory -Force -Path $configDir | Out-Null
        Write-Note "Created $configDir. The game had not been run on this machine yet; it fills the rest in on first launch."
    } else {
        Write-Note "$configDir does not exist yet: the game has not been run on this machine. Run the game once, then run this check again."
    }
}

$wanted = @($ModName)
if ($highlanderReady) { $wanted += $HighlanderName }
$wanted += $dlcPlugins

if (Test-Path -LiteralPath $modOptionsPath) {
    $cfg = Read-ConfigText $modOptionsPath
    $optsRaw = $cfg.Text
    $optsEnc = $cfg.Encoding
} else {
    $optsRaw = "[Engine.XComModOptions]`r`n`r`n"
    $optsEnc = [System.Text.Encoding]::ASCII
}
if ($optsRaw -notmatch '(?m)^\[Engine\.XComModOptions\]') {
    $optsRaw = "[Engine.XComModOptions]`r`n`r`n" + $optsRaw
}
$optsChanged = $false
foreach ($mod in $wanted) {
    # The launcher writes ActiveMods=Name; our build tooling writes ActiveMods="Name". Both load.
    if ($optsRaw -match ('(?m)^ActiveMods="?' + [regex]::Escape($mod) + '"?\s*$')) {
        Write-Ok "$mod is enabled in $modOptionsPath"
        continue
    }
    if ($mayWrite) {
        $optsRaw = [regex]::Replace(
            $optsRaw,
            '(?m)^(\[Engine\.XComModOptions\][^\r\n]*\r?\n)',
            ('${1}ActiveMods=' + $mod + "`r`n"),
            1)
        $optsChanged = $true
        Write-Fixed "$mod was not enabled; added it to ActiveMods in $modOptionsPath"
    } else {
        Write-Problem "$mod is not enabled in $modOptionsPath. Run this script again without -CheckOnly and with the game closed, or tick it in the mod launcher."
    }
}

# A DLC plugin switched on for a DLC this copy does not have. Only reported, never removed: the
# list is the player's and their launcher's, and this script switches mods ON, never off.
foreach ($dlc in $strayPlugins) {
    if ($optsRaw -match ('(?m)^ActiveMods="?' + [regex]::Escape($dlc.Mod) + '"?\s*$')) {
        Write-Problem "$($dlc.Mod) is enabled in $modOptionsPath, but the $($dlc.Title) DLC is not installed on this copy of the game. Untick it in your launcher, or delete its ActiveMods line from that file: its authors warn it breaks a game that has no $($dlc.Title)."
    } else {
        Write-Note "$($dlc.Mod) is installed but not enabled, which is right: it is the Highlander's $($dlc.Title) plugin and this copy of the game has no $($dlc.Title) DLC."
    }
}

if ($optsChanged) {
    Write-ConfigText $modOptionsPath $optsRaw $optsEnc
}

# ---- 8. the user config: where the game looks for mods ----------------------------------------

# ActiveMods is half the rule: a mod loads when it is listed there AND its folder is under a
# ModRootDirs root. The game ships the local Mods root in DefaultEngine.ini, but the user
# XComEngine.ini has been seen rewritten with only the workshop path, which drops every locally
# installed mod (ours) while every workshop mod keeps loading. Repair only an EXISTING file: when
# the game has never run there is no user file yet, and the one it generates carries the root.
$modsRoot = '../../XcomGame/Mods/'
if (Test-Path -LiteralPath $engineIniPath) {
    $cfg = Read-ConfigText $engineIniPath
    $engRaw = $cfg.Text
    # Slash direction and casing vary between what the game writes and what it ships, so match
    # on shape; the absolute spelling of the same folder counts too.
    $relativeRoot = '(?m)^ModRootDirs=\.\.[\\/]\.\.[\\/]XcomGame[\\/]Mods[\\/]?\s*$'
    $absoluteRoot = '(?m)^ModRootDirs=' + [regex]::Escape($modsDir) + '[\\/]?\s*$'
    if ($engRaw -match $relativeRoot -or $engRaw -imatch $absoluteRoot) {
        Write-Ok "Local Mods folder is a mod root in $engineIniPath"
    } elseif ($mayWrite) {
        if ($engRaw -match '(?m)^\[Engine\.DownloadableContentEnumerator\]') {
            $engRaw = [regex]::Replace(
                $engRaw,
                '(?m)^(\[Engine\.DownloadableContentEnumerator\][^\r\n]*\r?\n)',
                ('${1}ModRootDirs=' + $modsRoot + "`r`n"),
                1)
        } else {
            $engRaw = $engRaw.TrimEnd() + "`r`n`r`n[Engine.DownloadableContentEnumerator]`r`nModRootDirs=$modsRoot`r`n"
        }
        Write-ConfigText $engineIniPath $engRaw $cfg.Encoding
        Write-Fixed "Restored ModRootDirs=$modsRoot in $engineIniPath; without it the game never looks in XComGame\Mods, whatever ActiveMods says."
    } else {
        Write-Problem "$engineIniPath has no ModRootDirs entry for the local Mods folder, so the companion cannot load. Run this script again without -CheckOnly and with the game closed."
    }
}

# ---- 9. advice ----------------------------------------------------------------------------------

$tickList = @($ModName, 'the Community Highlander') + $dlcPlugins
$ticks = if ($tickList.Count -gt 1) {
    ($tickList[0..($tickList.Count - 2)] -join ', ') + ' and ' + $tickList[-1]
} else { $tickList[0] }
Write-Note "If you launch through the Alternative Mod Launcher, tick $ticks there as well: it rewrites this list from its own selection on every launch."
Write-Note 'Launch through your store or a mod launcher. If you start XCom2.exe directly, add -review to its arguments, or the game boots its developer shell where nothing reads.'

# ---- summary --------------------------------------------------------------------------------------

Write-Host ''
$summary = "Summary: $($script:Passed) OK, $($script:Fixed) fixed, $($script:Failed) problems."
if ($script:Failed -gt 0) {
    Write-Host "$summary Fix the PROBLEM lines above, then run this check again."
    exit 1
}
if ($script:Fixed -gt 0) {
    Write-Host "$summary Start the game; X2Access should speak from the main menu on."
} else {
    Write-Host "$summary Everything is in place."
}
exit 0
