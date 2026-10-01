# KikiEmu / KikiAOSP 0.1 Alpha

Version `0.1.0-alpha`, tag `v0.1.0-alpha`, GitHub **prerelease**.

Download [setup.exe from KikiEmu](https://github.com/kekeqwq/KikiEmu/releases/tag/v0.1.0-alpha)
and [the system ZIP from kikiaosp_test](https://github.com/kekeqwq/kikiaosp_test/releases/tag/v0.1.0-alpha).
Read [QUICK_START.md](QUICK_START.md). Build QEMU independently using
[QEMU_BUILD.md](QEMU_BUILD.md): clone upstream QEMU wherever you prefer, download
`main/build.ps1` into that checkout and run it with your MSYS2 path. A verified
revision is recommended, not enforced. KikiEmu does not include/rebuild QEMU.

## Manager changes

- `create` requires `--system`, `--storage`, `--size` and `--qemu`; `~/` and
  `~\` expand to the current Windows user's profile in ALL path options.
  Trailing directory separators are accepted. Quote each complete path.
- English errors identify missing files, conflicting/redirected paths and
  cleanup failures. A rollback failure no longer hides the original error.
  Ownership checks remain enabled; existing storage is never silently adopted.
- Creation reports resolved paths, QEMU preparation, extraction progress,
  payload checks, dynamic-disk/GPT creation, verified partition imports and
  registration. Progress is flushed immediately, not buffered until completion.
- Missing QEMU EXEs prompt compilation. If only dependencies/ROMs are missing,
  `create`, `set --qemu` and `doctor --qemu` use that checkout's build receipt
  and matching MSYS2/pc-bios to add missing private files. EXEs are not replaced,
  conflicting/in-use partial runtimes refuse, and a complete runtime is a no-op.
  Users do NOT run `prepare.ps1`. Full compatible runtime directories need no receipt.
- The native SDL window is titled **KikiEmu** and receives the application's
  custom mascot icon. There is no second terminal or manager UI.
- Disk size is immutable TOTAL GiB and dynamically allocated, not preallocated.
  Installed systems remain independent of their downloaded ZIP. Uninstall keeps
  instances; `delete --force --id NN` explicitly removes only the owned instance.

## Frozen system and source identities

The system was rebuilt in an independent, pinned Android17 source/output tree;
no developer userdata, disk, initialized service partition or old support tarball
is included. The format-1 ZIP contains only boot/system/vendor and manifest,
source-lock and licenses; KikiEmu creates the persistent disk locally.

System ZIP: **834421223 bytes**; SHA-256
`73a184068b2c1b5576add96bcbadf4621bf3c3998c7f01fffeb624ee44aad42a`.

- AOSP branch: `android17-release`;1084 fixed project revisions, manifest SHA
  `388a6995d81aabeda16d60b39d31cc05154b2f5389d1598fee04e4577279b598`.
- Actual built device: `5693f5550c09fad1581158f2ed29d91d40f02a14`.
- Packaging tool: `500c49cc5cc52edf2c9fa13829574d2fb99b3545`.
- Contract: `5cfd0a94c1b267661d2ae96bdc3875365ad686cf`.
- Kernel recipe: `8984112088b8fa067fdd913950bfd76166102620`, Linux7.3-rc4,
  4KiB; actual kernel SHA
  `02c5439434feb3f1b5f5d63f82b2d3fbd6ccb8aa81f93d708f3d0f3bba9a7ce8`.

The producer's public tag includes subsequent packaging/documentation work;
it is NOT falsely claimed to be the earlier actual built device commit.
Final launcher build/setup hashes and exact source commit are in the release
assets' `release-provenance.json` and `SHA256SUMS.txt`.

## Verification scope

321 native ARM64 internal checks passed, including home paths, separators,
progress, rollback/ownership and automatic runtime preparation. Actual selected
QEMU EXE copies from upstream `f7ada39edacaa5c26b30e98b94017b0b2ccbcf94`
with the four recorded patches required49DLLs/43ROMs; preparation preserved
all EXE hashes and the second pass made no changes. No bare WHPX/GPU probe ran.

The real clean ZIP previously booted fresh32/200-GiB instances and preserved
data over graceful restart;200GiB also rebooted with its source ZIP unavailable.
This manager update additionally booted a fresh32-GiB owned internal instance
with Android17, Linux7.3.0-rc4-4k, VirGL GLES3.1,120Hz,1003x1556/288dpi and
Launcher HOME readiness. Physical2880x1920 Windows screenshots were inspected
after READY, not during the transient boot black screen. Internal runner,
fixtures, initialized disks and screenshots are NOT release payloads.

The user reported successful public `create` after correcting their path and
authorized mainline/publication. The new final setup and installed public CLI
are NOT run by the agent. Do not interpret internal tests as user-side
installation/PATH/shortcut/uninstallation or physical-keyboard acceptance.
See [tests/SYSTEM_REGRESSION.md](tests/SYSTEM_REGRESSION.md) for reproducible
internal system checks and their boundaries.

## Source, licensing and limitations

Additional source kits are for source/rebuild/relinking access, not required to
install or run. The launcher kit contains its exact project source, matching
MSYS2/NSIS source archives and recipes, notices, static libraries and application
object files. The system kit contains actual Linux sources/configuration,
kernel/device recipes, complete pinned manifest and archives for all
notice-identified copyleft components and platform build interfaces. Preserve
each upstream license; project GPL does not relicense everything. See
[LICENSING.md](LICENSING.md) and [LICENSE_NOTICE.md](LICENSE_NOTICE.md).

Tested platform: Windows11 ARM64 Surface Snapdragon, patched native QEMU/WHPX,
SDL/VirGL/120Hz,8vCPU/4GiB default. GPU acceleration is implemented; prior
animation benchmarks exceeded60FPS, but daily UI interaction still has latency.
This is not a promise of120FPS, zero-latency input, compatibility with every
ARM64 GPU, protected video or demanding commercial games. Camera preview is
accelerated but capture/conversion/JPEG still use CPU work. No Google services.

Unsigned Alpha installer: Windows may warn. Userdebug/test keys, permissive
experimental system settings and per-instance loopback ADB are for testing,
not a hardened security boundary. No disk resize, in-place OS update or silent
QEMU replacement. Future fixes use a new version/NEW test storage under the
same format-1 contract. Never replace published bytes under this tag.
