param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $BuildDirectory).Path
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('VeilSmoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
Copy-Item (Join-Path $source 'XiaoChuang.exe') (Join-Path $tempRoot 'VeilBrowser.exe')
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
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, UIntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, UIntPtr w, ref RECT l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, UIntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h, string text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder text, int count);
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
            throw "Veil exited during smoke test: $($script:process.ExitCode)"
        }
        if (& $Condition) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([Environment]::TickCount64 -lt $until)
    throw "Timed out after $Timeout ms"
}
function Window-Text([IntPtr]$Handle) {
    $builder = New-Object Text.StringBuilder 2048
    [void][VeilNative]::GetWindowText($Handle, $builder, $builder.Capacity)
    return $builder.ToString()
}
function Set-Address([IntPtr]$Window, [string]$Url) {
    $address = [VeilNative]::GetDlgItem($Window, 1001)
    [void][VeilNative]::SetWindowText($address, $Url)
    [void][VeilNative]::SendMessage($Window, 0x111, [UIntPtr]1002, [IntPtr]::Zero)
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
function Invoke-NavigationAndWait([scriptblock]$Action, [scriptblock]$Extra, [int]$Timeout = 30000) {
    $beforeCount = Navigation-StartCount
    & $Action
    $script:pendingNavigationId = $null
    [void](Wait-Condition {
        if ((Navigation-StartCount) -gt $beforeCount) {
            $candidate = Latest-NavigationStartId
            $script:pendingNavigationId = $candidate
            return $true
        }
        return $false
    } 10000)
    $expectedId = $script:pendingNavigationId
    return Wait-Condition { (Navigation-Succeeded $expectedId) -and (& $Extra) } $Timeout
}
function Has-VisibleOwnedPopup([IntPtr]$Owner) {
    $popup = [VeilNative]::GetLastActivePopup($Owner)
    return $popup -ne [IntPtr]::Zero -and $popup -ne $Owner -and [VeilNative]::IsWindowVisible($popup)
}

$results = [ordered]@{}
$profilesBefore = @(Get-ChildItem ([IO.Path]::GetTempPath()) -Directory -Filter 'VeilBrowser-*' -ErrorAction SilentlyContinue | ForEach-Object FullName)
$initialStartCount = Navigation-StartCount
$process = Start-Process -FilePath (Join-Path $tempRoot 'VeilBrowser.exe') -WorkingDirectory $tempRoot -PassThru
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
    $script:pendingNavigationId = $null
    [void](Wait-Condition {
        if ((Navigation-StartCount) -gt $initialStartCount) {
            $script:pendingNavigationId = Latest-NavigationStartId
            return $true
        }
        return $false
    } 30000)
    $initialNavigationId = $script:pendingNavigationId
    $results.InitialBlankNavigationCompleted = Wait-Condition {
        (Navigation-Succeeded $initialNavigationId) -and
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -eq 'about:blank'
    } 30000

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

    [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1015,[IntPtr]::Zero)
    [void][VeilNative]::BringWindowToTop($probe)
    [void][VeilNative]::SetForegroundWindow($probe)
    $results.TopmostTurnsOff = Wait-Condition {
        $root = [VeilNative]::GetAncestor([VeilNative]::WindowFromPoint($point),2)
        $root -eq $probe -and ([VeilNative]::GetWindowLongPtr($window,-20).ToInt64() -band 0x8) -eq 0
    }
    [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1015,[IntPtr]::Zero)
    [void][VeilNative]::BringWindowToTop($probe)
    $results.TopmostTurnsOn = Wait-Condition {
        $root = [VeilNative]::GetAncestor([VeilNative]::WindowFromPoint($point),2)
        $root -eq $window -and ([VeilNative]::GetWindowLongPtr($window,-20).ToInt64() -band 0x8) -ne 0
    }

    [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1010,[IntPtr]::Zero)
    $settings = [IntPtr]::Zero
    $results.SettingsCaptureAffinity = Wait-Condition {
        $script:settings = [VeilNative]::GetLastActivePopup($window)
        if ($script:settings -eq $window -or $script:settings -eq [IntPtr]::Zero) { return $false }
        $popupAffinity=0u
        return [VeilNative]::GetWindowDisplayAffinity($script:settings,[ref]$popupAffinity) -and $popupAffinity -eq 0x11
    }
    if ($settings -ne [IntPtr]::Zero -and $settings -ne $window) { [void][VeilNative]::SendMessage($settings,0x10,[UIntPtr]::Zero,[IntPtr]::Zero) }

    $blockedStartCount = Navigation-StartCount
    $unsupportedBefore = Event-Count 'navigation\.unsupported_scheme(?: |$)'
    $address = [VeilNative]::GetDlgItem($window, 1001)
    [void][VeilNative]::SetWindowText($address, 'http://example.com/')
    [void][VeilNative]::PostMessage($window, 0x111, [UIntPtr]1002, [IntPtr]::Zero)
    $results.HttpRejectedBeforeWebView = Wait-Condition {
        (Has-VisibleOwnedPopup $window) -and
        (Event-Count 'navigation\.unsupported_scheme(?: |$)') -gt $unsupportedBefore -and
        (Navigation-StartCount) -eq $blockedStartCount
    }
    $blocked = [VeilNative]::GetLastActivePopup($window)
    if ($blocked -ne [IntPtr]::Zero -and $blocked -ne $window) { [void][VeilNative]::SendMessage($blocked,0x10,[UIntPtr]::Zero,[IntPtr]::Zero) }

    $results.FirstHttpsNavigation = Invoke-NavigationAndWait {
        Set-Address $window 'https://example.com/'
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://example.com/?$'
    }

    $results.SecondHttpsNavigation = Invoke-NavigationAndWait {
        Set-Address $window 'https://www.iana.org/help/example-domains'
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://www.iana.org/help/example-domains/?$'
    }

    $results.BackWorks = Invoke-NavigationAndWait {
        [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1012,[IntPtr]::Zero)
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://example.com/?$'
    }

    $results.ForwardWorks = Invoke-NavigationAndWait {
        [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1013,[IntPtr]::Zero)
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://www.iana.org/help/example-domains/?$'
    }

    $results.ReloadWorks = Invoke-NavigationAndWait {
        [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1014,[IntPtr]::Zero)
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))) -match '^https://www.iana.org/help/example-domains/?$'
    }

    $results.SearchWorks = Invoke-NavigationAndWait {
        Set-Address $window 'openai'
    } {
        (Window-Text ([VeilNative]::GetDlgItem($window,1001))).StartsWith('https://duckduckgo.com/?q=openai')
    }

    $tabs = [VeilNative]::GetDlgItem($window,1011)
    $add = [VeilNative]::GetDlgItem($window,1003)
    1..4 | ForEach-Object { [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1003,[IntPtr]::Zero) }
    $tabCount = [VeilNative]::SendMessage($tabs,0x1304,[UIntPtr]::Zero,[IntPtr]::Zero).ToInt32()
    $results.TabLimit = $tabCount -eq 5 -and -not [VeilNative]::IsWindowEnabled($add)
    $tabRect = New-Object VeilNative+RECT
    [void][VeilNative]::SendMessage($tabs,0x130A,[UIntPtr]4,[ref]$tabRect)
    $x = $tabRect.Right - 2; $y = [int](($tabRect.Top + $tabRect.Bottom) / 2)
    $packed = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [void][VeilNative]::SendMessage($tabs,0x0201,[UIntPtr]1,$packed)
    $results.TabCloseWorks = Wait-Condition { [VeilNative]::SendMessage($tabs,0x1304,[UIntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 4 }

    [void][VeilNative]::SetCursorPos(10,10)
    [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1016,[IntPtr]::Zero)
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
    Press-Key 0x39
    $results.ShowHotkeyWorks = Wait-Condition { [VeilNative]::IsWindowVisible($window) }

    [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1004,[IntPtr]::Zero)
    $results.MinimizeWorks = Wait-Condition { [VeilNative]::IsIconic($window) }
    [void][VeilNative]::ShowWindow($window,9)

    $profilesDuring = @(Get-ChildItem ([IO.Path]::GetTempPath()) -Directory -Filter 'VeilBrowser-*' -ErrorAction SilentlyContinue | ForEach-Object FullName)
    $createdProfiles = @($profilesDuring | Where-Object { $profilesBefore -notcontains $_ })
    $results.TemporaryProfileCreated = $createdProfiles.Count -eq 1

    [void][VeilNative]::SendMessage($window,0x111,[UIntPtr]1006,[IntPtr]::Zero)
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
