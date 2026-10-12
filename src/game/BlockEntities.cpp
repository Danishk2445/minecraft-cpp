#include "game/BlockEntities.h"

#include <algorithm>
#include <cmath>

#include "resources/ResourcePack.h"
#include "world/level/Level.h"

namespace mc {
namespace {

Json saveSlots(std::span<const ItemStack> slots) {
    Json list = Json::array();
    for (size_t i = 0; i < slots.size(); ++i) {
        Json s = stackToJson(slots[i]);
        if (s.is_null()) continue;
        s["slot"] = i;
        list.push_back(std::move(s));
    }
    return list;
}

void loadSlots(std::span<ItemStack> slots, const Json& list, const ItemRegistry& items) {
    for (ItemStack& s : slots) s = {};
    if (!list.is_array()) return;
    for (const Json& s : list) {
        const int slot = s.value("slot", -1);
        if (slot >= 0 && slot < int(slots.size())) slots[size_t(slot)] = stackFromJson(s, items);
    }
}

} // namespace

void ContainerBlockEntity::save(Json& out) const { out["items"] = saveSlots(items); }

void ContainerBlockEntity::load(const Json& in, const ItemRegistry& registry) {
    loadSlots(items, in.value("items", Json::array()), registry);
}

void ContainerBlockEntity::tick(Level& level) {
    if (isChest()) {
        oldOpenness = openness_;
        openness_ = viewers > 0 ? std::min(openness_ + 0.1f, 1.0f) : std::max(openness_ - 0.1f, 0.0f);
        return;
    }
    const StateId state = level.getBlock(pos);
    const std::string_view want = viewers > 0 ? "true" : "false";
    if (level.registry().value(state, "open") != want)
        if (auto changed = level.registry().with(state, "open", want)) level.setBlockAndUpdate(pos, *changed);
}

std::vector<ItemStack> ContainerBlockEntity::contents() const {
    std::vector<ItemStack> out;
    for (const ItemStack& s : items)
        if (!s.empty()) out.push_back(s);
    return out;
}

FurnaceBlockEntity::FurnaceBlockEntity(const BlockPos& p, RecipeType k, const RecipeBook* recipes)
    : BlockEntity(p), kind(k), recipes_(recipes) {
    cookingTotalTime = k == RecipeType::Smelting ? 200 : 100;
}

std::string_view FurnaceBlockEntity::type() const {
    return kind == RecipeType::Smoking ? "minecraft:smoker" : kind == RecipeType::Blasting ? "minecraft:blast_furnace"
                                                                                         : "minecraft:furnace";
}

const Recipe* FurnaceBlockEntity::recipeFor(const ItemStack& input) const {
    return recipes_ ? recipes_->findCooking(kind, input) : nullptr;
}

bool FurnaceBlockEntity::canBurn(const Recipe* recipe) const {
    if (items[kInput].empty() || !recipe) return false;
    const ItemStack& out = items[kOutput];
    if (out.empty()) return true;
    if (!out.sameItem(recipe->result)) return false;
    return out.count + recipe->result.count <= std::min(99, out.maxStack());
}

void FurnaceBlockEntity::tick(Level& level) {
    // Vanilla AbstractFurnaceBlockEntity.serverTick.
    const bool wasLit = lit();
    if (lit()) --litTime;
    ItemStack& fuel = items[kFuel];
    ItemStack& input = items[kInput];
    if (lit() || (!fuel.empty() && !input.empty())) {
        const Recipe* recipe = input.empty() ? nullptr : recipeFor(input);
        if (!lit() && canBurn(recipe)) {
            litTime = litDuration = fuel.empty() ? 0 : fuel.item->fuelTicks;
            if (lit() && !fuel.empty()) {
                const ItemStack left = recipes_ ? recipes_->remainder(fuel) : ItemStack{};
                fuel.shrink(1);
                if (fuel.empty()) fuel = left; // a lava bucket leaves its bucket
            }
        }
        if (lit() && canBurn(recipe)) {
            if (++cookingProgress == cookingTotalTime) {
                cookingProgress = 0;
                cookingTotalTime = recipe->cookingTime;
                // burn(): the result goes out, a wet sponge fills a bucket with its water.
                ItemStack& out = items[kOutput];
                if (out.empty()) out = recipe->result;
                else out.grow(recipe->result.count);
                if (shortId(input.item->id) == "wet_sponge" && !fuel.empty() && shortId(fuel.item->id) == "bucket")
                    if (const Item* water = level.itemRegistry() ? level.itemRegistry()->find("water_bucket") : nullptr)
                        fuel = {water, 1};
                input.shrink(1);
                storedExperience += recipe->experience;
            }
        } else {
            cookingProgress = 0;
        }
    } else if (!lit() && cookingProgress > 0) {
        cookingProgress = std::clamp(cookingProgress - 2, 0, cookingTotalTime);
    }
    // A new input starts at its own cooking time.
    if (const Recipe* r = input.empty() ? nullptr : recipeFor(input); r && cookingProgress == 0)
        cookingTotalTime = r->cookingTime;
    if (wasLit != lit()) {
        const StateId state = level.getBlock(pos);
        if (auto changed = level.registry().with(state, "lit", lit() ? "true" : "false"))
            level.setBlockAndUpdate(pos, *changed);
    }
}

float FurnaceBlockEntity::litProgress() const {
    const int total = litDuration == 0 ? 200 : litDuration;
    return std::clamp(float(litTime) / float(total), 0.0f, 1.0f);
}

float FurnaceBlockEntity::cookProgress() const {
    return cookingTotalTime != 0 && cookingProgress != 0
               ? std::clamp(float(cookingProgress) / float(cookingTotalTime), 0.0f, 1.0f)
               : 0.0f;
}

int FurnaceBlockEntity::takeExperience(float random01) {
    int points = int(std::floor(storedExperience));
    const float fraction = storedExperience - std::floor(storedExperience);
    if (fraction != 0.0f && random01 < fraction) ++points;
    storedExperience = 0.0f;
    return points;
}

void FurnaceBlockEntity::save(Json& out) const {
    out["items"] = saveSlots(items);
    out["lit_time"] = litTime;
    out["lit_duration"] = litDuration;
    out["cooking_time"] = cookingProgress;
    out["cooking_total_time"] = cookingTotalTime;
    out["experience"] = storedExperience;
}

void FurnaceBlockEntity::load(const Json& in, const ItemRegistry& registry) {
    loadSlots(items, in.value("items", Json::array()), registry);
    litTime = std::max(0, in.value("lit_time", 0));
    litDuration = std::max(0, in.value("lit_duration", 0));
    cookingProgress = std::max(0, in.value("cooking_time", 0));
    cookingTotalTime = std::max(1, in.value("cooking_total_time", cookingTotalTime));
    storedExperience = std::max(0.0f, in.value("experience", 0.0f));
}

std::vector<ItemStack> FurnaceBlockEntity::contents() const {
    std::vector<ItemStack> out;
    for (const ItemStack& s : items)
        if (!s.empty()) out.push_back(s);
    return out;
}

// Vanilla's names (SignText codec), so /setblock takes the same NBT as vanilla.
void SignBlockEntity::save(Json& out) const {
    auto side = [](const Side& s) {
        return Json{{"messages", s.lines}, {"color", s.color}, {"has_glowing_text", s.glowing}};
    };
    out["front_text"] = side(front);
    out["back_text"] = side(back);
    out["is_waxed"] = waxed;
}

void SignBlockEntity::load(const Json& in, const ItemRegistry&) {
    auto side = [](const Json& j, Side& s) {
        if (!j.is_object()) return;
        const Json lines = j.value("messages", Json::array());
        for (size_t i = 0; i < 4 && i < lines.size(); ++i)
            if (lines[i].is_string()) s.lines[i] = lines[i].get<std::string>();
        s.color = j.value("color", std::string("black"));
        // SNBT booleans arrive as 1b/0b.
        const Json glowing = j.value("has_glowing_text", Json(false));
        s.glowing = glowing.is_boolean() ? glowing.get<bool>() : glowing.is_number() && glowing.get<double>() != 0.0;
    };
    side(in.value("front_text", Json()), front);
    side(in.value("back_text", Json()), back);
    const Json waxedJson = in.value("is_waxed", Json(false));
    waxed = waxedJson.is_boolean() ? waxedJson.get<bool>() : waxedJson.is_number() && waxedJson.get<double>() != 0.0;
}

uint32_t dyeTextColor(std::string_view dye) {
    static const std::pair<std::string_view, uint32_t> kColors[] = {
        {"white", 0xFFFFFF},      {"orange", 0xFF681F}, {"magenta", 0xFF00FF}, {"light_blue", 0x9AC0CD},
        {"yellow", 0xFFFF00},     {"lime", 0xBFFF00},   {"pink", 0xFF69B4},    {"gray", 0x808080},
        {"light_gray", 0xD3D3D3}, {"cyan", 0x00FFFF},   {"purple", 0xA020F0},  {"blue", 0x0000FF},
        {"brown", 0x8B4513},      {"green", 0x00FF00},  {"red", 0xFF0000},     {"black", 0x000000}};
    for (const auto& [name, color] : kColors)
        if (name == dye) return color;
    return 0x000000;
}

std::shared_ptr<BlockEntity> createBlockEntity(std::string_view blockId, const BlockPos& pos, const RecipeBook* recipes) {
    if (blockId.starts_with("minecraft:")) blockId.remove_prefix(10);
    if (blockId.ends_with("_hanging_sign")) return std::make_shared<SignBlockEntity>(pos, true);
    if (blockId.ends_with("_sign")) return std::make_shared<SignBlockEntity>(pos, false);
    // The ender chest's slots are the player's; its entity only works the lid.
    if (blockId == "chest" || blockId == "trapped_chest" || blockId == "barrel" || blockId == "ender_chest")
        return std::make_shared<ContainerBlockEntity>(pos, "minecraft:" + std::string(blockId));
    if (blockId == "furnace") return std::make_shared<FurnaceBlockEntity>(pos, RecipeType::Smelting, recipes);
    if (blockId == "smoker") return std::make_shared<FurnaceBlockEntity>(pos, RecipeType::Smoking, recipes);
    if (blockId == "blast_furnace") return std::make_shared<FurnaceBlockEntity>(pos, RecipeType::Blasting, recipes);
    return nullptr;
}

} // namespace mc
