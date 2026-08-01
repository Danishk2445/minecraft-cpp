#pragma once
#include <glm/glm.hpp>

class World;

struct PlayerInput {
    bool forward = false, back = false, left = false, right = false;
    bool jump = false, down = false, sprint = false;
};

class Player {
public:
    glm::vec3 pos{8.0f, 80.0f, 8.0f}; // feet center
    glm::vec3 vel{0.0f};
    float yaw = -90.0f, pitch = 0.0f;
    bool onGround = false;
    bool flying = false;
    bool inWater = false;
    bool eyeInWater = false;
    float health = 20.0f;
    double lastDamage = -100.0;
    float hurtFlash = 0.0f;

    static constexpr float HALF_W = 0.3f;
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE = 1.62f;

    glm::vec3 eyePos() const { return pos + glm::vec3(0, EYE, 0); }
    glm::vec3 front() const;      // full look direction
    glm::vec3 frontFlat() const;  // horizontal movement basis

    void update(World& world, const PlayerInput& in, float dt);
    bool intersectsBlock(const glm::ivec3& cell) const;

private:
    void moveAxis(World& world, int axis, float amount);
};
