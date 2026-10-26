param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $BuildDirectory).Path
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('VeilSmoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$sourceExecutable = @('XiaoChuang.exe', 'VeilBrowser.exe') |
    ForEach-Object { Join-Path $source $_ } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $sourceExecutable) { throw "Missing Veil executable in $source" }
Copy-Item $sourceExecutable (Join-Path $tempRoot 'VeilBrowser.exe')
Copy-Item (Join-Path $source 'WebView2Loader.dll') $tempRoot
@'
[Settings]
AlwaysOnTop=1
ImmersionStyle=1
AutoHideOpacityPercent=40
HomeUrl=about:blank
'@ | Set-Content -LiteralPath (Join-Path $tempRoot 'config.ini') -Encoding Unicode

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
public static class VeilNative {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; public POINT(int x, int y) { X=x; Y=y; } }
    [StructLayout(LayoutKind.Sequential)] public struct MSG { public IntPtr hwnd; public uint message; public UIntPtr wParam; public IntPtr lParam; public uint time; public POINT pt; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct WNDCLASSEX {
        public uint cbSize, style; public IntPtr lpfnWndProc; public int cbClsExtra, cbWndExtra;
        public IntPtr hInstance, hIcon, hCursor, hbrBackground;
        [MarshalAs(UnmanagedType.LPWStr)] public string lpszMenuName;
        [MarshalAs(UnmanagedType.LPWStr)] public string lpszClassName;
        public IntPtr hIconSm;
    }
    public delegate IntPtr WndProc(IntPtr h, uint m, UIntPtr w, IntPtr l);
    static WndProc probeProc = ProbeWindowProc;
    public static int ProbeClicks;
    static IntPtr ProbeWindowProc(IntPtr h, uint m, UIntPtr w, IntPtr l) {
        if (m == 0x0202) Interlocked.Increment(ref ProbeClicks);
        return DefWindowProc(h,m,w,l);
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern ushort RegisterClassEx(ref WNDCLASSEX c);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr CreateWindowEx(uint ex, string cls, string title, uint style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
    [DllImport("user32.dll")] public static extern IntPtr DefWindowProc(IntPtr h, uint m, UIntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool DestroyWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool PeekMessage(out MSG msg, IntPtr h, uint min, uint max, uint remove);
    [DllImport("user32.dll")] static extern bool TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref MSG msg);
    public static IntPtr CreateProbe(int x, int y, int w, int h) {
        var name = "VeilSmokeProbeWindow";
        var c = new WNDCLASSEX(); c.cbSize=(uint)Marshal.SizeOf(c); c.lpfnWndProc=Marshal.GetFunctionPointerForDelegate(probeProc); c.hInstance=GetModuleHandle(null); c.lpszClassName=name; c.hbrBackground=(IntPtr)6;
        RegisterClassEx(ref c);
        return CreateWindowEx(0,name,"Veil Smoke Underlay",0x10CF0000,x,y,w,h,IntPtr.Zero,IntPtr.Zero,c.hInstance,IntPtr.Zero);
    }
    public static void Pump() { MSG m; while(PeekMessage(out m,IntPtr.Zero,0,0,1)){ TranslateMessage(ref m); DispatchMessage(ref m); } }
    [DllImport("user32.dll", SetLastError=true)] public static extern bool GetWindowDisplayAffinity(IntPtr h, out uint affinity);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll", SetLastError=true, ExactSpelling=true, EntryPoint="SendMessageTimeoutW")] static extern IntPtr SendMessageTimeout(IntPtr h, uint m, UIntPtr w, IntPtr l, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT rect);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true, ExactSpelling=true, EntryPoint="SendMessageTimeoutW")] static extern IntPtr SendMessageTimeoutText(IntPtr h, uint m, UIntPtr w, string l, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true, ExactSpelling=true, EntryPoint="SendMessageTimeoutW")] static extern IntPtr SendMessageTimeoutBuffer(IntPtr h, uint m, UIntPtr w, StringBuilder l, uint flags, uint timeout, out UIntPtr result);
    public static IntPtr SendMessageChecked(IntPtr h, uint m, UIntPtr w, IntPtr l) {
        UIntPtr result;
        if (SendMessageTimeout(h,m,w,l,2,5000,out result)==IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        return (IntPtr)(long)result.ToUInt64();
    }
    public static IntPtr SendMessageTextChecked(IntPtr h, uint m, UIntPtr w, string l) {
        UIntPtr result;
        if (SendMessageTimeoutText(h,m,w,l,2,5000,out result)==IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        return (IntPtr)(long)result.ToUInt64();
    }
    public static IntPtr SendMessageBufferChecked(IntPtr h, uint m, UIntPtr w, StringBuilder l) {
        UIntPtr result;
        if (SendMessageTimeoutBuffer(h,m,w,l,2,5000,out result)==IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        return (IntPtr)(long)result.ToUInt64();
    }

    [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtr(IntPtr h, int index);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetLastActivePopup(IntPtr h);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool GetLayeredWindowAttributes(IntPtr h, out uint key, out byte alpha, out uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
}
'@

function Wait-Condition([scriptblock]$Condition, [int]$Timeout = 15000, [bool]$AllowExited = $false) {
    $until = [Environment]::TickCount64 + $Timeout
    do {
        [VeilNative]::Pump()
        if (-not $AllowExited -and $script:process -and $script:process.HasExited) {
            $recentLog = @((Log-Lines) | Select-Object -Last 20) -join "`n"
            throw "Veil exited during smoke test: $($script:process.ExitCode)`nRecent log:`n$recentLog"
        }
        if (& $Condition) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([Environment]::TickCount64 -lt $until)
    $caller = (Get-PSCallStack | Select-Object -Skip 1 -First 1).Location
    $recentLog = @((Log-Lines) | Select-Object -Last 12) -join "`n"
    throw "Timed out after $Timeout ms at $caller`nRecent log:`n$recentLog"
}
function Window-Text([IntPtr]$Handle) {
    $builder = New-Object Text.StringBuilder 2048
    [void][VeilNative]::SendMessageBufferChecked($Handle, 0x000D, [UIntPtr]$builder.Capacity, $builder)
    return $builder.ToString()
}
function Set-Address([IntPtr]$Window, [string]$Url) {
    $address = [VeilNative]::GetDlgItem($Window, 1001)
    if ($address -eq [IntPtr]::Zero) { throw 'Address control not found' }
    [void][VeilNative]::SendMessageTextChecked($address, 0x000C, [UIntPtr]::Zero, $Url)
    $actual = Window-Text $address
    if ($actual -ne $Url) { throw "Address readback mismatch: expected '$Url', got '$actual'" }
    [void][VeilNative]::SendMessageChecked($Window, 0x111, [UIntPtr]1002, [IntPtr]::Zero)
}
function Press-Key([byte]$Key) {
    [VeilNative]::keybd_event($Key, 0, 0, [UIntPtr]::Zero)
    [VeilNative]::keybd_event($Key, 0, 2, [UIntPtr]::Zero)
}
function Log-Lines {
    $logDirectory = Join-Path $script:tempRoot 'logs'
    if (-not (Test-Path -LiteralPath $logDirectory)) { return @() }
    $log = Get-ChildItem -LiteralPath $logDirectory -Filter '*.log' -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $log) { return @() }
    return @(Get-Content -LiteralPath $log.FullName)
}
function Event-Count([string]$Pattern) {
    return @((Log-Lines) | Where-Object { $_ -match $Pattern }).Count
}
function Navigation-StartCount {
    return Event-Count 'navigation\.start .* id=\d+(?: |$)'
}
function Latest-NavigationStartId {
    $lines = @(Log-Lines)
    for ($index = $lines.Count - 1; $index -ge 0; --$index) {
        if ($lines[$index] -match 'navigation\.start .* id=(\d+)(?: |$)') {
            return [UInt64]$Matches[1]
        }
    }
    return $null
}
function Navigation-Succeeded([UInt64]$Id) {
    $pattern = "navigation\.completed .* id=$Id success=1(?: |$)"
    return @((Log-Lines) | Where-Object { $_ -match $pattern }).Count -gt 0
}
function Invoke-NavigationAndWait([string]$Name, [scriptblock]$Action, [scriptblock]$Extra, [int]$Timeout = 30000) {
    $beforeCount = Navigation-StartCount
    & $Action
    $script:pendingNavigationId = $null
    try {
        [void](Wait-Condition {
            if ((Navigation-StartCount) -gt $beforeCount) {
                $candidate = Latest-NavigationStartId
                $script:pendingNavigationId = $candidate
                return $true
            }
            return $false
        } 10000)
    } catch { throw "$Name did not start: $($_.Exception.Message)" }
    $expectedId = $script:pendingNavigationId
    try {
        $succeeded = Wait-Condition { (Navigation-Succeeded $expectedId) -and (& $Extra) } $Timeout
        Wait-NavigationIdle
        return $succeeded
    } catch { throw "$Name id=$expectedId did not complete: $($_.Exception.Message)" }
}

function Wait-NavigationIdle([int]$StableMilliseconds = 1000, [int]$Timeout = 10000) {
    $deadline = [Environment]::TickCount64 + $Timeout
    $lastCount = Navigation-StartCount
    $stableSince = [Environment]::TickCount64
    do {
        [VeilNative]::Pump()
        if ($script:process -and $script:process.HasExited) { throw 'Veil exited during idle wait' }
        $count = Navigation-StartCount
        if ($count -ne $lastCount) {
            $lastCount = $count
            $stableSince = [Environment]::TickCount64
        } elseif ([Environment]::TickCount64 - $stableSince -ge $StableMilliseconds) {
            return
        }
        Start-Sleep -Milliseconds 100
    } while ([Environment]::TickCount64 -lt $deadline)
    throw 'Navigation did not become idle'
}

$results = [ordered]@{}
$controllerReadyBefore = Event-Count 'webview\.controller\.ready(?: |$)'
$process = Start-Process -FilePath (Join-Path $tempRoot 'VeilBrowser.exe') -WorkingDirectory $tempRoot -PassThru
$profilePattern = "VeilBrowser-$($process.Id)-*"
$window = [IntPtr]::Zero
$probe = [IntPtr]::Zero
try {
    $results.WindowCreated = Wait-Condition {
        $process.Refresh()
        $process.MainWindowHandle -ne [IntPtr]::Zero -or $process.HasExited
    }
    $process.Refresh()
    if ($process.HasExited) { throw "Veil exited before creating a window: $($process.ExitCode)" }
    $window = $process.MainWindowHandle
    [void][VeilNative]::SetWindowPos($window, [IntPtr](-1), 100, 100, 1000, 700, 0x0040)
    $results.WebViewControllerReady = Wait-Condition {
        (Event-Count 'webview\.controller\.ready(?: |$)') -gt $controllerReadyBefore
    } 30000
    Wait-NavigationIdle

    $affinity = 0u
    $results.CaptureAffinity = [VeilNative]::GetWindowDisplayAffinity($window, [ref]$affinity) -and $affinity -eq 0x11
    $results.CaptureAffinityValue = ('0x{0:X}' -f $affinity)

    $controls = @(1012,1013,1014,1015,1016,1003,1004,1005,1006 | ForEach-Object { [VeilNative]::GetDlgItem($window, $_) })
    $results.RequiredControls = $controls -notcontains [IntPtr]::Zero
    $results.BilingualToolbar = (Window-Text ([VeilNative]::GetDlgItem($window,1012))).Contains('Kembali') -and
        (Window-Text ([VeilNative]::GetDlgItem($window,1015))).Contains('Atas') -and
        (Window-Text ([VeilNative]::GetDlgItem($window,1016))).Contains('Tembus')

    $probe = [VeilNative]::CreateProbe(100,100,1000,700)
    if ($probe -eq [IntPtr]::Zero) { throw 'Could not create z-order/click probe window' }
    [void][VeilNative]::SetWindowPos($probe,[IntPtr]::Zero,100,100,1000,700,0x0040)
    [void][VeilNative]::SetWindowPos($window,[IntPtr](-1),100,100,1000,700,0x0040)
    $point = [VeilNative+POINT]::new(300,300)

    [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1015,[IntPtr]::Zero)
    [void][VeilNative]::BringWindowToTop($probe)
    [void][VeilNative]::SetForegroundWindow($probe)
    $results.TopmostTurnsOff = Wait-Condition {
        $root = [VeilNative]::GetAncestor([VeilNative]::WindowFromPoint($point),2)
        $root -eq $probe -and ([VeilNative]::GetWindowLongPtr($window,-20).ToInt64() -band 0x8) -eq 0
    }
    [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1015,[IntPtr]::Zero)
    [void][VeilNative]::BringWindowToTop($probe)
    $results.TopmostTurnsOn = Wait-Condition {
        $root = [VeilNative]::GetAncestor([VeilNative]::WindowFromPoint($point),2)
        $root -eq $window -and ([VeilNative]::GetWindowLongPtr($window,-20).ToInt64() -band 0x8) -ne 0
    }

    [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1010,[IntPtr]::Zero)
    $settings = [IntPtr]::Zero
    $results.SettingsCaptureAffinity = Wait-Condition {
        $script:settings = [VeilNative]::GetLastActivePopup($window)
        if ($script:settings -eq $window -or $script:settings -eq [IntPtr]::Zero) { return $false }
        if ((Window-Text $script:settings) -ne 'Settings / Pengaturan · Veil Browser') { return $false }
        $popupAffinity=0u
        return [VeilNative]::GetWindowDisplayAffinity($script:settings,[ref]$popupAffinity) -and $popupAffinity -eq 0x11
    }
    $settings = $script:settings
    if ($settings -ne [IntPtr]::Zero -and $settings -ne $window) { [void][VeilNative]::SendMessageChecked($settings,0x10,[UIntPtr]::Zero,[IntPtr]::Zero) }

    $results.FirstHttpsNavigation = Invoke-NavigationAndWait 'FirstHttpsNavigation' {
        Set-Address $window 'https://example.com/'
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://example.com/?$'
    }
    Wait-NavigationIdle

    $results.SecondHttpsNavigation = Invoke-NavigationAndWait 'SecondHttpsNavigation' {
        Set-Address $window 'https://www.iana.org/help/example-domains'
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://www.iana.org/help/example-domains/?$'
    }

    $back = [VeilNative]::GetDlgItem($window,1012)
    $forward = [VeilNative]::GetDlgItem($window,1013)
    $results.BackAvailable = Wait-Condition { [VeilNative]::IsWindowEnabled($back) }
    $results.BackWorks = Invoke-NavigationAndWait 'BackWorks' {
        [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1012,[IntPtr]::Zero)
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://example.com/?$'
    }

    $results.ForwardAvailable = Wait-Condition { [VeilNative]::IsWindowEnabled($forward) }
    $results.ForwardWorks = Invoke-NavigationAndWait 'ForwardWorks' {
        [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1013,[IntPtr]::Zero)
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://www.iana.org/help/example-domains/?$'
    }

    $results.ReloadWorks = Invoke-NavigationAndWait 'ReloadWorks' {
        [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1014,[IntPtr]::Zero)
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://www.iana.org/help/example-domains/?$'
    }

    $results.SearchWorks = Invoke-NavigationAndWait 'SearchWorks' {
        Set-Address $window 'openai'
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))).StartsWith('https://duckduckgo.com/?q=openai')
    }

    $tabs = [VeilNative]::GetDlgItem($window,1011)
    $add = [VeilNative]::GetDlgItem($window,1003)
    1..4 | ForEach-Object { [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1003,[IntPtr]::Zero) }
    $tabCount = [VeilNative]::SendMessageChecked($tabs,0x1304,[UIntPtr]::Zero,[IntPtr]::Zero).ToInt32()
    $results.TabLimit = $tabCount -eq 5 -and -not [VeilNative]::IsWindowEnabled($add)
    $tabRect = New-Object VeilNative+RECT
    if (-not [VeilNative]::GetClientRect($tabs,[ref]$tabRect)) { throw 'Could not read tab bounds' }
    $x = $tabRect.Right - 8; $y = [int](($tabRect.Top + $tabRect.Bottom) / 2)
    $packed = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [void][VeilNative]::SendMessageChecked($tabs,0x0201,[UIntPtr]1,$packed)
    $results.TabCloseWorks = Wait-Condition { [VeilNative]::SendMessageChecked($tabs,0x1304,[UIntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 4 }

    [void][VeilNative]::SetCursorPos(10,10)
    [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1016,[IntPtr]::Zero)
    $results.PassThroughTurnsOn = Wait-Condition { ([VeilNative]::GetWindowLongPtr($window,-20).ToInt64() -band 0x20) -ne 0 }
    $color=0u; $alpha=[byte]0; $layerFlags=0u
    $results.OpacityApplied = [VeilNative]::GetLayeredWindowAttributes($window,[ref]$color,[ref]$alpha,[ref]$layerFlags) -and $alpha -eq 102
    [VeilNative]::ProbeClicks = 0
    [void][VeilNative]::SetCursorPos(300,300)
    [VeilNative]::mouse_event(0x0002,0,0,0,[UIntPtr]::Zero)
    [VeilNative]::mouse_event(0x0004,0,0,0,[UIntPtr]::Zero)
    $results.PointerActuallyPassesThrough = Wait-Condition { [VeilNative]::ProbeClicks -gt 0 }
    Press-Key 0x1B
    $results.EscapeRestoresInteraction = Wait-Condition { ([VeilNative]::GetWindowLongPtr($window,-20).ToInt64() -band 0x20) -eq 0 }

    [void][VeilNative]::SetForegroundWindow($probe)
    Press-Key 0x39
    $results.HideHotkeyWorks = Wait-Condition { -not [VeilNative]::IsWindowVisible($window) }
    Start-Sleep -Milliseconds 300
    Press-Key 0x39
    $results.ShowHotkeyWorks = Wait-Condition { [VeilNative]::IsWindowVisible($window) }

    [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1004,[IntPtr]::Zero)
    $results.MinimizeWorks = Wait-Condition { [VeilNative]::IsIconic($window) }
    [void][VeilNative]::ShowWindow($window,9)
    $results.RestoreAfterMinimizeWorks = Wait-Condition { -not [VeilNative]::IsIconic($window) -and [VeilNative]::IsWindowVisible($window) }
    [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1005,[IntPtr]::Zero)
    $results.MaximizeWorks = Wait-Condition { (Window-Text ([VeilNative]::GetDlgItem($window,1005))) -eq '❐' }
    [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1005,[IntPtr]::Zero)
    $results.RestoreFromMaximizeWorks = Wait-Condition { (Window-Text ([VeilNative]::GetDlgItem($window,1005))) -eq '□' }

    $createdProfiles = @(Get-ChildItem ([IO.Path]::GetTempPath()) -Directory -Filter $profilePattern -ErrorAction SilentlyContinue | ForEach-Object FullName)
    $results.TemporaryProfileCreated = $createdProfiles.Count -eq 1

    [void][VeilNative]::SendMessageChecked($window,0x111,[UIntPtr]1006,[IntPtr]::Zero)
    $results.CloseWorks = $process.WaitForExit(10000)
    $results.TemporaryProfileDeleted = Wait-Condition { @($createdProfiles | Where-Object { Test-Path $_ }).Count -eq 0 } 10000 $true
    $config = Get-Content -LiteralPath (Join-Path $tempRoot 'config.ini') -Raw
    $results.NoTabSessionPersisted = $config -match '(?m)^TabCount=0\r?$' -and $config -notmatch '(?m)^TabUrl\d+='
} finally {
    if (-not $process.HasExited) { $process.Kill($true); [void]$process.WaitForExit(5000) }
    if ($probe -ne [IntPtr]::Zero) { [void][VeilNative]::DestroyWindow($probe) }
    for ($attempt = 0; $attempt -lt 20 -and (Test-Path $tempRoot); $attempt++) {
        try { Remove-Item -LiteralPath $tempRoot -Recurse -Force } catch { Start-Sleep -Milliseconds 250 }
    }
}

$results.SmokeTempRootDeleted = -not (Test-Path -LiteralPath $tempRoot)

$results.GetEnumerator() | ForEach-Object { '{0}={1}' -f $_.Key, $_.Value }
if ($results.Values -contains $false) { exit 2 }
$global:LASTEXITCODE = 0
