param(
  [ValidateRange(10, 120)][int]$DurationSeconds = 30,
  [ValidateRange(100, 800)][int]$PollIntervalMs = 300,
  [string]$AdbSerial = '127.0.0.1:5555'
)

$ErrorActionPreference = 'Stop'

function Invoke-Adb([string[]]$AdbArgs) {
  $result = @(& adb -s $AdbSerial @AdbArgs 2>&1)
  if ($LASTEXITCODE -ne 0) {
    throw "adb $($AdbArgs -join ' ') failed: $($result -join ' ')"
  }
  return $result
}

function Get-BenchLayer {
  $lines = @(Invoke-Adb -AdbArgs @('shell', 'dumpsys SurfaceFlinger --list'))
  $names = @(
    foreach ($line in $lines) {
      $match = [regex]::Match($line,
        'RequestedLayerState\{(.+SurfaceView\[com\.kiki\.gpubench/[^]]+\]\(BLAST\)#\d+) parentId=')
      if ($match.Success) { $match.Groups[1].Value }
    }
  )
  if ($names.Count -ne 1) {
    throw "Expected one benchmark BLAST SurfaceView, found $($names.Count). The app may be minimized or recreating its surface."
  }
  return $names[0]
}

function Get-PresentTimes([string]$LayerName) {
  # adb shell needs the inner single quotes: the layer name contains spaces and parentheses.
  $lines = @(Invoke-Adb -AdbArgs @('shell', "dumpsys SurfaceFlinger --latency '$LayerName'"))
  $times = @(
    foreach ($line in $lines) {
      $match = [regex]::Match($line, '^\s*\d+\s+(\d+)\s+\d+\s*$')
      if ($match.Success -and $match.Groups[1].Value -ne '0') {
        [long]$match.Groups[1].Value
      }
    }
  )
  if ($times.Count -eq 0) { throw 'SurfaceFlinger returned no actual-present timestamps.' }
  return $times
}

function Get-Percentile([double[]]$Values, [double]$Fraction) {
  if ($Values.Count -eq 0) { return [double]::NaN }
  $sorted = @($Values | Sort-Object)
  $index = [int][Math]::Ceiling($Fraction * $sorted.Count) - 1
  return $sorted[[Math]::Max(0, $index)]
}

$deviceSize = (Invoke-Adb -AdbArgs @('shell', 'wm size')) -join ' '
$sizeMatch = [regex]::Match($deviceSize, 'Physical size:\s*(\d+)x(\d+)')
if (-not $sizeMatch.Success) { throw "Cannot verify physical display size: $deviceSize" }
$width = [int]$sizeMatch.Groups[1].Value
$height = [int]$sizeMatch.Groups[2].Value
if ([Math]::Min($width, $height) -lt 864 -or [Math]::Max($width, $height) -lt 1728) {
  throw "Guest display is below the 864x1728 minimum: ${width}x${height}"
}

$qemuProcesses = @(Get-Process qemu-system-aarch64 -ErrorAction SilentlyContinue)
if ($qemuProcesses.Count -ne 1 -or $qemuProcesses[0].MainWindowHandle -eq 0) {
  throw 'Expected exactly one QEMU process with a desktop window.'
}
Add-Type -MemberDefinition '[DllImport("user32.dll")] public static extern bool IsIconic(System.IntPtr hWnd);' -Name WindowProbe -Namespace Kiki
$windowHandle = $qemuProcesses[0].MainWindowHandle
if ([Kiki.WindowProbe]::IsIconic($windowHandle)) {
  throw 'QEMU is minimized; a normal foreground-performance sample would be invalid.'
}

$benchPid = (Invoke-Adb -AdbArgs @('shell', 'pidof com.kiki.gpubench')) -join ''
if ($benchPid -notmatch '^\d+$') { throw "Benchmark process is unavailable: $benchPid" }
$activityText = (Invoke-Adb -AdbArgs @('shell', 'dumpsys activity activities')) -join "`n"
if ($activityText -notmatch 'topResumedActivity=.*com\.kiki\.gpubench/\.GpuBenchActivity') {
  throw 'The benchmark is not the top-resumed Android activity.'
}

$layerName = Get-BenchLayer
$baseline = @(Get-PresentTimes $layerName)
$baselineMax = ($baseline | Measure-Object -Maximum).Maximum
$lastSeenMax = [long]$baselineMax
$seen = [Collections.Generic.HashSet[long]]::new()
$ringOverflowRisk = $false
$startEpochMs = [long]((Invoke-Adb -AdbArgs @('shell', 'date +%s%3N')) -join '')
$timer = [Diagnostics.Stopwatch]::StartNew()
$nextLayerCheck = 0.0

Write-Output "PRESENT_SAMPLE_START guest=${width}x${height} seconds=$DurationSeconds pid=$benchPid layer=$layerName"
while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
  Start-Sleep -Milliseconds $PollIntervalMs
  if (-not (Get-Process -Id $qemuProcesses[0].Id -ErrorAction SilentlyContinue)) {
    throw 'QEMU exited during the sample.'
  }
  if ([Kiki.WindowProbe]::IsIconic($windowHandle)) {
    throw 'QEMU was minimized during the sample; discard it.'
  }
  if ($timer.Elapsed.TotalSeconds -ge $nextLayerCheck) {
    if ((Get-BenchLayer) -ne $layerName) {
      throw 'The benchmark SurfaceView was recreated during the sample; discard it.'
    }
    $nextLayerCheck = $timer.Elapsed.TotalSeconds + 1.0
  }
  $ring = @(Get-PresentTimes $layerName)
  if ($ring.Count -ge 60 -and ($ring | Measure-Object -Minimum).Minimum -gt $lastSeenMax) {
    $ringOverflowRisk = $true
  }
  foreach ($time in $ring) {
    if ($time -gt $baselineMax) { [void]$seen.Add([long]$time) }
  }
  $lastSeenMax = [long](($ring | Measure-Object -Maximum).Maximum)
}
$timer.Stop()
$endEpochMs = [long]((Invoke-Adb -AdbArgs @('shell', 'date +%s%3N')) -join '')

$presentTimes = @($seen | Sort-Object)
$intervalsMs = @(
  for ($i = 1; $i -lt $presentTimes.Count; $i++) {
    ($presentTimes[$i] - $presentTimes[$i - 1]) / 1000000.0
  }
)
$logLines = @(Invoke-Adb -AdbArgs @('logcat', '-d', '-t', '600', '-v', 'epoch', '-s', 'KikiGpuBench:I'))
$appFps = @(
  foreach ($line in $logLines) {
    $match = [regex]::Match($line, '^\s*(\d+\.\d+)\s+(\d+)\s+\d+\s+I KikiGpuBench:\s+FPS=([\d.]+).*PROFILE=Extreme 3D INSTANCES=32768 RES=(\d+)x(\d+)')
    if (-not $match.Success -or $match.Groups[2].Value -ne $benchPid) { continue }
    $epochMs = [long]([double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) * 1000)
    if ($epochMs -lt ($startEpochMs + 1000) -or $epochMs -gt $endEpochMs) { continue }
    if ([int]$match.Groups[4].Value -ne $width -or [int]$match.Groups[5].Value -ne $height) { continue }
    [double]::Parse($match.Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
  }
)

if ($presentTimes.Count -lt 2 -or $appFps.Count -lt 3) {
  throw "Insufficient steady data: present=$($presentTimes.Count), appFpsWindows=$($appFps.Count)."
}
$actualDurationSeconds = ($endEpochMs - $startEpochMs) / 1000.0
$presentFps = $presentTimes.Count / $actualDurationSeconds
$missedRefreshGaps = @($intervalsMs | Where-Object { $_ -gt 25.0 }).Count
[pscustomobject]@{
  GuestSize = "${width}x${height}"
  Seconds = [Math]::Round($actualDurationSeconds, 2)
  PresentedFrames = $presentTimes.Count
  PresentedFps = [Math]::Round($presentFps, 2)
  PresentIntervalMedianMs = [Math]::Round((Get-Percentile $intervalsMs 0.50), 2)
  PresentIntervalP95Ms = [Math]::Round((Get-Percentile $intervalsMs 0.95), 2)
  PresentGapsOver25Ms = $missedRefreshGaps
  CallbackFpsWindows = $appFps.Count
  CallbackFpsMedian = [Math]::Round((Get-Percentile $appFps 0.50), 2)
  CallbackFpsMin = [Math]::Round((Get-Percentile $appFps 0.00), 2)
  CallbackFpsMax = [Math]::Round((Get-Percentile $appFps 1.00), 2)
  RingOverflowRisk = $ringOverflowRisk
  Note = 'SurfaceFlinger layer presents and APK callback windows are bounded to the same run; neither equals Windows DWM presents.'
}
