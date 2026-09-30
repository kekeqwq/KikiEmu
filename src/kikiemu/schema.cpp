// SPDX-License-Identifier: GPL-2.0-or-later
#include "schema.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>

namespace kiki {
using json = nlohmann::json;
namespace {
[[noreturn]] void fail(const std::string& at, const std::string& reason) {
    throw std::runtime_error("Package schema violation at " + at + ": " + reason + ".");
}
void visit(const json& value, const json& rule, const json& root, const std::string& at, unsigned depth) {
    if (!rule.is_object() || depth > 32) fail(at, "invalid/excessive schema nesting");
    const std::set<std::string> vocabulary = {"$schema", "$id", "title", "description", "$defs", "$ref",
        "type", "const", "enum", "properties", "required", "additionalProperties", "items", "minItems", "maxItems",
        "uniqueItems", "minLength", "maxLength", "pattern", "minimum", "maximum", "multipleOf"};
    for (const auto& [key, ignored] : rule.items()) {
        (void)ignored;
        if (!vocabulary.contains(key)) fail(at, "unsupported schema keyword " + key);
    }
    if (rule.contains("$ref")) {
        auto ref = rule.at("$ref").get<std::string>();
        if (!ref.starts_with("#/$defs/") || rule.size() != 1) fail(at, "unsupported schema reference");
        visit(value, root.at(json::json_pointer(ref.substr(1))), root, at, depth + 1); return;
    }
    if (rule.contains("type")) {
        auto type = rule.at("type").get<std::string>();
        bool matches = (type == "object" && value.is_object()) || (type == "array" && value.is_array()) ||
                       (type == "string" && value.is_string()) || (type == "integer" && value.is_number_integer()) ||
                       (type == "boolean" && value.is_boolean());
        if (!matches) fail(at, "expected " + type);
    }
    if (rule.contains("const") && value != rule.at("const")) fail(at, "unexpected fixed value");
    if (rule.contains("enum") && std::find(rule.at("enum").begin(), rule.at("enum").end(), value) == rule.at("enum").end())
        fail(at, "value is not in the supported set");
    if (value.is_object()) {
        if (rule.contains("required")) for (const auto& key : rule.at("required"))
            if (!value.contains(key.get<std::string>())) fail(at, "missing " + key.get<std::string>());
        for (const auto& [key, item] : value.items()) {
            if (rule.contains("properties") && rule.at("properties").contains(key))
                visit(item, rule.at("properties").at(key), root, at + "/" + key, depth + 1);
            else if (rule.value("additionalProperties", true) == false) fail(at, "unknown field " + key);
        }
    }
    if (value.is_array()) {
        if (rule.contains("minItems") && value.size() < rule.at("minItems").get<size_t>()) fail(at, "too few items");
        if (rule.contains("maxItems") && value.size() > rule.at("maxItems").get<size_t>()) fail(at, "too many items");
        if (rule.value("uniqueItems", false)) for (size_t i = 0; i < value.size(); ++i)
            for (size_t j = 0; j < i; ++j) if (value[i] == value[j]) fail(at, "duplicate array item");
        if (rule.contains("items")) for (size_t i = 0; i < value.size(); ++i)
            visit(value[i], rule.at("items"), root, at + "/" + std::to_string(i), depth + 1);
    }
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        // Count UTF-8 code points, not bytes, matching JSON Schema length.
        size_t length = 0;
        for (unsigned char byte : text) if ((byte & 0xc0) != 0x80) ++length;
        if (rule.contains("minLength") && length < rule.at("minLength").get<size_t>()) fail(at, "text too short");
        if (rule.contains("maxLength") && length > rule.at("maxLength").get<size_t>()) fail(at, "text too long");
        if (rule.contains("pattern") && !std::regex_search(text, std::regex(rule.at("pattern").get<std::string>())))
            fail(at, "text pattern mismatch");
    }
    if (value.is_number_integer()) {
        if (!value.is_number_unsigned() && value.get<int64_t>() < 0) fail(at, "negative integers are unsupported");
        auto number = value.get<uint64_t>();
        if (rule.contains("minimum") && number < rule.at("minimum").get<uint64_t>()) fail(at, "integer below minimum");
        if (rule.contains("maximum") && number > rule.at("maximum").get<uint64_t>()) fail(at, "integer above maximum");
        if (rule.contains("multipleOf")) {
            auto divisor = rule.at("multipleOf").get<uint64_t>();
            if (!divisor || number % divisor) fail(at, "integer alignment mismatch");
        }
    }
}
struct Version { std::array<uint64_t, 3> numbers; std::vector<std::string> prerelease; };
Version parse_version(const std::string& text) {
    std::smatch match;
    if (text.size() > 64 || !std::regex_match(text, match, std::regex("(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)(?:-([a-z0-9.-]+))?")))
        throw std::runtime_error("Unsupported version syntax.");
    Version result{{std::stoull(match[1]), std::stoull(match[2]), std::stoull(match[3])}, {}};
    if (match[4].matched) {
        auto tail = match[4].str(); size_t begin = 0;
        do {
            size_t end = tail.find('.', begin);
            auto part = tail.substr(begin, end == std::string::npos ? end : end - begin);
            if (part.empty() || (part.find_first_not_of("0123456789") == std::string::npos && part.size() > 1 && part[0] == '0'))
                throw std::runtime_error("Noncanonical version prerelease identifier.");
            result.prerelease.push_back(part);
            if (end == std::string::npos) break;
            begin = end + 1;
        } while (true);
    }
    return result;
}
bool greater(const Version& a, const Version& b) {
    if (a.numbers != b.numbers) return a.numbers > b.numbers;
    if (a.prerelease.empty() != b.prerelease.empty()) return a.prerelease.empty();
    for (size_t i = 0; i < std::min(a.prerelease.size(), b.prerelease.size()); ++i) {
        const auto& x = a.prerelease[i]; const auto& y = b.prerelease[i];
        if (x == y) continue;
        bool xn = x.find_first_not_of("0123456789") == std::string::npos, yn = y.find_first_not_of("0123456789") == std::string::npos;
        if (xn != yn) return !xn;
        if (xn && x.size() != y.size()) return x.size() > y.size();
        return x > y;
    }
    return a.prerelease.size() > b.prerelease.size();
}
} // namespace
void validate_schema(const json& value, const json& schema) { visit(value, schema, schema, "$", 0); }
bool version_supported(const std::string& minimum, const std::string& current) {
    return !greater(parse_version(minimum), parse_version(current));
}
} // namespace kiki
