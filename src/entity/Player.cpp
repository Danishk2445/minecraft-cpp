#include "entity/Player.h"

#include <cmath>

#include "entity/Physics.h"

namespace mc {
namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr double kGravity = 0.08;
constexpr float kSneakSpeed = 0.3f;      // vanilla sneaking_speed attribute
constexpr float kSprintSpeedBoost = 1.3f; // vanilla sprinting modifier (+30%)

} // namespace

std::string_view gameModeName(GameMode m) {
    switch (m) {
    case GameMode::Survival: return "survival";
    case GameMode::Creative: return "creative";
    case GameMode::Adventure: return "adventure";
    case GameMode::Spectator: return "spectator";
    }
    return "survival";
}

std::optional<GameMode> gameModeFromName(std::string_view name) {
    for (GameMode m : {GameMode::Survival, GameMode::Creative, GameMode::Adventure, GameMode::Spectator})
        if (gameModeName(m) == name) return m;
    return std::nullopt;
}

Player::Player() {
    width = 0.6f;
    height = 1.8f;
    stepHeight = 0.6f;
}

void Player::setMode(GameMode m) {
    mode_ = m;
    noPhysics = m == GameMode::Spectator;
    if (m == GameMode::Spectator) flying = true;
    else if (m != GameMode::Creative) flying = false;
}

bool Player::onClimbable(const BlockGetter& level) const {
    if (mode_ == GameMode::Spectator) return false;
    static thread_local const BlockRegistry* cached = nullptr;
    static thread_local TagId climbable = 0;
    if (cached != &level.registry()) {
        cached = &level.registry();
        climbable = level.registry().tag("climbable").value_or(TagId(0));
    }
    return level.hasTag(BlockPos(int(std::floor(pos.x)), int(std::floor(pos.y)), int(std::floor(pos.z))), climbable);
}

void Player::moveRelative(float speed, const glm::vec3& input) {
    const float lenSq = glm::dot(input, input);
    if (lenSq < 1.0e-7f) return;
    const glm::vec3 v = (lenSq > 1.0f ? glm::normalize(input) : input) * speed;
    const float s = std::sin(yaw * kDegToRad), c = std::cos(yaw * kDegToRad);
    velocity += glm::dvec3(v.x * c - v.z * s, v.y, v.z * c + v.x * s);
}

// Vanilla Player.maybeBackOffFromEdge: sneaking never walks off a ledge taller than a step.
glm::dvec3 Player::backOffFromEdge(const BlockGetter& level, glm::dvec3 delta) const {
    if (flying || delta.y > 0.0 || !crouching) return delta;
    const AABB b = box();
    auto canFall = [&](double dx, double dz, double distance) {
        return noCollision(level, AABB(b.min.x + 1e-7 + dx, b.min.y - distance - 1e-7, b.min.z + 1e-7 + dz,
                                       b.max.x - 1e-7 + dx, b.min.y, b.max.z - 1e-7 + dz));
    };
    const double step = stepHeight;
    const bool aboveGround = onGround || (fallDistance < step && !canFall(0.0, 0.0, step - fallDistance));
    if (!aboveGround) return delta;
    double dx = delta.x, dz = delta.z;
    const double sx = dx > 0 ? 0.05 : dx < 0 ? -0.05 : 0.0, sz = dz > 0 ? 0.05 : dz < 0 ? -0.05 : 0.0;
    while (dx != 0.0 && canFall(dx, 0.0, step)) {
        if (std::abs(dx) <= 0.05) {
            dx = 0.0;
            break;
        }
        dx -= sx;
    }
    while (dz != 0.0 && canFall(0.0, dz, step)) {
        if (std::abs(dz) <= 0.05) {
            dz = 0.0;
            break;
        }
        dz -= sz;
    }
    while (dx != 0.0 && dz != 0.0 && canFall(dx, dz, step)) {
        dx = std::abs(dx) <= 0.05 ? 0.0 : dx - sx;
        dz = std::abs(dz) <= 0.05 ? 0.0 : dz - sz;
    }
    return {dx, delta.y, dz};
}

void Player::tick(const BlockGetter& level, const PlayerInput& in) {
    oldPos = pos;
    oldYaw = yaw;
    oldPitch = pitch;
    beginLivingTick();
    xBobO = xBob;
    yBobO = yBob;
    xBob += (pitch - xBob) * 0.5f;
    yBob += (yaw - yBob) * 0.5f;
    const bool mayFly = mode_ == GameMode::Creative || mode_ == GameMode::Spectator;
    if (mode_ == GameMode::Spectator) flying = true;
    else if (in.doubleTapJump && mayFly) flying = !flying;

    updateFluidState(level);
    crouching = in.sneak && !flying && mode_ != GameMode::Spectator;
    float forward = in.forward, strafe = in.strafe;
    if (crouching) forward *= kSneakSpeed, strafe *= kSneakSpeed;
    // Sprint: started by the key while moving forward, stopped by slowing, sneaking or bumping.
    if (in.sprint && forward >= 0.8f && !crouching) sprinting = true;
    if (sprinting && (forward < 0.8f || crouching || (horizontalCollision && !flying))) sprinting = false;

    if (flying) {
        const int dir = (in.jump ? 1 : 0) - (in.sneak ? 1 : 0);
        if (dir != 0) velocity.y += double(dir) * flyingSpeed * 3.0;
    }

    // LivingEntity.aiStep (serverAiStep first: the swing advances and the head follows the look).
    updateSwingTime();
    headYaw = yaw;
    if (jumpDelay_ > 0) --jumpDelay_;
    for (int a = 0; a < 3; ++a)
        if (std::abs(velocity[a]) < 0.003) velocity[a] = 0.0;
    const bool jumping = in.jump && !flying;
    if (jumping) {
        constexpr double kFluidJumpThreshold = 0.4;
        if (inWater && (!onGround || waterHeight > kFluidJumpThreshold)) {
            velocity.y += 0.04;
        } else if (inLava && (!onGround || lavaHeight > kFluidJumpThreshold)) {
            velocity.y += 0.04;
        } else if ((onGround || (inWater && waterHeight <= kFluidJumpThreshold)) && jumpDelay_ == 0) {
            const BlockRegistry& reg = level.registry();
            float factor = reg.blockOf(level.getBlock(BlockPos(int(std::floor(pos.x)), int(std::floor(pos.y)),
                                                             int(std::floor(pos.z)))))
                               .jumpFactor;
            if (factor == 1.0f) factor = reg.blockOf(level.getBlock(blockBelow())).jumpFactor;
            velocity.y = std::max(0.42 * factor, velocity.y);
            if (sprinting) {
                const float r = yaw * kDegToRad;
                velocity.x += -std::sin(r) * 0.2;
                velocity.z += std::cos(r) * 0.2;
            }
            jumpDelay_ = 10;
        }
    } else {
        jumpDelay_ = 0;
    }
    travel(level, glm::vec3(strafe * 0.98f, 0.0f, forward * 0.98f), jumping);
    updateWalkAnimation();
    // Landing ends creative flight (vanilla checks after moving, so taking off works).
    if (flying && onGround && mode_ != GameMode::Spectator) flying = false;
    turnBody();
}

void Player::travel(const BlockGetter& level, glm::vec3 input, bool jumping) {
    const double flyingVertical = velocity.y;
    const float speed = walkSpeed * (sprinting ? kSprintSpeedBoost : 1.0f);
    const float airSpeed = flying ? flyingSpeed * (sprinting ? 2.0f : 1.0f) : (sprinting ? 0.025999999f : 0.02f);
    auto isFree = [&](const glm::dvec3& d) {
        const AABB b = box().moved(d);
        if (!noCollision(level, b)) return false;
        for (int y = int(std::floor(b.min.y)); y <= int(std::floor(b.max.y)); ++y)
            for (int z = int(std::floor(b.min.z)); z <= int(std::floor(b.max.z)); ++z)
                for (int x = int(std::floor(b.min.x)); x <= int(std::floor(b.max.x)); ++x)
                    if (!level.fluid({x, y, z}).empty()) return false;
        return true;
    };
    // Vanilla getFluidFallingAdjustedMovement: slow sinking in fluids.
    auto fluidFalling = [&](double gravity, bool falling, double vy) {
        if (gravity == 0.0 || sprinting) return vy;
        if (falling && std::abs(vy - 0.005) >= 0.003 && std::abs(vy - gravity / 16.0) < 0.003) return -0.003;
        return vy - gravity / 16.0;
    };

    if (inWater && !flying) {
        const double startY = pos.y;
        const float drag = sprinting ? 0.9f : 0.8f;
        moveRelative(0.02f, input);
        move(level, backOffFromEdge(level, velocity));
        glm::dvec3 v = velocity;
        if (horizontalCollision && onClimbable(level)) v.y = 0.2;
        v *= glm::dvec3(drag, 0.8, drag);
        v.y = fluidFalling(kGravity, v.y <= 0.0, v.y);
        velocity = v;
        if (horizontalCollision && isFree({velocity.x, velocity.y + 0.6 - pos.y + startY, velocity.z})) velocity.y = 0.3;
        return;
    }
    if (inLava && !flying) {
        const double startY = pos.y;
        moveRelative(0.02f, input);
        move(level, backOffFromEdge(level, velocity));
        if (lavaHeight <= 0.4) {
            velocity *= glm::dvec3(0.5, 0.8, 0.5);
            velocity.y = fluidFalling(kGravity, velocity.y <= 0.0, velocity.y);
        } else {
            velocity *= 0.5;
        }
        velocity.y -= kGravity / 4.0;
        if (horizontalCollision && isFree({velocity.x, velocity.y + 0.6 - pos.y + startY, velocity.z})) velocity.y = 0.3;
        return;
    }

    const float blockFriction = level.registry().blockOf(level.getBlock(blockBelow())).friction;
    const float friction = onGround ? blockFriction * 0.91f : 0.91f;
    moveRelative(onGround ? speed * (0.21600002f / (blockFriction * blockFriction * blockFriction)) : airSpeed, input);
    const bool climbing = !flying && onClimbable(level);
    if (climbing) {
        fallDistance = 0.0f;
        velocity.x = std::clamp(velocity.x, -0.15, 0.15);
        velocity.z = std::clamp(velocity.z, -0.15, 0.15);
        velocity.y = std::max(velocity.y, -0.15);
        if (velocity.y < 0.0 && crouching) velocity.y = 0.0;
    }
    move(level, backOffFromEdge(level, velocity));
    glm::dvec3 v = velocity;
    if ((horizontalCollision || jumping) && climbing) v.y = 0.2;
    v.y -= kGravity;
    velocity = glm::dvec3(v.x * friction, v.y * 0.98, v.z * friction);
    if (flying) {
        velocity.y = flyingVertical * 0.6;
        fallDistance = 0.0f;
    }
}

} // namespace mc
