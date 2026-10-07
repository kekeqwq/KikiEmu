# KikiEmu 0.3.1 Alpha Hotfix 1 — optional

**Optional launcher-only display fix. Not a new Android OTA and not required for the successful 0.3 → 0.3.1 OTA.** Existing 0.3/0.3.1 launcher and system releases remain available and unchanged. This fix will be included in the next normal launcher release.

## Change

The native QEMU boot console no longer shows a system version before Android has started. It initially shows logs, progress and runtime parameters only.

After the existing boot, guest identity, selected-slot and visible HOME checks finish, the launcher reads this boot's `ro.build.display.id` and `ro.build.version.incremental` through its existing private guest transport. The successful-start summary then shows the **running guest version**, not the original installation manifest. For example, `KIKI_0.3.1_ALPHA` is presented as `KikiAOSP 0.3.1 Alpha`.

A missing, malformed or unavailable version displays `Version unavailable`; it does not fail a successful boot or initiate slot fallback. No version cache, baseline manifest rewrite, updater counter reset or latest-catalog version assumption is used.

The existing QEMU already reads the dynamic `verified` status field. **No QEMU rebuild, runtime rebinding or Android update is required.** Boot readiness, managed shutdown, OTA publisher authorization and userdata handling are unchanged.

## Optional installation

1. If you want this cosmetic correction, normally shut down your KikiEmu instances and wait until they have stopped.
2. Download this hotfix release's **`setup.exe`** and optionally verify it against `SHA256SUMS.txt`.
3. Run it yourself to update the existing launcher installation. Keep your existing instances and QEMU runtime; do not create/reinstall a system or clear userdata.

You may skip this release. The displayed launcher version becomes `0.3.1-alpha-hotfix.1`; it is separate from the Android/KikiAOSP version displayed after boot.

## Attachments and evidence boundaries

This separate hotfix release includes the optional installer, its exact corresponding preferred source kit with dependency archives and relinkable objects, license/notices, build/runtime provenance, these notes and SHA-256 checksums. It does not replace attachments in the original releases or attach a new system ZIP/catalog/payload.

Compilation, core/script tests, source/relink checks, isolated native boot status/handoff verification and complete public attachment download/hash verification are recorded in release provenance. The agent does not execute `setup.exe`, run relinked results, or change host PATH/shortcuts/registry/default instance. Installer integration remains for the user to verify. No new power-loss, mount-failure, audio or GPU certification is claimed.
