# KikiEmu — Windows ARM 启动仓库

本仓库只保存 Windows ARM 本机启动和验证 KikiAOSP 的脚本。Android 设备树、系统构建与 AOSP 改动位于 [kikiaosp_test](https://github.com/kekeqwq/kikiaosp_test)；4 KiB 主线内核位于 [kikiaosp_kernel](https://github.com/kekeqwq/kikiaosp_kernel)。早期 HCS/.NET 方案已退出当前启动路线，历史代码仍可在 Git 历史中查看。

## 已验证的稳定基线（2026-09-26）

- Windows on ARM 上由本地构建的 `qemu-system-aarch64.exe` 经 WHPX 运行 AArch64 客机，Linux 7.3-rc4 4 KiB 内核。
- AOSP `kikiaosp_test_arm64_phone` 启动到 `sys.boot_completed=1`，Launcher3/Quickstep 为默认桌面，Settings 可打开；三键导航、多任务和通知栏已交互验证。
- `virtio-multitouch-pci` 供触摸输入，`virtio-keyboard-pci` 作为客机外接键盘设备；ADB 经本机 `127.0.0.1:5555` 访问。
- 最新系统镜像不预装 `KikiWindowTest`；旧测试 APK 仍是设备树中的可选回归用例，不是稳定产品配置。
- 目前是 GTK 窗口中的客机图形输出，未完成随窗口变化的动态分辨率、高刷或宿主 GPU 原生硬件渲染验证；窗口最大化会模糊，可能短暂黑屏，日常测试保持普通窗口。音频实际播放也尚未验证。

## 本机所需文件

启动脚本不会下载或构建镜像。将下列文件放在 `aosp/windows-arm64-test/`，并将已构建的 QEMU 放在 `tools/qemu-src/build/qemu-system-aarch64.exe`：

| 文件 | 用途 |
| --- | --- |
| `kernel-linux-7.3-rc4-4k-netfilter-20260925` | 4 KiB 内核 |
| `kiki-kernel-ramdisk-rc4-clean-odm-adb.img` | 启动 ramdisk |
| `system-kikiaosp-launcher3-settings-stable-20260926.img` | 已验证的系统镜像 |
| `vendor-kikiaosp-ui-scanout-pixel-count-20260926.img` | 对应 vendor 镜像 |
| `kiki-empty-product.img`, `kiki-empty-system_ext.img` | 空分区镜像 |
| `userdata-kikiaosp17-f2fs.img`, `misc.img`, `odm-kikiaosp17-empty.img` | 数据与辅助分区 |

这些二进制镜像和 QEMU 构建目录体积较大，不纳入此 Git 仓库。系统镜像的 SHA-256 为 `002e67758ff9cec0cc7c31161ba3cf12be3fad7a8fdfd0e6e4c559dcc830c85e`。QEMU 本地源码检出包含 Windows GTK 重绘修复，当前提交 `bde658e`；若在新机器构建 QEMU，须按设备树仓库 README 的构建说明准备相应版本和 Windows 适配。单有本仓库不足以复现二进制。

## 启动和验证

在 PowerShell 中执行：

```powershell
.\tools\run_kikiaosp_touch_local.ps1
adb connect 127.0.0.1:5555
adb shell getprop sys.boot_completed
adb shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.HOME
```

脚本默认附加触摸和外接键盘设备，使用 `-snapshot` 运行，不写回原始磁盘镜像。串口和客机日志保存在 `aosp/windows-arm64-test/`；每次运行生成新的时间戳标签，不覆盖旧日志。QEMU 监控端口仅监听 `127.0.0.1:4447`。

如需对指定版本测试：

```powershell
.\tools\run_kikiaosp_touch_local.ps1 -Tag my-test -SystemImage system-kikiaosp-launcher3-settings-stable-20260926.img
```

捕捉整张 Windows 桌面以核对实际前台 QEMU 窗口（保持普通窗口尺寸）：

```powershell
.\tools\capture_qemu_window.ps1 -Tag my-test -KeepWindowed
```

截图写入 `~/Downloads/temp/`，不上传仓库。测试结束请关闭 QEMU 窗口，避免它持续占用桌面和端口。
