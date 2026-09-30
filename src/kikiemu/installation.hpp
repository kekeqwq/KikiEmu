// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "config.hpp"
#include <memory>

namespace kiki {
// Shared read lease held for the WHOLE lifetime of every installed entry point.
// Setup/uninstall opens this same file exclusively before changing app files.
class InstallationLease {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit InstallationLease(const fs::path& appRoot);
    ~InstallationLease();
    InstallationLease(const InstallationLease&) = delete;
};
// Pure exact PATH-segment edits; preserve all other segments byte-for-byte.
std::wstring add_path_segment(const std::wstring& path, const std::wstring& entry);
std::wstring remove_path_segment(const std::wstring& path, const std::wstring& entry);
bool has_path_segment(const std::wstring& path, const std::wstring& entry);
} // namespace kiki
