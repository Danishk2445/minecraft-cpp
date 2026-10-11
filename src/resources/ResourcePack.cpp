#include "resources/ResourcePack.h"

#include <algorithm>

namespace mc {

ResourceLocation ResourceLocation::parse(std::string_view text) {
    ResourceLocation id;
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        id.path = std::string(text);
    } else {
        id.ns = std::string(text.substr(0, colon));
        id.path = std::string(text.substr(colon + 1));
    }
    return id;
}

std::string canonicalId(std::string_view text) {
    if (text.find(':') != std::string_view::npos) return std::string(text);
    return "minecraft:" + std::string(text);
}

std::string_view shortId(std::string_view id) {
    if (id.starts_with("minecraft:")) id.remove_prefix(10);
    return id;
}

ResourcePack::ResourcePack(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path ResourcePack::file(std::string_view ns, std::string_view relative) const {
    return root_ / "assets" / std::string(ns) / std::string(relative);
}

std::filesystem::path ResourcePack::resource(const ResourceLocation& id, std::string_view kind,
                                             std::string_view ext) const {
    return root_ / "assets" / id.ns / std::string(kind) / (id.path + std::string(ext));
}

std::optional<Json> ResourcePack::json(const std::filesystem::path& path, std::string* error) const {
    return loadJsonFile(path, error);
}

std::vector<std::string> ResourcePack::list(std::string_view ns, std::string_view dir, std::string_view ext) const {
    std::vector<std::string> out;
    const auto base = file(ns, dir);
    std::error_code ec;
    if (!std::filesystem::is_directory(base, ec)) return out;
    for (auto it = std::filesystem::recursive_directory_iterator(base, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const auto& p = it->path();
        const std::string name = p.filename().string();
        if (!name.ends_with(ext)) continue;
        std::string rel = std::filesystem::relative(p, base).generic_string();
        rel.resize(rel.size() - ext.size());
        out.push_back(std::move(rel));
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace mc
