param(
  [ValidateSet(0, 3, 4, 5)][int]$BufferCount = 0,
  [ValidateRange(10, 120)][int]$DurationSeconds = 20,
  [ValidateRange(2, 30)][int]$WarmupSeconds = 5,
  [string]$AdbSerial = '127.0.0.1:5555',
  [int]$ExpectedWidth = 864,
  [int]$ExpectedHeight = 1728,
  [ValidateSet('60 FPS target', 'Game load', 'Extreme 3D')][string]$Profile = 'Extreme 3D',
  [string]$Label = 'extreme'
)

$ErrorActionPreference = 'Stop'

function Invoke-Guest([string[]]$Arguments) {
  $output = (& adb -s $AdbSerial @Arguments 2>&1 | Out-String).Trim()
  if ($LASTEXITCODE -ne 0) { throw "adb $($Arguments -join ' ') failed: $output" }
  return $output
}

function Get-BenchLog([string]$BenchProcessId) {
  Invoke-Guest -Arguments @('logcat', '-d', "--pid=$BenchProcessId", '-s', 'KikiGpuBench:I', '*:S')
}

$size = Invoke-Guest -Arguments @('shell', 'wm', 'size')
if ($size -notmatch 'Physical size:\s*(\d+)x(\d+)') { throw "Cannot read guest size: $size" }
$width = [int]$Matches[1]
$height = [int]$Matches[2]
$profileInstances = switch ($Profile) {
  '60 FPS target' { 1024 }
  'Game load' { 8192 }
  'Extreme 3D' { 32768 }
}
$profileId = switch ($Profile) {
  '60 FPS target' { 0 }
  'Game load' { 1 }
  'Extreme 3D' { 2 }
}
if ($width -ne $ExpectedWidth -or $height -ne $ExpectedHeight) {
  throw "Expected ${ExpectedWidth}x${ExpectedHeight}, got ${width}x${height}"
}
if ([Math]::Min($width, $height) -lt 864 -or [Math]::Max($width, $height) -lt 1728) {
  throw "Guest is below the 864x1728 floor: ${width}x${height}"
}
$sleepState = Invoke-Guest -Arguments @('shell', 'dumpsys', 'activity', 'activities')
if ($sleepState -notmatch 'mSleeping=false') { throw 'Android display is asleep; benchmark would be invalid.' }

[void](Invoke-Guest -Arguments @('shell', 'am', 'force-stop', 'com.kiki.gpubench'))
[void](Invoke-Guest -Arguments @(
  'shell', 'am', 'start', '-W', '-n', 'com.kiki.gpubench/.GpuBenchActivity',
  '--ei', 'profile', [string]$profileId,
  '--ei', 'buffer_count', [string]$BufferCount
))
Start-Sleep -Seconds $WarmupSeconds
$benchProcessId = (Invoke-Guest -Arguments @('shell', 'pidof', 'com.kiki.gpubench')).Trim()
if ($benchProcessId -notmatch '^\d+$') { throw "Invalid benchmark PID: $benchProcessId" }
$before = Get-BenchLog $benchProcessId
if ($before -notmatch "SURFACE=${width}x${height}" -or
    $before -notmatch "PROFILE=$([regex]::Escape($Profile)) INSTANCES=$profileInstances") {
  throw "The full-resolution '$Profile' scene is not active."
}
if ($BufferCount -gt 0 -and $before -notmatch "BUFFER_COUNT_REQUEST=$BufferCount RESULT=0") {
  throw "Buffer count request $BufferCount was not accepted: $before"
}
if ($BufferCount -eq 0 -and $before -match 'BUFFER_COUNT_REQUEST=') {
  throw 'The default-buffer run unexpectedly requested a buffer count.'
}
$beforeFrameCount = @($before -split "`n" | Where-Object { $_ -match ' FPS=' }).Count
$started = Get-Date
Start-Sleep -Seconds $DurationSeconds
$after = Get-BenchLog $benchProcessId
$frameLines = @($after -split "`n" | Where-Object { $_ -match ' FPS=' } | Select-Object -Skip $beforeFrameCount)
$records = [System.Collections.Generic.List[object]]::new()
foreach ($line in $frameLines) {
  if ($line -notmatch "FPS=([0-9.]+).*?CLEAR_MS=([0-9.]+).*?GPU_DRAW_MS=([0-9.]+).*?PROFILE=$([regex]::Escape($Profile)) INSTANCES=$profileInstances RES=(\d+)x(\d+)") {
    throw "Unexpected benchmark record: $line"
  }
  if ([int]$Matches[4] -ne $width -or [int]$Matches[5] -ne $height) {
    throw "Benchmark changed resolution: $line"
  }
  $records.Add([pscustomobject]@{
    Fps = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
    ClearMs = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
    GpuDrawMs = [double]::Parse($Matches[3], [Globalization.CultureInfo]::InvariantCulture)
  })
}
if ($records.Count -lt 8) {
  throw "Only $($records.Count) benchmark records during ${DurationSeconds}s; check app focus and display power."
}

function Get-Median([double[]]$Values) {
  $sorted = @($Values | Sort-Object)
  $middle = [int][Math]::Floor($sorted.Count / 2)
  if ($sorted.Count % 2) { return $sorted[$middle] }
  return ($sorted[$middle - 1] + $sorted[$middle]) / 2
}

$fps = @($records | ForEach-Object Fps)
$clearMs = @($records | ForEach-Object ClearMs)
$gpuDrawMs = @($records | ForEach-Object GpuDrawMs)
$minimum = ($fps | Measure-Object -Minimum).Minimum
$maximum = ($fps | Measure-Object -Maximum).Maximum
$sleepAfter = Invoke-Guest -Arguments @('shell', 'dumpsys', 'activity', 'activities')
if ($sleepAfter -notmatch 'mSleeping=false') { throw 'Android display slept during the benchmark.' }
Write-Output ('GPU_BENCH_RESULT label={0} profile="{1}" buffer_count={2} guest={3}x{4} instances={5} duration={6}s samples={7} missing_second_windows={8} fps_median={9:N2} fps_min={10:N2} fps_max={11:N2} clear_ms_median={12:N3} gpu_draw_ms_median={13:N3} started={14}' -f $Label, $Profile, $BufferCount, $width, $height, $profileInstances, $DurationSeconds, $records.Count, [Math]::Max(0, $DurationSeconds - $records.Count), (Get-Median $fps), $minimum, $maximum, (Get-Median $clearMs), (Get-Median $gpuDrawMs), $started.ToString('o'))
