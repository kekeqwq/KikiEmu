param(
  [Parameter(Mandatory)][string]$ProfileLog,
  [ValidateRange(5, 120)][int]$DurationSeconds = 15,
  [string]$Label = 'manual-interaction',
  [string]$AdbSerial = '127.0.0.1:5555',
  [string]$UiPackage,
  [string[]]$UiPackages,
  [switch]$NotificationShadeAnimation,
  [switch]$DesktopNavigationAnimation
)

$ErrorActionPreference = 'Stop'
$path = (Resolve-Path -LiteralPath $ProfileLog).Path

# The host counts actual GTK GL render callbacks following guest scanout flushes.
# This intentionally does not use the benchmark APK's onDrawFrame FPS.
$sizeText = (& adb -s $AdbSerial shell wm size 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "ADB is unavailable: $sizeText" }
$sizeMatch = [regex]::Match($sizeText, 'Physical size:\s*(\d+)x(\d+)')
if (-not $sizeMatch.Success) { throw "Cannot verify guest display size: $sizeText" }
$width = [int]$sizeMatch.Groups[1].Value
$height = [int]$sizeMatch.Groups[2].Value
if ([Math]::Min($width, $height) -lt 864 -or [Math]::Max($width, $height) -lt 1728) {
  throw "Below the 864x1728 minimum baseline: ${width}x${height}"
}
if ($NotificationShadeAnimation -and $DesktopNavigationAnimation) {
  throw 'Choose only one automated UI workload per measurement.'
}
$measuredUiPackages = if ($UiPackages -and $UiPackages.Count) {
  @($UiPackages | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
} elseif (-not [string]::IsNullOrWhiteSpace($UiPackage)) {
  @($UiPackage)
} else {
  @()
}

function Get-SurfaceFlingerMissedFrameCounts {
  $dump = (& adb -s $AdbSerial shell dumpsys SurfaceFlinger 2>&1 | Out-String)
  if ($LASTEXITCODE -ne 0) { throw 'Cannot read SurfaceFlinger missed-frame counters.' }
  $totalMatch = [regex]::Match($dump, 'Total missed frame count:\s*(\d+)')
  $gpuMatch = [regex]::Match($dump, 'GPU missed frame count:\s*(\d+)')
  $hwcMatch = [regex]::Match($dump, 'HWC missed frame count:\s*(\d+)')
  if (-not $totalMatch.Success -or -not $gpuMatch.Success -or -not $hwcMatch.Success) {
    return $null
  }
  [pscustomobject]@{
    Total = [long]$totalMatch.Groups[1].Value
    GPU = [long]$gpuMatch.Groups[1].Value
    HWC = [long]$hwcMatch.Groups[1].Value
  }
}

$before = @(Get-Content -LiteralPath $path).Count
$sfBefore = Get-SurfaceFlingerMissedFrameCounts
foreach ($package in $measuredUiPackages) {
  & adb -s $AdbSerial shell dumpsys gfxinfo $package reset | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "Cannot reset gfxinfo for $package" }
}
$started = Get-Date
Write-Output "GLOBAL_FPS_START label=$Label guest=${width}x${height} duration=${DurationSeconds}s time=$($started.ToString('o'))"
if ($NotificationShadeAnimation) {
  Write-Output 'Driving repeatable notification-shade gestures through Android input; keep the QEMU window at its current size.'
  $deadline = [Diagnostics.Stopwatch]::StartNew()
  $centerX = [int]($width / 2)
  $middleY = [int]($height / 2)
  while ($deadline.Elapsed.TotalSeconds -lt $DurationSeconds) {
    & adb -s $AdbSerial shell input swipe $centerX 2 $centerX $middleY 450 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot expand the notification shade for the animation probe.' }
    & adb -s $AdbSerial shell input swipe $centerX $middleY $centerX 2 450 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot collapse the notification shade for the animation probe.' }
  }
} elseif ($DesktopNavigationAnimation) {
  Write-Output 'Driving native HOME/app-drawer/Settings/Overview navigation; the benchmark APK is not used.'
  $deadline = [Diagnostics.Stopwatch]::StartNew()
  $centerX = [int]($width / 2)
  $appDrawerStartY = [int]($height * 0.92)
  $appDrawerEndY = [int]($height * 0.50)
  $scrollStartY = [int]($height * 0.74)
  $scrollEndY = [int]($height * 0.36)
  while ($deadline.Elapsed.TotalSeconds -lt $DurationSeconds) {
    & adb -s $AdbSerial shell input keyevent HOME | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot return to HOME before app-drawer animation.' }
    & adb -s $AdbSerial shell input swipe $centerX $appDrawerStartY $centerX $appDrawerEndY 350 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot open the native app drawer.' }
    & adb -s $AdbSerial shell input swipe $centerX $scrollStartY $centerX $scrollEndY 350 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot scroll the native app drawer.' }
    & adb -s $AdbSerial shell input keyevent HOME | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot return from the native app drawer.' }

    & adb -s $AdbSerial shell am start -a android.settings.SETTINGS | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot launch native Android Settings.' }
    Start-Sleep -Milliseconds 700
    & adb -s $AdbSerial shell input swipe $centerX $scrollStartY $centerX $scrollEndY 400 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot scroll native Settings.' }
    & adb -s $AdbSerial shell input keyevent BACK | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot return from Settings.' }
    & adb -s $AdbSerial shell input keyevent APP_SWITCH | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot open Android Overview.' }
    Start-Sleep -Milliseconds 500
    & adb -s $AdbSerial shell input keyevent BACK | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot dismiss Android Overview.' }
  }
  & adb -s $AdbSerial shell input keyevent HOME | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'Cannot leave the guest on HOME after the desktop measurement.' }
} else {
  Write-Output 'Perform the touch interaction now; keep the QEMU window at its current size.'
  Start-Sleep -Seconds $DurationSeconds
}
$sfAfter = Get-SurfaceFlingerMissedFrameCounts
$lines = @(Get-Content -LiteralPath $path | Select-Object -Skip $before)

$pattern = '^KIKI-QEMU-GL-PROFILE span=(?<span>[0-9.]+)s flush=(?<flush>\d+) render=(?<render>\d+) fps=(?<fps>[0-9.]+) frame_p50=(?<p50>[0-9.]+)ms frame_p95=(?<p95>[0-9.]+)ms frame_p99=(?<p99>[0-9.]+)ms stalls33=(?<s33>\d+) stalls50=(?<s50>\d+) stalls100=(?<s100>\d+)(?: idle_gaps=(?<idle>\d+))? draw_avg=(?<draw>[0-9.]+)ms draw_max=(?<drawmax>[0-9.]+)ms queue_avg=(?<queue>[0-9.]+)ms$'
$samples = [System.Collections.Generic.List[object]]::new()
foreach ($line in $lines) {
  $match = [regex]::Match($line, $pattern)
  if (-not $match.Success) { continue }
  $samples.Add([pscustomobject]@{
    Span = [double]::Parse($match.Groups['span'].Value, [Globalization.CultureInfo]::InvariantCulture)
    Flush = [int]$match.Groups['flush'].Value
    Render = [int]$match.Groups['render'].Value
    Fps = [double]::Parse($match.Groups['fps'].Value, [Globalization.CultureInfo]::InvariantCulture)
    P50 = [double]::Parse($match.Groups['p50'].Value, [Globalization.CultureInfo]::InvariantCulture)
    P95 = [double]::Parse($match.Groups['p95'].Value, [Globalization.CultureInfo]::InvariantCulture)
    P99 = [double]::Parse($match.Groups['p99'].Value, [Globalization.CultureInfo]::InvariantCulture)
    Stalls33 = [int]$match.Groups['s33'].Value
    Stalls50 = [int]$match.Groups['s50'].Value
    Stalls100 = [int]$match.Groups['s100'].Value
    IdleGaps = if ($match.Groups['idle'].Success) { [int]$match.Groups['idle'].Value } else { 0 }
    Draw = [double]::Parse($match.Groups['draw'].Value, [Globalization.CultureInfo]::InvariantCulture)
    DrawMax = [double]::Parse($match.Groups['drawmax'].Value, [Globalization.CultureInfo]::InvariantCulture)
    Queue = [double]::Parse($match.Groups['queue'].Value, [Globalization.CultureInfo]::InvariantCulture)
  })
}
if ($samples.Count -eq 0) {
  throw 'No global present samples in this interval. Use a QEMU build with KIKI_GPU_PROFILE=1 and KIKI_GPU_PROFILE_PATH set, then animate the UI.'
}

$span = ($samples | Measure-Object -Property Span -Sum).Sum
$render = ($samples | Measure-Object -Property Render -Sum).Sum
$flush = ($samples | Measure-Object -Property Flush -Sum).Sum
$activeSamples = @($samples | Where-Object { $_.Flush -ge 5 })
$activeSpan = ($activeSamples | Measure-Object -Property Span -Sum).Sum
$activeRender = ($activeSamples | Measure-Object -Property Render -Sum).Sum
$activeFps = if ($activeSpan -gt 0) { $activeRender / $activeSpan } else { 0 }
$activeMedianFps = if ($activeSamples.Count) {
  $orderedActiveFps = @($activeSamples | Sort-Object -Property Fps | ForEach-Object { $_.Fps })
  $middle = [int][Math]::Floor($orderedActiveFps.Count / 2)
  if ($orderedActiveFps.Count % 2) {
    $orderedActiveFps[$middle]
  } else {
    ($orderedActiveFps[$middle - 1] + $orderedActiveFps[$middle]) / 2
  }
} else { 0 }
$fpsMin = if ($activeSamples.Count) { ($activeSamples | Measure-Object -Property Fps -Minimum).Minimum } else { 0 }
$stalls33 = ($activeSamples | Measure-Object -Property Stalls33 -Sum).Sum
$stalls50 = ($activeSamples | Measure-Object -Property Stalls50 -Sum).Sum
$stalls100 = ($activeSamples | Measure-Object -Property Stalls100 -Sum).Sum
$idleGaps = ($activeSamples | Measure-Object -Property IdleGaps -Sum).Sum
$p95Worst = if ($activeSamples.Count) { ($activeSamples | Measure-Object -Property P95 -Maximum).Maximum } else { 0 }
$p99Worst = if ($activeSamples.Count) { ($activeSamples | Measure-Object -Property P99 -Maximum).Maximum } else { 0 }
$drawMax = ($activeSamples | Measure-Object -Property DrawMax -Maximum).Maximum
$wallFps = if ($span -gt 0) { $render / $span } else { 0 }

Write-Output ('GLOBAL_FPS_RESULT label={0} guest={1}x{2} windows={3} observed={4:N2}s wall_fps={5:N2} active_windows={6} active_fps={7:N2} median_active_window_fps={8:N2} lowest_active_window_fps={9:N2} flush={10} render={11} active_p95_worst={12:N2}ms active_p99_worst={13:N2}ms active_stalls_over_33ms={14} active_stalls_over_50ms={15} active_stalls_over_100ms={16} active_idle_gaps={17} host_draw_max={18:N2}ms' -f $Label, $width, $height, $samples.Count, $span, $wallFps, $activeSamples.Count, $activeFps, $activeMedianFps, $fpsMin, $flush, $render, $p95Worst, $p99Worst, $stalls33, $stalls50, $stalls100, $idleGaps, $drawMax)
if ($sfBefore -and $sfAfter) {
  Write-Output ('SURFACEFLINGER_MISSED_FRAMES total_delta={0} gpu_delta={1} hwc_delta={2}' -f ($sfAfter.Total - $sfBefore.Total), ($sfAfter.GPU - $sfBefore.GPU), ($sfAfter.HWC - $sfBefore.HWC))
} else {
  Write-Output 'SURFACEFLINGER_MISSED_FRAMES unavailable=counter-not-reported'
}
foreach ($sample in $activeSamples) {
  Write-Output ('GLOBAL_FPS_WINDOW span={0:N2}s flush={1} render={2} fps={3:N2} frame_p50={4:N2}ms frame_p95={5:N2}ms frame_p99={6:N2}ms stalls33={7} stalls50={8} stalls100={9} idle_gaps={10} draw_max={11:N2}ms' -f $sample.Span, $sample.Flush, $sample.Render, $sample.Fps, $sample.P50, $sample.P95, $sample.P99, $sample.Stalls33, $sample.Stalls50, $sample.Stalls100, $sample.IdleGaps, $sample.DrawMax)
}
foreach ($package in $measuredUiPackages) {
  $gfxinfo = (& adb -s $AdbSerial shell dumpsys gfxinfo $package framestats 2>&1 | Out-String)
  if ($LASTEXITCODE -ne 0) { throw "Cannot read gfxinfo for $package" }
  $processStats = ($gfxinfo -split 'Window:', 2)[0]
  $frameMatch = [regex]::Match($processStats, 'Total frames rendered:\s*(\d+)')
  $jankMatch = [regex]::Match($processStats, 'Janky frames:\s*(\d+)\s*\(([0-9.]+)%\)')
  $p95Match = [regex]::Match($processStats, '95th percentile:\s*(\d+)ms')
  $highInputMatch = [regex]::Match($processStats, 'Number High input latency:\s*(\d+)')
  if ($frameMatch.Success -and $jankMatch.Success -and $p95Match.Success) {
    Write-Output ('ANDROID_UI_RESULT package={0} frames={1} janky={2} jank_percent={3}% frame_duration_p95={4}ms high_input_latency={5}' -f $package, $frameMatch.Groups[1].Value, $jankMatch.Groups[1].Value, $jankMatch.Groups[2].Value, $p95Match.Groups[1].Value, $(if ($highInputMatch.Success) { $highInputMatch.Groups[1].Value } else { 'unknown' }))
  } else {
    Write-Output "ANDROID_UI_RESULT package=$package unavailable=missing-gfxinfo-frame-stats"
  }
}
Write-Output 'wall_fps counts all fixed windows; active_fps includes windows with at least five guest flushes. Frame percentiles exclude >1s quiet gaps. This is QEMU GTK GL callback FPS, not physical DWM presents.'
