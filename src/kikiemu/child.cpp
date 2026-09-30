// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "process.hpp"
#include <array>
#include <cwchar>
#include <cwctype>
#include <stdexcept>

namespace kiki {
namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct IgnoreCase { bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; } };
std::vector<wchar_t> private_environment(const fs::path& exe, const std::map<std::wstring, std::wstring>& overrides) {
    std::map<std::wstring, std::wstring, IgnoreCase> values;
    LPWCH block = GetEnvironmentStringsW();
    if (!block) throw std::runtime_error("Could not read process environment.");
    for (auto entry = block; *entry; entry += std::wcslen(entry) + 1) {
        std::wstring item(entry); auto equals = item.find('=', item[0] == '=' ? 1 : 0);
        if (equals == std::wstring::npos) continue;
        auto name = item.substr(0, equals), upper = name;
        for (auto& c : upper) c = std::towupper(c);
        // Do not inherit development probes or runtime/renderer overrides.
        if (upper.starts_with(L"KIKI_") || upper.starts_with(L"QEMU_") || upper.starts_with(L"SDL_") ||
            upper.starts_with(L"MESA_") || upper.starts_with(L"LIBGL_") || upper.starts_with(L"LD_") ||
            upper.starts_with(L"ADB_") || upper == L"ANDROID_SERIAL") continue;
        values[name] = item.substr(equals + 1);
    }
    FreeEnvironmentStringsW(block);
    wchar_t system[32768], windows[32768];
    if (!GetSystemDirectoryW(system, 32768) || !GetWindowsDirectoryW(windows, 32768))
        throw std::runtime_error("Could not locate Windows system directories.");
    values[L"PATH"] = exe.parent_path().wstring() + L";" + system + L";" + windows;
    for (const auto& [name, value] : overrides) {
        if (name.empty() || name.find_first_of(L"=\0", 0, 2) != std::wstring::npos || value.find(L'\0') != std::wstring::npos)
            throw std::runtime_error("Invalid private child environment entry.");
        values[name] = value;
    }
    std::vector<wchar_t> result;
    for (const auto& [name, value] : values) {
        auto text = name + L"=" + value; result.insert(result.end(), text.begin(), text.end()); result.push_back(0);
    }
    result.push_back(0); return result;
}
} // namespace
struct PrivateChild::Impl {
    Handle process, thread, job;
    DWORD pid = 0;
    bool resumed = false;
    ~Impl() {
        // Failed creation/registration must never strand a suspended child.
        // Owned runtime trees must be fully stopped before their storage lease
        // is released. A deliberately detached, registered supervisor is the
        // sole exception after resume; its caller does not own that job.
        if (process.value && (!resumed || job.value)) {
            if (job.value) TerminateJobObject(job.value, 1);
            else TerminateProcess(process.value, 1);
            WaitForSingleObject(process.value, 10000);
        }
    }
};
PrivateChild::PrivateChild(const fs::path& exe, const fs::path& cwd, const std::vector<std::wstring>& args,
                         const std::map<std::wstring, std::wstring>& environment, const fs::path& log, bool ownedJob)
    : impl(std::make_unique<Impl>()) {
    if (!exe.is_absolute() || !cwd.is_absolute() || !log.is_absolute() || !fs::is_regular_file(exe))
        throw std::runtime_error("Child runtime requires explicit local executable, working and log paths.");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle input, output;
    input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    output.value = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input.value == INVALID_HANDLE_VALUE || output.value == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Could not create NEW private runtime log/input handles.");
    auto block = private_environment(exe, environment);
    std::wstring command = quote_windows_arg(exe.wstring());
    for (const auto& arg : args) command += L" " + quote_windows_arg(arg);
    SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size))
        throw std::runtime_error("Could not initialize private child handle list.");
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Cleanup() { DeleteProcThreadAttributeList(value); } } cleanup{startup.lpAttributeList};
    HANDLE inherited[] = {input.value, output.value};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("Could not restrict private child inherited handles.");
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value; startup.StartupInfo.hStdOutput = output.value; startup.StartupInfo.hStdError = output.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
            block.data(), cwd.c_str(), &startup.StartupInfo, &process))
        throw std::runtime_error("Could not create private child runtime (Windows error " + std::to_string(GetLastError()) + ").");
    impl->process.value = process.hProcess; impl->thread.value = process.hThread; impl->pid = process.dwProcessId;
    if (ownedJob) {
        impl->job.value = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!impl->job.value || !SetInformationJobObject(impl->job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(impl->job.value, impl->process.value))
            throw std::runtime_error("Could not isolate the new runtime in its owned job.");
    }
}
PrivateChild::~PrivateChild() = default;
uint32_t PrivateChild::pid() const { return impl->pid; }
void PrivateChild::resume() {
    if (impl->resumed || ResumeThread(impl->thread.value) == DWORD(-1)) throw std::runtime_error("Could not resume registered child runtime.");
    impl->resumed = true;
}
bool PrivateChild::running() const {
    const auto status = WaitForSingleObject(impl->process.value, 0);
    if (status != WAIT_TIMEOUT && status != WAIT_OBJECT_0) throw std::runtime_error("Could not inspect owned child state.");
    return status == WAIT_TIMEOUT;
}
uint32_t PrivateChild::exit_code() const {
    DWORD code = 0;
    if (running() || !GetExitCodeProcess(impl->process.value, &code)) throw std::runtime_error("Owned child has no final exit code.");
    return code;
}
bool PrivateChild::wait(uint32_t milliseconds) const {
    const auto status = WaitForSingleObject(impl->process.value, milliseconds);
    if (status != WAIT_TIMEOUT && status != WAIT_OBJECT_0) throw std::runtime_error("Could not wait for owned child exit.");
    return status == WAIT_OBJECT_0;
}
void PrivateChild::terminate() {
    if (!running()) return;
    if (!TerminateProcess(impl->process.value, 1) || !wait(10000)) throw std::runtime_error("Owned runtime could not be stopped.");
}
} // namespace kiki
