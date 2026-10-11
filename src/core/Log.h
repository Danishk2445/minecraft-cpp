// Log.h -- leveled, thread-safe logging to stderr (and optionally a file).
#pragma once

#include <format>
#include <string>
#include <string_view>

namespace mc::log {

enum class Level { Debug = 0, Info, Warn, Error };

void setLevel(Level level);
Level level();

// Mirrors every line into `path` (truncated on open). Empty path closes the file.
void setFile(const std::string& path);

// Name shown for the calling thread, e.g. "main" or "worker-3".
void setThreadName(std::string name);

void write(Level level, std::string_view message);

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Debug) write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Info) write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Warn) write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace mc::log
