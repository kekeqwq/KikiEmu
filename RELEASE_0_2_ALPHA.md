# KikiEmu / KikiAOSP 0.2 Alpha

> **Update all three components for this fix:** install KikiEmu 0.2 Alpha
> `setup.exe`, rebuild QEMU with the updated five-patch recipe, then create a
> **new** instance from the KikiAOSP 0.2 Alpha ZIP. Updating setup alone does
> not update QEMU or an existing instance's system/kernel.

## Downloads and dependency notices

- [KikiEmu setup.exe](https://github.com/kekeqwq/KikiEmu/releases/tag/v0.2.0-alpha):
  **QEMU rebuild required** for the black-screen/idle-overlay fix.
- [KikiAOSP system ZIP](https://github.com/kekeqwq/kikiaosp_test/releases/tag/v0.2.0-alpha):
  **install KikiEmu 0.2 Alpha setup.exe** for correct system-version display
  and the updated instance numbering behavior.
- Existing instances retain their original system/kernel. 0.2 does not add
  in-place system upgrades. Keep existing storage; create separate new storage.
- Published 0.1 tags/assets remain immutable. Do not reinstall old QEMU EXEs
  into a partially patched checkout or replace a running instance's runtime.

## QEMU update

Use a new checkout to avoid mixing 0.1's applied patch/build receipt with the
revised managed-close/scanout patch. In PowerShell:

```powershell
git clone https://gitlab.com/qemu-project/qemu.git ~/Repos/QEMU-0.2
cd ~/Repos/QEMU-0.2
git checkout --detach f7ada39edacaa5c26b30e98b94017b0b2ccbcf94
Invoke-WebRequest 'https://raw.githubusercontent.com/kekeqwq/KikiEmu/v0.2.0-alpha/build.ps1' -OutFile ./build.ps1
./build.ps1 --msys2 'C:\msys64'
```

Change the MSYS2 path as needed. The script may warn that this upstream commit
is different from its historical recommendation; this is the actual upstream
used for the 0.2 Windows regression. No QEMU executable is included in setup.
Select the resulting bin with `create --qemu` or `set --id NN --qemu PATH`.
See [QEMU_BUILD.md](QEMU_BUILD.md) and [QUICK_START.md](QUICK_START.md).

## Changes

- QEMU SDL presents the active VirGL scanout, not the black software
  placeholder, when pending updates remain after the boot overlay.
- An idle HOME's existing valid scanout can finish the overlay handoff without
  waiting for a touch or another guest frame.
- Launcher overlay reads the installed system's `buildIdentity.displayVersion`
  instead of hard-coding KikiAOSP 0.1. Old 0.1 instances still show their own
  0.1 identity when launched with KikiEmu 0.2.
- New instances use the lowest available display ID, starting at `01` when
  empty. `list` sorts IDs numerically. Existing instances are not renumbered;
  unfinished deletion records reserve their IDs until deletion succeeds.
  UUID/process/storage ownership checks remain intact.
- Cold-boot configuration calls have a bounded 30-second transport allowance
  (the cheap boot-completed probe remains five seconds), with the failing
  command included in diagnostics.
- System boot payload uses Linux **7.3.0-rc5-4k**, pinned upstream
  `72d3fcf802c45d00b300f25b848a93c3a2bd7c7e` with the existing two kernel patches.

## Source and verification boundaries

- Launcher built commit: `62a4768` (the release tag can include later release
  documentation). Matching source, third-party archives, licenses and relink
  objects are supplied in the launcher source kit.
- System built device: `ac4f97ecb09794ed5cabe93805244fc27bc2d390`;
  packaging pipeline: `eb18a3da42b13f0443ad5a4c85a3ac424c4bd04e`;
  kernel recipe: `d849aac5683957457a8fcb2eabb55cf2c8a65a19`.
- Android 17 format-1 package; 1084 fixed upstream projects. This is an
  incremental build in independent Btrfs copies of audited release 0.1
  sources/output, not an empty-output full rebuild or a developer disk export.
- 332 isolated native core checks, 58 build-script checks, full five-patch
  application checks, built-image property/EROFS/ZIP validation, and real
  independent Windows ARM64 SDL/WHPX/VirGL rc5 startup were performed.
- Physical-desktop observations for this fix do not activate/raise/maximize
  the window or inject a click. Raising the window can hide the original bug.
- Public installer/PATH/shortcut/uninstall acceptance belongs to the user.
  No performance optimization or new game-compatibility guarantee is included.

Artifacts include corresponding-source and provenance materials; third-party
licenses are unchanged. The source identity in each artifact's provenance is
more precise than the documentation-only release tag.
