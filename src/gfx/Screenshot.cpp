#include "gfx/Screenshot.h"

#include <ctime>
#include <format>

#include "core/Log.h"
#include "gfx/GL.h"

namespace mc {

Image captureFramebuffer(int width, int height) {
    Image image;
    if (width <= 0 || height <= 0) return image;
    image.width = width;
    image.height = height;
    image.pixels.resize(size_t(width) * size_t(height) * 4);

    GLint previousAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);

    // GL returns the bottom row first; flip to top-down and make it opaque.
    const size_t rowBytes = size_t(width) * 4;
    std::vector<uint8_t> scratch(rowBytes);
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = image.pixels.data() + size_t(y) * rowBytes;
        uint8_t* bottom = image.pixels.data() + size_t(height - 1 - y) * rowBytes;
        std::copy(top, top + rowBytes, scratch.begin());
        std::copy(bottom, bottom + rowBytes, top);
        std::copy(scratch.begin(), scratch.end(), bottom);
    }
    for (size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;
    return image;
}

bool saveScreenshot(const std::filesystem::path& path, int width, int height) {
    const Image image = captureFramebuffer(width, height);
    if (image.empty() || !image.savePng(path)) {
        log::error("Could not save screenshot to {}", path.string());
        return false;
    }
    log::info("Saved screenshot as {}", path.string());
    return true;
}

std::filesystem::path nextScreenshotPath(const std::filesystem::path& dir) {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    const std::string stem = std::format("{:04}-{:02}-{:02}_{:02}.{:02}.{:02}", tm.tm_year + 1900, tm.tm_mon + 1,
                                         tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    std::filesystem::path p = dir / (stem + ".png");
    for (int i = 1; std::filesystem::exists(p); ++i) p = dir / std::format("{}_{}.png", stem, i);
    return p;
}

} // namespace mc
