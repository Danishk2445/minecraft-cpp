// Timer.h -- monotonic stopwatch.
#pragma once

#include <chrono>

namespace mc {

class Stopwatch {
public:
    Stopwatch() : start_(clock::now()) {}
    void reset() { start_ = clock::now(); }
    double seconds() const { return std::chrono::duration<double>(clock::now() - start_).count(); }
    double millis() const { return seconds() * 1000.0; }

private:
    using clock = std::chrono::steady_clock;
    clock::time_point start_;
};

} // namespace mc
