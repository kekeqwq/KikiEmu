#!/usr/bin/env bash
set -euo pipefail

if [[ ${MSYSTEM:-} != CLANGARM64 ]]; then
    echo 'Run this script from the MSYS2 CLANGARM64 terminal on Windows ARM.' >&2
    exit 2
fi

# A minimal MSYS2 installation may only have Git for Windows available.
if ! command -v git >/dev/null && [[ -x '/c/Program Files/Git/cmd/git.exe' ]]; then
    export PATH="/c/Program Files/Git/cmd:$PATH"
fi

# A machine with several MSYS2 environments can leak UCRT64 pkg-config paths
# into CLANGARM64. Pin dependency discovery to this native ARM64 prefix so
# Meson never combines an x86_64 GLib config with the AArch64 compiler.
# Meson's native Python launches Windows pkg-config directly, so give it a
# Windows-form path rather than an MSYS path that becomes an unparseable C:/….
mingw_prefix=${MINGW_PREFIX:-/clangarm64}
pkgconfig_libdir=$(cygpath -w "$mingw_prefix/lib/pkgconfig")
pkgconfig_sysroot=$(cygpath -w /)
export PKG_CONFIG_LIBDIR="$pkgconfig_libdir"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export PKG_CONFIG_SYSROOT_DIR="$pkgconfig_sysroot"
[[ -x "$mingw_prefix/bin/pkgconf.exe" ]] || {
    echo 'Install mingw-w64-clang-aarch64-pkgconf; do not use MSYS/x86 pkg-config with Windows-form paths.' >&2
    exit 2
}
export PKG_CONFIG=$(cygpath -w "$mingw_prefix/bin/pkgconf.exe")

repo_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_dir=${1:-"$repo_root/tools/qemu-boot-src"}
qemu_rev=bde658eef6b38c45794bfd7ad4d2dd1b574e4694
upstream=https://gitlab.com/qemu-project/qemu.git

if [[ ! -e $source_dir ]]; then
    git clone --filter=blob:none "$upstream" "$source_dir"
fi
git -C "$source_dir" rev-parse --is-inside-work-tree >/dev/null || {
    echo "Not a QEMU Git checkout/worktree: $source_dir" >&2
    exit 2
}
if [[ -n $(git -C "$source_dir" status --porcelain) ]]; then
    echo "QEMU checkout is not clean; use a new source directory: $source_dir" >&2
    exit 2
fi

if ! git -C "$source_dir" cat-file -e "$qemu_rev^{commit}"; then
    git -C "$source_dir" fetch origin "$qemu_rev"
fi
git -C "$source_dir" checkout --detach "$qemu_rev"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-kikiaosp-tested-surface-20260929.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-kikiaosp-tested-surface-20260929.patch"
if [[ ${KIKI_BOOT_CONSOLE:-1} == 1 ]]; then
    git -C "$source_dir" apply --check "$repo_root/patches/qemu-sdl-boot-console.patch"
    git -C "$source_dir" apply "$repo_root/patches/qemu-sdl-boot-console.patch"
fi

mkdir -p "$source_dir/build"
(
    cd "$source_dir/build"
    ../configure \
        --cpu=aarch64 \
        --target-list=aarch64-softmmu \
        --enable-whpx \
        --enable-gtk \
        --enable-sdl \
        --enable-opengl \
        --enable-virglrenderer \
        --enable-slirp \
        --disable-werror \
        --disable-docs \
        --disable-dbus-display
)
ninja -C "$source_dir/build" -j "${KIKI_BUILD_JOBS:-8}" qemu-system-aarch64.exe
"$source_dir/build/qemu-system-aarch64.exe" --version
echo "QEMU ready: $source_dir/build/qemu-system-aarch64.exe"
