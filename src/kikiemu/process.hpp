// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "config.hpp"

namespace kiki {
struct ProcessResult { uint32_t exitCode; std::string output; };
// Child image tools only: explicit EXE, explicit cwd, private DLL PATH, no
// shell/no console. A timeout kills only the exact child created by this call.
ProcessResult image_tool(const fs::path& executable, const fs::path& cwd,
                         const std::vector<std::wstring>& args, uint32_t timeoutSeconds = 300);
} // namespace kiki
