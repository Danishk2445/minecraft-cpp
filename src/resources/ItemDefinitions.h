// ItemDefinitions.h -- the pack's item model definitions (items/<id>.json, the
// 1.21.4+ format): model, condition, select, range_dispatch, special,
// composite and bundle nodes with their tint sources. Evaluating one for a
// stack and context gives the layers to draw (vanilla ItemModelResolver).
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "resources/Model.h"

namespace mc {

class Colormap;
class ModelBaker;
class ResourcePack;
struct BakedModel;

// Entity-model renderers items use (vanilla SpecialModelRenderer types).
enum class SpecialModel : uint8_t { Bed, Banner, Chest, Conduit, DecoratedPot, Head, PlayerHead, Shield, ShulkerBox, Trident };

struct SpecialModelSpec {
    SpecialModel type = SpecialModel::Chest;
    std::string texture; // chest, bed and shulker box texture as written ("minecraft:normal")
    std::string kind;    // head kind ("skeleton", "dragon", ...)
    std::string color;   // banner base color ("white")
};

struct ItemTintSource {
    enum class Type : uint8_t { Constant, Dye, Grass, Firework, Potion, MapColor, Team, CustomModelData };
    Type type = Type::Constant;
    uint32_t value = 0xFFFFFFFF; // the constant, or the default used without item data
    float temperature = 0.5f, downfall = 1.0f; // grass
};

// What item model properties read: where the item is drawn and who holds it.
// Item data components don't exist yet, so properties that read them
// (damage, trims, dyes, bundles, block state) take their defaults.
struct ItemModelContext {
    DisplayContext display = DisplayContext::Gui;
    int count = 1, maxStack = 64;
    bool usingItem = false;
    int useDuration = 0; // ticks the holder has been using it (a bow's draw)
    bool overworld = true;
    float timeOfDay = 0.0f; // celestial angle, for clocks
    // Compasses point from the holder toward the world spawn; without a holder they spin.
    std::optional<glm::dvec3> holderPos;
    float holderYaw = 0.0f;
    glm::dvec3 spawnPos{0.0};
    float spin = 0.0f; // 0..1, for spinning compasses and clocks
};

// One part of an item (vanilla ItemStackRenderState.LayerRenderState).
struct ItemLayer {
    const BakedModel* model = nullptr;      // its quads (null for special models)
    const BakedModel* transforms = nullptr; // display transforms and GUI light (the special's base)
    const SpecialModelSpec* special = nullptr;
    std::array<uint32_t, 8> tints{};        // ARGB per tint index
    int tintCount = 0;
    uint32_t tint(int index) const { return index >= 0 && index < tintCount ? tints[size_t(index)] : 0xFFFFFFFF; }
};

class ItemDefinitions {
public:
    ItemDefinitions();
    ~ItemDefinitions();
    ItemDefinitions(ItemDefinitions&&) noexcept;
    ItemDefinitions& operator=(ItemDefinitions&&) noexcept;

    // Parses every items/<id>.json and bakes the models they name with `baker`.
    // Problems are listed in `problems`; returns false if no definitions loaded.
    bool load(const ResourcePack& pack, ModelBaker& baker, std::string* problems = nullptr);
    size_t size() const;
    bool has(std::string_view itemId) const;
    // Every item id with a definition, sorted.
    std::vector<std::string> ids() const;
    // The layers of `itemId` ("minecraft:stone") in `ctx`; the missing model for unknown ids.
    void evaluate(std::string_view itemId, const ItemModelContext& ctx, const Colormap* grass,
                  std::vector<ItemLayer>& out) const;
    // Every baked model any definition can select (for the icon sheet checks).
    std::vector<const BakedModel*> allModels(std::string_view itemId) const;

    struct Node; // a parsed definition node (opaque)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Vanilla ItemDisplayContext names ("gui", "firstperson_righthand", ...).
std::string_view displayContextName(DisplayContext c);

} // namespace mc
