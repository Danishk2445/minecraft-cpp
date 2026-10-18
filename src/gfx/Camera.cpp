#include "gfx/Camera.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace mc {

glm::vec3 Camera::forward() const {
    const float y = glm::radians(yaw), p = glm::radians(pitch);
    return {-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
}

glm::vec3 Camera::right() const {
    const float y = glm::radians(yaw);
    // Facing south (+z), +x (east) is to the left, so right is -x.
    return {-std::cos(y), 0.0f, -std::sin(y)};
}

glm::mat4 Camera::view() const {
    // Vanilla: ZP.rotationDegrees(roll) * bobView (on the projection side) * XP.rotationDegrees(pitch)
    // * YP.rotationDegrees(yaw + 180).
    glm::mat4 v = glm::rotate(glm::mat4(1.0f), glm::radians(roll), glm::vec3(0, 0, 1)) * bob;
    v = glm::rotate(v, glm::radians(pitch), glm::vec3(1, 0, 0));
    return glm::rotate(v, glm::radians(yaw + 180.0f), glm::vec3(0, 1, 0));
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

} // namespace mc
