#pragma once
#include <cmath>
#include <cstdint>

// Deterministic hash / value-noise helpers used by world generation.

inline uint32_t hashU32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline uint32_t hash2i(int x, int y, uint32_t seed) {
    return hashU32((uint32_t)x * 0x8da6b343U ^ (uint32_t)y * 0xd8163841U ^ seed * 0xcb1ab31fU);
}

inline uint32_t hash3i(int x, int y, int z, uint32_t seed) {
    return hashU32((uint32_t)x * 0x8da6b343U ^ (uint32_t)y * 0xd8163841U ^ (uint32_t)z * 0xa3d169d1U ^ seed * 0xcb1ab31fU);
}

inline float hash2f(int x, int y, uint32_t seed) { return (hash2i(x, y, seed) & 0xffffff) / 16777216.0f; }
inline float hash3f(int x, int y, int z, uint32_t seed) { return (hash3i(x, y, z, seed) & 0xffffff) / 16777216.0f; }

inline float smoothf(float t) { return t * t * (3.0f - 2.0f * t); }

inline float valueNoise2(uint32_t seed, float x, float y) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    float u = smoothf(xf), v = smoothf(yf);
    float a = hash2f(xi, yi, seed),     b = hash2f(xi + 1, yi, seed);
    float c = hash2f(xi, yi + 1, seed), d = hash2f(xi + 1, yi + 1, seed);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

inline float valueNoise3(uint32_t seed, float x, float y, float z) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    float xf = x - xi, yf = y - yi, zf = z - zi;
    float u = smoothf(xf), v = smoothf(yf), w = smoothf(zf);
    float c000 = hash3f(xi, yi, zi, seed),         c100 = hash3f(xi + 1, yi, zi, seed);
    float c010 = hash3f(xi, yi + 1, zi, seed),     c110 = hash3f(xi + 1, yi + 1, zi, seed);
    float c001 = hash3f(xi, yi, zi + 1, seed),     c101 = hash3f(xi + 1, yi, zi + 1, seed);
    float c011 = hash3f(xi, yi + 1, zi + 1, seed), c111 = hash3f(xi + 1, yi + 1, zi + 1, seed);
    float x00 = c000 + (c100 - c000) * u;
    float x10 = c010 + (c110 - c010) * u;
    float x01 = c001 + (c101 - c001) * u;
    float x11 = c011 + (c111 - c011) * u;
    float y0 = x00 + (x10 - x00) * v;
    float y1 = x01 + (x11 - x01) * v;
    return y0 + (y1 - y0) * w;
}

inline float fbm2(uint32_t seed, float x, float y, int octaves) {
    float sum = 0, amp = 0.5f, tot = 0;
    for (int i = 0; i < octaves; i++) {
        sum += valueNoise2(seed + i * 101, x, y) * amp;
        tot += amp;
        amp *= 0.5f;
        x *= 2.0f; y *= 2.0f;
    }
    return sum / tot;
}

inline float fbm3(uint32_t seed, float x, float y, float z, int octaves) {
    float sum = 0, amp = 0.5f, tot = 0;
    for (int i = 0; i < octaves; i++) {
        sum += valueNoise3(seed + i * 101, x, y, z) * amp;
        tot += amp;
        amp *= 0.5f;
        x *= 2.0f; y *= 2.0f; z *= 2.0f;
    }
    return sum / tot;
}
