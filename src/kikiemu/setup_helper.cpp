// SPDX-License-Identifier: GPL-2.0-or-later
// Internal installer helper, not a public manager command. Full-length user
// PATH operations are native ARM64; NSIS never reads/truncates PATH itself.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include "installation.hpp"
#include <optional>
#include <stdexcept>

namespace {
struct Key {
    HKEY value = nullptr;
    ~Key() { if (value) RegCloseKey(value); }
};
struct Text { DWORD type; std::wstring value; };
std::optional<Text> read(HKEY key, const wchar_t* name) {
    DWORD type = 0, bytes = 0;
    auto status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) return {};
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes < 2 || bytes > 65536 || bytes % 2) throw std::runtime_error("Unsupported or oversized environment value; no changes made.");
    std::wstring value(bytes / 2, L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS ||
        bytes != value.size() * 2 || value.back() != L'\0') throw std::runtime_error("Environment read changed or was interrupted.");
    value.pop_back();
    if (value.find(L'\0') != std::wstring::npos) throw std::runtime_error("Malformed environment string; no changes made.");
    return Text{type, value};
}
void write(HKEY key, const wchar_t* name, DWORD type, const std::wstring& text) {
    if (RegSetValueExW(key, name, 0, type, reinterpret_cast<const BYTE*>(text.c_str()),
                       static_cast<DWORD>((text.size() + 1) * 2)) != ERROR_SUCCESS)
        throw std::runtime_error("Could not persist the installer environment change.");
}
void remove(HKEY key, const wchar_t* name) {
    auto status = RegDeleteValueW(key, name);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) throw std::runtime_error("Could not remove the owned installer record.");
}
kiki::fs::path application_root() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) throw std::runtime_error("Could not locate current-user installation directory.");
    kiki::fs::path root(local); CoTaskMemFree(local);
    auto app = (root / "Programs/KikiEmu").lexically_normal().make_preferred();
    return app;
}
void preflight() {
    auto app = application_root();
    auto ancestor = app.root_path();
    for (const auto& component : app.relative_path()) {
        ancestor /= component;
        const auto attributes = GetFileAttributesW(ancestor.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) break;
            throw std::runtime_error("Could not verify installation ancestors.");
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("Installation directory is redirected; refusing to install/remove files.");
    }
    if (!kiki::fs::exists(app) || kiki::fs::is_empty(app)) return;
    Key marker;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\KikiEmu\\Installer", 0,
                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &marker.value) != ERROR_SUCCESS)
        throw std::runtime_error("Nonempty installation directory is not owned by KikiEmu setup.");
    auto owned = read(marker.value, L"InstallRoot");
    if (!owned || _wcsicmp(owned->value.c_str(), app.c_str()))
        throw std::runtime_error("Installation ownership does not match.");
    auto lock = app / "install.lock";
    auto attributes = GetFileAttributesW(lock.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Installation lock is redirected; operation refused.");
}
void edit_path(bool install) {
    auto app = application_root();
    preflight();
    wchar_t self[32768]; auto length = GetModuleFileNameW(nullptr, self, 32768);
    if (!length || length >= 32768 || _wcsicmp(kiki::fs::path(self).parent_path().c_str(), app.c_str()))
        throw std::runtime_error("Installer helper must run from the fixed current-user KikiEmu installation.");
    Key environment, marker;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                       nullptr, &environment.value, nullptr) != ERROR_SUCCESS ||
        RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\KikiEmu\\Installer", 0, nullptr, 0,
                       KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &marker.value, nullptr) != ERROR_SUCCESS)
        throw std::runtime_error("Could not open current-user environment records.");
    auto old = read(environment.value, L"Path");
    auto owned = read(marker.value, L"OwnedPathEntry");
    if (owned && _wcsicmp(owned->value.c_str(), app.c_str())) throw std::runtime_error("Installer PATH ownership differs; refusing to edit another installation.");
    const auto original = old ? old->value : L"";
    auto next = install ? kiki::add_path_segment(original, app.wstring()) :
                         owned ? kiki::remove_path_segment(original, app.wstring()) : original;
    auto again = read(environment.value, L"Path");
    if (bool(again) != bool(old) || (again && (again->value != old->value || again->type != old->type)))
        throw std::runtime_error("User PATH changed concurrently; retry. Nothing was overwritten.");
    if (install && next != original) {
        // Persist intent FIRST, so an interrupted installer can retry/remove
        // precisely this segment instead of treating it as a preexisting one.
        write(marker.value, L"OwnedPathEntry", REG_SZ, app.wstring());
        write(environment.value, L"Path", old ? old->type : REG_EXPAND_SZ, next);
    } else if (!install && owned) {
        if (next != original) write(environment.value, L"Path", old ? old->type : REG_EXPAND_SZ, next);
        remove(marker.value, L"OwnedPathEntry");
    }
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 3000, &result);
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) throw std::runtime_error("Could not read installer arguments.");
        struct Cleanup { wchar_t** value; ~Cleanup() { LocalFree(value); } } cleanup{argv};
        if (argc != 2) throw std::runtime_error("Unsupported internal installer operation.");
        if (std::wstring(argv[1]) == L"preflight") { preflight(); return 0; }
        if (std::wstring(argv[1]) != L"path-add" && std::wstring(argv[1]) != L"path-remove")
            throw std::runtime_error("Unsupported internal installer operation.");
        edit_path(std::wstring(argv[1]) == L"path-add"); return 0;
    } catch (const std::exception&) {
        MessageBoxW(nullptr, L"The installation or current-user PATH could not be safely verified/updated. Redirected or foreign directories are refused. No unrelated PATH entry was removed or truncated. Close other installers and retry.",
                    L"KikiEmu setup", MB_OK | MB_ICONERROR); return 1;
    }
}
