param(
  [string]$AdbSerial = '127.0.0.1:5555',
  [string]$OutputDir = "$env:USERPROFILE\Downloads\temp"
)

$ErrorActionPreference = 'Stop'

function Invoke-Adb([string[]]$Arguments) {
  $output = @(& adb -s $AdbSerial @Arguments 2>&1)
  if ($LASTEXITCODE -ne 0) { throw "adb $($Arguments -join ' ') failed: $($output -join ' ')" }
  return $output
}

if (-not (Test-Path -LiteralPath $OutputDir -PathType Container)) {
  throw "Output directory does not exist: $OutputDir"
}
$size = (Invoke-Adb -Arguments @('shell', 'wm size')) -join ' '
if ($size -notmatch 'Physical size:\s*864x1728') { throw "Unexpected guest size: $size" }
$systemUiPid = ((Invoke-Adb -Arguments @('shell', 'pidof com.android.systemui')) -join '').Trim()
if ($systemUiPid -notmatch '^\d+$') { throw "Cannot identify SystemUI PID: $systemUiPid" }
$tracingOn = ((Invoke-Adb -Arguments @('shell', 'cat /sys/kernel/tracing/tracing_on')) -join '').Trim()
if ($tracingOn -ne '0') { throw "Tracefs is already active: $tracingOn" }

Invoke-Adb -Arguments @('shell', 'input keyevent HOME') | Out-Null
Start-Sleep -Milliseconds 500
$started = $false
$trace = @()
try {
  Invoke-Adb -Arguments @('shell', 'atrace -c -b 32768 --async_start gfx view input sched') | Out-Null
  $started = $true
  Invoke-Adb -Arguments @('shell', 'echo KIKI_SWIPE_DOWN_START > /sys/kernel/tracing/trace_marker') | Out-Null
  Invoke-Adb -Arguments @('shell', 'input swipe 432 2 432 1100 1200') | Out-Null
  Invoke-Adb -Arguments @('shell', 'echo KIKI_SWIPE_DOWN_END > /sys/kernel/tracing/trace_marker') | Out-Null
  Start-Sleep -Milliseconds 300
  Invoke-Adb -Arguments @('shell', 'echo KIKI_SWIPE_UP_START > /sys/kernel/tracing/trace_marker') | Out-Null
  Invoke-Adb -Arguments @('shell', 'input swipe 432 1100 432 2 700') | Out-Null
  Invoke-Adb -Arguments @('shell', 'echo KIKI_SWIPE_UP_END > /sys/kernel/tracing/trace_marker') | Out-Null
  $trace = @(Invoke-Adb -Arguments @('shell', 'atrace --async_stop'))
  $started = $false
} finally {
  if ($started) { Invoke-Adb -Arguments @('shell', 'atrace --async_stop') | Out-Null }
  $tracingOn = ((Invoke-Adb -Arguments @('shell', 'cat /sys/kernel/tracing/tracing_on')) -join '').Trim()
  if ($tracingOn -ne '0') {
    Invoke-Adb -Arguments @('shell', 'echo 0 > /sys/kernel/tracing/tracing_on') | Out-Null
    throw 'Tracefs remained active; disabled it and discarded this sample.'
  }
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$path = Join-Path $OutputDir "kiki-shade-input-pipeline-$stamp.trace"
$trace | Set-Content -LiteralPath $path -Encoding utf8
Write-Output "SHADE_PIPELINE_TRACE path=$path lines=$($trace.Count) systemui_pid=$systemUiPid tracing_on=$tracingOn"
$trace | Where-Object { $_ -match 'KIKI_SWIPE_(?:DOWN|UP)_(?:START|END)' }
