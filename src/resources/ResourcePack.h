// ResourcePack.h -- resource locations and file lookup inside a vanilla-layout pack.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Json.h"

namespace mc {

// "minecraft:block/stone" or "block/stone" (namespace defaults to minecraft).
struct ResourceLocation {
    std::string ns = "minecraft";
    std::string path;

    static ResourceLocation parse(std::string_view text);
    std::string str() const { return ns + ":" + path; }
    bool operator==(const ResourceLocation&) const = default;
};

// Adds the default namespace: "block/stone" -> "minecraft:block/stone".
std::string canonicalId(std::string_view text);
// Strips "minecraft:" for display and file names.
std::string_view shortId(std::string_view id);

class ResourcePack {
public:
    explicit ResourcePack(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }

    // <root>/assets/<ns>/<relative>
    std::filesystem::path file(std::string_view ns, std::string_view relative) const;
    // <root>/assets/<ns>/<kind>/<path><ext>, e.g. kind "textures", ext ".png"
    std::filesystem::path resource(const ResourceLocation& id, std::string_view kind, std::string_view ext) const;

    std::optional<Json> json(const std::filesystem::path& path, std::string* error = nullptr) const;

    // Resource ids under assets/<ns>/<dir>/ (recursive) with the given extension,
    // relative to dir and without the extension, sorted. E.g. list("minecraft",
    // "textures/block", ".png") -> {"acacia_door_bottom", ...}.
    std::vector<std::string> list(std::string_view ns, std::string_view dir, std::string_view ext) const;

private:
    std::filesystem::path root_;
};

} // namespace mc
