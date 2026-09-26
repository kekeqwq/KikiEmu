# KikiEmu — Windows ARM 运行与打包主仓库

本仓库负责把 KikiAOSP 的镜像变成可在 Windows ARM 本机启动的运行包：保存原生 ARM64 QEMU 的补丁与构建配方，从开发机取得内核和 Android 镜像，校验配对、解包辅助磁盘，再启动和截图。早期 HCS/.NET 尝试已退出当前路线，历史代码仍可从 Git 历史找回。

| 仓库 | 负责什么 | 交付给本仓库的产物 |
| --- | --- | --- |
| [kikiaosp_kernel](https://github.com/kekeqwq/kikiaosp_kernel) | Linux 7.3-rc4、4 KiB 内核配置和 Nix 构建 | `result/boot/kernel` |
| [kikiaosp_test](https://github.com/kekeqwq/kikiaosp_test) | Android 17 `android17-release` 设备树、AOSP 补丁和系统构建 | `system.img`、`vendor.img`，以及本次冻结的运行辅助包 |
| 本仓库 | QEMU Windows ARM 补丁/构建、SSH 收集、SHA-256 校验、运行包和启动 | `bundles/network-audio-20260926/` 与本机 QEMU |

## 当前已验证的配对

`profiles/network-audio-20260926.json` 是当前主线配对。它固定设备树提交 `a371f1d`、内核提交 `ac04a34`、QEMU 上游提交 `5f664cd`。Windows ARM/WHPX 已验证 `sys.boot_completed=1`，ADB、Launcher3、Settings、Ethernet、扬声器 PCM、点按音和铃声试听可用。内核 SHA-256 为 `f33ef2371736dfd75122eaaf277223321117b57e572e235b5b35f558aa14db96`，system 为 `13c41b3d33b718075713d1472590a57b385f25610a2523bbe79d92a086df3636`，vendor 为 `67616fae719997d6a779e8d1c8a99c0de2ee8b7a72aa05c97c480bba56322eb2`。QEMU 的 PE Machine 为 `0xAA64`。此前的 Launcher3 + Settings 配对仍留在 `profiles/launcher3-settings-20260926.json`。

真实像素动态分辨率已经收进主线，开发记录仍在 `feature/native-resolution-20260926`。Surface 的 Windows 桌面为 2880×1920、系统缩放为 200%；QEMU 默认手机窗口驱动客体为 864×1728，最大化后自动切换为 2784×1876，任意横向窗口实测为 2374×1530，恢复窗口又回到 864×1728。Windows 整桌截图逐项确认了 Android 的四边、状态栏、搜索框和三键导航完整可见，不再只显示左上角，也不是拉伸旧画布。三次模式切换中 SurfaceFlinger 和 Launcher3 PID 均未变化，触摸在分辨率稳定后可交互。该结论使用的内核 SHA-256 为 `e7ede20ab411b628f59f5345a7fa5da1155cd2a0a40dad45247f9498cacca06b`，vendor SHA-256 为 `674652a3e965c36b20fd50eff2e3bd7c7a2ae1553cc8a76be3d1ed0925efb396`。当前主线内核 `f33ef2371736dfd75122eaaf277223321117b57e572e235b5b35f558aa14db96` 保留该 EDID 补丁，并增加 virtio-sound 与 dma-buf system heap。

运行时另需 `kikiaosp-runtime-support-20260926.tar.zst`，包含已经验证的启动 ramdisk、空 product/system_ext/odm、8 GiB F2FS userdata 与 misc。该压缩包只有约 21 MiB，当前保存在 185 的 `~/projects/kikiaosp_test/output/`，**不在 Git 中**；其中 ramdisk 并非当前 `m systemimage vendorimage` 的自动产物。收集脚本会验证压缩包及解出的每个文件，避免漏掉这项历史冻结依赖。换开发机时必须迁移同一压缩包；从新 AOSP 源码重新生成字节一致 ramdisk 的配方尚未完成。

## 目录

```text
patches/                           本机 QEMU 的两个下游源码补丁
profiles/                          已验证镜像配对及各文件哈希
tools/build_qemu_arm64.sh           MSYS2 CLANGARM64 构建 QEMU
tools/collect_kikiaosp_assets.ps1   SSH 取得并校验完整镜像组
tools/run_kikiaosp_touch_local.ps1  启动 QEMU/WHPX
tools/capture_qemu_window.ps1       截取实际 Windows 桌面上的 QEMU 窗口
bundles/                           收集后生成的运行包；不纳入 Git
tools/qemu-src/                     本地 QEMU 源码与构建输出；不纳入 Git
```

## 1. 让两台 Linux 构建仓库产出镜像

在 185 开发机上，设备树仓库按其 [README](https://github.com/kekeqwq/kikiaosp_test) 用固定 Android 17 manifest 同步 AOSP、应用补丁、审计并构建：

```bash
cd ~/aosp-master
source build/envsetup.sh
lunch kikiaosp_test_arm64_phone-trunk_staging-userdebug
m -j8 systemimage vendorimage
sha256sum out/target/product/kikiaosp_test/{system,vendor}.img
```

内核仓库按其 [README](https://github.com/kekeqwq/kikiaosp_kernel) 运行：

```bash
cd ~/projects/kikiaosp_kernel
nix build
sha256sum result/boot/kernel
```

本次固定辅助包位于 `~/projects/kikiaosp_test/output/kikiaosp-runtime-support-20260926.tar.zst`。它是未追踪的二进制运行资产，不应假设只克隆设备树仓库就会有它。上述三个路径和预期哈希均在 profile 中；产物变更应新建 profile，经过 Windows 实测后再更新默认值，而不是覆盖已验证版本。

## 2. 在 Windows ARM 构建本机 QEMU

安装 MSYS2，在 **CLANGARM64** 终端中更新并安装依赖；不要使用 x86_64 UCRT64 目标：

```bash
pacman -Syu
# 如 pacman 要求，重开 CLANGARM64 终端后执行 pacman -Su。
pacman -S --needed git make ninja python pkgconf \
  mingw-w64-clang-aarch64-clang mingw-w64-clang-aarch64-glib2 \
  mingw-w64-clang-aarch64-gtk3 mingw-w64-clang-aarch64-libslirp \
  mingw-w64-clang-aarch64-pixman mingw-w64-clang-aarch64-zstd \
  mingw-w64-clang-aarch64-libepoxy
./tools/build_qemu_arm64.sh
```

脚本从上游 QEMU 检出固定提交，依次应用 `patches/qemu-windows-gtk-full-redraw.patch`、`patches/qemu-windows-arm64-gtk-touch.patch` 和 `patches/qemu-windows-gtk-glarea-wgl.patch`，再构建 `aarch64-softmmu` 的 GTK/WHPX 版本。前两份补丁分别修复 Windows GTK 部分重绘，以及 Surface 原生 GDK 触摸、坐标映射、ARM64 MinGW 构建适配、动态尺寸和 HiDPI 1:1 绘制比例；第三份使 Windows GtkGLArea 使用 WGL 时不错误调用 EGL，供可选的 VirGL 路线使用。脚本遇到已有脏 QEMU 源码会拒绝覆盖；可传另一个空目录作参数。本机已有构建在 `tools/qemu-src/build/qemu-system-aarch64.exe`，运行时脚本会把 `C:\msys64\clangarm64\bin` 加入该进程的 DLL 搜索路径。单独复制 EXE 不等于可移植安装包，还需要匹配的 MSYS2 运行库。

在 Windows PowerShell 中检查已构建 EXE 的 PE Machine（ARM64 为 `0xAA64`）：

```powershell
$exe = (Resolve-Path .\tools\qemu-src\build\qemu-system-aarch64.exe).Path
$bytes = [IO.File]::ReadAllBytes($exe)
$offset = [BitConverter]::ToInt32($bytes, 0x3c)
'0x{0:X4}' -f [BitConverter]::ToUInt16($bytes, $offset + 4)
```

## 3. 从远端取回、验证并打包

Windows 需要 PowerShell 7、OpenSSH `ssh/scp`、系统 `tar` 和 Android `adb`。先确认能免密登录开发机，且 C 盘有至少 12 GiB 余量。当前开发机是 `keke@192.168.2.185`：

```powershell
ssh keke@192.168.2.185 'test -f ~/aosp-master/out/target/product/kikiaosp_test/system.img && echo READY'
.\tools\collect_kikiaosp_assets.ps1
```

收集脚本先核对两仓库 Git 修订，再用 SCP 拉回内核、system、vendor 与冻结辅助包；每项检查大小和 SHA-256，严格核对压缩包内容及解包后的六个辅助文件。默认使用主线清单 `profiles/network-audio-20260926.json`，成功后产生 `bundles/network-audio-20260926/` 与 `bundle-profile.json`。测试其他镜像时可传 `-ProfilePath`，输出目录默认取清单中的 `id`。若 SSH 地址或路径不同，可传 `-AospHost`、`-KernelHost`、`-AospRoot`、`-DeviceRepo`、`-KernelRepo`；若已手动取得同一辅助包，可用 `-LocalSupportArchive`。为防止混入旧镜像，脚本拒绝覆盖已有输出目录；再收集时使用新的 `-OutputDir`。失败会保留不完整目录供检查，不会伪称打包成功。

## 4. 启动与验证

```powershell
.\tools\run_kikiaosp_touch_local.ps1
adb connect 127.0.0.1:5555
adb shell getprop sys.boot_completed
adb shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.HOME
```

脚本默认使用刚收集的 bundle、原生 ARM64 QEMU、WHPX、`virtio-gpu-pci`、`virtio-multitouch-pci` 和作为客机外设的 `virtio-keyboard-pci`。Windows QEMU 监控端口只监听 `127.0.0.1:4447`，ADB 转发端口为 `127.0.0.1:5555`。`-snapshot` 使测试不写回基础磁盘；串口、客机日志和显示初始化记录写入 bundle，每次自动加时间戳而不覆盖旧日志。启动器会在后台等 Android 启动完成，再配置满电虚拟电池与 AC 供电、关闭锁屏和屏保、保持唤醒并回到 Launcher 桌面；配置结果写入 `.display.log`。可用 `-BundleDir`、`-QemuPath` 等参数测试新配对；不要把别的 vendor 与此 profile 混用。

实际画面证据要截整张 Windows 桌面且保持 QEMU 普通窗口，不要最大化：

```powershell
.\tools\capture_qemu_window.ps1 -Tag check -KeepWindowed
```

截图写入 `~/Downloads/temp/`，不上传仓库。测试完请关闭 QEMU 窗口，避免占用桌面和端口。

主线默认启动包含真实像素动态分辨率、Ethernet 和扬声器。点按音与铃声试听依赖当前 dma-buf 内核，以及 `media.c2.hal.selection=aidl` 的 system 镜像。VirGL 已能经 Adreno 渲染，但帧率离日用目标仍有明显差距，高刷也未达标。关闭扬声器可传 `-SpeakerOutput:$false`，关闭动态分辨率可传 `-NativeResolution:$false`。

GPU 加速实验记录见 [GPU_ACCELERATION.md](GPU_ACCELERATION.md)。功能分支增加了显式 `-GpuMode Virgl`、`-GpuBlob` 与 `-DryRun`。Mesa VirGL 已经在 Adreno 上渲染出 Android Settings；当前原生 864×1728 受控滑动中位帧耗时约 69ms，仍在优化。`-GpuBlob` 用来验证 QEMU 共享 GPU 内存路径，必须搭配本功能分支的 Mesa 镜像，例如：

```powershell
.\tools\run_kikiaosp_touch_local.ps1 `
  -BundleDir .\bundles\gpu-virgl-native-20260927 `
  -SystemImage system-kikiaosp-gpu-virgl-native.img `
  -VendorImage vendor-kikiaosp-gpu-virgl.img `
  -GpuMode Virgl `
  -GpuBlob
```

GPU 性能对比固定使用 864×1728 作为最小可用手机模式；启动器会拒绝更低的启动分辨率。基准程序按客机完整视口渲染，不做内部缩放，不通过降低分辨率制造虚高 FPS。

### 主线：真实像素动态分辨率

使用配套内核和 vendor 时传入 `-NativeResolution`。初始尺寸按物理像素指定，默认是手机比例的 864×1728；Windows 200% 缩放下窗口客户区是 432×864 逻辑单位，但占用并显示 864×1728 个物理像素：

```powershell
.\tools\run_kikiaosp_touch_local.ps1 `
  -NativeResolution `
  -ResolutionTrace `
  -KernelImage C:\path\to\kernel-linux-7.3-rc4-4k-edid-sync `
  -VendorImage C:\path\to\vendor-kikiaosp-resolution.img
```

窗口停止拖动约一秒后，QEMU 将客户区的物理像素尺寸通过 VirtIO GPU UIInfo/EDID 交给客体；内核等待 EDID 与 display-info 同步完成，HWC 在同一 Android 显示对象上更新参数。尺寸还没对齐时，GTK 把上一帧完整等比放进当前窗口，拖动过程中不会只剩左上角；对齐之后绘制回到一比一，Surface 200% 缩放下是一个客体像素对一个宿主物理像素。触摸坐标使用同一次绘制比例。模式切换时 virtio-gpu 会暂时保留上一帧，避免 Android 拆平面时闪成黑屏。

主线已验证 864×1728 → 2784×1876 → 864×1728，以及任意横向 2374×1530；这不等于高刷或宿主 GPU 加速已经完成。测试证据保存在本机 `~/Downloads/temp/`，其中整桌截图可能包含私人桌面背景，不提交或上传。

### 主线：Ethernet、扬声器与界面音效

Android 为 QEMU 的 `virtio-net-pci` 注册 Ethernet 默认网络，静态地址 `10.0.2.15/24`、网关 `10.0.2.2`、DNS `10.0.2.3`，并声明 `android.hardware.ethernet`。客机不提供 Wi-Fi 或移动数据；QEMU user networking 经 Windows 当前可用路由出站。

内核启用 `CONFIG_SND_VIRTIO=y`，以及 `CONFIG_DMABUF_HEAPS_SYSTEM=y`。启动脚本默认打开 `-SpeakerOutput` 和 `-NativeResolution`：QEMU 使用 `virtio-sound-pci` 的单路播放和 Windows DirectSound，同时关闭旧的 Android 音频软件输出开关。媒体流默认音量是 15/15，实际听感由 Windows 上 QEMU 的音量调节。`media.c2.hal.selection=aidl` 提供 Vorbis 解码；没有 `/dev/dma_heap/system` 时，PCM 能响，点按音和铃声试听会失败。

```powershell
.\tools\collect_kikiaosp_assets.ps1
.\tools\run_kikiaosp_touch_local.ps1

adb connect 127.0.0.1:5555
adb shell getprop sys.boot_completed
adb shell pm has-feature android.hardware.ethernet
adb shell dumpsys ethernet
adb shell cat /proc/asound/cards
adb shell getprop media.c2.hal.selection
adb shell cmd media_session volume --stream 3 --get
```

已在 Windows 客机确认 Ethernet、扬声器 PCM、设置里的点按音和铃声选择器试听。运行中的 QEMU 窗口归正在操作的人控制，测试结束前不要从脚本里关闭它。

当前产物在 `bundles/network-audio-20260926/`。内核、system、vendor 的 SHA-256 依次是 `f33ef2371736dfd75122eaaf277223321117b57e572e235b5b35f558aa14db96`、`13c41b3d33b718075713d1472590a57b385f25610a2523bbe79d92a086df3636`、`67616fae719997d6a779e8d1c8a99c0de2ee8b7a72aa05c97c480bba56322eb2`。这三份文件对应同一主线，不能与旧基线镜像混用。
