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

The launcher currently requires the three EXEs, their adjacent private DLLs
and `roms/`. This build-only script intentionally does not arrange deployment;
if those checks fail, keep the output and report the error for the next step.
Do not substitute an old runtime. Once compatible, select your directory:

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
[MSYS2 packages](https://www.msys2.org/docs/package-management/).
