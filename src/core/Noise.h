// Noise.h -- vanilla-style gradient noise: ImprovedNoise (3D Perlin), octave
// sums (PerlinNoise), NormalNoise (two offset octave sums) and BlendedNoise (the
// old 3D terrain noise). Same algorithms as vanilla; seeds are derived with our
// own RNG, so values are not seed-compatible.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "core/Rng.h"

namespace mc {

class ImprovedNoise {
public:
    explicit ImprovedNoise(Xoroshiro& random);
    // Plain 3D Perlin noise in roughly [-1, 1].
    double noise(double x, double y, double z) const { return noise(x, y, z, 0.0, 0.0); }
    // Vanilla's y-quantized variant used by BlendedNoise.
    double noise(double x, double y, double z, double yScale, double yMax) const;

private:
    int p(int i) const { return perm_[size_t(i & 0xFF)]; }
    double sampleAndLerp(int gx, int gy, int gz, double dx, double weirdDy, double dz, double dy) const;
    std::array<uint8_t, 256> perm_{};
    double xo_, yo_, zo_;
};

// Octave sum: octave i has frequency 2^(firstOctave + i) and weight amplitude[i].
class PerlinNoise {
public:
    PerlinNoise() = default;
    PerlinNoise(const Xoroshiro& seed, int firstOctave, const std::vector<double>& amplitudes);
    double value(double x, double y, double z) const;
    // Octave by index from the highest frequency down (vanilla getOctaveNoise).
    const ImprovedNoise* octaveFromTop(int i) const;
    size_t octaveCount() const { return levels_.size(); }

    static double wrap(double v);

private:
    std::vector<std::unique_ptr<ImprovedNoise>> levels_; // lowest frequency first; null for zero amplitude
    std::vector<double> amplitudes_;
    double lowestFreqInput_ = 1.0;
    double lowestFreqValue_ = 1.0;
};

// Vanilla NormalNoise: two octave sums, the second sampled at a slightly scaled
// position, normalized to roughly unit deviation.
class NormalNoise {
public:
    NormalNoise() = default;
    NormalNoise(const Xoroshiro& seed, std::string_view name, int firstOctave, const std::vector<double>& amplitudes);
    double value(double x, double y, double z) const;

private:
    PerlinNoise first_, second_;
    double valueFactor_ = 1.0;
};

// Vanilla BlendedNoise ("base_3d_noise"): interpolates between two 16-octave limit
// noises by an 8-octave selector. Gives terrain its overhangs.
class BlendedNoise {
public:
    BlendedNoise() = default;
    BlendedNoise(const Xoroshiro& seed, double xzScale, double yScale, double xzFactor, double yFactor,
                 double smearScaleMultiplier);
    double value(int x, int y, int z) const;

private:
    PerlinNoise minLimit_, maxLimit_, main_;
    double xzMultiplier_ = 0, yMultiplier_ = 0, xzFactor_ = 1, yFactor_ = 1, smear_ = 1;
};

} // namespace mc
