// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>

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
        "KIKI_SDL_RAW_KEYBOARD_TRACE", "KIKI_SDL_BOOT_STATUS", "KIKI_SDL_WINDOW_TITLE", "KIKI_SDL_CLOSE_EVENT"
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

namespace {
void plain_runtime_path(const fs::path& path, bool directory) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(attributes & FILE_ATTRIBUTE_DIRECTORY) != directory)
        throw std::runtime_error("Missing, redirected or invalid runtime preparation path: " + utf8(path.wstring()));
}
void runtime_not_in_use(const fs::path& bin) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not check whether QEMU is in use.");
    struct Close { HANDLE value; ~Close() { CloseHandle(value); } } close{snapshot};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) throw std::runtime_error("Could not enumerate running QEMU processes.");
    do {
        const auto name = lower(utf8(entry.szExeFile));
        if (name != "qemu-system-aarch64.exe" && name != "qemu-img.exe" && name != "qemu-io.exe") continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (!process) {
            if (GetLastError() == ERROR_INVALID_PARAMETER) continue; // Already exited.
            throw std::runtime_error("Cannot verify a QEMU process identity. Close QEMU before runtime preparation.");
        }
        Close held{process};
        wchar_t executable[32768]; DWORD length = std::size(executable);
        if (!QueryFullProcessImageNameW(process, 0, executable, &length)) {
            if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) continue;
            throw std::runtime_error("Cannot verify a running QEMU path. Close QEMU before runtime preparation.");
        }
        if (lower(utf8(fs::path(executable).parent_path().wstring())) == lower(utf8(bin.wstring())))
            throw std::runtime_error("This QEMU bin is in use. Close its processes before preparing missing dependencies.");
    } while (Process32NextW(snapshot, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw std::runtime_error("QEMU process enumeration was interrupted.");
}
struct RuntimeCopy { fs::path source, destination; std::string hash; };
}
json prepare_qemu(const fs::path& directory, const Progress& progress) {
    auto bin = normalize_directory(directory.wstring());
    for (const auto* name : {"qemu-system-aarch64.exe", "qemu-img.exe", "qemu-io.exe"}) {
        if (!fs::is_regular_file(bin / name))
            throw std::runtime_error("QEMU bin is missing " + std::string(name) + " at " + utf8(bin.wstring()) +
                ". Compile QEMU first using build.ps1 from the mainline README; select its bin directory, not an EXE.");
    }
    report(progress, "Checking QEMU EXEs, private DLLs and ROMs: " + utf8(bin.wstring()));
    try {
        auto binding = inspect_qemu(bin);
        report(progress, "QEMU runtime is ready. No files changed.");
        return binding;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (!message.starts_with("QEMU bin is missing a private DLL:") &&
            message != "QEMU bin is missing its private roms directory." && message != "QEMU roms directory is empty.") throw;
        report(progress, "QEMU runtime preparation required: " + message);
    }
    plain_runtime_path(bin, true);
    runtime_not_in_use(bin);
    const auto root = bin.parent_path(), receipt = root / ".kiki-qemu-build/state.json";
    if (!fs::is_regular_file(receipt))
        throw std::runtime_error("QEMU EXEs exist but runtime dependencies are incomplete and the build receipt is missing: " +
            utf8(receipt.wstring()) + ". Keep bin in the QEMU checkout built with build.ps1, or provide a complete private runtime.");
    plain_runtime_path(receipt.parent_path(), true); plain_runtime_path(receipt, false);
    if (fs::file_size(receipt) > 1048576) throw std::runtime_error("QEMU build receipt is oversized.");
    std::ifstream input(receipt, std::ios::binary);
    auto state = parse_json_document(std::string((std::istreambuf_iterator<char>(input)), {}));
    if (state.value("Version", 0) != 1 || !state.contains("Source") || !state.contains("Msys2") ||
        !state.at("Source").is_string() || !state.at("Msys2").is_string() ||
        !fs::equivalent(normalize_directory(utf16(state.at("Source").get<std::string>())), root))
        throw std::runtime_error("QEMU build receipt does not describe this checkout. No files changed.");
    const auto dependencies = normalize_directory(utf16(state.at("Msys2").get<std::string>())) / "clangarm64/bin";
    const auto bios = root / "pc-bios", roms = bin / "roms";
    plain_runtime_path(dependencies, true); plain_runtime_path(bios, true);
    auto exported = inspect_qemu(bin, {dependencies}); // Static only; never starts QEMU.
    std::vector<RuntimeCopy> copies;
    auto add = [&](const fs::path& source, const fs::path& destination, const std::string& expected) {
        plain_runtime_path(source, false);
        if (fs::exists(destination)) {
            plain_runtime_path(destination, false);
            if (sha256(destination) != expected)
                throw std::runtime_error("Existing runtime file conflicts with the build source: " + utf8(destination.wstring()) + ". Nothing was overwritten.");
        } else copies.push_back({source, destination, expected});
    };
    for (const auto& file : exported.at("files")) {
        const auto source = fs::path(utf16(file.at("source").get<std::string>()));
        const auto name = fs::path(utf16(file.at("name").get<std::string>()));
        if (source.parent_path() != bin && source.parent_path() != fs::canonical(dependencies))
            throw std::runtime_error("Linked/external runtime dependency refused: " + utf8(source.wstring()));
        add(source, bin / name, file.at("sha256").get<std::string>());
    }
    if (fs::exists(roms)) {
        plain_runtime_path(roms, true);
        for (const auto& entry : fs::directory_iterator(roms)) {
            plain_runtime_path(entry.path(), false);
            auto extension = lower(utf8(entry.path().extension().wstring()));
            if (extension != ".bin" && extension != ".rom" && extension != ".fd" && extension != ".dtb")
                throw std::runtime_error("Unexpected existing file in QEMU roms: " + utf8(entry.path().wstring()));
            if (!fs::is_regular_file(bios / entry.path().filename()))
                throw std::runtime_error("Existing ROM is not from the selected QEMU source: " + utf8(entry.path().wstring()));
        }
    }
    size_t romCount = 0;
    for (const auto& entry : fs::directory_iterator(bios)) {
        auto extension = lower(utf8(entry.path().extension().wstring()));
        if (extension != ".bin" && extension != ".rom" && extension != ".fd" && extension != ".dtb") continue;
        ++romCount; add(entry.path(), roms / entry.path().filename(), sha256(entry.path()));
    }
    if (!romCount) throw std::runtime_error("No matching QEMU ROMs were found. No files changed.");
    for (const auto& file : copies) {
        if (sha256(file.source) != file.hash || fs::exists(file.destination))
            throw std::runtime_error("Runtime source/destination changed during preparation. No files overwritten.");
    }
    runtime_not_in_use(bin);
    if (!fs::exists(roms)) fs::create_directory(roms);
    plain_runtime_path(roms, true);
    report(progress, "Preparing " + std::to_string(copies.size()) + " missing DLL/ROM files from this build's sources.");
    for (const auto& file : copies) {
        plain_runtime_path(file.destination.parent_path(), true);
        // Default copy_options::none refuses an existing destination.
        if (!fs::copy_file(file.source, file.destination) || sha256(file.destination) != file.hash)
            throw std::runtime_error("Runtime copy verification failed: " + utf8(file.destination.wstring()));
        report(progress, "Prepared " + utf8(file.destination.filename().wstring()));
    }
    auto binding = inspect_qemu(bin); // No MSYS2 fallback survives in saved bindings.
    report(progress, "QEMU runtime prepared and verified. EXEs were not replaced.");
    return binding;
}
} // namespace kiki
