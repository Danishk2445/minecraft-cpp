// Entity.h -- position, motion and vanilla Entity.move against blocks.
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include <glm/glm.hpp>

#include "core/Json.h"
#include "core/Math.h"
#include "world/block/BlockGetter.h"

namespace mc {

class ItemRegistry;
class Level;

class Entity {
public:
    virtual ~Entity() = default;

    // Level entities: an id unique in the level, and a flag set when discarded (the level
    // drops it after the tick). The type names it in saves ("minecraft:item"); entities
    // the level doesn't own (the player) have none.
    uint32_t id = 0;
    bool removed = false;
    int tickCount = 0; // ticks lived
    virtual std::string_view type() const { return {}; }
    // One game tick (only while its chunk is ticking).
    virtual void tick(Level& level) { (void)level; }
    // Type-specific save data; position, motion and rotation are saved for every entity.
    virtual void saveExtra(Json& out) const { (void)out; }
    virtual void loadExtra(const Json& in, const ItemRegistry& items) { (void)in, (void)items; }
    // A copy of its current state (for client-side effects such as the pickup animation).
    virtual std::unique_ptr<Entity> clone() const { return nullptr; }

    glm::dvec3 pos{0.0};      // feet (bottom center of the bounding box)
    glm::dvec3 oldPos{0.0};   // last tick, for interpolation
    glm::dvec3 velocity{0.0}; // blocks per tick
    float yaw = 0.0f, pitch = 0.0f, oldYaw = 0.0f, oldPitch = 0.0f; // degrees; yaw 0 faces +z
    float width = 0.6f, height = 1.8f;
    float stepHeight = 0.0f;
    bool noPhysics = false;

    bool onGround = false;
    // The last landing's fall distance and the block landed on (0 when consumed).
    double landedFall = 0.0;
    BlockPos landedOn{0, 0, 0};
    bool horizontalCollision = false, verticalCollision = false, verticalCollisionBelow = false;
    double fallDistance = 0.0; // a double since vanilla 1.21.5: long falls sum exactly
    // Fluid contact, refreshed by updateFluidState().
    bool inWater = false, inLava = false;
    double waterHeight = 0.0, lavaHeight = 0.0; // how deep the box reaches into each fluid

    // Unit vector of the look direction (vanilla calculateViewVector).
    glm::dvec3 viewVector() const;
    AABB box() const;
    AABB boxAt(const glm::dvec3& feet) const;
    // Vanilla Entity.move(SELF): collide, step, set collision flags, zero blocked
    // velocity components and apply the speed factor of the block underneath.
    void move(const BlockGetter& level, glm::dvec3 delta);
    // Vanilla updateInWaterStateAndDoFluidPushing (without pushing): fluid heights.
    void updateFluidState(const BlockGetter& level);
    // The block that decides friction/speed: just below the feet (vanilla 0.500001 down).
    BlockPos blockBelow() const;
    // Vanilla stuck-in-block slowdown (cobweb, berry bushes, powder snow), consumed by move().
    void makeStuckInBlock(const glm::dvec3& multiplier) { stuck_ = multiplier; }
    // Vanilla moveTowardsClosestSpace: when inside a solid block, a push toward the nearest
    // open neighbor (up when tied), for items and orbs stuck in walls.
    void moveTowardsClosestSpace(const BlockGetter& level, float random01);
    // Whether the box overlaps any block's collision shape.
    bool insideBlocks(const BlockGetter& level) const;

protected:
    glm::dvec3 stuck_{0.0};
};

} // namespace mc
