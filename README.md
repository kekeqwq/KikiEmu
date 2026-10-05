# KikiEmu — Windows ARM 原生运行仓库

把 KikiAOSP 镜像收集、校验并启动于 Surface 的 Windows ARM64。CPU 使用 QEMU/WHPX，界面采用 SDL 原生窗口和 VirGL GPU 渲染，不使用 RDP、远程显示或 Habumi 二进制。

| 仓库 | 职责 |
| --- | --- |
| [kikiaosp_test](https://github.com/kekeqwq/kikiaosp_test) | Android 17 设备树、AOSP 集成补丁、源码审计、system/vendor 镜像；维护系统安装包标准与干净发行规划 |
| [kikiaosp_kernel](https://github.com/kekeqwq/kikiaosp_kernel) | Linux 7.3 系列 4 KiB 内核与固定来源的 Nix 构建 |
| 本仓库 | Windows ARM64 QEMU 补丁及构建、摄像头桥接、开发镜像收集/启动/测试；终端用户配置管理器、系统安装器与桌面启动入口 |

## 0.2 Alpha

**本次需要同步更新 setup.exe 和 QEMU 五补丁构建，再用新系统 ZIP 新建实例。** 已有实例不会自动升级系统或内核。下载、准确构建来源、编号复用规则与验收边界见 [0.2 Alpha 发布说明](RELEASE_0_2_ALPHA.md)。0.1 标签和附件保持不变。

## 0.3：音频验收、rc6 与全量 OTA 方向（未发布）

用户长时播放验收未再失声，约 1% 短暂破音暂列已知问题，音频不再继续修改。Linux 7.3-rc6 已在独立实例完成构建、启动/重启验证；没有修改已发布资产。用户明确无需迁移 0.2 数据，允许 0.3 重做基线，后续要求全量 OTA 更新原实例并保留 userdata。当前需求及“设置为主、CLI 为辅、共用原生引擎”的建议见 [0.3 全量 OTA 方向](docs/FULL_OTA_0_3_DIRECTION.md)；[此前增量方案](docs/INCREMENTAL_OTA_DESIGN.md)只作历史。**原生 format-2 / 普通 A/B 已实际实现并构建，同实例已验证签名全量 OTA 1→2→3→4、冷换槽和数据保留；原生签名拒绝、低空间、安装中断续装及失败槽回退已有真实证据。英文更新页面、安全区及明暗配色已修复，候选运行待人工确认。** 详见 [原生 OTA 实施/验收说明](docs/NATIVE_OTA_0_3.md)。没有公开 0.3 Release；已发布 CLI/0.2 实例不因此获得新磁盘布局，不迁移或操作正式实例。

## 0.1 Alpha 与后续开发

终端用户下载 [KikiEmu 0.1 Alpha 的 setup.exe](https://github.com/kekeqwq/KikiEmu/releases/tag/v0.1.0-alpha) 和 [KikiAOSP 0.1 Alpha 系统 ZIP](https://github.com/kekeqwq/kikiaosp_test/releases/tag/v0.1.0-alpha)，先看 [英文使用说明](QUICK_START.md)。QEMU 独立按 [构建说明](QEMU_BUILD.md) 编译，KikiEmu 自动检查并准备 bin 内缺少的运行依赖，用户无需运行 prepare.ps1。版本范围、源码／许可材料和已知限制见 [0.1 Alpha 记录](RELEASE_0_1_ALPHA.md)，此前 R8 过程保留在 [候选记录](CANDIDATE_TEST_20261001.md)。不要使用下面历史 bundle 的启动命令初始化发行实例。

2026-10-01 的旧 format-1 路线仅适用于历史 0.1/0.2；0.3 已按新授权使用 format-2 原生 A/B 新基线，此后正常 OTA 更新原实例并保留数据，不逐版新建实例。取消独立 Dev 通道与严格开发／发行并行隔离门槛；保留单实例 UUID、进程／端点身份和磁盘所有权核验。已有实例不自动换系统，已发布资产不覆盖；0.1不支持就地升级系统或调整磁盘总容量。

### 普通 adb 没有设备时怎么连接

KikiEmu 内置的 ADB 直接访问实例，不自动登记到普通 `adb devices`。Android 已启动但列表为空时，先取得**这次启动**的端口，再连接：

```powershell
$instance = kikiemu info --id 01 | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Could not read instance 01.' }
$endpoint = @($instance.runtime.endpoints | Where-Object role -eq 'adb')
if ($endpoint.Count -ne 1) { throw 'Start instance 01 and wait for Android first.' }
$serial = '127.0.0.1:' + $endpoint[0].port
adb connect $serial
adb devices -l
adb -s $serial shell
```

普通 adb 需要另装 Android Platform Tools；端口每次启动重新分配，**不要固定使用5555**。重启后重新运行上面的命令；多设备时始终指定 `-s $serial`。安装 APK、传照片与断开连接的完整示例见 [QUICK_START.md](QUICK_START.md#connect-ordinary-android-platform-tools-adb)。不使用外部 adb 时可以直接 `kikiemu adb --id 01 --shell "uname -r"`。

开发侧内部回归实例不在公开实例 registry 中，不能用公开 `kikiemu info` 查询；发行用户按上面的命令获取自己的当前端口，不要复用开发日志中的数字。

## 当前开发路线与 0.2 Alpha

唯一 Windows 开发目录为 `C:\Users\keke\Repos\kikiemu`；源码与脚本追踪，QEMU checkout、独立原生组件、镜像、测试磁盘和日志全部放在被忽略的 `build/`。截图放在 `C:\Users\keke\Downloads\temp`。不依赖旧工作目录、用户安装或正式实例。

Android 与内核在 `192.168.2.185` 的干净版本分支上开发。Windows 使用 [五补丁构建配方](QEMU_BUILD.md) 和 [独立系统回归入口](tests/SYSTEM_REGRESSION.md)，不使用下方历史 bundle、固定端口或旧 `.sh` 配方重建当前版本。

0.2 Alpha 完成二次启动黑屏／空闲遮罩交接修补、Linux 7.3-rc5 升级，并修正启动器系统版本显示和编号复用。因启动器也发生修改，两边均发布0.2；发版仍按实际改动分别推进，不要求以后同步版本。已发布 0.1 标签及附件不覆盖，不开展性能优化。

## 历史开发回溯主线：2026-09-30

以下为历史实现与证据，包含已过时的候选状态、目录和构建命令，不作为当前开发或发版入口。

唯一默认清单是 [surface-main-20260930.json](profiles/surface-main-20260930.json)，启动入口是 [run_kikiaosp_local.ps1](tools/run_kikiaosp_local.ps1)。具体验收、哈希和限制见 [基线记录](BASELINE_20260930.md)。旧 profile 保留用于显式回溯，不再作为默认值。

- AOSP `android17-release`，产品 `kikiaosp_test_arm64_phone-cp2a-userdebug`，设备 `kikiaosp_test`，系统 `KikiAOSP`。
- Launcher3QuickStep、Settings、SystemUI、三键导航、多任务、通知栏、ADB、虚拟 Ethernet、扬声器和界面音效；设备保持唤醒，不依赖实体 SIM/电池。
- DocumentsUI 文件管理器、Gallery2、Camera2、完整 ThemePicker（壁纸、颜色、主题图标）。ThemePicker 通过设备资源包和 Launcher3 原生 provider 接入，不修改上游主题/桌面源码。
- Surface 前后真实摄像头可切换、方向修正、可拍 JPEG；退出相机释放设备。预览使用 VirGL 显示/合成；采集、传输、色彩转换和 JPEG 仍有 CPU 工作。不是零拷贝或硬件 JPEG 编码。详见 [相机说明](CAMERA_SUPPORT.md)。
- 默认 1003×1556 原生像素、288 dpi、字体缩放 1.5、120 Hz 客体、8 vCPU/4 GiB、SDL 窗口和音频、无 Grab/console；不改宿主显示设定。拉伸窗口仍走真实动态分辨率，不放大旧画布。最低测试预算为短边 864、总像素 1,492,992。
- 默认启用同窗口的原生启动控制台：固定点阵文本 Logo、实时内核/Android/配置日志、真实参数和启动成功摘要，桌面就绪后自动撤下；不是第二个 console 窗口。详见 [BOOT_CONSOLE.md](BOOT_CONSOLE.md)。
- VirGL 加速已实际出图。此前同资源预算的动画 APK 热身后达到 70.36–110.89 FPS；这是 APK 回调帧率，不是面板呈现 FPS。桌面操作仍有端到端延迟，不能宣称已完全跟手。历史诊断见 [GPU_ACCELERATION.md](GPU_ACCELERATION.md)。

## 1. 在 Linux 准备镜像

完整 AOSP 下载、精确 manifest、设备树同步及补丁步骤以 [设备仓库 README](https://github.com/kekeqwq/kikiaosp_test) 为准。当前开发机 `keke@192.168.2.185`，AOSP 为 `~/aosp-master`，两个开发仓库为 `~/projects/kikiaosp_test`、`~/projects/kikiaosp_kernel`。130 不再参与构建；Linux 不运行最终 QEMU 测试。

已应用集成补丁的工作树：

```bash
cd ~/projects/kikiaosp_test
scripts/sync-device-tree.sh ~/aosp-master
scripts/audit-device-tree-profile.sh
scripts/audit-aosp-integration.sh ~/aosp-master
cd ~/aosp-master
source build/envsetup.sh
lunch kikiaosp_test_arm64_phone-cp2a-userdebug
tmux new -s kikiaosp-build
m -j8 systemimage vendorimage
# 构建完成退出当前 tmux 会话；保留 out/ 用于增量构建。
```

首次干净源码才运行 `scripts/apply-aosp-integration.sh`，不要对已有修改的 AOSP 反复打完整补丁。内核按其仓库运行 `nix build`，交付 `result/boot/kernel`。

当前 CP2A 将 product/system_ext 内容打进 system 镜像。启动配对仍保留两个兼容辅助磁盘；不可拿旧磁盘上的 WallpaperPicker2 冒充当前实际运行的 ThemePicker。

## 2. 历史开发回溯：Windows ARM64 构建 QEMU 和摄像头桥接

本节的 `.sh`/`tools` 配方仅供本仓库旧开发基线回溯，**不是终端用户流程**。终端用户直接使用下节的独立 PowerShell 入口，无需克隆本仓库。

安装 MSYS2，在 **CLANGARM64** 终端更新并准备依赖（不是 UCRT64/x86_64）：

```bash
pacman -Syu
# 按 pacman 提示重开终端后完成 pacman -Su。
pacman -S --needed git make ninja python pkgconf \
  mingw-w64-clang-aarch64-clang mingw-w64-clang-aarch64-pkgconf mingw-w64-clang-aarch64-glib2 \
  mingw-w64-clang-aarch64-gtk3 mingw-w64-clang-aarch64-SDL2 \
  mingw-w64-clang-aarch64-libslirp mingw-w64-clang-aarch64-pixman \
  mingw-w64-clang-aarch64-zstd mingw-w64-clang-aarch64-libepoxy \
  mingw-w64-clang-aarch64-virglrenderer mingw-w64-clang-aarch64-cppwinrt
./tools/build_qemu_arm64.sh
```

脚本固定 QEMU 上游 `bde658eef6b38c45794bfd7ad4d2dd1b574e4694`，依次应用 [完整稳定补丁](patches/qemu-kikiaosp-tested-surface-20260929.patch)、[磁盘工具二进制读取补丁](patches/qemu-io-binary-source.patch) 和 [原生启动控制台补丁](patches/qemu-sdl-boot-console.patch)，显式启用 WHPX、SDL、OpenGL、VirGL，并保留 GTK 对照后端。补丁涵盖 Windows WGL、DPI 原生像素、WM_POINTER 触摸、外设键盘、禁止鼠标 Grab、120 Hz、SDL 音频和同窗口启动画面；新增 qemu-io 补丁仅保证 Windows 上从文件写入的磁盘字节不经过文本转换。默认构建目录为 `tools/qemu-boot-src`，并行度8，可用 `KIKI_BUILD_JOBS` 调整。已有脏源码会拒绝覆盖，可传入另一个新源码目录；已打补丁的源码后续直接 `ninja -C tools/qemu-boot-src/build -j8 qemu-system-aarch64.exe qemu-img.exe qemu-io.exe` 增量构建。不要把诊断补丁叠加到默认 EXE。

默认 EXE 为 `tools/qemu-boot-src/build/qemu-system-aarch64.exe`；已测试版本 SHA-256 为 `3004741332643cfd775f83ad990714716ca9975ae341f0355fda7fded4c4a651`。遮罩前的5b92版本留在旧构建目录，只有显式回溯 profile 才使用，不会自动回退到旧文件。重建的二进制可以哈希不同，但须保留能力并回归测试。运行不能只复制 EXE，还需要 ROM、运行库和宿主 OpenGL 驱动。Surface 已验证的宿主驱动为 Mesa D3D12 → Qualcomm Adreno；MSYS2 virglrenderer 是渲染协议库，不自动安装该宿主驱动。启动器把 `C:\msys64\clangarm64\bin` 放入进程 DLL 搜索路径。

PowerShell 7 构建原生摄像头桥接：

```powershell
$env:PATH = 'C:\msys64\clangarm64\bin;' + $env:PATH
.\tools\build_surface_camera_bridge.ps1
```

摄像头桥接源代码和构建脚本都在本仓库。桥接默认输出 `tools/build/surface-camera-bridge.exe`；不采集麦克风、不改 Windows 相机全局配置。

### 发行版使用用户指定的 QEMU

终端用户不需要克隆 KikiEmu，也不需要 `kikiemu/tools` 目录。准备好 Windows ARM64、Git、PowerShell 7 和 MSYS2 后，在 PowerShell 中只做以下步骤（目录可自行选择，QEMU 源码路径不要含空格）：

```powershell
git clone https://gitlab.com/qemu-project/qemu.git ~/Repos/qemu
if ($LASTEXITCODE -ne 0) { throw 'QEMU clone failed.' }
cd ~/Repos/qemu
Invoke-WebRequest 'https://raw.githubusercontent.com/kekeqwq/KikiEmu/main/build.ps1' -OutFile ./build.ps1
./build.ps1 --msys2 'C:\msys64'
```

已经克隆 QEMU 则从 `cd` 开始；把 MSYS2 路径改成自己的安装路径，例如 `C:\msys2`。脚本检查已安装包，只安装缺失依赖，校验并应用固定五份补丁，再开始构建。默认并行度8，支持 `-Jobs 4`；也接受 PowerShell 风格的 `-Msys2`。

建议但不强制使用已验证的 QEMU 提交 `bde658eef6b38c45794bfd7ad4d2dd1b574e4694`，需要时在构建前自行执行 `git checkout --detach bde658eef6b38c45794bfd7ad4d2dd1b574e4694`。其他提交只输出兼容性警告，仍会继续尝试补丁和构建，可能失败；脚本不会替用户切换或锁定提交。增量记录保存实际 HEAD，打过补丁后若换版本则使用新 checkout，不复用旧补丁／构建状态。

产物直接留在 **QEMU 源码目录的 `bin`**，重复运行同一命令可增量构建。`build.ps1` 仅处理依赖、补丁和构建，不移动、导出、清理或启动产物，不更改宿主的持久配置。KikiEmu 的 create／set --qemu／doctor --qemu 自动检查并准备运行依赖，不把部署混进构建脚本。独立英文说明见 [QEMU_BUILD.md](QEMU_BUILD.md)。上面历史开发流程中的 `.sh` 和内部导出工具不属于终端用户流程。

构建标准输出和错误输出实时显示，失败会附带最后的诊断信息。用户已完成 QEMU master `f7ada39e` 原生 ARM64 构建；静态检查不能代替宿主硬件和实际系统回归。

创建时 KikiEmu 检查三个 EXE：缺少时给出重新编译提示；目录已完整时不写任何运行文件；只缺 DLL／ROM 时读取 QEMU 源码根目录 `.kiki-qemu-build/state.json` 记录的 MSYS2 路径，静态检查递归 ARM64 依赖，再补齐私有 DLL 和配套 ROM。请保留编译时的 MSYS2 库版本和源码 pc-bios。已有冲突文件或正在使用的 bin 拒绝覆盖，不自动重编译、不替换 EXE、不启动系统、不改 PATH。完整独立运行目录可以放任意位置，也不需要 MSYS2／构建记录。prepare.ps1 仅留作开发诊断，不再要求用户运行。

0.1 的模块边界：KikiEmu 只做配置管理、系统安装和按配置启动；KikiAOSP ZIP 只包含系统必需文件及配套内核；QEMU 由用户独立构建/提供。以下接口已包含在R8候选安装器中，等待用户安装／公开CLI验收：

```powershell
kikiemu create --system ~/Downloads/KikiAOSP-0.1.0-alpha-arm64.zip --storage ~/MyAndroid --size 200g --qemu ~/Repos/qemu/bin
kikiemu set --id 01 --qemu ~/Tools/KikiQemu-v2/bin
kikiemu --delete --force --id 01
```

也接受 `kikiemu --create ...`、`kikiemu --set ...`。`create` 的 system/storage/size/qemu 四项必填；`--performance` 可选。`g` 表示 GiB；size 为手机总容量，创建后禁止修改。`--qemu` 指 **bin 目录**，不是 EXE 文件；该目录至少包含 `qemu-system-aarch64.exe`、`qemu-img.exe`、`qemu-io.exe`、配套第三方 DLL 和 `roms/`。兼容的路径变更不重装系统、不改用户磁盘，运行中的实例保持原配置，下次启动生效。

删除接口为 `kikiemu --delete --force --id 01`，也接受 `kikiemu delete --force --id 01`。它不再询问确认：核验目标实例和 storage 所有权后，终止仅属于该实例的 QEMU/配套进程，永久删除其 storage 文件夹及全部数据；若为默认实例，同时清除默认设置。必须显式提供 ID 和 `--force`，不允许传入任意删除路径；路径/实例/进程身份不符时拒绝，不会清理其他实例、原始 ZIP 或用户提供的 QEMU。卸载启动器仍默认保留用户系统数据。完整事务与安全约束见 [发行规划](RELEASE_PLAN.md#explicit-destructive-instance-deletion)。该接口已包含在编译好的候选 setup.exe 中；精确所属进程终止、运行中 storage lease 删除、默认注销及失败恢复通过内部检查，公开 CLI/delete 的用户验收仍待完成。

`~/xxx` 和 `~\xxx` 由 KikiEmu 展开为当前用户主目录，尾随分隔符可正常使用；空格路径请加引号。系统包不存在、目录已有数据、运行依赖不完整、路径歧义等报错给出实际目标，不把不存在的文件误报成路径重定向。create 实时输出检查、解包、导入进度、磁盘回读和注册日志；失败不静默覆盖用户已有目录。不要只复制一个 DLL 或用旧 EXE 冒充新构建。

发行配方包含 [通道标题补丁](patches/qemu-sdl-channel-title.patch)：正式版设置 `KIKI_SDL_WINDOW_TITLE=KikiEmu`，无该环境变量时保留开发标题行为。配置管理器要求这个编译能力标记，所以新入口固定应用全部五份补丁，不能用旧开发 EXE 代替。静态检查不能代替完整系统启动或宿主 GPU 兼容性实测。

发行启动将只使用实例保存的规范化 QEMU 路径及已验证文件身份；缺失、不兼容或文件变化时给出英文错误，不搜索 PATH、不回退到旧开发 EXE、不自动换成软件渲染。开发测试使用新建私有 bin/output 与 storage，不操作正式用户实例。KikiEmu 卸载不删除用户提供的 QEMU 或用户磁盘。

安装器、CLI 初始化、PATH/快捷方式和卸载的用户视角验收由用户执行；本轮开发不运行 setup.exe 或修改用户 PATH。构建检查与系统本身的启动/图形/持久化回归由开发侧负责，待候选准备好后交付用户。

### 内部单盘系统原型（不是用户验收入口）

`src/kikiemu` 已包含原生 ARM64 参数/运行目录检查、受限子进程、GPT/动态 QCOW2 安装、boot-v4 缓存、安全 storage 删除、精确进程/端点核验、原子实例记录和失败恢复事务。发布分支新增共用 schema/语义检查、受限 ZIP 读取器及管理器分发：create 安装模块，list、set、info、doctor 和 `--delete --force --id` 已接入；缺少显式删除意图时底层接口同样拒绝。只设置内存/CPU 保留其他已设资源值，QEMU 路径检查失败不提交部分配置。

内部检查只使用新建 TEMP fixtures 和自建测试子进程，不运行公开 CLI 的用户验收、不安装 setup.exe、不改用户 PATH 或真实 Android 数据。控制台管理器和无终端桌面 supervisor 已可编译为 ARM64，并接入 start/stop/logs；setup.exe 也已成功编译为候选安装包，配方见 [INSTALLER.md](INSTALLER.md)，但未执行/安装。干净独立 Android 构建及 format-1 系统 ZIP 生产已经完成，32 GiB 与200 GiB真实新建系统都已启动并核对容量；最新管理器重建回归、完整中断创建恢复、用户安装器验收和正式/Dev 并行系统验收仍未完成，不能把候选当作已验收发行。`kikiemu-disk-prototype.exe` 仍是只供开发者测试**系统磁盘**的入口。

运行管理源码为每次启动建立独立会话 UUID、ADB/QMP/相机回环端口和日志目录；进程先暂停创建、登记精确身份后才执行。正式窗口标题为 `KikiEmu`，开发计划为 `QEMU/Dev`，控制和删除不依赖标题。私有 ADB wire 直接连登记的客机，不使用全局 5037 服务、默认设备或宿主 ADB key；单次命令接口为 `kikiemu adb --id 01 --shell "getprop ro.serialno"`，暂不提供交互式 shell。原生启动配方沿用 SDL/VirGL/120 Hz、8 CPU/4 GiB、1003×1556、不 Grab 的基线，不对宿主显示/键盘/音频设定作修改。

强制删除已额外验证运行中的 supervisor 持有真实 storage lease 的情况：只读核验和锁定原目录身份，停止该实例后才升级删除权限；替换路径或未释放锁仍拒绝。当前 306 项隔离内部检查包括进程/端口隔离、私有 shell-v2 的分段数据/校验/超时/错误身份和持锁实例删除，以及安装运行期互斥、完整 PATH 精确分段/去重/长度检查、真实 HOME 就绪解析和专用系统测试目录隔离；额外传入 producer 非可启动 ZIP fixture 时另有一项交叉读取检查。真实 Android 的 adbd 报告最高支持版本；客户端按 [AOSP 协商逻辑](https://android.googlesource.com/platform/packages/modules/adb/+/HEAD/adb.cpp) 接受已知版本，但仍广告旧版并严格核验所有包的校验和，拒绝 AUTH/TLS/未知协议和错误实例，不使用全局 ADB 服务。所有安装入口持有 `install.lock` 共享 lease，setup/卸载必须先取得独占 lease，不能覆盖运行中的启动器，也不自动杀实例；卸载保留实例记录与磁盘。公开安装器/CLI 的最终用户验收仍由用户执行。

本轮独立干净产物的身份、校验值与已取得的真实系统证据见 [候选测试记录](CANDIDATE_TEST_20261001.md)。该记录区分候选包、实际系统验证和仍未完成的发版门槛，不用旧开发磁盘替代干净系统包。

正常关机的进程存活检查使用同一个持有的Windows进程句柄，避免进程恰好退出时映像查询失败被误报。仅映像查询异常时对同一个句柄有界等待最多1000ms，并且只有该对象实际发出退出信号才视作退出；等待超时、仍活着的创建时间/路径/哈希错误及查询权限失败继续拒绝，不能按名字或复用PID绕过检查。行为依据 [Windows进程对象与退出信号](https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-process)；十个确定性状态转换检查和已有真实受控子进程检查覆盖此逻辑。R8的32GiB新系统启动及持久化重启均正常关机成功，200GiB新系统正常关机后也已在原始ZIP路径不可用时持久化重启，具体证据以候选记录为准。

共用合同从设备仓库 commit `5cfd0a94c1b267661d2ae96bdc3875365ad686cf` 固定引入，见 [PIN.json](src/kikiemu/contracts/PIN.json)；构建时核对三个 JSON 文件的 SHA-256，再嵌入本机程序。ZIP/ZIP64 只允许规定文件、store/deflate、普通文件及一致的 local/central 名称；拒绝 SFX、重复/截断/NUL/路径跳转、隐藏附加文件、未知角色、开发身份或错误 ABI。长度、哈希、AOSP XML 精确项目提交、4K ARM64 boot/gzip 和 raw EROFS 再作语义核验，禁止执行包内代码。这里的测试 ZIP 是明确标记的非可启动假数据，绝不是用户需要的系统包。

原生组件构建依赖 MSYS2 CLANGARM64 的 clang、windres、nlohmann-json、libarchive、expat 及其依赖；运行 `tools/build_kikiemu_core.ps1 -RunUnitTests`。共享核心编译一次供两个产品入口和四个内部工具复用；产品入口嵌入原图标、版本、非提权和 DPI/长路径 manifest。libarchive/expat 静态链接；MSYS2 的 libarchive 仍导入 zlib，构建脚本将原生 `zlib1.dll` 私有复制在 EXE 旁，不依赖用户 MSYS2/PATH。相机桥接单独由 `tools/build_surface_camera_bridge.ps1` 构建，使用 Windows 官方 OneCore 导入库和静态 C++ runtime；只在实际打开相机时持有跨实例占用锁，关闭相机后释放。最终发行必须补齐静态/动态依赖的原许可和源码义务审计。

候选打包现在要求 core/相机各自的完成构建凭据：冻结 Git 提交、追踪输入哈希、干净状态、编译器与产物身份、core 内部检查记录；没有凭据、版本不符、失败构建残留、dirty source 或替换过的 EXE 均拒绝。自身源码继续 GPL-2.0-or-later，组合 manager/desktop 二进制选择 GPLv3（包含 Apache-2.0 OpenSSL），不统一重标第三方，详见 [许可选择与待完成的源码义务](LICENSE_NOTICE.md)。这不是已完成全部第三方合规审计的声明。

开发侧新增 [干净包真实系统测试入口与证据清单](tests/SYSTEM_REGRESSION.md)：`kikiemu-system-regression.exe` 只在新建、独立所有权的内部目录读取真正的发行 ZIP，复用生产 reader/磁盘/supervisor/private ADB 完整配方，支持实际系统启动、限定 shell、正常关机和持久化复测，不调用公开 CLI/desktop，不触碰用户安装的 registry/PATH。该工具不进 setup.exe；编译或内部 fixture 检查通过不等于实际系统启动成功，更不冒充用户安装器验收。

32 GiB 原型磁盘启动前约占1.16 GiB；GPT、boot/system/vendor 逐块读回校验及 QCOW2 检查通过，没有 backing file 或旧 userdata。首次系统启动发现上游 init 的 boot UUID 分类漏掉 virtio-blk，已在设备仓库追踪最小修补并重建。修正后单盘实际启动进入 SDL/VirGL/120Hz 桌面，剩余容量首次格式化为 F2FS，跨正常关机/重启的数据标记保持不变。

2026-10-01修复并实测容量显示：KikiAOSP专用开关让StorageManager/StorageStats采用完整块盘真实字节数，绕过实体手机营销容量档位；设置页也不再虚构最少1 GiB临时文件。新建32与200 GiB磁盘均实测API一致、F2FS实际容量正确，宿主QCOW2文件初次启动后约1.29 GB（十进制），没有预分配全部容量。32 GiB设置页为34 GB总计/约1.8 GB已用，Android17约1.3 GB、临时文件约518 MB；200 GiB为215 GB总计/约2.6 GB已用，Android17仍约1.3 GB。差别来自GB/GiB换算和实际文件系统开销，不是写死系统占用。数据和启动测试的具体边界见设备仓库[GPT存储验证记录](https://github.com/kekeqwq/kikiaosp_test/blob/feat/release-0_1-alpha/docs/GPT_STORAGE_PROTOTYPE.md)。这些仍是开发系统原型，不是干净发行包或公开CLI/安装器验收。

`tools/check_gpt_guest_storage.ps1` 是只读系统检查工具：必须显式指定 QEMU PID/EXE/实例路径，核对进程创建身份与 ADB端点所有权后，比较GPT记录、内核完整盘容量、vold及StorageStats两个接口、F2FS容量；不修改用户数据，不调用用户CLI初始化。真实Settings页面与持久化仍需分别观察。

历史低层入口提供 `PhoneDisk` / `BootPartitionUuid` / `RamdiskImage`，显式 GPT 模式只接入该独立磁盘、禁止 legacy overlay 混用且不加 `-snapshot`；不传这些参数时原多盘回溯基线不变。该入口仍使用旧开发端点，只用于显式回溯；未来修补按上面的同格式包／KikiEmu实例路线推进。

启动器图标主图及七尺寸 Windows ICO 已保存到 [assets](assets/README.md)，由内置 image_gen 按原创女孩抱机器人 brief 生成，完整 prompt 与转换步骤一并存档，已接入R8程序和安装器资源。

## 3. 收集、校验并打包（现有开发基线）

Windows 需要 PowerShell 7、OpenSSH、支持 zstd 的 `tar`、Android `adb`，以及免密访问构建机。预留至少 12 GiB 本地空间：

```powershell
ssh keke@192.168.2.185 'git -C ~/projects/kikiaosp_test status --short'
.\tools\collect_kikiaosp_assets.ps1
```

收集器验证设备/内核 Git 历史，取得 profile 对应资产，逐项校验字节数和 SHA-256，然后验证辅助压缩包的精确文件列表及解出的六个文件。输出 `bundles/surface-main-20260930/` 与 `bundle-profile.json`，已有输出目录会拒绝覆盖。可传 `-OutputDir`、`-AospHost`、`-KernelHost`、各远端目录或 `-LocalSupportArchive`。历史 profile 必须显式传 `-ProfilePath`。

本次已验收的 system/vendor 和兼容 product/system_ext 固定存于 185 设备仓库的 `output/`，不再依赖会随下一次构建变化的 `out/`。`sourceRepository: device` 指向该目录；未指定时仍兼容旧清单的 AOSP 输出路径。

镜像、QEMU 源码/二进制、运行依赖和辅助压缩包均不纳入 Git。辅助包 `kikiaosp-runtime-support-20260926.tar.zst` 只有约 21 MiB，但包含 8 GiB 基础 userdata、冻结 ramdisk、misc 与空辅助盘；它不是本次 `m systemimage vendorimage` 自动产物。复现上述冻结基线仍须迁移这些旧资产。发行分支另有设备侧 [源码生成 boot/initramfs 的原型](https://github.com/kekeqwq/kikiaosp_test/blob/feat/release-0_1-alpha/docs/BOOT_PAYLOAD.md)，已在同组开发系统镜像下实测启动；它不重建旧 ramdisk 的同一哈希，不证明新 GPT/新 userdata 或干净发行镜像已完成。

## 4. 历史 bundle 的启动、检查和退出

```powershell
.\tools\run_kikiaosp_local.ps1 -ValidateAllAssets
adb connect 127.0.0.1:5555
adb shell getprop sys.boot_completed
adb shell dumpsys SurfaceFlinger | Select-String 'GLES:'
adb shell pm path com.android.wallpaper
adb shell uname -a
```

正常启动每次校验所有文件长度和 kernel/system/vendor 的哈希；`-ValidateAllAssets` 额外检查全部辅助盘。QEMU 另检查已编译的 SDL 能力标记并打印 EXE 路径及哈希，避免巨大旧 GTK 窗口、触摸和 Grab 回归。当前 profile 默认开启启动遮罩，可用 `-BootConsole:$false` 临时禁用；旧 profile 没有此标记则不开启。可用 `-DryRun` 只生成命令，用 `-SurfaceCameras:$false` 关闭本次相机桥接。`run_kikiaosp_touch_local.ps1` 为显式参数 A/B 测试底层入口，不代替清单校验。

QEMU 在后台无控制台运行。监控仅监听 `127.0.0.1:4447`，ADB `127.0.0.1:5555`，相机桥接 `127.0.0.1:4455`。默认 snapshot 不写回基础磁盘：照片和应用数据需要保留时，关闭前先 `adb pull`。持久磁盘须显式使用独立 qcow2 overlay，不允许直接写基础 raw 镜像。日志保存在 bundle。

```powershell
.\tools\capture_qemu_window.ps1 -KeepWindowed -Tag check
# 将 <PID> 换为启动器打印的实际 QEMU_PID。
.\tools\stop_kikiaosp_local.ps1 -QemuProcessId <PID>
```

截图采用 DPI-aware Windows 整桌面 2880×1920，保存于 `~/Downloads/temp`；测试期间保持正常窗口，先看启动日志再截图。退出工具先停止相机，再通过 QEMU monitor 正常退出并等待进程结束，不盲杀其他进程。照片、截图及原始诊断日志可能含私人内容，不能提交公开仓库。

## 开发规则

终端用户英文初始化/管理/删除/卸载说明见 [QUICK_START.md](QUICK_START.md)。发行安装器的构建、原生 ARM64 程序边界、运行中拒绝覆盖和保留用户数据的卸载约束见 [INSTALLER.md](INSTALLER.md)。只编译安装包、不执行它；setup.exe 与公开 CLI 的用户视角验收由用户进行。系统源码的独立发行构建同时在设备仓库推进，不能用开发资产代替干净系统 ZIP。

0.1 Alpha 的 R8 setup.exe 和真正独立干净构建的系统 ZIP 已生成，系统已在32／200 GiB新盘启动并持久化重启；它们仍是待用户安装器验收的候选，不是已公开发版。见 [RELEASE_PLAN.md](RELEASE_PLAN.md) 和 [候选记录](CANDIDATE_TEST_20261001.md)。系统包仅交付干净安装材料，不包含用户磁盘；客户端新建固定总容量、动态占用的持久化磁盘。现有冻结 bundle/collector 只用于历史回溯，不能作为新版本发版输入。包合同由 kikiaosp_test 维护；后续版本继续使用相同格式及双方校验，不因0.2／0.3改文件形式。QEMU 通过 create/set --qemu 显式指定并校验，不强制内置。独立 Dev 通道与严格并行隔离门槛已按用户2026-10-01的新要求取消，不删除已实现的基本实例控制安全校验。

原生 SDL 启动控制台已通过实际启动和用户验收，纳入默认主线；Android/内核资产不变。遮罩前的冻结基线见 [surface-main-pre-boot-console-20260930.json](profiles/surface-main-pre-boot-console-20260930.json)，仅用于显式回溯。构建、状态判断和证据边界见 [BOOT_CONSOLE.md](BOOT_CONSOLE.md)。

三个仓库分别维护职责内的源码，功能另开分支、审计、增量构建、本地实测后再合并 main。新镜像必须新建 profile，保留旧冻结配对，不在旧文件名下覆盖新内容。当前默认入口和文档只指向已验收主线；历史帧率/回调边界、限制和失败记录不能被改写成新版本的保证。
