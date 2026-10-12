// Panorama.h -- the title screen's background (vanilla PanoramaRenderer and
// CubeMap): six views of a generated world around one point, spun slowly.
// The pack's own panorama is a 1x1 placeholder, so the game bakes its own.
#pragma once

#include <array>
#include <filesystem>

#include <glm/glm.hpp>

#include "gfx/Shader.h"
#include "gfx/Texture.h"

namespace mc {

struct Camera;

class Panorama {
public:
    static constexpr int kFaces = 6;
    // Face k's view: yaw offset and pitch (vanilla grabPanoramixScreenshot order).
    static glm::vec2 faceRotation(int face);
    // The camera that renders face k (90 degrees, square), for baking.
    static Camera faceCamera(int face, const glm::dvec3& eye, float baseYaw);

    ~Panorama();
    // panorama_0..5.png from `dir`; false if any is missing.
    bool load(const std::filesystem::path& dir);
    bool loaded() const { return loaded_; }
    // Turned `spin` degrees to the right, looking `pitch` down, with an 85-degree view
    // (vanilla CubeMap.render).
    void render(glm::ivec2 viewport, float spin, float pitch);

private:
    bool initGl();
    Shader shader_;
    unsigned vao_ = 0, vbo_ = 0;
    std::array<Texture, kFaces> faces_;
    bool loaded_ = false;
};

} // namespace mc
