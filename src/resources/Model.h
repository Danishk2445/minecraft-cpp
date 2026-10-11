// Model.h -- block/item model JSON: parsing and parent-chain resolution.
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "resources/ResourcePack.h"

namespace mc {

struct FaceDef {
    std::array<float, 4> uv{0, 0, 16, 16};
    bool hasUv = false;
    std::string texture; // variable name without '#'
    int cull = -1;       // Direction index, -1 = never culled
    int rotation = 0;    // 0, 90, 180, 270
    int tint = -1;
};

struct ElementDef {
    glm::vec3 from{0.0f};
    glm::vec3 to{16.0f};
    bool hasRotation = false;
    glm::vec3 rotationOrigin{8.0f};
    Axis rotationAxis = Axis::Y;
    float rotationAngle = 0.0f;
    bool rescale = false;
    bool shade = true;
    int lightEmission = 0;
    std::array<std::optional<FaceDef>, 6> faces; // by Direction index
};

struct ItemTransform {
    glm::vec3 rotation{0.0f};
    glm::vec3 translation{0.0f}; // pixels (clamped to +-80 at use)
    glm::vec3 scale{1.0f};
};

enum class DisplayContext : uint8_t {
    ThirdPersonRight,
    ThirdPersonLeft,
    FirstPersonRight,
    FirstPersonLeft,
    Head,
    Gui,
    Ground,
    Fixed,
    Count
};
std::optional<DisplayContext> displayContextFromName(std::string_view name);

struct ModelDef {
    std::string name;   // canonical id ("minecraft:block/stone")
    std::string parent; // canonical id or empty
    std::vector<std::pair<std::string, std::string>> textures;
    std::optional<std::vector<ElementDef>> elements;
    std::optional<bool> ambientOcclusion;
    std::optional<bool> guiLightFront;
    std::array<std::optional<ItemTransform>, size_t(DisplayContext::Count)> display;

    static std::optional<ModelDef> parse(std::string name, const Json& j, std::string* error);
};

struct ResolvedModel {
    std::string name;
    std::unordered_map<std::string, std::string> textures; // merged, values unresolved
    const std::vector<ElementDef>* elements = nullptr;      // nearest model that has elements
    bool ambientOcclusion = true;
    bool guiLightFront = false;
    bool builtinGenerated = false; // the chain reaches builtin/generated (item layers)
    std::array<ItemTransform, size_t(DisplayContext::Count)> display{};
    std::array<bool, size_t(DisplayContext::Count)> hasDisplay{};

    // Follows "#var" chains to a sprite id. Returns "" if the variable is unbound.
    std::string resolveTexture(std::string_view variable) const;
};

// Loads models on demand from assets/<ns>/models/ and caches them.
class ModelStore {
public:
    explicit ModelStore(const ResourcePack& pack);

    // nullptr if the file is missing or malformed (the error is recorded once).
    const ModelDef* get(const std::string& id);
    std::optional<ResolvedModel> resolve(const std::string& id);

    const std::vector<std::string>& errors() const { return errors_; }
    size_t loadedCount() const { return models_.size(); }

private:
    const ResourcePack& pack_;
    std::unordered_map<std::string, std::unique_ptr<ModelDef>> models_; // null entries = failed
    std::vector<std::string> errors_;
};

} // namespace mc
