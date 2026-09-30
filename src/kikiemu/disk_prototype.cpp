// SPDX-License-Identifier: GPL-2.0-or-later
// Internal SYSTEM prototype, not public CLI create/installation acceptance.
#include "disk.hpp"
#include <iostream>
#include <fstream>
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 5 && std::wstring(argv[1]) == L"--verify-installed") {
            auto bin = kiki::normalize_directory(argv[2]), directory = kiki::normalize_directory(argv[3]), scratch = kiki::normalize_directory(argv[4]);
            std::ifstream input(directory / "disk-layout.json");
            std::string text((std::istreambuf_iterator<char>(input)), {});
            kiki::verify_installed_disk(kiki::inspect_qemu(bin), directory, kiki::parse_json_document(text), scratch);
            std::cout << "Installed capacity, primary/backup GPT and boot-cache identity verified read-only.\n"; return 0;
        }
        if (argc != 7) {
            std::cerr << "Usage: kikiemu-disk-prototype.exe QEMU_BIN NEW_DIRECTORY TOTAL_GIB BOOT_IMAGE SYSTEM_IMAGE VENDOR_IMAGE\n";
            return 2;
        }
        auto binding = kiki::inspect_qemu(kiki::normalize_directory(argv[1]));
        auto record = kiki::install_disk(binding, kiki::normalize_directory(argv[2]), kiki::parse_gib(argv[3]),
                                         {{"boot", kiki::normalize_directory(argv[4])}, {"system", kiki::normalize_directory(argv[5])},
                                          {"vendor", kiki::normalize_directory(argv[6])}}, 8ULL << 30);
        std::cout << record.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n'; return 1;
    }
}
