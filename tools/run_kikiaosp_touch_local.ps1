param(
  [string]$Tag,
  [string]$BundleDir = (Join-Path $PSScriptRoot '..\bundles\network-audio-20260926'),
  [string]$QemuPath = (Join-Path $PSScriptRoot 'qemu-src\build\qemu-system-aarch64.exe'),
  [string]$Msys2Bin = 'C:\msys64\clangarm64\bin',
  [string]$KernelImage = 'kernel-linux-7.3-rc4-4k-dmabuf',
  [string]$SystemImage = 'system-kikiaosp-network-audio-c2aidl.img',
  [string]$VendorImage = 'vendor-kikiaosp-network-audio-cap37.img',
  [string]$ProductImage = 'kiki-empty-product.img',
  [string]$SystemExtImage = 'kiki-empty-system_ext.img',
  [string]$UserdataImage = 'userdata-kikiaosp17-f2fs.img',
  [string]$MiscImage = 'misc.img',
  [switch]$PersistentDisks,
  [ValidateSet('Software', 'Virgl')][string]$GpuMode = 'Software',
  [ValidateSet('Client', 'Guest')][string]$HwcMode = 'Client',
  [ValidateSet('skiaglthreaded', 'skiagl')][string]$RenderEngineBackend = 'skiaglthreaded',
  [ValidateSet('gtk', 'sdl')][string]$DisplayBackend = 'sdl',
  [ValidateSet('dsound', 'sdl')][string]$AudioBackend = 'sdl',
  [ValidateRange(11610, 100000)][int]$SdlAudioBufferLengthUsec = 20000,
  [ValidateRange(2, 8)][int]$SdlAudioBufferCount = 4,
  [ValidateSet(0, 1)][int]$SdlSwapInterval = 0,
  [ValidateSet('default', '0', '1')][string]$GtkSwapInterval = 'default',
  [ValidateRange(30, 120)][int]$GuestRefreshRateHz = 120,
  [switch]$AngleEgl,
  [switch]$GpuBlob,
  [ValidateRange(1, 16)][int]$VcpuCount = 4,
  [ValidateRange(0, 8)][int]$KernelLogLevel = 8,
  [switch]$NativeResolution = $true,
  [ValidateRange(864, 3840)][int]$PortraitWidthPixels = 1003,
  [ValidateRange(864, 3840)][int]$PortraitHeightPixels = 1556,
  [switch]$AudioStubOutput = $true,
  [switch]$SpeakerOutput = $true,
  [switch]$VirtioKeyboard = $true,
  [switch]$MouseTouchFallback,
  [switch]$InputTrace,
  [switch]$ResolutionTrace,
  [switch]$TraceVirglFences,
  [switch]$SurfaceCameras,
  [string]$SurfaceCameraBridgePath = (Join-Path $PSScriptRoot 'build\surface-camera-bridge.exe'),
  [ValidateRange(1024, 65535)][int]$SurfaceCameraPort = 4455,
  [switch]$SdlKeyboardTrace,
  [string]$TraceEventsPath,
  [switch]$GlobalFpsProfile,
  [switch]$DryRun
)

# Verified Ethernet, speaker, and Codec2 UI-sound baseline.
$ErrorActionPreference = 'Stop'
if ($GpuMode -eq 'Virgl' -and -not $DryRun -and
    $VendorImage -eq 'vendor-kikiaosp-network-audio-cap37.img') {
  throw 'VirGL requires a vendor image containing Mesa VirGL. Pass -VendorImage with the new verified image.'
}
if ($GpuBlob -and $GpuMode -ne 'Virgl') {
  throw '-GpuBlob requires -GpuMode Virgl'
}
if ($TraceVirglFences -and $GpuMode -ne 'Virgl') {
  throw '-TraceVirglFences requires -GpuMode Virgl'
}
if ($SdlKeyboardTrace -and $DisplayBackend -ne 'sdl') {
  throw '-SdlKeyboardTrace requires -DisplayBackend sdl'
}
if ($AngleEgl -and ($GpuMode -ne 'Virgl' -or $DisplayBackend -ne 'gtk')) {
  throw '-AngleEgl requires -GpuMode Virgl and -DisplayBackend gtk'
}
if ($GtkSwapInterval -ne 'default' -and $DisplayBackend -ne 'gtk') {
  throw '-GtkSwapInterval is only available with -DisplayBackend gtk'
}
if ($HwcMode -eq 'Guest' -and $GpuMode -ne 'Virgl') {
  throw '-HwcMode Guest requires -GpuMode Virgl'
}
if ($PersistentDisks -and
    ($UserdataImage -eq 'userdata-kikiaosp17-f2fs.img' -or $MiscImage -eq 'misc.img' -or
     -not $UserdataImage.EndsWith('.qcow2', [StringComparison]::OrdinalIgnoreCase) -or
     -not $MiscImage.EndsWith('.qcow2', [StringComparison]::OrdinalIgnoreCase))) {
  throw '-PersistentDisks requires explicit separate qcow2 overlay images for userdata and misc; baseline raw disks must remain untouched.'
}
if (-not $PersistentDisks -and
    ($UserdataImage -ne 'userdata-kikiaosp17-f2fs.img' -or $MiscImage -ne 'misc.img')) {
  throw 'Custom writable disks require -PersistentDisks, so the QEMU snapshot mode cannot silently discard their writes.'
}
$shortDisplaySide = [Math]::Min($PortraitWidthPixels, $PortraitHeightPixels)
$longDisplaySide = [Math]::Max($PortraitWidthPixels, $PortraitHeightPixels)
if ($shortDisplaySide -lt 864 -or ($PortraitWidthPixels * $PortraitHeightPixels) -lt (864 * 1728)) {
  throw "The minimum usable guest display is 864 pixels on the short side and 1,492,992 total pixels; refusing $($PortraitWidthPixels)x$($PortraitHeightPixels)."
}
if ([string]::IsNullOrWhiteSpace($Tag)) {
  $Tag = "network-audio-$((Get-Date).ToString('yyyyMMdd-HHmmss'))"
}
$images = (Resolve-Path -LiteralPath $BundleDir).Path
$qemu = (Resolve-Path -LiteralPath $QemuPath).Path
$qemuSha256 = (Get-FileHash -LiteralPath $qemu -Algorithm SHA256).Hash.ToLowerInvariant()
if ($DisplayBackend -eq 'sdl') {
  # A plain/old QEMU silently ignores our environment variables, reintroducing
  # giant DPI-scaled windows, mouse grab and the wrong guest refresh rate.
  # Check compiled feature markers before starting WHPX or opening a window.
  $requiredSdlFeatures = @('KIKI_SDL_DISABLE_GRAB', 'KIKI_SDL_DISABLE_IME', 'KIKI_SDL_GUEST_REFRESH_RATE_HZ')
  if ($NativeResolution) {
    $requiredSdlFeatures += @('KIKI_SDL_NATIVE_PIXELS', 'KIKI_SDL_START_WIDTH', 'KIKI_SDL_START_HEIGHT')
  }
  $foundSdlFeatures = [bool[]]::new($requiredSdlFeatures.Count)
  $qemuFeatureStream = [IO.File]::OpenRead($qemu)
  try {
    $qemuFeatureBuffer = [byte[]]::new(65536)
    $featureTail = ''
    $remainingFeatures = $requiredSdlFeatures.Count
    while ($remainingFeatures -gt 0 -and ($readCount = $qemuFeatureStream.Read($qemuFeatureBuffer, 0, $qemuFeatureBuffer.Length)) -gt 0) {
      $featureChunk = $featureTail + [Text.Encoding]::ASCII.GetString($qemuFeatureBuffer, 0, $readCount)
      for ($featureIndex = 0; $featureIndex -lt $requiredSdlFeatures.Count; $featureIndex++) {
        if (-not $foundSdlFeatures[$featureIndex] -and $featureChunk.Contains($requiredSdlFeatures[$featureIndex])) {
          $foundSdlFeatures[$featureIndex] = $true
          $remainingFeatures--
        }
      }
      $featureTail = $featureChunk.Substring([Math]::Max(0, $featureChunk.Length - 64))
    }
    if ($remainingFeatures -gt 0) {
      $missingFeatures = for ($featureIndex = 0; $featureIndex -lt $requiredSdlFeatures.Count; $featureIndex++) {
        if (-not $foundSdlFeatures[$featureIndex]) { $requiredSdlFeatures[$featureIndex] }
      }
      throw "QEMU lacks the tested SDL Surface features: $($missingFeatures -join ', '). Refusing an incompatible executable: $qemu (SHA256=$qemuSha256)."
    }
  } finally {
    $qemuFeatureStream.Dispose()
  }
}
$cameraBridge = if ([IO.Path]::IsPathRooted($SurfaceCameraBridgePath)) {
  $SurfaceCameraBridgePath
} else {
  Join-Path $PSScriptRoot $SurfaceCameraBridgePath
}
$kernel = if ([IO.Path]::IsPathRooted($KernelImage)) {
  $KernelImage
} else {
  Join-Path $images $KernelImage
}
if (-not (Test-Path -LiteralPath $kernel)) { throw "Missing kernel image $kernel" }
$serial = Join-Path $images "qemu-kikiaosp-$Tag.log"
$logcat = Join-Path $images "qemu-kikiaosp-$Tag.logcat"
$cameraBridgeLog = Join-Path $images "qemu-kikiaosp-$Tag.camera-bridge.log"
$qemuTrace = Join-Path $images "qemu-kikiaosp-$Tag.qemu-trace.log"
$globalFpsLog = Join-Path $images "qemu-kikiaosp-$Tag.global-fps.log"
foreach ($path in @($serial, $logcat)) {
  if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite $path" }
}
if ($SurfaceCameras -and (Test-Path -LiteralPath $cameraBridgeLog)) {
  throw "Refusing to overwrite $cameraBridgeLog"
}
if (($TraceVirglFences -or $SdlKeyboardTrace) -and (Test-Path -LiteralPath $qemuTrace)) {
  throw "Refusing to overwrite $qemuTrace"
}
if ($GlobalFpsProfile -and (Test-Path -LiteralPath $globalFpsLog)) {
  throw "Refusing to overwrite $globalFpsLog"
}
if (-not $DryRun -and (Get-NetTCPConnection -State Listen -LocalPort 4447,5555 -ErrorAction SilentlyContinue)) {
  throw 'QEMU test ports 4447 or 5555 are already in use'
}
if ($SurfaceCameras -and -not (Test-Path -LiteralPath $cameraBridge -PathType Leaf)) {
  throw "Surface camera bridge executable is missing: $cameraBridge. Build it with tools/build_surface_camera_bridge.ps1."
}
if ($SurfaceCameras -and -not $DryRun -and
    (Get-NetTCPConnection -State Listen -LocalPort $SurfaceCameraPort -ErrorAction SilentlyContinue)) {
  throw "Surface camera bridge port $SurfaceCameraPort is already in use"
}

$eglDriver = if ($GpuMode -eq 'Virgl') { 'mesa' } else { 'angle' }
$append = "earlycon=pl011,0x09000000 console=ttyAMA0 loglevel=$KernelLogLevel printk.devkmsg=on audit=0 androidboot.hardware=ranchu androidboot.hardwareegl=$eglDriver androidboot.hardware.egl=$eglDriver androidboot.hardware.gralloc=minigbm androidboot.hardware.hwcomposer=ranchu androidboot.hardware.vulkan=pastel androidboot.hardware.hwcomposer.mode=$($HwcMode.ToLowerInvariant()) androidboot.hardware.hwcomposer.display_finder_mode=drm androidboot.hardware.guest_hwui_renderer=gles androidboot.debug.renderengine.backend=$RenderEngineBackend androidboot.selinux=permissive enforcing=0 androidboot.force_normal_boot=1 androidboot.verifiedbootstate=orange androidboot.init_fatal_reboot_target=none androidboot.adb.secure=0 binder.devices=binder,hwbinder,vndbinder"
if ($SpeakerOutput) {
  $AudioStubOutput = $false
}
if ($AudioStubOutput) {
  $append += ' androidboot.audio.tinyalsa.ignore_output=true'
}
$arguments = [System.Collections.Generic.List[string]]::new()
foreach ($item in @('-M','virt','-accel','whpx','-cpu','host','-m','4096','-smp',[string]$VcpuCount,'-parallel','none','-kernel',$kernel,'-initrd',(Join-Path $images 'kiki-kernel-ramdisk-rc4-clean-odm-adb.img'),'-append',$append)) {
  $arguments.Add($item)
}
$partitions = @(
  @('system',$SystemImage,$true),
  @('vendor',$VendorImage,$true),
  @('product',$ProductImage,$true),
  @('system_ext',$SystemExtImage,$true),
  @('userdata',$UserdataImage,$false),
  @('misc',$MiscImage,$false),
  @('odm','odm-kikiaosp17-empty.img',$true)
)
foreach ($partition in $partitions) {
  $path = if ([IO.Path]::IsPathRooted($partition[1])) {
    $partition[1]
  } else {
    Join-Path $images $partition[1]
  }
  if (-not (Test-Path -LiteralPath $path)) { throw "Missing partition image $path" }
  $format = if ($PersistentDisks -and $partition[0] -in @('userdata', 'misc')) { 'qcow2' } else { 'raw' }
  $drive = "if=none,file=$path,format=$format,id=$($partition[0])"
  if ($partition[2]) { $drive += ',readonly=on' }
  $arguments.Add('-drive')
  $arguments.Add($drive)
  $arguments.Add('-device')
  $arguments.Add("virtio-blk-pci,drive=$($partition[0])")
}
$logcatQemuPath = $logcat.Replace('\','/')
$gpuResolution = if ($NativeResolution) { "xres=$PortraitWidthPixels,yres=$PortraitHeightPixels" } else { 'xres=1080,yres=2400' }
$glOption = if ($AngleEgl) { 'es' } elseif ($GpuMode -eq 'Virgl') { 'on' } else { 'off' }
$gpuDevice = if ($GpuMode -eq 'Virgl') { 'virtio-gpu-gl-pci' } else { 'virtio-gpu-pci' }
$gpuBlobOption = if ($GpuBlob) { ',blob=on' } else { '' }
$gpuDeviceOptions = "$gpuDevice,hostmem=256M,$gpuResolution$gpuBlobOption"
$displayOptions = if ($DisplayBackend -eq 'gtk') {
  if ($NativeResolution) { "gtk,gl=$glOption,show-menubar=off" } else { "gtk,gl=$glOption" }
} else {
  "sdl,gl=$glOption"
}
foreach ($item in @(
  '-device',$gpuDeviceOptions,
  '-device','virtio-multitouch-pci',
  '-netdev','user,id=net0,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3,hostfwd=tcp:127.0.0.1:5555-:5555',
  '-device','virtio-net-pci,netdev=net0',
  '-device','virtio-serial-pci,id=kiki-serial',
  '-chardev',"file,id=kiki-logcat,path=$logcatQemuPath",
  '-device','virtconsole,chardev=kiki-logcat,bus=kiki-serial.0,name=org.kikiaosp.logcat',
  '-display',$displayOptions,
  '-monitor','tcp:127.0.0.1:4447,server,nowait',
  '-serial',"file:$serial"
)) {
  $arguments.Add($item)
}
if (-not $PersistentDisks) { $arguments.Add('-snapshot') }
if ($TraceVirglFences -or $SdlKeyboardTrace) {
  $traceOptions = [System.Collections.Generic.List[string]]::new()
  if ($TraceVirglFences) {
    $traceEventList = if ([string]::IsNullOrWhiteSpace($TraceEventsPath)) {
      Join-Path $PSScriptRoot 'qemu-virgl-fence-events.txt'
    } elseif ([IO.Path]::IsPathRooted($TraceEventsPath)) {
      $TraceEventsPath
    } else {
      Join-Path $PSScriptRoot $TraceEventsPath
    }
    if (-not (Test-Path -LiteralPath $traceEventList -PathType Leaf)) {
      throw "Trace event list does not exist: $traceEventList"
    }
    $traceEventList = (Resolve-Path -LiteralPath $traceEventList).Path.Replace('\','/')
    $traceOptions.Add("events=$traceEventList")
  }
  if ($SdlKeyboardTrace) {
    $traceOptions.Add('enable=sdl2_process_key')
  }
  $qemuTracePath = $qemuTrace.Replace('\','/')
  $arguments.Add('-msg')
  $arguments.Add('timestamp=on')
  $arguments.Add('-trace')
  $arguments.Add(($traceOptions -join ','))
  $arguments.Add('-D')
  $arguments.Add($qemuTracePath)
}
if ($SpeakerOutput) {
  $arguments.Add('-audiodev')
  $audioDevice = if ($AudioBackend -eq 'sdl') {
    "sdl,id=kiki_audio,out.buffer-length=$SdlAudioBufferLengthUsec,out.buffer-count=$SdlAudioBufferCount"
  } else {
    "$AudioBackend,id=kiki_audio"
  }
  $arguments.Add($audioDevice)
  $arguments.Add('-device')
  $arguments.Add('virtio-sound-pci,audiodev=kiki_audio,streams=1')
}
if ($VirtioKeyboard) {
  $arguments.Add('-device')
  $arguments.Add('virtio-keyboard-pci')
}
if ($SurfaceCameras) {
  $arguments.Add('-chardev')
  $arguments.Add("socket,id=kiki-camera,host=127.0.0.1,port=$SurfaceCameraPort,server=on,wait=off")
  $arguments.Add('-device')
  $arguments.Add('virtserialport,bus=kiki-serial.0,chardev=kiki-camera,name=org.kikiaosp.camera')
}

if ($DryRun) {
  "QEMU_EXE=$qemu GPU_MODE=$GpuMode HWC_MODE=$HwcMode RENDERENGINE_BACKEND=$RenderEngineBackend GPU_BLOB=$GpuBlob AUDIO_BACKEND=$AudioBackend GUEST_REFRESH_RATE_HZ=$GuestRefreshRateHz VCPU_COUNT=$VcpuCount KERNEL_LOGLEVEL=$KernelLogLevel SDL_KEY_TRACE=$SdlKeyboardTrace SDL_MOUSE_GRAB=$(if ($DisplayBackend -eq 'sdl') { 'disabled' } else { 'backend-default' }) GLOBAL_FPS_PROFILE=$GlobalFpsProfile SURFACE_CAMERAS=$SurfaceCameras SURFACE_CAMERA_PORT=$(if ($SurfaceCameras) { $SurfaceCameraPort } else { 'off' }) SURFACE_CAMERA_LOG=$(if ($SurfaceCameras) { $cameraBridgeLog } else { 'off' }) PERSISTENT_DISKS=$PersistentDisks DRY_RUN=True"
  for ($i = 0; $i -lt $arguments.Count; $i++) {
    "QEMU_ARG[$i]=$($arguments[$i])"
  }
  return
}

$start = [System.Diagnostics.ProcessStartInfo]::new($qemu)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.Environment['KIKI_GTK_TOUCH_FIRST'] = '1'
if ($DisplayBackend -eq 'gtk') {
  $start.Environment['KIKI_GTK_GUEST_REFRESH_RATE_HZ'] = [string]$GuestRefreshRateHz
  if ($GtkSwapInterval -ne 'default') {
    $start.Environment['KIKI_GTK_SWAP_INTERVAL'] = $GtkSwapInterval
  }
} elseif ($DisplayBackend -eq 'sdl') {
  $start.Environment['KIKI_SDL_GUEST_REFRESH_RATE_HZ'] = [string]$GuestRefreshRateHz
  $start.Environment['KIKI_SDL_SWAP_INTERVAL'] = [string]$SdlSwapInterval
  # The guest's absolute touchscreen does not require SDL mouse capture.
  $start.Environment['KIKI_SDL_DISABLE_GRAB'] = '1'
  # Let the Android guest IME receive physical key events instead of the host IME.
  $start.Environment['KIKI_SDL_DISABLE_IME'] = '1'
  $start.Environment['KIKI_SDL_RAW_KEYBOARD_TRACE'] = if ($SdlKeyboardTrace) { '1' } else { '0' }
  if ($NativeResolution) {
    $start.Environment['KIKI_SDL_NATIVE_PIXELS'] = '1'
    $start.Environment['KIKI_SDL_START_WIDTH'] = [string]$PortraitWidthPixels
    $start.Environment['KIKI_SDL_START_HEIGHT'] = [string]$PortraitHeightPixels
  }
}
if ($AngleEgl) {
  $start.Environment['KIKI_GTK_ANGLE_EGL'] = '1'
  $angleContextLog = Join-Path $images "qemu-kikiaosp-$Tag.angle-context.log"
  if (Test-Path -LiteralPath $angleContextLog) { throw "Refusing to overwrite $angleContextLog" }
  $start.Environment['KIKI_ANGLE_CTX_LOG'] = $angleContextLog
}
if (Test-Path -LiteralPath $Msys2Bin -PathType Container) {
  # The native CLANGARM64 build resolves GTK/GLib runtime DLLs from MSYS2.
  $start.Environment['PATH'] = "$Msys2Bin;$($start.Environment['PATH'])"
}
if ($MouseTouchFallback) {
  $start.Environment['KIKI_GTK_MOUSE_AS_TOUCH'] = '1'
}
if ($InputTrace) {
  $inputTraceSuffix = if ($DisplayBackend -eq 'sdl') { 'sdl-input' } else { 'gtk-input' }
  $inputTracePath = Join-Path $images "qemu-kikiaosp-$Tag.$inputTraceSuffix.log"
  if (Test-Path -LiteralPath $inputTracePath) { throw "Refusing to overwrite $inputTracePath" }
  if ($DisplayBackend -eq 'sdl') {
    $start.Environment['KIKI_SDL_INPUT_TRACE'] = $inputTracePath
  } else {
    $start.Environment['KIKI_GTK_INPUT_TRACE'] = $inputTracePath
  }
}
if ($GlobalFpsProfile) {
  $start.Environment['KIKI_GPU_PROFILE'] = '1'
  $start.Environment['KIKI_GPU_PROFILE_PATH'] = $globalFpsLog
  if ($DisplayBackend -eq 'sdl') {
    $start.Environment['KIKI_SDL_PROFILE_PATH'] = $globalFpsLog
  }
}
if ($ResolutionTrace) {
  $resolutionTracePath = Join-Path $images "qemu-kikiaosp-$Tag.gtk-resolution.log"
  if (Test-Path -LiteralPath $resolutionTracePath) { throw "Refusing to overwrite $resolutionTracePath" }
  $start.Environment['KIKI_GTK_RESOLUTION_TRACE'] = $resolutionTracePath
}
if ($NativeResolution) {
  $start.Environment['KIKI_VIRTIO_GPU_HOLD_LAST_SCANOUT'] = '1'
  if ($DisplayBackend -eq 'gtk') {
    $start.Environment['KIKI_GTK_NATIVE_PIXELS'] = '1'
    $start.Environment['KIKI_GTK_START_WIDTH'] = [string]$PortraitWidthPixels
    $start.Environment['KIKI_GTK_START_HEIGHT'] = [string]$PortraitHeightPixels
  }
}
foreach ($item in $arguments) { $start.ArgumentList.Add($item) }
$process = [System.Diagnostics.Process]::Start($start)
Start-Sleep -Seconds 2
if ($process.HasExited) {
  throw "QEMU exited with code $($process.ExitCode); inspect the serial log at $serial"
}
$cameraBridgeProcess = $null
if ($SurfaceCameras) {
  $parentPath = $env:PATH
  if (Test-Path -LiteralPath $Msys2Bin -PathType Container) {
    $env:PATH = "$Msys2Bin;$parentPath"
  }
  try {
    $cameraBridgeProcess = Start-Process -FilePath $cameraBridge `
      -ArgumentList @('--serve', '127.0.0.1', [string]$SurfaceCameraPort, $cameraBridgeLog) `
      -WorkingDirectory $PSScriptRoot -NoNewWindow -PassThru
  } finally {
    $env:PATH = $parentPath
  }
  Start-Sleep -Milliseconds 500
  if ($cameraBridgeProcess.HasExited) {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    throw "Surface camera bridge exited with code $($cameraBridgeProcess.ExitCode); see inherited terminal output"
  }
}
$displayHelperPath = Join-Path $PSScriptRoot 'configure_kikiaosp_display.ps1'
$displayLog = Join-Path $images "qemu-kikiaosp-$Tag.display.log"
if (Test-Path -LiteralPath $displayLog) { throw "Refusing to overwrite $displayLog" }
$pwshPath = (Get-Command pwsh -ErrorAction Stop).Source
$helperArguments = "-NoProfile -File `"$displayHelperPath`" -Serial 127.0.0.1:5555 -LogPath `"$displayLog`" -DisplayWidthPixels $PortraitWidthPixels -DisplayHeightPixels $PortraitHeightPixels -DisplayDensityDpi 288 -FontScale 1.5"
$displayProcess = Start-Process -FilePath $pwshPath -ArgumentList $helperArguments `
  -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
$keyboardDevice = if ($VirtioKeyboard) { 'virtio-keyboard-pci' } else { 'none' }
"QEMU_PID=$($process.Id) QEMU_EXE=$qemu QEMU_SHA256=$qemuSha256 CAMERA_BRIDGE_PID=$($cameraBridgeProcess ? $cameraBridgeProcess.Id : 'off') CAMERA_BRIDGE_LOG=$(if ($SurfaceCameras) { $cameraBridgeLog } else { 'off' }) DISPLAY_BACKEND=$DisplayBackend AUDIO_BACKEND=$AudioBackend GTK_SWAP_INTERVAL=$GtkSwapInterval ANGLE_EGL=$AngleEgl DISPLAY_LOG=$displayLog SERIAL=$serial LOGCAT=$logcat QEMU_TRACE=$($TraceVirglFences -or $SdlKeyboardTrace ? $qemuTrace : 'off') SDL_KEY_TRACE=$SdlKeyboardTrace SDL_MOUSE_GRAB=$(if ($DisplayBackend -eq 'sdl') { 'disabled' } else { 'backend-default' }) GLOBAL_FPS_LOG=$($GlobalFpsProfile ? $globalFpsLog : 'off') GPU_MODE=$GpuMode HWC_MODE=$HwcMode RENDERENGINE_BACKEND=$RenderEngineBackend GPU_BLOB=$GpuBlob GUEST_REFRESH_RATE_HZ=$GuestRefreshRateHz VCPU_COUNT=$VcpuCount KERNEL_LOGLEVEL=$KernelLogLevel PARALLEL_PORT=disabled TOUCH_DEVICE=virtio-multitouch-pci KEYBOARD_DEVICE=$keyboardDevice SPEAKER_OUTPUT=$SpeakerOutput AUDIO_STUB_OUTPUT=$AudioStubOutput MOUSE_TOUCH_FALLBACK=$MouseTouchFallback INPUT_TRACE=$InputTrace RESOLUTION_TRACE=$ResolutionTrace NATIVE_RESOLUTION=$NativeResolution PORTRAIT_PIXELS=${PortraitWidthPixels}x${PortraitHeightPixels}"
