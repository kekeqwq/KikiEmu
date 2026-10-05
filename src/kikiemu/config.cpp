// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include "config.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace kiki {
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                  static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid Unicode argument.");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring utf16(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                  static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 text.");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

bool valid_instance_uuid(const std::string& value) {
    if (value.size() != 36 || value == "00000000-0000-0000-0000-000000000000") return false;
    for (size_t i = 0; i < value.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? value[i] != '-' : !(value[i] >= '0' && value[i] <= '9') &&
                                        !(value[i] >= 'a' && value[i] <= 'f')) return false;
    }
    return true;
}
std::string new_instance_uuid() {
    GUID value{}; wchar_t text[40];
    if (FAILED(CoCreateGuid(&value)) || !StringFromGUID2(value, text, 40))
        throw std::runtime_error("Could not allocate an instance identity.");
    auto result = utf8(std::wstring(text + 1, 36));
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
static uint64_t positive_integer(const std::wstring& value) {
    if (value.empty()) throw std::runtime_error("Expected a positive integer.");
    uint64_t number = 0;
    for (wchar_t c : value) {
        if (c < L'0' || c > L'9' ||
            number > (std::numeric_limits<uint64_t>::max() - (c - L'0')) / 10)
            throw std::runtime_error("Invalid or overflowing positive integer.");
        number = number * 10 + (c - L'0');
    }
    if (!number) throw std::runtime_error("Expected a positive integer.");
    return number;
}

uint64_t parse_gib(const std::wstring& value) {
    if (value.size() < 2 || (value.back() != L'g' && value.back() != L'G'))
        throw std::runtime_error("Use an integer followed by g (GiB), for example 200g.");
    uint64_t number = positive_integer(value.substr(0, value.size() - 1));
    if (number > (std::numeric_limits<int64_t>::max() >> 30))
        throw std::runtime_error("Capacity is too large.");
    return number << 30;
}

Command parse_command(const std::vector<std::wstring>& args) {
    if (args.empty()) return {"help", {}};
    std::string name = utf8(args.front());
    if (name.starts_with("--")) name.erase(0, 2);
    if (name == "h" || name == "-h") name = "help";
    const std::map<std::string, std::set<std::string>> allowed = {
        {"help", {}}, {"version", {}}, {"list", {}},
        {"create", {"system", "storage", "size", "qemu", "performance"}},
        {"set", {"id", "default", "qemu", "mem", "cpus", "performance"}},
        {"start", {"id"}}, {"stop", {"id"}}, {"info", {"id"}},
        {"update", {"id", "action", "package"}},
        {"logs", {"id"}}, {"doctor", {"qemu"}}, {"delete", {"id", "force"}}, {"adb", {"id", "shell"}}
    };
    auto permitted = allowed.find(name);
    if (permitted == allowed.end()) throw std::runtime_error("Unknown command: " + name);
    Command result{name, {}};
    for (size_t i = 1; i < args.size(); ++i) {
        std::string option = utf8(args[i]);
        if (!option.starts_with("--")) throw std::runtime_error("Expected a named option.");
        option.erase(0, 2);
        if (!permitted->second.contains(option))
            throw std::runtime_error("Unsupported option --" + option + " for " + name + ".");
        if (result.options.contains(option)) throw std::runtime_error("Duplicate option --" + option + ".");
        if (name == "delete" && option == "force") {
            result.options[option] = L"true";
            continue;
        }
        if (++i >= args.size() || args[i].empty() || args[i].starts_with(L"--"))
            throw std::runtime_error("Missing value for --" + option + ".");
        result.options[option] = args[i];
    }
    auto require = [&](const std::string& key) {
        if (!result.options.contains(key)) throw std::runtime_error("Missing required option --" + key + ".");
    };
    if (name == "create") {
        for (auto key : {"system", "storage", "size", "qemu"}) require(key);
        parse_gib(result.options.at("size"));
    }
    if (name == "set") {
        if (result.options.contains("default")) {
            if (result.options.size() != 1)
                throw std::runtime_error("Use set --default separately from instance settings.");
        } else {
            require("id");
            if (result.options.size() == 1) throw std::runtime_error("No instance settings were supplied.");
        }
    }
    if (name == "info" || name == "logs" || name == "stop" || name == "delete") require("id");
    if (name == "adb") { require("id"); require("shell"); }
    if (name == "update") {
        require("id"); require("action");const auto action=utf8(result.options.at("action"));
        if (action!="status"&&action!="check"&&action!="apply"&&action!="reboot") throw std::runtime_error("Update action must be status, check, apply or reboot.");
        if (action=="apply") require("package");else if(result.options.contains("package")) throw std::runtime_error("Only offline apply accepts --package.");
    }
    if (name == "delete" && !result.options.contains("force"))
        throw std::runtime_error("Deletion permanently removes all instance data. Use delete --force --id NN.");
    if (name == "create" || name == "set") resources(result);
    return result;
}

Resources resources(const Command& command) {
    return updated_resources(command, Resources{});
}
Resources updated_resources(const Command& command, const Resources& current) {
    Resources result = current;
    auto preset = command.options.find("performance");
    if (preset != command.options.end()) {
        result = Resources{};
        result.preset = utf8(preset->second);
        if (result.preset == "medium") result.memoryBytes = 6ULL << 30;
        else if (result.preset == "high") { result.cpus = 10; result.memoryBytes = 8ULL << 30; }
        else if (result.preset != "default")
            throw std::runtime_error("Performance must be default, medium or high.");
    }
    if (auto mem = command.options.find("mem"); mem != command.options.end()) {
        result.memoryBytes = parse_gib(mem->second);
        result.preset = "custom";
    }
    if (auto cpus = command.options.find("cpus"); cpus != command.options.end()) {
        auto count = positive_integer(cpus->second);
        if (count > 64) throw std::runtime_error("CPU count must be between 1 and 64.");
        result.cpus = static_cast<uint32_t>(count);
        result.preset = "custom";
    }
    return result;
}

fs::path normalize_directory(const std::wstring& value) {
    if (value.empty() || value.find(L'\0') != std::wstring::npos)
        throw std::runtime_error("Directory path is empty or invalid.");
    if (std::any_of(value.begin(), value.end(), [](wchar_t c) { return c < 32; }))
        throw std::runtime_error("Path contains a control character/newline. Pass the whole path as one quoted argument.");
    fs::path path(value);
    if (value.front() == L'~') {
        if (value.size() > 1 && value[1] != L'/' && value[1] != L'\\')
            throw std::runtime_error("Only ~/ and ~\\ home paths are supported.");
        PWSTR profile = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile)))
            throw std::runtime_error("Could not resolve the Windows user profile.");
        path = fs::path(profile);
        CoTaskMemFree(profile);
        if (value.size() > 2) path /= value.substr(2);
    }
    if (path.has_root_name() && !path.has_root_directory())
        throw std::runtime_error("Drive-relative paths are not supported; use an absolute path.");
    // Existing symlinks/junctions resolve before recording identity. Missing
    // storage targets are allowed here; the installer checks their ownership.
    auto result = fs::weakly_canonical(fs::absolute(path)).lexically_normal().make_preferred();
    // A trailing separator is a valid user directory spelling, not an empty
    // storage component. Strip it centrally, preserving drive/UNC roots.
    while (result != result.root_path() && !result.has_filename()) result = result.parent_path();
    return result;
}

std::wstring quote_windows_arg(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
} // namespace kiki
