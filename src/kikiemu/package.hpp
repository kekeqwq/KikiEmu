// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "schema.hpp"
#include "storage.hpp"

namespace kiki {
const nlohmann::json& manifest_schema();
const nlohmann::json& source_lock_schema();
std::map<std::string, nlohmann::json> validate_manifest(const nlohmann::json& manifest);
void validate_source_lock(const nlohmann::json& sourceLock);
struct SystemPackage {
    nlohmann::json manifest;
    nlohmann::json sourceLock;
    std::string archiveSha256;
    std::map<std::string, fs::path> payloads;
    StorageIdentity stagingOwner;
};
// Read-only ZIP validation first, then extraction into ONLY a new owned
// staging directory. libarchive is statically linked; no shell, archive-
// supplied executable or download/host PATH fallback is involved.
SystemPackage read_system_package(const fs::path& archive, const fs::path& newStagingDirectory);
} // namespace kiki
