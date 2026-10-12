#include "game/ItemUse.h"

#include <string>

#include "resources/ResourcePack.h"

namespace mc {
namespace {

// Copper's weathering stages (vanilla WeatheringCopper): "exposed_", "weathered_" and
// "oxidized_" prefixes on the base names, the full block being "copper_block" bare.
std::optional<std::string> copperPrevious(std::string_view id) {
    auto previous = [&](std::string_view prefix, std::string_view before) -> std::optional<std::string> {
        if (!id.starts_with(prefix)) return std::nullopt;
        std::string rest(id.substr(prefix.size()));
        if (before.empty() && rest == "copper") return "copper_block"; // exposed_copper -> copper_block
        return std::string(before) + rest;
    };
    if (auto p = previous("oxidized_", "weathered_")) return p;
    if (auto p = previous("weathered_", "exposed_")) return p;
    if (auto p = previous("exposed_", "")) return p;
    return std::nullopt;
}

bool isCopper(std::string_view id) { return id.find("copper") != std::string_view::npos && !id.ends_with("_ore"); }

} // namespace

StateId withSameProperties(const BlockRegistry& registry, StateId from, const Block& to) {
    StateId out = to.defaultState;
    const Block& source = registry.blockOf(from);
    for (uint16_t prop : to.props) {
        const std::string& name = registry.property(prop).name;
        if (std::find(source.props.begin(), source.props.end(), prop) == source.props.end()) continue;
        if (auto s = registry.with(out, name, registry.value(from, name))) out = *s;
    }
    return out;
}

std::optional<ItemUseResult> useItemOn(const BlockGetter& level, const ItemRegistry& items, const ItemStack& held,
                                       const BlockPos& pos, Direction face, Direction playerFacing) {
    if (held.empty()) return std::nullopt;
    const BlockRegistry& reg = level.registry();
    const StateId state = level.getBlock(pos);
    const std::string_view id = shortId(reg.blockOf(state).id);
    const std::string_view item = shortId(held.item->id);
    const bool airAbove = (reg.info(level.getBlock(pos.offset(0, 1, 0))).flags & kAir) != 0;
    auto become = [&](std::string_view block) -> std::optional<ItemUseResult> {
        const Block* b = reg.find(block);
        if (!b) return std::nullopt;
        ItemUseResult r;
        r.pos = pos;
        r.state = withSameProperties(reg, state, *b);
        r.wear = 1;
        return r;
    };
    auto stack = [&](std::string_view itemId, int n) {
        const Item* i = items.find(itemId);
        return i ? ItemStack{i, n} : ItemStack{};
    };

    switch (held.item->tool) {
    case ToolKind::Hoe: {
        // HoeItem.TILLABLES: farmland from grass, paths and dirt (with air above, not from
        // below); coarse dirt becomes dirt; rooted dirt gives up its roots from any side.
        if (id == "rooted_dirt") {
            auto r = become("dirt");
            if (r) r->drops.push_back(stack("hanging_roots", 1));
            return r;
        }
        if (face == Direction::Down || !airAbove) return std::nullopt;
        if (id == "grass_block" || id == "dirt_path" || id == "dirt") return become("farmland");
        if (id == "coarse_dirt") return become("dirt");
        return std::nullopt;
    }
    case ToolKind::Shovel: {
        // ShovelItem: paths (not from below, air above); lit campfires are put out.
        if (face == Direction::Down) return std::nullopt;
        if ((id == "grass_block" || id == "dirt" || id == "podzol" || id == "coarse_dirt" || id == "mycelium" ||
             id == "rooted_dirt") &&
            airAbove)
            return become("dirt_path");
        if ((id == "campfire" || id == "soul_campfire") && reg.boolValue(state, "lit")) {
            ItemUseResult r{pos, *reg.with(state, "lit", "false"), {}, 1, false};
            return r;
        }
        return std::nullopt;
    }
    case ToolKind::Axe: {
        // AxeItem: strip logs and wood, else scrape a stage of weathering off copper, else its wax.
        for (const char* suffix : {"_log", "_wood", "_stem", "_hyphae"})
            if (id.ends_with(suffix) && !id.starts_with("stripped_")) return become("stripped_" + std::string(id));
        if (id == "bamboo_block") return become("stripped_bamboo_block");
        if (isCopper(id)) {
            if (!id.starts_with("waxed_"))
                if (auto previous = copperPrevious(id)) return become(*previous);
            if (id.starts_with("waxed_")) return become(id.substr(6));
        }
        return std::nullopt;
    }
    case ToolKind::Shears: {
        // PumpkinBlock.useItemOn: carved toward the clicked side (or toward the player from
        // above and below), dropping four seeds.
        if (id != "pumpkin") return std::nullopt;
        const Direction facing = face == Direction::Up || face == Direction::Down ? opposite(playerFacing) : face;
        const Block* carved = reg.find("carved_pumpkin");
        if (!carved) return std::nullopt;
        ItemUseResult r;
        r.pos = pos;
        r.state = *reg.with(carved->defaultState, "facing", directionName(facing));
        r.drops.push_back(stack("pumpkin_seeds", 4));
        r.wear = 1;
        return r;
    }
    default: break;
    }

    if (item == "honeycomb" && isCopper(id) && !id.starts_with("waxed_")) {
        // HoneycombItem: copper keeps its stage, waxed.
        const std::string waxed = id == "copper_block" ? "waxed_copper_block" : "waxed_" + std::string(id);
        auto r = become(waxed);
        if (r) {
            r->wear = 0;
            r->consume = true;
        }
        return r;
    }
    if (item == "flint_and_steel") {
        // FlintAndSteelItem: light a campfire or candles (not under water), else set fire to
        // the air beside the clicked face (on a sturdy top; soul fire over soul soil and sand).
        const bool lightable = (id == "campfire" || id == "soul_campfire" || id.ends_with("candle") || id == "candle" ||
                                id.ends_with("candle_cake")) &&
                               reg.value(state, "lit") == "false" && !reg.boolValue(state, "waterlogged");
        if (lightable) return ItemUseResult{pos, *reg.with(state, "lit", "true"), {}, 1, false};
        const BlockPos firePos = pos.offset(face);
        if (!(reg.info(level.getBlock(firePos)).flags & kAir)) return std::nullopt;
        const StateId below = level.getBlock(firePos.offset(0, -1, 0));
        if (!(reg.info(below).sturdyFaces & (1u << int(Direction::Up)))) return std::nullopt;
        const std::string_view belowId = shortId(reg.blockOf(below).id);
        const Block* fire = reg.find(belowId == "soul_soil" || belowId == "soul_sand" ? "soul_fire" : "fire");
        if (!fire) return std::nullopt;
        return ItemUseResult{firePos, fire->defaultState, {}, 1, false};
    }
    return std::nullopt;
}

} // namespace mc
