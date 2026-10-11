// Rng.h -- deterministic random sources. Never use std::*_distribution: their
// output is implementation-defined, so worlds would differ between compilers.
#pragma once

#include <cstdint>
#include <string_view>

namespace mc {

// SplitMix64 / Stafford variant 13, as used by vanilla to expand seeds.
constexpr uint64_t mixStafford13(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Vanilla Mth.getSeed(x, y, z): the per-position seed for random model variants
// and plant offsets. x*3129871 deliberately wraps in 32 bits, as in Java.
constexpr int64_t positionSeed(int x, int y, int z) {
    const uint64_t a = uint64_t(int64_t(int32_t(uint32_t(x) * 3129871u)));
    const uint64_t b = uint64_t(int64_t(z)) * 116129781ull;
    uint64_t l = a ^ b ^ uint64_t(int64_t(y));
    l = l * l * 42317861ull + l * 11ull;
    return int64_t(l) >> 16;
}

// 64-bit FNV-1a, for deriving named sub-seeds ("ore_coal", "trees", ...).
constexpr uint64_t hashString(std::string_view s) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (char c : s) h = (h ^ uint8_t(c)) * 0x100000001B3ull;
    return h;
}

// java.util.Random: a 48-bit LCG. Used where vanilla uses it (random model variants).
class JavaRandom {
public:
    explicit JavaRandom(int64_t seed = 0) { setSeed(seed); }
    void setSeed(int64_t seed) { state_ = (uint64_t(seed) ^ kMul) & kMask; }

    int32_t next(int bits) {
        state_ = (state_ * kMul + 0xB) & kMask;
        return int32_t(int64_t(state_) >> (48 - bits));
    }
    int32_t nextInt() { return next(32); }
    int32_t nextInt(int32_t bound) {
        if ((bound & -bound) == bound) return int32_t((int64_t(bound) * int64_t(next(31))) >> 31);
        int32_t bits, val;
        do {
            bits = next(31);
            val = bits % bound;
        } while (int64_t(bits) - val + (bound - 1) > INT32_MAX); // Java's int overflow test
        return val;
    }
    int64_t nextLong() { return int64_t(uint64_t(int64_t(next(32))) << 32) + next(32); }
    bool nextBool() { return next(1) != 0; }
    float nextFloat() { return float(next(24)) / float(1 << 24); }
    double nextDouble() { return double((int64_t(next(26)) << 27) + next(27)) * 0x1.0p-53; }

private:
    static constexpr uint64_t kMul = 0x5DEECE66Dull;
    static constexpr uint64_t kMask = (1ull << 48) - 1;
    uint64_t state_ = 0;
};

// Xoroshiro128++ seeded like vanilla's XoroshiroRandomSource. The general-purpose
// source for world generation and gameplay.
class Xoroshiro {
public:
    explicit Xoroshiro(int64_t seed = 0) { setSeed(seed); }
    Xoroshiro(uint64_t lo, uint64_t hi) : lo_(lo), hi_(hi) {
        if ((lo_ | hi_) == 0) {
            lo_ = 0x9E3779B97F4A7C15ull;
            hi_ = 0x6A09E667F3BCC909ull;
        }
    }

    void setSeed(int64_t seed) {
        const uint64_t l = uint64_t(seed) ^ 0x6A09E667F3BCC909ull;
        const uint64_t h = l + 0x9E3779B97F4A7C15ull;
        *this = Xoroshiro(mixStafford13(l), mixStafford13(h));
    }

    uint64_t nextU64() {
        const uint64_t l = lo_;
        uint64_t h = hi_;
        const uint64_t out = rotl(l + h, 17) + l;
        h ^= l;
        lo_ = rotl(l, 49) ^ h ^ (h << 21);
        hi_ = rotl(h, 28);
        return out;
    }
    int64_t nextLong() { return int64_t(nextU64()); }
    int32_t nextInt() { return int32_t(nextU64()); }

    // Lemire's nearly-divisionless bounded integer, as in vanilla.
    int32_t nextInt(int32_t bound) {
        uint64_t l = uint32_t(nextInt());
        uint64_t m = l * uint64_t(uint32_t(bound));
        uint64_t low = m & 0xFFFFFFFFull;
        if (low < uint64_t(uint32_t(bound))) {
            const uint32_t threshold = uint32_t(-bound) % uint32_t(bound);
            while (low < threshold) {
                l = uint32_t(nextInt());
                m = l * uint64_t(uint32_t(bound));
                low = m & 0xFFFFFFFFull;
            }
        }
        return int32_t(m >> 32);
    }
    // Uniform in [lo, hi] inclusive.
    int32_t nextIntBetween(int32_t lo, int32_t hi) { return lo + nextInt(hi - lo + 1); }
    bool nextBool() { return (nextU64() >> 63) != 0; }
    float nextFloat() { return float(nextU64() >> 40) * 0x1.0p-24f; }
    double nextDouble() { return double(nextU64() >> 11) * 0x1.0p-53; }
    // Approximately normal (Box-Muller would need logs; this is the vanilla
    // MarsagliaPolarGaussian), mean 0 and stddev 1.
    double nextGaussian();

    // Independent child streams derived from this generator's state.
    Xoroshiro fork() { return Xoroshiro(nextU64(), nextU64()); }
    // Stateless derivations: same inputs always give the same stream.
    Xoroshiro at(int x, int y, int z) const {
        return Xoroshiro(mixStafford13(lo_ ^ uint64_t(positionSeed(x, y, z))), hi_);
    }
    Xoroshiro named(std::string_view name) const {
        const uint64_t h = hashString(name);
        return Xoroshiro(mixStafford13(lo_ ^ h), mixStafford13(hi_ ^ (h * 0x9E3779B97F4A7C15ull)));
    }

    uint64_t lo() const { return lo_; }
    uint64_t hi() const { return hi_; }

private:
    static constexpr uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t lo_ = 0, hi_ = 0;
    bool haveGaussian_ = false;
    double nextGaussianValue_ = 0;
};

} // namespace mc
