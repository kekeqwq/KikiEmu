// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define LIBARCHIVE_STATIC
#define XML_STATIC
#include <windows.h>
#include <objbase.h>
#include <archive.h>
#include <archive_entry.h>
#include <expat.h>
#include "package.hpp"
#include "boot.hpp"
#include "storage.hpp"
#include "package_contract.generated.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
namespace {
constexpr uint64_t maxTotal = 32ULL << 30, alignment = 1048576;
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE value) : value(value) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
using Held = std::unique_ptr<Handle>;
std::wstring extended(const fs::path& path) { return L"\\\\?\\" + path.lexically_normal().make_preferred().wstring(); }
Held pin(const fs::path& path, DWORD access, bool directory = false) {
    auto result = std::make_unique<Handle>(CreateFileW(extended(path).c_str(), access,
        directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (result->value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(result->value, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory)
        throw std::runtime_error("Package/staging path is redirected or cannot be pinned.");
    return result;
}
std::vector<Held> parents(const fs::path& path, bool createMissing = false) {
    auto current = path.root_path(); std::vector<Held> result;
    for (const auto& part : path.parent_path().relative_path()) {
        current /= part;
        if (createMissing && GetFileAttributesW(extended(current).c_str()) == INVALID_FILE_ATTRIBUTES &&
            !CreateDirectoryW(extended(current).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Could not create the owned staging parent.");
        result.push_back(pin(current, FILE_READ_ATTRIBUTES, true));
    }
    return result;
}
bool canonical_name(const std::string& name) {
    return name.size() <= 128 && std::regex_match(name, std::regex(
        "manifest[.]json|payload/(boot|system|vendor)[.]img|provenance/source-lock[.]json|licenses/[a-z0-9][a-z0-9_-]*[.]txt"));
}
struct Reader {
    archive* value;
    explicit Reader(const fs::path& file) : value(archive_read_new()) {
        if (!value) throw std::runtime_error("Could not allocate the ZIP reader.");
        if (archive_read_support_filter_none(value) != ARCHIVE_OK ||
            archive_read_support_format_zip_seekable(value) != ARCHIVE_OK ||
            archive_read_open_filename_w(value, file.c_str(), 65536) != ARCHIVE_OK) {
            archive_read_free(value); value = nullptr;
            throw std::runtime_error("Could not open a supported, seekable ZIP system package.");
        }
    }
    ~Reader() { if (value) archive_read_free(value); }
};
std::string name_of(archive_entry* entry) {
    const char* text = archive_entry_pathname_utf8(entry);
    if (!text || !canonical_name(text) || archive_entry_filetype(entry) != AE_IFREG ||
        archive_entry_symlink(entry) || archive_entry_hardlink(entry) || archive_entry_is_encrypted(entry) > 0)
        throw std::runtime_error("Forbidden, encrypted or noncanonical system ZIP entry.");
    return text;
}
std::string read_small(archive* reader, uint64_t declared, uint64_t limit) {
    if (!declared || declared > limit) throw std::runtime_error("Package metadata exceeds its size limit.");
    std::string result; result.reserve(static_cast<size_t>(declared)); std::array<char, 65536> buffer;
    la_ssize_t count;
    while ((count = archive_read_data(reader, buffer.data(), buffer.size())) > 0) {
        if (result.size() + count > declared) throw std::runtime_error("Package metadata exceeds its declared length.");
        result.append(buffer.data(), static_cast<size_t>(count));
    }
    if (count < 0 || result.size() != declared) throw std::runtime_error("Truncated/corrupt package metadata or ZIP CRC mismatch.");
    return result;
}
uint64_t little(const unsigned char* value, size_t width) {
    uint64_t result = 0; for (size_t i = 0; i < width; ++i) result |= uint64_t(value[i]) << (8 * i); return result;
}
// libarchive performs the actual ZIP/ZIP64/CRC/decompression work. This small
// metadata preflight additionally rejects raw embedded-NUL/case alias names,
// self-extracting stubs, multi-disk files, comments and trailing hidden bytes
// that an otherwise lenient decoder might normalize or ignore.
std::set<std::string> raw_zip_names(const fs::path& file, uint64_t bytes) {
    std::ifstream stream(file, std::ios::binary);
    auto read = [&](uint64_t at, size_t count) {
        if (at > bytes || count > bytes - at) throw std::runtime_error("ZIP metadata is out of bounds.");
        std::vector<unsigned char> result(count); stream.clear(); stream.seekg(at);
        if (!stream.read(reinterpret_cast<char*>(result.data()), count)) throw std::runtime_error("ZIP metadata is truncated.");
        return result;
    };
    if (bytes < 22 || little(read(0, 4).data(), 4) != 0x04034b50) throw std::runtime_error("System package must be a ZIP, not an SFX/executable.");
    auto end = read(bytes - 22, 22);
    if (little(end.data(), 4) != 0x06054b50 || little(end.data() + 20, 2) ||
        little(end.data() + 4, 2) || little(end.data() + 6, 2) || little(end.data() + 8, 2) != little(end.data() + 10, 2))
        throw std::runtime_error("Multi-disk/commented/trailing ZIP data is unsupported.");
    uint64_t entries = little(end.data() + 10, 2), size = little(end.data() + 12, 4), offset = little(end.data() + 16, 4);
    uint64_t centralEnd = bytes - 22;
    if (entries == 65535 || size == UINT32_MAX || offset == UINT32_MAX) {
        auto locator = read(bytes - 42, 20);
        if (little(locator.data(), 4) != 0x07064b50 || little(locator.data() + 4, 4) || little(locator.data() + 16, 4) != 1)
            throw std::runtime_error("Invalid ZIP64 locator.");
        uint64_t recordAt = little(locator.data() + 8, 8); auto record = read(recordAt, 56);
        if (little(record.data(), 4) != 0x06064b50 || little(record.data() + 4, 8) != 44 ||
            recordAt + 56 != bytes - 42 || little(record.data() + 16, 4) || little(record.data() + 20, 4) ||
            little(record.data() + 24, 8) != little(record.data() + 32, 8)) throw std::runtime_error("Invalid ZIP64 end record.");
        entries = little(record.data() + 32, 8); size = little(record.data() + 40, 8); offset = little(record.data() + 48, 8);
        centralEnd = recordAt;
    }
    if (!entries || entries > 69 || size > 1048576 || offset > centralEnd || size != centralEnd - offset)
        throw std::runtime_error("ZIP directory count/size/offset is unsupported.");
    std::set<std::string> names;
    uint64_t cursor = offset;
    for (uint64_t i = 0; i < entries; ++i) {
        auto header = read(cursor, 46);
        if (little(header.data(), 4) != 0x02014b50 || (little(header.data() + 8, 2) & 1) || little(header.data() + 34, 2))
            throw std::runtime_error("Invalid/encrypted/multi-disk ZIP entry.");
        auto nameBytes = little(header.data() + 28, 2), extra = little(header.data() + 30, 2), comment = little(header.data() + 32, 2);
        if (!nameBytes || nameBytes > 128 || comment || cursor + 46 + nameBytes + extra > centralEnd)
            throw std::runtime_error("Noncanonical ZIP entry metadata.");
        auto raw = read(cursor + 46, static_cast<size_t>(nameBytes));
        std::string name(raw.begin(), raw.end());
        if (!canonical_name(name) || !names.insert(name).second) throw std::runtime_error("Duplicate or unsafe raw ZIP pathname.");
        uint64_t localAt = little(header.data() + 42, 4);
        if (localAt == UINT32_MAX) {
            auto fields = read(cursor + 46 + nameBytes, static_cast<size_t>(extra)); bool found = false;
            for (size_t at = 0; at + 4 <= fields.size();) {
                auto tag = little(fields.data() + at, 2), length = little(fields.data() + at + 2, 2); at += 4;
                if (length > fields.size() - at) throw std::runtime_error("Truncated ZIP64 extra field.");
                if (tag == 1) {
                    size_t skip = (little(header.data() + 24, 4) == UINT32_MAX ? 8 : 0) +
                                  (little(header.data() + 20, 4) == UINT32_MAX ? 8 : 0);
                    if (found || skip + 8 > length) throw std::runtime_error("Missing/duplicate ZIP64 local offset.");
                    localAt = little(fields.data() + at + skip, 8); found = true;
                }
                at += static_cast<size_t>(length);
            }
            if (!found) throw std::runtime_error("ZIP64 local offset was not declared.");
        }
        if (localAt >= offset) throw std::runtime_error("ZIP local header overlaps its directory.");
        auto local = read(localAt, 30); auto localNameBytes = little(local.data() + 26, 2), localExtra = little(local.data() + 28, 2);
        auto method = little(header.data() + 10, 2);
        if (little(local.data(), 4) != 0x04034b50 || localNameBytes != nameBytes ||
            localAt + 30 + localNameBytes + localExtra > offset ||
            (method != 0 && method != 8) || little(local.data() + 8, 2) != method ||
            little(local.data() + 6, 2) != little(header.data() + 8, 2) ||
            read(localAt + 30, static_cast<size_t>(localNameBytes)) != raw)
            throw std::runtime_error("ZIP local/central name, flags or compression differ.");
        cursor += 46 + nameBytes + extra;
    }
    if (cursor != centralEnd) throw std::runtime_error("Unrecognized ZIP directory extension.");
    return names;
}
struct XmlState { XML_Parser parser; unsigned depth = 0; uint64_t projects = 0; bool invalid = false; std::set<std::string> paths; };
void XMLCALL start_element(void* user, const XML_Char* name, const XML_Char** attributes) {
    auto& state = *static_cast<XmlState*>(user);
    if ((state.depth == 0 && std::string(name) != "manifest") || state.depth >= 32) state.invalid = true;
    if (std::string(name) == "project") {
        std::map<std::string, std::string> values;
        for (auto item = attributes; *item; item += 2) values[item[0]] = item[1];
        auto path = values.contains("path") ? values["path"] : values["name"];
        bool unsafe = path.empty() || path[0] == '/' || path.find_first_of("\\:") != std::string::npos;
        std::istringstream parts(path); std::string part;
        while (std::getline(parts, part, '/')) if (part.empty() || part == "." || part == "..") unsafe = true;
        if (path.ends_with('/')) unsafe = true;
        if (state.depth != 1 || unsafe || !std::regex_match(values["revision"], std::regex("[0-9a-f]{40}")) ||
            !state.paths.insert(path).second) state.invalid = true;
        ++state.projects;
    }
    ++state.depth;
    if (state.invalid) XML_StopParser(state.parser, XML_FALSE);
}
void XMLCALL end_element(void* user, const XML_Char*) { --static_cast<XmlState*>(user)->depth; }
void XMLCALL doctype(void* user, const XML_Char*, const XML_Char*, const XML_Char*, int) {
    auto& state = *static_cast<XmlState*>(user); state.invalid = true; XML_StopParser(state.parser, XML_FALSE);
}
} // namespace
const json& manifest_schema() { static auto value = parse_json_document(kiki_contract::manifestSchema); return value; }
const json& source_lock_schema() { static auto value = parse_json_document(kiki_contract::sourceLockSchema); return value; }
std::map<std::string, json> validate_manifest(const json& value) {
    validate_schema(value, manifest_schema());
    version_supported(value.at("systemVersion").get<std::string>(), value.at("systemVersion").get<std::string>());
    if (!version_supported(value.at("minimumLauncherVersion"))) throw std::runtime_error("System requires a newer KikiEmu reader.");
    std::map<std::string, json> records{{"manifest.json", nullptr}}; std::set<std::string> roles;
    uint64_t total = 0;
    for (const auto& item : value.at("payloads")) {
        auto role = item.at("role").get<std::string>(); auto bytes = item.at("bytes").get<uint64_t>();
        if (!roles.insert(role).second || item.at("path") != "payload/" + role + ".img" ||
            item.at("imageFormat") != (role == "boot" ? "android-boot-v4" : "raw-erofs") ||
            item.at("partitionBytes") != (bytes + alignment - 1) / alignment * alignment)
            throw std::runtime_error("Payload role/path/format/partition constraints are inconsistent.");
        records[item.at("path").get<std::string>()] = item; total += bytes;
    }
    auto add = [&](const json& item) {
        if (!records.emplace(item.at("path").get<std::string>(), item).second) throw std::runtime_error("Duplicate manifest file declaration.");
        total += item.at("bytes").get<uint64_t>();
    };
    add(value.at("sourceLock")); for (const auto& item : value.at("licenses")) add(item);
    if (roles != std::set<std::string>{"boot", "system", "vendor"} || total > maxTotal ||
        !records.contains("licenses/aosp.txt") || !records.contains("licenses/kernel.txt"))
        throw std::runtime_error("Required roles/licenses are missing or package exceeds its size limit.");
    return records;
}
void validate_source_lock(const json& value) {
    validate_schema(value, source_lock_schema());
    auto xml = value.at("aosp").at("manifestXml").get<std::string>();
    if (xml.size() > 4194304 || sha256_text(xml) != value.at("aosp").at("manifestSha256").get<std::string>() ||
        value.at("contract").at("manifestSchemaSha256") != kiki_contract::manifestSha256 ||
        value.at("contract").at("sourceLockSchemaSha256") != kiki_contract::sourceLockSha256)
        throw std::runtime_error("Pinned schema/AOSP source-lock identity mismatch.");
    XML_Parser parser = XML_ParserCreate("UTF-8");
    if (!parser) throw std::runtime_error("Could not allocate the provenance XML reader.");
    struct Cleanup { XML_Parser value; ~Cleanup() { XML_ParserFree(value); } } cleanup{parser};
    XmlState state{parser, 0, 0, false, {}}; XML_SetUserData(parser, &state); XML_SetElementHandler(parser, start_element, end_element);
    XML_SetStartDoctypeDeclHandler(parser, doctype); XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_NEVER);
    if (XML_Parse(parser, xml.data(), static_cast<int>(xml.size()), XML_TRUE) != XML_STATUS_OK || state.invalid ||
        state.projects != value.at("aosp").at("projectCount").get<uint64_t>())
        throw std::runtime_error("AOSP source manifest is unsafe, unpinned or inconsistent.");
}
SystemPackage read_system_package(const fs::path& archiveFile, const fs::path& newStagingDirectory) {
    const auto file = normalize_directory(archiveFile.wstring()), stage = normalize_directory(newStagingDirectory.wstring());
    if (file.root_name().wstring().size() != 2 || stage.root_name().wstring().size() != 2 || fs::exists(stage))
        throw std::runtime_error("System ZIP/staging must use local paths and a NEW staging directory.");
    auto sourceParents = parents(file); auto source = pin(file, GENERIC_READ | FILE_READ_ATTRIBUTES);
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(source->value, &length) || length.QuadPart <= 0 || uint64_t(length.QuadPart) > maxTotal)
        throw std::runtime_error("System ZIP exceeds its supported size limit.");
    auto rawNames = raw_zip_names(file, static_cast<uint64_t>(length.QuadPart));
    std::map<std::string, uint64_t> lengths; json manifest;
    {
        Reader reader(file); archive_entry* entry = nullptr; int status;
        while ((status = archive_read_next_header(reader.value, &entry)) == ARCHIVE_OK) {
            auto name = name_of(entry); auto size = archive_entry_size(entry);
            if (size <= 0 || uint64_t(size) > maxTotal || !lengths.emplace(name, uint64_t(size)).second)
                throw std::runtime_error("Duplicate, empty or oversized system ZIP entry.");
            if (name == "manifest.json") manifest = parse_json_document(read_small(reader.value, size, 1048576));
            else if (archive_read_data_skip(reader.value) != ARCHIVE_OK) throw std::runtime_error("System ZIP scan failed.");
        }
        if (status != ARCHIVE_EOF || archive_read_has_encrypted_entries(reader.value) > 0)
            throw std::runtime_error("Corrupt/encrypted/unsupported system ZIP.");
    }
    auto declared = validate_manifest(manifest);
    if (declared.size() != lengths.size() || lengths.size() != rawNames.size()) throw std::runtime_error("ZIP differs from the manifest allowlist.");
    for (const auto& [name, item] : declared) {
        if (!rawNames.contains(name) || !lengths.contains(name) || (!item.is_null() && lengths.at(name) != item.at("bytes")))
            throw std::runtime_error("Declared ZIP file length/allowlist mismatch.");
    }
    auto stageParents = parents(stage, true);
    if (!CreateDirectoryW(extended(stage).c_str(), nullptr)) throw std::runtime_error("Could not create a NEW owned package staging directory.");
    auto stageRoot = pin(stage, FILE_READ_ATTRIBUTES, true);
    GUID stageUuid{}; wchar_t uuidText[40];
    if (FAILED(CoCreateGuid(&stageUuid)) || !StringFromGUID2(stageUuid, uuidText, 40))
        throw std::runtime_error("Could not allocate owned package staging identity.");
    auto uuid = utf8(std::wstring(uuidText + 1, 36));
    std::transform(uuid.begin(), uuid.end(), uuid.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto owner = claim_storage(stage, uuid, "release", {file});
    try {
    std::vector<Held> childDirectories;
    for (const auto* name : {L"payload", L"provenance", L"licenses"}) {
        if (!CreateDirectoryW(extended(stage / name).c_str(), nullptr)) throw std::runtime_error("Could not create NEW package staging subdirectories.");
        childDirectories.push_back(pin(stage / name, FILE_READ_ATTRIBUTES, true));
    }
    Reader reader(file); archive_entry* entry = nullptr; int status; std::array<char, 65536> buffer;
    while ((status = archive_read_next_header(reader.value, &entry)) == ARCHIVE_OK) {
        auto name = name_of(entry); uint64_t expected = lengths.at(name), written = 0;
        Handle output(CreateFileW(extended(stage / utf16(name)).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (output.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not write a NEW owned package staging file " + name +
            " (Windows error " + std::to_string(GetLastError()) + ").");
        la_ssize_t count;
        while ((count = archive_read_data(reader.value, buffer.data(), buffer.size())) > 0) {
            if (uint64_t(count) > expected - written) throw std::runtime_error("Extracted entry exceeds its declared size.");
            DWORD saved = 0;
            if (!WriteFile(output.value, buffer.data(), static_cast<DWORD>(count), &saved, nullptr) || saved != count)
                throw std::runtime_error("Package staging write failed.");
            written += static_cast<uint64_t>(count);
        }
        if (count < 0 || written != expected || !FlushFileBuffers(output.value)) throw std::runtime_error("ZIP CRC/length/staging flush verification failed.");
    }
    if (status != ARCHIVE_EOF) throw std::runtime_error("System ZIP extraction failed.");
    for (const auto& [name, item] : declared) if (!item.is_null() && sha256(stage / utf16(name)) != item.at("sha256").get<std::string>())
        throw std::runtime_error("Staged package file SHA-256 mismatch.");
    std::ifstream lockStream(stage / "provenance/source-lock.json", std::ios::binary);
    auto lock = parse_json_document(std::string((std::istreambuf_iterator<char>(lockStream)), {})); validate_source_lock(lock);
    SystemPackage result{manifest, lock, sha256(file), {}, owner};
    validate_boot_payload(stage / "payload/boot.img");
    std::ifstream bootStream(stage / "payload/boot.img", std::ios::binary); std::array<unsigned char, 4096> boot{};
    bootStream.read(reinterpret_cast<char*>(boot.data()), boot.size()); auto header = parse_boot_header(boot, fs::file_size(stage / "payload/boot.img"));
    bootStream.seekg(4096); std::string kernel(header.kernelBytes, '\0');
    if (!bootStream.read(kernel.data(), kernel.size()) || sha256_text(kernel) != lock.at("kernel").at("imageSha256").get<std::string>())
        throw std::runtime_error("Source-lock kernel does not match the boot payload.");
    for (const auto& item : manifest.at("payloads")) {
        auto role = item.at("role").get<std::string>(); auto path = stage / utf16(item.at("path").get<std::string>());
        if (role != "boot") {
            std::ifstream stream(path, std::ios::binary); std::array<unsigned char, 4096> bytes{};
            if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) || little(bytes.data() + 1024, 4) != 0xe0f5e1e2)
                throw std::runtime_error("System/vendor must be raw EROFS, not a sparse or whole-disk image.");
        }
        result.payloads.emplace(role, path);
    }
    return result;
    } catch (...) {
        auto original = std::current_exception(); stageRoot.reset();
        // All reader/file/child-directory handles inside the try have closed.
        // Remove ONLY this freshly created stage by the pinned owner identity;
        // an identity mismatch retains it rather than following another path.
        delete_storage(owner, {file}, [] {});
        std::rethrow_exception(original);
    }
}
} // namespace kiki
