# KikiEmu 📱⚡

**极轻量、极简风格的 Windows on ARM (Snapdragon X Elite) 原生 Android 17+ 模拟器**

KikiEmu 专为搭载高通骁龙（Snapdragon X Elite / X Plus）处理器与高刷触控屏的 **Microsoft Surface Pro 11** 及 Windows on ARM 设备量身打造。

通过 Windows Hypervisor Platform (WHPX) 实现宿主机与客机 **AArch64 指令集 1:1 直通裸机执行**，零指令转译开销，带来媲美甚至超越物理手机的流畅度与能效比。

---

## ✨ 核心特性

- 🚀 **纯原生 ARM64-on-ARM64 直通**：
  不依赖任何 x86 转译层（无 Houdini / Prism 损耗），Android 17 原生动态库直接在高通 Oryon 核心上全速运行。
- 📦 **专为 Android 17 与未来版本设计**：
  内置 Google 官方 Google Play ARM64 系统镜像源，全面支持 Android 17.0 (API 37) 以及最新的 16KB Page Size (ps16k) 预览版与 Canary 测试版。
- 🖥️ **双模态桌面启动器 (GUI)**：
  - **默认竖屏手机模式**：极简手机视口（9:19.5 现代比例），Fluent 风格。
  - **平板无边框全屏模式**：一键（`F11`）铺满 Surface Pro 11 的 2880x1920 (3:2) 屏幕，沉浸式体验大屏 Android 平板。
- ⚡ **Surface 120Hz 高刷与 10 点触控**：
  原生 Direct3D 12 渲染通道，垂直同步锁定 120Hz；完整支持屏幕十点触控与 Surface Slim Pen 手写笔。
- ⌨️ **物理外设键盘直通（非键鼠映射）**：
  向客机内核虚拟化标准 USB HID 物理键盘外设。Android 系统与常用 App（如外设控制软件、办公工具）能够直接检测到“键盘设置：已连接”，获得真实外接键盘输入体验，无需任何屏幕触控映射层。
- 🛠️ **职责解耦设计**：
  - **CLI 管理器 (`kikiemu`)**：负责镜像拉取、版本更新、实例创建/销毁、存储与缓存管理。
  - **桌面启动器 (`KikiEmu.UI`)**：零配置、零管理逻辑，双击即启动默认实例。

---

## 🏗️ 架构概览

```
+--------------------------------------------------------------------------+
|                              KikiEmu 体系                                 |
+------------------------------------+-------------------------------------+
|    CLI 管理器 (kikiemu.exe)         |    桌面启动器 (KikiEmu.UI.exe)       |
|    - 镜像下载与版本更新在线查询      |    - 纯启动与呈现，零管理开销        |
|    - 存储目录装配与 userdata 初始化 |    - 自动拉起默认配置实例            |
|    - 实例元数据管理 (config.json)   |    - 手机竖屏 / 3:2 平板全屏 (F11)  |
|    - 临时下载缓存清理               |    - 120Hz V-Sync、十点触控与外设键盘 |
+------------------------------------+-------------------------------------+
                                      |
                                      v
+--------------------------------------------------------------------------+
|                  底层轻量级虚拟化引擎 (KikiEmu-Engine)                    |
|   - 原生 Windows ARM64 WHPX 虚拟化驱动 (骁龙 Oryon 核心)                  |
|   - Direct3D 12 / Adreno 741 GPU 硬件渲染通道                            |
|   - 物理 USB HID 键盘外设直接直通                                         |
+--------------------------------------------------------------------------+
                                      |
                                      v
+--------------------------------------------------------------------------+
|             客机系统：Android 17 (API 37) + Google Play 官方服务          |
|   - 4KB 标准版 / 16KB 极速分页版 (ps16k)                                  |
|   - 用户数据完全隔离于指定存储目录                                        |
+--------------------------------------------------------------------------+
```

---

## 🚀 命令行管理器 (CLI) 指南

### 1. 查询可安装系统镜像与构建日期

```bash
# 查看支持的 Android 17+ 镜像列表
kikiemu list --system

# 从 Google 官方仓库在线更新最新镜像构建与发布日期
kikiemu list --system --update
```

输出示例：
```text
System ID        Version  Page   Build / Release Date     Status       Display Name
-----------------------------------------------------------------------------------------------
android17        17.0     4KB    2026-07-27 20:53 UTC     [Online]     Android 17.0 (API 37) Google Play
android17-16k    17.2     16KB   2026-08-28 22:00 UTC     [Online]     Android 17.2 (API 37) Google Play (16KB Page)
android-canary   Canary   16KB   2026-08-07 17:27 UTC     [Online]     Android Canary Google Play (16KB Page)
```

### 2. 创建新 Android 机器

```bash
# 格式：kikiemu create --storage <存储路径> --system <系统ID> --id <实例ID>
kikiemu create --storage ~/myandroid --system android17 --id 1
```
* 系统镜像 zip 下载包会自动存入全局缓存目录 `%USERPROFILE%\.kikiemu\cache`。
* 启动所需的解压系统文件与 `userdata.img` 将存入你指定的存储目录。

### 3. 设置默认启动实例

```bash
kikiemu set --default 1
```
设置后，双击桌面上的 `KikiEmu.UI` 主程序即会自动启动该机器。

### 4. 列出已创建实例

```bash
kikiemu list
```

### 5. 调试启动（命令行方式）

```bash
kikiemu run --id 1
# 或直接启动默认实例：
kikiemu run
```

### 6. 清理缓存与删除实例

```bash
# 清空临时下载缓存归档，释放磁盘空间
kikiemu delete --cache

# 删除指定机器（会弹出确认提示，确认后安全删除存储目录与配置文件）
kikiemu delete 1

# 免确认删除：
kikiemu delete 1 -y
```

---

## 💻 桌面启动器 (GUI) 使用说明

1. 在终端完成 `kikiemu create` 与 `kikiemu set --default` 后；
2. 双击打开 `KikiEmu.UI.exe`；
3. 默认以竖屏手机窗口展现；
4. 按键盘 **`F11`** 或点击右上角全屏按钮，瞬间进入 **Surface 3:2 平板全屏模式**；
5. 外接物理键盘（Surface 键盘盖或蓝牙键盘）会自动作为硬件外设直通给 Android 系统，App 内部可直接使用其快捷键与外设控制功能。

---

## 🛠️ 编译与开发

### 环境要求
- 搭载 Windows 11 ARM64 的设备（推荐 Snapdragon X Elite / Plus，如 Surface Pro 11）
- .NET 9.0 / 11.0 SDK (ARM64)
- 启用 Windows Hypervisor Platform (WHPX) 功能

### 编译指令

```powershell
# 还原依赖并编译所有组件
dotnet build

# 发布 CLI 单文件工具
dotnet publish src/KikiEmu.Cli/KikiEmu.Cli.csproj -c Release -r win-arm64 --self-contained true

# 发布 GUI 桌面启动器
dotnet publish src/KikiEmu.UI/KikiEmu.UI.csproj -c Release -r win-arm64 --self-contained true
```

---

## 📄 开源许可证

本项目遵循 [MIT License](LICENSE)。
