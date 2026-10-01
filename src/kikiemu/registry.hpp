// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "storage.hpp"
#include "lifecycle.hpp"
#include <memory>

namespace kiki {
// Fixed user-scoped release/dev roots in production; an explicit root here
// permits isolated internal fixtures, not a public --registry override.
class RegistryTransaction {
public:
    RegistryTransaction(const fs::path& root, const std::string& channel);
    ~RegistryTransaction();
    RegistryTransaction(const RegistryTransaction&) = delete;
    const nlohmann::json& state() const;
    std::vector<std::string> ordered_ids() const;
    nlohmann::json& instance(const std::string& id);
    void save();
    std::string register_installed(const StorageIdentity& owner, const nlohmann::json& immutableSource,
                                  const nlohmann::json& qemuBinding, const Resources& configuration);
    void set_default(const std::string& id);
    // App install/source roots are additional to automatic registry, BYO
    // QEMU and all other registered instance storage protections.
    bool delete_instance(const std::string& id, const std::vector<fs::path>& appProtectedTrees);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace kiki
