#include "core/Noise.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>

namespace mc {
namespace {

constexpr int kGradient[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                  {1, 0, -1}, {-1, 0, -1}, {0, 1, 1},  {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
                                  {1, 1, 0},  {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};

inline double gradDot(int g, double x, double y, double z) {
    const int* v = kGradient[g & 15];
    return v[0] * x + v[1] * y + v[2] * z;
}

inline double smoothstep(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
inline double lerp(double t, double a, double b) { return a + t * (b - a); }

} // namespace

ImprovedNoise::ImprovedNoise(Xoroshiro& random) {
    xo_ = random.nextDouble() * 256.0;
    yo_ = random.nextDouble() * 256.0;
    zo_ = random.nextDouble() * 256.0;
    for (int i = 0; i < 256; ++i) perm_[size_t(i)] = uint8_t(i);
    for (int i = 0; i < 256; ++i) {
        const int j = random.nextInt(256 - i);
        std::swap(perm_[size_t(i)], perm_[size_t(i + j)]);
    }
}

double ImprovedNoise::noise(double x, double y, double z, double yScale, double yMax) const {
    const double d = x + xo_, e = y + yo_, f = z + zo_;
    const int i = int(std::floor(d)), j = int(std::floor(e)), k = int(std::floor(f));
    const double g = d - i, h = e - j, l = f - k;
    double o = 0.0;
    if (yScale != 0.0) {
        const double m = (yMax >= 0.0 && yMax < h) ? yMax : h;
        o = std::floor(m / yScale + 1.0e-7) * yScale;
    }
    return sampleAndLerp(i, j, k, g, h - o, l, h);
}

double ImprovedNoise::sampleAndLerp(int gx, int gy, int gz, double dx, double wdy, double dz, double dy) const {
    const int i = p(gx), j = p(gx + 1);
    const int k = p(i + gy), l = p(i + gy + 1);
    const int m = p(j + gy), n = p(j + gy + 1);
    const double a = gradDot(p(k + gz), dx, wdy, dz);
    const double b = gradDot(p(m + gz), dx - 1.0, wdy, dz);
    const double c = gradDot(p(l + gz), dx, wdy - 1.0, dz);
    const double d = gradDot(p(n + gz), dx - 1.0, wdy - 1.0, dz);
    const double e = gradDot(p(k + gz + 1), dx, wdy, dz - 1.0);
    const double f = gradDot(p(m + gz + 1), dx - 1.0, wdy, dz - 1.0);
    const double g = gradDot(p(l + gz + 1), dx, wdy - 1.0, dz - 1.0);
    const double h = gradDot(p(n + gz + 1), dx - 1.0, wdy - 1.0, dz - 1.0);
    const double r = smoothstep(dx), s = smoothstep(dy), t = smoothstep(dz);
    return lerp(t, lerp(s, lerp(r, a, b), lerp(r, c, d)), lerp(s, lerp(r, e, f), lerp(r, g, h)));
}

double PerlinNoise::wrap(double v) {
    constexpr double kRange = 3.3554432e7; // 2^25: keeps coordinates where doubles stay precise
    return v - std::floor(v / kRange + 0.5) * kRange;
}

PerlinNoise::PerlinNoise(const Xoroshiro& seed, int firstOctave, const std::vector<double>& amplitudes)
    : amplitudes_(amplitudes) {
    const int n = int(amplitudes.size());
    for (int i = 0; i < n; ++i) {
        if (amplitudes[size_t(i)] == 0.0) {
            levels_.push_back(nullptr);
            continue;
        }
        Xoroshiro r = seed.named(std::format("octave_{}", firstOctave + i));
        levels_.push_back(std::make_unique<ImprovedNoise>(r));
    }
    lowestFreqInput_ = std::pow(2.0, firstOctave);
    lowestFreqValue_ = std::pow(2.0, n - 1) / (std::pow(2.0, n) - 1.0);
}

double PerlinNoise::value(double x, double y, double z) const {
    double sum = 0.0, in = lowestFreqInput_, val = lowestFreqValue_;
    for (size_t i = 0; i < levels_.size(); ++i) {
        if (levels_[i]) sum += amplitudes_[i] * val * levels_[i]->noise(wrap(x * in), wrap(y * in), wrap(z * in));
        in *= 2.0;
        val /= 2.0;
    }
    return sum;
}

const ImprovedNoise* PerlinNoise::octaveFromTop(int i) const {
    const int idx = int(levels_.size()) - 1 - i;
    return idx >= 0 ? levels_[size_t(idx)].get() : nullptr;
}

NormalNoise::NormalNoise(const Xoroshiro& seed, std::string_view name, int firstOctave,
                         const std::vector<double>& amplitudes) {
    const Xoroshiro base = seed.named(name);
    first_ = PerlinNoise(base.named("first"), firstOctave, amplitudes);
    second_ = PerlinNoise(base.named("second"), firstOctave, amplitudes);
    int lo = INT_MAX, hi = INT_MIN;
    for (int i = 0; i < int(amplitudes.size()); ++i)
        if (amplitudes[size_t(i)] != 0.0) {
            lo = std::min(lo, i);
            hi = std::max(hi, i);
        }
    const int span = hi - lo;
    const double expectedDeviation = 0.1 * (1.0 + 1.0 / double(span + 1));
    valueFactor_ = (1.0 / 6.0) / expectedDeviation;
}

double NormalNoise::value(double x, double y, double z) const {
    constexpr double k = 1.0181268882175227;
    return (first_.value(x, y, z) + second_.value(x * k, y * k, z * k)) * valueFactor_;
}

BlendedNoise::BlendedNoise(const Xoroshiro& seed, double xzScale, double yScale, double xzFactor, double yFactor,
                           double smear) {
    std::vector<double> sixteen(16, 1.0), eight(8, 1.0);
    minLimit_ = PerlinNoise(seed.named("min_limit"), -15, sixteen);
    maxLimit_ = PerlinNoise(seed.named("max_limit"), -15, sixteen);
    main_ = PerlinNoise(seed.named("main"), -7, eight);
    xzMultiplier_ = 684.412 * xzScale;
    yMultiplier_ = 684.412 * yScale;
    xzFactor_ = xzFactor;
    yFactor_ = yFactor;
    smear_ = smear;
}

double BlendedNoise::value(int bx, int by, int bz) const {
    const double d = bx * xzMultiplier_, e = by * yMultiplier_, f = bz * xzMultiplier_;
    const double g = d / xzFactor_, h = e / yFactor_, i = f / xzFactor_;
    const double j = yMultiplier_ * smear_;
    const double k = j / yFactor_;
    double l = 0.0, m = 0.0, n = 0.0, o = 1.0;
    for (int p = 0; p < 8; ++p) {
        if (const ImprovedNoise* in = main_.octaveFromTop(p))
            n += in->noise(PerlinNoise::wrap(g * o), PerlinNoise::wrap(h * o), PerlinNoise::wrap(i * o), k * o, h * o) / o;
        o /= 2.0;
    }
    const double q = (n / 10.0 + 1.0) / 2.0;
    const bool onlyMax = q >= 1.0, onlyMin = q <= 0.0;
    o = 1.0;
    for (int r = 0; r < 16; ++r) {
        const double s = PerlinNoise::wrap(d * o), t = PerlinNoise::wrap(e * o), u = PerlinNoise::wrap(f * o);
        const double v = j * o;
        if (!onlyMax)
            if (const ImprovedNoise* in = minLimit_.octaveFromTop(r)) l += in->noise(s, t, u, v, e * o) / o;
        if (!onlyMin)
            if (const ImprovedNoise* in = maxLimit_.octaveFromTop(r)) m += in->noise(s, t, u, v, e * o) / o;
        o /= 2.0;
    }
    const double a = l / 512.0, b = m / 512.0;
    const double clampedT = std::clamp(q, 0.0, 1.0);
    return (a + clampedT * (b - a)) / 128.0;
}

} // namespace mc
