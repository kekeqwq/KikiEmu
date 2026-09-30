// SPDX-License-Identifier: GPL-2.0-or-later
// Internal library tests. No released CLI, installer, registry or VM is used.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include "../src/kikiemu/runtime.hpp"
#include "../src/kikiemu/disk.hpp"
#include "../src/kikiemu/boot.hpp"
#include <algorithm>
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
int wmain() {
    try {
        using namespace kiki;
        check(parse_command({}).name == "help", "empty help");
        check(parse_command({L"--list"}).name == "list", "list alias");
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
        std::cout << "PASS: " << passed << " internal core checks. No installation, configuration or VM changes.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
