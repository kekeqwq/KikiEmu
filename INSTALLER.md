# Installer build and safety contract

The installer is built, not executed, by the developer. Public CLI/desktop and
setup acceptance belongs to the user. Building it does not change PATH,
shortcuts, installed applications, instance records or Windows settings.

The standard NSIS 3.13 bootstrap is **x86 Unicode**, which Windows ARM64 runs
under its compatibility layer for installation only. This is not an ARM64
installer-stub claim. All installed manager/desktop/setup-helper/camera
executables and private zlib runtime are **native ARM64**. QEMU remains a
separate, user-provided native ARM64 runtime. NSIS's official binary ZIP
contains x86 stubs; its preliminary ARM64 tooling support does not certify a
native ARM64 stub. See [NSIS command-line documentation](https://nsis.sourceforge.io/Docs/Chapter3.html).

Build with MSYS2's CLANGARM64 compiler available in the build process PATH:

```powershell
./tools/build_kikiemu_core.ps1 -RunUnitTests
./tools/build_surface_camera_bridge.ps1 -OutputPath build/kikiemu-core/surface-camera-bridge.exe
./tools/build_kikiemu_setup.ps1 -CoreDirectory build/kikiemu-core -NsisDirectory build/installer-tools/nsis-3.13/nsis-3.13 -OutputDirectory build/kikiemu-setup-candidate
```

Use the official NSIS ZIP pinned by `installer/nsis-toolchain.json`. Extract it
to a build-tools directory; no NSIS installation is necessary. Never use
`makensis /LAUNCH` or invoke generated setup.exe during build verification.
The packager enumerates only product-owned executables/assets and dependency
licenses, verifies ARM64 imports, and records source/payload/setup hashes.
Internal tests, synthetic ZIPs, QEMU, Android images/disks and developer logs
are not installer payloads.

`installer/kikiemu.nsi` uses a fixed current-user application directory, no
elevation and English text. Every installed entry point holds a shared read
lease on `install.lock` for its entire lifetime; setup/uninstall requires an
exclusive read/write lease BEFORE replacing/removing app files. This closes
the start/update race without process-name kills or permission changes.
Uninstall enumerates only its owned files; unknown files keep the directory
nonempty. It never recursively removes a user-selected storage directory.

The native private helper reads the complete user PATH, preserves its registry
type and unrelated segments, refuses malformed/oversized values, avoids
duplicate equivalent entries and journals ownership BEFORE adding its exact
segment. Uninstall removes a segment only when this installer owns it; an
existing user PATH entry is not adopted. It broadcasts the standard environment
change notification; open a NEW shell to receive the updated PATH. The helper
is not exposed as a public manager command and is not run by build tests.

Uninstall keeps `%LOCALAPPDATA%/KikiEmu/release` and all instances. Permanent
data deletion is exclusively the explicit `--delete --force --id` command.
There is no automatic update or implicit disk migration in 0.1.

Binary redistribution still requires the matching corresponding source/build
inputs and full third-party license audit. Self-authored source is
GPL-2.0-or-later, not a relicensing of AOSP, Windows APIs or dependencies.
Candidate compilation is not approval to publish an incomplete license/source
set. Real user installer acceptance and clean system acceptance remain gates.
