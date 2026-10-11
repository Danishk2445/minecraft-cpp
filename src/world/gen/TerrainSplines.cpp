#include "world/gen/TerrainSplines.h"

#include <algorithm>

namespace mc {

using In = CubicSpline::Input;
using Ptr = CubicSpline::Ptr;
using B = CubicSpline::Builder;

CubicSpline::Ptr CubicSpline::constant(float v) {
    auto s = std::make_shared<CubicSpline>();
    s->constant_ = true;
    s->value_ = v;
    return s;
}

CubicSpline::Builder& CubicSpline::Builder::add(float location, float value, float derivative) {
    return add(location, CubicSpline::constant(value), derivative);
}

CubicSpline::Builder& CubicSpline::Builder::add(float location, Ptr value, float derivative) {
    locations_.push_back(location);
    values_.push_back(std::move(value));
    derivatives_.push_back(derivative);
    return *this;
}

CubicSpline::Ptr CubicSpline::Builder::build() const {
    auto s = std::make_shared<CubicSpline>();
    s->constant_ = false;
    s->input_ = input_;
    s->locations_ = locations_;
    s->values_ = values_;
    s->derivatives_ = derivatives_;
    return s;
}

float CubicSpline::eval(const SplinePoint& p) const {
    if (constant_) return value_;
    float f = 0;
    switch (input_) {
    case Input::Continents: f = p.continents; break;
    case Input::Erosion: f = p.erosion; break;
    case Input::Weirdness: f = p.weirdness; break;
    case Input::Ridges: f = p.ridges; break;
    }
    // Last location <= f, or -1.
    const int n = int(locations_.size());
    int i = int(std::upper_bound(locations_.begin(), locations_.end(), f) - locations_.begin()) - 1;
    auto linearExtend = [&](int idx) {
        const float v = values_[size_t(idx)]->eval(p);
        const float d = derivatives_[size_t(idx)];
        return d == 0.0f ? v : v + d * (f - locations_[size_t(idx)]);
    };
    if (i < 0) return linearExtend(0);
    if (i == n - 1) return linearExtend(n - 1);
    const float g = locations_[size_t(i)], h = locations_[size_t(i + 1)];
    const float k = (f - g) / (h - g);
    const float l = derivatives_[size_t(i)], m = derivatives_[size_t(i + 1)];
    const float a = values_[size_t(i)]->eval(p), b = values_[size_t(i + 1)]->eval(p);
    const float pp = l * (h - g) - (b - a);
    const float q = -m * (h - g) + (b - a);
    return a + k * (b - a) + k * (1.0f - k) * (pp + k * (q - pp));
}

namespace {

float lerpf(float t, float a, float b) { return a + t * (b - a); }
float slope(float a, float b, float x0, float x1) { return (b - a) / (x1 - x0); }

float mountainContinentalness(float f, float g, float h) {
    const float k = 1.0f - (1.0f - g) * 0.5f;
    const float l = 0.5f * (1.0f - g);
    const float m = (f + 1.17f) * 0.46082947f;
    const float n = m * k - l;
    return f < h ? std::max(n, -0.2222f) : std::max(n, 0.0f);
}

float mountainRidgeZeroContinentalnessPoint(float f) {
    const float i = 1.0f - (1.0f - f) * 0.5f;
    const float j = 0.5f * (1.0f - f);
    return j / (0.46082947f * i) - 1.17f;
}

Ptr mountainRidgeSpline(float f, bool bl) {
    B b(In::Ridges);
    const float i = mountainContinentalness(-1.0f, f, -0.7f);
    const float k = mountainContinentalness(1.0f, f, -0.7f);
    const float l = mountainRidgeZeroContinentalnessPoint(f);
    if (-0.65f < l && l < 1.0f) {
        const float n = mountainContinentalness(-0.65f, f, -0.7f);
        const float p = mountainContinentalness(-0.75f, f, -0.7f);
        const float q = slope(i, p, -1.0f, -0.75f);
        b.add(-1.0f, i, q);
        b.add(-0.75f, p);
        b.add(-0.65f, n);
        const float r = mountainContinentalness(l, f, -0.7f);
        const float s = slope(r, k, l, 1.0f);
        b.add(l - 0.01f, r);
        b.add(l, r, s);
        b.add(1.0f, k, s);
    } else {
        const float n = slope(i, k, -1.0f, 1.0f);
        if (bl) {
            b.add(-1.0f, std::max(0.2f, i));
            b.add(0.0f, lerpf(0.5f, i, k), n);
        } else {
            b.add(-1.0f, i, n);
        }
        b.add(1.0f, k, n);
    }
    return b.build();
}

Ptr ridgeSpline(float f, float g, float h, float i, float j, float k) {
    const float l = std::max(0.5f * (g - f), k);
    const float m = 5.0f * (h - g);
    return B(In::Ridges)
        .add(-1.0f, f, l)
        .add(-0.4f, g, std::min(l, m))
        .add(0.0f, h, m)
        .add(0.4f, i, 2.0f * (i - h))
        .add(1.0f, j, 0.7f * (j - i))
        .build();
}

Ptr erosionOffsetSpline(float f, float g, float h, float i, float j, float k, bool bl, bool bl2) {
    const Ptr s1 = mountainRidgeSpline(lerpf(i, 0.6f, 1.5f), bl2);
    const Ptr s2 = mountainRidgeSpline(lerpf(i, 0.6f, 1.0f), bl2);
    const Ptr s3 = mountainRidgeSpline(i, bl2);
    const Ptr s4 = ridgeSpline(f - 0.15f, 0.5f * i, lerpf(0.5f, 0.5f, 0.5f) * i, 0.5f * i, 0.6f * i, 0.5f);
    const Ptr s5 = ridgeSpline(f, j * i, g * i, 0.5f * i, 0.6f * i, 0.5f);
    const Ptr s6 = ridgeSpline(f, j, j, g, h, 0.5f);
    const Ptr s7 = ridgeSpline(f, j, j, g, h, 0.5f);
    const Ptr s8 = B(In::Ridges).add(-1.0f, f).add(-0.4f, s6).add(0.0f, h + 0.07f).build();
    const Ptr s9 = ridgeSpline(-0.02f, k, k, g, h, 0.0f);
    B b(In::Erosion);
    b.add(-0.85f, s1).add(-0.7f, s2).add(-0.4f, s3).add(-0.35f, s4).add(-0.1f, s5).add(0.2f, s6);
    if (bl) b.add(0.4f, s7).add(0.45f, s8).add(0.55f, s8).add(0.58f, s7);
    b.add(0.7f, s9);
    return b.build();
}

Ptr overworldOffset() {
    const Ptr s1 = erosionOffsetSpline(-0.15f, 0.0f, 0.0f, 0.1f, 0.0f, -0.03f, false, false);
    const Ptr s2 = erosionOffsetSpline(-0.1f, 0.03f, 0.1f, 0.1f, 0.01f, -0.03f, false, false);
    const Ptr s3 = erosionOffsetSpline(-0.1f, 0.03f, 0.1f, 0.7f, 0.01f, -0.03f, true, true);
    const Ptr s4 = erosionOffsetSpline(-0.05f, 0.03f, 0.1f, 1.0f, 0.01f, 0.01f, true, true);
    return B(In::Continents)
        .add(-1.1f, 0.044f)
        .add(-1.02f, -0.2222f)
        .add(-0.51f, -0.2222f)
        .add(-0.44f, -0.12f)
        .add(-0.18f, -0.12f)
        .add(-0.16f, s1)
        .add(-0.15f, s1)
        .add(-0.1f, s2)
        .add(0.25f, s3)
        .add(1.0f, s4)
        .build();
}

Ptr erosionFactor(float f, bool bl) {
    const Ptr s = B(In::Weirdness).add(-0.2f, 6.3f).add(0.2f, f).build();
    B b(In::Erosion);
    b.add(-0.6f, s)
        .add(-0.5f, B(In::Weirdness).add(-0.05f, 6.3f).add(0.05f, 2.67f).build())
        .add(-0.35f, s)
        .add(-0.25f, s)
        .add(-0.1f, B(In::Weirdness).add(-0.05f, 2.67f).add(0.05f, 6.3f).build())
        .add(0.03f, s);
    if (bl) {
        const Ptr s2 = B(In::Weirdness).add(0.0f, f).add(0.1f, 0.625f).build();
        const Ptr s3 = B(In::Ridges).add(-0.9f, f).add(-0.69f, s2).build();
        b.add(0.35f, f).add(0.45f, s3).add(0.55f, s3).add(0.62f, f);
    } else {
        const Ptr s2 = B(In::Ridges).add(-0.7f, s).add(-0.15f, 1.37f).build();
        const Ptr s3 = B(In::Ridges).add(0.45f, s).add(0.7f, 1.56f).build();
        b.add(0.05f, s3).add(0.4f, s3).add(0.45f, s2).add(0.55f, s2).add(0.58f, f);
    }
    return b.build();
}

Ptr overworldFactor() {
    return B(In::Continents)
        .add(-0.19f, 3.95f)
        .add(-0.15f, erosionFactor(6.25f, true))
        .add(-0.1f, erosionFactor(5.47f, true))
        .add(0.03f, erosionFactor(5.08f, true))
        .add(0.06f, erosionFactor(4.69f, false))
        .build();
}

Ptr weirdnessJaggedness(float f) {
    return B(In::Weirdness).add(-0.01f, 0.63f * f).add(0.01f, 0.3f * f).build();
}

Ptr ridgeJaggedness(float f, float g) {
    const float h = peaksAndValleys(0.4f);
    const float i = peaksAndValleys(0.56666666f);
    const float j = (h + i) / 2.0f;
    B b(In::Ridges);
    b.add(h, 0.0f);
    if (g > 0.0f) b.add(j, weirdnessJaggedness(g));
    else b.add(j, 0.0f);
    if (f > 0.0f) b.add(1.0f, weirdnessJaggedness(f));
    else b.add(1.0f, 0.0f);
    return b.build();
}

Ptr erosionJaggedness(float f, float g, float h, float i) {
    const Ptr s1 = ridgeJaggedness(f, h);
    const Ptr s2 = ridgeJaggedness(g, i);
    return B(In::Erosion).add(-1.0f, s1).add(-0.78f, s2).add(-0.5775f, s2).add(-0.375f, 0.0f).build();
}

Ptr overworldJaggedness() {
    return B(In::Continents)
        .add(-0.11f, 0.0f)
        .add(0.03f, erosionJaggedness(1.0f, 0.5f, 0.0f, 0.0f))
        .add(0.65f, erosionJaggedness(1.0f, 1.0f, 1.0f, 0.0f))
        .build();
}

} // namespace

const TerrainSplines& TerrainSplines::overworld() {
    static const TerrainSplines splines{overworldOffset(), overworldFactor(), overworldJaggedness()};
    return splines;
}

} // namespace mc
