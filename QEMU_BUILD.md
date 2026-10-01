# Build QEMU — Windows ARM64

QEMU, KikiEmu's `setup.exe`, and the KikiAOSP system ZIP are independent.
**Do not clone KikiEmu to build QEMU.** You only need Git, PowerShell 7 and
an updated [MSYS2](https://www.msys2.org/) installation on Windows ARM64.

## Clone QEMU, download the script, build

Run in **PowerShell 7**. The checkout location is yours to choose; upstream
QEMU requires a source/build path without spaces. `~/Repos/qemu` is an example.

```powershell
git clone https://gitlab.com/qemu-project/qemu.git ~/Repos/qemu
if ($LASTEXITCODE -ne 0) { throw 'QEMU clone failed.' }
cd ~/Repos/qemu

Invoke-WebRequest 'https://raw.githubusercontent.com/kekeqwq/KikiEmu/2b4fe858b5bde615275949001ee6d0433cb76fa7/build.ps1' -OutFile ./build.ps1
./build.ps1 --msys2 'C:\msys64'
```

Already cloned? Start at `cd`, using your own checkout path. Replace
`C:\msys64` with your MSYS2 installation, for example `C:\msys2`.
`-Msys2` also works; parallelism defaults to 8 and can be changed with `-Jobs 4`.

Recommended, **not required**: use the verified upstream commit
`bde658eef6b38c45794bfd7ad4d2dd1b574e4694`. To opt into it before building:

```powershell
git checkout --detach bde658eef6b38c45794bfd7ad4d2dd1b574e4694
```

Other commits are allowed. The script prints a compatibility warning and
continues with your current HEAD; the fixed patches or compilation may fail.
It never checks out or locks an upstream revision for you.

The script checks installed packages, skips installation when none are missing,
installs only missing dependencies, verifies and applies the four fixed patches,
then builds native ARM64 QEMU with WHPX, SDL, GTK, OpenGL, VirGL and slirp.
It uses the supplied MSYS2's CLANGARM64 tools, never WSL. QEMU's shell-based
configure is called internally; there is no separate user `.sh` step.
Build stdout and stderr are displayed live; failures include the last diagnostic
lines rather than only a child-process exit code.

## Output

The original build output stays directly in **`<QEMU source>/bin`**:

- `qemu-system-aarch64.exe`
- `qemu-img.exe`
- `qemu-io.exe`
- Normal QEMU/Meson/Ninja intermediate files.

Rerun the same command for an incremental build. Verified patches and a build
receipt stay in `.kiki-qemu-build/`. The script refuses unrelated source changes
or an unmanaged nonempty `bin`; it does not reset source or clean old files.
The receipt records your actual HEAD; use a new checkout if changing revisions
after patching rather than reusing stale patch/build state.
It does **not** copy/export DLLs or ROMs, move/package output, install KikiEmu,
modify persistent host settings, or start QEMU. Actual build testing is yours.

The corrected script was tested locally on upstream HEAD
`f7ada39edacaa5c26b30e98b94017b0b2ccbcf94`: native ARM64 configuration passed
and Ninja started real C compilation. At the user's request the build was
stopped with intermediates retained. Full compilation and runtime acceptance
are still user-owned tests, not claimed as completed by this check.

## Prepare the runtime (separate from building)

After the build completes, remain in the **same QEMU checkout root** and run:

```powershell
Invoke-WebRequest 'https://raw.githubusercontent.com/kekeqwq/KikiEmu/feat/release-0_1-alpha/prepare.ps1' -OutFile ./prepare.ps1
./prepare.ps1 --msys2 'C:\msys64'
```

This standalone script does not require a KikiEmu checkout. It reads the three
compiled EXEs with CLANGARM64's `llvm-readobj`, recursively checks their normal
and delay-loaded imports for native ARM64, and copies only missing private DLLs
from your **matching MSYS2 installation** into `bin`. Windows system DLLs are
not copied. It also copies matching binary ROM files from this checkout's
`pc-bios` into `bin/roms`. Do not uninstall/update the build's dependencies
between compilation and preparation.

It never recompiles, launches QEMU, replaces EXEs, moves output, installs
packages or changes PATH/host settings. Existing identical files are skipped;
conflicting files cause an error, not an overwrite or cleanup. Close any
process using that QEMU bin first. Use a new bin/build for another version;
never copy old QEMU EXEs or unrelated DLLs to bypass an error. Repeating the
command with the same sources is safe. Optional `-BinDirectory 'D:\Qemu\bin'`
(or `--bin`) prepares another directory already containing the three matching
EXEs; it does not export those EXEs for you.

This resolves `QEMU bin is missing a private DLL: zlib1.dll` after a successful
build; copying just zlib is insufficient because imports are recursive. The
user's completed `f7ada39e` build was checked **using isolated EXE copies**:
49 private DLLs and 43 ROMs were prepared, then the launcher's native runtime
validator passed without a fallback dependency path. All three EXE hashes
were unchanged. This is static runtime validation, not a new boot/GPU test;
the user's original bin was not modified during these checks.

The launcher requires the three EXEs, adjacent private DLLs and `roms/`.
Once prepared, select your own directory:

```powershell
kikiemu create --system ~/Downloads/KikiAOSP-0.1.0-alpha-arm64.zip --storage ~/MyAndroid --size 200g --qemu ~/Repos/qemu/bin
kikiemu set --id 01 --qemu D:/MyQemu/bin
```

No fixed directory layout is required. Do not rebuild an in-use runtime.
If MSYS2 needs an update, complete `pacman -Syu` in its terminal before retrying;
the build script does not perform a whole-installation upgrade. Patch source is
fixed at `2130e523573d3304a3fa05b3c88e5dca79ff3ff5` and each SHA-256 is checked.
Host GPU drivers/WHPX setup are outside this script; other PCs are not certified.

References: [QEMU build system](https://www.qemu.org/docs/master/devel/build-system.html),
[MSYS2 environments](https://www.msys2.org/docs/environments/),
[MSYS2 packages](https://www.msys2.org/docs/package-management/),
[LLVM object reader](https://llvm.org/docs/CommandGuide/llvm-readobj.html).
