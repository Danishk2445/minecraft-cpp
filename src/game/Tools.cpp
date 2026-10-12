#include "game/Tools.h"

#include <limits>

#include "entity/Player.h"
#include "resources/ResourcePack.h"

namespace mc {
namespace {

bool inTag(const BlockRegistry& registry, StateId state, std::string_view tag) {
    const auto t = registry.tags().find(tag);
    return t && registry.tags().containsBlock(*t, registry.info(state).block);
}

bool isBlock(const BlockRegistry& registry, StateId state, std::string_view id) {
    return shortId(registry.blockOf(state).id) == id;
}

// One Tool rule's verdict on a block: a speed, whether it counts for drops, or no say.
struct Verdict {
    bool matched = false;
    float speed = -1.0f; // < 0: the rule sets no speed
    int drops = -1;      // -1: the rule doesn't decide drops
};

// The first rule of the item's Tool component that matches the block, per question
// (speed and drops are decided by the first rule that has an opinion on each).
void evaluate(const ItemStack& held, StateId state, const BlockRegistry& registry, float& speed, int& drops) {
    speed = -1.0f;
    drops = -1;
    if (held.empty()) return;
    const Item& item = *held.item;
    auto rule = [&](bool matches, float ruleSpeed, int ruleDrops) {
        if (!matches) return;
        if (speed < 0.0f && ruleSpeed >= 0.0f) speed = ruleSpeed;
        if (drops < 0 && ruleDrops >= 0) drops = ruleDrops;
    };
    switch (item.tool) {
    case ToolKind::Pickaxe:
    case ToolKind::Axe:
    case ToolKind::Shovel:
    case ToolKind::Hoe: {
        // deniesDrops(incorrect_for_<tier>_tool), then minesAndDrops(mineable/<kind>, speed).
        const bool incorrect = (item.toolLevel < 3 && inTag(registry, state, "needs_diamond_tool")) ||
                               (item.toolLevel < 2 && inTag(registry, state, "needs_iron_tool")) ||
                               (item.toolLevel < 1 && inTag(registry, state, "needs_stone_tool"));
        rule(incorrect, -1.0f, 0);
        const char* mineable = item.tool == ToolKind::Pickaxe ? "mineable/pickaxe"
                               : item.tool == ToolKind::Axe   ? "mineable/axe"
                               : item.tool == ToolKind::Shovel ? "mineable/shovel"
                                                               : "mineable/hoe";
        rule(inTag(registry, state, mineable), item.toolSpeed, 1);
        break;
    }
    case ToolKind::Sword:
        rule(isBlock(registry, state, "cobweb"), 15.0f, 1);
        rule(inTag(registry, state, "sword_instantly_mines"), std::numeric_limits<float>::max(), -1);
        rule(inTag(registry, state, "sword_efficient"), 1.5f, -1);
        break;
    case ToolKind::Shears:
        rule(isBlock(registry, state, "cobweb"), 15.0f, 1);
        rule(inTag(registry, state, "leaves"), 15.0f, -1);
        rule(inTag(registry, state, "wool"), 5.0f, -1);
        rule(isBlock(registry, state, "vine") || isBlock(registry, state, "glow_lichen"), 2.0f, -1);
        break;
    case ToolKind::None: break;
    }
}

} // namespace

float toolSpeed(const ItemStack& held, StateId state, const BlockRegistry& registry) {
    float speed;
    int drops;
    evaluate(held, state, registry, speed, drops);
    return speed >= 0.0f ? speed : 1.0f;
}

bool correctToolForDrops(const ItemStack& held, StateId state, const BlockRegistry& registry) {
    float speed;
    int drops;
    evaluate(held, state, registry, speed, drops);
    return drops == 1;
}

bool canHarvest(const ItemStack& held, StateId state, const BlockRegistry& registry) {
    return !registry.blockOf(state).requiresTool || correctToolForDrops(held, state, registry);
}

float destroyProgressPerTick(StateId state, const Player& player, const BlockRegistry& registry) {
    const Block& block = registry.blockOf(state);
    if (block.hardness < 0.0f) return 0.0f;
    if (block.hardness == 0.0f) return 1.0f;
    const ItemStack& held = player.inventory.selectedStack();
    float speed = toolSpeed(held, state, registry);
    if (player.inWater && player.waterHeight > player.eyeHeight()) speed *= 0.2f; // submerged_mining_speed
    if (!player.onGround) speed /= 5.0f;
    return speed / block.hardness / (canHarvest(held, state, registry) ? 30.0f : 100.0f);
}

int miningWear(const ItemStack& held, StateId state, const BlockRegistry& registry) {
    if (held.empty() || !held.damageable()) return 0;
    switch (held.item->tool) {
    case ToolKind::Shears: return inTag(registry, state, "fire") ? 0 : 1;
    case ToolKind::Sword: return registry.blockOf(state).hardness != 0.0f ? 2 : 0;
    case ToolKind::None: return 0;
    default: return registry.blockOf(state).hardness != 0.0f ? 1 : 0;
    }
}

} // namespace mc
