// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include "registry.hpp"
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
namespace {
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE value) : value(value) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
std::wstring extended(const fs::path& path) { return L"\\\\?\\" + path.wstring(); }
std::wstring new_token() {
    GUID value{}; wchar_t text[40];
    if (FAILED(CoCreateGuid(&value)) || !StringFromGUID2(value, text, 40))
        throw std::runtime_error("Could not allocate a registry transaction identity.");
    return text;
}
std::string display_id(uint64_t number) {
    std::ostringstream output; output << std::setfill('0') << std::setw(2) << number; return output.str();
}
bool valid_id(const std::string& value) {
    if (value.size() < 2 || value.size() > 7 || value.find_first_not_of("0123456789") != std::string::npos) return false;
    auto number = std::stoull(value); return number > 0 && number <= 1000000 && display_id(number) == value;
}
json read_json(const fs::path& file) {
    Handle input(CreateFileW(extended(file).c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (input.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not read the instance registry.");
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER length{};
    if (!GetFileInformationByHandle(input.value, &info) || info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ||
        !GetFileSizeEx(input.value, &length) || length.QuadPart <= 0 || length.QuadPart > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid, redirected or oversized registry file; operation refused.");
    std::string text(static_cast<size_t>(length.QuadPart), '\0'); DWORD read = 0;
    if (!ReadFile(input.value, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) || read != text.size())
        throw std::runtime_error("Registry read was interrupted.");
    return parse_json_document(text);
}
void validate(const json& state, const std::string& channel) {
    if (!state.is_object() || state.size() != 5 || state.at("registryVersion") != 1 || state.at("channel") != channel ||
        !state.at("instances").is_object() || !state.at("nextId").is_number_unsigned())
        throw std::runtime_error("Unsupported or foreign registry; operation refused.");
    const auto next = state.at("nextId").get<uint64_t>();
    if (next < 1 || next > 1000001 || state.at("instances").size() > 1000000)
        throw std::runtime_error("Invalid registry identity counter.");
    std::set<std::string> uuids;
    for (const auto& [id, record] : state.at("instances").items()) {
        if (!valid_id(id) || std::stoull(id) >= next || !record.is_object() || record.size() != 8)
            throw std::runtime_error("Invalid instance registration.");
        auto owner = parse_storage_identity(record.at("owner"));
        if (owner.channel != channel || record.at("uuid") != owner.instanceUuid || !uuids.insert(owner.instanceUuid).second ||
            !record.at("immutableSource").is_object() || !record.at("configuration").is_object() ||
            record.at("configuration").size() != 2 || !record.at("configuration").at("qemu").is_object())
            throw std::runtime_error("Foreign or malformed immutable instance identity.");
        const auto& budget = record.at("configuration").at("resources");
        if (!budget.at("cpus").is_number_unsigned() || !budget.at("memoryBytes").is_number_unsigned())
            throw std::runtime_error("Registered resources must use unsigned integer tokens.");
        auto memory = budget.at("memoryBytes").get<uint64_t>(); auto cpus = budget.at("cpus").get<uint64_t>();
        if (budget.size() != 3 || !cpus || cpus > 64 || !memory || memory > INT64_MAX || memory % (1ULL << 30) ||
            !budget.at("preset").is_string()) throw std::runtime_error("Invalid registered resource configuration.");
        const auto lifecycle = record.at("lifecycle").get<std::string>();
        if (lifecycle != "idle" && lifecycle != "starting" && lifecycle != "running" && lifecycle != "stopping" &&
            lifecycle != "deleting" && lifecycle != "delete-failed") throw std::runtime_error("Unknown lifecycle state.");
        if (!record.at("lastError").is_null() && !record.at("lastError").is_string())
            throw std::runtime_error("Invalid recovery diagnostics.");
        const auto& runtime = record.at("runtime");
        if (runtime.is_null()) {
            if (lifecycle == "running" || lifecycle == "starting" || lifecycle == "stopping") throw std::runtime_error("Live lifecycle has no owned runtime.");
        } else {
            if (!runtime.is_object() || (runtime.size() != 4 && runtime.size() != 5) || runtime.at("instanceUuid") != owner.instanceUuid ||
                runtime.at("channel") != channel || !runtime.at("processes").is_array() || !runtime.at("endpoints").is_array())
                throw std::runtime_error("Foreign runtime registration.");
            if (runtime.size() == 5 && (!runtime.contains("sessionUuid") || !runtime.at("sessionUuid").is_string() ||
                !valid_instance_uuid(runtime.at("sessionUuid").get<std::string>())))
                throw std::runtime_error("Invalid owned session identity.");
            for (const auto& process : runtime.at("processes")) {
                auto value = parse_owned_process(process);
                if (value.instanceUuid != owner.instanceUuid || value.channel != channel)
                    throw std::runtime_error("Runtime process belongs to another instance.");
            }
        }
        const auto& journal = record.at("deleteJournal");
        if (!journal.is_null() && (!journal.is_object() || journal.size() != 2 ||
            journal.at("instanceUuid") != owner.instanceUuid || journal.at("owner") != record.at("owner")))
            throw std::runtime_error("Delete recovery identity mismatch.");
        if ((lifecycle == "deleting" || lifecycle == "delete-failed") && journal.is_null())
            throw std::runtime_error("Interrupted deletion has no journal; operation refused.");
    }
    if (!state.at("defaultId").is_null()) {
        auto id = state.at("defaultId").get<std::string>();
        if (!valid_id(id) || !state.at("instances").contains(id)) throw std::runtime_error("Default references an unknown instance.");
    }
}
} // namespace
struct RegistryTransaction::Impl {
    fs::path root;
    std::string channel;
    std::vector<std::unique_ptr<Handle>> pins;
    std::unique_ptr<Handle> lock;
    json state;
};
RegistryTransaction::RegistryTransaction(const fs::path& root, const std::string& channel) : impl(std::make_unique<Impl>()) {
    if (channel != "release" && channel != "dev") throw std::runtime_error("Unknown manager channel.");
    if (!root.is_absolute() || root.root_name().wstring().size() != 2 || root == root.root_path())
        throw std::runtime_error("Registry requires a local absolute manager directory.");
    impl->root = root.lexically_normal().make_preferred(); impl->channel = channel;
    // Pin each existing ancestor BEFORE creating a missing child. This avoids
    // creating registry files through an attacker-replaced junction first.
    auto current = impl->root.root_path();
    for (const auto& part : impl->root.relative_path()) {
        current /= part;
        if (!fs::exists(current) && !CreateDirectoryW(extended(current).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Could not create the manager registry directory.");
        auto held = std::make_unique<Handle>(CreateFileW(extended(current).c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (held->value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(held->value, &info) ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("Registry directory is redirected or cannot be pinned; operation refused.");
        impl->pins.push_back(std::move(held));
    }
    impl->lock = std::make_unique<Handle>(CreateFileW(extended(impl->root / "registry.lock").c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (impl->lock->value == INVALID_HANDLE_VALUE) throw std::runtime_error("Another manager operation is active; retry when it finishes.");
    BY_HANDLE_FILE_INFORMATION lockInfo{};
    if (!GetFileInformationByHandle(impl->lock->value, &lockInfo) || lockInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("Redirected registry lock; operation refused.");
    const auto file = impl->root / "registry.json";
    auto attributes = GetFileAttributesW(extended(file).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) impl->state = read_json(file);
    else {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) throw std::runtime_error("Registry absence could not be verified; operation refused.");
        impl->state = {{"registryVersion", 1}, {"channel", channel}, {"nextId", uint64_t(1)},
                       {"defaultId", nullptr}, {"instances", json::object()}};
    }
    validate(impl->state, channel);
}
RegistryTransaction::~RegistryTransaction() = default;
const json& RegistryTransaction::state() const { return impl->state; }
json& RegistryTransaction::instance(const std::string& id) {
    if (!valid_id(id) || !impl->state.at("instances").contains(id)) throw std::runtime_error("Unknown instance ID: " + id + ".");
    return impl->state["instances"][id];
}
void RegistryTransaction::save() {
    validate(impl->state, impl->channel);
    std::string text = impl->state.dump(2) + '\n';
    if (text.size() > 16 * 1024 * 1024) throw std::runtime_error("Instance registry exceeds its size limit.");
    auto pending = impl->root / (L"registry.pending-" + new_token() + L".json");
    {
        Handle output(CreateFileW(extended(pending).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD written = 0;
        if (output.value == INVALID_HANDLE_VALUE || !WriteFile(output.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
            written != text.size() || !FlushFileBuffers(output.value)) throw std::runtime_error("Registry update could not be persisted; previous records retained.");
    }
    if (!MoveFileExW(extended(pending).c_str(), extended(impl->root / "registry.json").c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Registry atomic replacement failed; previous records retained.");
}
std::string RegistryTransaction::register_installed(const StorageIdentity& owner, const json& source,
                                                   const json& binding, const Resources& resources) {
    if (owner.channel != impl->channel) throw std::runtime_error("Cannot register another channel's storage.");
    std::vector<fs::path> protectedTrees{impl->root};
    protectedTrees.emplace_back(utf16(binding.at("binDirectory").get<std::string>()));
    for (const auto& [id, other] : impl->state.at("instances").items()) {
        (void)id;
        protectedTrees.push_back(parse_storage_identity(other.at("owner")).directory);
        protectedTrees.emplace_back(utf16(other.at("configuration").at("qemu").at("binDirectory").get<std::string>()));
    }
    verify_storage_owner(owner, protectedTrees);
    const auto number = impl->state.at("nextId").get<uint64_t>();
    if (number > 1000000) throw std::runtime_error("Instance ID space exhausted.");
    const auto id = display_id(number);
    impl->state["instances"][id] = {{"uuid", owner.instanceUuid}, {"owner", storage_identity_json(owner)},
        {"immutableSource", source}, {"configuration", {{"qemu", binding}, {"resources", {
            {"cpus", resources.cpus}, {"memoryBytes", resources.memoryBytes}, {"preset", resources.preset}}}}},
        {"runtime", nullptr}, {"lifecycle", "idle"}, {"deleteJournal", nullptr}, {"lastError", nullptr}};
    impl->state["nextId"] = number + 1;
    save(); return id;
}
void RegistryTransaction::set_default(const std::string& id) {
    const auto& record = instance(id);
    if (!record.at("deleteJournal").is_null()) throw std::runtime_error("Instance has an unfinished deletion; it cannot be default.");
    impl->state["defaultId"] = id; save();
}
bool RegistryTransaction::delete_instance(const std::string& id, const std::vector<fs::path>& appProtectedTrees) {
    auto& record = instance(id);
    auto owner = parse_storage_identity(record.at("owner"));
    std::vector<fs::path> protectedTrees = appProtectedTrees; protectedTrees.push_back(impl->root);
    for (const auto& [otherId, other] : impl->state.at("instances").items()) {
        if (otherId != id) protectedTrees.push_back(parse_storage_identity(other.at("owner")).directory);
        protectedTrees.emplace_back(utf16(other.at("configuration").at("qemu").at("binDirectory").get<std::string>()));
    }
    std::vector<OwnedProcess> processes;
    std::vector<OwnedEndpoint> endpoints;
    if (!record.at("runtime").is_null()) {
        for (const auto& process : record.at("runtime").at("processes")) processes.push_back(parse_owned_process(process));
        for (const auto& endpoint : record.at("runtime").at("endpoints")) {
            auto port = endpoint.at("port").get<uint64_t>(), pid = endpoint.at("pid").get<uint64_t>();
            if (!port || port > 65535 || !pid || pid > UINT32_MAX) throw std::runtime_error("Invalid registered endpoint.");
            endpoints.push_back({static_cast<uint32_t>(pid), static_cast<uint16_t>(port)});
        }
    }
    bool journaled = false;
    try {
        auto stop = [&] {
            verify_owned_endpoints(processes, endpoints);
            record["lifecycle"] = "deleting";
            record["deleteJournal"] = {{"instanceUuid", owner.instanceUuid}, {"owner", record.at("owner")}};
            record["lastError"] = nullptr; save(); journaled = true;
            force_stop_owned(owner.instanceUuid, impl->channel, processes);
        };
        if (!record.at("deleteJournal").is_null() && storage_target_missing(owner, protectedTrees)) {
            // Crash after actual removal but before atomic unregister. This
            // journal is already tied to the original owner; do not create,
            // follow or remove a replacement path to "repair" the operation.
            stop();
        } else delete_storage(owner, protectedTrees, stop);
    } catch (const std::exception& error) {
        if (journaled) {
            record["lifecycle"] = "delete-failed"; record["lastError"] = std::string(error.what()).substr(0, 4096);
            save();
        }
        throw;
    }
    const bool wasDefault = impl->state.at("defaultId") == id;
    impl->state["instances"].erase(id);
    if (wasDefault) impl->state["defaultId"] = nullptr;
    save(); return wasDefault;
}
} // namespace kiki
