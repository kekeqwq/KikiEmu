param(
  [string]$Tag,
  [string]$BundleDir = (Join-Path $PSScriptRoot '..\bundles\network-audio-20260926'),
  [string]$QemuPath = (Join-Path $PSScriptRoot 'qemu-src\build\qemu-system-aarch64.exe'),
  [string]$Msys2Bin = 'C:\msys64\clangarm64\bin',
  [string]$KernelImage = 'kernel-linux-7.3-rc4-4k-dmabuf',
  [string]$SystemImage = 'system-kikiaosp-network-audio-c2aidl.img',
  [string]$VendorImage = 'vendor-kikiaosp-network-audio-cap37.img',
  [ValidateSet('Software', 'Virgl')][string]$GpuMode = 'Software',
  [ValidateSet('gtk', 'sdl')][string]$DisplayBackend = 'gtk',
  [switch]$GpuBlob,
  [ValidateRange(1, 16)][int]$VcpuCount = 4,
  [ValidateRange(0, 8)][int]$KernelLogLevel = 8,
  [switch]$NativeResolution = $true,
  [ValidateRange(864, 3840)][int]$PortraitWidthPixels = 864,
  [ValidateRange(864, 3840)][int]$PortraitHeightPixels = 1728,
  [switch]$AudioStubOutput = $true,
  [switch]$SpeakerOutput = $true,
  [switch]$VirtioKeyboard = $true,
  [switch]$MouseTouchFallback,
  [switch]$InputTrace,
  [switch]$ResolutionTrace,
  [switch]$TraceVirglFences,
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
$shortDisplaySide = [Math]::Min($PortraitWidthPixels, $PortraitHeightPixels)
$longDisplaySide = [Math]::Max($PortraitWidthPixels, $PortraitHeightPixels)
if ($shortDisplaySide -lt 864 -or $longDisplaySide -lt 1728) {
  throw "The minimum usable guest display is 864x1728 (either orientation); refusing $($PortraitWidthPixels)x$($PortraitHeightPixels)."
}
if ([string]::IsNullOrWhiteSpace($Tag)) {
  $Tag = "network-audio-$((Get-Date).ToString('yyyyMMdd-HHmmss'))"
}
$images = (Resolve-Path -LiteralPath $BundleDir).Path
$qemu = (Resolve-Path -LiteralPath $QemuPath).Path
$kernel = if ([IO.Path]::IsPathRooted($KernelImage)) {
  $KernelImage
} else {
  Join-Path $images $KernelImage
}
if (-not (Test-Path -LiteralPath $kernel)) { throw "Missing kernel image $kernel" }
$serial = Join-Path $images "qemu-kikiaosp-$Tag.log"
$logcat = Join-Path $images "qemu-kikiaosp-$Tag.logcat"
$qemuTrace = Join-Path $images "qemu-kikiaosp-$Tag.qemu-trace.log"
foreach ($path in @($serial, $logcat)) {
  if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite $path" }
}
if ($TraceVirglFences -and (Test-Path -LiteralPath $qemuTrace)) {
  throw "Refusing to overwrite $qemuTrace"
}
if (-not $DryRun -and (Get-NetTCPConnection -State Listen -LocalPort 4447,5555 -ErrorAction SilentlyContinue)) {
  throw 'QEMU test ports 4447 or 5555 are already in use'
}

$eglDriver = if ($GpuMode -eq 'Virgl') { 'mesa' } else { 'angle' }
$append = "earlycon=pl011,0x09000000 console=ttyAMA0 loglevel=$KernelLogLevel printk.devkmsg=on audit=0 androidboot.hardware=ranchu androidboot.hardwareegl=$eglDriver androidboot.hardware.egl=$eglDriver androidboot.hardware.gralloc=minigbm androidboot.hardware.hwcomposer=ranchu androidboot.hardware.vulkan=pastel androidboot.hardware.hwcomposer.mode=client androidboot.hardware.hwcomposer.display_finder_mode=drm androidboot.hardware.guest_hwui_renderer=gles androidboot.debug.renderengine.backend=skiaglthreaded androidboot.selinux=permissive enforcing=0 androidboot.force_normal_boot=1 androidboot.verifiedbootstate=orange androidboot.init_fatal_reboot_target=none androidboot.adb.secure=0 binder.devices=binder,hwbinder,vndbinder"
if ($SpeakerOutput) {
  $AudioStubOutput = $false
}
if ($AudioStubOutput) {
  $append += ' androidboot.audio.tinyalsa.ignore_output=true'
}
$arguments = [System.Collections.Generic.List[string]]::new()
foreach ($item in @('-M','virt','-accel','whpx','-cpu','host','-m','4096','-smp',[string]$VcpuCount,'-kernel',$kernel,'-initrd',(Join-Path $images 'kiki-kernel-ramdisk-rc4-clean-odm-adb.img'),'-append',$append)) {
  $arguments.Add($item)
}
$partitions = @(
  @('system',$SystemImage,$true),
  @('vendor',$VendorImage,$true),
  @('product','kiki-empty-product.img',$true),
  @('system_ext','kiki-empty-system_ext.img',$true),
  @('userdata','userdata-kikiaosp17-f2fs.img',$false),
  @('misc','misc.img',$false),
  @('odm','odm-kikiaosp17-empty.img',$true)
)
foreach ($partition in $partitions) {
  $path = if ([IO.Path]::IsPathRooted($partition[1])) {
    $partition[1]
  } else {
    Join-Path $images $partition[1]
  }
  if (-not (Test-Path -LiteralPath $path)) { throw "Missing partition image $path" }
  $drive = "if=none,file=$path,format=raw,id=$($partition[0])"
  if ($partition[2]) { $drive += ',readonly=on' }
  $arguments.Add('-drive')
  $arguments.Add($drive)
  $arguments.Add('-device')
  $arguments.Add("virtio-blk-pci,drive=$($partition[0])")
}
$logcatQemuPath = $logcat.Replace('\','/')
$gpuResolution = if ($NativeResolution) { "xres=$PortraitWidthPixels,yres=$PortraitHeightPixels" } else { 'xres=1080,yres=2400' }
$glOption = if ($GpuMode -eq 'Virgl') { 'on' } else { 'off' }
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
  '-serial',"file:$serial",'-snapshot'
)) {
  $arguments.Add($item)
}
if ($TraceVirglFences) {
  $traceEventsPath = (Join-Path $PSScriptRoot 'qemu-virgl-fence-events.txt').Replace('\','/')
  $qemuTracePath = $qemuTrace.Replace('\','/')
  $arguments.Add('-msg')
  $arguments.Add('timestamp=on')
  $arguments.Add('-trace')
  $arguments.Add("events=$traceEventsPath")
  $arguments.Add('-D')
  $arguments.Add($qemuTracePath)
}
if ($SpeakerOutput) {
  $arguments.Add('-audiodev')
  $arguments.Add('dsound,id=kiki_audio')
  $arguments.Add('-device')
  $arguments.Add('virtio-sound-pci,audiodev=kiki_audio,streams=1')
}
if ($VirtioKeyboard) {
  $arguments.Add('-device')
  $arguments.Add('virtio-keyboard-pci')
}

if ($DryRun) {
  "QEMU_EXE=$qemu GPU_MODE=$GpuMode GPU_BLOB=$GpuBlob VCPU_COUNT=$VcpuCount KERNEL_LOGLEVEL=$KernelLogLevel DRY_RUN=True"
  for ($i = 0; $i -lt $arguments.Count; $i++) {
    "QEMU_ARG[$i]=$($arguments[$i])"
  }
  return
}

$start = [System.Diagnostics.ProcessStartInfo]::new($qemu)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
if (Test-Path -LiteralPath $Msys2Bin -PathType Container) {
  # The native CLANGARM64 build resolves GTK/GLib runtime DLLs from MSYS2.
  $start.Environment['PATH'] = "$Msys2Bin;$($start.Environment['PATH'])"
}
if ($MouseTouchFallback) {
  $start.Environment['KIKI_GTK_MOUSE_AS_TOUCH'] = '1'
}
if ($InputTrace) {
  $inputTracePath = Join-Path $images "qemu-kikiaosp-$Tag.gtk-input.log"
  if (Test-Path -LiteralPath $inputTracePath) { throw "Refusing to overwrite $inputTracePath" }
  $start.Environment['KIKI_GTK_INPUT_TRACE'] = $inputTracePath
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
$displayHelperPath = Join-Path $PSScriptRoot 'configure_kikiaosp_display.ps1'
$displayLog = Join-Path $images "qemu-kikiaosp-$Tag.display.log"
if (Test-Path -LiteralPath $displayLog) { throw "Refusing to overwrite $displayLog" }
$pwshPath = (Get-Command pwsh -ErrorAction Stop).Source
$helperArguments = "-NoProfile -File `"$displayHelperPath`" -Serial 127.0.0.1:5555 -LogPath `"$displayLog`""
$displayProcess = Start-Process -FilePath $pwshPath -ArgumentList $helperArguments `
  -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
$keyboardDevice = if ($VirtioKeyboard) { 'virtio-keyboard-pci' } else { 'none' }
"QEMU_PID=$($process.Id) DISPLAY_HELPER_PID=$($displayProcess.Id) DISPLAY_BACKEND=$DisplayBackend DISPLAY_LOG=$displayLog SERIAL=$serial LOGCAT=$logcat QEMU_TRACE=$($TraceVirglFences ? $qemuTrace : 'off') GPU_MODE=$GpuMode GPU_BLOB=$GpuBlob VCPU_COUNT=$VcpuCount KERNEL_LOGLEVEL=$KernelLogLevel TOUCH_DEVICE=virtio-multitouch-pci KEYBOARD_DEVICE=$keyboardDevice SPEAKER_OUTPUT=$SpeakerOutput AUDIO_STUB_OUTPUT=$AudioStubOutput MOUSE_TOUCH_FALLBACK=$MouseTouchFallback INPUT_TRACE=$InputTrace RESOLUTION_TRACE=$ResolutionTrace NATIVE_RESOLUTION=$NativeResolution PORTRAIT_PIXELS=${PortraitWidthPixels}x${PortraitHeightPixels}"
