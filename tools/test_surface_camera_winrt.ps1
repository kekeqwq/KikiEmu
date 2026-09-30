param(
    [ValidateSet('front', 'rear')][string]$Facing = 'front',
    [ValidateSet('reader', 'preview')][string]$Mode = 'reader',
    [ValidateRange(2, 120)][int]$Seconds = 12,
    [switch]$CpuMemory,
    [switch]$CopyFrames
)

# Diagnostic only: run with Windows PowerShell 5.1 -STA. No camera images are saved.
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -ne 'Desktop') {
    throw 'Use powershell.exe -NoProfile -STA -File for the Windows Runtime projection.'
}
[void][Reflection.Assembly]::LoadWithPartialName('System.Runtime.WindowsRuntime')
$captureType = [Windows.Media.Capture.MediaCapture, Windows, ContentType=WindowsRuntime]
$settingsType = [Windows.Media.Capture.MediaCaptureInitializationSettings, Windows, ContentType=WindowsRuntime]
$deviceType = [Windows.Devices.Enumeration.DeviceInformation, Windows, ContentType=WindowsRuntime]
$deviceClassType = [Windows.Devices.Enumeration.DeviceClass, Windows, ContentType=WindowsRuntime]
$deviceCollectionType = [Windows.Devices.Enumeration.DeviceInformationCollection, Windows, ContentType=WindowsRuntime]
$readerType = [Windows.Media.Capture.Frames.MediaFrameReader, Windows, ContentType=WindowsRuntime]
$startType = [Windows.Media.Capture.Frames.MediaFrameReaderStartStatus, Windows, ContentType=WindowsRuntime]
$bufferType = [Windows.Storage.Streams.Buffer, Windows, ContentType=WindowsRuntime]
$videoFrameType = [Windows.Media.VideoFrame, Windows, ContentType=WindowsRuntime]

function Wait-Action($Operation) {
    $method = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and -not $_.IsGenericMethod -and
        $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.FullName -eq 'Windows.Foundation.IAsyncAction'
    } | Select-Object -First 1
    $task = $method.Invoke($null, @($Operation))
    if (-not $task.Wait(15000)) { throw 'WinRT action timed out after 15 seconds.' }
    [void]$task.GetAwaiter().GetResult()
}

function Wait-Operation($Operation, [Type]$ResultType) {
    $method = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and
        $_.GetGenericArguments().Count -eq 1 -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.IsGenericType -and
        $_.GetParameters()[0].ParameterType.GetGenericTypeDefinition().Name -eq 'IAsyncOperation`1'
    } | Select-Object -First 1
    $task = $method.MakeGenericMethod($ResultType).Invoke($null, @($Operation))
    if (-not $task.Wait(15000)) { throw 'WinRT operation timed out after 15 seconds.' }
    return $task.GetAwaiter().GetResult()
}

$devices = Wait-Operation ($deviceType::FindAllAsync($deviceClassType::VideoCapture)) $deviceCollectionType
$name = if ($Facing -eq 'front') { 'Surface Camera Front' } else { 'Surface Camera Rear' }
$device = $devices | Where-Object Name -eq $name | Select-Object -First 1
if (-not $device) { throw "Camera not found: $name" }
$capture = $captureType::new()
$reader = $null
$previewStarted = $false
$nonNull = 0
$nullFrames = 0
$pixelChanges = 0
$lastChecksum = $null
$timestamps = New-Object 'System.Collections.Generic.HashSet[string]'
$watch = [Diagnostics.Stopwatch]::new()
try {
    $settings = $settingsType::new()
    $settings.VideoDeviceId = $device.Id
    $settings.StreamingCaptureMode = [Windows.Media.Capture.StreamingCaptureMode, Windows, ContentType=WindowsRuntime]::Video
    if ($CpuMemory) {
        $settings.MemoryPreference = [Windows.Media.Capture.MediaCaptureMemoryPreference, Windows, ContentType=WindowsRuntime]::Cpu
    }
    Wait-Action ($capture.InitializeAsync($settings))
    Write-Output "INITIALIZED facing=$Facing mode=$Mode cpu=$CpuMemory copy=$CopyFrames"
    if ($Mode -eq 'reader') {
        $sources = @($capture.FrameSources | ForEach-Object { $_.Value })
        $source = $sources | Where-Object {
            $_.Info.SourceKind.ToString() -eq 'Color'
        } | Select-Object -First 1
        Write-Output "SOURCE id=$($source.Info.Id) stream=$($source.Info.MediaStreamType)"
        $format = $source.SupportedFormats | Where-Object {
            $_.VideoFormat.Width -eq 640 -and $_.VideoFormat.Height -eq 480 -and $_.Subtype -eq 'NV12'
        } | Select-Object -First 1
        if ($format) { Wait-Action ($source.SetFormatAsync($format)) }
        $operation = if ($CopyFrames) { $capture.CreateFrameReaderAsync($source, 'NV12') } else { $capture.CreateFrameReaderAsync($source) }
        $reader = Wait-Operation $operation $readerType
        $reader.AcquisitionMode = [Windows.Media.Capture.Frames.MediaFrameReaderAcquisitionMode, Windows, ContentType=WindowsRuntime]::Realtime
        $status = Wait-Operation ($reader.StartAsync()) $startType
        Write-Output "START status=$status"
        if ($status.ToString() -ne 'Success') { throw "Reader did not start: $status" }
    } else {
        Wait-Action ($capture.StartPreviewAsync())
        $previewStarted = $true
        Write-Output 'PREVIEW_STARTED'
    }
    $watch.Start()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        $frame = $null
        $bitmap = $null
        try {
            if ($Mode -eq 'reader') {
                $frame = $reader.TryAcquireLatestFrame()
                if ($frame) {
                    [void]$timestamps.Add([string]$frame.SystemRelativeTime)
                    $bitmap = $frame.VideoMediaFrame.SoftwareBitmap
                }
            } else {
                $frame = Wait-Operation ($capture.GetPreviewFrameAsync()) $videoFrameType
                if ($frame) {
                    [void]$timestamps.Add([string]$frame.RelativeTime)
                    $bitmap = $frame.SoftwareBitmap
                }
            }
            if ($frame) {
                $nonNull++
                if ($bitmap) {
                    $bytes = $bufferType::new([uint32]($bitmap.PixelWidth * $bitmap.PixelHeight * 4))
                    $bitmap.CopyToBuffer($bytes)
                    $pixels = [System.Runtime.InteropServices.WindowsRuntime.WindowsRuntimeBufferExtensions]::ToArray($bytes)
                    [long]$checksum = 0
                    for ($i = 0; $i -lt $pixels.Length; $i += 4096) { $checksum += $pixels[$i] }
                    if ($null -ne $lastChecksum -and $checksum -ne $lastChecksum) { $pixelChanges++ }
                    $lastChecksum = $checksum
                }
            } else { $nullFrames++ }
        } finally {
            if ($bitmap) { $bitmap.Dispose() }
            if ($frame) { $frame.Dispose() }
        }
        Start-Sleep -Milliseconds 35
    }
    Write-Output "RESULT facing=$Facing elapsed=$($watch.Elapsed.TotalSeconds.ToString('F2')) nonnull=$nonNull null=$nullFrames unique_times=$($timestamps.Count) pixel_changes=$pixelChanges"
} catch {
    $errorException = $_.Exception
    while ($errorException.InnerException) { $errorException = $errorException.InnerException }
    Write-Output ("CAPTURE_ERROR type={0} hr=0x{1:X8} message={2}" -f $errorException.GetType().FullName, $errorException.HResult, $errorException.Message)
    throw
} finally {
    if ($reader) {
        try { Wait-Action ($reader.StopAsync()) } catch { Write-Warning $_ }
        $reader.Dispose()
    }
    if ($previewStarted) {
        try { Wait-Action ($capture.StopPreviewAsync()) } catch { Write-Warning $_ }
    }
    $capture.Dispose()
    Write-Output 'CAMERA_RELEASED'
}
