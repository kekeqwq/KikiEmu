param(
  [ValidateRange(1, 8)][int]$Cycles = 3,
  [string]$AdbSerial = '127.0.0.1:5555'
)

$ErrorActionPreference = 'Stop'

function Invoke-Adb([string[]]$Arguments) {
  $result = @(& adb -s $AdbSerial @Arguments 2>&1)
  if ($LASTEXITCODE -ne 0) { throw "adb $($Arguments -join ' ') failed: $($result -join ' ')" }
  return $result
}

function Get-Percentile([double[]]$Values, [double]$Fraction) {
  if ($Values.Count -eq 0) { return [double]::NaN }
  $sorted = @($Values | Sort-Object)
  return $sorted[[Math]::Max(0, [int][Math]::Ceiling($Fraction * $sorted.Count) - 1)]
}

$size = (Invoke-Adb -Arguments @('shell', 'wm size')) -join ' '
$match = [regex]::Match($size, 'Physical size:\s*(\d+)x(\d+)')
if (-not $match.Success) { throw "Cannot verify guest size: $size" }
$width = [int]$match.Groups[1].Value
$height = [int]$match.Groups[2].Value
if ([Math]::Min($width, $height) -lt 864 -or [Math]::Max($width, $height) -lt 1728) {
  throw "Below the 864x1728 floor: ${width}x${height}"
}
$tracingOn = (Invoke-Adb -Arguments @('shell', 'cat /sys/kernel/tracing/tracing_on')) -join ''
if ($tracingOn.Trim() -ne '0') { throw "Tracefs is already active: $tracingOn" }
$systemUiPid = ((Invoke-Adb -Arguments @('shell', 'pidof com.android.systemui')) -join '').Trim()
if ($systemUiPid -notmatch '^\d+$') { throw "Cannot identify SystemUI PID: $systemUiPid" }

Invoke-Adb -Arguments @('shell', 'input keyevent HOME') | Out-Null
Start-Sleep -Milliseconds 500
Invoke-Adb -Arguments @('shell', 'dumpsys gfxinfo com.android.systemui reset') | Out-Null

$trace = @()
$started = $false
try {
  Invoke-Adb -Arguments @('shell', 'atrace -c -b 32768 --async_start gfx view input') | Out-Null
  $started = $true
  $centerX = [int]($width / 2)
  $bottomY = [int]($height * 0.64)
  for ($cycle = 0; $cycle -lt $Cycles; $cycle++) {
    Invoke-Adb -Arguments @('shell', "input swipe $centerX 2 $centerX $bottomY 450") | Out-Null
    Start-Sleep -Milliseconds 250
    Invoke-Adb -Arguments @('shell', "input swipe $centerX $bottomY $centerX 2 450") | Out-Null
    Start-Sleep -Milliseconds 250
  }
  $trace = @(Invoke-Adb -Arguments @('shell', 'atrace --async_stop'))
  $started = $false
} finally {
  if ($started) { Invoke-Adb -Arguments @('shell', 'atrace --async_stop') | Out-Null }
  $tracingOn = (Invoke-Adb -Arguments @('shell', 'cat /sys/kernel/tracing/tracing_on')) -join ''
  if ($tracingOn.Trim() -ne '0') {
    Invoke-Adb -Arguments @('shell', 'echo 0 > /sys/kernel/tracing/tracing_on') | Out-Null
    throw 'Tracefs was left active; it was disabled and this sample is discarded.'
  }
}

$stacks = @{}
$durations = @{}
$mainThreadDurations = @{}
$markers = 0
foreach ($line in $trace) {
  $match = [regex]::Match($line,
    '^\s*(.+)-(\d+)\s+\(\s*\d+\)\s+\[\d+\]\s+\S+\s+(\d+\.\d+): tracing_mark_write: ([BE])\|(\d+)(?:\|(.*))?$')
  if (-not $match.Success) { continue }
  $markers++
  $threadId = $match.Groups[2].Value
  $time = [double]::Parse($match.Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
  if ($match.Groups[4].Value -eq 'B') {
    if (-not $stacks.ContainsKey($threadId)) { $stacks[$threadId] = [Collections.ArrayList]::new() }
    [void]$stacks[$threadId].Add([pscustomobject]@{
      Name = $match.Groups[6].Value; Start = $time; Task = $match.Groups[1].Value.Trim();
      OwnerPid = $match.Groups[5].Value
    })
    continue
  }
  if (-not $stacks.ContainsKey($threadId) -or $stacks[$threadId].Count -eq 0) { continue }
  $stack = $stacks[$threadId]
  $entry = $stack[$stack.Count - 1]
  $stack.RemoveAt($stack.Count - 1)
  $name = $entry.Name
  $durationMs = ($time - $entry.Start) * 1000.0
  if ($threadId -eq $systemUiPid -and $entry.OwnerPid -eq $systemUiPid -and $durationMs -ge 1.0) {
    if (-not $mainThreadDurations.ContainsKey($name)) {
      $mainThreadDurations[$name] = [Collections.ArrayList]::new()
    }
    [void]$mainThreadDurations[$name].Add($durationMs)
  }
  if ($name -notmatch 'waitForBufferRelease|eglSwapBuffers|postAndWait|OpsTask::onExecute|composeSurfaces|atomic_commit|flushToDisplay|dequeueBuffer|queueBuffer|doFrame|DrawFrame') {
    continue
  }
  $taskKey = if ($entry.Task -match 'RenderThread|android\.systemui|surfaceflinger|android\.hardwar|GLThread') {
    $entry.Task
  } else { 'other' }
  $key = "$name [$taskKey]"
  if (-not $durations.ContainsKey($key)) { $durations[$key] = [Collections.ArrayList]::new() }
  [void]$durations[$key].Add($durationMs)
}

Write-Output "SHADE_TRACE guest=${width}x${height} cycles=$Cycles lines=$($trace.Count) markers=$markers tracing_on=$($tracingOn.Trim())"
foreach ($key in @($durations.Keys | Sort-Object)) {
  $values = [double[]]@($durations[$key])
  if ($values.Count -lt 3) { continue }
  [pscustomobject]@{
    Stage = $key
    Count = $values.Count
    MedianMs = [Math]::Round((Get-Percentile $values 0.50), 3)
    P95Ms = [Math]::Round((Get-Percentile $values 0.95), 3)
    MaxMs = [Math]::Round((Get-Percentile $values 1.00), 3)
    Over33Ms = @($values | Where-Object { $_ -gt 33.3 }).Count
  }
}

Write-Output "SYSTEMUI_MAIN_THREAD_LONG_SLICES pid=$systemUiPid minimum=1ms top_by_max_duration"
$topMainSlices = @(
  foreach ($name in $mainThreadDurations.Keys) {
    $values = [double[]]@($mainThreadDurations[$name])
    [pscustomobject]@{
      Name = $name
      Count = $values.Count
      MedianMs = [Math]::Round((Get-Percentile $values 0.50), 3)
      P95Ms = [Math]::Round((Get-Percentile $values 0.95), 3)
      MaxMs = [Math]::Round((Get-Percentile $values 1.00), 3)
    }
  }
)
foreach ($slice in @($topMainSlices | Sort-Object -Property MaxMs -Descending | Select-Object -First 20)) {
  Write-Output ('SYSTEMUI_MAIN_SLICE name="{0}" count={1} median={2:N2}ms p95={3:N2}ms max={4:N2}ms' -f `
      $slice.Name, $slice.Count, $slice.MedianMs, $slice.P95Ms, $slice.MaxMs)
}

$gfxinfo = (Invoke-Adb -Arguments @('shell', 'dumpsys gfxinfo com.android.systemui')) -join "`n"
foreach ($pattern in @('Total frames rendered:[^\r\n]+', 'Janky frames:[^\r\n]+',
    '50th percentile:[^\r\n]+', '95th percentile:[^\r\n]+',
    'Number Slow UI thread:[^\r\n]+', 'Number Slow draw commands:[^\r\n]+')) {
  $match = [regex]::Match($gfxinfo, $pattern)
  if ($match.Success) { Write-Output "SYSTEMUI_$($match.Value.Trim())" }
}
Write-Output 'This synthetic gesture bypasses Windows touch; trace instrumentation changes timings. Use stage shape, not FPS, to choose the next fix.'
