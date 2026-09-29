param(
  [Parameter(Mandatory)][string]$TracePath,
  [Parameter(Mandatory)][int]$SystemUiPid
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $TracePath -PathType Leaf)) {
  throw "Missing trace: $TracePath"
}

function Get-Percentile([double[]]$Values, [double]$Fraction) {
  if ($Values.Count -eq 0) { return [double]::NaN }
  $sorted = @($Values | Sort-Object)
  return $sorted[[Math]::Max(0, [int][Math]::Ceiling($Fraction * $sorted.Count) - 1)]
}

$marker = [regex]::new(
  '^\s*(.+)-(\d+)\s+\(\s*\d+\)\s+\[\d+\]\s+\S+\s+(\d+\.\d+): tracing_mark_write: ([BE])\|(\d+)(?:\|(.*))?$',
  [Text.RegularExpressions.RegexOptions]::Compiled
)
$stacks = @{}
$durations = @{}
$counts = @{}
$lineCount = 0
foreach ($batch in Get-Content -LiteralPath $TracePath -ReadCount 4096) {
  foreach ($line in $batch) {
    $lineCount++
    $match = $marker.Match($line)
    if (-not $match.Success) { continue }
    $tid = $match.Groups[2].Value
    $time = [double]::Parse($match.Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
    if ($match.Groups[4].Value -eq 'B') {
      if (-not $stacks.ContainsKey($tid)) { $stacks[$tid] = [Collections.ArrayList]::new() }
      [void]$stacks[$tid].Add([pscustomobject]@{
        Name = $match.Groups[6].Value
        Start = $time
        OwnerPid = [int]$match.Groups[5].Value
        Task = $match.Groups[1].Value.Trim()
      })
      continue
    }
    if (-not $stacks.ContainsKey($tid) -or $stacks[$tid].Count -eq 0) { continue }
    $stack = $stacks[$tid]
    $entry = $stack[$stack.Count - 1]
    $stack.RemoveAt($stack.Count - 1)
    if ($entry.OwnerPid -ne $SystemUiPid) { continue }
    $name = $entry.Name
    $kind = if ($name -match '^Choreographer#doFrame(?:\s|$)' -and
        $tid -eq [string]$SystemUiPid) { 'main doFrame' }
      elseif ($name -eq 'postAndWait') { 'main/RenderThread postAndWait' }
      elseif ($name -eq 'flush layers') { 'RenderThread flush layers' }
      elseif ($name -match '^eglSwapBuffers') { 'RenderThread eglSwapBuffers' }
      elseif ($name -match '^Texture upload\(') { 'RenderThread texture upload' }
      elseif ($name -eq 'drawLayer [graphicsLayer] 864.0 x 1728.0') { 'RenderThread full-screen graphicsLayer' }
      else { $null }
    if (-not $kind) { continue }
    if (-not $counts.ContainsKey($kind)) { $counts[$kind] = 0 }
    $counts[$kind]++
    $durationMs = ($time - $entry.Start) * 1000.0
    if ($durationMs -lt 0) { continue }
    if (-not $durations.ContainsKey($kind)) { $durations[$kind] = [Collections.ArrayList]::new() }
    [void]$durations[$kind].Add($durationMs)
  }
}

"TRACE=$TracePath SYSTEMUI_PID=$SystemUiPid LINES=$lineCount"
foreach ($kind in @($counts.Keys | Sort-Object)) {
  $values = [double[]]@($durations[$kind])
  if ($values.Count -eq 0) { continue }
  '{0}: count={1} median={2:N2}ms p95={3:N2}ms max={4:N2}ms over33={5}' -f `
    $kind, $counts[$kind], (Get-Percentile $values 0.50),
    (Get-Percentile $values 0.95), (Get-Percentile $values 1.00),
    @($values | Where-Object { $_ -gt 33.3 }).Count
}
'Trace instrumentation affects timing. Compare stage shape and work counts, not FPS or exact finger-to-photon delay.'
