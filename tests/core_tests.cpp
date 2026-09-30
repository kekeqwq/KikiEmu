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
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

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
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--owned-library-test-child") {
        Sleep(INFINITE); return 0;
    }
    try {
        using namespace kiki;
        check(parse_command({}).name == "help", "empty help");
        check(parse_command({L"--list"}).name == "list", "list alias");
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
        check(parse_owned_process(owned_process_json(owned)).creationTime == owned.creationTime, "process ownership round trip");
        auto selfSupervisor = describe_owned_process(GetCurrentProcessId(), fs::path(executable), uuid, "release", "supervisor");
        check(parse_owned_process(owned_process_json(selfSupervisor)).pid == GetCurrentProcessId(), "supervisor may read its own registered identity");
        rejects([&] { force_stop_owned(uuid, "release", {selfSupervisor}); }, "manager self-termination accepted");
        auto reused = owned; reused.creationTime += 1;
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
