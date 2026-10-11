// Camera.h -- vanilla-convention camera (yaw 0 faces +z, positive pitch looks down).
#pragma once

#include <glm/glm.hpp>

namespace mc {

struct Camera {
    glm::dvec3 position{0.0, 80.0, 0.0};
    float yaw = 0.0f;   // degrees
    float pitch = 0.0f; // degrees, clamped to [-90, 90]
    float fov = 70.0f;  // vertical, degrees
    float nearPlane = 0.05f;
    float farPlane = 1024.0f;

    glm::vec3 forward() const;
    glm::vec3 right() const; // horizontal, perpendicular to forward
    // Rotation only: geometry is drawn relative to the camera position.
    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;
};

} // namespace mc
