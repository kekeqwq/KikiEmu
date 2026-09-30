// SPDX-License-Identifier: GPL-2.0-or-later
#include "manager.hpp"
#include "installation.hpp"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    try {
        const auto command = kiki::parse_command(std::vector<std::wstring>(argv + 1, argv + argc));
        auto paths = kiki::user_manager_paths();
        kiki::InstallationLease installation(paths.appRoot);
        std::cout << kiki::execute_management(command, paths);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n'; return 1;
    }
}
