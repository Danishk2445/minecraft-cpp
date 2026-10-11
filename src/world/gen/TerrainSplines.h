// TerrainSplines.h -- vanilla CubicSpline and the overworld terrain-shaping
// splines (TerrainProvider): offset (base height), factor (vertical squash) and
// jaggedness (peak spikiness), as functions of continentalness, erosion,
// weirdness and folded ridges.
#pragma once

#include <memory>
#include <vector>

namespace mc {

struct SplinePoint {
    float continents = 0, erosion = 0, weirdness = 0, ridges = 0;
};

class CubicSpline {
public:
    enum class Input { Continents, Erosion, Weirdness, Ridges };
    using Ptr = std::shared_ptr<const CubicSpline>;

    static Ptr constant(float v);
    float eval(const SplinePoint& p) const;

    class Builder {
    public:
        explicit Builder(Input input) : input_(input) {}
        Builder& add(float location, float value, float derivative = 0.0f);
        Builder& add(float location, Ptr value, float derivative = 0.0f);
        Ptr build() const;

    private:
        Input input_;
        std::vector<float> locations_, derivatives_;
        std::vector<Ptr> values_;
    };

private:
    bool constant_ = true;
    float value_ = 0;
    Input input_ = Input::Continents;
    std::vector<float> locations_, derivatives_;
    std::vector<Ptr> values_;
};

struct TerrainSplines {
    CubicSpline::Ptr offset, factor, jaggedness;
    static const TerrainSplines& overworld();
};

// Vanilla NoiseRouterData.peaksAndValleys: folds weirdness into ridges (-1 valley .. 1 peak).
inline float peaksAndValleys(float weirdness) {
    const float a = weirdness < 0 ? -weirdness : weirdness;
    const float b = a - 0.6666667f;
    return -((b < 0 ? -b : b) - 0.33333334f) * 3.0f;
}

} // namespace mc
