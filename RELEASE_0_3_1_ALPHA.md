# KikiEmu / KikiAOSP 0.3.1 Alpha

Authorized storage-accounting and native OTA startup hotfix; native publisher sequence **5**.

## Updating an existing 0.3 instance

On a format-2 physical A/B 0.3 instance, open **Settings → System → System update**, choose **Check for updates**, then download/install the signed **0.3.1 Alpha** target. Restart only when the updater requests it. Apps, settings and shared userdata stay on the SAME disk. The CLI uses the same KikiUpdater / Android `update_engine`.

The existing 0.3 launcher remains compatible with this OTA. The 0.3.1 launcher installer is optional version-matched delivery, not a prerequisite for updating Android. The full baseline ZIP is for NEW instances only, never a replacement for an existing userdata disk. Fresh 0.3.1 instances are already sequence5 and correctly reject the same-sequence OTA. There is no 0.2 disk migration or userdata formatting.

User-side real online OTA acceptance can now be performed using **0.3 → 0.3.1**, rather than waiting for 0.4. Engineering regression results and a user's manual acceptance are distinct; this release does not claim the user's acceptance has happened.

## Native OTA startup repair

An initial unpublished engineering candidate installed successfully but rebooted during checkpoint commit. Its userdata mounted correctly; this was not a partition-layout or userdata-format migration. The failure was traced to vold receiving `commitChanges()` before `needsCheckpoint/prepareCheckpoint` initialized its normal boot lifecycle. On this physical A/B product, filesystem-checkpoint flags are absent, so fs_mgr and apexd do not provide a deterministic initialization call.

0.3.1 explicitly runs the standard vold `prepareCheckpoint` API synchronously after userdata mounting and early Boot HAL startup, before framework startup. Preparation failure aborts startup; the regular checkpoint commit, boot-success checks and native rollback behavior remain required. No checkpoint bypass, early forced boot success, counter reset, disk replacement or userdata formatting is used. This repair is delivered in the signed native FULL OTA, which updates the system slots while keeping the original userdata disk.

## Storage correction

0.3 incorrectly treated `whole disk capacity - userdata filesystem capacity` as occupied system storage. Its sparse A/B layout reserves 64MiB boot, 4GiB system and 512MiB vendor **per slot**; unwritten headroom is not an installed system's actual image footprint.

0.3.1 inventories BOTH slots using a bounded, read-only native header reader:

- Boot: validated unsigned Android header-v4 kernel/ramdisk sizes and alignment.
- System/vendor: actual EROFS block counts, not their maximum partition budgets.
- Userdata used: live filesystem blocks, including caches; no inflated free-space API.
- Reserved: unused partition headroom and filesystem reserves, shown separately.
- Available for user data: actual writable userdata capacity, not sparse OTA reserves.

The storage dashboard, system category, home summary, category drill-down, cached classic views and Catalyst views use this accounting. An unavailable/invalid inventory conservatively falls back; it does not pretend corrupt images take zero space. No partitions are resized, no disk is converted, no boot/data blocks are rewritten by the accounting reader, and no userdata is formatted.

Android allocation and Windows file allocation are different measurements. QCOW2 metadata, 64KiB allocation granularity, unreclaimed clusters, host boot caches/logs and GB/GiB formatting can still differ. This fix does not falsely advertise reserved system blocks as writable user space or promise byte-for-byte equality with a Windows folder's size.

## Delivery and boundaries

Android17 / Linux7.3.0-rc6-4k, same physical A/B layout and publisher trust store. The kernel and audited five-patch QEMU runtime are unchanged from0.3; no invented kernel/QEMU upgrade. The historical retained kernel's Nix restore is still explicitly **not byte-reproducible**. Corresponding sources, actual embedded configuration, licensing/provenance and SHA-256 checksums accompany the binaries.

Installer compilation is not installation/PATH/shortcut/uninstall acceptance; the agent does not execute setup.exe or alter the formal0.2 instance. Power-loss, simultaneous boot-image corruption and userdata mount-failure fault injection are not claimed passed by this hotfix.

Per user request, older0.1/0.2 GitHub Release pages/assets may be retired after verified0.3.1 delivery. Git tags/history and0.3 remain. Older preferred source kits are retained as source-only attachments in0.3.1, not offered as old install packages.
