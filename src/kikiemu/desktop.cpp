// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include "session.hpp"
#include "installation.hpp"
#include <stdexcept>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        int count = 0; auto words = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!words) throw std::runtime_error("Could not read desktop startup arguments.");
        struct Cleanup { wchar_t** words; ~Cleanup() { LocalFree(words); } } cleanup{words};
        auto paths = kiki::user_manager_paths();
        kiki::InstallationLease installation(paths.appRoot);
        if (count == 1) { kiki::start_instance(paths); return 0; }
        if (count != 6 || std::wstring(words[1]) != L"--supervise" || std::wstring(words[2]) != L"--id" ||
            std::wstring(words[4]) != L"--session" || !kiki::valid_instance_uuid(kiki::utf8(words[5])))
            throw std::runtime_error("Unsupported desktop startup arguments. Configure instances with the kikiemu command.");
        kiki::supervise_instance(paths, kiki::utf8(words[3]), kiki::utf8(words[5])); return 0;
    } catch (const std::exception& error) {
        auto message = kiki::utf16(std::string(error.what()) + "\n\nSee the KikiEmu README for initialization and troubleshooting.");
        MessageBoxW(nullptr, message.c_str(), L"KikiEmu", MB_OK | MB_ICONERROR); return 1;
    }
}
