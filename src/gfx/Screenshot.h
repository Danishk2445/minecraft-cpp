// Screenshot.h -- read back the default framebuffer and save it as PNG.
#pragma once

#include <filesystem>

#include "core/Image.h"

namespace mc {

// Reads the bound read framebuffer's color buffer into a top-down RGBA image
// with alpha forced to 255.
Image captureFramebuffer(int width, int height);

bool saveScreenshot(const std::filesystem::path& path, int width, int height);

// <dir>/YYYY-MM-DD_HH.MM.SS.png, with a _N suffix if the name is taken.
std::filesystem::path nextScreenshotPath(const std::filesystem::path& dir);

} // namespace mc
