// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "config.hpp"
#include <nlohmann/json.hpp>

namespace kiki {
struct PeInfo {
    uint16_t machine = 0;
    std::vector<std::string> imports;
};
PeInfo inspect_pe(const fs::path& file);
std::string sha256(const fs::path& file);

// Default inspection accepts dependencies ONLY beside the configured EXEs
// or from Windows system DLLs. Additional sources are BUILD EXPORT inputs,
// never launcher fallback directories. Nothing here starts QEMU or loads DLLs.
nlohmann::json inspect_qemu(const fs::path& bin,
                           const std::vector<fs::path>& buildDependencySources = {});
void verify_qemu_binding(const nlohmann::json& binding);
} // namespace kiki
