# Build QEMU — Windows ARM64

> **0.2 Alpha requires rebuilding QEMU with the revised five-patch recipe.**
> A 0.1 runtime can still exhibit black-screen/idle-overlay behavior even after
> updating setup.exe. Use a fresh checkout (for example `~/Repos/QEMU-0.2`),
> not a mixed 0.1 patch/build cache. The actual 0.2 tested upstream is
> `f7ada39edacaa5c26b30e98b94017b0b2ccbcf94`; the historical recommendation
> below could not be fetched during the clean 0.2 rebuild. Patches and the
> script are available under immutable tag `v0.2.0-alpha`.
> Follow the exact update commands in [0.2 release notes](RELEASE_0_2_ALPHA.md).

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

Invoke-WebRequest 'https://raw.githubusercontent.com/kekeqwq/KikiEmu/main/build.ps1' -OutFile ./build.ps1
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
installs only missing dependencies, verifies and applies the five fixed patches,
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
The verified earlier four-patch recipe can receive the fifth managed-close
fix incrementally, without resetting source or deleting build output. Stop
your instance before recompiling, download the updated build.ps1 and rerun it;
then use `kikiemu set --id 01 --qemu <your bin directory>` to revalidate it.
This fix is required: old binaries do not forward the SDL close button to
Android's orderly shutdown. Closing the window must save/unmount guest data,
not cut power. Only explicit `delete --force` may force-stop a guest.
It does **not** copy/export DLLs or ROMs, move/package output, install KikiEmu,
modify persistent host settings, or start QEMU. Actual build testing is yours.

The user completed native ARM64 compilation on upstream HEAD
`f7ada39edacaa5c26b30e98b94017b0b2ccbcf94`. A different checkout/GPU still
requires system testing; successful compilation is not hardware certification.

## Let KikiEmu prepare the runtime

After compilation, pass `<QEMU source>/bin` directly to `kikiemu create`.
You do **not** download or run prepare.ps1. The manager checks the three EXEs,
their native ARM64 static DLL dependency closure and private ROM directory.

Missing EXEs produce a compile-first error. A complete runtime is used without
writing files, even when it lives outside the source checkout. If only private
DLLs/ROMs are missing, KikiEmu reads `.kiki-qemu-build/state.json` in the matching
source root and uses its recorded **MSYS2 installation**, not a hardcoded path.
It copies only missing private imports from CLANGARM64 and binary ROMs from
that checkout's `pc-bios` into `bin/roms`, verifies SHA-256, then validates the
complete private runtime. Saved launch bindings do not depend on MSYS2/PATH.

Keep the build receipt, source tree and matching MSYS2 dependency versions
until preparation finishes. No packages are installed, EXEs replaced, source
reset, host settings changed or QEMU launched by preparation. Conflicting files,
external links, unknown build sources and an in-use incomplete bin refuse.
Do not substitute old runtimes to bypass these checks. prepare.ps1 remains a
developer diagnostic only, not an end-user prerequisite.

The launcher requires the three EXEs, adjacent private DLLs and `roms/`.
Select your own directory; create prints preparation and installation logs:

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
