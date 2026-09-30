# KikiEmu — Windows ARM 运行与打包主仓库

本仓库负责把 KikiAOSP 的镜像变成可在 Windows ARM 本机启动的运行包：保存原生 ARM64 QEMU 的补丁与构建配方，从开发机取得内核和 Android 镜像，校验配对、解包辅助磁盘，再启动和截图。早期 HCS/.NET 尝试已退出当前路线，历史代码仍可从 Git 历史找回。

| 仓库 | 负责什么 | 交付给本仓库的产物 |
| --- | --- | --- |
| [kikiaosp_kernel](https://github.com/kekeqwq/kikiaosp_kernel) | Linux 7.3-rc4、4 KiB 内核配置和 Nix 构建 | `result/boot/kernel` |
| [kikiaosp_test](https://github.com/kekeqwq/kikiaosp_test) | Android 17 `android17-release` 设备树、AOSP 补丁和系统构建 | `system.img`、`vendor.img`，以及本次冻结的运行辅助包 |
| 本仓库 | QEMU Windows ARM 补丁/构建、SSH 收集、SHA-256 校验、运行包和启动 | `bundles/network-audio-20260926/` 与本机 QEMU |

## 当前已验证的配对

2026-09-30 的 CP2A / Surface 前后摄新功能目前独立于下述旧冻结配对，参见 [相机功能、构建与启动说明](CAMERA_SUPPORT.md)。新配对使用稳定 SDL/VirGL QEMU、正向前后摄和 CPU YUV 拍照路径；不要混用旧 GTK EXE 或旧 vendor。

`profiles/network-audio-20260926.json` 是当前主线配对。它固定设备树提交 `a371f1d`、内核提交 `ac04a34`、QEMU 上游提交 `5f664cd`。Windows ARM/WHPX 已验证 `sys.boot_completed=1`，ADB、Launcher3、Settings、Ethernet、扬声器 PCM、点按音和铃声试听可用。内核 SHA-256 为 `f33ef2371736dfd75122eaaf277223321117b57e572e235b5b35f558aa14db96`，system 为 `13c41b3d33b718075713d1472590a57b385f25610a2523bbe79d92a086df3636`，vendor 为 `67616fae719997d6a779e8d1c8a99c0de2ee8b7a72aa05c97c480bba56322eb2`。QEMU 的 PE Machine 为 `0xAA64`。此前的 Launcher3 + Settings 配对仍留在 `profiles/launcher3-settings-20260926.json`。

真实像素动态分辨率已经收进主线，开发记录仍在 `feature/native-resolution-20260926`。Surface 的 Windows 桌面为 2880×1920、系统缩放为 200%；QEMU 默认手机窗口驱动客体为 864×1728，最大化后自动切换为 2784×1876，任意横向窗口实测为 2374×1530，恢复窗口又回到 864×1728。Windows 整桌截图逐项确认了 Android 的四边、状态栏、搜索框和三键导航完整可见，不再只显示左上角，也不是拉伸旧画布。三次模式切换中 SurfaceFlinger 和 Launcher3 PID 均未变化，触摸在分辨率稳定后可交互。该结论使用的内核 SHA-256 为 `e7ede20ab411b628f59f5345a7fa5da1155cd2a0a40dad45247f9498cacca06b`，vendor SHA-256 为 `674652a3e965c36b20fd50eff2e3bd7c7a2ae1553cc8a76be3d1ed0925efb396`。当前主线内核 `f33ef2371736dfd75122eaaf277223321117b57e572e235b5b35f558aa14db96` 保留该 EDID 补丁，并增加 virtio-sound 与 dma-buf system heap。

运行时另需 `kikiaosp-runtime-support-20260926.tar.zst`，包含已经验证的启动 ramdisk、空 product/system_ext/odm、8 GiB F2FS userdata 与 misc。该压缩包只有约 21 MiB，当前保存在 185 的 `~/projects/kikiaosp_test/output/`，**不在 Git 中**；其中 ramdisk 并非当前 `m systemimage vendorimage` 的自动产物。收集脚本会验证压缩包及解出的每个文件，避免漏掉这项历史冻结依赖。换开发机时必须迁移同一压缩包；从新 AOSP 源码重新生成字节一致 ramdisk 的配方尚未完成。

## 目录

```text
patches/                           本机 QEMU 的 Windows ARM、动态分辨率和诊断补丁
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

脚本固定到 QEMU 上游 `bde658eef6b38c45794bfd7ad4d2dd1b574e4694`，应用 `patches/qemu-kikiaosp-tested-surface-20260929.patch` 中已在 Surface 验证过的 SDL/WGL、DPI 原生像素、Windows WM_POINTER 触摸、禁止鼠标抓取、120Hz Guest UIInfo、VirGL 与 SDL 音频改动，构建 `aarch64-softmmu` 的 GTK/WHPX 版本。此前脚本仍锁在较旧的 `5f664cd` 并只应用部分 GTK 补丁，导致回溯构建出现窗口尺寸、刷新率、触摸和 Grab 回归；新脚本从干净源码检出完整稳定改动。它会将 `pkg-config` 依赖搜索锁定在 CLANGARM64 前缀，避免同机 UCRT64/x86_64 GLib 混入 ARM64 构建。脚本遇到已有脏 QEMU 源码会拒绝覆盖；可传另一个空目录作参数。本机已验证构建在 `tools/qemu-src/build/qemu-system-aarch64.exe`；GPU 实验版通过 `-QemuPath` 指向对应的独立 QEMU 产物。运行时脚本会把 `C:\msys64\clangarm64\bin` 加入该进程的 DLL 搜索路径。单独复制 EXE 不等于可移植安装包，还需要匹配的 MSYS2 运行库。

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

脚本默认使用 SDL 窗口与 SDL 音频后端、原生 ARM64 QEMU、WHPX、`virtio-gpu-pci`、`virtio-multitouch-pci` 和作为客机外设的 `virtio-keyboard-pci`。当前 Surface 配置对应客机原生像素 `1003×1556`、Android 显示密度 `288 dpi` 与字体缩放 `1.5`；在宿主 200% 缩放下，SDL 初始客户区约为 `501×778`。Windows QEMU 监控端口只监听 `127.0.0.1:4447`，ADB 转发端口为 `127.0.0.1:5555`。`-snapshot` 使测试不写回基础磁盘；串口、客机日志和显示初始化记录写入 bundle，每次自动加时间戳而不覆盖旧日志。启动器会在后台等 Android 启动完成，再配置满电虚拟电池与 AC 供电、关闭锁屏和屏保、保持唤醒并回到 Launcher 桌面；配置结果写入 `.display.log`。可用 `-BundleDir`、`-QemuPath` 等参数测试新配对；不要把别的 vendor 与此 profile 混用。

实际画面证据要截整张 Windows 桌面且保持 QEMU 普通窗口，不要最大化：

```powershell
.\tools\capture_qemu_window.ps1 -Tag check -KeepWindowed
```

截图写入 `~/Downloads/temp/`，不上传仓库。测试完请关闭 QEMU 窗口，避免占用桌面和端口。

主线包含真实像素动态分辨率、Ethernet、扬声器，以及可选的 Mesa VirGL 图形加速。VirGL 路径已在 Surface 的 Adreno 宿主上实际渲染 Android UI 和动态 3D 测试场景；它仍需配套 Mesa system/vendor 镜像并显式传 `-GpuMode Virgl`，旧的 network-audio 软件镜像仍保持兼容。点按音与铃声试听依赖 dma-buf 内核及 `media.c2.hal.selection=aidl`。关闭扬声器可传 `-SpeakerOutput:$false`，关闭动态分辨率可传 `-NativeResolution:$false`。

GPU 加速和端到端延迟证据见 [GPU_ACCELERATION.md](GPU_ACCELERATION.md)。VirGL 已从实验路径收敛为可启动、可见并持续渲染的加速路径：客机报告 `Mesa/X.org, virgl, OpenGL ES 3.1 Mesa 20.3.4`。2026-09-29 在 1003×1556、8 vCPU/4 GiB、120 Hz 的 60-FPS-target 动态场景中，热身后连续 30 秒的 29 个一秒窗口为 **70.36–110.89 FPS**，中位数 **80.53 FPS**。这是 APK 的渲染回调帧率，不是 Windows DWM/屏幕实际呈现帧率。桌面、设置和通知栏操作仍有明显延迟与不跟手；加速已实装不等于交互延迟已解决。继续优化以正常资源预算为约束，不靠占满宿主核心或缩小分辨率。`-GpuBlob` 仍是可选共享 GPU 内存实验，当前稳定验证组合不启用它：

```powershell
.\tools\run_kikiaosp_touch_local.ps1 `
  -BundleDir .\bundles\input-audio-apps-20260929 `
  -QemuPath .\tools\qemu-src\build\qemu-system-aarch64.exe `
  -KernelImage kernel-linux-7.3-rc4-4k-fencefix-20260927 `
  -SystemImage system-kikiaosp-input-audio-wallpaper-allowlist-20260929.img `
  -VendorImage vendor-hwc-ebusy-fallback-blocking-default-20260928.img `
  -ProductImage kiki-empty-product.img `
  -SystemExtImage kiki-empty-system_ext.img `
  -GpuMode Virgl -HwcMode Client -DisplayBackend sdl `
  -AudioBackend sdl -SdlSwapInterval 0 -GuestRefreshRateHz 120 `
  -RenderEngineBackend skiaglthreaded -VcpuCount 8 `
  -PortraitWidthPixels 1003 -PortraitHeightPixels 1556
```

启动器默认使用 `-snapshot`，不会写回客机磁盘。上述 APK 帧率样本使用本机 SDL QEMU sidecar 构建；干净 QEMU 构建配方仍以 `tools/build_qemu_arm64.sh` 固定的上游提交和补丁为准。早期 2026-09-28 的 30.41 秒 **SurfaceFlinger presented-frame** 样本为 58.49 FPS；它与后续 APK 回调 FPS 是不同测量边界，不能据此互相替代。旧 `vendor-kikiaosp-gpu-virgl.img` 含逐帧同步诊断，不应用来代表当前稳定组合。

864×1728 是已验证的最小像素预算，不是固定分辨率；启动器要求短边至少 864 像素且总像素不低于 1,492,992。当前默认 `1003×1556` 满足相同像素预算，窗口拉伸仍由客机原生动态分辨率适配，不把低分辨率画面放大糊弄成高分辨率。每次 A/B 必须保持同一客机分辨率，基准程序按完整视口渲染，不做内部缩放，不通过降低分辨率制造虚高 FPS。

GPU 功能分支的产品默认显示密度为 248 dpi、初始字体缩放为 1.3；本机 Surface 启动配置另设 288 dpi、字体缩放 1.5。支持的 SurfaceFlinger GL 后端可用 `-RenderEngineBackend skiaglthreaded`（默认）与 `-RenderEngineBackend skiagl` 做同尺寸对照。

单个 APK 的 FPS 不等于桌面、通知栏的流畅度。GPU 功能分支的 QEMU 源码补丁 `patches/qemu-gtk-global-fps-profile.patch` 提供可选的宿主 GTK GL 渲染回调探针：启动时加 `-GlobalFpsProfile`，记下输出的 `GLOBAL_FPS_LOG` 路径；保持 QEMU 手机窗口大小不变，另开 PowerShell，在 15 秒采样期间实际下拉通知栏：

```powershell
.\tools\measure_kikiaosp_global_fps.ps1 `
  -ProfileLog .\bundles\gpu-virgl-native-20260927\qemu-kikiaosp-<Tag>.global-fps.log `
  -DurationSeconds 15 -Label notification-shade -UiPackages com.android.systemui,com.android.launcher3 `
  -NotificationShadeAnimation
```

探针按固定 5 秒窗口写日志，即使画面静止也会采样；报告将所有窗口的平均回调率与有至少 5 次客机 scanout flush 的窗口分开统计，长于 1 秒的安静间隔不计入帧耗时百分位。窗口如果跨过动画开始/结束，也可能有低帧率和 `idle_gaps`，比较时须查看各窗口记录。`-NotificationShadeAnimation` 通过 ADB 重复展开/收起通知栏；可改用 `-DesktopNavigationAnimation` 测试 HOME、应用抽屉、设置和多任务，不传时可手动操作。报告还给出 Android `gfxinfo`、SurfaceFlinger 错帧计数差值、活动窗口 P95/P99 与 QEMU GL 回调耗时。宿主回调不是 Windows DWM 最终屏幕呈现的直接计数，需与 Android 数据和真实窗口操作一并判断。

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

主线已验证 864×1728 → 2784×1876 → 864×1728，以及任意横向 2374×1530；该尺寸切换测试本身不测量刷新率或 GPU 加速。高刷和 VirGL 另有独立的客机/应用渲染验证，实际宿主呈现帧率仍需单独采集。测试证据保存在本机 `~/Downloads/temp/`，其中整桌截图可能包含私人桌面背景，不提交或上传。

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
