// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "registry.hpp"

namespace kiki {
struct ManagerPaths {
    fs::path registryRoot, appRoot;
    std::string channel = "release";
};
// Production paths are fixed to the current user/channel. Explicit fixture
// roots are an internal-library interface, NOT public CLI/env overrides.
ManagerPaths user_manager_paths();
std::string execute_management(const Command& command, const ManagerPaths& paths);
const char* management_help();
} // namespace kiki
