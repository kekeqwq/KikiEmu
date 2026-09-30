// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "config.hpp"
#include <memory>

namespace kiki {
struct ProcessResult { uint32_t exitCode; std::string output; };
// Child image tools only: explicit EXE, explicit cwd, private DLL PATH, no
// shell/no console. A timeout kills only the exact child created by this call.
ProcessResult image_tool(const fs::path& executable, const fs::path& cwd,
                         const std::vector<std::wstring>& args, uint32_t timeoutSeconds = 300);
class PrivateChild {
public:
    // Always suspended until its exact identity has been persisted. An owned
    // job closes with the supervisor and kills ONLY this new child's tree.
    PrivateChild(const fs::path& exe, const fs::path& cwd, const std::vector<std::wstring>& args,
                 const std::map<std::wstring, std::wstring>& environment, const fs::path& log,
                 bool ownedJob = true);
    ~PrivateChild();
    PrivateChild(const PrivateChild&) = delete;
    uint32_t pid() const;
    void resume();
    bool running() const;
    uint32_t exit_code() const;
    bool wait(uint32_t milliseconds) const;
    void terminate();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace kiki
