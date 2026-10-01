# Licensing

New self-authored KikiEmu manager/core code and build/export tools marked
`SPDX-License-Identifier: GPL-2.0-or-later` are licensed under GNU GPL version 2
or, at your option, a later version. The full GPL version 2 text is in [LICENSE](LICENSE)
and version 3 is in [LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt). The combined
manager/desktop binary distribution selects GPLv3; the reason and limits of
that selection are in [LICENSE_NOTICE.md](LICENSE_NOTICE.md).

This does not relicense upstream QEMU, AOSP, Linux, Microsoft platform code,
libraries, artwork, fonts or other third-party material. Preserve their own
copyright notices and license terms. Patches to an upstream work remain subject
to that work's applicable license. A GPL declaration alone is not a complete
binary-distribution compliance audit.

The native core uses nlohmann/json, libarchive, expat and their static
compression/crypto/iconv/regex closure, as well as Windows system APIs. Its
current build also imports a private native zlib DLL; the camera bridge uses
MinGW/LLVM/C++/WinRT and Windows system libraries. Preserve the original
per-file/component notices rather than assuming all dependencies share one
license. In particular the static closure includes Apache-2.0 OpenSSL 3,
so the GPLv2-only interpretation is not appropriate for the combined binary.
Public artifacts must include the corresponding dependency notices and
the required corresponding source/build/relinking inputs before release. A
ZIP of this repository's own source is not that complete closure. QEMU is
user-provided; the documented runtime export is a local development tool, not
a claim that its output already satisfies every redistribution requirement.

The 0.1 Alpha release supplies a separate source kit: exact project sources,
installed MSYS2 dependency source archives/recipes, matching NSIS sources,
component notices, application object files and static libraries. Use its
`relink_launcher.ps1` with MSYS2 CLANGARM64; `-LibraryDirectory` places compatible
replacement libraries before the supplied libraries. Relinking creates new
executables and does not execute/install them. The project source ZIP inside
the kit also provides full recompilation and installer build recipes. Preserve
all original notices when rebuilding or redistributing.

The KikiAOSP release separately supplies its actual kernel source, pinned
kernel configuration/patches, built device tree, AOSP manifest and source
archives for notice-identified copyleft components/build interfaces. See
[the release record](RELEASE_0_1_ALPHA.md). This is a documented distribution
inventory, not a blanket relicensing or a guarantee about every future
dependency/version; repeat the inventory when inputs change.
