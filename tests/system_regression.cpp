// SPDX-License-Identifier: GPL-2.0-or-later
// Developer-only REAL-SYSTEM runner. Never packaged in setup.exe. Does not
// execute the public CLI/desktop, install KikiEmu, or access its user registry.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "system_fixture.hpp"
#include "../src/kikiemu/package.hpp"
#include "../src/kikiemu/disk.hpp"
#include "../src/kikiemu/session.hpp"
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
using namespace kiki;
using json = nlohmann::json;
struct Handle {
    HANDLE value;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
fs::path self_executable() {
    wchar_t text[32768]; auto count = GetModuleFileNameW(nullptr, text, 32768);
    if (!count || count >= 32768) throw std::runtime_error("Could not identify the internal test executable.");
    return fs::path(text);
}
json read_record(const fs::path& file) {
    Handle input{CreateFileW(file.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER length{};
    if (input.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(input.value, &info) ||
        info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY) ||
        !GetFileSizeEx(input.value, &length) || length.QuadPart <= 0 || length.QuadPart > 2 * 1024 * 1024)
        throw std::runtime_error("Missing, redirected or oversized internal system-test marker.");
    std::string text(static_cast<size_t>(length.QuadPart), '\0'); DWORD count = 0;
    if (!ReadFile(input.value, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) || count != text.size())
        throw std::runtime_error("Could not read the internal system-test marker.");
    return parse_json_document(text);
}
void write_new_record(const fs::path& file, const json& record) {
    auto text = record.dump(2) + '\n';
    Handle output{CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    DWORD written = 0;
    if (output.value == INVALID_HANDLE_VALUE || !WriteFile(output.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
        written != text.size() || !FlushFileBuffers(output.value))
        throw std::runtime_error("Could not persist a NEW internal system-test marker.");
}
struct LoadedFixture {
    kiki_test::SystemFixture fixture;
    StorageLease rootLease, instanceLease;
    LoadedFixture(kiki_test::SystemFixture value)
        : fixture(std::move(value)), rootLease(fixture.root, {fixture.manager.appRoot}),
          instanceLease(fixture.instance, {fixture.manager.appRoot, fixture.manager.registryRoot}) {
        RegistryTransaction registry(fixture.manager.registryRoot, "release");
        if (registry.state().at("instances").size() != 1 || !registry.state().at("defaultId").is_null())
            throw std::runtime_error("Internal system test cannot operate a public/default/multi-instance registry.");
        kiki_test::verify_system_fixture_registration(fixture, registry.instance(fixture.id));
    }
};
void prepare(const fs::path& archive, const fs::path& bin, const fs::path& root, uint64_t total) {
    const auto app = self_executable().parent_path();
    auto binding = inspect_qemu(bin); // Static validation, never a bare GPU/WHPX probe.
    std::vector<fs::path> protectedTrees{app, bin, archive};
    auto rootOwner = create_storage(root, new_instance_uuid(), "release", protectedTrees);
    try {
        StorageLease rootLease(rootOwner, protectedTrees);
        auto package = read_system_package(archive, root / "package-staging");
        std::map<std::string, uint64_t> sizes; std::map<std::string, std::string> hashes;
        for (const auto& item : package.manifest.at("payloads")) {
            const auto role = item.at("role").get<std::string>();
            sizes[role] = item.at("bytes").get<uint64_t>(); hashes[role] = item.at("sha256").get<std::string>();
        }
        const auto minimum = package.manifest.at("minimumDataBytes").get<uint64_t>();
        plan_disk(total, sizes, minimum);
        auto instanceOwner = create_storage(root / "instance", new_instance_uuid(), "release", protectedTrees);
        json disk;
        {
            StorageLease instanceLease(instanceOwner, protectedTrees), staged(package.stagingOwner);
            disk = install_disk(binding, instanceOwner.directory / "disk", total, package.payloads, minimum, hashes);
        }
        delete_storage(package.stagingOwner, {instanceOwner.directory, app, bin, archive}, [] {});
        json source{{"manifest", package.manifest}, {"sourceLock", package.sourceLock},
                    {"archiveSha256", package.archiveSha256}, {"layout", disk}};
        RegistryTransaction registry(root / "registry", "release");
        auto id = registry.register_installed(instanceOwner, source, binding, Resources{});
        if (id != "01") throw std::runtime_error("NEW internal fixture unexpectedly reused existing registrations.");
        write_new_record(root / "system-regression.json", {{"fixtureVersion", 1},
            {"purpose", "internal-clean-system-regression"}, {"rootOwner", storage_identity_json(rootOwner)},
            {"instanceOwner", storage_identity_json(instanceOwner)}, {"id", id},
            {"appRoot", utf8(app.wstring())}, {"archiveSha256", package.archiveSha256}, {"totalBytes", total}});
        std::cout << "Internal REAL-SYSTEM fixture prepared from the validated ZIP; not yet booted/accepted.\n"
                  << "No public CLI/installer/default registry/PATH/shortcut was accessed.\n"
                  << source.dump(2) << '\n';
    } catch (...) {
        auto error = std::current_exception();
        // This root was CREATED by this call, not adopted from any existing
        // path. Owner/file identity checks remain mandatory during cleanup.
        delete_storage(rootOwner, protectedTrees, [] {});
        std::rethrow_exception(error);
    }
}
void boot(LoadedFixture& loaded) {
    const auto& fixture = loaded.fixture;
    const auto session = new_instance_uuid();
    Handle logRootPin{INVALID_HANDLE_VALUE};
    {
        RegistryTransaction registry(fixture.manager.registryRoot, "release");
        auto& record = registry.instance(fixture.id);
        if (!record.at("deleteJournal").is_null()) throw std::runtime_error("Internal test has an unfinished deletion.");
        if (!record.at("runtime").is_null())
            for (const auto& item : record.at("runtime").at("processes"))
                if (owned_process_is_live(parse_owned_process(item))) throw std::runtime_error("Internal test already has a live owned runtime.");
        verify_qemu_binding(record.at("configuration").at("qemu"));
        auto logRoot = fixture.instance.directory / "logs";
        if (!fs::exists(logRoot) && !fs::create_directory(logRoot)) throw std::runtime_error("Could not create internal test logs.");
        // Retain a lease on each existing parent before creating a child; never
        // accept a redirected/reused log directory as the new session.
        logRootPin.value = CreateFileW(logRoot.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION logInfo{};
        if (logRootPin.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(logRootPin.value, &logInfo) ||
            !(logInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || logInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("Internal test log directory is redirected or cannot be pinned.");
        auto log = logRoot / utf16(session);
        if (!fs::create_directory(log)) throw std::runtime_error("NEW internal test session logs already exist.");
        auto self = describe_owned_process(GetCurrentProcessId(), self_executable(), fixture.instance.instanceUuid, "release", "supervisor");
        record["runtime"] = {{"instanceUuid", fixture.instance.instanceUuid}, {"channel", "release"}, {"sessionUuid", session},
                             {"processes", json::array({owned_process_json(self)})}, {"endpoints", json::array()}};
        record["lifecycle"] = "starting"; record["lastError"] = nullptr; registry.save();
    }
    std::cout << "Starting REAL-SYSTEM regression using the shared full SDL/VirGL/120-Hz recipe.\n"
              << "Fixture: " << utf8(fixture.root.directory.wstring()) << "\nSession: " << session << std::endl;
    // The shared supervisor requires this exact recorded SELF identity. It
    // never invokes the public desktop or installed user-manager paths.
    supervise_instance(fixture.manager, fixture.id, session);
    std::cout << "Internal system session exited; fixture retained for persistence/reboot verification.\n";
}
void verify_guest_identity(const kiki_test::SystemFixture& fixture) {
    auto expected = "kiki-release-" + fixture.instance.instanceUuid;
    const auto identity = instance_shell(fixture.manager, fixture.id,
        "printf '%s\\n%s\\n' \"$(getprop ro.serialno)\" \"$(getprop ro.kikiaosp.build_channel)\"");
    if (identity != expected + "\nrelease\n") throw std::runtime_error("Internal test guest identity mismatch; requested command not executed.");
}
struct CloseWindow {
    OwnedProcess process;
    unsigned windows = 0;
    static BOOL CALLBACK visit(HWND window, LPARAM value) {
        auto& self = *reinterpret_cast<CloseWindow*>(value);
        DWORD owner = 0; GetWindowThreadProcessId(window, &owner);
        wchar_t className[128]{}; GetClassNameW(window, className, 128);
        if (owner != self.process.pid || !IsWindowVisible(window) ||
            std::wstring(className).find(L"SDL") != 0) return TRUE;
        if (!owned_process_is_live(self.process) || !PostMessageW(window, WM_CLOSE, 0, 0)) return FALSE;
        ++self.windows; return TRUE;
    }
};
void close_window(const kiki_test::SystemFixture& fixture) {
    json record;
    { RegistryTransaction registry(fixture.manager.registryRoot, "release"); record = registry.instance(fixture.id); }
    std::optional<OwnedProcess> target;
    for (const auto& item : record.at("runtime").at("processes")) {
        auto process = parse_owned_process(item);
        if (process.role == "qemu") {
            if (target) throw std::runtime_error("Multiple owned QEMU targets.");
            target = process;
        }
    }
    if (!target || !owned_process_is_live(*target)) throw std::runtime_error("No verified owned test QEMU.");
    CloseWindow request{*target}; EnumWindows(CloseWindow::visit, reinterpret_cast<LPARAM>(&request));
    if (request.windows != 1) throw std::runtime_error("Expected one owned SDL test window; no global window action was taken.");
    const auto deadline = GetTickCount64() + 60000;
    while (owned_process_is_live(*target) && GetTickCount64() < deadline) Sleep(100);
    if (owned_process_is_live(*target)) throw std::runtime_error("SDL close did not finish gracefully; test VM retained, not killed.");
    std::cout << "Actual owned SDL WM_CLOSE finished; inspect Android shutdown and media hashes.\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 6 && std::wstring(argv[1]) == L"--prepare") {
            prepare(normalize_directory(argv[2]), normalize_directory(argv[3]), normalize_directory(argv[4]), parse_gib(argv[5])); return 0;
        }
        if ((argc == 3 || argc == 4) && (std::wstring(argv[1]) == L"--boot" || std::wstring(argv[1]) == L"--describe" ||
             std::wstring(argv[1]) == L"--shutdown" || std::wstring(argv[1]) == L"--shell" || std::wstring(argv[1]) == L"--close-window")) {
            const auto mode = std::wstring(argv[1]);
            if ((mode == L"--shell") != (argc == 4)) throw std::runtime_error("Internal system-test argument count mismatch.");
            auto root = normalize_directory(argv[2]);
            LoadedFixture loaded(kiki_test::validate_system_fixture(read_record(root / "system-regression.json"), root, self_executable().parent_path()));
            const auto& fixture = loaded.fixture;
            if (mode == L"--boot") boot(loaded);
            else if (mode == L"--describe") {
                RegistryTransaction registry(fixture.manager.registryRoot, "release");
                std::cout << registry.instance(fixture.id).dump(2) << '\n';
            } else {
                verify_guest_identity(fixture);
                if (mode == L"--close-window") close_window(fixture);
                else std::cout << (mode == L"--shutdown" ? stop_instance(fixture.manager, fixture.id) :
                                  instance_shell(fixture.manager, fixture.id, utf8(argv[3])));
            }
            return 0;
        }
        std::cerr << "Developer-only REAL-SYSTEM regression. NOT public installer/CLI acceptance.\n"
                  << "  --prepare REAL_ZIP QEMU_BIN NEW_FIXTURE_ROOT TOTAL_GIB\n"
                  << "  --boot FIXTURE_ROOT\n  --describe FIXTURE_ROOT\n  --shell FIXTURE_ROOT COMMAND\n  --shutdown FIXTURE_ROOT\n  --close-window FIXTURE_ROOT\n"
                  << "No installed/public instance, user PATH, host ADB daemon or host settings are accessed.\n";
        return 2;
    } catch (const std::exception& error) { std::cerr << "Internal system regression failed: " << error.what() << '\n'; return 1; }
}
