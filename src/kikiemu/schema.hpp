// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "runtime.hpp"

namespace kiki {
// Implements ONLY the bounded standard keywords used by the pinned producer
// schema. Unknown keywords/external refs fail closed: never claim a general
// JSON Schema implementation or silently accept future schema vocabulary.
void validate_schema(const nlohmann::json& value, const nlohmann::json& schema);
bool version_supported(const std::string& minimum, const std::string& current = "0.1.0-alpha");
} // namespace kiki
