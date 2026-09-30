# Surface 前后摄 — 2026-09-30 功能分支

当前成果仍在 `feat/surface-camera-20260930`，不覆盖已冻结的主线配对。实际 Windows ARM64 QEMU + WHPX、SDL、VirGL 下，前后摄预览已出真实影像；修正方向后，用户确认快门可拍摄，三张 640×480 JPEG 已保存到客机 `Pictures` 并在本机验证可解码。

## 构建与启动

设备树仓库负责 Camera HAL 和 AOSP 集成补丁，本仓库负责 Windows 原生采集桥接及 QEMU 启动。内核此次无需重编。先在 185 的设备树仓库应用、审计补丁，然后增量构建：

```bash
cd ~/projects/kikiaosp_test
# 首次准备干净 AOSP 源码按该仓库 README 应用完整集成；不要对已修改源码盲目重复 patch。
./scripts/audit-aosp-integration.sh ~/aosp-master
cd ~/aosp-master
source build/envsetup.sh
lunch kikiaosp_test_arm64_phone-cp2a-userdebug
m -j8 vendorimage
sha256sum out/target/product/kikiaosp_test/vendor.img
```

本功能的最终 vendor 为 `115626c3ad9a31744389307e135e8a6a08922749ec1e02cf5f3b3d3cb2848350`，其他镜像配对见 [profile](profiles/surface-camera-20260930.json)。已冻结产物仍是 Git 之外的运行资产；只 clone Git 不会自动取得镜像。此 profile 是新启动器的配对记录，不适用于旧的 `collect_kikiaosp_assets.ps1` 收集格式。

在 MSYS2 **CLANGARM64** 安装 C++/WinRT 头文件：

```bash
pacman -S --needed mingw-w64-clang-aarch64-cppwinrt
```

PowerShell 7 在本仓库运行：

```powershell
$env:PATH = 'C:\msys64\clangarm64\bin;' + $env:PATH
./tools/build_surface_camera_bridge.ps1
scp keke@192.168.2.185:/home/keke/aosp-master/out/target/product/kikiaosp_test/vendor.img ./bundles/cp2a-camera2-20260930/vendor-kikiaosp-camera-virgl-final-20260930.img
./tools/run_kikiaosp_surface_camera.ps1 -ValidateAllAssets
adb connect 127.0.0.1:5555
adb shell am start -W -n com.android.camera2/com.android.camera.CameraActivity
```

QEMU 使用 [已追踪的稳定 SDL 补丁](patches/qemu-kikiaosp-tested-surface-20260929.patch)，不需要本次 transfer 日志诊断改动。构建方法见主 README。保持 QEMU 在正常 build 目录，不能仅复制一个 EXE 到没有 ROM/运行依赖的目录后裸启动。

启动器保持窗口 1003×1556、Android 288dpi / font scale 1.5、客机 120Hz、8 vCPU / 4 GiB，无 Grab、无 console。宿主显示设定不变。`-ValidateAllAssets` 检查全部镜像哈希，包含大 userdata；正常启动只验证全部文件长度及内核/vendor 哈希。启动脚本另核对编译出的 SDL 能力标记并输出 QEMU 路径与 SHA-256。

默认 `-snapshot`，本次启动中新增照片和应用数据不持久保存。关闭前需要保留照片时先 `adb pull /sdcard/Pictures <本机私有目录>`。不得把这些照片和桌面截图提交到仓库。

## 修复边界

- 前摄原默认 profile 在真实 Windows 回调中报 `MF_E_UNEXPECTED`。桥接使用设备提供的 Windows Studio Effects **no-effects color passthrough** profile `{E4ED96D9-CD40-412F-B20A-B7402A43DCD2},0`，在专用 STA 线程运行 WinRT reader；后摄使用 Media Foundation SourceReader。不改宿主摄像头全局设定，不采集麦克风。
- 桥接传输紧凑 NV12，经专用 virtio-serial channel 到设备树内的 SurfaceCamera HAL。OPEN 先释放旧 source；CLOSE 释放 Windows source/reader 后才回复 CLOSED；QEMU 断开后桥接退出。
- VirGL 黑预览的根因不是相机没有数据：同一帧 host/HAL 签名一致、RGBA 非黑，但 host renderer 的上传/读回返回 EINVAL。minigbm FD 在首次 PRIME import 前没有初始化 context，导入资源未附着于后创建的 context。设备仓库的 `aosp-kikiaosp-virgl-context-before-prime.patch` 提前初始化 VirGL context，实测 transfer 恢复成功。只在 VirGL 3D + CONTEXT_INIT 路径生效，排除 gfxstream。
- Windows 交付的帧已正向，设备树不应照抄手机的 `270°`。前后摄均报告 `SENSOR_ORIENTATION=0`，前摄镜像由 Android 处理。依据为 [Android 相机方向说明](https://developer.android.com/develop/devices/chromeos/learn/camera-orientation)，并用实际 Windows 桌面截图核对。
- 快门灰掉的根因是 Camera2 的 CPU `YUV_420_888` ImageReader（usage `0x20033`）没有可分配组合。`aosp-kikiaosp-virgl-cpu-camera-yuv.patch` 增加 CPU/camera NV12 组合，并在无 host GBM、无 GPU/scanout usage、原生不支持多平面格式时使用 minigbm 已有的 R8 字节缓冲 emulation。不是强行声明不存在的原生 NV12 GPU 纹理支持，也不关闭 VirGL。

这里的“硬件加速”是相机预览使用已打通的 VirGL GPU 显示/合成路径；采集、NV12 传输、HAL 色彩转换及 JPEG 压缩仍有 CPU 工作。没有宣称 GPU 零拷贝、硬件 JPEG 编码或视频录制已验收。

## 诊断与退出

```powershell
adb shell dumpsys SurfaceFlinger | Select-String 'GLES:'
adb shell dumpsys media.camera
adb shell ls -l /sdcard/Pictures
./tools/capture_qemu_window.ps1 -KeepWindowed -Tag camera-check
# 使用启动器输出的确切 PID，优先关闭相机并等待 QEMU 正常退出。
./tools/stop_kikiaosp_local.ps1 -QemuProcessId <PID>
```

屏幕证据保存在本机 `~/Downloads/temp`，DPI-aware 全桌面 2880×1920，窗口不最大化。bridge 自己将日志写到运行包，不能用外部终端 stderr pipe/tee 长期转发，以免管道退出后阻塞采集。完整截图含私人内容，不应上传公开仓库。

参考： [WinRT MediaFrameReader](https://learn.microsoft.com/en-us/windows/apps/develop/camera/process-media-frames-with-mediaframereader)、[微软 no-effects profile 定义](https://github.com/microsoft/Windows-Camera/blob/master/Samples/WindowsStudio/Windows%20Studio%20Effects%20DDIs.md)、[Android BufferQueue / gralloc](https://source.android.com/docs/core/graphics/arch-bq-gralloc)。
