param(
  [ValidateRange(10, 30)][int]$DurationSeconds = 18,
  [string]$AdbSerial = '127.0.0.1:5555'
)

$ErrorActionPreference = 'Stop'

function Get-PresentTimes([string]$LayerName) {
  $lines = @(& adb -s $AdbSerial shell "dumpsys SurfaceFlinger --latency '$LayerName'" 2>&1)
  if ($LASTEXITCODE -ne 0) { throw 'Cannot read SurfaceFlinger shade layer latency.' }
  @(
    foreach ($line in $lines) {
      $match = [regex]::Match($line, '^\s*\d+\s+(\d+)\s+\d+\s*$')
      if ($match.Success -and $match.Groups[1].Value -ne '0') {
        [long]$match.Groups[1].Value
      }
    }
  )
}

function Get-Percentile([double[]]$Values, [double]$Fraction) {
  if ($Values.Count -eq 0) { return [double]::NaN }
  $sorted = @($Values | Sort-Object)
  $index = [Math]::Max(0, [int][Math]::Ceiling($Fraction * $sorted.Count) - 1)
  $sorted[$index]
}

$size = (& adb -s $AdbSerial shell wm size 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $size -notmatch 'Physical size:\s*(\d+)x(\d+)') {
  throw "Cannot verify guest display size: $size"
}
$width = [int]$Matches[1]
$height = [int]$Matches[2]
$inputName = (& adb -s $AdbSerial shell getevent -pl /dev/input/event0 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $inputName -notmatch 'QEMU Virtio MultiTouch') {
  throw '/dev/input/event0 is not the verified QEMU Virtio MultiTouch device.'
}
$layerLines = @(& adb -s $AdbSerial shell dumpsys SurfaceFlinger --list 2>&1)
$shadeMatches = @($layerLines | ForEach-Object {
  [regex]::Match($_, 'RequestedLayerState\{(VRI-NotificationShade#\d+)(?:\s|\})')
} | Where-Object Success)
if ($shadeMatches.Count -ne 1) { throw "Expected one NotificationShade output layer, found $($shadeMatches.Count)." }
$shadeLayer = $shadeMatches[0].Groups[1].Value
$baseline = @(Get-PresentTimes $shadeLayer)
$baselineMax = if ($baseline.Count) { [long](($baseline | Measure-Object -Maximum).Maximum) } else { 0L }
$presentTimes = [Collections.Generic.HashSet[long]]::new()

$inputJob = Start-Job -ScriptBlock {
  param($Serial, $Seconds)
  & adb -s $Serial shell "timeout $Seconds getevent -lt /dev/input/event0" 2>&1
} -ArgumentList $AdbSerial, ($DurationSeconds + 2)
$watch = [Diagnostics.Stopwatch]::StartNew()
Write-Output "REAL_TOUCH_CAPTURE_START seconds=$DurationSeconds guest=${width}x${height} shade=$shadeLayer"
try {
  while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
    Start-Sleep -Milliseconds 220
    $current = @(Get-PresentTimes $shadeLayer)
    foreach ($time in $current) {
      if ($time -gt $baselineMax) { [void]$presentTimes.Add($time) }
    }
  }
} finally {
  $watch.Stop()
}
$inputLines = @(Receive-Job -Job $inputJob -Wait -AutoRemoveJob)
$strokes = [Collections.ArrayList]::new()
$touchDown = 0
$touchUp = 0
$activeStroke = $null
$lastY = $null
foreach ($line in $inputLines) {
  $stamp = [regex]::Match($line, '^\[\s*([0-9]+\.[0-9]+)\]')
  if (-not $stamp.Success) { continue }
  $time = [double]::Parse($stamp.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
  $tracking = [regex]::Match($line, 'ABS_MT_TRACKING_ID\s+(\S+)')
  if ($tracking.Success) {
    if ($tracking.Groups[1].Value -match '^f{8}$') {
      if ($null -ne $activeStroke) {
        $activeStroke.End = $time
        [void]$strokes.Add($activeStroke)
        $activeStroke = $null
      }
      $touchUp++
    } else {
      $activeStroke = [pscustomobject]@{
        Start = $time
        End = $time
        Samples = [Collections.ArrayList]::new()
      }
      $lastY = $null
      $touchDown++
    }
    continue
  }
  $y = [regex]::Match($line, 'ABS_MT_POSITION_Y\s+([0-9a-fA-F]+)')
  if ($y.Success) {
    $lastY = [Convert]::ToInt32($y.Groups[1].Value, 16)
    continue
  }
  if ($null -ne $activeStroke -and $null -ne $lastY -and $line -match 'EV_SYN\s+SYN_REPORT') {
    [void]$activeStroke.Samples.Add([pscustomobject]@{ Time = $time; Y = $lastY })
  }
}
if ($null -ne $activeStroke) { [void]$strokes.Add($activeStroke) }
$orderedPresent = @($presentTimes | Sort-Object)
$endLayerLines = @(& adb -s $AdbSerial shell dumpsys SurfaceFlinger --list 2>&1)
$endShadeMatches = @($endLayerLines | ForEach-Object {
  [regex]::Match($_, 'RequestedLayerState\{(VRI-NotificationShade#\d+)(?:\s|\})')
} | Where-Object Success)
$endShadeLayer = if ($endShadeMatches.Count -eq 1) {
  $endShadeMatches[0].Groups[1].Value
} else { '' }
$layerChanged = $endShadeLayer -ne $shadeLayer
Write-Output "REAL_TOUCH_CAPTURE_RESULT seconds=$([Math]::Round($watch.Elapsed.TotalSeconds,2)) raw_input_lines=$($inputLines.Count) touch_down=$touchDown touch_up=$touchUp strokes=$($strokes.Count) shade_presents=$($orderedPresent.Count) layer_changed=$layerChanged"
if ($layerChanged) {
  Write-Output 'NotificationShade layer was recreated; presentation correlation is invalid.'
  exit 0
}
$strokeIndex = 0
foreach ($stroke in $strokes) {
  $strokeIndex++
  if ($stroke.Samples.Count -eq 0) {
    Write-Output "STROKE_$strokeIndex samples=0 duration_ms=$([Math]::Round(($stroke.End-$stroke.Start)*1000,1))"
    continue
  }
  $strokeYs = @($stroke.Samples | ForEach-Object Y)
  Write-Output "STROKE_$strokeIndex samples=$($stroke.Samples.Count) duration_ms=$([Math]::Round(($stroke.End-$stroke.Start)*1000,1)) y_start=$($strokeYs[0]) y_end=$($strokeYs[-1]) y_min=$(($strokeYs | Measure-Object -Minimum).Minimum) y_max=$(($strokeYs | Measure-Object -Maximum).Maximum)"
}
$downStrokes = @(
  foreach ($stroke in $strokes) {
    if ($stroke.Samples.Count -lt 3) { continue }
    $samples = @($stroke.Samples)
    $peakIndex = 0
    for ($index = 1; $index -lt $samples.Count; $index++) {
      if ($samples[$index].Y -gt $samples[$peakIndex].Y) { $peakIndex = $index }
    }
    if ($samples[0].Y -ge 8000 -or ($samples[$peakIndex].Y - $samples[0].Y) -le 5000) {
      continue
    }
    [pscustomobject]@{
      Start = $samples[0].Time
      End = $samples[$peakIndex].Time
      Samples = @($samples[0..$peakIndex])
    }
  }
)
Write-Output "QUALIFYING_DOWNWARD_PHASES=$($downStrokes.Count) start_y_under_8000 peak_delta_y_over_5000 raw_range_0_to_32767"
$allDelays = [Collections.ArrayList]::new()
$allFrameIntervals = [Collections.ArrayList]::new()
$noFrameWithin200 = 0
$strokeNumber = 0
foreach ($stroke in $downStrokes) {
  $strokeNumber++
  $strokeDelays = [Collections.ArrayList]::new()
  $strokePresents = @($orderedPresent | Where-Object {
      ($_ / 1000000000.0) -ge $stroke.Start -and ($_ / 1000000000.0) -le $stroke.End
    })
  $frameIntervals = [Collections.ArrayList]::new()
  for ($index = 1; $index -lt $strokePresents.Count; $index++) {
    $intervalMs = ($strokePresents[$index] - $strokePresents[$index - 1]) / 1000000.0
    [void]$frameIntervals.Add($intervalMs)
    [void]$allFrameIntervals.Add($intervalMs)
  }
  foreach ($sample in $stroke.Samples) {
    $next = @($orderedPresent | Where-Object { ($_ / 1000000000.0) -ge $sample.Time } | Select-Object -First 1)
    if ($next.Count -ne 1 -or (($next[0] / 1000000000.0) - $sample.Time) -gt 0.2) {
      $noFrameWithin200++
      continue
    }
    $delay = (($next[0] / 1000000000.0) - $sample.Time) * 1000.0
    [void]$strokeDelays.Add($delay)
    [void]$allDelays.Add($delay)
  }
  $values = [double[]]@($strokeDelays)
  $intervalValues = [double[]]@($frameIntervals)
  Write-Output "DOWN_PHASE_$strokeNumber duration_ms=$([Math]::Round(($stroke.End-$stroke.Start)*1000,1)) y=$($stroke.Samples[0].Y)-$($stroke.Samples[$stroke.Samples.Count-1].Y) touch_samples=$($stroke.Samples.Count) shade_presents=$($strokePresents.Count) matched_within_200ms=$($values.Count) median_to_next_present_ms=$([Math]::Round((Get-Percentile $values 0.50),1)) p95_to_next_present_ms=$([Math]::Round((Get-Percentile $values 0.95),1)) p95_present_interval_ms=$([Math]::Round((Get-Percentile $intervalValues 0.95),1))"
}
$delayValues = [double[]]@($allDelays)
if ($delayValues.Count) {
  Write-Output "DOWN_DRAG_INPUT_TO_NEXT_SHADE_PRESENT_PROXY matched=$($delayValues.Count) no_present_within_200ms=$noFrameWithin200 median=$([Math]::Round((Get-Percentile $delayValues 0.50),2))ms p95=$([Math]::Round((Get-Percentile $delayValues 0.95),2))ms"
}
$allIntervals = [double[]]@($allFrameIntervals)
if ($allIntervals.Count) {
  $over50 = @($allIntervals | Where-Object { $_ -gt 50 }).Count
  Write-Output "DOWN_DRAG_PRESENT_INTERVALS count=$($allIntervals.Count) median=$([Math]::Round((Get-Percentile $allIntervals 0.50),2))ms p95=$([Math]::Round((Get-Percentile $allIntervals 0.95),2))ms over_50ms=$over50"
}
Write-Output 'This is guest input SYN to the next NotificationShade layer-present timestamp, not exact touch-to-photon. It excludes Windows-to-QEMU input delay and may associate a frame prepared before an input sample. Non-downward strokes are excluded.'
