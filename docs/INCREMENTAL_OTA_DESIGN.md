# KikiEmu 增量系统更新设计（0.2 基底）

状态：**设计提案，不是已实现的 `update` 命令或已可安装的 OTA**。暂不发版。
音频修复按用户验收保留：100 分钟/250 个音频未失声，约 1% 短暂破音为已知问题；本轮不继续修改 HAL。
Linux 7.3-rc6 独立构建/实例验证记录放在忽略的 `build/alpha-0.3-rc6/`。

## 1. 结论

第一阶段采用 **Windows 宿主机离线、块级增量、完整磁盘代际事务**。
借鉴 Android A/B 的源版本校验、非活动目标写入、目标全量哈希、试启动与成功标记，
但**不是 Android 原生 A/B / Virtual A/B，也不兼容原生 `payload.bin`**。

- 下载的是小差分，不是整个 qcow2，不带 userdata。
- 先保留旧系统和升级前数据，在独立的新 qcow2 上升级；禁止原位改活动盘。
- kernel、ramdisk、system、vendor、目标身份记录作为一个整体切换。
- 第一阶段不改变 GPT、分区 UUID、userdata 位置/容量及用户选择的 TOTAL GiB。
- 没有通过验证的新代际绝不成为正式运行代际。断电或失败不要求重新导入系统。
- 0.2 老实例不需要先在 Android 内装 updater；但 **0.2 启动器没有 update 命令，必须先安装支持该协议的新启动器**。
  这是独立的 Windows 软件升级，不冒称“旧 setup 已经支持 OTA”。QEMU 不因 OTA 自动升级。

## 2. 为什么不直接打开 Android Virtual A/B

现有 `gpt-v1` 只有 `boot/system/vendor/misc/userdata`，没有双 slot、super、动态分区、
与 slot 选择联动的 bootloader。宿主直接加载磁盘派生的 kernel/ramdisk；guest fstab 使用 by-name EROFS/F2FS。
仅加入 `update_engine` 或 `AB_OTA_UPDATER` 不会补齐 boot_control、快照、启动选择及回滚。

Virtual A/B 还需要 dynamic partitions、snapuserd、内核接口、init/SELinux 转换与断电可恢复的 merge。
这些改造不应混进第一次 0.2 老盘更新；不能照搬旧 recovery OTA 的包内可执行脚本。
宿主机第一阶段全程不执行 OTA 内的 EXE、脚本或任意 postinstall。
未来若做真正 Virtual A/B，应单独设计新布局、旧实例迁移和原生 releasetools/target-files 管线。

## 3. 0.2→0.3 / 0.2→0.5 的包策略

差分绑定 **确切源镜像**，不是仅匹配版本字符串。

```text
发布 0.3：0.2 -> 0.3
发布 0.4：0.2 -> 0.4，0.3 -> 0.4
发布 0.5：0.2 -> 0.5，0.3 -> 0.5，0.4 -> 0.5
```

0.2 是本系列的长期基底。每个支持的源版本对应独立差分包，文件名明确源/目标：
`KikiAOSP-0.2.0-alpha-to-0.3.0-alpha.ota.zip`。
用户示例中的 `0.3alpha.ota.zip` 可以作为重命名后的路径，身份以签名 manifest 为准。
0.2→0.5 直接用 0.2 和 0.5 两份实际产物生成/验收，不必串行安装 0.3、0.4。
**0.4→0.5 的差分绝不能用到 0.2**。不靠网络自动找包、不隐式链式更新。

可另做含多条源分支的统一包，但通常比单源包大；第一版不用这一复杂度。
跨越 Android 大版本、userdata 数据模型或文件系统特性变更时，必须有直达迁移测试。
无兼容证据则明确拒绝并给出所需的中间版本，不能把“能重建镜像”说成“数据一定兼容”。
不能保证遥远版本永远可由任意老版本直达。早期 0.3 候选与正式 0.3 即使版本/fingerprint 相同，也不能自动互用差分；源镜像摘要不同就拒绝，除非另行发布明确支持该候选的包。

发布侧保留每个正式版本的不可变 raw boot/system/vendor、manifest、source-lock 和完整构建审计，
用于生成差分、源代码合规和后续重现；它们不是用户必须再次下载的全量更新。
新装仍可先导入已有 0.2 全量基底，再装直达 OTA。内部完整目标 ZIP 仅用于构建/验收，不作为本轮交付。

## 4. 包格式与完整性

单独定义 `kiki-ota-v1`，不改变现有 system-package format-1 含义。

```text
manifest.json              签名覆盖的原始字节，限制大小
manifest.sig               发布密钥签名
operations.json            有界块操作表，摘要在 manifest 内
blobs/<sha256>             去重、压缩差分数据，长度/摘要在 manifest 内
metadata/target-manifest.json
metadata/target-source-lock.json
licenses/...
```

manifest 必须绑定：产品/device/架构/channel、协议版本、minimumUpdaterVersion、
源和目标 fingerprint、boot/system/vendor 的有效长度与 SHA256、分区约束、
目标 kernel 版本、OTA 序列号、操作表/所有 blob/目标 metadata 的摘要和长度。
不绑定实例 UUID或 qcow2 文件哈希：不同实例的 GPT UUID 和 userdata 合法不同。

使用独立的发布签名密钥，公钥由新启动器固定；不能信任包自己提供的公钥。
私钥不进入仓库、source-kit、实例或日志；首个 OTA 发布前完成离线保管/备份/轮换方案。
仅 SHA256 不证明来源。第一版固定 RSA-PSS（RSA-3072、SHA256、32 B salt），利用现有 Windows CNG/BCrypt 验签，不额外引入宿主加密 DLL。
公钥模数/指数、key-id、算法及签名长度严格固定；生产端离线签名工具和 Windows ARM64 实现仍必须实际互验。
不为“方便”添加 unsigned/skip-signature/force 开关。常规 update 拒绝降级/旧 OTA 序列；
回滚只允许该实例已保存的受管完整代际，最高已接受序列不随数据快照回退。
已发布 0.2 的系统包没有这个 OTA 签名协议，因此新启动器以固定的 0.2 正式产物身份作为基底，
再读取实际盘中源分区哈希；不把未知自构建、同版本不同字节的系统算作兼容基底。

采用现有静态 ZIP 库及 fail-closed 解析原则：拒绝重复 JSON key、重复 ZIP entry、
未知字段/操作/算法、外部引用、路径穿越、绝对路径、链接、附带执行文件及解压炸弹。
整数/offset/length 的溢出、所有源/目标范围和资源上限须先验检查。
签名覆盖精确 manifest 字节，其他 entry 由其摘要传递认证，避免 JSON 重序列化歧义。

## 5. 差分算法与应用规则

针对 **raw 分区镜像字节**，不针对 ZIP、qcow2、Android 文件名或用户文件。
参考 Android 块级 OTA：`COPY_SOURCE`、`REPLACE`、`ZERO`；可在有充分测试后增加有界 XOR/二进制差分。
默认 4 KiB 块，COPY 可从不可变源分区的其他位置读取，连续范围合并。
压缩采用已审计算法；第一版优先少算法、少攻击面，不预先堆叠所有 bsdiff/zstd 特性。
生成端按实际压缩成本选择 COPY/REPLACE/ZERO，不能假定 XOR 总是更小。

- 操作定义必须完整覆盖目标有效镜像，禁止重叠、缺口和越界。
- 所有 COPY 都读旧盘，绝不读已经写过的新目标，以免覆盖依赖和重试歧义。
- 更新前完整读取源 boot/system/vendor 的有效镜像，验证长度、摘要和允许的尾部填充。
  注册表/version/fingerprint 只是筛选条件，不代替盘中实际字节校验。
- 更新只写副本的上述三个分区；保留 GPT、misc、userdata 的全部字节，规范化系统分区尾部填充。
- 更新后从新 qcow2 再读出三个目标镜像，核对完整 SHA256，而不是只检查改变的块。
- 从新盘 boot 内容重新派生 direct-boot cache，核对 kernel/ramdisk。
  不能单独覆盖 Windows 的 kernel 文件而继续启动旧 boot/system/vendor。

实际音频-only R2 与正式 0.2 的镜像大小完全相同：

| 角色 | 有效字节 | 0.2 分区字节 |
|---|---:|---:|
| boot | 37,195,776 | 37,748,736 |
| system | 1,111,408,640 | 1,111,490,560 |
| vendor | 101,437,440 | 101,711,872 |

已在本地读取两份完整 ZIP 并验证镜像摘要做尺寸实验：4 KiB 变化块的 REPLACE/Deflate 数据约 **29.5 MiB**，
远小于约 796 MiB 的全量 ZIP。这仅是 **音频-only 候选的估算**，不含操作表、签名和 rc6 boot 差分，
不是已生成可安装 OTA，也没有完成端到端 OTA 回归。重建 EROFS/APEX 会造成块布局变化，不能按源码改动行数估计下载大小。
最终 rc6 OTA 大小必须等实际目标产物和打包器生成后实测。

## 6. 老布局容量是硬约束

现有 system 仅剩 81,920 B，vendor 剩 274,432 B，boot 剩 552,960 B。
必须对 rc6 与每个后续目标检查 **payload <= 当前实例对应分区长度**，按实际安装记录核验，不按文件名猜。
第一阶段不移动/缩小 F2FS，不扩大 TOTAL GiB、不重新格式化、不修改不可变容量。

目标超出任何分区，生成端不发布标称兼容 gpt-v1 的包；客户端也必须在写入前拒绝。
即便存在 0.2→0.5 差分，若 0.5 超过老布局容量，也不能保证原布局升级。
这是必须明确的限制，不是差分算法能解决的问题。

后续增长方案要单独做：新实例的 layout-v2 预留系统增长空间；老实例若需迁移，
单独制定保留整个 userdata 文件系统、总容量契约和安全备份的显式迁移流程。
不能把 layout-v2 分区上限扩大后悄悄用于 0.2 老盘。

## 7. 宿主事务、试启动、回滚

第一版 **独立 qcow2 副本**，没有 backing chain。当前 verifier 拒绝 backing file，
不为提速削弱它。之后可研究严格受管的短链 COW，但那需要另一个验证/GC/flatten 协议。
下载小不等于宿主升级没有额外磁盘开销；克隆成本包含该实例已分配的用户数据。
预检查宿主空闲空间/卷类型/临时数据/试启动写入余量。低空间或 I/O 失败只污染新目标，不碰旧盘。

```text
PRECHECK -> BASE_VERIFIED -> CLONING -> APPLYING -> TARGET_VERIFIED
         -> TRIAL_BOOT -> HEALTH_VERIFIED -> COMMIT_PENDING -> COMMITTED -> GC
```

1. 固定实例 UUID、storage/file identity，取得实例专属 lease 和更新锁；校验 owned PID/创建时间/端点。
   第一版仅允许 idle 且没有 live owned VM；运行中返回“请先正常关闭实例”，不偷偷停机。
   源盘和输入包以拒绝重解析/替换/写入的句柄固定到复制、验签与校验结束，堵住 hash 后换文件的 TOCTOU。
   所有权锁不能代替操作系统文件共享限制；不能在运行盘上先拷贝再指望 qemu-img check 证明一致性。
2. 创建带所有权标记的私有事务目录、UUID 和 journal；每步先落盘状态再执行。
3. 用被绑定/哈希审计的 qemu-img 离线生成完整、同虚拟容量、无 backing 的独立副本。
   不依赖 host PATH，不使用 QEMU 内存 snapshot，也不复制运行中的 qcow2。
4. 应用差分、qemu-img check、验证两份 GPT/各目标分区/boot cache/metadata。
5. 旧代际仍为正式指针；本事务的 owned supervisor 只试启动新代际。其他 start/update/set/delete 被该实例状态机拦截。
6. 核验目标 fingerprint、uname、sys.boot_completed、HOME/显示、电源、APEX/关键服务稳定、userdata 挂载和文件系统身份。
   检查旧数据仍可读及独立试读写探针；不得以自动格式化后能启动作为通过。
   给出有限超时/尝试次数，不把一次 boot_completed 当所有应用都兼容。
7. 试启动数据迁移只发生在新副本。成功后 **正常关机/卸载落盘**，确认本事务所有写盘进程已退出，再提交。
8. 用持久化代际描述符和原子注册表替换，一次切换 disk、boot cache 和 immutable system identity。
   实例 id、UUID、storage owner、资源/QEMU配置不变。返回时恢复为 idle。
9. 保留至少一个升级前完整代际，在下一次正常启动/关机验收后才允许受管回收。

不能用“依次改名三个文件”模拟原子提交。新启动器须统一通过 generation resolver 选路径，
改造当前 manager/runtime/session 对 `disk/phone.qcow2`、`disk/boot` 的硬编码。
旧 `disk/` 可以作为 legacy generation，首次更新前不先破坏/搬走它。
注册表协议需版本化并有一次性迁移审计、备份、原子写/FlushFileBuffers/ReplaceFileW；
旧启动器必须 fail-closed，不得在新格式/待处理事务下启动旧代际。
升级启动器时必须排除仍在写旧注册表协议的 supervisor，不能让新旧进程混写。

### 异常恢复

- PRECHECK/APPLYING 中断：旧代际有效；只检查/清理明确 owned 的未完成副本。
- TARGET_VERIFIED 后中断：按 journal 再验证所有摘要，可重试试启动；不自动宣称提交。
- TRIAL_BOOT 中断：先复验并处理本事务 owned VM 是否仍写盘；不按进程名杀进程。
  不能把宿主/CLI中断计作系统不兼容，也不能在未退出写盘进程时提交或删除。
- COMMIT_PENDING 中断：先读取原子提交点及代际摘要，明确恢复为旧或新；绝不拼接两者。
- GC 中断：只影响可回收旧代际，不影响当前代际；删除失败保留记录，不误删其他路径。
- journal/指针损坏或归属无法证明：拒绝自动启动/删除，保留两份数据并输出修复指引。

**回滚必须同时恢复升级前 userdata**。新 Android 可能已经迁移数据库，不能只退 kernel/system 却用新数据。
试启动失败退回旧完整代际不会损失试启动前数据；升级正式使用后的手动回滚会丢失此后的新数据，
必须显式确认并先保留当前代际。OTA 完整代际备份不是用户长期独立备份的替代品。
电源中断恢复依赖文件系统/硬件正常履行持久化写入；底层卷损坏不在软件原子提交保证内。

## 8. CLI 契约（待实现）

```powershell
kikiemu update --id 01 --ota ~/Downloads/0.3alpha.ota.zip --check
kikiemu update --id 01 --ota ~/Downloads/0.3alpha.ota.zip
kikiemu update --id 01 --status
kikiemu update --id 01 --resume
kikiemu update --id 01 --rollback --confirm-data-loss
```

必须显式 `--id`，不默认批量更新或选默认实例；沿用现有 `~/`、`~\`、带空格路径支持。
`--check` 验包、验基底、布局和空间，不启动 VM、不改盘、不迁移注册表。
默认 update 在 idle 实例上重建、试启动并正常关闭，成功提交后仍 idle。
`--status` 只读；`--resume` 只恢复该实例的现有事务，不需要信任用户重复提供的不同包。
`--rollback` 的确认不能隐含在一般 update 中。

输出源/目标版本、kernel、预计宿主空间、阶段百分比、结果/回滚原因及 owned 日志位置。
错误分别区分包签名、源版本不匹配、源盘改动、分区太小、空间不足、实例运行中、
中断恢复、试启动失败；不能用一个“更新失败”掩盖仍在运行的事务。
OTA ZIP 成功导入并提交后可删除；恢复依赖受管 staging/journal，不依赖 Downloads 中原包路径。

## 9. 实施与验收门禁

1. 协议/schema、签名、包生成与独立重建验证器；不同差分算法以实际包体积/资源成本取舍。
2. 私有 fixtures 上实现代际 resolver、clone/apply/readback、journal/原子提交、恢复与清理。
3. CLI 接线与启动器版本/注册表迁移；先编译测试，不运行公共 installer 或操作正式实例。
4. 实际正式 0.2→实际 0.3-rc6：至少多个容量/UUID、已有 APK/设置/文件/应用数据，正常关机后升级；
   完整镜像 hash、数据保留、HOME、图形、网络、生成音频、关机重启/持久化回归。
5. 注入每个状态边界的进程退出、短写、低空间、掉电模拟、错误基底、GPT/cache/签名/blob 损坏、
   重复 entry/key、非法路径、越界/重叠、错误 kernel、无法启动目标、回滚与 GC 失败。
   恢复必须只得到“旧完整可启动”或“新完整已验证”，不允许静默格式化或半更新。
6. 0.5 尚不存在：现在用清晰 NONRELEASE 的合成多版本夹具测试选择/直达，
   等 0.5 实际产物存在再跑真正 0.2→0.5，不能预先冒称实测通过。
7. 只有全部门禁与人工验收通过、取得明确授权后才发布 OTA；不覆盖 0.1/0.2 标签/附件。

## 10. 参考与取舍

- [Android OTA tools](https://source.android.com/docs/core/ota/tools)：增量只适用于生成包时的精确源 build；pre/post 条件。
- [A/B updates](https://source.android.com/docs/core/ota/ab)：非活动目标、整分区回读、试启动/成功标志和失败回退。
- [Virtual A/B](https://source.android.com/docs/core/ota/virtual_ab)：COW、snapuserd、可中断 merge；不是现有 qcow2 backing 的同义词。
- [Non-A/B](https://source.android.com/docs/core/ota/nonab)：旧 recovery/update-binary 模式仅作比较，不采纳包内任意执行。
- [AOSP update_engine](https://android.googlesource.com/platform/system/update_engine/+/refs/heads/main/README.md)：
  metadata/payload 校验、检查点和目标整分区校验；原生 OTA 生成需要匹配的 target-files，不是重命名现有 ZIP。
