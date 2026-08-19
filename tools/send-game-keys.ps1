# Send keys to XCOM 2 via raw SendInput SCANCODE events (hardware-identical; WScript.Shell
# SendKeys delivered letters but silently dropped extended keys — arrows never reached the
# game's WNDPROC). Tokens: UP DOWN LEFT RIGHT ENTER ESC DEL BS TAB C LT GT (LT/GT = Shift+comma
# / Shift+period, the tile reader's floor keys).
Param(
    [Parameter(Mandatory = $true)] [string[]] $Keys,
    [int] $DelayMs = 500
)
$proc = Get-Process XCom2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Output "NO-GAME"; exit 1 }

Add-Type -Namespace Native -Name Input -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr hWnd);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[StructLayout(LayoutKind.Sequential)]
public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public System.IntPtr dwExtraInfo; }
[StructLayout(LayoutKind.Explicit, Size = 40)]
public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
[DllImport("user32.dll", SetLastError = true)] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
public const uint KEYEVENTF_SCANCODE = 0x0008;
public const uint KEYEVENTF_KEYUP = 0x0002;
public const uint KEYEVENTF_EXTENDEDKEY = 0x0001;
public static void Tap(ushort scan, bool extended) {
    INPUT[] seq = new INPUT[2];
    seq[0].type = 1; seq[0].ki.wScan = scan;
    seq[0].ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    seq[1] = seq[0]; seq[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(2, seq, Marshal.SizeOf(typeof(INPUT)));
}
public static void Chord(ushort modScan, ushort scan, bool extended) {
    INPUT[] seq = new INPUT[4];
    seq[0].type = 1; seq[0].ki.wScan = modScan; seq[0].ki.dwFlags = KEYEVENTF_SCANCODE;
    seq[1].type = 1; seq[1].ki.wScan = scan;
    seq[1].ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    seq[2] = seq[1]; seq[2].ki.dwFlags |= KEYEVENTF_KEYUP;
    seq[3] = seq[0]; seq[3].ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(4, seq, Marshal.SizeOf(typeof(INPUT)));
}
'@

$hwnd = $proc.MainWindowHandle
[void][Native.Input]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 400
if ([Native.Input]::GetForegroundWindow() -ne $hwnd) { Write-Output "FOCUS-FAILED"; exit 2 }

# scan codes: set-1 make codes; arrows/Del are extended
$map = @{
    "UP" = @(0x48, $true); "DOWN" = @(0x50, $true); "LEFT" = @(0x4B, $true); "RIGHT" = @(0x4D, $true)
    "ENTER" = @(0x1C, $false); "ESC" = @(0x01, $false); "DEL" = @(0x53, $true); "BS" = @(0x0E, $false)
    "TAB" = @(0x0F, $false); "C" = @(0x2E, $false)
}
foreach ($k in $Keys) {
    $token = $k.ToUpper()
    if ($token -eq "LT") { [Native.Input]::Chord(0x2A, 0x33, $false) }        # Shift+comma
    elseif ($token -eq "GT") { [Native.Input]::Chord(0x2A, 0x34, $false) }    # Shift+period
    elseif ($map.ContainsKey($token)) {
        $e = $map[$token]
        [Native.Input]::Tap([uint16]$e[0], [bool]$e[1])
    } else { Write-Output "UNKNOWN-KEY $k"; exit 3 }
    Start-Sleep -Milliseconds $DelayMs
}
Write-Output "sent: $($Keys -join ' ')"
