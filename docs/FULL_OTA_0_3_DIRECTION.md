# 0.3 全量 OTA：新基线与入口讨论

状态：**已开始实际实现普通 A/B、Kiki 更新客户端和共享原生 CLI；候选构建/真实 OTA 回归尚未完成，不发版。**
实现与当前边界见 [原生 OTA 实施说明](NATIVE_OTA_0_3.md)。
此前 [0.2 基底增量方案](INCREMENTAL_OTA_DESIGN.md) 保留作历史，不再作为实施目标。

## 已确定的目标

- 用户明确不要求保留/迁移 0.2 数据，允许 0.3 重做磁盘和启动基线。
- 不操作或删除用户现有 0.2 实例；0.3 新基线在独立实例中初始化。
- 从这个 0.3 基线起，后续全量 OTA 更新原实例，保留应用、设置和 userdata，不要求每次新建实例。
- 每个目标版本提供通用全量 OTA，而不是要求维护所有源/目标版本组合的差分。
  仍须满足 device/架构、磁盘布局容量、签名/防降级和数据迁移兼容条件，不能承诺无限跨版/无限增长。
- 音频 HAL 保持已验收策略，短暂破音为已知问题；保留已验证的 Linux 7.3-rc6 来源。

## 推荐：设置为主、CLI 为辅，同一个原生引擎

```text
Android 设置 -> Kiki 系统更新客户端 ---+
                                     +-> update_engine -> 非活动系统槽
KikiEmu CLI -> 实例专属受控接口 -------+                   -> 校验/待重启
                                                           -> 启动选择/成功标记/失败回退
```

### 设置页：日常更新入口（建议）

- 检查更新、版本说明、下载进度、暂停/失败状态、安装进度、“重启完成更新”。
- 可以增加本地 OTA 文件选择；用户无需打开 Windows 终端。
- 更新任务归后台服务/update_engine，不依赖页面持续打开。
- 不未经同意重启，也不修改宿主音频、PATH、公共 ADB 或用户正式实例。

### CLI：离线、自动化与恢复入口（建议）

- 保留 `kikiemu update --id 01 --ota ~/Downloads/0.6.ota.zip` 的离线包入口。
- 通过本实例受控接口提交到相同 update_engine；不另写一套绕过校验的宿主刷分区算法。
- 状态查询、日志、待重启提示与设置页一致；实例内更新互斥，不能同时发起两次更新。
- guest 无法启动时，CLI 的恢复能力另行设计为选择已验证备用槽/受信恢复环境。
  **不能声称 Android 起不来时还可调用它的 update_engine**，不能靠重建 userdata 解决系统启动失败。

## 已核对：当前设置条目不是完整 OTA 客户端

实际 Android 17 Settings 源码：

- `packages/apps/Settings/res/xml/system_dashboard_fragment.xml` 使用 `android.settings.SYSTEM_UPDATE_SETTINGS` intent。
- `.../system/SystemUpdateRepository.kt` 用 `MATCH_SYSTEM_ONLY` 查找系统更新 Activity。
- `.../system/SystemUpdatePreferenceController.kt` 未找到 Activity 时移除条目，更新摘要来自 `SystemUpdateManager`。

因此 Settings 提供的是入口/状态展示接口，不是一个已经包含 Kiki 更新源、下载、签名、安装和回滚的通用程序。
需要提供系统级 Kiki 更新客户端并实现该 intent；无需把 Google 的专有 updater/GMS 当成前提。
当前实际 installed-files 没有匹配的 update_engine/update_verifier/boot-control 服务可执行文件；
存在 `libboot_control_client.so` 或 `otacerts.zip` 不表示已有完整 A/B 能力。

## 0.3 基线必须补齐的核心，而非只接两个按钮

1. A/B 启动及系统布局、合理增长预算、共享 userdata；或完整 Virtual A/B 动态分区/快照方案。
   普通 A/B 与 Virtual A/B 的选择仍需设计评估；包为全量与槽机制是两个独立问题。
2. 原生 update_engine、boot_control、成功标记/有限启动尝试、失败回退，及标准签名 target-files/OTA 管线。
3. 启动器必须加载所选槽的真实 boot/kernel/ramdisk。现有 `session.cpp` 固定 direct-boot cache，
   guest 内重启不能被假定为自动加载新槽内核；需实现可信启动选择及宿主/guest 重启闭环。
4. 启动前/启动时系统完整性校验及密钥策略；不能以现有 permissive/orange 启动参数冒称 Verified Boot 已实现。
5. 断电/宿主退出/CLI退出、下载损坏、错误签名、安装失败、新槽无法启动、跨版数据保留的真实回归。
6. A/B 默认仅回退系统槽，**不会自动回退 userdata 数据库迁移**。必须另定数据兼容、备份及失败回退策略。

实际 rc6 `.config` 已有 `CONFIG_BLK_DEV_DM=y`、`CONFIG_DM_SNAPSHOT=y`，未找到启用的 `CONFIG_DM_USER`。
不能拿 Nix 的声明覆盖项或 Android 版本号当 Virtual A/B 压缩快照已经可用的证据。

讨论结论建议：**日常使用优先设置页，CLI 作为同引擎的离线和救援补充**。
不是二选一，也不是两套互不相通的更新实现。“完美 OTA”应转化为可验证的安全/数据保留/恢复门禁，不能预先承诺零风险。
