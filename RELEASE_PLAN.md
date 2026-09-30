# KikiEmu / KikiAOSP 0.1 Alpha release plan

Status: implementation branch, 2026-09-30. Native internal disk-install/boot-cache modules and an icon master exist; no accepted release build, public manager/installer, setup.exe, format-1 ZIP, tag or GitHub Release yet. The accepted mainline remains the development rollback baseline.

## Agreed requirements

- System brand `KikiAOSP`; launcher/CLI/desktop/window `KikiEmu`. Initial version `0.1.0-alpha`, prerelease/tag `v0.1.0-alpha`.
- TWO project release downloads: setup.exe from KikiEmu and the system ZIP from kikiaosp_test. QEMU is user-provided via required `create --qemu <bin-directory>`; README documents the exact patched native ARM64 build and deployment prerequisites. setup.exe does not silently select or install another QEMU. Users do not manually assemble Android images.
- Publish only clean system installation materials, NOT a whole disk, QCOW2, userdata, used service disks, accounts, authorized keys or the old runtime-support tarball.
- `create --system --storage --size` creates a new persistent user disk. Size is TOTAL virtual phone capacity chosen by the user, not just data capacity or a fixed128/200GiB preset. Once created, size/layout cannot change.
- Dynamic physical allocation, persistent APK/photo/settings data, and no dependency on the downloaded ZIP after installation. Host actual use and guest free space are distinct and include their respective metadata/overheads.
- English product-owned CLI/installer/dialog/startup text. No management GUI or extra console: desktop entry goes directly to the accepted native SDL boot logo/logs, then Android.
- Native ARM64 CLI/desktop/QEMU/camera route, VirGL and guest120Hz; default1003x1556/288dpi/font1.5/8vCPU/4GiB. Launcher-owned dependencies are private. The configured QEMU bin directory must contain its own validated runtime dependencies; never append a developer MSYS2 directory or fall back to PATH at launch.
- Original pastel pink icon: short-haired girl hugging an original robot, not Google's Android robot/logo. New self-authored code uses GPL-2.0-or-later; third-party licenses/notices remain their own.

## Producer-owned contract

The canonical [package contract](https://github.com/kekeqwq/kikiaosp_test/blob/5af23aeeeec69e01eb4d3fa2d56778c029eb34ad/RELEASE_FORMAT.md) and [release policy](https://github.com/kekeqwq/kikiaosp_test/blob/5af23aeeeec69e01eb4d3fa2d56778c029eb34ad/RELEASE_POLICY.md) live in kikiaosp_test. These are drafts; this exact revision records the user-provided QEMU and user-owned installer acceptance requirements. The consumer must pin the exact schema/fixture revision when the package reader is implemented, not build against a floating URL or independently redefine the format.

Proposed format1: ZIP with manifest.json, a boot-header-v4 boot.img containing matching kernel/full initramfs, raw EROFS system.img/vendor.img, source-lock and licenses. No full disk/data image/initialized service disk/nested support archive. CP2A product/system_ext are inside system; historic compatibility disks are not public payload roles. Freeze header/layout/ABI rules only AFTER the producer/consumer installation prototype boots and passes.

The public reader is a NEW implementation, not a renamed developer collector/bundle. collect_kikiaosp_assets.ps1 stays a development rollback tool. Shared machine-readable schema, semantic validator and pinned positive/negative fixtures enforce the contract in BOTH repositories. Check hashes/lengths, required roles, architecture, ABI, capacity, boot headers, archive allowlist and safe paths before registering an instance. Reject unsupported formats rather than guessing from filenames.

System release version is separate from format/layout/runtime ABI versions. No silent change of ZIP format, paths, encodings or required roles within frozen format1. Breaking changes require new contract versions and reader support/backward-compatibility gates. Published assets cannot be overwritten under the same version, and launcher updates cannot silently rewrite installed OS/disks.

## User-created storage

Proposal: standalone phone.qcow2, GPT-v1, 512-byte sectors/1MiB alignment, no preallocation/backing dependency, total bytes fixed by --size. Install boot/system/vendor payloads, create required service ranges locally and format fresh F2FS userdata in the remaining aligned capacity. Minimum size comes from manifest/layout and tested filesystem requirements.

All partitions/GPT/reserves count against the total. A small verified direct-boot cache is derived locally from the installed boot payload for QEMU kernel/ramdisk loading, never from Downloads; count cache/logs in host actual usage, not Android free space. Development GPT/direct-boot behavior is verified below; final clean ZIP installation still requires acceptance.

The rollback/default fstab mounts entire vda/vdb/vde devices. A single GPT phone disk changes the boot/storage ABI: named partitions, first-stage discovery, fresh formatting and initramfs generation are implemented together in the DEVICE repository, not just in Windows arguments. Do not change the accepted development baseline in place. The new tracked source-built static-init/GPT recipe now boots with VirGL/120Hz and fresh full-capacity F2FS; it is not a clean final ZIP.

Implementation checkpoint: the native internal prototype creates new standalone32/200-GiB QCOW2 disks with five aligned GPT partitions, independent UUIDs and no backing/preallocation; all GPT/payload writes are read back and QCOW2 is checked. Boot cache is derived from the installed boot partition, with strict v4/ARM64/4-KiB/gzip checks and no manifest-supplied command line. The device repository owns the minimal virtio boot-UUID fix and both-init-stage recipe. The corrected system boots to SDL/VirGL/120Hz HOME and formats fresh full-capacity F2FS; a data marker survives normal shutdown/reboot. Both capacities now pass exact kernel/vold/StorageStats checks and actual Settings screenshots, with a product-gated bypass of phone-tier rounding and the fabricated one-GiB temporary-file floor.32GiB is displayed as34GB/1.8GB used;200GiB as215GB/2.6GB used; Android17/system reserve is about1.3GB for this payload set, NOT a fixed imposed number. Final clean-source packaging and public producer/consumer/isolation acceptance remain required. These internal tools do not constitute user CLI/installer acceptance.

Create validates/stages/installs/checks atomically before registering a UUID/display ID. First-boot data initialization must mount the real correctly-sized filesystem before showing startup success; interruption recovery must never format an established user disk. Normal public boot persists writes, never the current developer temporary `-snapshot`. Validate immutable actual capacity/layout at startup and refuse external drift; no set/resize/repartition/reformat/system replacement in0.1.

## CLI / desktop / installer

```powershell
kikiemu create --system ~/Downloads/KikiAOSP-0.1.0-alpha-arm64.zip --storage ~/MyAndroid --size 200g --qemu ~/Tools/KikiQemu/bin
kikiemu list
kikiemu set --default 01
kikiemu set --id 01 --mem 8g
kikiemu set --id 01 --cpus 8
kikiemu set --id 01 --qemu ~/Tools/KikiQemu-v2/bin
kikiemu start --id 01
kikiemu stop --id 01
kikiemu info --id 01
kikiemu logs --id 01
kikiemu doctor
kikiemu adb --id 01 shell
```

Create requires system/storage/size/qemu; performance is optional. `--create` and `--set` are accepted aliases for the `create` and `set` subcommands. `g` means GiB and help states it. Lock and atomically save manager edits. List total capacity, actual host use, resource configuration, GPU VirGL and runtime status. Proposed presets: default8CPU/4GiB, medium8CPU/6GiB, high10CPU/8GiB, with identical render/resolution baseline and host-aware validation; no fictional additional GPUs.

`set` mutable whitelist initially covers supported QEMU startup resource fields --mem/--cpus/--performance, the validated QEMU bin binding --qemu, plus manager --default. A running instance keeps its effective config and original QEMU binding. Report `Updated instance 01. Changes will take effect on the next start.` No promise of live hotplug. Reject --size, raw QEMU arguments, unsafe backend/kernel/OS/boot/ABI changes.

Store a normalized absolute QEMU bin path plus executable/dependency identities at create/set. A QEMU change must pass architecture/compiled-capability/dependency/ABI checks BEFORE atomically changing configuration. Start resolves the saved path, revalidates it and refuses missing/incompatible/changed files; no PATH search, old-development fallback, or silent software renderer. Building new Dev QEMU uses a separate source/output/bin directory and never overwrites the configured release runtime. A hash verifies byte identity, not publisher trust or end-to-end hardware compatibility.

Console kikiemu.exe and a GUI-subsystem desktop entry share core validation/configuration. Desktop reads the RELEASE default and launches without a terminal/manager UI. Missing initialization/default/runtime support produces an English dialog with commands. Double-click while running activates the registered window, never a second writer of the same disk.

Proposed per-user app location: %LOCALAPPDATA%/Programs/KikiEmu with private launcher/camera dependencies and CLI bin in user PATH. Release registry lives under %LOCALAPPDATA%/KikiEmu/release, separate from Dev. QEMU remains in the user-configured directory; installing/uninstalling KikiEmu does not overwrite/delete it. Installer creates shortcuts and a registered uninstaller. Uninstall removes owned app files/PATH/shortcuts, retains user disks and recoverable records by default; never recursively delete user-selected storage. Future upgrades cannot replace an in-use runtime or migrate an instance silently.

## Release/dev isolation — required in INITIAL 0.1 and every repair

- Release window KikiEmu, guest version/model/build identity branded KikiAOSP0.1Alpha, unique kiki-release-UUID boot serial. Dev window QEMU/Dev, explicit Dev model/build identity and kiki-dev-UUID. Keep required ranchu/HAL/upstream-package protocol names intact.
- Separate release/dev registry/default/storage/log/mutex/channel identities and private ADB servers/transports, QEMU control and camera IPC. Collision-safe endpoint allocation, not hardcoded developer5555/4447/4455. TCP adb shows address/port; model/serial branding alone is not transport isolation.
- All commands target owned instance UUID, exact executable path, process creation identity and control/ADB endpoint, accounting for PID reuse. No generic kill-all-QEMU, global adb kill-server, title-only selection or unscoped adb shell. Dev commands must fail closed on release targets.
- Audit the EXISTING tracked development launch/stop/capture/benchmark/ADB helpers too. Route them through a guarded Dev transport/process resolver; do not protect only the new public CLI while leaving legacy development commands able to select user release instances.
- Use distinct validated release/dev runtime directories and pinned identities: development builds never replace the configured release QEMU/DLLs/kernel/system/config or depend on release storage. Installed release users never implicitly read mutable developer source/out/SDK paths. New launchers must retain the user's compatible QEMU binding for existing instances.
- Shared host hardware still needs coordination: do not steal cameras, change host GL/audio/IME/display/keyboard settings, restart the host or exhaust resources as an automatic dev step while release is in use. Global-driver tests require another environment or explicit coordination. Namespaces do not guarantee zero resource contention or immunity from host-driver failures.
- Mandatory regression: keep release A running with an app/data marker; build Dev, start B, set B resources, use scoped ADB/camera, stop/restart B and close Dev tools. Verify A's process/control identity, disk/source identity, data/defaults and operation remain intact. Wrong-target commands and collisions must reject. Run whenever identity/IPC/launcher code changes, not only at final tagging.

## Work phases and publication gates

1. **Contract/identity:** jointly review draft, implement schema/semantic validator/fixtures and release/dev channel/build/endpoint identity. Freeze format1 only after its working prototype.
2. **Storage/boot:** complete tracked fresh initramfs/boot recipe, named GPT discovery, immutable-total dynamic disk, fresh-data formatting and persistence. The old frozen-ramdisk recipe gap is a release blocker.
3. **Manager:** native CLI/shared core/desktop/dialogs, immutable instance registration, safe next-start settings and process/endpoint ownership.
4. **Distribution:** original icon, private redistributable runtime dependency audit, environment doctor, English installer/uninstaller. Do not assume the developer's custom host GL installation exists on recipients' PCs.
5. **Clean candidate:** freeze source commits/pinned AOSP manifest and freshly build deliverables from clean source/independent output. No imported developer bundles/disks; record provenance/licenses/hashes.
6. **Acceptance:** the user performs setup.exe installation/uninstallation, PATH/shortcuts and CLI initialization/configuration acceptance using the supplied candidate; the agent does not install setup.exe or modify the user's PATH. The agent owns build checks and system boot/GPT/F2FS/persistence/input/UI/audio/camera/GPU/120Hz checks. Record the user's results explicitly, not as agent-tested. Several capacities, deleted ZIP, QEMU path rebinding, release-in-use/Dev coexistence and uninstall keeping data must pass before public release.
7. **Publish:** only then tag immutable accepted sources and publish the TWO prereleases. Record exact contract/producer/consumer/kernel/AOSP identities and final hashes. Corrections use a new version; don't replace old asset bytes.

Implementation is authorized but is not claimed complete. All work remains on separate feature/release branches and the accepted developer mainline stays available throughout. Candidate setup.exe is handed to the user for testing; no public Release until the required results are recorded.
