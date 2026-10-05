# 0.3 原生全量 OTA（用户授权工程实测发版）

## 新基线

选择普通物理 A/B，不使用自定义差分刷盘器或 Virtual A/B 快照合并。
`formatVersion=2` / `gpt-ab-v1`：每槽 boot 64 MiB、system 4 GiB、vendor 512 MiB；
共享 misc 4 MiB 和 F2FS userdata（至少 8 GiB）。正常 OTA 仅包含 boot/system/vendor。
0.2 不迁移；现有实例和已发布资产不改动。

宿主作为虚拟 bootloader，读取 AOSP `bootloader_control` 的 CRC、优先级、剩余尝试和成功位。
每次启动从所选实际 boot 分区提取新的、会话专属的 kernel/ramdisk，而非复用固定旧缓存。
新槽须通过 Android/HOME/显示/电源门禁才调用原生 `bootctl mark-boot-successful`。
更新重启先正常卸载/关机，再冷启动选槽；失败槽回退不能格式化共享 userdata。
初次 NEW 实例格式化有专属 token，普通后续启动由 fs_mgr no-wipe 门禁拒绝 OTA 隐式清数据。

## 前端和发现

系统应用 `com.kiki.updater` 处理 Settings 的 `android.settings.SYSTEM_UPDATE_SETTINGS`。
设置只是入口；KikiUpdater 通过 GitHub 官方系统仓库 Releases 发现兼容目标：
`https://github.com/kekeqwq/kikiaosp_test/releases`，包括 Alpha prerelease，跳过 draft。
用户不选择源版本或源→目标组合。

每个适用 Release 附件：

- `KikiAOSP-ota.json`：已签名目标目录，含 device、layout、Android major、单调序列、完整 payload 摘要/长度和下载地址。
- `KikiAOSP-ota.sig`：发布者 RSA/SHA256 签名。
- 目录指定的 `*.ota.zip`：原生 Android signed FULL `payload.bin` 和属性，以及内置签名目录。

GitHub metadata/HTTPS/SHA256 不替代发布者认证。客户端验证内置公钥签名、设备、布局、Android major、序列和完整 payload；
原生 update_engine 再验证 payload metadata/内容签名并只写备用槽。最高兼容已签名序列决定目标。
损坏/无法验证的发布信息不能被报告为“已经最新”。当前目录协议限定 Android 17 和此布局；不是无限跨 Android 版本保证。

CLI `update --id ID --action status|check|reboot`，离线辅助
`update --id ID --action apply --package SIGNED_FULL_OTA_ZIP`，通过专属实例端点和受保护 receiver 使用同一 KikiUpdater/update_engine。
不启动全局 adb server，不走第二套刷盘算法。离线输入先有界传至 NEW inbox，再交由系统客户端认证；不信任包内可执行程序。
持久 private stage 支持原生引擎中断续装，成功新槽启动后才确认目标序列。

## 信任和构建边界

独立发布者 OTA key 不进入 Git/源码包；公钥和证书进入可审计设备源。
原生引擎使用专属 `system/etc/update_engine/kiki-otacerts.zip`；缺失该证书库必须失败，不能回退成允许 unsigned。
不为了 OTA 改无关 APK/platform 签名身份。Android 的 public testkey 不是 Kiki 原生 OTA 的授权发布者。
`package-native-ab.py` 从实际审计的 release 构建和 matching target-files 生成全量 signed payload，检查实际系统属性、证书库和三分区集合。
内核保留此前已审计、实际启动的 rc6 Image；Android kernel/target-files 有独立生成输入记录，不向 device 源添加私钥或二进制。
Nix GC 后恢复同一 derivation 的 Image 与原字节不同；构建记录明确 `byteReproducible: false`，
通过精确历史包/source-lock/boot 和 retained Image 收据核验原产物，不把重建结果冒称原字节或偷偷替换。
源码包从实际 shipped Image 提取内嵌配置，分别保留实际配置与声明输入。
自有 7.3 kernel matrix 检查实际 built-in 驱动并声明 shipping FCM 202604；完整 VINTF 检查通过，未使用 skip-compatibility。
这不是官方 GKI/LTS/VTS 认证。

**当前仍是 userdebug / unlocked direct boot，不声称 AVB Verified Boot 或抵御宿主/guest root 篡改。**
A/B 回退系统槽不自动回退数据库迁移，发行方须验证数据兼容，不承诺任意大版本跨越或任意容量增长。
此前 NONRELEASE 目标仅用于私有回归，未创建公开测试 Release。现获用户 0.3 发版授权：提升实际测试 sequence4 的目录/下载地址为发布版，保留原已签名 payload 和镜像字节。

## 验收状态

宿主实际编译通过 344 个内部 core checks。NEW format-2 候选已真实 READY，Settings 与 CLI 共用原生引擎。
同一个 32 GiB 实例已实际安装 publisher-signed FULL OTA：NONRELEASE sequence 1→2→3→4，
冷重启槽 A→B→A→B，新槽 successful、目标序列确认、pending/stage 清理；未重建实例或格式化 userdata。
测试 APK 字节/UID、应用私有数据、文件、font_scale、旋转设置和自定义设置跨真实升级、安装中断、系统槽回退和恢复升级保持。
前置认证坏签名、已签名错误 device/layout 和 downgrade 已拒绝；不把这些当 native payload-signature 负向验收。

更新页面、通知及错误为英文。实际用户发现导航栏挡住按钮、浅色状态栏图标不可读后，
已通过真实 OTA 安装系统 APK 修正：WindowInsets 安全区、可滚动信息区、固定安全按钮区、系统 DayNight 配色及明暗系统栏图标。
浅色和深色均有实际截图/Activity view bounds 验证；最后按钮底部 1441px，小于导航栏顶部 1470px。
已恢复原浅色模式，留下运行中的候选供人工确认，没有 updater APK overlay 冒充系统升级。

后续真实门禁已通过：

- 原生 metadata signature 单字节损坏，外层目录仍有有效 publisher 签名且 SHA/长度正确；update_engine 使用专属证书库实际拒绝，错误 26。
- 真实低空间：写入生成填充文件、确认剩余约 1.1 GiB，private 第二份持久复制 ENOSPC；未 ACK、inbox 保留、private stage 清理，没有提交引擎或清 userdata。
  早期 harness 的 Android mksh 32 位容量算术溢出已纠正，失败尝试不计 PASS。
- 安装进行中约 9% 使用原生 suspend 注入中断，再正常关机冷重启；真实引擎从 59/513 operations / 70,264,712 bytes 续装，不是从头安装或重建盘。
- 活跃事务重复提交拒绝，原 pending/stage 不丢失；已接受序列重复/降级拒绝。
- 已安装但尚未启动的备用槽 boot header 故障注入：宿主拒绝损坏 boot、标坏 B 槽并实际回退 A，rollback=1，用户数据保持、pending 清理。
  之后重新提交同一有效 sequence 4 FULL，由原生引擎修复备用槽并成功启动 B；没有重置 acceptedSequence。
- 两槽 priority 都不可启动：宿主在启动 QEMU 前拒绝且保盘。仅还原测试前备份的标准 BCB 512B、读回验证，原实例及数据恢复；此恢复是开发测试撤销，不是对外第二套 OTA flasher。

仍未实际验收：断电/强制宿主崩溃、两个 boot image 同时损坏、userdata mount-failure no-wipe 故障。
GitHub 自动检查实际无新已签名候选路径通过；未创建公开测试 Release，正向在线自动下载路径未实际验收。
签名/打包/VINTF、真实安装、UI 显示与用户人工确认分别记录；不把运行候选当全部发布门禁通过。
用户现已授权 main/tag/0.3 Release，依据工程实测发版；用户端真实在线升级人工验收明确留待未来 0.4，不冒称已通过。setup.exe 仍仅编译，未由 agent 执行。发布说明见 [RELEASE_0_3_ALPHA.md](../RELEASE_0_3_ALPHA.md)。
