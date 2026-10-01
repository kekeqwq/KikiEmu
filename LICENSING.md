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

An installer candidate has been compiled, not installed or publicly released;
the actual clean system ZIP is still being built. Copying notices, selecting
GPLv3 and recording binary hashes do not establish completion of the full
third-party source/license audit. Release license/provenance auditing remains
a publication gate in [RELEASE_PLAN.md](RELEASE_PLAN.md).
