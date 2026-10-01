# KikiEmu / KikiAOSP 0.1 Alpha release plan

Status: test-candidate handoff, 2026-10-01. The R8 native ARM64 manager/desktop/camera and setup.exe candidate are compiled from clean commit2130e52; 306 isolated internal checks pass. setup.exe and public CLI/desktop entry points have NOT been executed by the agent. Independent frozen Android17 source/output produced the actual validated clean system ZIP. Fresh32/200-GiB systems have booted, preserved data through normal reboot and passed exact capacity checks; the200-GiB reboot also ran with the original ZIP path unavailable. Candidate hashes and actual evidence are recorded in [CANDIDATE_TEST_20261001.md](CANDIDATE_TEST_20261001.md). User installer/CLI acceptance and the remaining publication gates below are not claimed complete. The user explicitly withdrew strict release/Dev isolation on2026-10-01: future development uses the same versioned package baseline. No accepted release, tag, GitHub Release or mainline merge is claimed.

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

The canonical [package contract](https://github.com/kekeqwq/kikiaosp_test/blob/5cfd0a94c1b267661d2ae96bdc3875365ad686cf/RELEASE_FORMAT.md) and [release policy](https://github.com/kekeqwq/kikiaosp_test/blob/5cfd0a94c1b267661d2ae96bdc3875365ad686cf/RELEASE_POLICY.md) live in kikiaosp_test. These are drafts. Producer-owned manifest/source-lock schemas and metadata fixtures are vendored byte-for-byte from that exact commit, with SHA-256 records in `src/kikiemu/contracts/PIN.json`, and embedded at build time. The native reader and producer validator share the schemas and semantic rules; no floating runtime URL or independently guessed format.

Proposed format1: ZIP with manifest.json, a boot-header-v4 boot.img containing matching kernel/full initramfs, raw EROFS system.img/vendor.img, source-lock and licenses. No full disk/data image/initialized service disk/nested support archive. CP2A product/system_ext are inside system; historic compatibility disks are not public payload roles. Freeze header/layout/ABI rules only AFTER the producer/consumer installation prototype boots and passes.

The public reader is a NEW implementation, not a renamed developer collector/bundle. collect_kikiaosp_assets.ps1 stays a development rollback tool. Shared machine-readable schema, semantic validator and pinned positive/negative fixtures enforce the contract in BOTH repositories. Check hashes/lengths, required roles, architecture, ABI, capacity, boot headers, archive allowlist and safe paths before registering an instance. Reject unsupported formats rather than guessing from filenames.

Implementation checkpoint: bounded native JSON Schema subset, semantic/provenance checks and libarchive ZIP/ZIP64 extraction are implemented and cross-read a Python-produced SYNTHETIC NONBOOTABLE deflate/local-ZIP64 fixture. Raw names/local headers are checked too; SFX, comments/trailers, duplicate/NUL paths, unknown file roles, encrypted/special entries and size/hash/boot/EROFS mismatches reject. Extraction uses only a new owned staging directory and cleans that exact directory on validation failure. This verifies parsing/integrity, not a clean build, publisher trust, first boot or a release package. The producer has validators/tests, NOT the final clean-build/packaging pipeline yet.

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
kikiemu --delete --force --id 01
kikiemu info --id 01
kikiemu logs --id 01
kikiemu doctor
kikiemu adb --id 01 --shell "getprop ro.serialno"
```

Create requires system/storage/size/qemu; performance is optional. `--create` and `--set` are accepted aliases for the `create` and `set` subcommands. `g` means GiB and help states it. Lock and atomically save manager edits. List total capacity, actual host use, resource configuration, GPU VirGL and runtime status. Proposed presets: default8CPU/4GiB, medium8CPU/6GiB, high10CPU/8GiB, with identical render/resolution baseline and host-aware validation; no fictional additional GPUs.

### Explicit destructive instance deletion

`kikiemu --delete --force --id 01` (also `kikiemu delete --force --id 01`) permanently deletes that instance. `--force` is a valueless flag and mandatory: without it refuse with an English instruction, rather than silently deleting or prompting from the desktop entry. Always require an explicit ID; deletion never falls back to the default. This is a separately authorized destructive operation, not an implicit part of stop, set or uninstall.

The manager locks the instance against start/set, resolves its registered UUID/channel, and performs read-only preflight before touching processes or files. Require its creation-time directory file identity, matching `.kikiemu-owner.json`, normalized local absolute path and no junction/symlink/reparse redirection in ancestors or children. Reject volume/profile/protected application roots, overlap with other instances/manager/install/QEMU/workspace directories, and edited/foreign ownership. No caller-supplied `--storage` path on delete. Pin ancestor/root handles through the transaction; do not delegate recursive deletion to a shell or follow reparse targets.

Journal the delete outside storage, then validate ALL recorded live process identities (UUID/channel, exact EXE/SHA, creation time, PID-reuse protection and endpoint ownership) before stopping ANY. Terminate only the owned supervisor/QEMU/camera/private ADB runtime and wait for actual exit before removing the registered storage directory including its disk, boot cache and local logs. Force deletion does not promise a graceful guest shutdown or recoverability: the user's data is intentionally discarded. Never kill by process name/window title or call global `adb kill-server`.

Unregister and clear the default ONLY after filesystem removal succeeds; keep a recoverable failed-delete record on error/interruption. Refuse to redirect a recovery to another path. Delete leaves the original system ZIP, BYO QEMU directory, other instances and the installation intact. English success: `Deleted instance 01 and its storage. Default system cleared.` (omit the last sentence when it was not default). Distinguish this from uninstall, which retains disks and recoverable records.

Internal deletion-library tests exercise ONLY brand-new temporary fixtures and exact disposable test children, not the user's public CLI/installer acceptance or actual VM disks. The library now covers owner/directory/PID/channel/endpoint guards, protected/redirected paths, target-only termination, default removal, partial failure/retry and recovery after disk removal before unregister. A live supervisor holding a real storage lease is tested: preflight uses read-only pins, stops the exact runtime, then upgrades to delete handles while overlapping identity handles preserve the original file IDs; requesting DELETE before stopping would incorrectly fail on the running lease. End-to-end public delete/default/journal recovery and wrong-target tests join the user-owned acceptance matrix before publication; the library is not a claim of public manager/setup/delete delivery.

Windows safety primitives: [CreateFileW sharing/reparse semantics](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew), [handle-based file disposition](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle), [process creation time](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes) and [TCP endpoint owner inspection](https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getextendedtcptable). These checks prevent accidental cross-instance operations; records are not a cryptographic security boundary against the same Windows user deliberately editing all authoritative files.

`set` mutable whitelist initially covers supported QEMU startup resource fields --mem/--cpus/--performance, the validated QEMU bin binding --qemu, plus manager --default. A running instance keeps its effective config and original QEMU binding. Report `Updated instance 01. Changes will take effect on the next start.` No promise of live hotplug. Reject --size, raw QEMU arguments, unsafe backend/kernel/OS/boot/ABI changes.

Store a normalized absolute QEMU bin path plus executable/dependency identities at create/set. A QEMU change must pass architecture/compiled-capability/dependency/ABI checks BEFORE atomically changing configuration. Start resolves the saved path, revalidates it and refuses missing/incompatible/changed files; no PATH search, old-development fallback, or silent software renderer. Building new Dev QEMU uses a separate source/output/bin directory and never overwrites the configured release runtime. A hash verifies byte identity, not publisher trust or end-to-end hardware compatibility.

Console kikiemu.exe and a GUI-subsystem desktop entry share core validation/configuration. Desktop reads the RELEASE default and launches without a terminal/manager UI. Missing initialization/default/runtime support produces an English dialog with commands. Double-click while running activates the registered window, never a second writer of the same disk.

The native console and GUI-subsystem desktop components compile with shared management/runtime dispatch. Guarded deletion is connected to the exact `--delete --force --id` spelling, including force rechecking at the library boundary, registration/default cleanup and fail-closed recovery. Start/stop/logs and bounded private ADB shell source are implemented; suspended child registration, owned jobs, per-session endpoints, disk/boot validation and boot-overlay status are shared by the desktop supervisor. Only isolated internal library fixtures have exercised management/wire operations; the public CLI/desktop has not been executed for user acceptance. Real supervisor/system and concurrent release/Dev regressions, complete interrupted-create recovery, installer and real clean package acceptance remain publication blockers; don't present these components as setup.exe.

Private `adb --id ID --shell COMMAND` connects directly to that owned QEMU's adbd wire endpoint with bounded shell-v2 stdout/stderr/exit status. It does not start/stop a host ADB daemon, select a default transport, use global 5037 or alter host ADB keys. It is initially a one-shot guest-command interface, not an interactive shell/file-transfer replacement. Mock wire tests validate framing, checksums, fragmented/coalesced streams, timeouts and identity failures; actual adbd compatibility still requires system regression.

Proposed per-user app location: %LOCALAPPDATA%/Programs/KikiEmu with private launcher/camera dependencies and CLI bin in user PATH. Release registry lives under %LOCALAPPDATA%/KikiEmu/release, separate from Dev. QEMU remains in the user-configured directory; installing/uninstalling KikiEmu does not overwrite/delete it. Installer creates shortcuts and a registered uninstaller. Uninstall removes owned app files/PATH/shortcuts, retains user disks and recoverable records by default; never recursively delete user-selected storage. Future upgrades cannot replace an in-use runtime or migrate an instance silently.

## Versioned-package development and scoped control

**Requirement update,2026-10-01:** the user withdrew the independent Dev channel,
separate Dev management/tool suite and mandatory release-A/Dev-B concurrency
gate. Do not keep those obsolete requirements as a blocker or falsely claim
that their old test matrix passed. Future fixes start from this clean package
baseline, preserve format1, and become new0.2/0.3 versions after testing.

- Build tracked changes in the responsible source repositories, produce a new
  system ZIP, and use the same KikiEmu create/start/ADB/stop workflow with NEW
  storage and its explicit instance ID. Historical multi-disk developer helpers
  are rollback references, not the next-version development route.
- Retain already implemented per-instance UUID, exact EXE path/hash/process
  creation identity, storage ownership and private endpoint checks. No generic
  kill-all-QEMU or global `adb kill-server`; direct Platform Tools access uses
  the currently recorded port and explicit `-s`, as documented in QUICK_START.
- Keep the actual release brand/version/fingerprint and unique serial. Preserve
  required ranchu/HAL/upstream-package protocol identifiers. A separate Dev
  suffix/title/registry is no longer required for future package tests.
- Do not overwrite published asset bytes, an established OS disk or an in-use
  QEMU runtime. A new system version is a new installation in0.1, not `set`
  replacing existing payloads. A separately built QEMU can be selected through
  validated `set --qemu` for the next boot.
- Do not change host display/audio/keyboard/IME/GL-driver settings or steal
  shared devices as an automatic development step. Multiple instances still
  share real host resources; no performance or host-fault isolation is promised.

## Work phases and publication gates

1. **Contract/identity:** jointly review draft, implement schema/semantic validator/fixtures and release/dev channel/build/endpoint identity. Freeze format1 only after its working prototype.
2. **Storage/boot:** complete tracked fresh initramfs/boot recipe, named GPT discovery, immutable-total dynamic disk, fresh-data formatting and persistence. The old frozen-ramdisk recipe gap is a release blocker.
3. **Manager:** native CLI/shared core/desktop/dialogs, immutable instance registration, safe next-start settings and process/endpoint ownership.
4. **Distribution:** original icon, private redistributable runtime dependency audit, environment doctor, English installer/uninstaller. Do not assume the developer's custom host GL installation exists on recipients' PCs.
5. **Clean candidate:** freeze source commits/pinned AOSP manifest and freshly build deliverables from clean source/independent output. No imported developer bundles/disks; record provenance/licenses/hashes.
6. **Acceptance:** the user performs setup.exe installation/uninstallation, PATH/shortcuts and CLI initialization/configuration acceptance using the supplied candidate; the agent does not install setup.exe or modify the user's PATH. The agent owns build checks and system boot/GPT/F2FS/persistence/UI/audio/camera/GPU/120Hz checks; actual host touch/physical keyboard acceptance is explicitly identified as user work when only guest-injected actions were observed. Record the user's results, not as agent-tested. Several capacities, deleted ZIP, QEMU path rebinding, instance-target safety and uninstall keeping data must pass before public release. Strict independent Dev coexistence is no longer a gate under the updated requirement.
7. **Publish:** only then tag immutable accepted sources and publish the TWO prereleases. Record exact contract/producer/consumer/kernel/AOSP identities and final hashes. Corrections use a new version; don't replace old asset bytes.

Implementation is authorized but is not claimed complete. All work remains on separate feature/release branches and the accepted developer mainline stays available throughout. Candidate setup.exe is handed to the user for testing; no public Release until the required results are recorded.
