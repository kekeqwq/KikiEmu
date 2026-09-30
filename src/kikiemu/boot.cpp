// SPDX-License-Identifier: GPL-2.0-or-later
#include "boot.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace kiki {
static uint64_t little(const unsigned char* data, size_t width) {
    uint64_t result = 0;
    for (size_t i = 0; i < width; ++i) result |= uint64_t(data[i]) << (8 * i);
    return result;
}
static uint64_t page(uint64_t bytes) { return (bytes + 4095) / 4096 * 4096; }
BootHeader parse_boot_header(const std::array<unsigned char, 4096>& bytes, uint64_t fileBytes) {
    if (std::memcmp(bytes.data(), "ANDROID!", 8) || little(bytes.data() + 40, 4) != 4 ||
        little(bytes.data() + 20, 4) != 1584 || little(bytes.data() + 1580, 4) != 0)
        throw std::runtime_error("Unsupported Android boot header; GPT-v1 requires unsigned header-v4 direct boot.");
    for (size_t i = 24; i < 40; ++i)
        if (bytes[i]) throw std::runtime_error("Boot reserved fields must be zero.");
    for (size_t i = 44; i < 1580; ++i)
        if (bytes[i]) throw std::runtime_error("A system package cannot supply hidden host/guest launch arguments.");
    for (size_t i = 1584; i < bytes.size(); ++i)
        if (bytes[i]) throw std::runtime_error("Boot header padding must be zero.");
    uint64_t kernel = little(bytes.data() + 8, 4), ramdisk = little(bytes.data() + 12, 4);
    if (kernel < 64 || kernel > 256ULL << 20 || ramdisk < 10 || ramdisk > 64ULL << 20)
        throw std::runtime_error("Boot payload component sizes are invalid or exceed the supported limits.");
    uint64_t ramdiskOffset = 4096 + page(kernel);
    uint64_t expected = ramdiskOffset + page(ramdisk);
    if (fileBytes != expected) throw std::runtime_error("Boot payload is truncated or contains unsupported trailing data.");
    return {kernel, ramdisk, ramdiskOffset, expected};
}
nlohmann::json derive_boot_cache(const fs::path& payload, const fs::path& newDirectory) {
    if (fs::exists(newDirectory)) throw std::runtime_error("Direct-boot cache destination already exists.");
    std::ifstream input(payload, std::ios::binary);
    std::array<unsigned char, 4096> bytes{};
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("Truncated boot payload header.");
    auto header = parse_boot_header(bytes, fs::file_size(payload));
    std::array<unsigned char, 64> kernel{};
    input.seekg(4096);
    if (!input.read(reinterpret_cast<char*>(kernel.data()), kernel.size()) ||
        std::memcmp(kernel.data() + 56, "ARM\x64", 4) || ((little(kernel.data() + 24, 8) >> 1) & 3) != 1)
        throw std::runtime_error("Boot kernel must be an uncompressed AArch64 Linux Image with 4-KiB pages.");
    input.seekg(header.ramdiskOffset);
    unsigned char gzip[3];
    if (!input.read(reinterpret_cast<char*>(gzip), 3) || gzip[0] != 0x1f || gzip[1] != 0x8b || gzip[2] != 8)
        throw std::runtime_error("Boot ramdisk must use the tracked gzip initramfs ABI.");
    auto expected = sha256(payload);
    fs::create_directories(newDirectory);
    auto copy = [&](const wchar_t* name, uint64_t offset, uint64_t length) {
        input.clear(); input.seekg(offset);
        std::ofstream output(newDirectory / name, std::ios::binary);
        std::array<char, 65536> buffer;
        while (length) {
            auto count = std::min<uint64_t>(length, buffer.size());
            if (!input.read(buffer.data(), count) || !output.write(buffer.data(), count))
                throw std::runtime_error("Could not derive the owned direct-boot cache.");
            length -= count;
        }
    };
    copy(L"kernel", 4096, header.kernelBytes);
    copy(L"ramdisk.img", header.ramdiskOffset, header.ramdiskBytes);
    if (sha256(payload) != expected) throw std::runtime_error("Boot payload changed during cache generation.");
    return {{"sourceBootSha256", expected}, {"kernel", "boot/kernel"}, {"ramdisk", "boot/ramdisk.img"},
            {"kernelBytes", header.kernelBytes}, {"ramdiskBytes", header.ramdiskBytes},
            {"kernelSha256", sha256(newDirectory / "kernel")}, {"ramdiskSha256", sha256(newDirectory / "ramdisk.img")}};
}
} // namespace kiki
