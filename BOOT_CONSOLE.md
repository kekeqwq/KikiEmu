# KikiAOSP 原生 SDL 启动控制台

最初在 `feat/sdl-boot-console-20260930` 独立开发，2026-09-30 实测并经用户“很完美了，推入主线”验收后纳入 main。未修改已验收 Android 镜像或内核，也未覆盖遮罩前的冻结 QEMU。遮罩直接绘制在同一个 SDL/OpenGL 窗口，不建立第二个 Windows 窗口，不使用远程画面或网页。

## 行为

1. SDL 创建窗口时先隐藏，准备并绘制完整启动画面后再显示。首画面为纯文本 `#` 拼出的 KIKIAOSP 大 Logo；Logo 固定在上方。
2. 下方滚动显示当前启动的内核串口 `[KRN]`、Android logcat `[AND]`、启动配置 `[HOST]`。仅做有界文件读取，不接管或阻塞日志写入；完整原始日志仍在 bundle。采用 QEMU 自带 VGA 点阵字体，不依赖字体包或图片；UI 日志显示 ASCII，原始 UTF-8 不改变。
3. 启动配置辅助进程验证 `sys.boot_completed=1`、120 Hz 实际 render rate、显示/电源配置、Launcher HOME 焦点与 SurfaceFlinger 桌面层，再原子写入 READY 状态。
4. 显示“System started successfully”和本次真实启动参数。只有 READY 和客机有效 scanout 均具备时才短暂展示成功摘要、交还 Android 桌面；不是固定时间播放完就假定开机。
5. 超时或配置异常显示 STARTUP FAILED，保留日志及原因；不会显示成功或自行把错误隐藏。窗口仍可正常关闭。遮罩期间不转发操作到隐藏的 Android；交接后走原有原生触摸和外设键盘路径。
6. 交接后停止读日志、释放遮罩纹理/像素缓冲/着色器，不叠加在日常 Android 渲染上。

实现依据为 [SDL 创建窗口](https://wiki.libsdl.org/SDL2/SDL_CreateWindow) 的 hidden/HiDPI 标志和 [OpenGL 窗口呈现](https://wiki.libsdl.org/SDL2/SDL_GL_SwapWindow)，具体集成以本仓库固定 QEMU 版本的 SDL refresh/scanout 回调为准。

## 构建与启动

MSYS2 CLANGARM64 依赖同主 README，另外确保安装原生 `mingw-w64-clang-aarch64-pkgconf`。使用新的干净源码目录，保留原来的稳定 EXE：

```bash
./tools/build_qemu_arm64.sh
```

构建器默认目录为 `tools/qemu-boot-src`，依次应用原稳定补丁与 `patches/qemu-sdl-boot-console.patch`，默认8路，脏源码仍拒绝覆盖。当前开发 worktree 已打过补丁，后续增量使用 `ninja -C tools/qemu-boot-src/build -j8 qemu-system-aarch64.exe`，不要对它重复运行完整应用步骤。要验证从零应用补丁，可以传一个新的空源码目录；只有显式 `KIKI_BOOT_CONSOLE=0` 才构建不带遮罩的对照版。

```powershell
.\tools\run_kikiaosp_local.ps1
# 单次不显示启动遮罩（仍使用同一新版 QEMU）：
.\tools\run_kikiaosp_local.ps1 -BootConsole:$false
# 显式回溯旧 EXE 和同组镜像：
.\tools\run_kikiaosp_local.ps1 -ProfilePath .\profiles\surface-main-pre-boot-console-20260930.json
```

统一启动器根据 profile 自动启用遮罩，显式 `-BootConsole:$false` 可关闭；低层 A/B 启动器仍须显式 `-BootConsole`。启动器会检查新 EXE 含 `KIKI_SDL_BOOT_STATUS` 能力，不能用旧 QEMU 静默忽略遮罩选项。默认使用已验收的 surface-main-20260930 全镜像组：8 vCPU/4 GiB、1003×1556、288 dpi、字体1.5、SDL/VirGL、120 Hz、无 Grab、相机和音频保留。不改宿主刷新率。

## 状态与证据

每个唯一启动 Tag 生成独立 `.boot-status.ini`、`.boot-events.log`，以及原串口、logcat、display 配置日志。BOOT 状态通过临时文件加原子替换写入，避免读取半行 READY。启动参数由启动器生成，成功摘要的 Android/内核版本及 active render rate 来自实际 ADB 查询。

验收版本为原生 Windows ARM64 QEMU 11.1.50，EXE SHA-256 `3004741332643cfd775f83ad990714716ca9975ae341f0355fda7fded4c4a651`。完整稳定资产启动 Tag 为 `sdl-boot-first-20260930`，窗口 PID16704；实际 Windows 整桌截图中，固定文本 Logo 和不断更新的内核日志已可见，随后成功切回 Launcher 桌面。用户当场确认效果并要求纳入主线。

本次事件日志记录：控制台创建0.009秒、Android HOME 验证33.006秒、实际客机 scanout34.082秒、桌面交接34.810秒。这是一次实际样本，不是承诺开机时间，也不是固定34秒撤遮罩。状态为 `READY`；实际查询 Android17、Linux7.3.0-rc4-4k、active render120.00Hz、HOME有焦点和SurfaceFlinger层。成功摘要展示约1.8秒后交接，交接后不再轮询日志。保留原生像素、无 Grab、无额外 console 窗口。

PowerShell状态写入单元测试覆盖 WAITING/READY/ERROR、INI换行和反斜杠转义、UTF-8无BOM和原子替换；脚本语法及补丁检查通过。正常启动/交接已有实测及用户验收；启动失败分支有错误显示/不撤遮罩实现，但本次没有故意破坏已验收镜像来做端到端故障注入，不把它写成已完成的实机验收。

提交前用独立临时 Git index 从固定上游顺序应用两份已追踪补丁，五个启动控制台集成文件与实际编译源码完全一致。默认入口 DryRun 确认自动启用遮罩、新 EXE、SDL/VirGL、120Hz、1003×1556、8vCPU/4GiB；旧5b92 EXE强制开启遮罩被能力检查拒绝。PE头确认为原生ARM64(0xAA64)，不是x86转译版本；新旧profile全部客机资产逐项相同，旧EXE哈希仍匹配。

截图 `qemu-desktop-sdl-boot-early-20260930.png` 和 `qemu-desktop-sdl-boot-desktop-20260930.png` 只留本机 `~/Downloads/temp`，原始日志仍在 bundle，均不进入 Git。遮罩前的5b92冻结 EXE仍保留。MSYS2构建修正采用原生CLANGARM64 `pkgconf.exe`，原自定义包装器已另存，不修改宿主显示或键盘全局配置。
