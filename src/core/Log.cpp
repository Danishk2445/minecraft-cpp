#include "core/Log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace mc::log {
namespace {

std::atomic<Level> g_level{Level::Info};
std::mutex g_mutex;
std::ofstream g_file;
thread_local std::string t_threadName;

const char* levelName(Level l) {
    switch (l) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warn: return "WARN";
    case Level::Error: return "ERROR";
    }
    return "?";
}

} // namespace

void setLevel(Level level) { g_level.store(level, std::memory_order_relaxed); }
Level level() { return g_level.load(std::memory_order_relaxed); }

void setFile(const std::string& path) {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
    if (!path.empty()) g_file.open(path, std::ios::out | std::ios::trunc);
}

void setThreadName(std::string name) { t_threadName = std::move(name); }

void write(Level level, std::string_view message) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t secs = system_clock::to_time_t(now);
    const int millis = int(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
    localtime_r(&secs, &tm);

    const std::string_view thread = t_threadName.empty() ? std::string_view("main") : t_threadName;
    const std::string line = std::format("[{:02}:{:02}:{:02}.{:03}] [{}/{}] {}\n", tm.tm_hour, tm.tm_min,
                                         tm.tm_sec, millis, thread, levelName(level), message);

    std::lock_guard lock(g_mutex);
    std::fwrite(line.data(), 1, line.size(), stderr);
    if (g_file.is_open()) {
        g_file << line;
        if (level >= Level::Warn) g_file.flush();
    }
}

} // namespace mc::log
