#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

class World;

enum MobType { MOB_PIG, MOB_COW, MOB_ZOMBIE, MOB_CREEPER, MOB_TYPE_COUNT };

struct Mob {
    int type = MOB_PIG;
    glm::vec3 pos{0}, vel{0};
    float yaw = 0;                    // body facing, degrees (same convention as player)
    float headYaw = 0, headPitch = 0; // relative to body
    float legPhase = 0, legAmp = 0;
    float health = 10;
    float hurtTime = 0, attackCooldown = 0, burnAccum = 0;
    int state = 0; // 0 idle, 1 wander, 2 chase/flee
    float aiTimer = 0;
    glm::vec3 target{0};
    float fuse = 0; // creeper
    bool onGround = false;
    bool dead = false;
    float deathTime = 0;
    bool frozen = false; // --mobtest lineup: hold pose for screenshots
};

class Mobs {
public:
    void init(const std::string& texDir);
    void update(World& w, glm::vec3 playerPos, float dt, float sunF, bool naturalSpawns);
    void render(const glm::mat4& vp, const glm::vec3& eye, World& w, float sunF,
                const glm::vec3& fog, float fogStart, float fogEnd);
    // Ray vs mob AABBs; returns index and distance of the nearest hit.
    bool raycast(glm::vec3 o, glm::vec3 d, float maxDist, int& idx, float& t) const;
    void hurt(int idx, float dmg, glm::vec3 fromDir);
    void spawnAt(int type, glm::vec3 pos);

    // main provides these: applies damage + knockback to the player
    std::function<void(float dmg, glm::vec3 knock)> damagePlayer;
    std::function<void(glm::vec3 center, float radius)> explosionAtPlayer;

    std::vector<Mob> mobs;

private:
    void updateOne(World& w, Mob& m, glm::vec3 playerPos, float dt, float sunF);
    void explode(World& w, glm::vec3 center, float radius, int sourceIdx);
    void trySpawn(World& w, glm::vec3 playerPos, float sunF);
    float spawnTimer_ = 0;
};

// AABB size per mob type
void mobSize(int type, float& halfW, float& height);
