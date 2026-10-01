# KikiEmu binary distribution license selection

KikiEmu's self-authored source files marked `GPL-2.0-or-later` remain under
that license. The complete version 2 terms are in `LICENSE`, and version 3
terms are in `LICENSES/GPL-3.0.txt`.

For the combined native manager/desktop binary distribution, we select
**GNU GPL version 3**, exercising the source's "or later" option. The static
dependency closure includes OpenSSL 3 under Apache-2.0; the Apache and GNU
projects explain compatibility with GPLv3, not GPLv2. See the
[Apache compatibility statement](https://www.apache.org/licenses/GPL-compatibility.html)
and [GNU license list](https://www.gnu.org/licenses/license-list.html#apache2).

This is not a blanket relicensing of third-party sources or independently
bundled programs. Keep their original license/copyright notices, including
the NSIS bootstrap, MSYS2/MinGW/LLVM runtimes and compression/crypto/XML/JSON
libraries. The Surface camera bridge is separately built from its recorded
source/dependencies. User-provided QEMU and the independent KikiAOSP/kernel
release retain their own licensing obligations.

Before a public binary release, provide its matching corresponding source,
recorded build inputs/recipes and dependency source/relinking materials as
required by the licenses. The manager's own Git source ZIP alone is not the
complete corresponding-source closure. Copying license texts or compiling
an installer does not establish that this review is finished. Candidate
testing is not authorization to publish an incomplete source/license set.
