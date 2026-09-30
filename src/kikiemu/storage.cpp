// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include "storage.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
namespace {
constexpr const wchar_t* ownerName = L".kikiemu-owner.json";
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value) : value(value) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
using Held = std::unique_ptr<Handle>;
std::runtime_error failure(const char* message) {
    return std::runtime_error(std::string(message) + " (Windows error " + std::to_string(GetLastError()) + ").");
}
std::wstring folded(const fs::path& value) {
    auto result = value.lexically_normal().make_preferred().wstring();
    while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    CharLowerBuffW(result.data(), static_cast<DWORD>(result.size()));
    return result;
}
bool same_or_parent(const fs::path& ancestor, const fs::path& child) {
    const auto a = folded(ancestor), b = folded(child);
    return a == b || (b.size() > a.size() && b.starts_with(a) &&
                     (a.back() == L'\\' || b[a.size()] == L'\\'));
}
void check_identity_fields(const StorageIdentity& identity) {
    if (!valid_instance_uuid(identity.instanceUuid) || (identity.channel != "release" && identity.channel != "dev"))
        throw std::runtime_error("Invalid storage instance UUID or channel.");
}
fs::path known(REFKNOWNFOLDERID id) {
    PWSTR text = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &text)))
        throw std::runtime_error("Could not resolve protected Windows folders; deletion refused.");
    fs::path result(text); CoTaskMemFree(text); return result;
}
void safe_target(const fs::path& path, const std::vector<fs::path>& protectedTrees) {
    // Deliberately lexical: resolving a junction before inspecting its handle
    // would hide redirection and could turn an old registration into a new target.
    const auto name = path.root_name().wstring();
    if (!path.is_absolute() || name.size() != 2 || name[1] != L':' ||
        !((name[0] >= L'A' && name[0] <= L'Z') || (name[0] >= L'a' && name[0] <= L'z')) ||
        path == path.root_path() || folded(path) != folded(path.lexically_normal()))
        throw std::runtime_error("Storage removal requires a normalized local absolute instance directory.");
    for (const auto& part : path.relative_path()) {
        auto text = part.wstring();
        if (text.empty() || text == L"." || text == L".." || text.back() == L'.' || text.back() == L' ' ||
            text.find_first_of(L":*?\"<>|") != std::wstring::npos)
            throw std::runtime_error("Ambiguous storage path; deletion refused.");
    }
    // Reject these directories or their ancestors, not normal user-created
    // children such as Downloads/MyAndroid. System/program trees are forbidden
    // in BOTH directions. More application/workspace/QEMU roots are supplied
    // by the manager, never trusted from the instance's editable storage marker.
    for (auto id : {&FOLDERID_Profile, &FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_Documents,
                    &FOLDERID_LocalAppData, &FOLDERID_RoamingAppData})
        if (same_or_parent(path, known(*id))) throw std::runtime_error("Protected user folder; deletion refused.");
    std::vector<fs::path> both = protectedTrees;
    both.push_back(known(FOLDERID_Windows));
    both.push_back(known(FOLDERID_ProgramFiles));
    // FOLDERID_ProgramFilesX86 may not exist on Windows ARM64, so derive only
    // when the known-folder call supplies a valid path.
    PWSTR x86 = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, KF_FLAG_DONT_VERIFY, nullptr, &x86))) {
        both.emplace_back(x86); CoTaskMemFree(x86);
    }
    for (const auto& root : both) {
        if (root.empty() || !root.is_absolute()) throw std::runtime_error("Invalid protected directory.");
        if (same_or_parent(path, root) || same_or_parent(root, path))
            throw std::runtime_error("Storage overlaps a protected application, workspace or another instance.");
    }
}
std::wstring extended(const fs::path& path) { return L"\\\\?\\" + path.wstring(); }
Held open_path(const fs::path& path, DWORD access, DWORD sharing = FILE_SHARE_READ) {
    auto handle = std::make_unique<Handle>(CreateFileW(extended(path).c_str(), access, sharing, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle->value == INVALID_HANDLE_VALUE) throw failure("Could not exclusively pin the registered storage entry");
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (!GetFileInformationByHandleEx(handle->value, FileAttributeTagInfo, &attributes, sizeof(attributes)))
        throw failure("Could not inspect the storage entry");
    if (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("Storage contains a junction, symlink or reparse point; deletion refused.");
    return handle;
}
BY_HANDLE_FILE_INFORMATION information(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION value{};
    if (!GetFileInformationByHandle(handle, &value)) throw failure("Could not inspect storage file identity");
    return value;
}
uint64_t file_id(const BY_HANDLE_FILE_INFORMATION& value) {
    return uint64_t(value.nFileIndexHigh) << 32 | value.nFileIndexLow;
}
std::vector<Held> pin_ancestors(const fs::path& path) {
    std::vector<Held> result;
    auto current = path.root_path();
    for (const auto& part : path.parent_path().relative_path()) {
        current /= part;
        auto handle = open_path(current, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE);
        if (!(information(handle->value).dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("A storage ancestor is not a directory.");
        result.push_back(std::move(handle));
    }
    return result;
}
std::vector<fs::path> entries(const fs::path& path) {
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW(extended(path / L"*").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
        throw failure("Could not enumerate the owned storage directory");
    }
    struct Search { HANDLE value; ~Search() { FindClose(value); } } close{search};
    std::vector<fs::path> result;
    do {
        if (std::wcscmp(data.cFileName, L".") && std::wcscmp(data.cFileName, L"..")) {
            if (result.size() >= 1000000) throw std::runtime_error("Storage directory exceeds safe enumeration limits.");
            result.push_back(path / data.cFileName);
        }
    } while (FindNextFileW(search, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw failure("Storage enumeration was interrupted");
    return result;
}
void preflight_tree(const fs::path& path, unsigned depth, uint64_t& count) {
    if (depth > 128 || ++count > 1000000) throw std::runtime_error("Storage tree exceeds safe removal limits.");
    for (const auto& child : entries(path)) {
        if (++count > 1000000) throw std::runtime_error("Storage tree exceeds safe removal limits.");
        // Attribute-only handles can coexist with QEMU's open disk; actual
        // delete handles are obtained only AFTER its exact process exits.
        auto held = open_path(child, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
        if (information(held->value).dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) preflight_tree(child, depth + 1, count);
    }
}
json read_owner(HANDLE handle) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0 || size.QuadPart > 16384)
        throw std::runtime_error("Missing or invalid storage ownership marker.");
    std::string text(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(handle, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) || read != text.size())
        throw failure("Could not read the storage ownership marker");
    return parse_json_document(text);
}
void mark_deleted(HANDLE handle) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)))
        throw failure("Owned storage entry could not be deleted; registration is retained for recovery");
}
void write_owner(const StorageIdentity& identity) {
    Handle marker(CreateFileW(extended(identity.directory / ownerName).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_HIDDEN, nullptr));
    if (marker.value == INVALID_HANDLE_VALUE) throw failure("Could not create a new instance ownership marker");
    std::string text = storage_identity_json(identity).dump(2) + '\n';
    DWORD written = 0;
    if (!WriteFile(marker.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size() ||
        !FlushFileBuffers(marker.value)) throw failure("Could not persist the storage ownership marker");
}
void remove_contents(const fs::path& path, unsigned depth, uint64_t& count, bool root) {
    if (depth > 128 || ++count > 1000000) throw std::runtime_error("Storage changed beyond safe removal limits.");
    for (const auto& child : entries(path)) {
        if (++count > 1000000) throw std::runtime_error("Storage changed beyond safe removal limits.");
        if (root && folded(child.filename()) == folded(fs::path(ownerName))) continue;
        auto held = open_path(child, DELETE | FILE_READ_ATTRIBUTES);
        if (information(held->value).dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            remove_contents(child, depth + 1, count, false);
        mark_deleted(held->value);
        // Close here before deleting the parent: disposition is delete-on-close.
    }
}
} // namespace

StorageIdentity claim_storage(const fs::path& directory, const std::string& uuid, const std::string& channel,
                              const std::vector<fs::path>& protectedTrees) {
    StorageIdentity identity{directory, uuid, channel, 0, 0};
    check_identity_fields(identity); safe_target(directory, protectedTrees);
    auto parents = pin_ancestors(directory);
    auto root = open_path(directory, FILE_READ_ATTRIBUTES);
    auto info = information(root->value);
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("Storage must be a directory.");
    identity.volumeSerial = info.dwVolumeSerialNumber; identity.directoryFileId = file_id(info);
    write_owner(identity);
    return identity;
}
StorageIdentity create_storage(const fs::path& directory, const std::string& uuid, const std::string& channel,
                               const std::vector<fs::path>& protectedTrees) {
    StorageIdentity identity{directory, uuid, channel, 0, 0};
    check_identity_fields(identity); safe_target(directory, protectedTrees);
    auto parents = pin_ancestors(directory);
    if (!CreateDirectoryW(extended(directory).c_str(), nullptr))
        throw failure("Storage destination must be a NEW directory with an existing parent");
    auto root = open_path(directory, FILE_READ_ATTRIBUTES);
    auto info = information(root->value);
    identity.volumeSerial = info.dwVolumeSerialNumber; identity.directoryFileId = file_id(info);
    write_owner(identity); return identity;
}
struct StorageLease::Impl {
    std::vector<Held> parents;
    Held root, marker;
};
StorageLease::StorageLease(const StorageIdentity& identity, const std::vector<fs::path>& protectedTrees)
    : impl(std::make_unique<Impl>()) {
    check_identity_fields(identity); safe_target(identity.directory, protectedTrees);
    impl->parents = pin_ancestors(identity.directory);
    impl->root = open_path(identity.directory, FILE_READ_ATTRIBUTES);
    const auto info = information(impl->root->value);
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.dwVolumeSerialNumber != identity.volumeSerial ||
        file_id(info) != identity.directoryFileId)
        throw std::runtime_error("Registered storage directory identity changed; operation refused.");
    impl->marker = open_path(identity.directory / ownerName, GENERIC_READ | FILE_READ_ATTRIBUTES);
    if (read_owner(impl->marker->value) != storage_identity_json(identity))
        throw std::runtime_error("Storage ownership does not match the manager registration.");
}
StorageLease::~StorageLease() = default;
json storage_identity_json(const StorageIdentity& identity) {
    return {{"ownerVersion", 1}, {"directory", utf8(identity.directory.wstring())},
            {"instanceUuid", identity.instanceUuid}, {"channel", identity.channel},
            {"volumeSerial", identity.volumeSerial}, {"directoryFileId", std::to_string(identity.directoryFileId)}};
}
StorageIdentity parse_storage_identity(const json& value) {
    if (!value.is_object() || value.size() != 6 || value.at("ownerVersion") != 1)
        throw std::runtime_error("Unsupported storage ownership record.");
    const auto& fileId = value.at("directoryFileId").get_ref<const std::string&>();
    if (fileId.empty() || fileId.size() > 20 || fileId.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid storage directory file identity.");
    const auto& serial = value.at("volumeSerial");
    if (!serial.is_number_unsigned() || serial.get<uint64_t>() > UINT32_MAX)
        throw std::runtime_error("Invalid storage volume identity.");
    StorageIdentity identity{fs::path(utf16(value.at("directory").get<std::string>())),
        value.at("instanceUuid").get<std::string>(), value.at("channel").get<std::string>(),
        value.at("volumeSerial").get<uint32_t>(), std::stoull(fileId)};
    check_identity_fields(identity); return identity;
}
void verify_storage_owner(const StorageIdentity& identity, const std::vector<fs::path>& protectedTrees) {
    StorageLease lease(identity, protectedTrees);
}
bool storage_target_missing(const StorageIdentity& identity, const std::vector<fs::path>& protectedTrees) {
    check_identity_fields(identity); safe_target(identity.directory, protectedTrees);
    auto parents = pin_ancestors(identity.directory);
    auto attributes = GetFileAttributesW(extended(identity.directory).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) return false;
    if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
        throw failure("Could not verify deletion recovery target absence");
    return true;
}
void delete_storage(const StorageIdentity& identity, const std::vector<fs::path>& protectedTrees,
                    const std::function<void()>& stopOwnedRuntime) {
    check_identity_fields(identity); safe_target(identity.directory, protectedTrees);
    if (!stopOwnedRuntime) throw std::runtime_error("Deletion requires an explicit owned-runtime stop operation.");
    auto parents = pin_ancestors(identity.directory);
    // Read-only pins coexist with the supervisor's running StorageLease.
    // Asking for DELETE access now would fail BEFORE the owned process can
    // be stopped, making --delete --force unusable for a live instance.
    auto root = open_path(identity.directory, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES);
    const auto info = information(root->value);
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.dwVolumeSerialNumber != identity.volumeSerial ||
        file_id(info) != identity.directoryFileId)
        throw std::runtime_error("Registered storage directory identity changed; deletion refused.");
    auto marker = open_path(identity.directory / ownerName, GENERIC_READ | FILE_READ_ATTRIBUTES);
    if (read_owner(marker->value) != storage_identity_json(identity))
        throw std::runtime_error("Storage ownership does not match the manager registration; deletion refused.");
    uint64_t count = 0; preflight_tree(identity.directory, 0, count);
    stopOwnedRuntime(); // Throwing leaves the entire storage untouched.
    // Keep the original objects alive while upgrading access. The first pins
    // forbid rename through the stop callback; overlapping delete-sharing
    // read handles retain their exact file IDs during the upgrade. A path
    // replacement is refused BEFORE any storage contents are removed.
    const auto markerInfo = information(marker->value);
    auto rootIdentity = open_path(identity.directory, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE);
    auto markerIdentity = open_path(identity.directory / ownerName, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE);
    root.reset(); marker.reset();
    root = open_path(identity.directory, DELETE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES);
    marker = open_path(identity.directory / ownerName, GENERIC_READ | DELETE | FILE_READ_ATTRIBUTES);
    const auto deletionRoot = information(root->value), deletionMarker = information(marker->value);
    if (deletionRoot.dwVolumeSerialNumber != info.dwVolumeSerialNumber || file_id(deletionRoot) != file_id(info) ||
        deletionMarker.dwVolumeSerialNumber != markerInfo.dwVolumeSerialNumber || file_id(deletionMarker) != file_id(markerInfo) ||
        read_owner(marker->value) != storage_identity_json(identity))
        throw std::runtime_error("Storage identity changed during runtime stop; deletion refused.");
    rootIdentity.reset(); markerIdentity.reset();
    count = 0; remove_contents(identity.directory, 0, count, true);
    // Marker survives partial failure and is the LAST file removed.
    if (entries(identity.directory).size() != 1)
        throw std::runtime_error("Storage changed during deletion; ownership and registration retained.");
    mark_deleted(marker->value); marker.reset();
    try { mark_deleted(root->value); }
    catch (...) {
        // The last directory deletion can fail (a concurrently created file,
        // antivirus or permissions). Restore the same owner for a safe retry;
        // an external journal is still required if this restoration also fails.
        write_owner(identity);
        throw;
    }
    root.reset();
    if (fs::exists(identity.directory))
        throw std::runtime_error("Storage removal has not completed; registration retained.");
}
} // namespace kiki
