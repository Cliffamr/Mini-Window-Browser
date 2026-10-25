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

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class VeilNative {
    [DllImport("user32.dll", SetLastError=true)] public static extern bool GetWindowDisplayAffinity(IntPtr h, out uint affinity);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, UIntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h, string text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtr(IntPtr h, int index);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int command);
}
'@

function Wait-Condition([scriptblock]$Condition, [int]$Timeout = 15000) {
    $until = [Environment]::TickCount64 + $Timeout
    do {
        if (& $Condition) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([Environment]::TickCount64 -lt $until)
    return $false
}

$results = [ordered]@{}
$process = Start-Process -FilePath (Join-Path $tempRoot 'VeilBrowser.exe') -WorkingDirectory $tempRoot -PassThru
$window = [IntPtr]::Zero
try {
    $results.WindowCreated = Wait-Condition {
        $process.Refresh()
        $process.MainWindowHandle -ne [IntPtr]::Zero -or $process.HasExited
    }
    $process.Refresh()
    if ($process.HasExited) { throw "Veil exited before creating a window: $($process.ExitCode)" }
    $window = $process.MainWindowHandle

    $affinity = 0u
    $results.CaptureAffinity = [VeilNative]::GetWindowDisplayAffinity($window, [ref]$affinity) -and $affinity -eq 0x11
    $results.CaptureAffinityValue = ('0x{0:X}' -f $affinity)

    $back = [VeilNative]::GetDlgItem($window, 1012)
    $forward = [VeilNative]::GetDlgItem($window, 1013)
    $reload = [VeilNative]::GetDlgItem($window, 1014)
    $topmost = [VeilNative]::GetDlgItem($window, 1015)
    $pass = [VeilNative]::GetDlgItem($window, 1016)
    $results.RequiredControls = @($back, $forward, $reload, $topmost, $pass) -notcontains [IntPtr]::Zero

    $exStyle = [VeilNative]::GetWindowLongPtr($window, -20).ToInt64()
    $results.TopmostStartsOn = ($exStyle -band 0x8) -ne 0
    [void][VeilNative]::SendMessage($window, 0x111, [UIntPtr]1015, [IntPtr]::Zero)
    $results.TopmostTurnsOff = Wait-Condition {
        ([VeilNative]::GetWindowLongPtr($window, -20).ToInt64() -band 0x8) -eq 0
    }
    [void][VeilNative]::SendMessage($window, 0x111, [UIntPtr]1015, [IntPtr]::Zero)
    $results.TopmostTurnsOn = Wait-Condition {
        ([VeilNative]::GetWindowLongPtr($window, -20).ToInt64() -band 0x8) -ne 0
    }

    [void][VeilNative]::SendMessage($window, 0x111, [UIntPtr]1016, [IntPtr]::Zero)
    $results.PassThroughTurnsOn = Wait-Condition {
        $style = [VeilNative]::GetWindowLongPtr($window, -20).ToInt64()
        ($style -band 0x20) -ne 0 -and ($style -band 0x08000000) -ne 0
    }
    [void][VeilNative]::SendMessage($window, 0x111, [UIntPtr]1016, [IntPtr]::Zero)
    $results.PassThroughTurnsOff = Wait-Condition {
        ([VeilNative]::GetWindowLongPtr($window, -20).ToInt64() -band 0x20) -eq 0
    }

    $tabs = [VeilNative]::GetDlgItem($window, 1011)
    1..7 | ForEach-Object {
        [void][VeilNative]::SendMessage($window, 0x111, [UIntPtr]1003, [IntPtr]::Zero)
    }
    $tabCount = [VeilNative]::SendMessage($tabs, 0x1304, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $results.TabLimit = $tabCount -eq 5
    $results.TabCount = $tabCount

    $address = [VeilNative]::GetDlgItem($window, 1001)
    [void][VeilNative]::SetWindowText($address, 'openai')
    [void][VeilNative]::SendMessage($window, 0x111, [UIntPtr]1002, [IntPtr]::Zero)
    $builder = New-Object Text.StringBuilder 2048
    $results.SearchNormalizesToHttps = Wait-Condition {
        [void]$builder.Clear()
        [void][VeilNative]::GetWindowText($address, $builder, $builder.Capacity)
        return $builder.ToString().StartsWith('https://duckduckgo.com/?q=openai')
    } 5000

    [void][VeilNative]::SendMessage($window, 0x111, [UIntPtr]1004, [IntPtr]::Zero)
    $results.MinimizeWorks = Wait-Condition { [VeilNative]::IsIconic($window) }
    [void][VeilNative]::ShowWindow($window, 9)

    [void][VeilNative]::SendMessage($window, 0x111, [UIntPtr]1006, [IntPtr]::Zero)
    $results.CloseWorks = $process.WaitForExit(8000)
} finally {
    if (-not $process.HasExited) { $process.Kill($true); [void]$process.WaitForExit(5000) }
    for ($attempt = 0; $attempt -lt 20 -and (Test-Path $tempRoot); $attempt++) {
        try { Remove-Item -LiteralPath $tempRoot -Recurse -Force } catch { Start-Sleep -Milliseconds 250 }
    }
}

$results.GetEnumerator() | ForEach-Object { '{0}={1}' -f $_.Key, $_.Value }
if ($results.Values -contains $false) { exit 2 }
