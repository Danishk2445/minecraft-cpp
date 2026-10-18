// Controls.h -- how a mob's AI moves its body (vanilla MoveControl, LookControl,
// JumpControl, BodyRotationControl) and what it can see (Sensing): goals and the
// navigation set wishes; the controls turn them into input each tick.
#pragma once

#include <cstdint>
#include <unordered_set>

#include <glm/glm.hpp>

namespace mc {

class Entity;
class Level;
class Mob;

// Vanilla Mth.rotateIfNecessary / LookControl.rotateTowards / MoveControl.rotlerp.
float rotateTowards(float from, float to, float maxDelta);
float rotateIfNecessary(float current, float target, float max);
float rotlerpWrapped(float from, float to, float max); // MoveControl.rotlerp: kept in [0, 360]

class MoveControl {
public:
    enum class Op : uint8_t { Wait, MoveTo, Strafe, Jumping };
    explicit MoveControl(Mob& mob) : mob_(mob) {}
    void setWantedPosition(const glm::dvec3& p, double speed) {
        wanted_ = p;
        speedModifier_ = speed;
        if (op_ != Op::Jumping) op_ = Op::MoveTo;
    }
    void strafe(float forwards, float right) {
        op_ = Op::Strafe;
        strafeForwards_ = forwards;
        strafeRight_ = right;
        speedModifier_ = 0.25;
    }
    bool hasWanted() const { return op_ == Op::MoveTo; }
    double speedModifier() const { return speedModifier_; }
    const glm::dvec3& wanted() const { return wanted_; }
    void tick(Level& level);

private:
    Mob& mob_;
    glm::dvec3 wanted_{0.0};
    double speedModifier_ = 0.0;
    float strafeForwards_ = 0.0f, strafeRight_ = 0.0f;
    Op op_ = Op::Wait;
};

class LookControl {
public:
    explicit LookControl(Mob& mob) : mob_(mob) {}
    // Turns the head toward a point at up to these degrees per tick, for 2 ticks.
    void setLookAt(const glm::dvec3& p, float yawSpeed, float pitchSpeed);
    void setLookAt(const glm::dvec3& p); // the mob's own head speeds
    void setLookAt(const Entity& e, float yawSpeed, float pitchSpeed);
    bool looking() const { return cooldown_ > 0; }
    void tick();

private:
    Mob& mob_;
    glm::dvec3 wanted_{0.0};
    float yawSpeed_ = 0.0f, pitchSpeed_ = 0.0f;
    int cooldown_ = 0;
};

class JumpControl {
public:
    explicit JumpControl(Mob& mob) : mob_(mob) {}
    void jump() { jump_ = true; }
    void tick();

private:
    Mob& mob_;
    bool jump_ = false;
};

class BodyRotationControl {
public:
    explicit BodyRotationControl(Mob& mob) : mob_(mob) {}
    void tick();

private:
    Mob& mob_;
    int headStableTime_ = 0;
    float lastStableHeadYaw_ = 0.0f;
};

// Vanilla Sensing: line of sight to other entities, cached for one tick.
class Sensing {
public:
    explicit Sensing(Mob& mob) : mob_(mob) {}
    void tick() {
        seen_.clear();
        unseen_.clear();
    }
    bool hasLineOfSight(const Level& level, const Entity& e);

private:
    Mob& mob_;
    std::unordered_set<uint32_t> seen_, unseen_;
};

} // namespace mc
