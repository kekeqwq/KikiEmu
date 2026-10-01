# KikiEmu / KikiAOSP 0.1 Alpha candidate — 2026-10-01

This record identifies a **test candidate**, not an accepted public release.
Do not replace published asset bytes or treat internal system testing as the
user's installer/CLI acceptance. The binaries below were built from the stated
frozen commits; later documentation commits do not change that provenance.

## Current R8 compiled candidate

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `setup.exe` (R8) | 2,074,660 | `b1a6b5100838ecbbf0aa27d5a54e70afb3ae268ac68ba0ac6dc1a28548af36fe` |
| `KikiAOSP-0.1.0-alpha-arm64.zip` | 834,421,223 | `73a184068b2c1b5576add96bcbadf4621bf3c3998c7f01fffeb624ee44aad42a` |
| `KikiEmu-source-2130e52.zip` | 2,175,472 | `28f76f1a497a6108507ae0af3435b03e68d772472eb4f313181a71649ec284d2` |

R8 native source is `2130e523573d3304a3fa05b3c88e5dca79ff3ff5`, built from a
clean detached checkout. Both completed native receipts match that commit;
306 isolated internal checks passed. Static installer extraction verified
nine key embedded component/license hashes and excluded disks, QEMU and
internal test tools. **setup.exe has not been executed or installed.**

R8's independently installed 32 GiB clean system reached real HOME, preserved
its written data marker through normal reboot, retained exact block/vold/
StorageStats capacity and 120-Hz rendering, and passed **two** graceful
shutdowns with a successful manager result and complete owned cleanup.
The independently installed 200 GiB clean system reached HOME, passed the
same exact capacity checks and its first graceful shutdown. Its normal
persistent reboot also reached READY with the original ZIP path unavailable;
the data marker, exact capacity and 120-Hz field survived. The original ZIP
was then restored and its SHA-256 rechecked. This does not certify the
remaining system/publication matrix.

R8's 200 GiB reboot additionally passed an actual virtual-Ethernet external
ping, rear/front preview switching, two saved JPEGs, and camera release on
returning HOME. Its native camera bridge was rebuilt with R8 and has SHA-256
`8341ca879424681d3b08adbbe3f2f1bd0097a4cdc262b4c7125bdb662b8f124b`;
these are new R8 observations, not assumed byte identity with R6. The sampled
crash buffer was empty. Ringtone selection produced an active 48-kHz speaker
output with AudioFlinger frames advancing from 925,696 to 1,024,000; this is
output-pipeline evidence, not a new human audio-quality acceptance.

R8 also changed actual guest rendering to2784×1875 on maximization and restored
1003×1556 afterward, preserving SystemUI PID775 and the120.00001 active field.
The full physical Windows captures show complete HOME rather than a scaled
corner. Ordinary Platform Tools ADB was independently connected to this
session's51590 port: `adb devices -l` reported the expected KikiAOSP0.1Alpha
device, and an explicitly targeted shell returned its exact serial,
`sys.boot_completed=1` and `7.3.0-rc4-4k`. The built-in transport does not
automatically register a device in the Platform Tools server; see QUICK_START.

The same200-GiB reboot remained running for more than30minutes, including the
network/camera/audio/resize tests. A later live sample recorded guest uptime
2,221.96seconds, unchanged system_server/SystemUI/Launcher PIDs528/775/963,
1003×1556 and120.00001, with no entries in the sampled crash/ANR event buffers.
This is observed runtime stability, not a continuous FPS/latency guarantee.
The test window is retained for the user; it has not been closed for handoff.

The companion/remote user guide includes the later ADB instructions and the
user's revised development policy. R8's embedded guide and archived source
remain the actual frozen2130e52 inputs; the documentation update does not
pretend these already compiled bytes came from a later source commit.

## R6 artifacts (historical, not the final handoff)

Real shutdown exposed an exit/query race in R6: Android and its owned runtime
exited cleanly, but the manager reported that it could not verify the process
image. R6 is retained for diagnosis, not recommended for installation. The
successor will be rebuilt from a new clean commit and recorded separately;
do not pretend the following R6 bytes include that fix.

The R7 successor's actual shutdown test found the additional teardown interval
before the process object signals exit. R7 also remains a diagnostic candidate,
not the final handoff. A bounded wait on the already held process handle is
required; neither a missing image nor an observation timeout is exit evidence.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `setup.exe` (R6) | 2,076,147 | `ad506a9396cfcd3fddd6c76d961a5fb5076454a68f7801df0ac38b34b176f45a` |
| `KikiAOSP-0.1.0-alpha-arm64.zip` | 834,421,223 | `73a184068b2c1b5576add96bcbadf4621bf3c3998c7f01fffeb624ee44aad42a` |
| `KikiEmu-source-278ad65.zip` | 2,169,601 | `564caeb964f0237ccc5c6bb1c465634838127f57102de3d9084cd28eace8819c` |

The source ZIP contains this repository's tracked source, **not** the complete
corresponding-source closure of all bundled third-party dependencies.

- Launcher/native camera source: `278ad65f2b1d0a92b918c706813461a3ec0e8d06`.
- Device source actually built: `5693f5550c09fad1581158f2ed29d91d40f02a14`.
- Producer packager source: `500c49cc5cc52edf2c9fa13829574d2fb99b3545`.
- Kernel source: `8984112088b8fa067fdd913950bfd76166102620` (`7.3-rc4`, 4 KiB).
- Frozen AOSP manifest SHA-256:
  `388a6995d81aabeda16d60b39d31cc05154b2f5389d1598fee04e4577279b598`.
- Format-1 contract revision: `5cfd0a94c1b267661d2ae96bdc3875365ad686cf`.
- Patched QEMU upstream revision: `bde658eef6b38c45794bfd7ad4d2dd1b574e4694`.
  The tested exported `qemu-system-aarch64.exe` SHA-256 is
  `35c91f77c8ae783295b4ab98aae23989d79a74dcc01126b225d5f46157fb84fc`.
  This identifies tested bytes, not a guarantee for other PCs/drivers.

The Android package came from a separate clean pinned AOSP source tree and
independent output. It has nine members: manifest, boot/system/vendor payloads,
source lock and four license/source-notice files. It contains no initialized
disk, userdata, QEMU, developer logs, keys or screenshots. The kernel is inside
the boot payload. Full EROFS extraction and actual release property checks
preceded canonical package validation.

## Historical R6 native build checks

The five installed native runtime components are ARM64. NSIS 3.13 supplies the
standard installer stub. Both native build receipts report the same clean
frozen commit, completed compilation and matching output hashes; 296 isolated
internal checks passed. Static extraction verified nine key embedded payload
and license hashes. Neither setup.exe nor public CLI/desktop entry points were
executed by the agent. User PATH, shortcuts and installed instances were not
modified by these checks.

## Historical R6 clean-system evidence

An internal system-only runner used the real ZIP and the complete paired
WHPX/ARM64/SDL/VirGL recipe, not a metadata fixture or bare QEMU probe.

For the independently created **32 GiB** instance:

- Fresh boot and normal persistent reboot reached real Android 17, release
  channel, kernel `7.3.0-rc4-4k`, exact instance-specific release serial and
  `Asia/Shanghai` timezone. A written data marker survived the reboot.
- Kernel block size, vold and StorageStats agree on **34,359,738,368 bytes**.
  Settings shows **34 GB total / about 1.8 GB used**. Android uses decimal GB;
  `32g` in the CLI means GiB. Initial `/data` use was approximately 502 MiB.
  The initial host QCOW2 was 1,250,689,024 bytes, not 32 GiB preallocated.
- The native window has no Grab or extra console. Baseline is 1003×1556,
  288 dpi, font scale 1.5, 8 vCPU and 4 GiB. Renderer reports VirGL GLES 3.1.
  The active refresh field is `120.00001`, a period/float representation of
  120 Hz, not a claim that all applications present 120 frames per second.
- Maximization changed actual Android rendering to 2784×1875; restoration
  returned to 1003×1556. SystemUI retained its process and no Java fatal
  exception or ANR was observed in the sampled resize logs.
- Virtual Ethernet obtained 10.0.2.15; two external ping packets succeeded.
- Both real Surface cameras streamed distinct frames, Camera2 switched from
  rear to front, and both produced saved JPEGs in **Pictures**. Returning HOME
  removed active camera clients; the bridge acknowledged device release.
- RingtonePicker opened and selecting a tone created an active speaker output
  with increasing AudioFlinger frame counts. This is pipeline evidence, not
  independent human confirmation of sound quality or absence of exit pops.
- Full physical 2880×1920 Windows desktop captures were inspected privately.
  Raw captures/photos contain private content and are not distribution inputs.

Two command-line Java diagnostic tools (`uiautomator dump` and `content query`)
aborted in this guest; their native crash records are retained for diagnosis.
Do not label those commands supported because the UI itself works. Guest
`input`/`am`/`dumpsys`/private shell-v2 commands used above succeeded. Actual
host touch and physical keyboard acceptance is separate from guest-injected
test actions.

The separately created **200 GiB** instance also reached real SDL/VirGL/120-Hz
Android. Kernel, vold and StorageStats agree on **214,748,364,800 bytes**.
Settings shows **215 GB total / about 2.6 GB used**, not a fabricated fixed
system allocation. Initial `/data` use was 1,355,184 KiB, including real F2FS
overhead for this larger volume. Its data marker was written and synchronized;
R6's reboot result is not claimed here. R8's separate successful reboot is
recorded above; the two fixtures and native candidates are not interchangeable.

## Instructions and outstanding gates

- [User guide](QUICK_START.md): create/list/default, resource/QEMU settings,
  start, scoped ADB, explicit force-delete and uninstall semantics.
- [QEMU build guide](QEMU_BUILD.md): upstream QEMU only, recommended verified revision,
  standalone PowerShell download, MSYS2 package checks and four fixed patches.
  This new build-only entry leaves output in the checkout's bin; its actual
  user build test and any runtime deployment follow-up are still pending.
- [README](README.md): the same standalone Windows build workflow; no KikiEmu
  clone or internal inspector is required by an end user.
- [Installer build](INSTALLER.md): clean native receipts and setup assembly.
- [System regression](tests/SYSTEM_REGRESSION.md): real-system test boundaries
  and required evidence.
- [Release plan](RELEASE_PLAN.md): immutable contracts and publication gates.

User installation/uninstallation, PATH/shortcuts and public management flows
remain user-owned acceptance work. The full multi-capacity clean-system matrix,
complete interrupted-creation recovery and third-party corresponding-source/
license closure are not all certified by the checks above. On2026-10-01 the
user explicitly withdrew strict release/Dev isolation and chose same-format
versioned package development. The former legacy-Dev guard/concurrency matrix
has not passed and is no longer a candidate/publication gate; basic instance
safety checks remain. No public Release/tag or mainline merge is authorized
by this record alone.
