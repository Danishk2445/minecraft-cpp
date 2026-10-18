// Benchmark.h -- --bench-fly: once the view around the start is meshed, the camera flies
// a fixed path east (weaving, looking around) for a set time while every frame's time is
// recorded; then the frame rate (average, 1% and 0.1% lows), frame time percentiles,
// memory and chunk counts are reported.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Timer.h"

namespace mc {

class WorldSession;

struct BenchmarkResult {
    int frames = 0;
    double seconds = 0.0;
    double avgFps = 0.0, low1Fps = 0.0, low01Fps = 0.0; // lows: the slowest 1% / 0.1% of frames
    double p50Ms = 0.0, p99Ms = 0.0, maxMs = 0.0;
    double rssMb = 0.0, peakRssMb = 0.0;
    double gpuMb = 0.0;
    int sections = 0, drawnSections = 0;
    double drawnVertices = 0.0; // terrain vertices drawn per frame, on average
    bool warmedUp = true; // the start view was meshed before the clock started
    std::string json() const;
};

// The resident and peak memory of this process (Linux /proc/self/status), in MB.
double residentMb();
double peakResidentMb();

// Frame statistics from frame times in seconds.
BenchmarkResult frameStats(std::vector<double> frameSeconds);

class FlyBenchmark {
public:
    FlyBenchmark(double seconds, std::filesystem::path out) : seconds_(seconds), out_(std::move(out)) {}
    // Once a frame after the world's frame(): warms up, then moves the camera along the
    // path and times the frames. Returns true when the run is over.
    bool frame(WorldSession& world, double dt);
    const BenchmarkResult& result() const { return result_; }

    // The path at `t` seconds from `start` (the eyes): position and yaw/pitch.
    static glm::dvec3 position(const glm::dvec3& start, double t);
    static glm::vec2 rotation(double t);
    static constexpr double kSpeed = 20.0;      // blocks per second (about creative sprint-flying)
    static constexpr double kWarmupLimit = 120; // seconds to wait for the start view

private:
    void finish(WorldSession& world);

    double seconds_;
    std::filesystem::path out_;
    enum class Phase { Warmup, Flying, Done } phase_ = Phase::Warmup;
    Stopwatch clock_;
    std::optional<glm::dvec3> start_;
    std::vector<double> frames_;
    double vertices_ = 0.0; // summed over the timed frames
    bool firstFlyingFrame_ = true;
    BenchmarkResult result_;
};

} // namespace mc
