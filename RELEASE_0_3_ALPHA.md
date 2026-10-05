# KikiEmu / KikiAOSP 0.3 Alpha

> **Release decision:** Published at the user's instruction on the basis of
> completed engineering tests. **User-side real online OTA acceptance is
> deferred until a future 0.4 update is available.** This is not a claim that
> a user has already tested a public 0.3-to-0.4 upgrade.

## Downloads and starting baseline

- [KikiEmu 0.3 setup.exe](https://github.com/kekeqwq/KikiEmu/releases/tag/v0.3.0-alpha)
- [KikiAOSP 0.3 system and FULL OTA](https://github.com/kekeqwq/kikiaosp_test/releases/tag/v0.3.0-alpha)
- Install the 0.3 launcher and create a **NEW** instance from
  `KikiAOSP-0.3.0-alpha-arm64-ab.zip`. This is the format-2 physical A/B
  baseline, Android 17, Linux **7.3.0-rc6-4k**, publisher sequence **4**.
- **0.2 disks are not converted or migrated.** Keep existing 0.2 storage.
  Creating the 0.3 baseline is separate from later data-preserving OTA.
- QEMU is unchanged from the audited 0.2 native ARM64 five-patch runtime.
  If you already use that runtime, no new QEMU rebuild is required for 0.3.
  Otherwise follow [QEMU_BUILD.md](QEMU_BUILD.md). QEMU is not in setup.exe.

## Phone-style native FULL OTA

Open **Settings > System > System update**. The English Kiki updater checks
`kekeqwq/kikiaosp_test` GitHub Releases, authenticates publisher-signed metadata
and chooses the highest compatible sequence. There is no source/target version
selection. **Download and install**, then **Restart to finish**.

The CLI uses the **same** KikiUpdater / Android `update_engine`:

```powershell
kikiemu update --id 01 --action check
kikiemu update --id 01 --action status
# Optional signed FULL OTA delivery, not a second host flasher:
kikiemu update --id 01 --action apply --package "$HOME/Downloads/KikiAOSP-0.3.0-alpha-full.ota.zip"
kikiemu update --id 01 --action reboot
```

Normal updates write the inactive boot/system/vendor slot, preserving apps,
settings and shared F2FS userdata. Restart is a normal shutdown followed by
an owned cold boot loading the selected slot's actual kernel/ramdisk.
An import ACK proves a durable private copy, **not** completed installation.
The fresh 0.3 baseline already has sequence4; reapplying its sequence4 OTA is
correctly rejected. A future compatible higher sequence is required to update it.

Trust does not come from a GitHub digest alone. A dedicated publisher key signs
both the update catalog and native payload; `update_engine` uses the required
Kiki-only certificate store. Device/layout/Android-major/sequence/capacity
checks remain mandatory. Publisher private keys are not distributed.

## Completed engineering tests

- Actual signed FULL installs on the **same** 32 GiB fixture, sequence
  1 -> 2 -> 3 -> 4, cold slot switching A -> B -> A -> B and successful marking.
- APK bytes, UID, private app data, file markers and settings retained;
  no replacement upgrade disk, userdata reset or pre-test sync shortcut.
- Native metadata-signature corruption rejected by `update_engine` (error26).
- Real private staging ENOSPC: no durable-copy ACK, incomplete private stage
  cleaned, exact inbox retained, no installation or userdata format.
- In-flight update suspended at9%, normal shutdown/cold restart, native resume
  from59/513 operations / 70,264,712 bytes; active duplicate submission rejected.
- Damaged inactive boot header refused; previous slot started with data intact.
  The same valid FULL OTA subsequently repaired the failed slot and booted it.
- Both slots unbootable in standard BCB: refused **before QEMU startup**, disk
  retained. Undoing the exact developer BCB fault is not a public recovery API.
- English updater, navigation/status-bar safe areas, readable light/dark
  system-bar icons and system-following colors verified on the real guest.
- 344 native core checks, 58 isolated build-script checks, matching installer
  compilation, source/dependency hashes and actual source-kit object relink.

## Acceptance and limitations

**User-side real online OTA acceptance waits for the future 0.4 update.**
Engineering local FULL/resume/fallback tests are not that future user test.
At this release decision, no positive public online download/install or user
installer/PATH/shortcut/uninstall acceptance is claimed. setup.exe was compiled,
not executed by the agent. Power-loss, simultaneous corruption of both boot
images and userdata mount-failure fault injection were not claimed tested.

This is userdebug/unlocked direct boot, not AVB Verified Boot, GKI/LTS/VTS
certification. System-slot fallback does **not** roll back app databases or
shared userdata migrations. Back up important data; no unlimited capacity,
Android-major compatibility or zero-risk guarantee is made.

The user-accepted audio recovery policy is unchanged. About1% brief crackle
remains a known limitation; the previously persistent loss of sound was not
observed during the user's 100-minute / 250-clip acceptance.

## Sources and reproducibility

System built device commit: `4b3e71fca9d63c1f8ce3be5430444c7c52290971`.
The release catalog/wrapper is promoted from the exact tested sequence4 payload;
changing its signed public URL/version does not rebuild or change system images.
Launcher and packaging/collection commits are recorded in each
`release-provenance.json`, separately from documentation-only tag changes.
Corresponding sources, notices, preferred kernel configuration and launcher
relink objects (including native A/B `ab.o`) are supplied.

The shipped rc6 Image is the exact retained, historically audited and booted
kernel, SHA256 `38c6ad1d6cffc76af1f4b6ce66ba42db4d8865b0f60b842fd147bef52b3a5b7c`.
Restoring the **same** Nix derivation after GC produced different bytes. This is
explicitly recorded as **`byteReproducible: false`**, not silently substituted or
claimed identical. Actual source/patches and configuration extracted from the
shipped Image are supplied with the historical receipt.

Published 0.1/0.2 tags and assets are unchanged. QEMU was not revised merely to
match a version number, and the formal 0.2 instance was not operated or modified.
