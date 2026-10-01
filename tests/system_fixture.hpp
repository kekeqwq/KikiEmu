// SPDX-License-Identifier: GPL-2.0-or-later
// Internal system-regression namespace, never a public manager root override.
#pragma once
#include "../src/kikiemu/manager.hpp"
#include <cwchar>
#include <stdexcept>

namespace kiki_test {
namespace fs = kiki::fs;
using json = nlohmann::json;
struct SystemFixture {
    kiki::StorageIdentity root, instance;
    kiki::ManagerPaths manager;
    std::string id, archiveSha256;
    uint64_t totalBytes;
};
inline bool same_path(const fs::path& left, const fs::path& right) {
    auto a = left.lexically_normal().make_preferred().wstring();
    auto b = right.lexically_normal().make_preferred().wstring();
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
inline SystemFixture validate_system_fixture(const json& value, const fs::path& root, const fs::path& app) {
    if (!value.is_object() || value.size() != 8 || value.at("fixtureVersion") != 1 ||
        value.at("purpose") != "internal-clean-system-regression" || value.at("id") != "01")
        throw std::runtime_error("Not an internal single-instance system test. Public instances are refused.");
    auto rootOwner = kiki::parse_storage_identity(value.at("rootOwner"));
    auto instance = kiki::parse_storage_identity(value.at("instanceOwner"));
    auto recordedApp = fs::path(kiki::utf16(value.at("appRoot").get<std::string>()));
    auto hash = value.at("archiveSha256").get<std::string>();
    const auto& total = value.at("totalBytes");
    if (!root.is_absolute() || !app.is_absolute() || !recordedApp.is_absolute() ||
        !same_path(rootOwner.directory, root) || !same_path(instance.directory, root / "instance") ||
        !same_path(recordedApp, app) || rootOwner.channel != "release" || instance.channel != "release" ||
        rootOwner.instanceUuid == instance.instanceUuid || hash.size() != 64 ||
        hash.find_first_not_of("0123456789abcdef") != std::string::npos || !total.is_number_unsigned() ||
        total.get<uint64_t>() < (1ULL << 30) || total.get<uint64_t>() > INT64_MAX || total.get<uint64_t>() % (1ULL << 30))
        throw std::runtime_error("Internal system fixture path, channel, source or immutable capacity mismatch.");
    return {rootOwner, instance, {root / "registry", app, "release"}, "01", hash, total.get<uint64_t>()};
}
inline void verify_system_fixture_registration(const SystemFixture& fixture, const json& record) {
    if (record.at("owner") != kiki::storage_identity_json(fixture.instance) ||
        record.at("uuid") != fixture.instance.instanceUuid ||
        record.at("immutableSource").at("archiveSha256") != fixture.archiveSha256 ||
        !record.at("immutableSource").at("layout").at("totalBytes").is_number_unsigned() ||
        record.at("immutableSource").at("layout").at("totalBytes") != fixture.totalBytes)
        throw std::runtime_error("Internal test registration no longer belongs to this fixture/package/capacity.");
}
} // namespace kiki_test
