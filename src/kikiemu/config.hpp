// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <exception>
#include <map>
#include <string>
#include <vector>

namespace kiki {
namespace fs = std::filesystem;
using Progress = std::function<void(const std::string&)>;
inline void report(const Progress& progress, const std::string& message) {
    if (progress) progress(message);
}
inline std::string exception_message(std::exception_ptr error) {
    try { std::rethrow_exception(error); }
    catch (const std::exception& original) { return original.what(); }
    catch (...) { return "Unknown installation failure"; }
}

struct Command {
    std::string name;
    std::map<std::string, std::wstring> options;
};

struct Resources {
    uint32_t cpus = 8;
    uint64_t memoryBytes = 4ULL << 30;
    std::string preset = "default";
};

// Parsing has no filesystem/configuration side effects. create cannot supply
// arbitrary QEMU arguments, and set has no disk-size or OS replacement field.
Command parse_command(const std::vector<std::wstring>& args);
uint64_t parse_gib(const std::wstring& value);
Resources resources(const Command& command);
Resources updated_resources(const Command& command, const Resources& current);
fs::path normalize_directory(const std::wstring& value);
std::string utf8(const std::wstring& value);
std::wstring utf16(const std::string& value);
std::wstring quote_windows_arg(const std::wstring& value);
bool valid_instance_uuid(const std::string& value);
std::string new_instance_uuid();
} // namespace kiki
