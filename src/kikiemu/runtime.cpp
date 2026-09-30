// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "runtime.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
json parse_json_document(const std::string& text) {
    if (text.starts_with("\xef\xbb\xbf")) throw std::runtime_error("Product JSON must use UTF-8 without a BOM.");
    std::map<int, std::set<std::string>> keys;
    return json::parse(text, [&](int depth, json::parse_event_t event, json& parsed) {
        if (depth > 64) throw std::runtime_error("Product JSON nesting exceeds its supported limit.");
        if (event == json::parse_event_t::object_start) keys[depth + 1].clear();
        if (event == json::parse_event_t::key && !keys[depth].insert(parsed.get<std::string>()).second)
            throw std::runtime_error("Duplicate JSON object key; operation refused.");
        return true;
    });
}
static std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

static std::vector<unsigned char> read_pe_bytes(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Could not open runtime file: " + utf8(file.wstring()));
    auto size = stream.tellg();
    if (size < 64 || size > 512LL * 1024 * 1024)
        throw std::runtime_error("Invalid or excessively large PE image.");
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Could not read PE image.");
    return bytes;
}

PeInfo inspect_pe(const fs::path& file) {
    auto bytes = read_pe_bytes(file);
    auto number = [&](uint64_t at, size_t width) -> uint32_t {
        if (at > bytes.size() || width > bytes.size() - at)
            throw std::runtime_error("Truncated PE image.");
        uint32_t result = 0;
        for (size_t i = 0; i < width; ++i) result |= uint32_t(bytes[at + i]) << (8 * i);
        return result;
    };
    if (number(0, 2) != 0x5a4d) throw std::runtime_error("Runtime file is not a Windows PE image.");
    uint64_t pe = number(60, 4);
    if (number(pe, 4) != 0x4550) throw std::runtime_error("Invalid PE signature.");
    PeInfo result;
    result.machine = static_cast<uint16_t>(number(pe + 4, 2));
    uint32_t sections = number(pe + 6, 2);
    uint32_t optionalSize = number(pe + 20, 2);
    uint64_t optional = pe + 24;
    uint32_t magic = number(optional, 2);
    if (magic != 0x20b && magic != 0x10b) throw std::runtime_error("Unsupported PE optional header.");
    uint64_t directory = optional + (magic == 0x20b ? 112 : 96);
    if (optionalSize < (magic == 0x20b ? 112U : 96U) + 16)
        throw std::runtime_error("Truncated PE optional header.");
    uint32_t importRva = number(directory + 8, 4);
    uint32_t importSize = number(directory + 12, 4);
    uint64_t sectionTable = optional + optionalSize;
    if (sections > 256 || sectionTable > bytes.size() || sections * 40 > bytes.size() - sectionTable)
        throw std::runtime_error("Invalid PE section table.");
    auto rva_offset = [&](uint32_t rva) -> uint64_t {
        for (uint32_t i = 0; i < sections; ++i) {
            uint64_t section = sectionTable + 40 * i;
            uint32_t address = number(section + 12, 4);
            uint32_t rawSize = number(section + 16, 4);
            uint32_t rawOffset = number(section + 20, 4);
            if (rva >= address && uint64_t(rva) - address < rawSize) {
                uint64_t at = uint64_t(rawOffset) + rva - address;
                if (at >= bytes.size()) throw std::runtime_error("Invalid PE RVA.");
                return at;
            }
        }
        throw std::runtime_error("Unmapped PE import RVA.");
    };
    if (!importRva && !importSize) return result;
    uint64_t imports = rva_offset(importRva);
    bool terminated = false;
    for (size_t i = 0; i < 512; ++i) {
        uint64_t entry = imports + 20 * i;
        if ((i + 1) * 20 > importSize) break;
        uint32_t nameRva = number(entry + 12, 4);
        if (!nameRva) { terminated = true; break; }
        uint64_t at = rva_offset(nameRva);
        std::string name;
        while (at < bytes.size() && bytes[at] && name.size() < 260) name += static_cast<char>(bytes[at++]);
        if (at >= bytes.size() || bytes[at] || name.empty() ||
            name.find_first_of("/\\:") != std::string::npos ||
            (!lower(name).ends_with(".dll") && !lower(name).ends_with(".drv")))
            throw std::runtime_error("Invalid PE imported DLL name in " + utf8(file.filename().wstring()) + ": " + name + ".");
        result.imports.push_back(name);
    }
    if (!terminated) throw std::runtime_error("Unterminated PE import directory.");
    return result;
}

std::string sha256(const fs::path& file) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto check = [](NTSTATUS status) {
        if (status < 0) throw std::runtime_error("Windows SHA-256 operation failed.");
    };
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    struct Cleanup {
        BCRYPT_ALG_HANDLE& algorithm;
        BCRYPT_HASH_HANDLE& hash;
        ~Cleanup() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } cleanup{algorithm, hash};
    check(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0));
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not open file for SHA-256.");
    std::array<unsigned char, 1 << 16> buffer;
    while (stream.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || stream.gcount())
        check(BCryptHashData(hash, buffer.data(), static_cast<ULONG>(stream.gcount()), 0));
    if (!stream.eof()) throw std::runtime_error("Could not finish reading file for SHA-256.");
    std::array<unsigned char, 32> digest;
    check(BCryptFinishHash(hash, digest.data(), digest.size(), 0));
    std::ostringstream result;
    for (auto byte : digest) result << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return result.str();
}

std::string sha256_text(const std::string& text) {
    if (text.size() > ULONG_MAX) throw std::runtime_error("SHA-256 input exceeds the supported length.");
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    struct Cleanup {
        BCRYPT_ALG_HANDLE& algorithm; BCRYPT_HASH_HANDLE& hash;
        ~Cleanup() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } cleanup{algorithm, hash};
    std::array<unsigned char, 32> digest;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())), static_cast<ULONG>(text.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), digest.size(), 0) < 0) throw std::runtime_error("SHA-256 operation failed.");
    std::ostringstream result;
    for (auto byte : digest) result << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return result.str();
}
static bool windows_dll(const std::string& name) {
    auto key = lower(name);
    if (key.starts_with("api-ms-win-") || key.starts_with("ext-ms-")) return true;
    std::array<wchar_t, MAX_PATH> system;
    UINT length = GetSystemDirectoryW(system.data(), system.size());
    if (!length || length >= system.size()) throw std::runtime_error("Could not find Windows system DLL directory.");
    return fs::is_regular_file(fs::path(system.data()) / utf16(name));
}

static std::vector<std::string> capabilities(const fs::path& executable) {
    const std::vector<std::string> required = {
        "KIKI_SDL_DISABLE_GRAB", "KIKI_SDL_DISABLE_IME", "KIKI_SDL_NATIVE_PIXELS",
        "KIKI_SDL_START_WIDTH", "KIKI_SDL_START_HEIGHT", "KIKI_SDL_GUEST_REFRESH_RATE_HZ",
        "KIKI_SDL_RAW_KEYBOARD_TRACE", "KIKI_SDL_BOOT_STATUS", "KIKI_SDL_WINDOW_TITLE"
    };
    auto bytes = read_pe_bytes(executable);
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    for (const auto& marker : required) if (text.find(marker) == std::string_view::npos)
        throw std::runtime_error("QEMU is missing a required KikiEmu patch capability: " + marker +
                                 ". Build the patched ARM64 QEMU described in README.md.");
    return required;
}

nlohmann::json inspect_qemu(const fs::path& directory, const std::vector<fs::path>& buildSources) {
    auto bin = normalize_directory(directory.wstring());
    if (!fs::is_directory(bin)) throw std::runtime_error("QEMU bin directory does not exist.");
    json result = {{"bindingVersion", 1}, {"binDirectory", utf8(bin.wstring())},
                   {"architecture", "aarch64"}, {"files", json::array()},
                   {"validation", "static-only; boot/hardware/ABI acceptance still required"}};
    std::queue<fs::path> pending;
    std::set<std::string> seen;
    for (auto name : {"qemu-system-aarch64.exe", "qemu-img.exe", "qemu-io.exe"}) {
        auto path = bin / name;
        if (!fs::is_regular_file(path)) throw std::runtime_error("QEMU bin is missing " + std::string(name) + ".");
        pending.push(path);
    }
    result["compiledMarkers"] = capabilities(bin / "qemu-system-aarch64.exe");
    while (!pending.empty()) {
        auto path = pending.front(); pending.pop();
        if (buildSources.empty() && fs::canonical(path).parent_path() != bin)
            throw std::runtime_error("Runtime files must be private copies in the configured QEMU bin, not external links.");
        auto key = lower(utf8(path.filename().wstring()));
        if (!seen.insert(key).second) continue;
        auto info = inspect_pe(path);
        if (info.machine != 0xaa64) throw std::runtime_error("Expected a native ARM64 runtime file: " + key + ".");
        result["files"].push_back({{"name", utf8(path.filename().wstring())},
                                   {"source", utf8(fs::canonical(path).wstring())},
                                   {"bytes", fs::file_size(path)}, {"sha256", sha256(path)}});
        for (const auto& name : info.imports) {
            // System DLLs resolve through Windows; third-party files must be
            // beside the EXE. Export-only sources cannot become saved bindings.
            fs::path dependency = bin / utf16(name);
            if (!fs::is_regular_file(dependency)) {
                if (windows_dll(name)) continue;
                dependency.clear();
                for (const auto& source : buildSources) {
                    auto candidate = source / utf16(name);
                    if (fs::is_regular_file(candidate)) { dependency = candidate; break; }
                }
                if (dependency.empty()) throw std::runtime_error("QEMU bin is missing a private DLL: " + name + ".");
            }
            pending.push(dependency);
        }
    }
    // ROM lookup is explicit (-L <configured-bin>/roms) in the manager.
    // Export inspection uses the source tree; final bindings require a private,
    // independently hashed directory and never borrow pc-bios from a checkout.
    result["roms"] = json::array();
    if (buildSources.empty()) {
        auto roms = bin / "roms";
        if (!fs::is_directory(roms) || fs::canonical(roms).parent_path() != bin)
            throw std::runtime_error("QEMU bin is missing its private roms directory.");
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(roms)) {
            if (!entry.is_regular_file()) throw std::runtime_error("Unsupported entry in QEMU roms directory.");
            auto extension = lower(utf8(entry.path().extension().wstring()));
            if (extension != ".bin" && extension != ".rom" && extension != ".fd" && extension != ".dtb")
                throw std::runtime_error("Unexpected file in QEMU roms directory.");
            if (fs::canonical(entry.path()).parent_path() != fs::canonical(roms))
                throw std::runtime_error("QEMU ROMs must be private files, not external links.");
            files.push_back(entry.path());
        }
        if (files.empty()) throw std::runtime_error("QEMU roms directory is empty.");
        std::sort(files.begin(), files.end());
        for (const auto& file : files) result["roms"].push_back({
            {"name", utf8(file.filename().wstring())}, {"bytes", fs::file_size(file)}, {"sha256", sha256(file)}});
    }
    result["exportOnly"] = !buildSources.empty();
    return result;
}

void verify_qemu_binding(const json& binding) {
    if (binding.at("bindingVersion") != 1 || binding.at("architecture") != "aarch64" ||
        binding.value("exportOnly", true)) throw std::runtime_error("Unsupported or export-only QEMU binding.");
    auto current = inspect_qemu(fs::path(utf16(binding.at("binDirectory").get<std::string>())));
    if (current != binding)
        throw std::runtime_error("The configured QEMU runtime changed. Use set --qemu to validate it again.");
}
} // namespace kiki
