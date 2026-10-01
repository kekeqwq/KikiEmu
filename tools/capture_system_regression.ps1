# SPDX-License-Identifier: GPL-2.0-or-later
# Physical Windows evidence for ONLY an owned internal real-system fixture.
param(
    [Parameter(Mandatory)][string]$Runner,
    [Parameter(Mandatory)][string]$FixtureRoot,
    [ValidatePattern('^[A-Za-z0-9_-]{1,48}$')][string]$Tag = 'clean-system',
    [switch]$Activate,
    [switch]$Maximize,
    [switch]$Unobscured
)
$ErrorActionPreference = 'Stop'
$runnerPath = (Resolve-Path -LiteralPath $Runner).Path
$fixturePath = (Resolve-Path -LiteralPath $FixtureRoot).Path
$description = & $runnerPath --describe $fixturePath
if ($LASTEXITCODE -ne 0) { throw 'Internal fixture identity refused; no window was selected.' }
$record = ($description -join "`n") | ConvertFrom-Json -ErrorAction Stop
if ($record.lifecycle -cne 'running' -or $record.runtime.channel -cne 'release' -or
    $record.uuid -cne $record.runtime.instanceUuid -or $record.runtime.sessionUuid -notmatch '^[0-9a-f-]{36}$') {
    throw 'Wait for actual owned HOME/120-Hz readiness before collecting final UI evidence.'
}
$owned = @($record.runtime.processes | Where-Object role -CEQ 'qemu')
if ($owned.Count -ne 1 -or $owned[0].instanceUuid -cne $record.uuid -or $owned[0].channel -cne 'release') {
    throw 'No unique owned internal-fixture QEMU process.'
}
$identity = $owned[0]
$process = Get-Process -Id ([int]$identity.pid) -ErrorAction Stop
$temporarilyTopmost = $false
$capturedWindow = [IntPtr]::Zero
try {
    # Holding its process handle prevents selecting a reused PID. Never choose
    # the newest QEMU, a title-only window or an installed public default.
    $pinnedHandle = $process.Handle
    if ($pinnedHandle -eq [IntPtr]::Zero -or $process.HasExited -or
        $process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -cne $identity.creationTime -or
        [IO.Path]::GetFullPath($process.Path) -ine [IO.Path]::GetFullPath($identity.executable) -or
        (Get-FileHash -LiteralPath $identity.executable).Hash.ToLowerInvariant() -cne $identity.executableSha256) {
        throw 'Process creation/path/hash identity changed. No window was activated.'
    }
    if (-not ('KikiOwnedSystemCapture' -as [type])) {
        Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class KikiOwnedSystemCapture {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr data);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
}
'@
    }
    $windows = [Collections.Generic.List[IntPtr]]::new()
    $callback = [KikiOwnedSystemCapture+EnumProc]{ param($window, $unused)
        $windowPid = [uint32]0
        [KikiOwnedSystemCapture]::GetWindowThreadProcessId($window, [ref]$windowPid) | Out-Null
        if ($windowPid -eq $process.Id -and [KikiOwnedSystemCapture]::IsWindowVisible($window)) {
            $title = [Text.StringBuilder]::new(256); $class = [Text.StringBuilder]::new(128)
            [KikiOwnedSystemCapture]::GetWindowText($window, $title, 256) | Out-Null
            [KikiOwnedSystemCapture]::GetClassName($window, $class, 128) | Out-Null
            if ($title.ToString() -ceq 'KikiEmu' -and $class.ToString().StartsWith('SDL')) { $windows.Add($window) }
        }
        return $true
    }
    [KikiOwnedSystemCapture]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
    if ($windows.Count -ne 1 -or $process.HasExited) { throw 'No unique owned native SDL display window; console/Grab/wrong windows refused.' }
    $capturedWindow = $windows[0]
    if ($Maximize) { [KikiOwnedSystemCapture]::ShowWindow($windows[0], 3) | Out-Null }
    if ($Activate) {
        if (-not $Maximize) { [KikiOwnedSystemCapture]::ShowWindow($windows[0], 9) | Out-Null }
        if (-not [KikiOwnedSystemCapture]::SetForegroundWindow($windows[0])) { throw 'Windows refused foreground activation. No keyboard/thread-input settings were changed.' }
        Start-Sleep -Milliseconds 500
    }
    if ($Unobscured -and (([KikiOwnedSystemCapture]::GetWindowLongPtr($capturedWindow, -20).ToInt64() -band 8) -eq 0)) {
        # Windows may refuse foreground activation while the user is typing.
        # Raise ONLY this pinned SDL test window, without activation or input
        # attachment; restore its original non-topmost state in finally.
        if (-not [KikiOwnedSystemCapture]::SetWindowPos($capturedWindow, [IntPtr]::new(-1), 0, 0, 0, 0, 0x13)) {
            throw 'Could not temporarily uncover the exact owned test window.'
        }
        $temporarilyTopmost = $true
        Start-Sleep -Milliseconds 500
    }
    $previousDpi = [KikiOwnedSystemCapture]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
    if ($previousDpi -eq [IntPtr]::Zero) { throw 'Physical-pixel per-monitor DPI context could not be established.' }
    try {
        Add-Type -AssemblyName System.Drawing
        $x = [KikiOwnedSystemCapture]::GetSystemMetrics(76); $y = [KikiOwnedSystemCapture]::GetSystemMetrics(77)
        $width = [KikiOwnedSystemCapture]::GetSystemMetrics(78); $height = [KikiOwnedSystemCapture]::GetSystemMetrics(79)
        if ($width -le 0 -or $height -le 0) { throw 'Invalid physical virtual-desktop bounds.' }
        $bitmap = [Drawing.Bitmap]::new($width, $height)
        try {
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            try { $graphics.CopyFromScreen($x, $y, 0, 0, [Drawing.Size]::new($width, $height)) }
            finally { $graphics.Dispose() }
            if ($process.HasExited) { throw 'Owned system exited while capturing; screenshot is not final-state evidence.' }
            $output = Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Downloads/temp'
            New-Item -ItemType Directory -Force -Path $output | Out-Null
            $name = 'kiki-system-{0}-{1}-{2}.png' -f $Tag, (Get-Date -Format yyyyMMdd-HHmmss), ([guid]::NewGuid().ToString('N').Substring(0, 8))
            $path = Join-Path $output $name
            $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
            "OWNED_QEMU_PID=$($process.Id) SESSION=$($record.runtime.sessionUuid) PIXELS=${width}x${height} PATH=$path"
        } finally { $bitmap.Dispose() }
    } finally { [KikiOwnedSystemCapture]::SetThreadDpiAwarenessContext($previousDpi) | Out-Null }
} finally {
    try {
        if ($temporarilyTopmost -and -not $process.HasExited) {
            $restorePid = [uint32]0
            [KikiOwnedSystemCapture]::GetWindowThreadProcessId($capturedWindow, [ref]$restorePid) | Out-Null
            if ($restorePid -eq $process.Id -and
                -not [KikiOwnedSystemCapture]::SetWindowPos($capturedWindow, [IntPtr]::new(-2), 0, 0, 0, 0, 0x13)) {
                Write-Warning 'The owned test window could not be restored to non-topmost; no other window was changed.'
            }
        }
    } finally { $process.Dispose() }
}
