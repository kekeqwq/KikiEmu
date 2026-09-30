// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "runtime.hpp"
#include <functional>

namespace kiki {
struct StorageIdentity {
    fs::path directory;
    std::string instanceUuid;
    std::string channel;
    uint32_t volumeSerial;
    uint64_t directoryFileId;
};

// Called only for the manager's newly installed directory. The marker is
// CREATE_NEW, never adopted from an existing instance or arbitrary directory.
StorageIdentity claim_storage(const fs::path& directory, const std::string& instanceUuid,
                              const std::string& channel,
                              const std::vector<fs::path>& protectedTrees = {});
nlohmann::json storage_identity_json(const StorageIdentity& identity);
StorageIdentity parse_storage_identity(const nlohmann::json& value);
void verify_storage_owner(const StorageIdentity& identity, const std::vector<fs::path>& protectedTrees = {});
// Recovery ONLY: guarded absence check after an external delete journal was
// persisted. No filesystem changes; a different existing directory is not
// treated as deleted and must pass the normal ownership checks instead.
bool storage_target_missing(const StorageIdentity& identity, const std::vector<fs::path>& protectedTrees);

// All read-only path/owner/tree checks precede the callback. Ancestor/root
// handles remain pinned while it stops the EXACT owned runtime. Never follows
// reparse points; a mismatch or partial removal throws and retains the owner
// marker/manager record for recovery. The manager serializes this transaction,
// journals it before terminating processes, and unregisters ONLY on success.
void delete_storage(const StorageIdentity& identity, const std::vector<fs::path>& protectedTrees,
                    const std::function<void()>& stopOwnedRuntime);
} // namespace kiki
