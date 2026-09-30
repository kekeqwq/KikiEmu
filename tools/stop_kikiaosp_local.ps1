param(
    [Parameter(Mandatory)][ValidateRange(1, 2147483647)][int]$QemuProcessId,
    [ValidateRange(1024, 65535)][int]$MonitorPort = 4447,
    [ValidateRange(5, 60)][int]$TimeoutSeconds = 25
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$process = Get-Process -Id $QemuProcessId -ErrorAction Stop
if ($process.ProcessName -notlike 'qemu*' -or
    -not $process.Path.StartsWith(($repoRoot + '\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to stop a process outside this repository or a non-QEMU process.'
}
$listener = Get-NetTCPConnection -State Listen -LocalPort $MonitorPort -ErrorAction Stop
if ($listener.OwningProcess -ne $QemuProcessId) {
    throw "Monitor port $MonitorPort is not owned by QEMU PID $QemuProcessId."
}

# Release the actual Windows camera before requesting QEMU shutdown. Only use
# the ADB endpoint if its listener belongs to this exact QEMU instance.
$adbListener = Get-NetTCPConnection -State Listen -LocalPort 5555 -ErrorAction SilentlyContinue
$adb = Get-Command adb -ErrorAction SilentlyContinue
if ($adb -and $adbListener -and $adbListener.OwningProcess -eq $QemuProcessId) {
    $online = (& $adb.Source devices) -match '^127\.0\.0\.1:5555\s+device$'
    if ($online) {
        & $adb.Source -s 127.0.0.1:5555 shell am force-stop com.android.camera2
    }
}

$monitor = [Net.Sockets.TcpClient]::new('127.0.0.1', $MonitorPort)
try {
    $stream = $monitor.GetStream()
    $stream.ReadTimeout = 2500
    $buffer = [byte[]]::new(4096)
    [void]$stream.Read($buffer, 0, $buffer.Length)
    $command = [Text.Encoding]::ASCII.GetBytes("quit`n")
    $stream.Write($command, 0, $command.Length)
    $stream.Flush()
    # Keep the channel connected while QEMU consumes its HMP command and
    # finishes backend teardown. Disposing it immediately can lose the quit.
    try { [void]$stream.Read($buffer, 0, $buffer.Length) } catch [IO.IOException] {}
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        throw "QEMU PID $QemuProcessId did not exit; no forced kill was performed."
    }
} finally {
    $monitor.Dispose()
}
Write-Output "QEMU PID $QemuProcessId exited gracefully."
