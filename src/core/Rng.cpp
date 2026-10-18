#include "core/Rng.h"

#include <cmath>

namespace mc {

double JavaRandom::nextGaussian() {
    if (haveGaussian_) {
        haveGaussian_ = false;
        return nextGaussianValue_;
    }
    double v1, v2, s;
    do {
        v1 = 2.0 * nextDouble() - 1.0;
        v2 = 2.0 * nextDouble() - 1.0;
        s = v1 * v1 + v2 * v2;
    } while (s >= 1.0 || s == 0.0);
    const double mul = std::sqrt(-2.0 * std::log(s) / s);
    nextGaussianValue_ = v2 * mul;
    haveGaussian_ = true;
    return v1 * mul;
}

double Xoroshiro::nextGaussian() {
    if (haveGaussian_) {
        haveGaussian_ = false;
        return nextGaussianValue_;
    }
    double v1, v2, s;
    do {
        v1 = 2.0 * nextDouble() - 1.0;
        v2 = 2.0 * nextDouble() - 1.0;
        s = v1 * v1 + v2 * v2;
    } while (s >= 1.0 || s == 0.0);
    const double mul = std::sqrt(-2.0 * std::log(s) / s);
    nextGaussianValue_ = v2 * mul;
    haveGaussian_ = true;
    return v1 * mul;
}

} // namespace mc
