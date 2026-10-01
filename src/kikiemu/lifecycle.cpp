// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include "lifecycle.hpp"
#include "process_state.hpp"
#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
namespace {
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE value) : value(value) {}
    ~Handle() { if (value) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
uint64_t created(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
        throw std::runtime_error("Could not verify process creation identity; termination refused.");
    return uint64_t(creation.dwHighDateTime) << 32 | creation.dwLowDateTime;
}
fs::path executable(HANDLE process) {
    std::wstring result(32768, L'\0'); DWORD length = static_cast<DWORD>(result.size());
    if (!QueryFullProcessImageNameW(process, 0, result.data(), &length))
        throw std::runtime_error("Could not verify process executable; termination refused.");
    result.resize(length); return fs::path(result);
}
void fields(const OwnedProcess& process) {
    if (process.pid == 0 || process.creationTime == 0 ||
        !process.executable.is_absolute() || !valid_instance_uuid(process.instanceUuid) ||
        (process.channel != "release" && process.channel != "dev") ||
        (process.role != "qemu" && process.role != "camera" && process.role != "adb" && process.role != "supervisor") ||
        process.executableSha256.size() != 64 ||
        process.executableSha256.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Invalid owned-process record; termination refused.");
}
bool verified_live_handle(HANDLE handle, const OwnedProcess& record) {
    return detail::verify_live_identity([&] {
        switch (WaitForSingleObject(handle, 0)) {
        case WAIT_TIMEOUT: return detail::ProcessState::live;
        case WAIT_OBJECT_0: return detail::ProcessState::exited;
        default: return detail::ProcessState::failed;
        }
    }, [&] {
        if (created(handle) != record.creationTime ||
            _wcsicmp(executable(handle).lexically_normal().c_str(), record.executable.lexically_normal().c_str()) ||
            sha256(record.executable) != record.executableSha256)
            throw std::runtime_error("Registered live process identity changed (possible PID reuse); operation refused.");
    });
}
} // namespace
json owned_process_json(const OwnedProcess& process) {
    return {{"processVersion", 1}, {"instanceUuid", process.instanceUuid}, {"channel", process.channel},
            {"role", process.role}, {"pid", process.pid}, {"creationTime", std::to_string(process.creationTime)},
            {"executable", utf8(process.executable.wstring())}, {"executableSha256", process.executableSha256}};
}
OwnedProcess parse_owned_process(const json& value) {
    if (!value.is_object() || value.size() != 8 || value.at("processVersion") != 1)
        throw std::runtime_error("Unsupported owned-process record.");
    auto time = value.at("creationTime").get<std::string>();
    if (time.empty() || time.size() > 20 || time.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid process creation identity.");
    const auto& pid = value.at("pid");
    if (!pid.is_number_unsigned() || pid.get<uint64_t>() > UINT32_MAX)
        throw std::runtime_error("Invalid process identifier.");
    OwnedProcess result{value.at("instanceUuid").get<std::string>(), value.at("channel").get<std::string>(),
        value.at("role").get<std::string>(), value.at("pid").get<uint32_t>(), std::stoull(time),
        fs::path(utf16(value.at("executable").get<std::string>())), value.at("executableSha256").get<std::string>()};
    fields(result); return result;
}
OwnedProcess describe_owned_process(uint32_t pid, const fs::path& expectedExe,
                                   const std::string& uuid, const std::string& channel, const std::string& role) {
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid)};
    if (!process.value || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT)
        throw std::runtime_error("Could not record the new live owned process.");
    auto actual = executable(process.value);
    if (_wcsicmp(actual.lexically_normal().c_str(), expectedExe.lexically_normal().c_str()))
        throw std::runtime_error("New child does not match the explicit executable.");
    OwnedProcess result{uuid, channel, role, pid, created(process.value), actual, sha256(actual)};
    fields(result); return result;
}
bool owned_process_is_live(const OwnedProcess& record) {
    fields(record);
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, record.pid)};
    if (!process.value) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) return false;
        throw std::runtime_error("Could not inspect registered process liveness.");
    }
    return verified_live_handle(process.value, record);
}
void verify_owned_endpoints(const std::vector<OwnedProcess>& processes,
                            const std::vector<OwnedEndpoint>& endpoints) {
    if (endpoints.empty()) return;
    std::set<uint32_t> pids;
    for (const auto& process : processes) { fields(process); pids.insert(process.pid); }
    DWORD size = 0;
    DWORD status = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (status != ERROR_INSUFFICIENT_BUFFER || size > 16 * 1024 * 1024)
        throw std::runtime_error("Could not inspect endpoint ownership; operation refused.");
    std::vector<unsigned char> bytes;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        if (size > 16 * 1024 * 1024) throw std::runtime_error("Endpoint table exceeds validation limit.");
        bytes.resize(size);
        status = GetExtendedTcpTable(bytes.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (status != ERROR_INSUFFICIENT_BUFFER) break;
    }
    if (status != NO_ERROR) throw std::runtime_error("Could not verify endpoint ownership; operation refused.");
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(bytes.data());
    std::set<uint16_t> ports;
    for (const auto& endpoint : endpoints) {
        if (!endpoint.port || !pids.contains(endpoint.pid) || !ports.insert(endpoint.port).second)
            throw std::runtime_error("Invalid or foreign endpoint registration.");
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const auto& row = table->table[i];
            if (ntohs(static_cast<uint16_t>(row.dwLocalPort)) != endpoint.port) continue;
            if (row.dwOwningPid != endpoint.pid || row.dwLocalAddr != htonl(INADDR_LOOPBACK))
                throw std::runtime_error("Endpoint is owned by another process or is not loopback-only; operation refused.");
        }
    }
}
void force_stop_owned(const std::string& uuid, const std::string& channel,
                      const std::vector<OwnedProcess>& processes, uint32_t timeoutMs) {
    if (!valid_instance_uuid(uuid) || (channel != "release" && channel != "dev") || !timeoutMs || timeoutMs > 60000)
        throw std::runtime_error("Invalid force-stop target or timeout.");
    struct Pinned { std::string role; std::unique_ptr<Handle> handle; };
    std::vector<Pinned> pinned;
    std::set<uint32_t> pids;
    for (const auto& record : processes) {
        fields(record);
        if (record.pid == GetCurrentProcessId())
            throw std::runtime_error("Refusing to terminate the current manager process.");
        if (record.instanceUuid != uuid || record.channel != channel || !pids.insert(record.pid).second)
            throw std::runtime_error("Runtime belongs to another instance/channel or has duplicate identities; termination refused.");
        auto handle = std::make_unique<Handle>(OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, record.pid));
        if (!handle->value) {
            if (GetLastError() == ERROR_INVALID_PARAMETER) continue; // PID no longer exists.
            throw std::runtime_error("Could not inspect the owned process; termination refused.");
        }
        if (!verified_live_handle(handle->value, record)) continue;
        pinned.push_back({record.role, std::move(handle)});
    }
    // Suppress an owned supervisor before QEMU so it cannot respawn children.
    std::stable_sort(pinned.begin(), pinned.end(), [](const Pinned& a, const Pinned& b) {
        return a.role == "supervisor" && b.role != "supervisor";
    });
    for (auto& entry : pinned) {
        auto& handle = entry.handle;
        if (WaitForSingleObject(handle->value, 0) == WAIT_OBJECT_0) continue;
        if (!TerminateProcess(handle->value, 1)) {
            if (WaitForSingleObject(handle->value, 0) == WAIT_OBJECT_0) continue;
            throw std::runtime_error("An owned process could not be terminated; storage is retained.");
        }
        if (WaitForSingleObject(handle->value, timeoutMs) != WAIT_OBJECT_0)
            throw std::runtime_error("Owned process has not exited; storage is retained.");
    }
}
} // namespace kiki
