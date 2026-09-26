param(
  [string]$Tag,
  [string]$KernelImage = 'kernel-linux-7.3-rc4-4k-netfilter-20260925',
  [string]$SystemImage = 'system-kikiaosp-launcher3-settings-stable-20260926.img',
  [string]$VendorImage = 'vendor-kikiaosp-ui-scanout-pixel-count-20260926.img',
  [switch]$AudioStubOutput = $true,
  [switch]$VirtioKeyboard = $true,
  [switch]$MouseTouchFallback,
  [switch]$InputTrace
)

# Verified Launcher3 + Settings baseline with guest multitouch and keyboard devices.
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Tag)) {
  $Tag = "launcher3-settings-$((Get-Date).ToString('yyyyMMdd-HHmmss'))"
}
$images = (Resolve-Path (Join-Path $PSScriptRoot '..\aosp\windows-arm64-test')).Path
$qemu = (Resolve-Path (Join-Path $PSScriptRoot 'qemu-src\build\qemu-system-aarch64.exe')).Path
$kernel = Join-Path $images $KernelImage
if (-not (Test-Path -LiteralPath $kernel)) { throw "Missing kernel image $kernel" }
$serial = Join-Path $images "qemu-kikiaosp-$Tag.log"
$logcat = Join-Path $images "qemu-kikiaosp-$Tag.logcat"
foreach ($path in @($serial, $logcat)) {
  if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite $path" }
}
if (Get-NetTCPConnection -State Listen -LocalPort 4447,5555 -ErrorAction SilentlyContinue) {
  throw 'QEMU test ports 4447 or 5555 are already in use'
}

$append = 'earlycon=pl011,0x09000000 console=ttyAMA0 loglevel=8 printk.devkmsg=on audit=0 androidboot.hardware=ranchu androidboot.hardwareegl=angle androidboot.hardware.egl=angle androidboot.hardware.gralloc=minigbm androidboot.hardware.hwcomposer=ranchu androidboot.hardware.vulkan=pastel androidboot.hardware.hwcomposer.mode=client androidboot.hardware.hwcomposer.display_finder_mode=drm androidboot.hardware.guest_hwui_renderer=gles androidboot.debug.renderengine.backend=skiaglthreaded androidboot.selinux=permissive enforcing=0 androidboot.force_normal_boot=1 androidboot.verifiedbootstate=orange androidboot.init_fatal_reboot_target=none androidboot.adb.secure=0 binder.devices=binder,hwbinder,vndbinder'
if ($AudioStubOutput) {
  $append += ' androidboot.audio.tinyalsa.ignore_output=true'
}
$arguments = [System.Collections.Generic.List[string]]::new()
foreach ($item in @('-M','virt','-accel','whpx','-cpu','host','-m','4096','-smp','4','-kernel',$kernel,'-initrd',(Join-Path $images 'kiki-kernel-ramdisk-rc4-clean-odm-adb.img'),'-append',$append)) {
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
  $path = Join-Path $images $partition[1]
  if (-not (Test-Path -LiteralPath $path)) { throw "Missing partition image $path" }
  $drive = "if=none,file=$path,format=raw,id=$($partition[0])"
  if ($partition[2]) { $drive += ',readonly=on' }
  $arguments.Add('-drive')
  $arguments.Add($drive)
  $arguments.Add('-device')
  $arguments.Add("virtio-blk-pci,drive=$($partition[0])")
}
$logcatQemuPath = $logcat.Replace('\','/')
foreach ($item in @(
  '-device','virtio-gpu-pci,hostmem=256M,xres=1080,yres=2400',
  '-device','virtio-multitouch-pci',
  '-netdev','user,id=net0,hostfwd=tcp:127.0.0.1:5555-:5555',
  '-device','virtio-net-pci,netdev=net0',
  '-device','virtio-serial-pci,id=kiki-serial',
  '-chardev',"file,id=kiki-logcat,path=$logcatQemuPath",
  '-device','virtconsole,chardev=kiki-logcat,bus=kiki-serial.0,name=org.kikiaosp.logcat',
  '-display','gtk,gl=off',
  '-monitor','tcp:127.0.0.1:4447,server,nowait',
  '-serial',"file:$serial",'-snapshot'
)) {
  $arguments.Add($item)
}
if ($VirtioKeyboard) {
  $arguments.Add('-device')
  $arguments.Add('virtio-keyboard-pci')
}

$start = [System.Diagnostics.ProcessStartInfo]::new($qemu)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
if ($MouseTouchFallback) {
  $start.Environment['KIKI_GTK_MOUSE_AS_TOUCH'] = '1'
}
if ($InputTrace) {
  $inputTracePath = Join-Path $images "qemu-kikiaosp-$Tag.gtk-input.log"
  if (Test-Path -LiteralPath $inputTracePath) { throw "Refusing to overwrite $inputTracePath" }
  $start.Environment['KIKI_GTK_INPUT_TRACE'] = $inputTracePath
}
foreach ($item in $arguments) { $start.ArgumentList.Add($item) }
$process = [System.Diagnostics.Process]::Start($start)
Start-Sleep -Seconds 2
if ($process.HasExited) {
  throw "QEMU exited with code $($process.ExitCode); inspect the serial log at $serial"
}
$keyboardDevice = if ($VirtioKeyboard) { 'virtio-keyboard-pci' } else { 'none' }
"QEMU_PID=$($process.Id) SERIAL=$serial LOGCAT=$logcat TOUCH_DEVICE=virtio-multitouch-pci KEYBOARD_DEVICE=$keyboardDevice MOUSE_TOUCH_FALLBACK=$MouseTouchFallback INPUT_TRACE=$InputTrace"
