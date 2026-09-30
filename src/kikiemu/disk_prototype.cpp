// SPDX-License-Identifier: GPL-2.0-or-later
// Internal SYSTEM prototype, not public CLI create/installation acceptance.
#include "disk.hpp"
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    try {
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
