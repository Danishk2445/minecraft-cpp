#include "core/Json.h"

#include <format>

#include "core/Fs.h"

namespace mc {

Json parseJson(std::string_view text, std::string_view source) {
    try {
        return Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        throw JsonError(std::format("{}: {}", source, e.what()));
    }
}

std::optional<Json> loadJsonFile(const std::filesystem::path& path, std::string* error) {
    auto text = fs::readText(path);
    if (!text) {
        if (error) *error = std::format("{}: cannot read file", path.string());
        return std::nullopt;
    }
    try {
        return parseJson(*text, path.string());
    } catch (const JsonError& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

} // namespace mc
