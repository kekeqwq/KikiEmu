param(
  [Parameter(Mandatory)][string]$GuestTracePath,
  [Parameter(Mandatory)][string]$QemuTracePath,
  [string]$GuestBeginMarker = 'KIKI_GPU_BENCH_PIPELINE_BEGIN',
  [string]$GuestEndMarker = 'KIKI_GPU_BENCH_PIPELINE_END'
)

$ErrorActionPreference = 'Stop'

function Get-Percentile([double[]]$Values, [double]$Fraction) {
  if ($Values.Count -eq 0) { return [double]::NaN }
  $sorted = @($Values | Sort-Object)
  $index = [Math]::Max(0, [int][Math]::Ceiling($Fraction * $sorted.Count) - 1)
  return $sorted[$index]
}

function Write-Stats([string]$Name, [double[]]$Values, [string]$Unit = 'ms') {
  if ($Values.Count -eq 0) { "$Name`: no samples"; return }
  "{0}: N={1} P50={2:N3}{3} P95={4:N3}{3} MAX={5:N3}{3}" -f `
    $Name, $Values.Count, (Get-Percentile $Values 0.50), $Unit, `
    (Get-Percentile $Values 0.95), (Get-Percentile $Values 1.00)
}

$guestQueues = @{}
$guestResponses = @{}
$guestResourceFlushes = [Collections.Generic.List[object]]::new()
$guestCrtcSignals = [Collections.Generic.List[object]]::new()
$guestFlipEvents = [Collections.Generic.List[object]]::new()
$guestVblankTicks = [Collections.Generic.List[object]]::new()
$guestWaitStacks = @{}
$guestReleaseWaits = [Collections.Generic.List[object]]::new()
$inside = $false
$beginTime = [double]::NaN
$endTime = [double]::NaN
$guestLineRx = [regex]::new(
  '^\s*(?<comm>.*?)-(?<tid>\d+)\s+\(\s*(?<tgid>[\d-]+)\)\s+\[(?<cpu>[\d-]+)\]\s+\S+\s+(?<time>\d+\.\d+):\s+(?<event>[^:]+):\s*(?<payload>.*)$',
  [Text.RegularExpressions.RegexOptions]::Compiled)
$queueRx = [regex]::new(
  'type=0x(?<type>[0-9a-fA-F]+).*?fence_id=(?<fence>\d+).*?ctx_id=(?<ctx>\d+)',
  [Text.RegularExpressions.RegexOptions]::Compiled)

foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $GuestTracePath))) {
  $m = $guestLineRx.Match($line)
  if (-not $m.Success) { continue }
  $event = $m.Groups['event'].Value.Trim()
  $payload = $m.Groups['payload'].Value
  $time = [double]::Parse($m.Groups['time'].Value, [Globalization.CultureInfo]::InvariantCulture)
  if ($event -eq 'tracing_mark_write' -and $payload -eq $GuestBeginMarker) {
    $inside = $true
    $beginTime = $time
  }
  if ($inside -and $event -eq 'virtio_gpu_cmd_queue' -and $payload -match 'type=0x104\b') {
    $seq = [regex]::Match($payload, 'seqno=(?<seq>\d+)').Groups['seq'].Value
    $guestResourceFlushes.Add([pscustomobject]@{ Time=$time; Seq=$seq })
  }
  if ($inside -and $event -eq 'dma_fence_signaled' -and $payload -match 'timeline=CRTC:') {
    $seq = [regex]::Match($payload, 'seqno=(?<seq>\d+)').Groups['seq'].Value
    $guestCrtcSignals.Add([pscustomobject]@{ Time=$time; Seq=$seq; Payload=$payload })
  }
  if ($inside -and $event -eq 'drm_vblank_event_delivered' -and $payload -match 'crtc=0') {
    $seq = [regex]::Match($payload, 'seq=(?<seq>\d+)').Groups['seq'].Value
    $guestFlipEvents.Add([pscustomobject]@{ Time=$time; Seq=$seq })
  }
  if ($inside -and $event -eq 'drm_vblank_event' -and $payload -match 'crtc=0') {
    $seq = [regex]::Match($payload, 'seq=(?<seq>\d+)').Groups['seq'].Value
    $timeNs = [regex]::Match($payload, 'time=(?<ns>\d+)').Groups['ns'].Value
    if ($timeNs) {
      $vblankTimestamp = [double]$timeNs / 1000000000.0
      $guestVblankTicks.Add([pscustomobject]@{ Time=$vblankTimestamp; TraceTime=$time; Seq=$seq })
    }
  }
  if ($inside -and $event -eq 'tracing_mark_write') {
    $slice = [regex]::Match($payload, '^(?<kind>[BE])\|(?<pid>\d+)\|?(?<name>.*)$')
    if ($slice.Success) {
      # B/E nesting is thread-local even though the atrace payload repeats a process id.
      $sliceTid = $m.Groups['tid'].Value
      if ($slice.Groups['kind'].Value -eq 'B') {
        if (-not $guestWaitStacks.ContainsKey($sliceTid)) {
          $guestWaitStacks[$sliceTid] = [Collections.Generic.List[object]]::new()
        }
        $guestWaitStacks[$sliceTid].Add([pscustomobject]@{ Name=$slice.Groups['name'].Value; Time=$time })
      } elseif ($guestWaitStacks.ContainsKey($sliceTid) -and $guestWaitStacks[$sliceTid].Count -gt 0) {
        $stack = $guestWaitStacks[$sliceTid]
        $top = $stack[$stack.Count - 1]
        $stack.RemoveAt($stack.Count - 1)
        if ($top.Name -eq 'waitForBufferRelease' -and $time -ge $top.Time) {
          $guestReleaseWaits.Add([pscustomobject]@{ Start=$top.Time; End=$time; Tid=$sliceTid })
        }
      }
    }
  }
  if ($inside -and ($event -eq 'virtio_gpu_cmd_queue' -or $event -eq 'virtio_gpu_cmd_response')) {
    $mQueue = $queueRx.Match($payload)
    if ($mQueue.Success -and $mQueue.Groups['fence'].Value -ne '0') {
      $id = $mQueue.Groups['fence'].Value
      if ($event -eq 'virtio_gpu_cmd_queue' -and $mQueue.Groups['type'].Value -eq '207') {
        $guestQueues[$id] = $time
      } elseif ($event -eq 'virtio_gpu_cmd_response') {
        $guestResponses[$id] = $time
      }
    }
  }
  if ($event -eq 'tracing_mark_write' -and $payload -eq $GuestEndMarker) {
    $endTime = $time
    break
  }
}
if ([double]::IsNaN($beginTime) -or [double]::IsNaN($endTime) -or $endTime -le $beginTime) {
  throw 'Guest trace has no valid pipeline begin/end markers.'
}

$hostFences = @{}
$hostEvents = [Collections.Generic.List[object]]::new()
$hostLineRx = [regex]::new(
  '^(?<time>\d{4}-\d\d-\d\dT[\d:.]+Z)\s+(?<event>\S+)(?:\s+(?<payload>.*))?$',
  [Text.RegularExpressions.RegexOptions]::Compiled)
$hostFenceRx = [regex]::new(
  'fence 0x(?<id>[0-9a-fA-F]+)(?:, type 0x(?<type>[0-9a-fA-F]+))?',
  [Text.RegularExpressions.RegexOptions]::Compiled)

foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $QemuTracePath))) {
  if ($line.IndexOf(' virtio_gpu_fence_', [StringComparison]::Ordinal) -lt 0 -and
      $line.IndexOf(' virtio_gpu_ctrl_', [StringComparison]::Ordinal) -lt 0 -and
      $line.IndexOf(' virtio_gpu_cmd_process_', [StringComparison]::Ordinal) -lt 0 -and
      $line.IndexOf(' kiki_gtk_', [StringComparison]::Ordinal) -lt 0) { continue }
  $m = $hostLineRx.Match($line)
  if (-not $m.Success) { continue }
  $event = $m.Groups['event'].Value
  $payload = $m.Groups['payload'].Value
  $hostDate = [DateTimeOffset]::Parse($m.Groups['time'].Value, [Globalization.CultureInfo]::InvariantCulture).UtcDateTime
  $time = ($hostDate.Ticks - [DateTime]::UnixEpoch.Ticks) / [TimeSpan]::TicksPerSecond
  $hostEvents.Add([pscustomobject]@{ Time=$time; Event=$event; Payload=$payload })
  if ($event -ne 'virtio_gpu_fence_ctrl' -and $event -ne 'virtio_gpu_fence_resp') { continue }
  $mFence = $hostFenceRx.Match($payload)
  if (-not $mFence.Success) { continue }
  $id = [Convert]::ToUInt64($mFence.Groups['id'].Value, 16).ToString([Globalization.CultureInfo]::InvariantCulture)
  if (-not $hostFences.ContainsKey($id)) { $hostFences[$id] = @{} }
  if ($event -eq 'virtio_gpu_fence_ctrl' -and $mFence.Groups['type'].Value -eq '207') { $hostFences[$id].Ctrl = $time }
  if ($event -eq 'virtio_gpu_fence_resp') { $hostFences[$id].Resp = $time }
}

$offsets = [Collections.Generic.List[double]]::new()
$matched = 0
foreach ($id in $guestQueues.Keys) {
  if (-not $guestResponses.ContainsKey($id) -or -not $hostFences.ContainsKey($id)) { continue }
  $h = $hostFences[$id]
  if (-not $h.ContainsKey('Ctrl') -or -not $h.ContainsKey('Resp')) { continue }
  if ($guestResponses[$id] -lt $guestQueues[$id] -or $h.Resp -lt $h.Ctrl) { continue }
  $offsets.Add((($h.Ctrl - $guestQueues[$id]) + ($h.Resp - $guestResponses[$id])) / 2.0)
  $matched++
}
if ($matched -lt 20) { throw "Too few guest/QEMU SUBMIT_3D fence pairs for clock alignment: $matched" }
$offset = Get-Percentile ([double[]]$offsets) 0.50
$offsetResidualMs = [double[]]@($offsets | ForEach-Object { [Math]::Abs($_ - $offset) * 1000.0 })
$hostBegin = $beginTime + $offset
$hostEnd = $endTime + $offset
$vblankTimesSorted = @($guestFlipEvents | Sort-Object Time)
$vblankTickTimesSorted = @($guestVblankTicks | Sort-Object Time)
$window = @($hostEvents | Where-Object { $_.Time -ge ($hostBegin - 0.025) -and $_.Time -le ($hostEnd + 0.025) })
$contextWindow = @($hostEvents | Where-Object { $_.Time -ge ($hostBegin - 0.100) -and $_.Time -le ($hostEnd + 0.100) })

"Guest trace window: $([Math]::Round($endTime-$beginTime,3))s; guest/QEMU matching SUBMIT_3D ids=$matched"
"Estimated QEMU-host minus guest-trace clock offset: $([Math]::Round($offset,6))s"
Write-Stats 'Clock-anchor absolute residual' $offsetResidualMs
"Aligned QEMU window UTC: $([DateTimeOffset]::FromUnixTimeMilliseconds([long]($hostBegin*1000)).ToString('o')) .. $([DateTimeOffset]::FromUnixTimeMilliseconds([long]($hostEnd*1000)).ToString('o')); events=$($window.Count)"

$flushBySeq = @{}
$drawStartBySeq = @{}
$drawEndBySeq = @{}
$seqRx = [regex]::new('seq=(?<seq>\d+)', [Text.RegularExpressions.RegexOptions]::Compiled)
foreach ($e in $window) {
  if ($e.Event -notin @('kiki_gtk_scanout_flush','kiki_gtk_draw_start','kiki_gtk_draw_end')) { continue }
  $mSeq = $seqRx.Match($e.Payload)
  if (-not $mSeq.Success) { continue }
  $seq = $mSeq.Groups['seq'].Value
  switch ($e.Event) {
    'kiki_gtk_scanout_flush' { $flushBySeq[$seq] = $e.Time }
    'kiki_gtk_draw_start' { $drawStartBySeq[$seq] = $e.Time }
    'kiki_gtk_draw_end' { $drawEndBySeq[$seq] = $e.Time }
  }
}
$flushToDraw = [Collections.Generic.List[double]]::new()
$drawDuration = [Collections.Generic.List[double]]::new()
$scanoutIntervals = [Collections.Generic.List[object]]::new()
foreach ($seq in $flushBySeq.Keys) {
  if (-not $drawStartBySeq.ContainsKey($seq) -or -not $drawEndBySeq.ContainsKey($seq)) { continue }
  $startDelay = ($drawStartBySeq[$seq]-$flushBySeq[$seq])*1000.0
  $duration = ($drawEndBySeq[$seq]-$drawStartBySeq[$seq])*1000.0
  if ($startDelay -lt 0 -or $duration -lt 0) { continue }
  $flushToDraw.Add($startDelay)
  $drawDuration.Add($duration)
  $scanoutIntervals.Add([pscustomobject]@{ Start=$flushBySeq[$seq]; Draw=$drawStartBySeq[$seq]; End=$drawEndBySeq[$seq]; Seq=$seq })
}
Write-Stats 'QEMU scanout flush→GTK draw start' ([double[]]$flushToDraw)
Write-Stats 'QEMU GTK draw callback' ([double[]]$drawDuration)

$bhIntervals = [Collections.Generic.List[object]]::new()
$bhDurations = [Collections.Generic.List[double]]::new()
$commandDurations = [Collections.Generic.List[double]]::new()
$batchCommandCounts = [Collections.Generic.List[double]]::new()
$callbackCount = @($window | Where-Object Event -eq 'virtio_gpu_ctrl_cb').Count
$bhEnterCount = @($window | Where-Object Event -eq 'virtio_gpu_ctrl_bh_enter').Count
$bhExitCount = @($window | Where-Object Event -eq 'virtio_gpu_ctrl_bh_exit').Count
$activeBh = $null
$activeCommandStart = [double]::NaN
foreach ($e in $window) {
  switch ($e.Event) {
    'virtio_gpu_ctrl_bh_enter' {
      $activeBh = [pscustomobject]@{ Start=$e.Time; End=[double]::NaN; Count=0 }
      $activeCommandStart = [double]::NaN
    }
    'virtio_gpu_cmd_process_start' {
      $activeCommandStart = $e.Time
    }
    'virtio_gpu_cmd_process_done' {
      if (-not [double]::IsNaN($activeCommandStart)) {
        $duration = ($e.Time-$activeCommandStart)*1000.0
        if ($duration -ge 0) { $commandDurations.Add($duration) }
        if ($null -ne $activeBh) { $activeBh.Count++ }
        $activeCommandStart = [double]::NaN
      }
    }
    'virtio_gpu_ctrl_bh_exit' {
      if ($null -eq $activeBh) { continue }
      $activeBh.End = $e.Time
      $duration = ($activeBh.End-$activeBh.Start)*1000.0
      if ($duration -ge 0) {
        $bhDurations.Add($duration)
        $batchCommandCounts.Add([double]$activeBh.Count)
        $bhIntervals.Add($activeBh)
      }
      $activeBh = $null
    }
  }
}
"QEMU control event counts: callbacks=$callbackCount, BH_enter=$bhEnterCount, BH_exit=$bhExitCount; per-callback delay is not paired because QEMU coalesces schedules while a BH is pending."
Write-Stats 'QEMU control bottom-half duration' ([double[]]$bhDurations)
Write-Stats 'QEMU command handler duration' ([double[]]$commandDurations)
Write-Stats 'commands per control bottom-half' ([double[]]$batchCommandCounts) 'cmds'
$bhLong = @($bhIntervals | Where-Object { ($_.End-$_.Start)*1000.0 -gt 16.667 })
"Control bottom halves >16.667ms: $($bhLong.Count)/$($bhIntervals.Count)"
$flushOverlaps = 0
foreach ($scanout in $scanoutIntervals) {
  if (@($bhIntervals | Where-Object { $_.Start -lt $scanout.Draw -and $_.End -gt $scanout.Start }).Count -gt 0) { $flushOverlaps++ }
}
"Scanout flush→draw intervals overlapping a control BH: $flushOverlaps/$($scanoutIntervals.Count)"

# Cross-clock event proximity is reported as correlation, not strict causality:
# RESOURCE_FLUSH is queued in the guest, while the QEMU GTK trace records later
# display-listener activity and can coalesce or omit updates.
$qemuFlushTimes = @($contextWindow | Where-Object Event -eq 'kiki_gtk_scanout_flush' | Sort-Object Time)
$flushLags = [Collections.Generic.List[double]]::new()
$flushNearestAbs = [Collections.Generic.List[double]]::new()
$matchedFlushDrawDelay = [Collections.Generic.List[double]]::new()
$pairedVblankToGtkDraw = [Collections.Generic.List[double]]::new()
$pairedFenceToVblank = [Collections.Generic.List[double]]::new()
$flushMatched = 0
$lastQemuFlushIndex = -1
$guestFlushesSorted = @($guestResourceFlushes | Sort-Object Time)
for ($guestIndex = 0; $guestIndex -lt $guestFlushesSorted.Count; $guestIndex++) {
  $guestFlush = $guestFlushesSorted[$guestIndex]
  $guestHostTime = $guestFlush.Time + $offset
  $nearest = $null
  $nearestIndex = -1
  $nearestAbs = [double]::PositiveInfinity
  for ($qemuIndex = $lastQemuFlushIndex + 1; $qemuIndex -lt $qemuFlushTimes.Count; $qemuIndex++) {
    $qemuFlush = $qemuFlushTimes[$qemuIndex]
    $delta = ($qemuFlush.Time - $guestHostTime) * 1000.0
    if ($delta -gt 20.0 -and $null -ne $nearest) { break }
    $absDelta = [Math]::Abs($delta)
    if ($absDelta -lt $nearestAbs) { $nearest = $qemuFlush; $nearestIndex = $qemuIndex; $nearestAbs = $absDelta }
  }
  if ($null -ne $nearest -and $nearestAbs -le 20.0) {
    $flushMatched++
    $lastQemuFlushIndex = $nearestIndex
    $flushLags.Add(($nearest.Time - $guestHostTime) * 1000.0)
    $flushNearestAbs.Add($nearestAbs)
    $mMatchedSeq = $seqRx.Match($nearest.Payload)
    if ($mMatchedSeq.Success) {
      $matchedSeq = $mMatchedSeq.Groups['seq'].Value
      if ($drawStartBySeq.ContainsKey($matchedSeq)) {
        $drawHostTime = $drawStartBySeq[$matchedSeq]
        $matchedFlushDrawDelay.Add(($drawHostTime-$guestHostTime)*1000.0)
        $followingVblank = @($vblankTimesSorted | Where-Object { $_.Time -ge $guestFlush.Time } | Select-Object -First 1)
        $followingFence = @($guestCrtcSignals | Where-Object { $_.Time -ge $guestFlush.Time } | Sort-Object Time | Select-Object -First 1)
        if ($followingVblank.Count -gt 0 -and $followingFence.Count -gt 0 -and
            ($followingVblank[0].Time-$guestFlush.Time) -le 0.050 -and
            ($followingFence[0].Time-$guestFlush.Time) -le 0.050) {
          $pairedVblankToGtkDraw.Add(($drawHostTime-($followingVblank[0].Time+$offset))*1000.0)
          $pairedFenceToVblank.Add(($followingFence[0].Time-$followingVblank[0].Time)*1000.0)
        }
      }
    }
  }
}
"Guest RESOURCE_FLUSH type=0x104: $($guestResourceFlushes.Count); QEMU GTK scanout_flush in padded window: $($qemuFlushTimes.Count); unique monotonic pairs within 20ms: $flushMatched; unmatched guest flushes: $($guestResourceFlushes.Count-$flushMatched)"
Write-Stats 'Nearest QEMU scanout_flush minus guest RESOURCE_FLUSH' ([double[]]$flushLags)
Write-Stats 'Absolute RESOURCE_FLUSH/scanout_flush separation' ([double[]]$flushNearestAbs)
Write-Stats 'Same-sequence guest RESOURCE_FLUSH→QEMU GTK draw start' ([double[]]$matchedFlushDrawDelay)
Write-Stats 'Same-frame vblank delivery→QEMU GTK draw start' ([double[]]$pairedVblankToGtkDraw)
Write-Stats 'Next CRTC fence signal minus vblank delivery' ([double[]]$pairedFenceToVblank)

$waitSignalLatency = [Collections.Generic.List[double]]::new()
$signalToWaitReturn = [Collections.Generic.List[double]]::new()
$waitWithSignal = 0
$waitWithQemuFlush = 0
$waitSignalToQemuDraw = [Collections.Generic.List[double]]::new()
$crtcSignalHostTimes = @($guestCrtcSignals | ForEach-Object {
  [pscustomobject]@{ Time=($_.Time+$offset); Seq=$_.Seq }
} | Sort-Object Time)
foreach ($wait in $guestReleaseWaits) {
  $waitStart = $wait.Start + $offset
  $waitEnd = $wait.End + $offset
  $signalsDuringWait = @($crtcSignalHostTimes | Where-Object { $_.Time -ge $waitStart -and $_.Time -le $waitEnd })
  if ($signalsDuringWait.Count -gt 0) {
    $waitWithSignal++
    $signal = $signalsDuringWait[0]
    $waitSignalLatency.Add(($signal.Time-$waitStart)*1000.0)
    $signalToWaitReturn.Add(($waitEnd-$signal.Time)*1000.0)
    $precedingFlush = @($qemuFlushTimes | Where-Object { $_.Time -le $signal.Time } | Select-Object -Last 1)
    $followingDraw = @($contextWindow | Where-Object { $_.Event -eq 'kiki_gtk_draw_start' -and $_.Time -ge $signal.Time } | Sort-Object Time | Select-Object -First 1)
    if ($precedingFlush.Count -gt 0 -and $followingDraw.Count -gt 0) {
      $waitWithQemuFlush++
      $waitSignalToQemuDraw.Add(($followingDraw[0].Time-$signal.Time)*1000.0)
    }
  }
}
"waitForBufferRelease slices: $($guestReleaseWaits.Count); containing a CRTC fence signal: $waitWithSignal"
Write-Stats 'wait start→first CRTC signal' ([double[]]$waitSignalLatency)
Write-Stats 'CRTC signal→wait return' ([double[]]$signalToWaitReturn)
"Wait/signal pairs with surrounding QEMU flush + next GTK draw: $waitWithQemuFlush"
Write-Stats 'CRTC signal→next QEMU GTK draw start' ([double[]]$waitSignalToQemuDraw)

$vblankIntervalsMs = [Collections.Generic.List[double]]::new()
for ($i = 1; $i -lt $vblankTimesSorted.Count; $i++) {
  $vblankIntervalsMs.Add(($vblankTimesSorted[$i].Time-$vblankTimesSorted[$i-1].Time)*1000.0)
}
"Guest CRTC0 flip/vblank completion events: $($guestFlipEvents.Count)"
Write-Stats 'Guest flip/vblank completion interval' ([double[]]$vblankIntervalsMs)

$vblankTickIntervalsMs = [Collections.Generic.List[double]]::new()
$vblankTickServiceDelayMs = [Collections.Generic.List[double]]::new()
for ($i = 1; $i -lt $vblankTickTimesSorted.Count; $i++) {
  $vblankTickIntervalsMs.Add(($vblankTickTimesSorted[$i].Time-$vblankTickTimesSorted[$i-1].Time)*1000.0)
}
foreach ($tick in $guestVblankTicks) {
  $vblankTickServiceDelayMs.Add(($tick.TraceTime-$tick.Time)*1000.0)
}
"Guest CRTC0 drm_vblank_event ticks: $($guestVblankTicks.Count)"
Write-Stats 'Guest CRTC0 vblank timestamp interval' ([double[]]$vblankTickIntervalsMs)
Write-Stats 'vblank timestamp→ftrace event service delay' ([double[]]$vblankTickServiceDelayMs)

$fenceToVblankMs = [Collections.Generic.List[double]]::new()
$usedVblank = @{}
foreach ($fence in ($guestCrtcSignals | Sort-Object Time)) {
  $nearest = $null
  $nearestIndex = -1
  $nearestAbs = [double]::PositiveInfinity
  for ($i = 0; $i -lt $vblankTickTimesSorted.Count; $i++) {
    if ($usedVblank.ContainsKey($i)) { continue }
    $candidate = $vblankTickTimesSorted[$i]
    $absDelta = [Math]::Abs(($fence.Time-$candidate.Time)*1000.0)
    if ($absDelta -lt $nearestAbs) { $nearest=$candidate; $nearestIndex=$i; $nearestAbs=$absDelta }
  }
  if ($null -ne $nearest -and $nearestAbs -le 4.0) {
    $usedVblank[$nearestIndex] = $true
    $fenceToVblankMs.Add(($fence.Time-$nearest.Time)*1000.0)
  }
}
"Unique CRTC fence/vblank-timestamp pairs within 4ms: $($fenceToVblankMs.Count)"
Write-Stats 'CRTC fence signal minus drm_vblank_event timestamp' ([double[]]$fenceToVblankMs)

$gtkDrawTimes = @($contextWindow | Where-Object Event -eq 'kiki_gtk_draw_start' | Sort-Object Time)
$lastGtkDrawIndex = -1
$vblankToGtkDrawMs = [Collections.Generic.List[double]]::new()
foreach ($vblank in $vblankTimesSorted) {
  $vblankHostTime = $vblank.Time + $offset
  $nearest = $null
  $nearestIndex = -1
  $nearestAbs = [double]::PositiveInfinity
  for ($i = $lastGtkDrawIndex + 1; $i -lt $gtkDrawTimes.Count; $i++) {
    $candidate = $gtkDrawTimes[$i]
    $delta = ($candidate.Time-$vblankHostTime)*1000.0
    if ($delta -gt 20.0 -and $null -ne $nearest) { break }
    $absDelta = [Math]::Abs($delta)
    if ($absDelta -lt $nearestAbs) { $nearest=$candidate; $nearestIndex=$i; $nearestAbs=$absDelta }
  }
  if ($null -ne $nearest -and $nearestAbs -le 20.0) {
    $lastGtkDrawIndex = $nearestIndex
    $vblankToGtkDrawMs.Add(($nearest.Time-$vblankHostTime)*1000.0)
  }
}
"Unique monotonic guest-flip-event/QEMU-GTK-draw pairs within 20ms: $($vblankToGtkDrawMs.Count)/$($guestFlipEvents.Count)"
Write-Stats 'QEMU GTK draw start minus guest flip/vblank completion event' ([double[]]$vblankToGtkDrawMs)

# The GtkGLArea render callback returns before GDK's Win32 WGL end-frame path.
# This optional trace pairs each draw end with the first after-paint marker.
# Some builds emit multiple after-paint callbacks for one frame clock; extra
# callbacks are ignored rather than counted as additional presented frames.
$postRenderIntervals = [Collections.Generic.List[object]]::new()
$pendingDrawEnd = $null
foreach ($e in $window) {
  if ($e.Event -eq 'kiki_gtk_draw_end') {
    $pendingDrawEnd = $e.Time
  } elseif ($e.Event -eq 'kiki_gtk_after_paint' -and $null -ne $pendingDrawEnd -and
            $e.Time -ge $pendingDrawEnd) {
    $postRenderIntervals.Add([pscustomobject]@{ Start=$pendingDrawEnd; End=$e.Time })
    $pendingDrawEnd = $null
  }
}

function Summarize-PostRenderAtTick([string]$Label, [object[]]$Ticks) {
  $overlapMs = [Collections.Generic.List[double]]::new()
  $activeAtTick = 0
  $windowSeconds = 1.0/60.0
  foreach ($tick in $Ticks) {
    $hostTick = $tick.Time + $offset
    $start = $hostTick - $windowSeconds
    $overlap = 0.0
    foreach ($interval in $postRenderIntervals) {
      if ($interval.End -le $start) { continue }
      if ($interval.Start -ge $hostTick) { break }
      $overlap += [Math]::Max(0.0,
        [Math]::Min($hostTick,$interval.End)-[Math]::Max($start,$interval.Start))
      if ($interval.Start -le $hostTick -and $interval.End -ge $hostTick) {
        $activeAtTick++
      }
    }
    $overlapMs.Add($overlap*1000.0)
  }
  "${Label}: ticks=$($Ticks.Count) GTK_POST_RENDER_ACTIVE_AT_TICK=$activeAtTick"
  Write-Stats "${Label} GTK post-render overlap in preceding 16.667ms" ([double[]]$overlapMs)
}

"GTK draw_end→after_paint intervals in aligned window: $($postRenderIntervals.Count)"
if ($postRenderIntervals.Count) {
  $flipTickGroup = @($guestVblankTicks | Where-Object { $guestFlipEvents.Seq -contains $_.Seq })
  $missTickGroup = @($guestVblankTicks | Where-Object { $guestFlipEvents.Seq -notcontains $_.Seq })
  Summarize-PostRenderAtTick 'FLIP_TICK' $flipTickGroup
  Summarize-PostRenderAtTick 'NO_FLIP_TICK' $missTickGroup
}
