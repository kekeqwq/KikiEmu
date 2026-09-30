# Licensing

New self-authored KikiEmu manager/core code and build/export tools marked
`SPDX-License-Identifier: GPL-2.0-or-later` are licensed under GNU GPL version 2
or, at your option, a later version. The full GPL version 2 text is in [LICENSE](LICENSE).

This does not relicense upstream QEMU, AOSP, Linux, Microsoft platform code,
libraries, artwork, fonts or other third-party material. Preserve their own
copyright notices and license terms. Patches to an upstream work remain subject
to that work's applicable license. A GPL declaration alone is not a complete
binary-distribution compliance audit.

The native core currently uses nlohmann/json headers (MIT) and Windows system
APIs. Public artifacts must include the corresponding dependency notices and
the required corresponding source/build inputs before release. QEMU is
user-provided; the documented runtime export is a local development tool, not
a claim that its output already satisfies every redistribution requirement.

No system ZIP, installer, third-party binary bundle or mascot is published by
this implementation-stage commit. Release license/provenance auditing remains
a publication gate in [RELEASE_PLAN.md](RELEASE_PLAN.md).
