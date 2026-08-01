#pragma once
#include <glm/glm.hpp>

struct Frustum {
    glm::vec4 planes[6];

    void fromMatrix(const glm::mat4& m) {
        // Gribb-Hartmann plane extraction (rows of the transposed matrix).
        glm::mat4 t = glm::transpose(m);
        planes[0] = t[3] + t[0];
        planes[1] = t[3] - t[0];
        planes[2] = t[3] + t[1];
        planes[3] = t[3] - t[1];
        planes[4] = t[3] + t[2];
        planes[5] = t[3] - t[2];
    }

    bool aabbVisible(const glm::vec3& mn, const glm::vec3& mx) const {
        for (int i = 0; i < 6; i++) {
            const glm::vec4& p = planes[i];
            glm::vec3 v(p.x > 0 ? mx.x : mn.x, p.y > 0 ? mx.y : mn.y, p.z > 0 ? mx.z : mn.z);
            if (glm::dot(glm::vec3(p), v) + p.w < 0) return false;
        }
        return true;
    }
};
