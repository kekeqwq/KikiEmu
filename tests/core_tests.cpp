// SPDX-License-Identifier: GPL-2.0-or-later
// Internal library tests. No released CLI, installer, registry or VM is used.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <winioctl.h>
#include "../src/kikiemu/runtime.hpp"
#include "../src/kikiemu/disk.hpp"
#include "../src/kikiemu/boot.hpp"
#include "../src/kikiemu/storage.hpp"
#include "../src/kikiemu/lifecycle.hpp"
#include "../src/kikiemu/registry.hpp"
#include "../src/kikiemu/package.hpp"
#include "../src/kikiemu/manager.hpp"
#include "../src/kikiemu/process.hpp"
#include "../src/kikiemu/transport.hpp"
#include "../src/kikiemu/session.hpp"
#include "../src/kikiemu/installation.hpp"
#include "package_contract.generated.hpp"
#define LIBARCHIVE_STATIC
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "wire_fixture.hpp"

static unsigned passed = 0;
static void check(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
    ++passed;
}
template<class Function> static void rejects(Function function, const char* name) {
    try { function(); } catch (const std::exception&) { ++passed; return; }
    throw std::runtime_error(name);
}
static void write_fixture(const kiki::fs::path& path, const std::string& value) {
    // Existing hidden ownership markers cannot be reopened by CREATE_ALWAYS
    // with FILE_ATTRIBUTE_NORMAL. Open the exact existing fixture and truncate
    // via its handle instead; new fixture names use CREATE_NEW.
    const auto exists = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               exists ? OPEN_EXISTING : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open an isolated library-test fixture.");
    DWORD written = 0;
    bool okay = WriteFile(output, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) &&
                written == value.size() && SetEndOfFile(output) && FlushFileBuffers(output);
    CloseHandle(output);
    if (!okay) throw std::runtime_error("Could not write an isolated library-test fixture.");
}
static kiki::fs::path temporary_fixture_root() {
    wchar_t temp[32768];
    if (!GetTempPathW(32768, temp)) throw std::runtime_error("Could not get the test temporary directory.");
    GUID uuid{}; wchar_t text[40];
    if (FAILED(CoCreateGuid(&uuid)) || !StringFromGUID2(uuid, text, 40)) throw std::runtime_error("Test UUID generation failed.");
    auto result = kiki::fs::path(temp) / (L"kikiemu-library-tests-" + std::wstring(text));
    if (!kiki::fs::create_directory(result)) throw std::runtime_error("Test directory was not new.");
    return result;
}
static void make_test_junction(const kiki::fs::path& link, const kiki::fs::path& target) {
    kiki::fs::create_directory(link);
    HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open the isolated test junction.");
    std::wstring substitute = L"\\??\\" + target.wstring(), print = target.wstring();
    std::vector<unsigned char> bytes(16 + (substitute.size() + print.size() + 2) * sizeof(wchar_t));
    auto put16 = [&](size_t at, uint16_t value) { bytes[at] = value; bytes[at + 1] = value >> 8; };
    DWORD tag = IO_REPARSE_TAG_MOUNT_POINT; std::memcpy(bytes.data(), &tag, 4);
    put16(4, static_cast<uint16_t>(bytes.size() - 8));
    put16(8, 0); put16(10, static_cast<uint16_t>(substitute.size() * 2));
    put16(12, static_cast<uint16_t>((substitute.size() + 1) * 2)); put16(14, static_cast<uint16_t>(print.size() * 2));
    std::memcpy(bytes.data() + 16, substitute.c_str(), (substitute.size() + 1) * 2);
    std::memcpy(bytes.data() + 16 + (substitute.size() + 1) * 2, print.c_str(), (print.size() + 1) * 2);
    DWORD returned = 0;
    BOOL okay = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, bytes.data(), static_cast<DWORD>(bytes.size()),
                               nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    if (!okay) throw std::runtime_error("Could not create the isolated test junction.");
}
struct TestChild {
    HANDLE process = nullptr;
    DWORD pid = 0;
    explicit TestChild(const kiki::fs::path& executable) {
        auto command = kiki::quote_windows_arg(executable.wstring()) + L" --owned-library-test-child";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, executable.parent_path().c_str(), &startup, &child))
            throw std::runtime_error("Could not start isolated library-test child.");
        CloseHandle(child.hThread); process = child.hProcess; pid = child.dwProcessId;
    }
    ~TestChild() { if (process) { if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) TerminateProcess(process, 1); CloseHandle(process); } }
};
static void fixture_zip(const kiki::fs::path& path, const std::vector<std::pair<std::string, std::string>>& files) {
    archive* writer = archive_write_new();
    if (!writer) throw std::runtime_error("Could not allocate test ZIP writer.");
    struct Cleanup { archive* value; ~Cleanup() { archive_write_free(value); } } cleanup{writer};
    if (archive_write_set_format_zip(writer) != ARCHIVE_OK ||
        archive_write_set_options(writer, "zip:compression=store") != ARCHIVE_OK ||
        archive_write_open_filename_w(writer, path.c_str()) != ARCHIVE_OK) throw std::runtime_error("Could not create fixture ZIP.");
    for (const auto& [name, data] : files) {
        archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.c_str()); archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644); archive_entry_set_size(entry, data.size()); archive_entry_set_mtime(entry, 315532800, 0);
        int status = archive_write_header(writer, entry); archive_entry_free(entry);
        if (status != ARCHIVE_OK || archive_write_data(writer, data.data(), data.size()) != static_cast<la_ssize_t>(data.size()))
            throw std::runtime_error("Could not write fixture ZIP entry.");
    }
    if (archive_write_close(writer) != ARCHIVE_OK) throw std::runtime_error("Could not close fixture ZIP.");
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--leased-library-test-child") {
        try {
            std::ifstream input{ kiki::fs::path(argv[2]) }; std::string text((std::istreambuf_iterator<char>(input)), {});
            kiki::StorageLease lease(kiki::parse_storage_identity(kiki::parse_json_document(text)));
            { std::ofstream ready{kiki::fs::path(argv[3])}; ready << "READY\n"; }
            Sleep(INFINITE); return 0;
        } catch (...) { return 30; }
    }
    if (argc == 5 && std::wstring(argv[1]) == L"--wire-library-test-child")
        return wire_fixture::serve(static_cast<uint16_t>(std::stoul(argv[2])), argv[3], argv[4]);
    if (argc == 2 && std::wstring(argv[1]) == L"--owned-library-test-child") {
        Sleep(INFINITE); return 0;
    }
    try {
        using namespace kiki;
        if (argc != 1 && !(argc == 3 && std::wstring(argv[1]) == L"--producer-package-fixture"))
            throw std::runtime_error("Unsupported internal test argument.");
        check(parse_command({}).name == "help", "empty help");
        check(parse_command({L"--list"}).name == "list", "list alias");
        check(add_path_segment(L"C:\\other;", L"C:\\KikiEmu") == L"C:\\other;;C:\\KikiEmu", "append preserves existing empty PATH segment");
        check(add_path_segment(L"C:\\other; \"c:/kikiemu/\" ;C:\\last", L"C:\\KikiEmu") == L"C:\\other; \"c:/kikiemu/\" ;C:\\last", "equivalent existing PATH is not replaced");
        check(remove_path_segment(L"C:\\other;;C:\\KikiEmu;C:\\last;", L"C:\\KikiEmu") == L"C:\\other;;C:\\last;", "remove only exact PATH segment, preserves unrelated bytes");
        check(remove_path_segment(L"C:\\KikiEmu-dev;C:\\KikiEmu", L"C:\\KikiEmu") == L"C:\\KikiEmu-dev", "release PATH removal leaves dev prefix");
        rejects([&] { add_path_segment(L"C:\\other", L"C:\\KikiEmu;C:\\foreign"); }, "PATH separator injection accepted");
        rejects([&] { add_path_segment(std::wstring(32760, L'x'), L"C:\\KikiEmu"); }, "oversized PATH truncated/accepted");
        auto deletion = parse_command({L"--delete", L"--force", L"--id", L"01"});
        check(deletion.name == "delete" && deletion.options.at("force") == L"true" && deletion.options.at("id") == L"01", "delete force is a valueless flag");
        check(parse_command({L"delete", L"--id", L"01", L"--force"}).name == "delete", "delete subcommand option ordering");
        rejects([] { parse_command({L"delete", L"--id", L"01"}); }, "deletion without explicit force accepted");
        rejects([] { parse_command({L"delete", L"--force"}); }, "deletion without explicit ID accepted");
        rejects([] { parse_command({L"delete", L"--id", L"01", L"--force", L"false"}); }, "ambiguous force value accepted");
        rejects([] { parse_command({L"delete", L"--force", L"--id", L"01", L"--storage", L"C:/"}); }, "user-controlled delete path accepted");
        rejects([] { parse_command({L"delete", L"--force", L"--force", L"--id", L"01"}); }, "duplicate force flag accepted");
        auto create = parse_command({L"--create", L"--system", L"~/Downloads/os.zip", L"--storage",
                                     L"~/My Android", L"--size", L"200g", L"--qemu", L"~/Tools/Qemu/bin"});
        check(create.name == "create", "create alias");
        check(parse_gib(create.options.at("size")) == 200ULL << 30, "non-fixed total capacity");
        check(resources(create).cpus == 8 && resources(create).memoryBytes == 4ULL << 30, "default budget");
        check(parse_gib(L"128G") == 128ULL << 30, "capital unit");
        for (auto bad : {L"0g", L"-1g", L"1.5g", L"200", L"200gb", L"18446744073709551615g"})
            rejects([&] { parse_gib(bad); }, "invalid capacity accepted");
        rejects([] { parse_command({L"create", L"--system", L"os.zip", L"--storage", L"data", L"--size", L"200g"}); }, "missing qemu accepted");
        rejects([] { parse_command({L"create", L"--qemu", L"bin"}); }, "missing installation inputs accepted");
        auto set = parse_command({L"--set", L"--id", L"01", L"--qemu", L"C:/New Qemu/bin"});
        check(set.name == "set" && set.options.at("qemu") == L"C:/New Qemu/bin", "runtime rebind alias");
        rejects([] { parse_command({L"set", L"--id", L"01", L"--size", L"200g"}); }, "capacity mutation accepted");
        rejects([] { parse_command({L"set", L"--id", L"01", L"--system", L"other.zip"}); }, "OS mutation accepted");
        rejects([] { parse_command({L"set", L"--id", L"01", L"--qemu-args", L"-snapshot"}); }, "raw args accepted");
        rejects([] { parse_command({L"set", L"--id", L"01", L"--mem", L"8g", L"--mem", L"4g"}); }, "duplicate option accepted");
        rejects([] { parse_command({L"set", L"--default", L"01", L"--qemu", L"bin"}); }, "default mixed with rebind");
        rejects([] { parse_command({L"set", L"--id", L"01"}); }, "empty update accepted");
        rejects([] { parse_command({L"set", L"--id", L"01", L"--qemu"}); }, "missing option value accepted");
        rejects([] { parse_command({L"set", L"--id", L"01", L"--cpus", L"0"}); }, "invalid CPUs accepted");
        rejects([] { parse_command({L"set", L"--id", L"01", L"--performance", L"ultra"}); }, "unknown preset accepted");
        auto high = resources(parse_command({L"set", L"--id", L"01", L"--performance", L"high"}));
        check(high.cpus == 10 && high.memoryBytes == 8ULL << 30, "high preset");
        auto custom = resources(parse_command({L"set", L"--id", L"01", L"--mem", L"8g", L"--cpus", L"6"}));
        check(custom.cpus == 6 && custom.memoryBytes == 8ULL << 30 && custom.preset == "custom", "resource override");
        auto memoryOnly = updated_resources(parse_command({L"set", L"--id", L"01", L"--mem", L"6g"}), custom);
        check(memoryOnly.cpus == 6 && memoryOnly.memoryBytes == 6ULL << 30, "memory-only update preserves saved CPU count");
        auto cpuOnly = updated_resources(parse_command({L"set", L"--id", L"01", L"--cpus", L"10"}), custom);
        check(cpuOnly.cpus == 10 && cpuOnly.memoryBytes == 8ULL << 30, "CPU-only update preserves saved memory");
        check(normalize_directory(L"~/Tools/../My Android").is_absolute(), "tilde path normalized");
        rejects([] { normalize_directory(L"~someone/bin"); }, "foreign home expansion accepted");
        rejects([] { normalize_directory(L"C:bin"); }, "drive relative path accepted");
        check(utf16(utf8(L"测试路径 / QEMU")) == L"测试路径 / QEMU", "Unicode round trip");
        for (auto value : {L"", L"space path", L"C:\\Qemu path\\", L"quoted\"name", L"C:\\x\\\"y"}) {
            auto line = L"tool " + quote_windows_arg(value);
            int count = 0;
            auto argv = CommandLineToArgvW(line.c_str(), &count);
            check(argv && count == 2 && argv[1] == std::wstring(value), "Windows argv quote round trip");
            LocalFree(argv);
        }
        wchar_t executable[32768];
        DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        check(length > 0 && inspect_pe(fs::path(executable)).machine == 0xaa64, "native ARM64 build");
        check(sha256(fs::path(executable)).size() == 64, "streaming SHA-256");
        check(parse_json_document("{\"items\":[{\"x\":1},{\"x\":2}],\"x\":3}").at("x") == 3,
              "distinct nested objects may use the same key");
        rejects([] { parse_json_document("{\"a\":1,\"a\":2}"); }, "top-level duplicate JSON key accepted");
        rejects([] { parse_json_document("{\"items\":[{\"a\":1,\"a\":2}]}"); }, "nested duplicate JSON key accepted");
        rejects([] { parse_json_document("\xef\xbb\xbf{}"); }, "product JSON BOM accepted");
        rejects([] { parse_json_document(std::string(100, '[') + "0" + std::string(100, ']')); }, "excessive JSON nesting accepted");
        auto contractFixtures = parse_json_document(kiki_contract::fixtures);
        auto contractValid = contractFixtures.at("validManifest");
        check(validate_manifest(contractValid).size() == 7, "canonical producer metadata fixture accepted by native consumer");
        for (const auto& mutation : contractFixtures.at("negativeMutations")) {
            auto bad = contractValid;
            bad[nlohmann::json::json_pointer(mutation.at("pointer").get<std::string>())] = mutation.at("value");
            rejects([&] { validate_manifest(bad); }, mutation.at("name").get_ref<const std::string&>().c_str());
        }
        rejects([] { validate_schema(1, {{"anyOf", nlohmann::json::array()}}); }, "future schema vocabulary silently ignored");
        rejects([] { validate_schema(1, {{"$ref", "https://untrusted.invalid/schema"}}); }, "external schema reference fetched/accepted");
        check(!version_supported("0.1.0") && version_supported("0.1.0-alpha") && version_supported("0.0.9"), "alpha launcher version ordering");
        check(version_supported("0.1.0-alpha.2", "0.1.0-alpha.10"), "numeric prerelease ordering");
        rejects([] { version_supported("0.1.0-alpha.01"); }, "noncanonical numeric prerelease accepted");
        check(sha256_text("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "text SHA-256 known vector");
        rejects([] { inspect_qemu(normalize_directory(L"./this-qemu-bin-does-not-exist")); }, "missing runtime accepted");
        std::map<std::string, uint64_t> payloads = {{"boot", 37130240}, {"system", 1109270528}, {"vendor", 101437440}};
        auto layout = plan_disk(200ULL << 30, payloads, 8ULL << 30);
        check(layout.totalBytes == 200ULL << 30 && layout.partitions.size() == 5, "total capacity and five GPT partitions");
        check(layout.partitions.front().name == "boot" && layout.partitions.back().name == "userdata", "GPT boot/data discovery order");
        check(layout.partitions.back().lengthBytes < layout.totalBytes, "data capacity excludes required system partitions");
        uint64_t previousEnd = 1ULL << 20;
        for (const auto& partition : layout.partitions) {
            check(partition.offsetBytes == previousEnd && partition.offsetBytes % (1ULL << 20) == 0 &&
                  partition.lengthBytes % (1ULL << 20) == 0, "aligned non-overlapping contiguous partitions");
            previousEnd += partition.lengthBytes;
        }
        check(previousEnd + 33 * 512 <= layout.totalBytes, "backup GPT outside userdata");
        auto second = plan_disk(32ULL << 30, payloads, 8ULL << 30);
        check(second.uuid != layout.uuid && second.partitions[0].uuid != layout.partitions[0].uuid, "fresh independent disk/partition identities");
        check(layout_json(second).at("totalBytes") == 32ULL << 30, "layout metadata capacity");
        rejects([&] { plan_disk(1ULL << 30, payloads, 8ULL << 30); }, "too small system capacity accepted");
        rejects([&] { plan_disk(4ULL << 30, payloads, 8ULL << 30); }, "too small userdata capacity accepted");
        rejects([&] { auto bad = payloads; bad.erase("vendor"); plan_disk(32ULL << 30, bad, 8ULL << 30); }, "missing vendor accepted");
        rejects([&] { auto bad = payloads; bad["system"] += 1; plan_disk(32ULL << 30, bad, 8ULL << 30); }, "unaligned image accepted");
        rejects([&] { plan_disk((32ULL << 30) + 512, payloads, 8ULL << 30); }, "unaligned total accepted");
        rejects([&] { plan_disk(1ULL << 63, payloads, 8ULL << 30); }, "signed-offset overflow accepted");
        check(crc32(reinterpret_cast<const unsigned char*>("123456789"), 9) == 0xcbf43926U, "UEFI CRC32 known vector");
        std::array<unsigned char, 4096> boot{};
        auto put = [&](size_t at, uint32_t value) {
            for (size_t i = 0; i < 4; ++i) boot[at + i] = static_cast<unsigned char>(value >> (8 * i));
        };
        std::copy_n(reinterpret_cast<const unsigned char*>("ANDROID!"), 8, boot.begin());
        put(8, 64); put(12, 32); put(20, 1584); put(40, 4);
        auto parsed = parse_boot_header(boot, 12288);
        check(parsed.ramdiskOffset == 8192 && parsed.kernelBytes == 64 && parsed.ramdiskBytes == 32, "boot v4 extraction offsets");
        rejects([&] { parse_boot_header(boot, 12289); }, "trailing boot data accepted");
        rejects([&] { parse_boot_header(boot, 8192); }, "truncated ramdisk accepted");
        boot[44] = 'a'; rejects([&] { parse_boot_header(boot, 12288); }, "hidden command line accepted"); boot[44] = 0;
        put(1580, 4096); rejects([&] { parse_boot_header(boot, 16384); }, "unsupported boot signature accepted"); put(1580, 0);
        put(40, 3); rejects([&] { parse_boot_header(boot, 12288); }, "wrong boot version accepted"); put(40, 4);
        put(8, 0); rejects([&] { parse_boot_header(boot, 12288); }, "empty kernel accepted"); put(8, 64);
        boot[24] = 1; rejects([&] { parse_boot_header(boot, 12288); }, "nonzero boot reserved fields accepted"); boot[24] = 0;
        boot[4095] = 1; rejects([&] { parse_boot_header(boot, 12288); }, "nonzero header padding accepted");
        // Destructive checks use ONLY brand-new isolated temporary fixtures,
        // never development/user VM disks or the public CLI/installer registry.
        const auto temp = temporary_fixture_root();
        auto appFixture = temp / "installed-app";
        fs::create_directory(appFixture);
        rejects([&] { InstallationLease missing(appFixture); }, "missing installation lease accepted");
        write_fixture(appFixture / "install.lock", "");
        {
            InstallationLease running(appFixture), concurrent(appFixture);
            HANDLE update = CreateFileW((appFixture / "install.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                                       0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            check(update == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION, "installer overwrites running app lease");
        }
        HANDLE update = CreateFileW((appFixture / "install.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                                   0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(update != INVALID_HANDLE_VALUE, "idle installation cannot acquire exclusive update lease");
        rejects([&] { InstallationLease blocked(appFixture); }, "app starts while installer holds exclusive lease");
        CloseHandle(update);
        fs::remove(appFixture / "install.lock"); fs::remove(appFixture);
        const auto storage = temp / L"实例 storage", neighbour = temp / L"unrelated.txt";
        fs::create_directory(storage); fs::create_directory(storage / "boot");
        write_fixture(storage / "phone.qcow2", "isolated fake disk, not a VM");
        write_fixture(storage / "boot" / "kernel", "isolated fake kernel");
        write_fixture(neighbour, "must survive");
        const std::string uuid = "ba6da5bf-99d3-4165-b44b-e3732e273713";
        auto owner = claim_storage(storage, uuid, "release");
        check(parse_storage_identity(storage_identity_json(owner)).directoryFileId == owner.directoryFileId,
              "ownership identity round trip");
        auto duplicateOwner = storage_identity_json(owner).dump();
        duplicateOwner.insert(1, "\"channel\":\"dev\",");
        write_fixture(storage / ".kikiemu-owner.json", duplicateOwner);
        rejects([&] { delete_storage(owner, {}, [] {}); }, "duplicate ownership key accepted");
        write_fixture(storage / ".kikiemu-owner.json", storage_identity_json(owner).dump());
        rejects([&] { claim_storage(storage, uuid, "release"); }, "existing ownership marker adopted");
        bool stopped = false;
        auto stop = [&] { stopped = true; };
        auto wrong = owner; wrong.instanceUuid = "2ec85e8d-7150-4a35-8dfe-49a1e6634fc4";
        rejects([&] { delete_storage(wrong, {}, stop); }, "foreign ownership accepted");
        check(!stopped && fs::exists(storage / "phone.qcow2"), "owner failure occurs before callback/files");
        wrong = owner; wrong.directoryFileId ^= 1;
        rejects([&] { delete_storage(wrong, {}, stop); }, "changed directory identity accepted");
        check(!stopped, "directory identity checked before process stop");
        rejects([&] { delete_storage(owner, {temp}, stop); }, "protected ancestor accepted");
        rejects([&] { delete_storage(owner, {storage / "boot"}, stop); }, "protected descendant accepted");
        wrong = owner; wrong.directory = temp.root_path();
        rejects([&] { delete_storage(wrong, {}, stop); }, "drive root deletion accepted");
        wrong = owner; wrong.directory = normalize_directory(L"~");
        rejects([&] { delete_storage(wrong, {}, stop); }, "home root deletion accepted");
        wrong = owner; wrong.directory = storage / L".." / storage.filename();
        rejects([&] { delete_storage(wrong, {}, stop); }, "non-normalized path accepted");
        rejects([&] { delete_storage(owner, {}, [] { throw std::runtime_error("owned-process stop failed"); }); }, "stop failure ignored");
        check(fs::exists(storage / "phone.qcow2") && fs::exists(storage / ".kikiemu-owner.json"), "stop failure retains entire storage");
        bool moved = false;
        rejects([&] { delete_storage(owner, {}, [&] {
            moved = MoveFileExW(storage.c_str(), (temp / "swapped").c_str(), 0) != FALSE;
            throw std::runtime_error("only test the root-rename guard");
        }); }, "root pin test did not throw");
        check(!moved && fs::exists(storage), "root cannot be renamed between preflight and deletion");
        const auto outside = temp / "outside", link = storage / "redirect";
        fs::create_directory(outside); write_fixture(outside / "keep.txt", "never traverse here");
        make_test_junction(link, outside);
        rejects([&] { delete_storage(owner, {}, stop); }, "nested junction followed");
        check(!stopped && fs::exists(outside / "keep.txt"), "junction rejected before stopping or deleting");
        check(RemoveDirectoryW(link.c_str()) != FALSE, "unlink only the isolated test junction");
        const auto alias = temp / "alias"; make_test_junction(alias, storage);
        wrong = owner; wrong.directory = alias;
        rejects([&] { delete_storage(wrong, {}, stop); }, "root junction accepted");
        check(RemoveDirectoryW(alias.c_str()) != FALSE, "unlink test root alias only");
        TestChild child(executable);
        auto owned = describe_owned_process(child.pid, fs::path(executable), uuid, "release", "qemu");
        check(owned_process_is_live(owned), "readonly status confirms exact owned live child");
        check(parse_owned_process(owned_process_json(owned)).creationTime == owned.creationTime, "process ownership round trip");
        auto selfSupervisor = describe_owned_process(GetCurrentProcessId(), fs::path(executable), uuid, "release", "supervisor");
        check(parse_owned_process(owned_process_json(selfSupervisor)).pid == GetCurrentProcessId(), "supervisor may read its own registered identity");
        rejects([&] { force_stop_owned(uuid, "release", {selfSupervisor}); }, "manager self-termination accepted");
        auto reused = owned; reused.creationTime += 1;
        rejects([&] { owned_process_is_live(reused); }, "readonly status adopted a reused PID");
        rejects([&] { force_stop_owned(uuid, "release", {reused}); }, "PID reuse ignored");
        auto foreign = owned; foreign.instanceUuid = "2ec85e8d-7150-4a35-8dfe-49a1e6634fc4";
        rejects([&] { force_stop_owned(uuid, "release", {foreign}); }, "foreign process ownership accepted");
        rejects([&] { force_stop_owned(uuid, "dev", {owned}); }, "release process stopped by dev channel");
        auto changed = owned; changed.executableSha256[0] = changed.executableSha256[0] == 'a' ? 'b' : 'a';
        rejects([&] { force_stop_owned(uuid, "release", {changed}); }, "changed executable bytes ignored");
        rejects([&] { force_stop_owned(uuid, "release", {owned, foreign}); }, "partially valid multi-process kill accepted");
        check(WaitForSingleObject(child.process, 0) == WAIT_TIMEOUT, "all negative process guards leave owned child alive");
        WSADATA winsock{}; check(WSAStartup(MAKEWORD(2, 2), &winsock) == 0, "internal endpoint-test setup");
        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(listener != INVALID_SOCKET && bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
              listen(listener, 1) == 0, "isolated ephemeral loopback listener");
        int addressBytes = sizeof(address);
        check(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressBytes) == 0, "endpoint fixture actual port");
        rejects([&] { verify_owned_endpoints({owned}, {{owned.pid, ntohs(address.sin_port)}}); }, "foreign process endpoint adopted");
        rejects([&] { verify_owned_endpoints({owned}, {{owned.pid, 0}}); }, "zero port accepted");
        check(WaitForSingleObject(child.process, 0) == WAIT_TIMEOUT, "endpoint rejection does not terminate anything");
        closesocket(listener); WSACleanup();
        delete_storage(owner, {}, [&] { force_stop_owned(uuid, "release", {owned}); stopped = true; });
        check(stopped && !fs::exists(storage) && WaitForSingleObject(child.process, 0) == WAIT_OBJECT_0,
              "exact owned child stopped before exact storage removal");
        check(!owned_process_is_live(owned), "readonly status detects the exact child's exit");
        check(fs::exists(neighbour) && fs::exists(outside / "keep.txt"), "unrelated neighbours remain");
        const auto registryRoot = temp / "registry", fakeBin = temp / "qemu-bin";
        const auto instanceA = temp / "A", instanceB = temp / "B";
        fs::create_directory(fakeBin); fs::create_directory(instanceA); fs::create_directory(instanceB);
        write_fixture(instanceA / "phone.qcow2", "test A"); write_fixture(instanceB / "phone.qcow2", "test B");
        auto ownerA = claim_storage(instanceA, uuid, "release");
        const std::string uuidB = "2ec85e8d-7150-4a35-8dfe-49a1e6634fc4";
        auto ownerB = claim_storage(instanceB, uuidB, "release");
        nlohmann::json binding = {{"binDirectory", utf8(fakeBin.wstring())}};
        {
            RegistryTransaction registry(registryRoot, "release");
            check(registry.register_installed(ownerA, nlohmann::json::object(), binding, {}) == "01", "first manager ID");
            check(registry.register_installed(ownerB, nlohmann::json::object(), binding, {}) == "02", "second manager ID");
            registry.set_default("01");
            rejects([&] { RegistryTransaction concurrent(registryRoot, "release"); }, "concurrent registry transaction permitted");
            rejects([&] { registry.delete_instance("00", {}); }, "unknown delete target accepted");
        }
        TestChild target(executable), unrelated(executable);
        auto targetProcess = describe_owned_process(target.pid, fs::path(executable), uuid, "release", "qemu");
        auto otherProcess = describe_owned_process(unrelated.pid, fs::path(executable), uuidB, "release", "qemu");
        {
            RegistryTransaction registry(registryRoot, "release");
            auto& a = registry.instance("01");
            a["runtime"] = {{"instanceUuid", uuid}, {"channel", "release"},
                            {"processes", nlohmann::json::array({owned_process_json(targetProcess)})}, {"endpoints", nlohmann::json::array()}};
            a["lifecycle"] = "running";
            auto& b = registry.instance("02");
            b["runtime"] = {{"instanceUuid", uuidB}, {"channel", "release"},
                            {"processes", nlohmann::json::array({owned_process_json(otherProcess)})}, {"endpoints", nlohmann::json::array()}};
            b["lifecycle"] = "running"; registry.save();
            check(registry.delete_instance("01", {}) && registry.state().at("defaultId").is_null(), "successful default deletion clears only its selection");
            check(!fs::exists(instanceA) && fs::exists(instanceB / "phone.qcow2") && registry.state().at("instances").contains("02"), "other registered storage remains intact");
            check(WaitForSingleObject(target.process, 0) == WAIT_OBJECT_0 && WaitForSingleObject(unrelated.process, 0) == WAIT_TIMEOUT,
                  "registry deletion kills only target child, leaves other running");
            check(fs::exists(fakeBin), "BYO QEMU directory is not deleted");
            // Fail after external journal: invalid creation time must retain
            // storage, registration and default, then survive reopen/retry.
            registry.set_default("02");
            auto brokenProcess = otherProcess; brokenProcess.creationTime += 1;
            b["runtime"]["processes"] = nlohmann::json::array({owned_process_json(brokenProcess)}); registry.save();
            rejects([&] { registry.delete_instance("02", {}); }, "delete ignores owned-runtime stop failure");
            check(registry.instance("02").at("lifecycle") == "delete-failed" && registry.state().at("defaultId") == "02" &&
                  fs::exists(instanceB / "phone.qcow2") && WaitForSingleObject(unrelated.process, 0) == WAIT_TIMEOUT, "failed delete journal/default/data/live child retained");
        }
        {
            RegistryTransaction registry(registryRoot, "release");
            check(!registry.instance("02").at("deleteJournal").is_null(), "failed delete recovers exact owner after reopen");
            registry.instance("02")["runtime"]["processes"] = nlohmann::json::array({owned_process_json(otherProcess)}); registry.save();
            HANDLE busy = CreateFileW((instanceB / "phone.qcow2").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            check(busy != INVALID_HANDLE_VALUE, "isolated locked-file deletion fixture");
            rejects([&] { registry.delete_instance("02", {}); }, "storage sharing failure ignored");
            check(registry.instance("02").at("lifecycle") == "delete-failed" && registry.state().at("defaultId") == "02" &&
                  fs::exists(instanceB / ".kikiemu-owner.json"), "partial filesystem failure retains owner, default and journal");
            CloseHandle(busy);
            check(registry.delete_instance("02", {}) && !fs::exists(instanceB), "retry completes exact failed deletion");
            check(registry.state().at("nextId") == 3, "deleted display IDs are not silently reused");
        }
        rejects([&] { RegistryTransaction wrongChannel(registryRoot, "dev"); }, "release registry opened through dev channel");
        const auto instanceC = temp / "C"; fs::create_directory(instanceC); write_fixture(instanceC / "disk", "test crash recovery");
        auto ownerC = claim_storage(instanceC, "b7773a04-ae8d-40f9-8c89-030377147c8a", "release");
        {
            RegistryTransaction registry(registryRoot, "release");
            check(registry.register_installed(ownerC, nlohmann::json::object(), binding, {}) == "03", "registration after delete has fresh monotonic ID");
            registry.set_default("03");
            auto& c = registry.instance("03");
            c["lifecycle"] = "deleting"; c["deleteJournal"] = {{"instanceUuid", ownerC.instanceUuid}, {"owner", c.at("owner")}}; registry.save();
        }
        delete_storage(ownerC, {}, [] {}); // Simulate crash after files, before unregister.
        fs::create_directory(instanceC); write_fixture(instanceC / "keep", "a DIFFERENT directory at the old path");
        auto replacementOwner = claim_storage(instanceC, ownerC.instanceUuid, "release");
        {
            RegistryTransaction registry(registryRoot, "release");
            rejects([&] { registry.delete_instance("03", {}); }, "delete recovery adopted a replacement directory");
            check(fs::exists(instanceC / "keep") && registry.state().at("defaultId") == "03", "replacement storage and default preserved");
        }
        delete_storage(replacementOwner, {}, [] {}); // Remove only this isolated replacement fixture.
        {
            RegistryTransaction registry(registryRoot, "release");
            check(registry.delete_instance("03", {}) && registry.state().at("instances").empty(), "post-removal interrupted journal finalizes without adopting another path");
        }
        // Corrupt duplicate keys must be refused, never guessed/reset.
        const auto savedRegistry = registryRoot / "registry.json";
        std::ifstream registryInput(savedRegistry, std::ios::binary);
        std::string goodRegistry((std::istreambuf_iterator<char>(registryInput)), {}); registryInput.close();
        write_fixture(savedRegistry, "{\"registryVersion\":1,\"channel\":\"release\",\"channel\":\"dev\",\"nextId\":1,\"defaultId\":null,\"instances\":{}}");
        rejects([&] { RegistryTransaction duplicateKeys(registryRoot, "release"); }, "duplicate registry keys accepted");
        write_fixture(savedRegistry, goodRegistry);
        // Nonbootable ZIP fixture exercises the container/import pipeline, not
        // a clean candidate, installer/CLI acceptance or Android launch.
        std::string fixtureBoot(12288, '\0');
        auto put32 = [&](size_t at, uint32_t value) { for (size_t i = 0; i < 4; ++i) fixtureBoot[at + i] = static_cast<char>(value >> (8 * i)); };
        fixtureBoot.replace(0, 8, "ANDROID!"); put32(8, 64); put32(12, 32); put32(20, 1584); put32(40, 4);
        fixtureBoot.replace(4096 + 56, 4, "ARM\x64"); put32(4096 + 24, 2);
        fixtureBoot[8192] = '\x1f'; fixtureBoot[8193] = '\x8b'; fixtureBoot[8194] = 8;
        std::string erofs(4096, '\0'); erofs.replace(1024, 4, "\xe2\xe1\xf5\xe0");
        std::string aospXml = "<manifest><remote name=\"aosp\" fetch=\"https://android.googlesource.com/\"/><project name=\"platform/test\" path=\"test\" revision=\"" + std::string(40, 'a') + "\"/></manifest>";
        nlohmann::json sourceLock = {
            {"sourceLockVersion", 1},
            {"contract", {{"repository", "https://github.com/kekeqwq/kikiaosp_test"}, {"revision", kiki_contract::producerRevision},
                          {"manifestSchemaSha256", kiki_contract::manifestSha256}, {"sourceLockSchemaSha256", kiki_contract::sourceLockSha256}}},
            {"aosp", {{"branch", "android17-release"}, {"manifestXml", aospXml}, {"manifestSha256", sha256_text(aospXml)}, {"projectCount", 1}}},
            {"device", {{"repository", "https://github.com/kekeqwq/kikiaosp_test"}, {"commit", std::string(40, 'b')}}},
            {"kernel", {{"repository", "https://github.com/kekeqwq/kikiaosp_kernel"}, {"commit", std::string(40, 'c')},
                        {"flakeLockSha256", std::string(64, 'd')}, {"sourceVersion", "7.3-rc4"}, {"imageSha256", sha256_text(fixtureBoot.substr(4096, 64))}}},
            {"build", {{"cleanSource", true}, {"independentOutput", true}, {"outputRecipe", "kikiaosp-release-v1"},
                       {"tools", {{"mkbootfsSha256", std::string(64, 'e')}, {"mkbootimgSha256", std::string(64, 'f')}, {"mkfsErofsSha256", std::string(64, '0')}}}}}
        };
        validate_source_lock(sourceLock); ++passed;
        auto badLock = sourceLock; badLock["aosp"]["manifestSha256"] = std::string(64, 'a');
        rejects([&] { validate_source_lock(badLock); }, "AOSP manifest digest mismatch accepted");
        badLock = sourceLock; badLock["aosp"]["projectCount"] = 2;
        rejects([&] { validate_source_lock(badLock); }, "AOSP project count mismatch accepted");
        badLock = sourceLock; badLock["aosp"]["manifestXml"] = aospXml.substr(0, aospXml.size() - 11) + "<project name=\"unpinned\" revision=\"main\"/></manifest>";
        badLock["aosp"]["manifestSha256"] = sha256_text(badLock["aosp"]["manifestXml"].get<std::string>());
        badLock["aosp"]["projectCount"] = 2;
        rejects([&] { validate_source_lock(badLock); }, "floating AOSP project accepted");
        auto actual = contractValid;
        std::vector<std::pair<std::string, std::string>> zipFiles = {{"payload/boot.img", fixtureBoot},
            {"payload/system.img", erofs}, {"payload/vendor.img", erofs}, {"provenance/source-lock.json", sourceLock.dump()},
            {"licenses/aosp.txt", "SYNTHETIC internal fixture, not AOSP redistributable"},
            {"licenses/kernel.txt", "SYNTHETIC internal fixture, not Linux redistributable"}};
        for (auto& entry : actual["payloads"]) {
            auto name = entry.at("path").get<std::string>();
            for (const auto& [path, data] : zipFiles) if (path == name) { entry["bytes"] = data.size(); entry["sha256"] = sha256_text(data); }
        }
        for (auto* entry : {&actual["sourceLock"], &actual["licenses"][0], &actual["licenses"][1]}) {
            for (const auto& [path, data] : zipFiles) if (path == entry->at("path").get<std::string>()) { (*entry)["bytes"] = data.size(); (*entry)["sha256"] = sha256_text(data); }
        }
        zipFiles.insert(zipFiles.begin(), {"manifest.json", actual.dump()});
        const auto zipPath = temp / "contract-fixture-NOT-BOOTABLE.zip", stage = temp / "package-stage";
        fixture_zip(zipPath, zipFiles);
        auto package = read_system_package(zipPath, stage);
        check(package.manifest == actual && package.payloads.size() == 3 && package.archiveSha256 == sha256(zipPath), "native ZIP reader validates and stages only declared synthetic files");
        check(sha256(package.payloads.at("boot")) == sha256_text(fixtureBoot), "ZIP binary readback retains NUL/CRLF/byte identity");
        rejects([&] { read_system_package(zipPath, stage); }, "existing staging destination overwritten");
        delete_storage(package.stagingOwner, {zipPath}, [] {});
        auto duplicateFiles = zipFiles; duplicateFiles.push_back(zipFiles[0]); fixture_zip(zipPath, duplicateFiles);
        rejects([&] { read_system_package(zipPath, stage); }, "duplicate raw archive name accepted");
        check(!fs::exists(stage), "duplicate archive rejected before staging");
        auto forbiddenFiles = zipFiles; forbiddenFiles.push_back({"userdata.img", "do not ship used disks"}); fixture_zip(zipPath, forbiddenFiles);
        rejects([&] { read_system_package(zipPath, stage); }, "userdata archive entry accepted");
        auto wrongHash = zipFiles; auto corruptManifest = actual; corruptManifest["payloads"][0]["sha256"] = std::string(64, '0');
        wrongHash[0].second = corruptManifest.dump(); fixture_zip(zipPath, wrongHash);
        rejects([&] { read_system_package(zipPath, stage); }, "corrupt payload hash accepted");
        check(!fs::exists(stage), "failed extraction removes only its freshly created owned staging");
        fixture_zip(zipPath, zipFiles);
        std::ifstream zipInput(zipPath, std::ios::binary);
        std::string validZip((std::istreambuf_iterator<char>(zipInput)), {}); zipInput.close();
        for (auto altered : {std::string("MZ-hidden-executable") + validZip, validZip + "trailer", validZip.substr(0, validZip.size() - 1)}) {
            write_fixture(zipPath, altered);
            rejects([&] { read_system_package(zipPath, stage); }, "noncanonical raw ZIP framing accepted");
        }
        auto mismatchZip = validZip; mismatchZip[30] = 'x'; write_fixture(zipPath, mismatchZip);
        rejects([&] { read_system_package(zipPath, stage); }, "local/central archive pathname disagreement accepted");
        auto nulZip = validZip; auto centralAt = nulZip.find("PK\x01\x02");
        check(centralAt != std::string::npos, "fixture central directory present");
        nulZip[centralAt + 46 + 4] = '\0'; write_fixture(zipPath, nulZip);
        rejects([&] { read_system_package(zipPath, stage); }, "raw embedded-NUL archive pathname accepted");
        check(!fs::exists(stage), "noncanonical ZIP mutations rejected before any staging");
        if (argc == 3) {
            auto cross = read_system_package(normalize_directory(argv[2]), stage);
            check(cross.payloads.size() == 3 && cross.manifest.at("channel") == "release", "producer deflate/local-ZIP64 synthetic fixture accepted by native reader");
            delete_storage(cross.stagingOwner, {normalize_directory(argv[2])}, [] {});
        }
        fs::remove(zipPath);
        auto managerRoot = temp / "manager-api", managerDisk = temp / "manager-api-disk";
        ManagerPaths manager{managerRoot, temp / "app-fixture", "release"};
        const auto createdOwner = create_storage(managerDisk, new_instance_uuid(), "release", {fakeBin, managerRoot, manager.appRoot});
        {
            StorageLease lease(createdOwner);
            check(!MoveFileExW(managerDisk.c_str(), (temp / "renamed-manager-api-disk").c_str(), 0), "storage lease pins directory during install/start");
            rejects([&] { delete_storage(createdOwner, {}, [] {}); }, "storage removed while an installation lease is active");
        }
        rejects([&] { create_storage(managerDisk, new_instance_uuid(), "release", {}); }, "create adopted preexisting storage");
        verify_storage_owner(createdOwner); ++passed;
        {
            RegistryTransaction registry(managerRoot, "release");
            registry.register_installed(createdOwner, {{"layout", {{"totalBytes", 200ULL << 30}}}},
                                        {{"binDirectory", utf8(fakeBin.wstring())}}, Resources{6, 8ULL << 30, "custom"});
        }
        auto listing = execute_management(parse_command({L"list"}), manager);
        check(listing.find("200g  6  VirGL  8g  idle") != std::string::npos, "manager API lists immutable total and saved resources");
        check(execute_management(parse_command({L"set", L"--default", L"01"}), manager) == "Set default system to 01\n", "manager API sets default");
        check(execute_management(parse_command({L"--set", L"--id", L"01", L"--mem", L"4g"}), manager).find("next start") != std::string::npos,
              "manager API reports deferred next-start settings");
        rejects([&] { execute_management(parse_command({L"set", L"--id", L"01", L"--qemu", fakeBin.wstring(), L"--cpus", L"10"}), manager); },
                "invalid QEMU binding committed a partial settings change");
        {
            RegistryTransaction registry(managerRoot, "release");
            const auto& config = registry.instance("01").at("configuration").at("resources");
            check(config.at("cpus") == 6 && config.at("memoryBytes") == 4ULL << 30, "manager API preserves CPU and commits no invalid partial QEMU/resource changes");
        }
        rejects([&] { execute_management({"delete", {{"id", L"01"}}}, manager); }, "manager API bypassed explicit force requirement");
        check(fs::exists(managerDisk), "missing force leaves instance storage untouched");
        check(execute_management(parse_command({L"--delete", L"--force", L"--id", L"01"}), manager) ==
              "Deleted instance 01 and its storage.\nDefault system cleared.\n", "exact requested delete spelling reaches guarded registered-instance deletion");
        {
            RegistryTransaction registry(managerRoot, "release");
            check(registry.state().at("instances").empty() && registry.state().at("defaultId").is_null() && !fs::exists(managerDisk),
                  "manager API deletion persists unregister/default cleanup and removes only its fixture storage");
        }
        fs::remove(managerRoot / "registry.json"); fs::remove(managerRoot / "registry.lock"); fs::remove(managerRoot);
        wchar_t self[32768]; auto selfLength = GetModuleFileNameW(nullptr, self, 32768);
        check(selfLength && selfLength < 32768, "private fixture executable located");
        auto selfExe = fs::path(self);
        HANDLE childHandle = nullptr;
        auto suspendedLog = temp / "suspended-child.log";
        {
            PrivateChild child(selfExe, temp, {L"--owned-library-test-child"}, {}, suspendedLog);
            auto identity = describe_owned_process(child.pid(), selfExe, new_instance_uuid(), "release", "supervisor");
            check(owned_process_is_live(identity) && child.running(), "new private child is owned while suspended");
            childHandle = OpenProcess(SYNCHRONIZE, FALSE, child.pid());
            check(childHandle != nullptr, "owned suspended fixture handle pinned");
        }
        check(WaitForSingleObject(childHandle, 0) == WAIT_OBJECT_0, "unregistered suspended child cannot be stranded"); CloseHandle(childHandle);
        fs::remove(suspendedLog);
        auto ownedLog = temp / "owned-job-child.log";
        {
            PrivateChild child(selfExe, temp, {L"--owned-library-test-child"}, {}, ownedLog);
            childHandle = OpenProcess(SYNCHRONIZE, FALSE, child.pid()); child.resume();
            check(child.running(), "private owned job resumes without a console");
            rejects([&] { child.resume(); }, "private child resumed twice");
        }
        check(WaitForSingleObject(childHandle, 0) == WAIT_OBJECT_0, "owned job stopped and reaped before storage release"); CloseHandle(childHandle);
        fs::remove(ownedLog);
        auto liveStorage = temp / "live-lease-disk", liveRegistry = temp / "live-lease-registry";
        auto liveOwner = create_storage(liveStorage, new_instance_uuid(), "release", {fakeBin, liveRegistry});
        auto leaseSource = temp / "live-owner-fixture.json", leaseReady = temp / "live-lease-ready.txt", leaseLog = temp / "live-lease.log";
        write_fixture(leaseSource, storage_identity_json(liveOwner).dump());
        write_fixture(liveStorage / "keep-until-stop.txt", "owned fixture only");
        {
            PrivateChild child(selfExe, temp, {L"--leased-library-test-child", leaseSource.wstring(), leaseReady.wstring()}, {}, leaseLog);
            auto process = describe_owned_process(child.pid(), selfExe, liveOwner.instanceUuid, "release", "supervisor");
            child.resume(); const auto until = GetTickCount64() + 5000;
            while (!fs::exists(leaseReady) && child.running() && GetTickCount64() < until) Sleep(10);
            check(fs::exists(leaseReady), "registered running supervisor holds a real storage lease");
            RegistryTransaction registry(liveRegistry, "release");
            auto id = registry.register_installed(liveOwner, {{"layout", {{"totalBytes", 32ULL << 30}}}},
                {{"binDirectory", utf8(fakeBin.wstring())}}, Resources{});
            registry.set_default(id);
            registry.instance(id)["runtime"] = {{"instanceUuid", liveOwner.instanceUuid}, {"channel", "release"},
                {"processes", nlohmann::json::array({owned_process_json(process)})}, {"endpoints", nlohmann::json::array()}};
            registry.instance(id)["lifecycle"] = "running"; registry.save();
            check(registry.delete_instance(id, {}) && child.wait(5000) && !fs::exists(liveStorage),
                  "force delete stops leased running supervisor BEFORE upgrading access and removing storage");
            check(registry.state().at("instances").empty() && registry.state().at("defaultId").is_null(),
                  "live leased deletion unregisters and clears default atomically");
        }
        fs::remove(leaseSource); fs::remove(leaseReady); fs::remove(leaseLog);
        fs::remove(liveRegistry / "registry.json"); fs::remove(liveRegistry / "registry.lock"); fs::remove(liveRegistry);
        nlohmann::json launchFixture = {{"owner", storage_identity_json(createdOwner)},
            {"immutableSource", {{"layout", {{"partitions", nlohmann::json::array({{{"name", "boot"}, {"uuid", new_instance_uuid()}}})}}}}},
            {"configuration", {{"qemu", {{"binDirectory", utf8(fakeBin.wstring())}}},
                {"resources", {{"cpus", uint32_t(8)}, {"memoryBytes", 4ULL << 30}}}}}};
        auto plan = runtime_plan(launchFixture, manager, new_instance_uuid(), 60001, 60002, 60003);
        const std::string homeWindow = "  mCurrentFocus=Window{abc123 u0 com.android.launcher3/com.android.launcher3.uioverrides.QuickstepLauncher}\n";
        const std::string homeLayers = "SurfaceView\ncom.android.launcher3/com.android.launcher3.uioverrides.QuickstepLauncher#23\n";
        check(launcher_display_ready(homeWindow, homeLayers, "  mActiveRenderFrameRate=120.0\n"),
              "ready gate accepts exact WindowManager Home focus plus actual Home layer and render 120");
        check(launcher_display_ready("mCurrentFocus=Window{abc u0 com.android.launcher3/.uioverrides.QuickstepLauncher}\r\n",
                                     homeLayers, "mActiveRenderFrameRate=120\r\n"), "ready gate supports canonical abbreviated component and CRLF");
        check(!launcher_display_ready("ResumedActivity: ActivityRecord{abc u0 com.android.launcher3/.uioverrides.QuickstepLauncher}\n",
                                      homeLayers, "mActiveRenderFrameRate=120\n"), "activity-only evidence is not WindowManager focus");
        check(!launcher_display_ready("mCurrentFocus=null\n", homeLayers, "mActiveRenderFrameRate=120\n"), "null focus cannot expose desktop");
        check(!launcher_display_ready("mCurrentFocus=Window{abc u0 com.android.settings/.Settings}\n", homeLayers,
                                      "mActiveRenderFrameRate=120\n"), "stale Home layer cannot override a different focused application");
        check(!launcher_display_ready(homeWindow, "SurfaceFlinger layer list unavailable", "mActiveRenderFrameRate=120\n"),
              "focused Home without actual compositor layer is not ready");
        for (const auto& rate : {"60", "1200", "120.01", "120.0Hz"})
            check(!launcher_display_ready(homeWindow, homeLayers, "mActiveRenderFrameRate=" + std::string(rate) + "\n"),
                  "ready gate refuses non-120 or malformed active render rates");
        check(plan.serial == "kiki-release-" + createdOwner.instanceUuid && plan.environment.at(L"KIKI_SDL_WINDOW_TITLE") == L"KikiEmu",
              "release runtime serial and title are isolated from development");
        check(plan.arguments.at(7) == L"host" && plan.environment.at(L"KIKI_SDL_DISABLE_GRAB") == L"1" &&
              plan.environment.at(L"KIKI_SDL_START_WIDTH") == L"1003" && plan.environment.at(L"KIKI_SDL_START_HEIGHT") == L"1556",
              "runtime plan keeps validated paired WHPX/native SDL/input/window baseline");
        check(std::find(plan.arguments.begin(), plan.arguments.end(), L"sdl,gl=on") != plan.arguments.end() &&
              std::find(plan.arguments.begin(), plan.arguments.end(), L"-snapshot") == plan.arguments.end(),
              "runtime plan uses native accelerated persistent storage, not snapshots");
        rejects([&] { runtime_plan(launchFixture, manager, new_instance_uuid(), 5555, 60002, 60003); }, "runtime plan accepted development ADB port");
        rejects([&] { runtime_plan(launchFixture, manager, new_instance_uuid(), 60001, 60001, 60003); }, "runtime plan accepted colliding endpoints");
        auto privateWire = [&](const std::wstring& mode, const std::function<void(const OwnedProcess&, uint16_t)>& operation) {
            PortReservation reserved; auto port = reserved.port();
            check(port != 5555 && port != 5037 && port != 4447 && port != 4455, "wire fixture stays outside global/development ports");
            auto log = temp / (mode + L"-wire.log"), ready = temp / (mode + L"-ready.txt");
            {
                PrivateChild child(selfExe, temp, {L"--wire-library-test-child", std::to_wstring(port), mode, ready.wstring()}, {}, log);
                auto identity = describe_owned_process(child.pid(), selfExe, new_instance_uuid(), "release", "qemu");
                reserved.release(); child.resume(); const auto until = GetTickCount64() + 5000;
                while (!fs::exists(ready) && child.running() && GetTickCount64() < until) Sleep(10);
                check(fs::exists(ready), "exact owned fake adbd ready");
                operation(identity, port);
                if (mode == L"success") check(child.wait(5000) && child.exit_code() == 0, "fake adbd validates complete wire handshake and acknowledgement");
            }
            fs::remove(log); fs::remove(ready);
        };
        privateWire(L"success", [&](const OwnedProcess& process, uint16_t port) {
            auto result = guest_shell(process, port, "echo fixture");
            check(result.output == "fixture\n" && result.error == "warning\n" && result.exitCode == 7,
                  "private ADB handles fragmented/coalesced shell-v2 stdout stderr and exit status");
        });
        for (const auto& mode : {L"corrupt", L"foreign-stream", L"no-exit", L"timeout"})
            privateWire(mode, [&](const OwnedProcess& process, uint16_t port) {
                rejects([&] { guest_shell(process, port, "echo fixture", 200); }, "invalid/foreign/incomplete/stalled ADB stream accepted");
            });
        privateWire(L"foreign-owner", [&](const OwnedProcess& process, uint16_t port) {
            auto invalid = process; ++invalid.creationTime;
            rejects([&] { guest_shell(invalid, port, "echo fixture"); }, "private ADB accepted PID creation mismatch");
            invalid = process; invalid.role = "supervisor";
            rejects([&] { guest_shell(invalid, port, "echo fixture"); }, "private ADB accepted non-QEMU role");
            rejects([&] { guest_shell(process, port, ""); }, "private ADB accepted empty command");
        });
        // Exact, nonrecursive cleanup of the remaining fixtures we just created.
        fs::remove(neighbour); fs::remove(outside / "keep.txt"); fs::remove(outside);
        fs::remove(registryRoot / "registry.json"); fs::remove(registryRoot / "registry.lock");
        fs::remove(registryRoot); fs::remove(fakeBin); fs::remove(temp);
        std::cout << "PASS: " << passed << " internal core checks. Only isolated temporary fixtures/owned test children changed; no installation, user configuration or VM changes.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
