// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdexcept>

namespace kiki::detail {
enum class ProcessState { live, exited, failed };

// Both callbacks MUST refer to one held process handle. Never reopen a PID
// between these observations: an exited handle cannot become a reused PID.
// Windows image teardown can precede the process object's exit signal. The
// wait callback takes true ONLY on query failure, allowing a bounded wait on
// that same handle. Failure is an exit ONLY once its object actually signals.
template<class Wait, class Verify>
bool verify_live_identity(Wait wait, Verify verify) {
    const auto before = wait(false);
    if (before == ProcessState::exited) return false;
    if (before != ProcessState::live)
        throw std::runtime_error("Could not inspect pinned process state; operation refused.");
    try { verify(); }
    catch (const std::exception&) {
        if (wait(true) == ProcessState::exited) return false;
        throw; // Live mismatches, access failures and unknown states fail closed.
    }
    const auto after = wait(false);
    if (after == ProcessState::exited) return false;
    if (after != ProcessState::live)
        throw std::runtime_error("Could not recheck pinned process state; operation refused.");
    return true;
}
} // namespace kiki::detail
