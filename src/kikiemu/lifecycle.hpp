// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "runtime.hpp"

namespace kiki {
struct OwnedProcess {
    std::string instanceUuid;
    std::string channel;
    std::string role; // qemu, camera, adb or supervisor
    uint32_t pid;
    uint64_t creationTime;
    fs::path executable;
    std::string executableSha256;
};
nlohmann::json owned_process_json(const OwnedProcess& process);
OwnedProcess parse_owned_process(const nlohmann::json& value);
// Only record a process immediately after this manager created it. This
// function is NOT authority to adopt arbitrary discovered processes.
OwnedProcess describe_owned_process(uint32_t pid, const fs::path& expectedExe,
                                   const std::string& uuid, const std::string& channel,
                                   const std::string& role);

struct OwnedEndpoint { uint32_t pid; uint16_t port; };
// Check IPv4 loopback listeners against the OS owner table. Missing listeners
// are allowed for starting/exited children; existing foreign/public bindings
// are refused. Process identity is separately checked before termination.
void verify_owned_endpoints(const std::vector<OwnedProcess>& processes,
                            const std::vector<OwnedEndpoint>& endpoints);

// Validate ALL handles before killing ANY: exact EXE/SHA/creation identity,
// UUID and channel. Dead/stale PIDs are ignored ONLY when absent; PID reuse,
// permission failure or changed live identity fail closed. Never title/name
// selection, kill-all-QEMU, a PID-only kill or global ADB kill-server.
void force_stop_owned(const std::string& instanceUuid, const std::string& channel,
                      const std::vector<OwnedProcess>& processes, uint32_t timeoutMs = 10000);
} // namespace kiki
