// Fs.h -- whole-file reads and atomic writes.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mc::fs {

std::optional<std::string> readText(const std::filesystem::path& path);
std::optional<std::vector<uint8_t>> readBytes(const std::filesystem::path& path);

// Writes to "<path>.tmp" and renames it over `path`, creating parent directories.
// A crash mid-write leaves the old file intact.
bool writeAtomic(const std::filesystem::path& path, std::span<const uint8_t> data);
bool writeTextAtomic(const std::filesystem::path& path, std::string_view text);

} // namespace mc::fs
