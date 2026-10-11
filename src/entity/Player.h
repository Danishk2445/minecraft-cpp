// Player.h -- the local player's movement (vanilla LocalPlayer/Player/LivingEntity
// aiStep and travel): walking, sprinting, sneaking with edge guard, jumping,
// ladders, swimming, lava, creative flight and spectator noclip.
#pragma once

#include "entity/LivingEntity.h"
#include "game/Inventory.h"

namespace mc {

enum class GameMode : uint8_t { Survival, Creative, Adventure, Spectator };
std::string_view gameModeName(GameMode m);
std::optional<GameMode> gameModeFromName(std::string_view name);

struct PlayerInput {
    float forward = 0.0f; // -1..1 (W/S)
    float strafe = 0.0f;  // -1..1 (A/D), positive = left like vanilla xxa
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    bool doubleTapJump = false; // toggles creative flight
};

class Player : public LivingEntity {
public:
    Player();

    GameMode mode() const { return mode_; }
    void setMode(GameMode m);
    bool flying = false;
    bool sprinting = false;
    bool crouching = false;
    float walkSpeed = 0.1f;   // movement_speed attribute
    float flyingSpeed = 0.05f;
    Inventory inventory;

    // One game tick: input, movement and collision, then the body and limb animation.
    void tick(const BlockGetter& level, const PlayerInput& input);
    float eyeHeight() const { return crouching ? 1.27f : 1.62f; }
    glm::dvec3 eyePos() const { return pos + glm::dvec3(0.0, eyeHeight(), 0.0); }
    bool onClimbable(const BlockGetter& level) const;

private:
    void travel(const BlockGetter& level, glm::vec3 input, bool jumping);
    void moveRelative(float speed, const glm::vec3& input);
    glm::dvec3 backOffFromEdge(const BlockGetter& level, glm::dvec3 delta) const;

    GameMode mode_ = GameMode::Survival;
    int jumpDelay_ = 0;
};

} // namespace mc
