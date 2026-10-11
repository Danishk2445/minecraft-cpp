// Frustum.h -- view-frustum planes extracted from a view-projection matrix.
#pragma once

#include <array>

#include <glm/glm.hpp>

namespace mc {

struct Frustum {
    std::array<glm::vec4, 6> planes{}; // ax + by + cz + d >= 0 inside

    static Frustum fromMatrix(const glm::mat4& m) {
        Frustum f;
        const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
        const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
        const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
        const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
        f.planes = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
        for (auto& p : f.planes) p /= glm::length(glm::vec3(p));
        return f;
    }

    // True if the box may be visible (conservative).
    bool intersects(const glm::vec3& lo, const glm::vec3& hi) const {
        for (const auto& p : planes) {
            const glm::vec3 v(p.x >= 0 ? hi.x : lo.x, p.y >= 0 ? hi.y : lo.y, p.z >= 0 ? hi.z : lo.z);
            if (glm::dot(glm::vec3(p), v) + p.w < 0) return false;
        }
        return true;
    }
};

} // namespace mc
