param([string]$Tag = 'capture', [switch]$KeepWindowed)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

if (-not ('KikiDesktopQemuCapture' -as [type])) {
  Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class KikiDesktopQemuCapture {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr extra);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr extra);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern IntPtr SetActiveWindow(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint from, uint to, bool attach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
}
'@
}

$qemu = Get-Process -ErrorAction SilentlyContinue |
  Where-Object ProcessName -Like 'qemu-system-aarch64*' |
  Sort-Object StartTime -Descending |
  Select-Object -First 1
if (-not $qemu) { throw 'No qemu-system-aarch64* process is running' }
$qemuPid = [uint32]$qemu.Id
$script:qemuGtkWindow = [IntPtr]::Zero
$callback = [KikiDesktopQemuCapture+EnumProc]{ param($hwnd,$extra)
  $windowPid = [uint32]0
  [KikiDesktopQemuCapture]::GetWindowThreadProcessId($hwnd,[ref]$windowPid) | Out-Null
  if ($windowPid -eq $qemuPid) {
    $title = [System.Text.StringBuilder]::new(128)
    [KikiDesktopQemuCapture]::GetWindowText($hwnd,$title,128) | Out-Null
    if ($title.ToString() -eq 'QEMU' -or
        $title.ToString().StartsWith('QEMU - Press Ctrl+Alt+G')) {
      $script:qemuGtkWindow = $hwnd
    }
  }
  return $true
}
[KikiDesktopQemuCapture]::EnumWindows($callback,[IntPtr]::Zero) | Out-Null
if ($script:qemuGtkWindow -eq [IntPtr]::Zero) {
  throw "Could not find the GTK display window for QEMU PID $qemuPid"
}

# Maximize the actual GTK display, not the hidden console window attached to
# qemu-system-aarch64.exe, then capture the visible Windows desktop.
$currentThread = [KikiDesktopQemuCapture]::GetCurrentThreadId()
$foregroundWindow = [KikiDesktopQemuCapture]::GetForegroundWindow()
$foregroundPid = [uint32]0
$foregroundThread = [KikiDesktopQemuCapture]::GetWindowThreadProcessId($foregroundWindow,[ref]$foregroundPid)
$qemuPidFromWindow = [uint32]0
$qemuThread = [KikiDesktopQemuCapture]::GetWindowThreadProcessId($script:qemuGtkWindow,[ref]$qemuPidFromWindow)
$attachedForeground = $false
$attachedQemu = $false
try {
  if ($foregroundThread -ne $currentThread) {
    $attachedForeground = [KikiDesktopQemuCapture]::AttachThreadInput($currentThread,$foregroundThread,$true)
  }
  if ($qemuThread -ne $currentThread) {
    $attachedQemu = [KikiDesktopQemuCapture]::AttachThreadInput($currentThread,$qemuThread,$true)
  }
  if ($KeepWindowed) {
    # Keep/restore the normal window size to compare guest output before and
    # after a host-side maximize/resize transition.
    [KikiDesktopQemuCapture]::ShowWindow($script:qemuGtkWindow,9) | Out-Null
  } else {
    [KikiDesktopQemuCapture]::ShowWindow($script:qemuGtkWindow,3) | Out-Null
  }
  [KikiDesktopQemuCapture]::BringWindowToTop($script:qemuGtkWindow) | Out-Null
  [KikiDesktopQemuCapture]::SetActiveWindow($script:qemuGtkWindow) | Out-Null
  [KikiDesktopQemuCapture]::SetForegroundWindow($script:qemuGtkWindow) | Out-Null
  [KikiDesktopQemuCapture]::SetFocus($script:qemuGtkWindow) | Out-Null
} finally {
  if ($attachedQemu) {
    [KikiDesktopQemuCapture]::AttachThreadInput($currentThread,$qemuThread,$false) | Out-Null
  }
  if ($attachedForeground) {
    [KikiDesktopQemuCapture]::AttachThreadInput($currentThread,$foregroundThread,$false) | Out-Null
  }
}
Start-Sleep -Milliseconds 500
$foreground = [KikiDesktopQemuCapture]::GetForegroundWindow()
$foregroundTitle = [System.Text.StringBuilder]::new(128)
[KikiDesktopQemuCapture]::GetWindowText($foreground,$foregroundTitle,128) | Out-Null
if ($foreground -ne $script:qemuGtkWindow) {
  throw "QEMU was not foreground (active window: '$($foregroundTitle.ToString())')"
}

[KikiDesktopQemuCapture]::SetProcessDPIAware() | Out-Null
$bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bitmap = [System.Drawing.Bitmap]::new($bounds.Width,$bounds.Height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($bounds.X,$bounds.Y,0,0,$bounds.Size)
$graphics.Dispose()
$outputDir = Join-Path $env:USERPROFILE 'Downloads\temp'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$path = Join-Path $outputDir "qemu-desktop-$Tag.png"
$bitmap.Save($path,[System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
"QEMU_PID=$qemuPid FOREGROUND='$($foregroundTitle.ToString())' PATH=$path SIZE=$($bounds.Width)x$($bounds.Height)"
