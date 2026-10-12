#include "game/FoodData.h"

#include <algorithm>

namespace mc {

void FoodData::eat(int nutrition, float saturationAdded) {
    foodLevel = std::clamp(foodLevel + nutrition, 0, 20);
    saturation = std::clamp(saturation + saturationAdded, 0.0f, float(foodLevel));
}

void FoodData::addExhaustion(float amount) { exhaustion = std::min(exhaustion + amount, 40.0f); }

} // namespace mc
