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
                         uint16_t qmpPort, uint16_t cameraPort);
std::string start_instance(const ManagerPaths& paths, const std::string& requestedId = {});
std::string stop_instance(const ManagerPaths& paths, const std::string& id);
std::string instance_logs(const ManagerPaths& paths, const std::string& id);
std::string instance_shell(const ManagerPaths& paths, const std::string& id, const std::string& command);
// Internal desktop supervisor only; requires a previously registered nonce
// and the exact suspended-child identity, not a caller-selected PID/path.
void supervise_instance(const ManagerPaths& paths, const std::string& id, const std::string& sessionUuid);
} // namespace kiki
