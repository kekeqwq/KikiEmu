// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "runtime.hpp"

namespace kiki {
struct Partition {
    std::string name;
    std::string uuid;
    uint64_t offsetBytes;
    uint64_t lengthBytes;
};
struct DiskLayout {
    std::string uuid;
    uint64_t totalBytes;
    std::vector<Partition> partitions;
};
DiskLayout plan_disk(uint64_t totalBytes, const std::map<std::string, uint64_t>& payloadBytes,
                     uint64_t minimumDataBytes);
nlohmann::json layout_json(const DiskLayout& layout);
uint32_t crc32(const unsigned char* data, size_t size);

// Only caller-verified installation materials are imported. This routine
// creates a NEW standalone qcow2; it cannot resize/reformat an existing disk.
// It does not register a manager instance or boot a VM.
nlohmann::json install_disk(const nlohmann::json& qemuBinding, const fs::path& newDirectory,
                           uint64_t totalBytes, const std::map<std::string, fs::path>& payloads,
                           uint64_t minimumDataBytes);
} // namespace kiki
