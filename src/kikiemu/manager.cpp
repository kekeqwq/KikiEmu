// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include "manager.hpp"
#include "package.hpp"
#include "disk.hpp"
#include "session.hpp"
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
namespace {
std::string value(const Command& command, const char* name) { return utf8(command.options.at(name)); }
std::vector<fs::path> protected_paths(const RegistryTransaction& registry, const ManagerPaths& paths) {
    std::vector<fs::path> result{paths.appRoot, paths.registryRoot};
    for (const auto& [id, record] : registry.state().at("instances").items()) {
        (void)id;
        result.push_back(parse_storage_identity(record.at("owner")).directory);
        result.emplace_back(utf16(record.at("configuration").at("qemu").at("binDirectory").get<std::string>()));
    }
    return result;
}
Resources saved_resources(const json& record) {
    const auto& value = record.at("configuration").at("resources");
    return {value.at("cpus").get<uint32_t>(), value.at("memoryBytes").get<uint64_t>(), value.at("preset").get<std::string>()};
}
std::string display_status(const json& record) {
    auto status = record.at("lifecycle").get<std::string>();
    if (status == "deleting" || status == "delete-failed" || record.at("runtime").is_null()) return status;
    bool live = false;
    for (const auto& process : record.at("runtime").at("processes"))
        live = owned_process_is_live(parse_owned_process(process)) || live;
    return live ? status : "idle";
}
std::string create_instance(const Command& command, const ManagerPaths& paths, RegistryTransaction& registry, const Progress& progress) {
    if (paths.channel != "release") throw std::runtime_error("Published system packages cannot initialize a development identity.");
    report(progress, "Resolving system, storage and QEMU paths (~/ expands to the current user's home).");
    const auto archive = normalize_directory(command.options.at("system"));
    const auto target = normalize_directory(command.options.at("storage"));
    const auto bin = normalize_directory(command.options.at("qemu"));
    const auto capacity = parse_gib(command.options.at("size"));
    if (fs::exists(target)) throw std::runtime_error("Storage destination already exists. Choose a NEW directory; no existing data will be overwritten.");
    report(progress, "System package: " + utf8(archive.wstring()));
    report(progress, "Storage: " + utf8(target.wstring()) + " (" + std::to_string(capacity >> 30) + " GiB total)");
    if (!fs::is_regular_file(archive)) throw std::runtime_error("System package does not exist or is not a ZIP file: " + utf8(archive.wstring()) + ". Check --system.");
    auto binding = prepare_qemu(bin, progress);
    auto protect = protected_paths(registry, paths); protect.push_back(bin); protect.push_back(archive);
    const auto uuid = new_instance_uuid();
    auto package = read_system_package(archive, paths.registryRoot / "staging" / utf16(uuid), progress);
    std::optional<StorageIdentity> owner;
    bool stagePresent = true;
    try {
        std::map<std::string, uint64_t> sizes; std::map<std::string, std::string> hashes;
        for (const auto& item : package.manifest.at("payloads")) {
            auto role = item.at("role").get<std::string>();
            sizes[role] = item.at("bytes").get<uint64_t>(); hashes[role] = item.at("sha256").get<std::string>();
        }
        const auto minimum = package.manifest.at("minimumDataBytes").get<uint64_t>();
        plan_disk(capacity, sizes, minimum, package.manifest.at("layoutVersion") == "gpt-ab-v1"); // Reject insufficient TOTAL capacity before creating the target.
        owner = create_storage(target, uuid, paths.channel, protect);
        json disk;
        {
            StorageLease destination(*owner, protect);
            StorageLease staged(package.stagingOwner);
            disk = install_disk(binding, target / "disk", capacity, package.payloads, minimum, hashes, progress, package.manifest.at("layoutVersion") == "gpt-ab-v1");
        }
        // Everything necessary is now in the standalone disk/cache. Nothing
        // at runtime depends on the original download or these extracted files.
        report(progress, "Cleaning this installation's owned temporary staging files.");
        delete_storage(package.stagingOwner, {target, bin, archive, paths.appRoot}, [] {}); stagePresent = false;
        json source{{"manifest", package.manifest}, {"sourceLock", package.sourceLock},
                    {"archiveSha256", package.archiveSha256}, {"layout", disk}};
        report(progress, "Registering the verified persistent instance.");
        auto id = registry.register_installed(*owner, source, binding, resources(command));
        return "Created id " + id + "\n";
    } catch (...) {
        auto original = std::current_exception();
        // Fail closed: these identities belong ONLY to directories freshly
        // created by this transaction, never a preexisting user's folder.
        // An ownership/removal failure retains the marker and reports failure.
        std::string cleanupFailures;
        auto cleanup = [&](const StorageIdentity& identity, const std::vector<fs::path>& roots) {
            try { delete_storage(identity, roots, [] {}); }
            catch (const std::exception& error) {
                cleanupFailures += " Kept " + utf8(identity.directory.wstring()) + ": " + error.what() + ";";
            }
        };
        if (owner) cleanup(*owner, protect);
        if (stagePresent) cleanup(package.stagingOwner, {target, bin, archive, paths.appRoot});
        if (!cleanupFailures.empty()) throw std::runtime_error("Installation failed: " + exception_message(original) +
            ". Owned temporary cleanup also failed:" + cleanupFailures);
        std::rethrow_exception(original);
    }
}
} // namespace
ManagerPaths user_manager_paths() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        throw std::runtime_error("Could not locate current-user application data.");
    fs::path root(local); CoTaskMemFree(local);
    wchar_t executable[32768]; DWORD count = GetModuleFileNameW(nullptr, executable, 32768);
    if (!count || count >= 32768) throw std::runtime_error("Could not locate the installed manager.");
    return {root / "KikiEmu" / "release", fs::path(executable).parent_path(), "release"};
}
const char* management_help() {
    return "KikiEmu 0.3.0-alpha\n"
           "  kikiemu create --system ZIP --storage NEW_FOLDER --size 200g --qemu QEMU_BIN [--performance default|medium|high]\n"
           "  kikiemu list\n"
           "  kikiemu set --default ID\n"
           "  kikiemu set --id ID [--mem 8g] [--cpus 8] [--performance default|medium|high] [--qemu QEMU_BIN]\n"
           "  kikiemu info --id ID\n"
           "  kikiemu doctor --qemu QEMU_BIN\n"
           "  kikiemu start [--id ID]\n"
           "  kikiemu stop --id ID\n"
           "  kikiemu logs --id ID\n"
           "  kikiemu adb --id ID --shell COMMAND\n"
           "  kikiemu update --id ID --action status|check|reboot\n"
           "  kikiemu update --id ID --action apply --package SIGNED_FULL_OTA_ZIP\n"
           "  kikiemu delete --force --id ID\n"
           "Commands also accept --create, --list, --set and --delete spellings.\n"
           "Size is immutable TOTAL GiB; disks allocate host space as used.\n"
           "Delete permanently removes ALL storage/data for the explicitly named instance.\n";
}
std::string execute_management(const Command& command, const ManagerPaths& paths, const Progress& progress) {
    if (command.name == "help") return management_help();
    if (command.name == "version") return "KikiEmu 0.3.0-alpha (Windows ARM64)\n";
    if (command.name == "doctor") {
        if (!command.options.contains("qemu")) throw std::runtime_error("Use doctor --qemu QEMU_BIN.");
        return prepare_qemu(normalize_directory(command.options.at("qemu")), progress).dump(2) +
            "\nRuntime files verified. Actual WHPX/GPU/display compatibility still requires a system boot.\n";
    }
    if (command.name == "start") return start_instance(paths, command.options.contains("id") ? value(command, "id") : "");
    if (command.name == "stop") return stop_instance(paths, value(command, "id"));
    if (command.name == "logs") return instance_logs(paths, value(command, "id"));
    if (command.name == "adb") return instance_shell(paths, value(command, "id"), value(command, "shell"));
    if (command.name == "update") return instance_update(paths, value(command,"id"), value(command,"action"), command.options.contains("package") ? normalize_directory(command.options.at("package")) : fs::path{}, progress);
    RegistryTransaction registry(paths.registryRoot, paths.channel);
    if (command.name == "create") return create_instance(command, paths, registry, progress);
    if (command.name == "list") {
        std::ostringstream out; out << "id  system          storage  size  cpu  gpu    mem  status\n";
        for (const auto& id : registry.ordered_ids()) {
            const auto& record = registry.state().at("instances").at(id);
            auto budget = saved_resources(record);
            out << id << "  kikiaosp_test  " << record.at("owner").at("directory").get<std::string>() << "  "
                << (record.at("immutableSource").at("layout").at("totalBytes").get<uint64_t>() >> 30) << "g  "
                << budget.cpus << "  VirGL  " << (budget.memoryBytes >> 30) << "g  "
                << display_status(record) << '\n';
        }
        return out.str();
    }
    if (command.name == "set") {
        if (command.options.contains("default")) {
            auto id = value(command, "default"); registry.set_default(id); return "Set default system to " + id + "\n";
        }
        auto id = value(command, "id"); auto& record = registry.instance(id);
        if (!record.at("deleteJournal").is_null()) throw std::runtime_error("Instance has an unfinished deletion; settings cannot be changed.");
        auto budget = updated_resources(command, saved_resources(record));
        json binding = record.at("configuration").at("qemu");
        if (command.options.contains("qemu")) {
            auto bin = normalize_directory(command.options.at("qemu")); binding = prepare_qemu(bin, progress);
            for (const auto& [otherId, other] : registry.state().at("instances").items()) {
                (void)otherId; verify_storage_owner(parse_storage_identity(other.at("owner")), {bin, paths.appRoot, paths.registryRoot});
            }
        }
        record["configuration"] = {{"qemu", binding}, {"resources", {{"cpus", budget.cpus}, {"memoryBytes", budget.memoryBytes}, {"preset", budget.preset}}}};
        registry.save(); return "Updated instance " + id + ". Changes will take effect on the next start.\n";
    }
    if (command.name == "info") return registry.instance(value(command, "id")).dump(2) + '\n';
    if (command.name == "delete") {
        // Enforce destructive intent HERE too, not solely in the CLI parser.
        // Internal callers cannot delete by constructing an incomplete Command.
        if (!command.options.contains("id") || command.options.find("force") == command.options.end() ||
            command.options.at("force") != L"true") throw std::runtime_error("Use delete --force --id ID to permanently remove that instance.");
        auto id = value(command, "id"); const bool wasDefault = registry.delete_instance(id, {paths.appRoot});
        return "Deleted instance " + id + " and its storage.\n" + (wasDefault ? "Default system cleared.\n" : "");
    }
    throw std::runtime_error("Runtime supervision is not yet available in this development build. No VM was started or stopped.");
}
} // namespace kiki
