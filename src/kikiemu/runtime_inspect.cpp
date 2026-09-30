// SPDX-License-Identifier: GPL-2.0-or-later
// Development build tool, NOT the released instance manager. Does not start a
// VM, install an app, alter PATH or write any configuration/user disk.
#include "runtime.hpp"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 2 || argc > 3) {
            std::cerr << "Usage: kikiemu-runtime-inspect.exe QEMU_BIN [BUILD_DLL_SOURCE]\n";
            return 2;
        }
        std::vector<kiki::fs::path> sources;
        if (argc == 3) sources.push_back(kiki::normalize_directory(argv[2]));
        auto result = kiki::inspect_qemu(kiki::normalize_directory(argv[1]), sources);
        std::cout << result.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
