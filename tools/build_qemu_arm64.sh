#!/usr/bin/env bash
set -euo pipefail

if [[ ${MSYSTEM:-} != CLANGARM64 ]]; then
    echo 'Run this script from the MSYS2 CLANGARM64 terminal on Windows ARM.' >&2
    exit 2
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

repo_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_dir=${1:-"$repo_root/tools/qemu-src"}
qemu_rev=5f664cd37aec17e8145aa117d8da68f507edc8f1
upstream=https://gitlab.com/qemu-project/qemu.git

if [[ ! -e $source_dir ]]; then
    git clone --filter=blob:none "$upstream" "$source_dir"
fi
[[ -d $source_dir/.git ]] || { echo "Not a QEMU Git checkout: $source_dir" >&2; exit 2; }
if [[ -n $(git -C "$source_dir" status --porcelain) ]]; then
    echo "QEMU checkout is not clean; use a new source directory: $source_dir" >&2
    exit 2
fi

if ! git -C "$source_dir" cat-file -e "$qemu_rev^{commit}"; then
    git -C "$source_dir" fetch origin "$qemu_rev"
fi
git -C "$source_dir" checkout --detach "$qemu_rev"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-windows-gtk-full-redraw.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-windows-gtk-full-redraw.patch"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-windows-arm64-gtk-touch.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-windows-arm64-gtk-touch.patch"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-windows-gtk-glarea-wgl.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-windows-gtk-glarea-wgl.patch"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-windows-sdl-gl-wgl.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-windows-sdl-gl-wgl.patch"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-windows-gtk-glarea-native-pixels.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-windows-gtk-glarea-native-pixels.patch"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-gtk-native-resize-touch-focus.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-gtk-native-resize-touch-focus.patch"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-gtk-global-fps-profile.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-gtk-global-fps-profile.patch"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-gtk-guest-refresh-rate.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-gtk-guest-refresh-rate.patch"
git -C "$source_dir" apply --recount --check "$repo_root/patches/qemu-windows-gtk-resize-commit-on-release.patch"
git -C "$source_dir" apply --recount "$repo_root/patches/qemu-windows-gtk-resize-commit-on-release.patch"
git -C "$source_dir" apply --check "$repo_root/patches/qemu-sdl-ignore-host-key-repeat.patch"
git -C "$source_dir" apply "$repo_root/patches/qemu-sdl-ignore-host-key-repeat.patch"

mkdir -p "$source_dir/build"
(
    cd "$source_dir/build"
    ../configure \
        --cpu=aarch64 \
        --target-list=aarch64-softmmu \
        --enable-whpx \
        --enable-gtk \
        --enable-slirp \
        --disable-werror \
        --disable-docs \
        --disable-dbus-display
)
ninja -C "$source_dir/build" qemu-system-aarch64.exe
"$source_dir/build/qemu-system-aarch64.exe" --version
echo "QEMU ready: $source_dir/build/qemu-system-aarch64.exe"
