param(
  [Parameter(Mandatory)][string]$GuestTracePath,
  [switch]$UseWholeTrace
)

$ErrorActionPreference = 'Stop'

function Get-Percentile([double[]]$Values, [double]$Fraction) {
  if ($Values.Count -eq 0) { return [double]::NaN }
  $sorted = @($Values | Sort-Object)
  $index = [Math]::Max(0, [int][Math]::Ceiling($Fraction * $sorted.Count) - 1)
  return $sorted[$index]
}

function Show-Stats([string]$Label, [double[]]$Values) {
  if (-not $Values.Count) { Write-Output "$Label`: no samples"; return }
  Write-Output ('{0}: N={1} P50={2:N3}ms P95={3:N3}ms MAX={4:N3}ms' -f `
    $Label, $Values.Count, (Get-Percentile $Values 0.5), `
    (Get-Percentile $Values 0.95), (Get-Percentile $Values 1.0))
}

$lineRx = [regex]::new(
  '^\s*(?<task>.*?)-(?<tid>\d+)\s+\(\s*[\d-]+\)\s+\[[^]]+\]\s+\S+\s+(?<time>\d+\.\d+):\s+(?<event>[^:]+):\s*(?<payload>.*)$',
  [Text.RegularExpressions.RegexOptions]::Compiled)
$ticks = [Collections.Generic.List[object]]::new()
$delivered = [Collections.Generic.HashSet[long]]::new()
$slices = [Collections.Generic.List[object]]::new()
$stacks = @{}
$fenceWaitStarts = @{}
$fenceWaits = [Collections.Generic.List[object]]::new()
$resourceFlushes = [Collections.Generic.List[object]]::new()
$crtcSignals = [Collections.Generic.List[object]]::new()
$inside = [bool]$UseWholeTrace
$begin = [double]::NaN
$end = [double]::NaN

foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $GuestTracePath))) {
  $m = $lineRx.Match($line)
  if (-not $m.Success) { continue }
  $event = $m.Groups['event'].Value.Trim()
  $payload = $m.Groups['payload'].Value
  $traceTime = [double]::Parse($m.Groups['time'].Value, [Globalization.CultureInfo]::InvariantCulture)
  if ($UseWholeTrace) {
    if ([double]::IsNaN($begin)) { $begin = $traceTime }
    $end = $traceTime
  }
  if ($event -eq 'tracing_mark_write' -and $payload -eq 'KIKI_GPU_BENCH_PIPELINE_BEGIN') {
    $inside = $true
    $begin = $traceTime
  }
  if (-not $inside) { continue }
  if ($event -eq 'drm_vblank_event' -and $payload -match 'crtc=0, seq=(\d+), time=(\d+)') {
    $ticks.Add([pscustomobject]@{ Seq=[long]$Matches[1]; Time=([double]$Matches[2] / 1e9); TraceTime=$traceTime })
  } elseif ($event -eq 'drm_vblank_event_delivered' -and $payload -match 'crtc=0, seq=(\d+)') {
    [void]$delivered.Add([long]$Matches[1])
  } elseif ($event -eq 'dma_fence_wait_start' -and $payload -match 'timeline=controlq') {
    $fenceWaitStarts[$m.Groups['tid'].Value] = $traceTime
  } elseif ($event -eq 'dma_fence_wait_end') {
    $tid = $m.Groups['tid'].Value
    if ($fenceWaitStarts.ContainsKey($tid)) {
      $fenceWaits.Add([pscustomobject]@{ Start=$fenceWaitStarts[$tid]; End=$traceTime })
      $fenceWaitStarts.Remove($tid)
    }
  } elseif ($event -eq 'virtio_gpu_cmd_queue' -and $payload -match 'type=0x104\b') {
    $resourceFlushes.Add([pscustomobject]@{ Time=$traceTime; Tid=$m.Groups['tid'].Value })
  } elseif ($event -eq 'dma_fence_signaled' -and
            $payload -match 'driver=virtio_gpu timeline=CRTC:[^ ]+ context=\d+ seqno=(\d+)') {
    $crtcSignals.Add([pscustomobject]@{ Time=$traceTime; Seq=[long]$Matches[1] })
  } elseif ($event -eq 'tracing_mark_write') {
    $slice = [regex]::Match($payload, '^(?<kind>[BE])\|\d+(?:\|(?<name>.*))?$')
    if ($slice.Success) {
      $tid = $m.Groups['tid'].Value
      if (-not $stacks.ContainsKey($tid)) { $stacks[$tid] = [Collections.Generic.List[object]]::new() }
      $stack = $stacks[$tid]
      if ($slice.Groups['kind'].Value -eq 'B') {
        $stack.Add([pscustomobject]@{ Name=$slice.Groups['name'].Value; Start=$traceTime })
      } elseif ($stack.Count) {
        $top = $stack[$stack.Count - 1]
        $stack.RemoveAt($stack.Count - 1)
        if ($top.Name -in @('flushToDisplay','KikiDrmDisplay::atomic_commit','waitForBufferRelease',
            'onDrawFrame','eglSwapBuffers','queueBuffer','dequeueBuffer') -or
            $top.Name -like 'releaseBufferCallback - *com.kiki.gpubench*') {
          $slices.Add([pscustomobject]@{ Name=$top.Name; Start=$top.Start; End=$traceTime; Tid=$tid })
        }
      }
    }
  }
  if (-not $UseWholeTrace -and $event -eq 'tracing_mark_write' -and
      $payload -eq 'KIKI_GPU_BENCH_PIPELINE_END') {
    $end = $traceTime
    break
  }
}
if ([double]::IsNaN($begin) -or [double]::IsNaN($end) -or $end -le $begin) {
  throw 'Missing valid KIKI_GPU_BENCH_PIPELINE_BEGIN/END markers.'
}

$orderedTicks = @($ticks | Sort-Object Time)
$flushes = @($slices | Where-Object Name -eq 'flushToDisplay' | Sort-Object Start)
$commits = @($slices | Where-Object Name -eq 'KikiDrmDisplay::atomic_commit' | Sort-Object Start)
$draws = @($slices | Where-Object Name -eq 'onDrawFrame' | Sort-Object Start)
if (-not $draws.Count) { throw 'No benchmark onDrawFrame slices in the marked trace window.' }
$appTid = $draws[0].Tid
$waits = @($slices | Where-Object { $_.Name -eq 'waitForBufferRelease' -and $_.Tid -eq $appTid } | Sort-Object Start)
$appReleases = @($slices | Where-Object {
  $_.Tid -eq $appTid -and $_.Name -like 'releaseBufferCallback - *com.kiki.gpubench*'
} | Sort-Object Start)
$swaps = @($slices | Where-Object { $_.Name -eq 'eglSwapBuffers' -and $_.Tid -eq $appTid } | Sort-Object Start)
$rawQueues = @($slices | Where-Object { $_.Name -eq 'queueBuffer' -and $_.Tid -eq $appTid } | Sort-Object Start)
$queues = @(foreach ($swap in $swaps) {
  $queue = $rawQueues | Where-Object { $_.Start -ge $swap.Start -and $_.End -le $swap.End } | Select-Object -First 1
  if ($queue) { $queue }
})
$dequeues = @($slices | Where-Object { $_.Name -eq 'dequeueBuffer' -and $_.Tid -eq $appTid } | Sort-Object Start)
$missing = @($orderedTicks | Where-Object { -not $delivered.Contains($_.Seq) })
$hit = @($orderedTicks | Where-Object { $delivered.Contains($_.Seq) })
$tickSequenceGaps = 0
for ($i=1; $i -lt $orderedTicks.Count; $i++) {
  $gap = $orderedTicks[$i].Seq - $orderedTicks[$i-1].Seq - 1
  if ($gap -gt 0) { $tickSequenceGaps += $gap }
}

function Summarize-Ticks([string]$Label, [object[]]$Group) {
  $activeFlush = 0
  $activeCommit = 0
  $activeWait = 0
  $activeInputFenceWait = 0
  $activeDraw = 0
  $activeSwap = 0
  $activeDequeue = 0
  $activeCommitWithResourceFlushQueued = 0
  $resourceFlushToTick = [Collections.Generic.List[double]]::new()
  $activeCommitAge = [Collections.Generic.List[double]]::new()
  $activeCommitRemaining = [Collections.Generic.List[double]]::new()
  $sincePriorFlushEnd = [Collections.Generic.List[double]]::new()
  $untilNextFlushStart = [Collections.Generic.List[double]]::new()
  $untilNextQueueStart = [Collections.Generic.List[double]]::new()
  $untilNextReleaseCallback = [Collections.Generic.List[double]]::new()
  $untilNextCrtcSignal = [Collections.Generic.List[double]]::new()
  $tickServiceDelay = [Collections.Generic.List[double]]::new()
  foreach ($tick in $Group) {
    $time = $tick.Time
    $tickServiceDelay.Add(($tick.TraceTime-$time)*1000.0)
    if (@($flushes | Where-Object { $_.Start -le $time -and $_.End -ge $time }).Count) { $activeFlush++ }
    $currentCommit = @($commits | Where-Object { $_.Start -le $time -and $_.End -ge $time } | Select-Object -First 1)
    if ($currentCommit.Count) {
      $activeCommit++
      $activeCommitAge.Add(($time-$currentCommit[0].Start)*1000.0)
      $activeCommitRemaining.Add(($currentCommit[0].End-$time)*1000.0)
      $queuedFlush = @($resourceFlushes | Where-Object {
        $_.Tid -eq $currentCommit[0].Tid -and
        $_.Time -ge $currentCommit[0].Start -and $_.Time -le $time
      } | Select-Object -Last 1)
      if ($queuedFlush.Count) {
        $activeCommitWithResourceFlushQueued++
        $resourceFlushToTick.Add(($time-$queuedFlush[0].Time)*1000.0)
      }
    }
    if (@($waits | Where-Object { $_.Start -le $time -and $_.End -ge $time }).Count) { $activeWait++ }
    if (@($draws | Where-Object { $_.Start -le $time -and $_.End -ge $time }).Count) { $activeDraw++ }
    if (@($swaps | Where-Object { $_.Start -le $time -and $_.End -ge $time }).Count) { $activeSwap++ }
    if (@($dequeues | Where-Object { $_.Start -le $time -and $_.End -ge $time }).Count) { $activeDequeue++ }
    if (@($fenceWaits | Where-Object { $_.Start -le $time -and $_.End -ge $time }).Count) { $activeInputFenceWait++ }
    $prior = @($flushes | Where-Object End -le $time | Select-Object -Last 1)
    $next = @($flushes | Where-Object Start -ge $time | Select-Object -First 1)
    $nextQueue = @($queues | Where-Object Start -ge $time | Select-Object -First 1)
    $nextRelease = @($appReleases | Where-Object Start -ge $time | Select-Object -First 1)
    $nextSignal = @($crtcSignals | Where-Object Time -ge $time | Select-Object -First 1)
    if ($prior.Count) { $sincePriorFlushEnd.Add(($time-$prior[0].End)*1000.0) }
    if ($next.Count) { $untilNextFlushStart.Add(($next[0].Start-$time)*1000.0) }
    if ($nextQueue.Count) { $untilNextQueueStart.Add(($nextQueue[0].Start-$time)*1000.0) }
    if ($nextRelease.Count) { $untilNextReleaseCallback.Add(($nextRelease[0].Start-$time)*1000.0) }
    if ($nextSignal.Count) { $untilNextCrtcSignal.Add(($nextSignal[0].Time-$time)*1000.0) }
  }
  Write-Output "$Label`: ticks=$($Group.Count) active_flush=$activeFlush active_commit=$activeCommit active_producer_wait=$activeWait active_input_fence_wait=$activeInputFenceWait active_draw=$activeDraw active_swap=$activeSwap active_dequeue=$activeDequeue"
  Write-Output "$Label active commit with RESOURCE_FLUSH queued by tick: $activeCommitWithResourceFlushQueued/$activeCommit"
  Show-Stats "$Label RESOURCE_FLUSH queue→tick" ([double[]]$resourceFlushToTick)
  Show-Stats "$Label active commit age at tick" ([double[]]$activeCommitAge)
  Show-Stats "$Label active commit remaining after tick" ([double[]]$activeCommitRemaining)
  Show-Stats "$Label tick minus prior HWC flush end" ([double[]]$sincePriorFlushEnd)
  Show-Stats "$Label next HWC flush start minus tick" ([double[]]$untilNextFlushStart)
  Show-Stats "$Label next app queue start minus tick" ([double[]]$untilNextQueueStart)
  Show-Stats "$Label next app release callback minus tick" ([double[]]$untilNextReleaseCallback)
  Show-Stats "$Label next CRTC signal minus tick" ([double[]]$untilNextCrtcSignal)
  Show-Stats "$Label vblank timestamp→ftrace event service" ([double[]]$tickServiceDelay)
}

Write-Output "TRACE_WINDOW_SECONDS=$([Math]::Round($end-$begin,3)) TICKS=$($orderedTicks.Count) FLIP_EVENTS=$($delivered.Count) MISSING_FLIP_TICKS=$($missing.Count) UNOBSERVED_TICK_SEQUENCES=$tickSequenceGaps FLUSHES=$($flushes.Count) COMMITS=$($commits.Count) APP_DRAWS=$($draws.Count) APP_SWAPS=$($swaps.Count) APP_QUEUES=$($queues.Count) APP_DEQUEUES=$($dequeues.Count) APP_WAITS=$($waits.Count) APP_RELEASE_CALLBACKS=$($appReleases.Count) CRTC_SIGNALS=$($crtcSignals.Count) INPUT_FENCE_WAITS=$($fenceWaits.Count) RESOURCE_FLUSH_QUEUES=$($resourceFlushes.Count)"
$waitToCallback = [Collections.Generic.List[double]]::new()
$signalToCallback = [Collections.Generic.List[double]]::new()
$callbackToWaitEnd = [Collections.Generic.List[double]]::new()
$waitsWithCallback = 0
$waitsWithPriorSignal = 0
foreach ($wait in $waits) {
  $callback = $appReleases | Where-Object {
    $_.Start -ge $wait.Start -and $_.Start -le $wait.End
  } | Select-Object -First 1
  if (-not $callback) { continue }
  $waitsWithCallback++
  $waitToCallback.Add(($callback.Start-$wait.Start)*1000.0)
  $callbackToWaitEnd.Add(($wait.End-$callback.Start)*1000.0)
  $signal = $crtcSignals | Where-Object {
    $_.Time -ge $wait.Start -and $_.Time -le $callback.Start
  } | Select-Object -Last 1
  if ($signal) {
    $waitsWithPriorSignal++
    $signalToCallback.Add(($callback.Start-$signal.Time)*1000.0)
  }
}
Write-Output "RELEASE_CHANNEL_MATCHES wait_to_same_thread_callback=$waitsWithCallback/$($waits.Count) callback_with_prior_CRTC_signal=$waitsWithPriorSignal/$waitsWithCallback"
Show-Stats 'Producer wait start→release callback start' ([double[]]$waitToCallback)
Show-Stats 'Prior CRTC signal→release callback start (correlation only)' ([double[]]$signalToCallback)
Show-Stats 'Release callback start→producer wait end' ([double[]]$callbackToWaitEnd)
Show-Stats 'App onDrawFrame duration' ([double[]]@($draws | ForEach-Object { ($_.End-$_.Start)*1000.0 }))
Show-Stats 'App eglSwapBuffers duration' ([double[]]@($swaps | ForEach-Object { ($_.End-$_.Start)*1000.0 }))
$drawWithoutReleaseWait = [Collections.Generic.List[double]]::new()
foreach ($draw in $draws) {
  $releaseWaitSeconds = 0.0
  foreach ($wait in $waits) {
    if ($wait.End -le $draw.Start) { continue }
    if ($wait.Start -ge $draw.End) { break }
    $releaseWaitSeconds += [Math]::Max(0.0, [Math]::Min($draw.End,$wait.End)-[Math]::Max($draw.Start,$wait.Start))
  }
  $drawWithoutReleaseWait.Add((($draw.End-$draw.Start)-$releaseWaitSeconds)*1000.0)
}
Show-Stats 'App onDrawFrame excluding release-wait overlap' ([double[]]$drawWithoutReleaseWait)
$queueToHwc = [Collections.Generic.List[double]]::new()
foreach ($queue in $queues) {
  $nextHwc = $flushes | Where-Object Start -ge $queue.End | Select-Object -First 1
  if ($nextHwc -and $nextHwc.Start-$queue.End -le 0.05) {
    $queueToHwc.Add(($nextHwc.Start-$queue.End)*1000.0)
  }
}
Show-Stats 'App queue end to next HWC flush start (within 50ms)' ([double[]]$queueToHwc)
Summarize-Ticks 'FLIP' $hit
Summarize-Ticks 'NO_FLIP' $missing
Write-Output 'First ten no-flip tick sequences and local timing:'
foreach ($tick in ($missing | Select-Object -First 10)) {
  $prior = @($flushes | Where-Object End -le $tick.Time | Select-Object -Last 1)
  $next = @($flushes | Where-Object Start -ge $tick.Time | Select-Object -First 1)
  $priorMs = if ($prior.Count) { [Math]::Round(($tick.Time-$prior[0].End)*1000.0,3) } else { [double]::NaN }
  $nextMs = if ($next.Count) { [Math]::Round(($next[0].Start-$tick.Time)*1000.0,3) } else { [double]::NaN }
  $currentCommit = @($commits | Where-Object { $_.Start -le $tick.Time -and $_.End -ge $tick.Time } | Select-Object -First 1)
  $commitAgeMs = if ($currentCommit.Count) { [Math]::Round(($tick.Time-$currentCommit[0].Start)*1000.0,3) } else { [double]::NaN }
  $commitRemainingMs = if ($currentCommit.Count) { [Math]::Round(($currentCommit[0].End-$tick.Time)*1000.0,3) } else { [double]::NaN }
  $inputFenceWait = @($fenceWaits | Where-Object { $_.Start -le $tick.Time -and $_.End -ge $tick.Time }).Count -gt 0
  Write-Output "seq=$($tick.Seq) tick=$($tick.Time) prior_flush_end_ms=$priorMs next_flush_start_ms=$nextMs commit_age_ms=$commitAgeMs commit_remaining_ms=$commitRemainingMs input_fence_wait=$inputFenceWait"
}
