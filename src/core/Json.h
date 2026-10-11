// Json.h -- nlohmann::json with error messages that name their source.
#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace mc {

using Json = nlohmann::json;

struct JsonError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Throws JsonError("<source>: <parser message>") on malformed input.
Json parseJson(std::string_view text, std::string_view source);

// Reads and parses a file; returns nullopt and fills `error` on failure.
std::optional<Json> loadJsonFile(const std::filesystem::path& path, std::string* error = nullptr);

// Typed lookups with a default, tolerant of missing keys and wrong types.
template <class T>
T jsonGet(const Json& j, std::string_view key, T fallback) {
    if (!j.is_object()) return fallback;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try {
        return it->get<T>();
    } catch (const Json::exception&) {
        return fallback;
    }
}

} // namespace mc
