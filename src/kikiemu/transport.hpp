// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lifecycle.hpp"
#include <memory>

namespace kiki {
class PortReservation {
public:
    PortReservation();
    ~PortReservation();
    PortReservation(const PortReservation&) = delete;
    uint16_t port() const;
    void release();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
struct ShellResult { std::string output, error; uint32_t exitCode; };
// Direct, private adbd wire connection. No host adb.exe daemon, default
// transport, global 5037 server or host ADB key configuration is touched.
ShellResult guest_shell(const OwnedProcess& qemu, uint16_t port, const std::string& command,
                        uint32_t timeoutMs = 5000);
void qmp_powerdown(const OwnedProcess& qemu, uint16_t port);
} // namespace kiki
