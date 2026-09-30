// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "installation.hpp"
#include <cwctype>
#include <stdexcept>

namespace kiki {
struct InstallationLease::Impl {
    HANDLE file = INVALID_HANDLE_VALUE;
    ~Impl() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};
InstallationLease::InstallationLease(const fs::path& appRoot) : impl(std::make_unique<Impl>()) {
    if (!appRoot.is_absolute()) throw std::runtime_error("Installed application directory must be absolute.");
    auto lock = (appRoot / "install.lock").lexically_normal().make_preferred();
    auto extended = L"\\\\?\\" + lock.wstring();
    impl->file = CreateFileW(extended.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (impl->file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(impl->file, &info) ||
        info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("KikiEmu is not installed, or setup/uninstall is active. Install setup.exe or retry after it finishes.");
}
InstallationLease::~InstallationLease() = default;

namespace {
std::wstring comparable(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t"), last = value.find_last_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    value = value.substr(first, last - first + 1);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') value = value.substr(1, value.size() - 2);
    while (value.size() > 3 && (value.back() == L'\\' || value.back() == L'/')) value.pop_back();
    for (auto& c : value) { if (c == L'/') c = L'\\'; c = towlower(c); }
    return value;
}
void valid_entry(const std::wstring& entry) {
    if (entry.empty() || entry.find_first_of(L";\r\n\0", 0, 4) != std::wstring::npos || entry.size() > 32700)
        throw std::runtime_error("Invalid application PATH entry.");
}
}
bool has_path_segment(const std::wstring& path, const std::wstring& entry) {
    valid_entry(entry);
    const auto expected = comparable(entry);
    for (size_t start = 0; start <= path.size();) {
        auto end = path.find(L';', start);
        if (end == std::wstring::npos) end = path.size();
        if (comparable(path.substr(start, end - start)) == expected) return true;
        if (end == path.size()) break;
        start = end + 1;
    }
    return false;
}
std::wstring add_path_segment(const std::wstring& path, const std::wstring& entry) {
    if (has_path_segment(path, entry)) return path;
    auto result = path.empty() ? entry : path + L";" + entry;
    if (result.size() >= 32760) throw std::runtime_error("User PATH is too long; no value was truncated or changed.");
    return result;
}
std::wstring remove_path_segment(const std::wstring& path, const std::wstring& entry) {
    valid_entry(entry);
    const auto expected = comparable(entry);
    std::wstring result;
    bool emitted = false;
    for (size_t start = 0; start <= path.size();) {
        auto end = path.find(L';', start);
        if (end == std::wstring::npos) end = path.size();
        const auto segment = path.substr(start, end - start);
        if (comparable(segment) != expected) {
            if (emitted) result += L';';
            result += segment; emitted = true;
        }
        if (end == path.size()) break;
        start = end + 1;
    }
    return result;
}
} // namespace kiki
