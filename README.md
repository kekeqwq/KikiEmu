# KikiEmu — Windows ARM 运行与打包主仓库

本仓库负责把 KikiAOSP 的镜像变成可在 Windows ARM 本机启动的运行包：保存原生 ARM64 QEMU 的补丁与构建配方，从开发机取得内核和 Android 镜像，校验配对、解包辅助磁盘，再启动和截图。早期 HCS/.NET 尝试已退出当前路线，历史代码仍可从 Git 历史找回。

| 仓库 | 负责什么 | 交付给本仓库的产物 |
| --- | --- | --- |
| [kikiaosp_kernel](https://github.com/kekeqwq/kikiaosp_kernel) | Linux 7.3-rc4、4 KiB 内核配置和 Nix 构建 | `result/boot/kernel` |
| [kikiaosp_test](https://github.com/kekeqwq/kikiaosp_test) | Android 17 `android17-release` 设备树、AOSP 补丁和系统构建 | `system.img`、`vendor.img`，以及本次冻结的运行辅助包 |
| 本仓库 | QEMU Windows ARM 补丁/构建、SSH 收集、SHA-256 校验、运行包和启动 | `bundles/launcher3-settings-20260926/` 与本机 QEMU |

## 当前已验证的配对

`profiles/launcher3-settings-20260926.json` 是主分支稳定配对的文件名、字节数、SHA-256 和远端路径清单。它固定设备树源提交 `f26f688`、内核提交 `b43c32b`、QEMU 上游提交 `5f664cd`。185 开发机当前输出的 `system.img`（SHA-256 `002e6775…c830c85e`）和 `vendor.img`（`be0725a5…97b591be`）已经在 Windows ARM/WHPX 上一起启动过：`sys.boot_completed=1`，ADB、Launcher3、Settings 与真实多任务界面可用。QEMU 的 PE Machine 为 `0xAA64`，不是经 x86 转译的程序。

`feature/native-resolution-20260926` 在这条稳定线之上完成了真实像素动态分辨率验证。Surface 的 Windows 桌面为 2880×1920、系统缩放为 200%；QEMU 默认手机窗口驱动客体为 864×1728，最大化后自动切换为 2784×1876，任意横向窗口实测为 2374×1530，恢复窗口又回到 864×1728。Windows 整桌截图逐项确认了 Android 的四边、状态栏、搜索框和三键导航完整可见，不再只显示左上角，也不是拉伸旧画布。三次模式切换中 SurfaceFlinger 和 Launcher3 PID 均未变化，触摸在分辨率稳定后可交互。本功能分支配套的内核 SHA-256 为 `e7ede20ab411b628f59f5345a7fa5da1155cd2a0a40dad45247f9498cacca06b`，实测 vendor SHA-256 为 `674652a3e965c36b20fd50eff2e3bd7c7a2ae1553cc8a76be3d1ed0925efb396`。

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

脚本从上游 QEMU 检出固定提交，依次应用 `patches/qemu-windows-gtk-full-redraw.patch` 与 `patches/qemu-windows-arm64-gtk-touch.patch`，再构建 `aarch64-softmmu` 的 GTK/WHPX 版本。第一份补丁修复 Windows GTK 部分重绘；第二份包含 Surface 原生 GDK 触摸、坐标映射、ARM64 MinGW 构建适配、GTK 窗口到 VirtIO GPU UIInfo 的动态尺寸传递，以及 Windows HiDPI 下客体像素到宿主物理像素的 1:1 Cairo 绘制比例。脚本遇到已有脏 QEMU 源码会拒绝覆盖；可传另一个空目录作参数。实测本机已有构建在 `tools/qemu-src/build/qemu-system-aarch64.exe`，运行时脚本会把 `C:\msys64\clangarm64\bin` 加入该进程的 DLL 搜索路径。单独复制 EXE 不等于可移植安装包，还需要匹配的 MSYS2 运行库。

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

收集脚本先核对两仓库 Git 修订，再用 SCP 拉回内核、system、vendor 与冻结辅助包；每项检查大小和 SHA-256，严格核对压缩包内容及解包后的六个辅助文件。成功后产生 `bundles/launcher3-settings-20260926/` 与 `bundle-profile.json`。若 SSH 地址或路径不同，可传 `-AospHost`、`-KernelHost`、`-AospRoot`、`-DeviceRepo`、`-KernelRepo`；若已手动取得同一辅助包，可用 `-LocalSupportArchive`。为防止混入旧镜像，脚本拒绝覆盖已有输出目录；再收集时使用新的 `-OutputDir`。失败会保留不完整目录供检查，不会伪称打包成功。

## 4. 启动与验证

```powershell
.\tools\run_kikiaosp_touch_local.ps1
adb connect 127.0.0.1:5555
adb shell getprop sys.boot_completed
adb shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.HOME
```

脚本默认使用刚收集的 bundle、原生 ARM64 QEMU、WHPX、`virtio-gpu-pci`、`virtio-multitouch-pci` 和作为客机外设的 `virtio-keyboard-pci`。Windows QEMU 监控端口只监听 `127.0.0.1:4447`，ADB 转发端口为 `127.0.0.1:5555`。`-snapshot` 使测试不写回基础磁盘；串口和客机日志写入 bundle，每次自动加时间戳而不覆盖旧日志。可用 `-BundleDir`、`-QemuPath` 等参数测试新配对；不要把别的 vendor 与此 profile 混用。首次启动若停在锁屏，可按键或执行 `adb shell input keyevent 82` 解锁。

实际画面证据要截整张 Windows 桌面且保持 QEMU 普通窗口，不要最大化：

```powershell
.\tools\capture_qemu_window.ps1 -Tag check -KeepWindowed
```

截图写入 `~/Downloads/temp/`，不上传仓库。测试完请关闭 QEMU 窗口，避免占用桌面和端口。

主分支稳定包只验证基本图形、Launcher3/Settings、多任务、通知栏与触摸/外接键盘路径；使用主分支配对时最大化仍会拉伸模糊。真实像素动态分辨率已在下述功能分支配对上验证，高刷、宿主 GPU 硬件渲染和真实扬声器播放仍未完成。

### 功能分支：真实像素动态分辨率

使用匹配的功能分支 kernel/vendor 产物时传入 `-NativeResolution`。初始尺寸按物理像素指定，默认是手机比例的 864×1728；Windows 200% 缩放下窗口客户区是 432×864 逻辑单位，但占用并显示 864×1728 个物理像素：

```powershell
.\tools\run_kikiaosp_touch_local.ps1 `
  -NativeResolution `
  -ResolutionTrace `
  -KernelImage C:\path\to\kernel-linux-7.3-rc4-4k-edid-sync `
  -VendorImage C:\path\to\vendor-kikiaosp-resolution.img
```

窗口停止拖动约一秒后，QEMU 将客户区的物理像素尺寸通过 VirtIO GPU UIInfo/EDID 交给客体；内核等待 EDID 与 display-info 同步完成，HWC 在同一 Android 显示对象上更新参数。QEMU GTK 绘制端使用 `1 / gtk_widget_get_scale_factor()`，因此在 Surface 的 200% DPI 下不会把完整客体帧裁成左上角。触摸事件仍以 GTK 逻辑坐标进入，再按同一绘制比例映射到新客体尺寸。

本功能分支已验证 864×1728 → 2784×1876 → 864×1728，以及任意横向 2374×1530；这不等于高刷或宿主 GPU 加速已经完成。测试证据保存在本机 `~/Downloads/temp/`，其中整桌截图可能包含私人桌面背景，不提交或上传。
