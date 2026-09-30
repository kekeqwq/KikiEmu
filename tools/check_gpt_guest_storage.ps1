# SPDX-License-Identifier: GPL-2.0-or-later
# Read-only SYSTEM prototype acceptance; not the public create/installer test.
param(
    [Parameter(Mandatory)][int]$QemuProcessId,
    [Parameter(Mandatory)][string]$InstanceDirectory,
    [Parameter(Mandatory)][string]$QemuPath,
    [ValidateRange(1024, 65535)][int]$AdbPort = 5555
)
$ErrorActionPreference = 'Stop'
$instance = (Resolve-Path -LiteralPath $InstanceDirectory).Path
$exe = (Resolve-Path -LiteralPath $QemuPath).Path
$layout = Get-Content -LiteralPath (Join-Path $instance 'disk-layout.json') -Raw | ConvertFrom-Json
if ($layout.layoutVersion -ne 'gpt-v1' -or $layout.diskFile -ne 'phone.qcow2') {
    throw 'This check only supports the recorded GPT-v1 prototype.'
}
$disk = Join-Path $instance 'phone.qcow2'
$creation = (Get-CimInstance Win32_Process -Filter "ProcessId=$QemuProcessId").CreationDate
if (-not $creation) { throw 'The specified QEMU process is not running.' }
$adb = (Get-Command adb -ErrorAction Stop).Source
$serial = "127.0.0.1:$AdbPort"
function Confirm-Owner {
    $process = Get-CimInstance Win32_Process -Filter "ProcessId=$QemuProcessId"
    if (-not $process -or $process.CreationDate -ne $creation -or
        -not [string]::Equals($process.ExecutablePath, $exe, [StringComparison]::OrdinalIgnoreCase) -or
        $process.CommandLine.IndexOf($disk, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw 'QEMU creation/EXE/disk identity mismatch; refusing ADB access.'
    }
    $listeners = @(Get-NetTCPConnection -LocalPort $AdbPort -State Listen -ErrorAction Stop)
    if ($listeners.Count -ne 1 -or $listeners[0].LocalAddress -ne '127.0.0.1' -or
        $listeners[0].OwningProcess -ne $QemuProcessId) {
        throw 'The private ADB endpoint is not owned by this exact QEMU process.'
    }
}
function Read-Guest([string]$Command) {
    Confirm-Owner
    $lines = & $adb -s $serial shell $Command
    if ($LASTEXITCODE -ne 0) { throw "Guest read failed: $Command" }
    return ($lines -join "`n").Trim()
}
function Read-ParcelLong([string]$Parcel) {
    # Pinned CP2A read-only AIDL transactions below return exception + int64.
    # Remove addresses and ASCII annotations, not the actual eight-digit words.
    $body = ($Parcel -split 'Parcel\(', 2)[-1]
    $body = ($body -split "'", 2)[0] -replace '0x[0-9a-fA-F]+:', ''
    $words = @([regex]::Matches($body, '\b[0-9a-fA-F]{8}\b') | ForEach-Object Value)
    if ($words.Count -ne 3 -or $words[0] -ne '00000000') {
        throw "Unexpected storage-service reply: $Parcel"
    }
    return [uint64]([Convert]::ToUInt32($words[1], 16)) +
        [uint64]([Convert]::ToUInt32($words[2], 16)) * [uint64]4294967296
}
if ((Read-Guest 'getprop sys.boot_completed') -ne '1') { throw 'Android has not finished booting.' }
if ((Read-Guest 'getprop ro.kikiaosp.exact_storage_size') -ne 'true') {
    throw 'The candidate does not enable exact storage capacity reporting.'
}
$boot = @($layout.partitions | Where-Object name -eq 'boot')[0]
if ((Read-Guest 'getprop ro.boot.boot_part_uuid') -ne $boot.uuid) {
    throw 'The live guest is not the recorded installed boot partition.'
}
$sectors = [uint64](Read-Guest 'cat /sys/block/vda/size')
$kernelBytes = $sectors * [uint64]512
# IStorageManager.aidl uses explicit zero-based method ID 98; transaction=99.
$voldBytes = Read-ParcelLong (Read-Guest 'service call mount 99')
# IStorageStatsManager: getTotalBytes is transaction 3. Encode a NULL String16
# UUID as -1, NOT the literal string "null"; then pass the caller package.
$statsBytes = Read-ParcelLong (Read-Guest 'service call storagestats 3 i32 -1 s16 com.android.shell')
$stat = (Read-Guest "stat -f -c '%S %b %a' /data") -split '\s+'
if ($stat.Count -ne 3) { throw 'Unexpected F2FS statfs output.' }
$dataBytes = [uint64]$stat[0] * [uint64]$stat[1]
$available = [uint64]$stat[0] * [uint64]$stat[2]
$dataPartition = @($layout.partitions | Where-Object name -eq 'userdata')[0]
foreach ($value in @($kernelBytes, $voldBytes, $statsBytes)) {
    if ($value -ne [uint64]$layout.totalBytes) {
        throw "Capacity mismatch: requested=$($layout.totalBytes), observed=$value bytes."
    }
}
if ($dataBytes -gt [uint64]$dataPartition.lengthBytes -or $dataBytes -lt
    ([uint64]$dataPartition.lengthBytes - [uint64]67108864) -or $available -gt $dataBytes) {
    throw 'The live userdata filesystem does not match the installed partition capacity.'
}
[ordered]@{
    result = 'PASS: exact whole-disk capacity matches both Android storage services'
    diskUuid = $layout.diskUuid
    totalBytes = $kernelBytes
    voldBytes = $voldBytes
    storageStatsBytes = $statsBytes
    userdataFilesystemBytes = $dataBytes
    userdataAvailableBytes = $available
    systemAndReserveBytes = $kernelBytes - $dataBytes
    hostImageFileBytes = (Get-Item -LiteralPath $disk).Length
    boundary = 'Read-only system check; Settings screenshot and persistence still require observation.'
} | ConvertTo-Json
