#include "app/Benchmark.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <numeric>

#include "app/WorldSession.h"
#include "core/Log.h"
#include "render/WorldRenderer.h"

namespace mc {
namespace {

// A "VmRSS:   123456 kB" line of /proc/self/status, in MB.
double procStatusMb(std::string_view key) {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line))
        if (line.starts_with(key)) return std::strtod(line.c_str() + key.size(), nullptr) / 1024.0;
    return 0.0;
}

} // namespace

double residentMb() { return procStatusMb("VmRSS:"); }
double peakResidentMb() { return procStatusMb("VmHWM:"); }

BenchmarkResult frameStats(std::vector<double> frameSeconds) {
    BenchmarkResult r;
    if (frameSeconds.empty()) return r;
    r.frames = int(frameSeconds.size());
    r.seconds = std::accumulate(frameSeconds.begin(), frameSeconds.end(), 0.0);
    r.avgFps = double(r.frames) / std::max(r.seconds, 1e-9);
    std::sort(frameSeconds.begin(), frameSeconds.end());
    const auto percentile = [&](double p) {
        return frameSeconds[std::min(frameSeconds.size() - 1, size_t(p * double(frameSeconds.size())))] * 1000.0;
    };
    r.p50Ms = percentile(0.5);
    r.p99Ms = percentile(0.99);
    r.maxMs = frameSeconds.back() * 1000.0;
    // The "1% low": the frame rate over the slowest 1% of frames.
    const auto low = [&](double fraction) {
        const size_t n = std::max<size_t>(1, size_t(std::ceil(fraction * double(frameSeconds.size()))));
        const double sum = std::accumulate(frameSeconds.end() - std::ptrdiff_t(n), frameSeconds.end(), 0.0);
        return double(n) / std::max(sum, 1e-9);
    };
    r.low1Fps = low(0.01);
    r.low01Fps = low(0.001);
    return r;
}

std::string BenchmarkResult::json() const {
    return std::format("{{\"frames\": {}, \"seconds\": {:.3f}, \"avg_fps\": {:.1f}, \"low1_fps\": {:.1f}, "
                       "\"low01_fps\": {:.1f}, \"p50_ms\": {:.3f}, \"p99_ms\": {:.3f}, \"max_ms\": {:.3f}, "
                       "\"rss_mb\": {:.1f}, \"peak_rss_mb\": {:.1f}, \"gpu_mb\": {:.1f}, \"sections\": {}, "
                       "\"drawn_sections\": {}, \"drawn_vertices\": {:.0f}, \"warmed_up\": {}}}",
                       frames, seconds, avgFps, low1Fps, low01Fps, p50Ms, p99Ms, maxMs, rssMb, peakRssMb, gpuMb,
                       sections, drawnSections, drawnVertices, warmedUp ? "true" : "false");
}

glm::dvec3 FlyBenchmark::position(const glm::dvec3& start, double t) {
    // East at kSpeed, weaving 24 blocks either side every 30 s.
    return start + glm::dvec3(kSpeed * t, 0.0, 24.0 * std::sin(t * 2.0 * 3.141592653589793 / 30.0));
}

glm::vec2 FlyBenchmark::rotation(double t) {
    // Facing east (yaw -90), looking 40 degrees either way every 20 s, and down 10-25 degrees.
    const double tau = 2.0 * 3.141592653589793;
    return {float(-90.0 + 40.0 * std::sin(t * tau / 20.0)), float(17.5 + 7.5 * std::sin(t * tau / 13.0))};
}

bool FlyBenchmark::frame(WorldSession& world, double dt) {
    switch (phase_) {
    case Phase::Warmup: {
        if (!start_) {
            start_ = world.camera().position;
            clock_.reset();
            log::info("Benchmark: waiting for the view around ({:.0f}, {:.0f}, {:.0f})", start_->x, start_->y, start_->z);
        }
        const glm::vec2 rot = rotation(0.0);
        world.setPose(*start_, rot.x, rot.y);
        const bool meshed = world.viewMeshed();
        if (!meshed && clock_.seconds() < kWarmupLimit) return false;
        result_.warmedUp = meshed;
        log::info("Benchmark: view {} after {:.1f} s; flying for {:.0f} s", meshed ? "meshed" : "NOT meshed",
                  clock_.seconds(), seconds_);
        phase_ = Phase::Flying;
        frames_.reserve(1 << 16);
        clock_.reset();
        return false;
    }
    case Phase::Flying: {
        // `dt` is the time of the frame before: the first one here was the warm-up's last.
        if (!firstFlyingFrame_) {
            frames_.push_back(dt);
            vertices_ += double(world.worldRenderer().chunkRenderer().stats().vertices);
        }
        firstFlyingFrame_ = false;
        const double t = clock_.seconds();
        if (t >= seconds_) {
            finish(world);
            return true;
        }
        const glm::vec2 rot = rotation(t);
        world.setPose(position(*start_, t), rot.x, rot.y);
        return false;
    }
    case Phase::Done: return true;
    }
    return true;
}

void FlyBenchmark::finish(WorldSession& world) {
    phase_ = Phase::Done;
    const bool warmedUp = result_.warmedUp;
    result_ = frameStats(frames_);
    result_.warmedUp = warmedUp;
    result_.rssMb = residentMb();
    result_.peakRssMb = peakResidentMb();
    const ChunkRenderer& chunks = world.worldRenderer().chunkRenderer();
    result_.gpuMb = double(chunks.gpuBytes()) / (1024.0 * 1024.0);
    result_.sections = int(chunks.sectionCount());
    result_.drawnSections = chunks.stats().drawn;
    result_.drawnVertices = vertices_ / std::max<double>(1.0, double(frames_.size()));
    const BenchmarkResult& r = result_;
    log::info("Benchmark: {} frames in {:.1f} s: {:.1f} fps average, 1% low {:.1f}, 0.1% low {:.1f}", r.frames,
              r.seconds, r.avgFps, r.low1Fps, r.low01Fps);
    log::info("Benchmark: frame time p50 {:.2f} ms, p99 {:.2f} ms, max {:.2f} ms", r.p50Ms, r.p99Ms, r.maxMs);
    log::info("Benchmark: RSS {:.0f} MB (peak {:.0f} MB), chunk meshes {:.0f} MB on the GPU, {} sections ({} drawn at "
              "the end), {:.2f} M terrain vertices drawn per frame",
              r.rssMb, r.peakRssMb, r.gpuMb, r.sections, r.drawnSections, r.drawnVertices / 1e6);
    if (!out_.empty()) {
        std::ofstream f(out_);
        f << r.json() << "\n";
        if (!f) log::warn("Benchmark: couldn't write {}", out_.string());
    }
}

} // namespace mc
