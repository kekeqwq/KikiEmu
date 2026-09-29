param(
  [ValidateRange(2, 10)][int]$DurationSeconds = 3,
  [string]$AdbSerial = '127.0.0.1:5555'
)

$ErrorActionPreference = 'Stop'

function Get-Percentile([double[]]$Values, [double]$Fraction) {
  if ($Values.Count -eq 0) { return [double]::NaN }
  $sorted = @($Values | Sort-Object)
  $index = [Math]::Max(0, [int][Math]::Ceiling($Fraction * $sorted.Count) - 1)
  return $sorted[$index]
}

$before = (& adb -s $AdbSerial shell cat /sys/kernel/tracing/tracing_on 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $before -ne '0') {
  throw "Tracefs must be idle before this bounded probe; tracing_on=$before"
}
$activity = (& adb -s $AdbSerial shell dumpsys activity activities 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $activity -notmatch 'topResumedActivity=.*com\.kiki\.gpubench/\.GpuBenchActivity') {
  throw 'The Kiki GPU benchmark Activity must be top-resumed before tracing.'
}

# Atrace is deliberately short because its instrumentation changes frame timing.
# Keep the trace in memory; do not modify the VM images or the desktop window.
$trace = @(& adb -s $AdbSerial shell atrace -c -t $DurationSeconds gfx view 2>&1)
$traceExit = $LASTEXITCODE
$after = (& adb -s $AdbSerial shell cat /sys/kernel/tracing/tracing_on 2>&1 | Out-String).Trim()
if ($after -ne '0') {
  & adb -s $AdbSerial shell 'echo 0 > /sys/kernel/tracing/tracing_on' | Out-Null
  throw 'Atrace left tracefs enabled; it was disabled, but the sample is discarded.'
}
if ($traceExit -ne 0) { throw "atrace failed with exit code $traceExit" }

$stacks = @{}
$durations = @{}
$parsedEvents = 0
foreach ($line in $trace) {
  $match = [regex]::Match($line,
    '^\s*.+-(\d+)\s+\(\s*\d+\)\s+\[\d+\]\s+\S+\s+(\d+\.\d+): tracing_mark_write: ([BE])\|\d+(?:\|(.*))?$')
  if (-not $match.Success) { continue }
  $parsedEvents++
  $threadId = $match.Groups[1].Value
  $time = [double]::Parse($match.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
  if ($match.Groups[3].Value -eq 'B') {
    if (-not $stacks.ContainsKey($threadId)) { $stacks[$threadId] = [Collections.ArrayList]::new() }
    [void]$stacks[$threadId].Add([pscustomobject]@{ Name = $match.Groups[4].Value; Start = $time })
    continue
  }
  if (-not $stacks.ContainsKey($threadId) -or $stacks[$threadId].Count -eq 0) { continue }
  $stack = $stacks[$threadId]
  $entry = $stack[$stack.Count - 1]
  $stack.RemoveAt($stack.Count - 1)
  $name = $entry.Name
  if ($name -notmatch 'waitForBufferRelease|eglSwapBuffers|dequeueBuffer|queueBuffer|postAndWait|OpsTask::onExecute|composeSurfaces|atomic_commit|flushToDisplay') {
    continue
  }
  if (-not $durations.ContainsKey($name)) { $durations[$name] = [Collections.ArrayList]::new() }
  [void]$durations[$name].Add(($time - $entry.Start) * 1000.0)
}

Write-Output "TRACE_SAMPLE seconds=$DurationSeconds lines=$($trace.Count) parsedMarkers=$parsedEvents tracing_on=$after"
foreach ($name in @($durations.Keys | Sort-Object)) {
  $values = [double[]]@($durations[$name])
  [pscustomobject]@{
    Stage = $name
    Count = $values.Count
    MedianMs = [Math]::Round((Get-Percentile $values 0.50), 3)
    P95Ms = [Math]::Round((Get-Percentile $values 0.95), 3)
    MaxMs = [Math]::Round((Get-Percentile $values 1.00), 3)
  }
}
