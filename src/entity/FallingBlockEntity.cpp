#include "entity/FallingBlockEntity.h"

#include <cmath>

#include "entity/ItemEntity.h"
#include "entity/Player.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockRules.h"
#include "world/level/Level.h"
#include "world/level/Raycast.h"

namespace mc {

FallingBlockEntity::FallingBlockEntity(const BlockRegistry& registry) : registry_(&registry) {
    width = height = 0.98f;
}

FallingBlockEntity::Kind FallingBlockEntity::kindOf(const Block& block) {
    const std::string_view id = shortId(block.id);
    if (id == "sand" || id == "red_sand" || id == "gravel" || id == "dragon_egg") return Kind::Plain;
    if (id == "suspicious_sand" || id == "suspicious_gravel") return Kind::Brushable;
    if (id.ends_with("_concrete_powder")) return Kind::ConcretePowder;
    if (id == "anvil" || id == "chipped_anvil" || id == "damaged_anvil") return Kind::Anvil;
    return Kind::None;
}

bool FallingBlockEntity::isFree(const BlockRegistry& registry, StateId state) {
    // Air, #fire, liquids, and replaceable blocks (every snow layer counts here).
    const StateInfo& info = registry.info(state);
    if (info.flags & (kAir | kReplaceable)) return true;
    const Block& b = registry.blockOf(state);
    return b.className == "liquid" || b.className == "fire" || b.className == "snow_layer";
}

bool FallingBlockEntity::touchesWater(const BlockGetter& level, const BlockPos& pos) {
    // Vanilla's loop reads the block at `pos` itself before the first (downward) step, so
    // water below counts only when the powder's own cell holds water too.
    const bool selfWet = level.fluid(pos).kind == FluidKind::Water;
    for (Direction d : kDirections) {
        if (d == Direction::Down && !selfWet) continue;
        const BlockPos n = pos.offset(d);
        if (level.fluid(n).kind == FluidKind::Water && !level.sturdy(n, opposite(d))) return true;
    }
    return false;
}

StateId FallingBlockEntity::concreteOf(const BlockRegistry& registry, StateId powder) {
    std::string id(registry.blockOf(powder).id);
    id.resize(id.size() - std::string_view("_powder").size());
    return registry.defaultState(id);
}

FallingBlockEntity& FallingBlockEntity::fall(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    auto e = std::make_unique<FallingBlockEntity>(reg);
    // Waterlogged blocks fall dry; the cell keeps the water.
    const bool wet = reg.boolValue(state, "waterlogged");
    if (wet) state = *reg.with(state, "waterlogged", "false");
    e->state = state;
    e->pos = e->oldPos = glm::dvec3(pos.x + 0.5, pos.y, pos.z + 0.5);
    e->startPos = pos;
    switch (kindOf(reg.blockOf(state))) {
    case Kind::Anvil: // AnvilBlock.falling
        e->hurtEntities = true;
        e->fallDamagePerDistance = 2.0f;
        e->fallDamageMax = 40;
        break;
    case Kind::Brushable: e->cancelDrop = true; break; // BrushableBlock: breaks wherever it lands
    default: break;
    }
    level.setBlockAndUpdate(pos, wet ? reg.defaultState("water") : BlockRegistry::kAirState);
    return static_cast<FallingBlockEntity&>(level.addEntity(std::move(e)));
}

void FallingBlockEntity::tick(Level& level) {
    const BlockRegistry& reg = level.registry();
    if (reg.info(state).flags & kAir) {
        removed = true;
        return;
    }
    ++tickCount;
    ++time;
    oldPos = pos;
    velocity.y -= 0.04; // gravity
    move(level, velocity);
    if (landedFall > 0.0) {
        // Vanilla checkFallDamage -> fallOn -> causeFallDamage (beds halve the distance).
        double fall = landedFall;
        landedFall = 0.0;
        if (reg.blockOf(level.getBlock(landedOn)).className == "bed") fall *= 0.5;
        causeFallDamage(level, fall);
    }
    BlockPos at = BlockPos::containing(pos);
    const Kind kind = kindOf(reg.blockOf(state));
    const bool powder = kind == Kind::ConcretePowder;
    bool inWater = powder && level.fluid(at).kind == FluidKind::Water;
    if (powder && glm::dot(velocity, velocity) > 1.0) {
        // Fast enough to skip a thin layer of water: look along this tick's path for a source.
        const glm::dvec3 path = pos - oldPos;
        const double length = glm::length(path);
        if (length > 0.0)
            if (auto hit = raycastBlocks(level, oldPos, path / length, length, RayFluid::SourceOnly, RayShape::Visual);
                hit && level.fluid(hit->pos).kind == FluidKind::Water) {
                at = hit->pos;
                inWater = true;
            }
    }
    if (!onGround && !inWater) {
        if ((time > 100 && (at.y <= kWorldMinY || at.y > kWorldMaxY)) || time > 600) {
            if (dropItem) spawnItem(level);
            removed = true;
        }
    } else {
        const StateId replaced = level.getBlock(at);
        velocity *= glm::dvec3(0.7, -0.5, 0.7);
        if (cancelDrop) {
            removed = true;
            breakAfterFall(level, at);
        } else {
            // Vanilla canBeReplaced with a downward placement and no item: replaceable blocks
            // (a single snow layer, not deeper snow).
            const bool replaceable = (reg.info(replaced).flags & (kAir | kReplaceable)) != 0;
            const bool belowFree = isFree(reg, level.getBlock(at.below())) && !(powder && inWater);
            const bool survives = reg.rules().canSurvive(level, state, at) && !belowFree;
            if (replaceable && survives) {
                land(level, at, replaced);
            } else {
                removed = true;
                if (dropItem) {
                    breakAfterFall(level, at);
                    spawnItem(level);
                }
            }
        }
    }
    velocity *= 0.98;
}

void FallingBlockEntity::land(Level& level, const BlockPos& at, StateId replaced) {
    const BlockRegistry& reg = level.registry();
    StateId placed = state;
    if (!reg.value(placed, "waterlogged").empty() && level.fluid(at).kind == FluidKind::Water && level.fluid(at).source())
        placed = *reg.with(placed, "waterlogged", "true");
    if (!level.setBlockAndUpdate(at, placed)) {
        removed = true;
        if (dropItem) {
            breakAfterFall(level, at);
            spawnItem(level);
        }
        return;
    }
    removed = true;
    // Fallable.onLand: concrete powder sets where water touches it (or where it replaced
    // water).
    if (kindOf(reg.blockOf(placed)) == Kind::ConcretePowder &&
        (reg.info(replaced).fluid.kind == FluidKind::Water || touchesWater(level, at)))
        level.setBlockAndUpdate(at, concreteOf(reg, placed));
}

void FallingBlockEntity::breakAfterFall(Level& level, const BlockPos& at) {
    // Brushable blocks crumble into particles (vanilla onBrokenAfterFall, levelEvent 2001).
    if (kindOf(level.registry().blockOf(state)) == Kind::Brushable)
        level.levelEvent(LevelEventType::BlockBroken, BlockPos::containing(box().center()), state);
    (void)at;
}

void FallingBlockEntity::spawnItem(Level& level) {
    // Vanilla spawnAtLocation: the block's item at the entity, with the usual toss.
    if (!level.doEntityDrops || !level.itemRegistry()) return;
    const Item* item = level.itemRegistry()->forBlock(level.registry().blockOf(state));
    if (!item) return;
    ItemEntity& drop = ItemEntity::spawn(level, pos, ItemStack{item, 1});
    drop.pickupDelay = 10;
}

void FallingBlockEntity::causeFallDamage(Level& level, double fallDistance) {
    if (!hurtEntities) return;
    const int distance = int(std::ceil(fallDistance - 1.0));
    if (distance < 0) return;
    const float damage = std::min(std::floor(float(distance) * fallDamagePerDistance), float(fallDamageMax));
    const DamageType type = kindOf(level.registry().blockOf(state)) == Kind::Anvil ? DamageType::Anvil : DamageType::FallingBlock;
    // Living entities in its box, but not creative or spectating players.
    if (Player* p = level.player(); p && !p->dead() && p->box().intersects(box()) &&
                                    p->mode() != GameMode::Creative && p->mode() != GameMode::Spectator)
        p->hurt(type, damage);
    // AnvilBlock.damage: the harder the fall, the likelier it chips (or breaks: no drop).
    if (kindOf(level.registry().blockOf(state)) == Kind::Anvil && damage > 0.0f &&
        level.random().nextFloat() < 0.05f + float(distance) * 0.05f) {
        const BlockRegistry& reg = level.registry();
        const std::string_view id = shortId(reg.blockOf(state).id);
        const char* next = id == "anvil" ? "chipped_anvil" : id == "chipped_anvil" ? "damaged_anvil" : nullptr;
        if (!next) {
            cancelDrop = true;
            return;
        }
        StateId chipped = reg.defaultState(next);
        if (auto f = reg.with(chipped, "facing", reg.value(state, "facing"))) chipped = *f;
        state = chipped;
    }
}

void FallingBlockEntity::saveExtra(Json& out) const {
    // Vanilla names: BlockState, Time, DropItem, HurtEntities, FallHurtAmount, FallHurtMax,
    // CancelDrop (as one state string).
    out["block_state"] = registry_->describe(state);
    out["time"] = time;
    out["drop_item"] = dropItem;
    out["hurt_entities"] = hurtEntities;
    out["fall_hurt_amount"] = fallDamagePerDistance;
    out["fall_hurt_max"] = fallDamageMax;
    out["cancel_drop"] = cancelDrop;
    out["start_pos"] = Json::array({startPos.x, startPos.y, startPos.z});
}

void FallingBlockEntity::loadExtra(const Json& in, const ItemRegistry&) {
    state = registry_->parse(in.value("block_state", std::string())).value_or(BlockRegistry::kAirState);
    time = in.value("time", 0);
    dropItem = in.value("drop_item", true);
    hurtEntities = in.value("hurt_entities", false);
    fallDamagePerDistance = in.value("fall_hurt_amount", 0.0f);
    fallDamageMax = in.value("fall_hurt_max", 40);
    cancelDrop = in.value("cancel_drop", false);
    if (const Json p = in.value("start_pos", Json::array()); p.is_array() && p.size() == 3)
        startPos = {p[0].get<int>(), p[1].get<int>(), p[2].get<int>()};
    else
        startPos = BlockPos::containing(pos);
}

} // namespace mc
