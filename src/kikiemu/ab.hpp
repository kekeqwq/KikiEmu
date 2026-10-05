// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "disk.hpp"
#include <array>
namespace kiki {
// AOSP bootloader_control, CRC32, priorities, tries and success semantics.
int select_ab_slot(std::array<unsigned char,32>& control);
nlohmann::json prepare_ab_boot(const nlohmann::json& binding,const fs::path& disk,const nlohmann::json& layout,const fs::path& log);
void fail_ab_slot(const nlohmann::json& binding,const fs::path& disk,const nlohmann::json& layout,int slot,const fs::path& log);
}
