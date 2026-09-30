// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "process.hpp"
#include <algorithm>
#include <array>
#include <cwchar>
#include <map>
#include <stdexcept>

namespace kiki {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct IgnoreCase {
    bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; }
};
ProcessResult image_tool(const fs::path& executable, const fs::path& cwd,
                         const std::vector<std::wstring>& args, uint32_t timeoutSeconds) {
    if (!fs::is_regular_file(executable) || !executable.is_absolute() || !cwd.is_absolute())
        throw std::runtime_error("Image tool requires an explicit existing EXE and absolute working directory.");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle outputRead, outputWrite, inputNull;
    if (!CreatePipe(&outputRead.value, &outputWrite.value, &security, 0) ||
        !SetHandleInformation(outputRead.value, HANDLE_FLAG_INHERIT, 0))
        throw std::runtime_error("Could not create the image-tool output pipe.");
    inputNull.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (inputNull.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open the image-tool input handle.");
    std::wstring command = quote_windows_arg(executable.wstring());
    for (const auto& arg : args) command += L" " + quote_windows_arg(arg);
    std::map<std::wstring, std::wstring, IgnoreCase> environment;
    LPWCH original = GetEnvironmentStringsW();
    if (!original) throw std::runtime_error("Could not read the process environment.");
    for (auto entry = original; *entry; entry += std::wcslen(entry) + 1) {
        std::wstring item(entry);
        size_t equals = item.find(L'=', item.front() == L'=' ? 1 : 0);
        if (equals != std::wstring::npos) environment[item.substr(0, equals)] = item.substr(equals + 1);
    }
    FreeEnvironmentStringsW(original);
    wchar_t system[32768], windows[32768];
    if (!GetSystemDirectoryW(system, 32768) || !GetWindowsDirectoryW(windows, 32768))
        throw std::runtime_error("Could not find the Windows system directories.");
    environment[L"PATH"] = executable.parent_path().wstring() + L";" + system + L";" + windows;
    std::vector<wchar_t> block;
    for (const auto& [name, value] : environment) {
        std::wstring item = name + L"=" + value;
        block.insert(block.end(), item.begin(), item.end()); block.push_back(0);
    }
    block.push_back(0);
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributes(attributeBytes);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inputNull.value;
    startup.StartupInfo.hStdOutput = outputWrite.value;
    startup.StartupInfo.hStdError = outputWrite.value;
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeBytes))
        throw std::runtime_error("Could not initialize the child handle allowlist.");
    struct AttributeCleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeCleanup() { DeleteProcThreadAttributeList(value); }
    } attributeCleanup{startup.lpAttributeList};
    HANDLE inherited[] = {inputNull.value, outputWrite.value};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                  inherited, sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("Could not set the child handle allowlist.");
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                        block.data(), cwd.c_str(), &startup.StartupInfo, &child))
        throw std::runtime_error("Could not start the configured image tool (Windows error " + std::to_string(GetLastError()) + ").");
    Handle process{child.hProcess}, thread{child.hThread};
    CloseHandle(outputWrite.value); outputWrite.value = nullptr;
    ProcessResult result{};
    std::array<char, 16384> buffer;
    ULONGLONG deadline = GetTickCount64() + uint64_t(timeoutSeconds) * 1000;
    while (true) {
        if (GetTickCount64() >= deadline) {
            TerminateProcess(process.value, 1); WaitForSingleObject(process.value, INFINITE);
            throw std::runtime_error("Image tool timed out; the exact owned child was terminated. No instance was registered.");
        }
        DWORD available = 0;
        if (PeekNamedPipe(outputRead.value, nullptr, 0, nullptr, &available, nullptr) && available) {
            DWORD read = 0;
            if (ReadFile(outputRead.value, buffer.data(), std::min<DWORD>(available, buffer.size()), &read, nullptr)) {
                if (result.output.size() < 1024 * 1024) result.output.append(buffer.data(), std::min<size_t>(read, 1024 * 1024 - result.output.size()));
                continue;
            }
        }
        if (WaitForSingleObject(process.value, 10) == WAIT_OBJECT_0) {
            // Once the child exits, drain its remaining bounded diagnostics.
            while (ReadFile(outputRead.value, buffer.data(), buffer.size(), &available, nullptr) && available)
                if (result.output.size() < 1024 * 1024) result.output.append(buffer.data(), std::min<size_t>(available, 1024 * 1024 - result.output.size()));
            DWORD code = 0;
            if (!GetExitCodeProcess(process.value, &code)) throw std::runtime_error("Could not read the image-tool exit code.");
            result.exitCode = code;
            return result;
        }
    }
}
} // namespace kiki
