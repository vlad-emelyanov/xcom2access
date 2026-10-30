# Drive the game from a shell: build, launch, press something, read what the mod said.
#
#   tools/xctl.ps1 build              rebuild the ASI with the dev harness and install it
#   tools/xctl.ps1 stop | start       close the game | launch it, return at the first speech
#   tools/xctl.ps1 status             game process, frame pump, controller layer, screen stack
#   tools/xctl.ps1 key Down,Enter     real keypresses; prints the speech each one caused
#   tools/xctl.ps1 pad A,DDown        virtual controller presses; same output
#   tools/xctl.ps1 sr                 speech since the last command
#   tools/xctl.ps1 wait <regex>       block until a spoken line matches
#   tools/xctl.ps1 log [tail]         the ASI log
#   tools/xctl.ps1 shot               screenshot; prints the path
#
# Speech is read back as "SR> line", and a press returns once the speech has gone quiet.
# Exit codes: 0 ok, 1 failed or timed out, 5 the game crashed or hung.
Param(
    [Parameter(Mandatory = $true, Position = 0)] [string] $Command,
    [Parameter(Position = 1)] [string] $Arg = "",
    [int] $Hold = 80,          # ms a virtual button stays down
    [int] $Settle = 1500,      # longest wait for a press to say something
    [int] $Quiet = 300,        # ms of silence that ends the wait
    [int] $Timeout = 120
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$envFile = Join-Path $root ".env"
$gamePath = $env:XCOM2_GAME_PATH
$sdkPath = $env:XCOM2_SDK_PATH
if (Test-Path $envFile) {
    foreach ($line in Get-Content $envFile) {
        if ($line -match '^\s*XCOM2_GAME_PATH\s*=\s*"?([^"]+?)"?\s*$') { $gamePath = $Matches[1] }
        if ($line -match '^\s*XCOM2_SDK_PATH\s*=\s*"?([^"]+?)"?\s*$') { $sdkPath = $Matches[1] }
    }
}
$win64 = Join-Path $gamePath "Binaries/Win64"
$scripts = Join-Path $win64 "scripts"
$curl = Join-Path $env:SystemRoot "System32/curl.exe"
$port = 8774
$cursorFile = Join-Path $env:TEMP "xctl-speech-cursor.txt"

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction Ignore
    if ($onPath) { return $onPath.Source }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -property installationPath
    return Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}

function Invoke-Harness([string] $Path, [int] $MaxTime = 20) {
    $out = & $curl -sS --max-time $MaxTime "http://127.0.0.1:$port/$Path" 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ($out -join "`n")
}

function Test-Alive {
    if (-not (Get-Process XCom2 -ErrorAction Ignore)) {
        Write-Output "GAME CRASHED: XCom2 is not running"
        exit 5
    }
    $health = Invoke-Harness "health" 5
    if ($health -and $health -match 'pump-age-ms[:= ]+(\d+)' -and [int]$Matches[1] -gt 8000) {
        Write-Output "GAME HUNG: no frame for $($Matches[1]) ms"
        exit 5
    }
}

function Read-Speech {
    $since = 0
    if (Test-Path $cursorFile) { $since = [int](Get-Content $cursorFile) }
    $text = Invoke-Harness "speech?since=$since"
    if ($null -eq $text) { return @() }
    $lines = @()
    foreach ($line in ($text -split "`n")) {
        if ($line -match '^cursor:\s*(\d+)') { Set-Content -Path $cursorFile -Value $Matches[1] }
        elseif ($line -match '^\d+:\s?(.*)$') { $lines += $Matches[1] }
    }
    return $lines
}

# Waits for speech to start, then for it to stop. A press that says nothing costs the full $Settle.
function Wait-Speech {
    $all = @()
    $started = Get-Date
    $last = $null
    while ($true) {
        Start-Sleep -Milliseconds 50
        $new = @(Read-Speech)
        if ($new.Count) { $all += $new; $last = Get-Date }
        $now = Get-Date
        if ($last -and ($now - $last).TotalMilliseconds -ge $Quiet) { break }
        if (-not $last -and ($now - $started).TotalMilliseconds -ge $Settle) { break }
    }
    return $all
}

function Show-Speech([string[]] $Lines) {
    if (-not $Lines.Count) { Write-Output "(silence)"; return }
    foreach ($l in $Lines) { Write-Output "SR> $l" }
}

switch ($Command) {
    "build" {
        if (Get-Process XCom2 -ErrorAction Ignore) { throw "the game is running; run stop first" }
        $cmake = Find-CMake
        $pwsh = (Get-Command pwsh).Source
        & $cmake -S $root -B (Join-Path $root "build") -A x64 "-DXCOM2_GAME_PATH=$gamePath" "-DXCOM2_SDK_PATH=$sdkPath" "-DPWSH_EXECUTABLE=$pwsh" -DX2ACCESS_DEVSERVER=ON | Out-Null
        if ($LASTEXITCODE -ne 0) { exit 1 }
        & $cmake --build (Join-Path $root "build") --config Release --target X2Access -- /m /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) { exit 1 }
        foreach ($f in "X2Access.asi", "X2Access.pdb") {
            $src = Join-Path $root "build/bin/Release/$f"
            if (Test-Path $src) { Copy-Item $src (Join-Path $scripts $f) -Force }
        }
        New-Item -ItemType File -Force (Join-Path $scripts "X2Access.devserver") | Out-Null
        Write-Output "installed and armed"
    }
    "stop" {
        if (Get-Process XCom2 -ErrorAction Ignore) { Stop-Process -Name XCom2 -Force }
        Wait-Process -Name XCom2 -Timeout 30 -ErrorAction Ignore
        Remove-Item $cursorFile -ErrorAction Ignore
        Write-Output "stopped"
    }
    "start" {
        if (Get-Process XCom2 -ErrorAction Ignore) { throw "the game is already running" }
        Remove-Item $cursorFile -ErrorAction Ignore
        Start-Process -FilePath (Join-Path $win64 "XCom2.exe") -ArgumentList "-review -noRedScreens -noStartUpMovies" -WorkingDirectory $win64
        $deadline = (Get-Date).AddSeconds($Timeout)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 1
            if (-not (Get-Process XCom2 -ErrorAction Ignore)) { Write-Output "GAME CRASHED during startup"; exit 5 }
            $lines = @(Read-Speech)
            if ($lines.Count) { Show-Speech $lines; exit 0 }
        }
        Write-Output "timed out waiting for the first speech"
        exit 1
    }
    "status" {
        Test-Alive
        Invoke-Harness "health" 5
        Invoke-Harness "pad"
        Invoke-Harness "status"
    }
    "key" {
        # Insert and the numpad operators are JAWS commands: NumPad minus turns on the JAWS cursor,
        # after which arrows stop reaching any window, and that outlives a game restart.
        foreach ($k in ($Arg -split ",")) {
            if ($k -match '^(Insert|Add|Subtract|Multiply|Decimal|NumPad\d)$') { throw "refused, JAWS key: $k" }
        }
        Test-Alive
        [void](Read-Speech)
        foreach ($k in ($Arg -split ",")) {
            [void](Invoke-Harness "input?keys=$k&hold=150")
            Write-Output "-- $k"
            Show-Speech (Wait-Speech)
        }
        Test-Alive
    }
    "pad" {
        Test-Alive
        [void](Read-Speech)
        foreach ($b in ($Arg -split ",")) {
            $reply = Invoke-Harness "pad?press=$b&hold=$Hold"
            if ($reply -notmatch 'queued') { Write-Output $reply; exit 1 }
            Write-Output "-- $b"
            Show-Speech (Wait-Speech)
        }
        Test-Alive
    }
    "sr" { Show-Speech (@(Read-Speech)) }
    "wait" {
        $deadline = (Get-Date).AddSeconds($Timeout)
        while ((Get-Date) -lt $deadline) {
            Test-Alive
            foreach ($l in @(Read-Speech)) {
                Write-Output "SR> $l"
                if ($l -match $Arg) { exit 0 }
            }
            Start-Sleep -Milliseconds 250
        }
        Write-Output "timed out"
        exit 1
    }
    "log" {
        $tail = if ($Arg) { $Arg } else { "60" }
        Invoke-Harness "log?tail=$tail"
    }
    "shot" { Invoke-Harness "screenshot" 30 }
    default { throw "unknown command: $Command" }
}
