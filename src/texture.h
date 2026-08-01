#pragma once
#include <GL/glew.h>
#include <cstdint>
#include <string>
#include <vector>

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // RGBA8
};

Image loadPNG(const std::string& path);
void savePNG(const std::string& path, int w, int h, const uint8_t* rgbaBottomUp);

// Multiply RGB by a color (0-255 per channel); used to bake biome tints.
Image tintImage(Image img, int r, int g, int b);
// Alpha-composite src over dst (same size).
void compositeOver(Image& dst, const Image& src);

GLuint loadTexture2D(const std::string& path, int* outW = nullptr, int* outH = nullptr,
                     GLenum wrap = GL_CLAMP_TO_EDGE);

// 16x16 block textures stacked into a GL_TEXTURE_2D_ARRAY.
// Animated strips (e.g. water_still.png) occupy one layer whose contents are
// re-uploaded as time advances.
class BlockAtlas {
public:
    std::string blockDir; // .../textures/block

    Image load(const char* name) const; // loads blockDir/name.png
    uint16_t add(Image img);            // crops to top-left 16x16 if larger
    uint16_t addFile(const char* name) { return add(load(name)); }
    uint16_t addAnimated(Image strip, float secondsPerFrame);

    void upload();
    void bind(int unit) const;
    void update(double timeSeconds); // advance animations

private:
    struct Anim {
        uint16_t layer;
        std::vector<Image> frames;
        float secondsPerFrame;
        int current = -1;
    };
    std::vector<Image> layers_;
    std::vector<Anim> anims_;
    GLuint tex_ = 0;
};
