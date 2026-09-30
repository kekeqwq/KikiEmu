// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "runtime.hpp"
#include <array>

namespace kiki {
struct BootHeader {
    uint64_t kernelBytes, ramdiskBytes, ramdiskOffset, fileBytes;
};
// Supported ABI: header v4, 4-KiB pages, no signature/vendor_boot, no
// package-supplied command line. Launch arguments belong to the manager.
BootHeader parse_boot_header(const std::array<unsigned char, 4096>& header, uint64_t fileBytes);
BootHeader validate_boot_payload(const fs::path& payload);
nlohmann::json derive_boot_cache(const fs::path& bootPayload, const fs::path& newDirectory);
} // namespace kiki
