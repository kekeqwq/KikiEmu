// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "manager.hpp"
#include "process.hpp"
#include "transport.hpp"

namespace kiki {
struct RuntimePlan {
    fs::path qemu, camera, cwd, logDirectory;
    std::vector<std::wstring> arguments;
    std::map<std::wstring, std::wstring> environment;
    std::string serial;
};
// Shared launch recipe. Pure construction, never a bare WHPX/GPU probe.
RuntimePlan runtime_plan(const nlohmann::json& instance, const ManagerPaths& paths,
                         const std::string& sessionUuid, uint16_t adbPort,
                         uint16_t qmpPort, uint16_t cameraPort,
                         const nlohmann::json& nativeBoot = nlohmann::json::object());
// Pure boot-overlay gate. windowDump is from dumpsys window displays, NOT
// dumpsys activity activities (which does not emit DisplayContent focus).
bool launcher_display_ready(const std::string& windowDump, const std::string& layers,
                            const std::string& displayDump);
bool display_power_ready(const std::string& powerDump, const std::string& displayDump);
// Running guest properties only. No immutable baseline/host version fallback.
// Invalid/unavailable metadata is presentation-only, never a boot failure.
std::string boot_version_summary(const std::string& displayId, const std::string& incremental);
std::string start_instance(const ManagerPaths& paths, const std::string& requestedId = {});
std::string stop_instance(const ManagerPaths& paths, const std::string& id);
std::string instance_logs(const ManagerPaths& paths, const std::string& id);
std::string instance_shell(const ManagerPaths& paths, const std::string& id, const std::string& command);
std::string instance_update(const ManagerPaths& paths, const std::string& id, const std::string& action, const fs::path& package = {}, const Progress& progress = {});
// Internal desktop supervisor only; requires a previously registered nonce
// and the exact suspended-child identity, not a caller-selected PID/path.
void supervise_instance(const ManagerPaths& paths, const std::string& id, const std::string& sessionUuid, unsigned recoveryAttempt = 0);
} // namespace kiki
