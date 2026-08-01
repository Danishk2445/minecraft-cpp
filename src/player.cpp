#include "player.h"
#include "world.h"
#include <algorithm>
#include <cmath>

glm::vec3 Player::front() const {
    float cy = std::cos(glm::radians(yaw)), sy = std::sin(glm::radians(yaw));
    float cp = std::cos(glm::radians(pitch)), sp = std::sin(glm::radians(pitch));
    return glm::normalize(glm::vec3(cy * cp, sp, sy * cp));
}

glm::vec3 Player::frontFlat() const {
    return glm::vec3(std::cos(glm::radians(yaw)), 0, std::sin(glm::radians(yaw)));
}

bool Player::intersectsBlock(const glm::ivec3& c) const {
    return c.x + 1 > pos.x - HALF_W && c.x < pos.x + HALF_W &&
           c.y + 1 > pos.y && c.y < pos.y + HEIGHT &&
           c.z + 1 > pos.z - HALF_W && c.z < pos.z + HALF_W;
}

void Player::moveAxis(World& w, int axis, float amount) {
    const float EPS = 1e-4f;
    // Substep to avoid tunneling at high speeds.
    int steps = std::max(1, (int)std::ceil(std::abs(amount) / 0.4f));
    float step = amount / steps;
    for (int s = 0; s < steps; s++) {
        pos[axis] += step;
        glm::vec3 mn(pos.x - HALF_W, pos.y, pos.z - HALF_W);
        glm::vec3 mx(pos.x + HALF_W, pos.y + HEIGHT, pos.z + HALF_W);
        int x0 = (int)std::floor(mn.x), x1 = (int)std::floor(mx.x - EPS);
        int y0 = (int)std::floor(mn.y), y1 = (int)std::floor(mx.y - EPS);
        int z0 = (int)std::floor(mn.z), z1 = (int)std::floor(mx.z - EPS);
        bool hit = false;
        for (int y = y0; y <= y1 && !hit; y++)
            for (int z = z0; z <= z1 && !hit; z++)
                for (int x = x0; x <= x1 && !hit; x++) {
                    if (!blockSolid(w.block(x, y, z))) continue;
                    hit = true;
                    if (axis == 0) pos.x = step > 0 ? x - HALF_W - EPS : x + 1 + HALF_W + EPS;
                    if (axis == 1) {
                        if (step > 0) pos.y = y - HEIGHT - EPS;
                        else { pos.y = (float)(y + 1); onGround = true; }
                    }
                    if (axis == 2) pos.z = step > 0 ? z - HALF_W - EPS : z + 1 + HALF_W + EPS;
                    vel[axis] = 0;
                }
        if (hit) break;
    }
}

void Player::update(World& w, const PlayerInput& in, float dt) {
    // Freeze physics until the chunk under the player exists.
    Chunk* c = w.chunk((int)std::floor(pos.x) >> 4, (int)std::floor(pos.z) >> 4);
    if (!c || !c->generated) return;

    uint8_t feetBlock = w.block((int)std::floor(pos.x), (int)std::floor(pos.y + 0.4f), (int)std::floor(pos.z));
    uint8_t eyeBlock = w.block((int)std::floor(pos.x), (int)std::floor(pos.y + EYE), (int)std::floor(pos.z));
    inWater = g_blocks[feetBlock].fluid;
    eyeInWater = g_blocks[eyeBlock].fluid;

    glm::vec3 f = frontFlat();
    glm::vec3 r = glm::vec3(-f.z, 0, f.x);
    glm::vec3 wish(0.0f);
    if (in.forward) wish += f;
    if (in.back) wish -= f;
    if (in.right) wish += r;
    if (in.left) wish -= r;
    if (glm::dot(wish, wish) > 0) wish = glm::normalize(wish);

    if (flying) {
        float speed = in.sprint ? 22.0f : 11.0f;
        glm::vec3 target = wish * speed;
        target.y = (in.jump ? speed : 0.0f) + (in.down ? -speed : 0.0f);
        float k = 1.0f - std::exp(-10.0f * dt);
        vel += (target - vel) * k;
    } else {
        float speed = 4.317f * (in.sprint ? 1.3f : 1.0f);
        if (inWater) speed *= 0.55f;
        float k = 1.0f - std::exp(-(onGround ? 16.0f : 3.5f) * dt);
        vel.x += (wish.x * speed - vel.x) * k;
        vel.z += (wish.z * speed - vel.z) * k;

        if (inWater) {
            vel.y -= 14.0f * dt;
            vel.y = std::max(vel.y, -6.0f);
            if (in.jump) vel.y += 40.0f * dt;
            vel.y = std::min(vel.y, 5.0f);
        } else {
            vel.y -= 32.0f * dt;
            vel.y = std::max(vel.y, -78.0f);
            if (in.jump && onGround) { vel.y = 8.6f; onGround = false; }
        }
    }

    onGround = false;
    moveAxis(w, 1, vel.y * dt);
    moveAxis(w, 0, vel.x * dt);
    moveAxis(w, 2, vel.z * dt);

    // Fell into the void: respawn above terrain.
    if (pos.y < -12.0f) {
        pos = glm::vec3(pos.x, (float)(w.terrainHeight((int)pos.x, (int)pos.z) + 3), pos.z);
        vel = glm::vec3(0);
    }
}
