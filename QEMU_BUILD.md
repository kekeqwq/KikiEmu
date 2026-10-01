# Build the compatible QEMU runtime — Windows ARM64

QEMU is user-provided. KikiEmu's setup installs the manager, desktop entry and
camera bridge; it does **not** include QEMU or an Android disk. The independent
KikiAOSP ZIP contains the kernel and system payloads, not QEMU.

These instructions describe the current 0.1 Alpha **candidate**, not a
published/accepted release. Use a separate checkout/build/bin directory for
development; never overwrite a runtime already configured for an instance.

## 1. Obtain the matching source

In the MSYS2 **CLANGARM64** terminal on native Windows ARM64:

```bash
git clone --branch feat/release-0_1-alpha https://github.com/kekeqwq/KikiEmu.git KikiEmu-alpha
cd KikiEmu-alpha
git checkout --detach 2130e523573d3304a3fa05b3c88e5dca79ff3ff5
```

The candidate source is frozen here; default `main` still represents the
accepted development baseline, not this candidate's manager/runtime contract.
Once an accepted immutable release tag exists, use its documented source
instead. Do not guess a newer upstream QEMU revision or omit a patch.

## 2. Install native build dependencies

Update MSYS2 with `pacman -Syu`, reopen the terminal if requested, and complete
the update before building. Use CLANGARM64, **not** UCRT64/x86_64 or WSL:

```bash
pacman -S --needed git make ninja python pkgconf \
  mingw-w64-clang-aarch64-clang mingw-w64-clang-aarch64-pkgconf \
  mingw-w64-clang-aarch64-glib2 mingw-w64-clang-aarch64-gtk3 \
  mingw-w64-clang-aarch64-SDL2 mingw-w64-clang-aarch64-libslirp \
  mingw-w64-clang-aarch64-pixman mingw-w64-clang-aarch64-zstd \
  mingw-w64-clang-aarch64-libepoxy mingw-w64-clang-aarch64-virglrenderer \
  mingw-w64-clang-aarch64-nlohmann-json mingw-w64-clang-aarch64-libarchive \
  mingw-w64-clang-aarch64-expat mingw-w64-clang-aarch64-cppwinrt
```

The JSON/libarchive/expat dependencies are also needed by the native internal
export inspector. Native QEMU/system testing requires Windows Hypervisor
Platform and the host's compatible hardware OpenGL path. VirGL's protocol
library is **not** an Adreno graphics driver. This Surface's tested path is
Mesa D3D12 to the Qualcomm GPU; the scripts do not install/change that driver
or host display settings. Other GPUs/PCs have not been certified.

## 3. Build all three QEMU tools

```bash
KIKI_BOOT_CONSOLE=1 KIKI_BUILD_JOBS=8 \
  ./tools/build_qemu_arm64.sh "$PWD/tools/qemu-alpha-user-src"
```

The script obtains official upstream QEMU and pins
`bde658eef6b38c45794bfd7ad4d2dd1b574e4694`. It checks the source tree is clean,
then applies these tracked patches **in order**:

1. `patches/qemu-kikiaosp-tested-surface-20260929.patch`
2. `patches/qemu-io-binary-source.patch`
3. `patches/qemu-sdl-boot-console.patch`
4. `patches/qemu-sdl-channel-title.patch`

The configure recipe explicitly enables native AArch64, WHPX, SDL, OpenGL,
VirGL and slirp. It builds `qemu-system-aarch64.exe`, `qemu-img.exe` and
`qemu-io.exe`. Re-running the initial patching script on its already patched
tree is intentionally refused. For an unchanged patched tree, incrementally
rebuild with:

```bash
ninja -C tools/qemu-alpha-user-src/build -j8 \
  qemu-system-aarch64.exe qemu-img.exe qemu-io.exe
```

Do not start a bare WHPX/GPU probe on this Surface. Test through the complete
paired kernel/system/SDL recipe after initialization, not an empty QEMU guest.

## 4. Export a private bin directory

Use PowerShell 7 at the same checkout. The PATH assignment below affects this
build terminal only; it is not a persistent host PATH edit:

```powershell
$env:PATH = 'C:\msys64\clangarm64\bin;' + $env:PATH
./tools/build_kikiemu_core.ps1 -RunUnitTests
./tools/export_qemu_runtime.ps1 `
  -BuildDirectory ./tools/qemu-alpha-user-src/build `
  -OutputBin "$HOME/Tools/KikiQemu-alpha/bin" `
  -InspectorPath ./build/kikiemu-core/kikiemu-runtime-inspect.exe
```

`OutputBin` must be new. The internal inspector/exporter does not install
KikiEmu or execute its public CLI. It collects native QEMU executables, the
static imported DLL closure and `roms/`, records hashes and validates that the
result does not depend on the developer's MSYS2 DLL search directory.

Keep the **entire** exported directory together. A copied EXE alone is not a
usable runtime. Byte/architecture/compiled-marker checks do not certify
WHPX, dynamically loaded host modules or hardware GPU compatibility; a real
complete system boot is still required.

## 5. Configure the installed manager

After the user installs the matching setup.exe, open a new PowerShell:

```powershell
kikiemu create --system ~/Downloads/KikiAOSP-0.1.0-alpha-arm64.zip --storage ~/MyAndroid --size 200g --qemu ~/Tools/KikiQemu-alpha/bin
kikiemu set --default 01
```

200g is only an example of immutable total GiB. QEMU's bin directory is a
required explicit binding; start revalidates its saved file identities and
does not fall back to PATH or an older developer QEMU. The tested 0.1 defaults
are 8 vCPU / 4 GiB, SDL/VirGL, 120-Hz guest rendering and 1003×1556 native pixels.

To select a separately built/exported compatible runtime for the next boot:

```powershell
kikiemu set --id 01 --qemu ~/Tools/KikiQemu-next/bin
```

Do not modify an in-use runtime in place. QEMU rebuilding/rebinding is not an
Android reinstall or a disk resize. See [QUICK_START.md](QUICK_START.md) for
management, scoped ADB, force-delete and uninstall behavior.
