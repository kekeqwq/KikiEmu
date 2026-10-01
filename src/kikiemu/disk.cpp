// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include "disk.hpp"
#include "process.hpp"
#include "boot.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
static constexpr uint64_t MiB = 1ULL << 20;
static uint64_t align_up(uint64_t bytes) {
    if (bytes > std::numeric_limits<int64_t>::max() - MiB)
        throw std::runtime_error("Partition size overflow.");
    return (bytes + MiB - 1) / MiB * MiB;
}
static std::string new_uuid() {
    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Could not create the disk UUID.");
    wchar_t text[39];
    if (!StringFromGUID2(guid, text, 39)) throw std::runtime_error("Could not encode the disk UUID.");
    auto result = utf8(std::wstring(text + 1, 36));
    for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
DiskLayout plan_disk(uint64_t totalBytes, const std::map<std::string, uint64_t>& payloadBytes,
                     uint64_t minimumDataBytes) {
    if (payloadBytes.size() != 3 || !payloadBytes.contains("boot") || !payloadBytes.contains("system") || !payloadBytes.contains("vendor"))
        throw std::runtime_error("GPT-v1 requires exactly boot, system and vendor payloads.");
    if (totalBytes > uint64_t(std::numeric_limits<int64_t>::max()) || totalBytes % MiB || totalBytes < 4 * MiB)
        throw std::runtime_error("Total disk size must be MiB-aligned and within the supported range.");
    DiskLayout layout{new_uuid(), totalBytes, {}};
    uint64_t next = MiB;
    for (const auto& name : {"boot", "system", "vendor", "misc"}) {
        uint64_t bytes = std::string(name) == "misc" ? 4 * MiB : payloadBytes.at(name);
        if (!bytes || bytes % 4096) throw std::runtime_error("Boot and EROFS payloads must be nonempty and 4-KiB-aligned.");
        uint64_t length = align_up(bytes);
        if (length > totalBytes || next > totalBytes - length) throw std::runtime_error("Total capacity is too small for the system partitions.");
        layout.partitions.push_back({name, new_uuid(), next, length});
        next += length;
    }
    uint64_t end = (totalBytes - 33 * 512) / MiB * MiB;
    if (next >= end || end - next < minimumDataBytes) throw std::runtime_error("Total capacity is too small for the required userdata filesystem.");
    layout.partitions.push_back({"userdata", new_uuid(), next, end - next});
    return layout;
}
json layout_json(const DiskLayout& layout) {
    json result = {{"layoutVersion", "gpt-v1"}, {"sectorSizeBytes", 512}, {"alignmentBytes", MiB},
                   {"diskUuid", layout.uuid}, {"totalBytes", layout.totalBytes}, {"partitions", json::array()}};
    for (const auto& partition : layout.partitions) result["partitions"].push_back({
        {"name", partition.name}, {"uuid", partition.uuid}, {"offsetBytes", partition.offsetBytes}, {"lengthBytes", partition.lengthBytes}});
    return result;
}
uint32_t crc32(const unsigned char* data, size_t size) {
    uint32_t value = 0xffffffff;
    for (size_t i = 0; i < size; ++i) {
        value ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
    }
    return ~value;
}
static void integer(std::vector<unsigned char>& bytes, size_t at, uint64_t value, size_t width) {
    if (at + width > bytes.size()) throw std::runtime_error("GPT serialization overflow.");
    for (size_t i = 0; i < width; ++i) bytes[at + i] = static_cast<unsigned char>(value >> (8 * i));
}
static void guid_bytes(std::vector<unsigned char>& bytes, size_t at, const std::string& text) {
    GUID guid;
    auto wide = L"{" + utf16(text) + L"}";
    if (FAILED(CLSIDFromString(wide.c_str(), &guid)) || at + sizeof(guid) > bytes.size())
        throw std::runtime_error("Invalid GPT UUID.");
    std::memcpy(bytes.data() + at, &guid, sizeof(guid));
}
static std::vector<unsigned char> entries(const DiskLayout& layout) {
    std::vector<unsigned char> bytes(128 * 128, 0);
    for (size_t i = 0; i < layout.partitions.size(); ++i) {
        const auto& partition = layout.partitions[i];
        size_t at = i * 128;
        guid_bytes(bytes, at, "0fc63daf-8483-4772-8e79-3d69d8477de4");
        guid_bytes(bytes, at + 16, partition.uuid);
        integer(bytes, at + 32, partition.offsetBytes / 512, 8);
        integer(bytes, at + 40, (partition.offsetBytes + partition.lengthBytes) / 512 - 1, 8);
        // GPT entry names are UTF-16LE, not localized filesystem labels.
        for (size_t n = 0; n < partition.name.size(); ++n) integer(bytes, at + 56 + 2 * n, partition.name[n], 2);
    }
    return bytes;
}
static std::vector<unsigned char> header(const DiskLayout& layout, bool backup, uint32_t entryCrc) {
    uint64_t last = layout.totalBytes / 512 - 1;
    std::vector<unsigned char> bytes(512, 0);
    std::memcpy(bytes.data(), "EFI PART", 8);
    integer(bytes, 8, 0x00010000, 4); integer(bytes, 12, 92, 4);
    integer(bytes, 24, backup ? last : 1, 8); integer(bytes, 32, backup ? 1 : last, 8);
    integer(bytes, 40, 34, 8); integer(bytes, 48, last - 33, 8);
    guid_bytes(bytes, 56, layout.uuid);
    integer(bytes, 72, backup ? last - 32 : 2, 8);
    integer(bytes, 80, 128, 4); integer(bytes, 84, 128, 4); integer(bytes, 88, entryCrc, 4);
    integer(bytes, 16, crc32(bytes.data(), 92), 4);
    return bytes;
}
static void write_bytes(const fs::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size())) throw std::runtime_error("Could not write the installation chunk.");
}
static std::vector<unsigned char> primary_table(const DiskLayout& layout, const std::vector<unsigned char>& table) {
    auto primaryHeader = header(layout, false, crc32(table.data(), table.size()));
    std::vector<unsigned char> primary(65536, 0);
    primary[447] = 0; primary[448] = 2; primary[449] = 0; primary[450] = 0xee;
    primary[451] = 0xff; primary[452] = 0xff; primary[453] = 0xff;
    integer(primary, 454, 1, 4); integer(primary, 458, std::min<uint64_t>(layout.totalBytes / 512 - 1, 0xffffffff), 4);
    primary[510] = 0x55; primary[511] = 0xaa;
    std::copy(primaryHeader.begin(), primaryHeader.end(), primary.begin() + 512);
    std::copy(table.begin(), table.end(), primary.begin() + 1024);
    return primary;
}
static void check_image_header(const std::string& role, const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    std::array<unsigned char, 4096> bytes{};
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("Truncated image payload.");
    if (role == "boot") {
        if (std::memcmp(bytes.data(), "ANDROID!", 8) || bytes[40] != 4 || bytes[41] || bytes[42] || bytes[43])
            throw std::runtime_error("Boot payload must use Android boot header version 4.");
    } else {
        if (bytes[1024] != 0xe2 || bytes[1025] != 0xe1 || bytes[1026] != 0xf5 || bytes[1027] != 0xe0)
            throw std::runtime_error("System/vendor payload must be raw EROFS, not a sparse or whole-disk image.");
    }
}
json install_disk(const json& binding, const fs::path& newDirectory, uint64_t totalBytes,
                  const std::map<std::string, fs::path>& payloads, uint64_t minimumDataBytes,
                  const std::map<std::string, std::string>& manifestHashes, const Progress& progress) {
    report(progress, "Validating QEMU image tools and installation payloads.");
    verify_qemu_binding(binding);
    auto directory = normalize_directory(newDirectory.wstring());
    if (fs::exists(directory)) throw std::runtime_error("Installation destination already exists; no files were changed.");
    std::map<std::string, uint64_t> lengths;
    std::map<std::string, std::string> validatedHashes;
    if (!manifestHashes.empty() && manifestHashes.size() != payloads.size())
        throw std::runtime_error("Manifest payload identity set differs from the installation inputs.");
    for (const auto& [role, file] : payloads) {
        check_image_header(role, file);
        lengths[role] = fs::file_size(file);
        validatedHashes[role] = sha256(file);
        if (!manifestHashes.empty() && (!manifestHashes.contains(role) || validatedHashes.at(role) != manifestHashes.at(role)))
            throw std::runtime_error("Installation input differs from the verified manifest; no disk was created.");
    }
    auto layout = plan_disk(totalBytes, lengths, minimumDataBytes);
    auto bin = fs::path(utf16(binding.at("binDirectory").get<std::string>()));
    fs::create_directories(directory);
    report(progress, "Creating a " + std::to_string(totalBytes >> 30) + " GiB dynamic disk (no preallocation).");
    auto tool = [&](const wchar_t* name, const std::vector<std::wstring>& args) {
        auto result = image_tool(bin / name, directory, args);
        if (result.exitCode) throw std::runtime_error("Image tool failed: " + result.output);
        return result.output;
    };
    tool(L"qemu-img.exe", {L"create", L"-f", L"qcow2", L"-o", L"compat=1.1,cluster_size=65536,preallocation=off",
                           L"phone.qcow2", std::to_wstring(totalBytes)});
    auto info = json::parse(tool(L"qemu-img.exe", {L"info", L"--output=json", L"phone.qcow2"}));
    if (info.at("format") != "qcow2" || info.at("virtual-size") != totalBytes || info.contains("backing-filename"))
        throw std::runtime_error("Created disk capacity/format/backing identity is incorrect.");
    auto write_and_verify = [&](uint64_t offset, const std::vector<unsigned char>& bytes) {
        write_bytes(directory / "install-chunk.bin", bytes);
        std::wstring command = L"write -q -s install-chunk.bin " + std::to_wstring(offset) + L" " + std::to_wstring(bytes.size());
        tool(L"qemu-io.exe", {L"-f", L"qcow2", L"-c", command, L"phone.qcow2"});
        uint64_t block = offset % 4096 || bytes.size() % 4096 ? 512 : 4096;
        // This pinned QEMU's img_dd clips its input end to count*bs BEFORE
        // subtracting skip. Use an absolute end count, not POSIX dd semantics.
        tool(L"qemu-img.exe", {L"dd", L"-f", L"qcow2", L"-O", L"raw", L"if=phone.qcow2", L"of=verify-chunk.bin",
                               L"bs=" + std::to_wstring(block), L"skip=" + std::to_wstring(offset / block),
                               L"count=" + std::to_wstring((offset + bytes.size()) / block)});
        if (fs::file_size(directory / "verify-chunk.bin") != bytes.size() ||
            sha256(directory / "verify-chunk.bin") != sha256(directory / "install-chunk.bin"))
            throw std::runtime_error("Disk payload readback mismatch at byte " + std::to_string(offset) +
                                     ". Installation was not registered; check the required image-tool patches.");
    };
    auto table = entries(layout);
    report(progress, "Writing and verifying primary/backup GPT partition tables.");
    write_and_verify(0, primary_table(layout, table));
    auto backupHeader = header(layout, true, crc32(table.data(), table.size()));
    auto backup = table; backup.insert(backup.end(), backupHeader.begin(), backupHeader.end());
    write_and_verify(totalBytes - backup.size(), backup);
    auto record = layout_json(layout);
    record["payloads"] = json::array();
    for (const auto& partition : layout.partitions) {
        auto found = payloads.find(partition.name);
        if (found == payloads.end()) continue; // Fresh misc/data remain all zero.
        auto expected = validatedHashes.at(partition.name);
        if (sha256(found->second) != expected || fs::file_size(found->second) != lengths.at(partition.name))
            throw std::runtime_error("Installation input changed before import; instance was not registered.");
        std::ifstream stream(found->second, std::ios::binary);
        uint64_t written = 0;
        report(progress, "Importing " + partition.name + " with disk readback verification.");
        while (written < lengths.at(partition.name)) {
            size_t count = static_cast<size_t>(std::min<uint64_t>(16 * MiB, lengths.at(partition.name) - written));
            std::vector<unsigned char> bytes(count);
            if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("Could not read the installation payload.");
            write_and_verify(partition.offsetBytes + written, bytes);
            written += bytes.size();
            report(progress, "  " + partition.name + ": " + std::to_string(written * 100 / lengths.at(partition.name)) + "% (" +
                std::to_string(written) + "/" + std::to_string(lengths.at(partition.name)) + " bytes)");
        }
        if (sha256(found->second) != expected) throw std::runtime_error("Installation input changed during import.");
        record["payloads"].push_back({{"role", partition.name}, {"bytes", written}, {"sha256", expected}});
    }
    report(progress, "Checking QCOW2 consistency and extracting the installed boot payload.");
    tool(L"qemu-img.exe", {L"check", L"-f", L"qcow2", L"phone.qcow2"});
    const auto& bootPartition = layout.partitions.front();
    tool(L"qemu-img.exe", {L"dd", L"-f", L"qcow2", L"-O", L"raw", L"if=phone.qcow2", L"of=installed-boot.bin",
                           L"bs=4096", L"skip=" + std::to_wstring(bootPartition.offsetBytes / 4096),
                           L"count=" + std::to_wstring((bootPartition.offsetBytes + lengths.at("boot")) / 4096)});
    if (sha256(directory / "installed-boot.bin") != sha256(payloads.at("boot")))
        throw std::runtime_error("Installed boot partition identity changed before cache generation.");
    report(progress, "Deriving the kernel/initramfs boot cache from the installed disk.");
    record["directBoot"] = derive_boot_cache(directory / "installed-boot.bin", directory / "boot");
    fs::remove(directory / "installed-boot.bin");
    record["diskFile"] = "phone.qcow2";
    record["status"] = "installed-not-booted; GPT/fresh-data acceptance still required";
    record["actualImageBytes"] = fs::file_size(directory / "phone.qcow2");
    fs::remove(directory / "install-chunk.bin"); fs::remove(directory / "verify-chunk.bin");
    std::ofstream output(directory / "disk-layout.json", std::ios::binary);
    output << record.dump(2) << '\n';
    if (!output) throw std::runtime_error("Could not write the immutable disk layout record.");
    return record;
}
void verify_installed_disk(const json& binding, const fs::path& directory, const json& installed, const fs::path& scratch) {
    verify_qemu_binding(binding);
    if (installed.at("layoutVersion") != "gpt-v1" || installed.at("sectorSizeBytes") != 512 ||
        installed.at("alignmentBytes") != MiB || installed.at("diskFile") != "phone.qcow2" ||
        installed.at("partitions").size() != 5 || !installed.at("totalBytes").is_number_unsigned())
        throw std::runtime_error("Unsupported installed disk record.");
    DiskLayout layout{installed.at("diskUuid").get<std::string>(), installed.at("totalBytes").get<uint64_t>(), {}};
    if (!valid_instance_uuid(layout.uuid) || layout.totalBytes > INT64_MAX || layout.totalBytes < (8ULL << 30) ||
        layout.totalBytes % (1ULL << 30)) throw std::runtime_error("Invalid immutable disk capacity/UUID.");
    const std::array<const char*, 5> roles{"boot", "system", "vendor", "misc", "userdata"};
    uint64_t next = MiB;
    for (size_t i = 0; i < roles.size(); ++i) {
        const auto& item = installed.at("partitions")[i];
        auto length = item.at("lengthBytes").get<uint64_t>();
        auto uuid = item.at("uuid").get<std::string>();
        if (item.size() != 4 || item.at("name") != roles[i] || !item.at("offsetBytes").is_number_unsigned() ||
            !item.at("lengthBytes").is_number_unsigned() || item.at("offsetBytes") != next ||
            !valid_instance_uuid(uuid) || !length || length % MiB || next > layout.totalBytes || length > layout.totalBytes - next)
            throw std::runtime_error("Invalid immutable GPT partition layout.");
        layout.partitions.push_back({roles[i], uuid, next, length}); next += length;
    }
    if (layout.partitions[3].lengthBytes != 4 * MiB || layout.partitions[4].lengthBytes < (8ULL << 30) ||
        next != (layout.totalBytes - 33 * 512) / MiB * MiB)
        throw std::runtime_error("Installed GPT partitions do not consume the recorded total capacity.");
    const auto& boot = installed.at("directBoot");
    if (boot.at("kernel") != "boot/kernel" || boot.at("ramdisk") != "boot/ramdisk.img")
        throw std::runtime_error("Foreign direct-boot cache path.");
    for (const auto& role : {"kernel", "ramdisk"}) {
        auto path = directory / utf16(boot.at(role).get<std::string>());
        if (!fs::is_regular_file(path) || fs::is_symlink(path) || fs::file_size(path) != boot.at(std::string(role) + "Bytes") ||
            sha256(path) != boot.at(std::string(role) + "Sha256").get<std::string>())
            throw std::runtime_error("Installed direct-boot cache changed; start refused.");
    }
    if (fs::exists(scratch) || !fs::create_directory(scratch)) throw std::runtime_error("Disk validation scratch must be NEW.");
    auto bin = fs::path(utf16(binding.at("binDirectory").get<std::string>()));
    auto tool = [&](const std::vector<std::wstring>& args) {
        auto result = image_tool(bin / "qemu-img.exe", directory, args);
        if (result.exitCode) throw std::runtime_error("Installed disk validation failed: " + result.output);
        return result.output;
    };
    auto info = parse_json_document(tool({L"info", L"--output=json", L"phone.qcow2"}));
    if (info.at("format") != "qcow2" || info.at("virtual-size") != layout.totalBytes || info.contains("backing-filename"))
        throw std::runtime_error("Disk was externally resized/replaced or uses a backing file; start refused.");
    auto table = entries(layout);
    auto backup = table, tail = header(layout, true, crc32(table.data(), table.size()));
    backup.insert(backup.end(), tail.begin(), tail.end());
    auto readback = [&](const wchar_t* name, uint64_t offset, const std::vector<unsigned char>& expected) {
        auto path = scratch / name;
        tool({L"dd", L"-f", L"qcow2", L"-O", L"raw", L"if=phone.qcow2", L"of=" + path.wstring(), L"bs=512",
              L"skip=" + std::to_wstring(offset / 512), L"count=" + std::to_wstring((offset + expected.size()) / 512)});
        std::ifstream input(path, std::ios::binary);
        std::vector<unsigned char> actual((std::istreambuf_iterator<char>(input)), {}); input.close();
        if (actual != expected) throw std::runtime_error("Installed GPT identity/CRC does not match the registry; start refused.");
        fs::remove(path);
    };
    readback(L"primary.bin", 0, primary_table(layout, table));
    readback(L"backup.bin", layout.totalBytes - backup.size(), backup);
    fs::remove(scratch);
}
} // namespace kiki
