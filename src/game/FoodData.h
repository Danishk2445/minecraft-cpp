// FoodData.h -- hunger (vanilla FoodData): food level, saturation and exhaustion, with
// natural regeneration and starvation.
#pragma once

#include "game/WorldParams.h"

namespace mc {

struct FoodData {
    int foodLevel = 20;      // 0..20
    float saturation = 5.0f; // 0..foodLevel
    float exhaustion = 0.0f; // 0..40; every 4 costs a point of saturation, then food
    int tickTimer = 0;

    // Eating: food and saturation added (saturation capped at the new food level).
    void eat(int nutrition, float saturationAdded);
    void addExhaustion(float amount);
    bool needsFood() const { return foodLevel < 20; }

    // One tick: exhaustion drains saturation, then food (not on Peaceful); with natural
    // regeneration a full bar heals fast (saturation) or slowly (18+); an empty one
    // starves down to 10 health on Easy, 1 on Normal and to death on Hard. `heal` and
    // `starve` act on the player.
    template <class Heal, class Starve>
    void tick(float health, float maxHealth, Difficulty difficulty, bool naturalRegeneration, Heal&& heal, Starve&& starve);
};

template <class Heal, class Starve>
void FoodData::tick(float health, float maxHealth, Difficulty difficulty, bool naturalRegeneration, Heal&& heal,
                    Starve&& starve) {
    if (exhaustion > 4.0f) {
        exhaustion -= 4.0f;
        if (saturation > 0.0f) saturation = saturation - 1.0f > 0.0f ? saturation - 1.0f : 0.0f;
        else if (difficulty != Difficulty::Peaceful) foodLevel = foodLevel > 0 ? foodLevel - 1 : 0;
    }
    const bool hurt = health > 0.0f && health < maxHealth;
    if (naturalRegeneration && saturation > 0.0f && hurt && foodLevel >= 20) {
        if (++tickTimer >= 10) {
            const float f = saturation < 6.0f ? saturation : 6.0f;
            heal(f / 6.0f);
            addExhaustion(f);
            tickTimer = 0;
        }
    } else if (naturalRegeneration && foodLevel >= 18 && hurt) {
        if (++tickTimer >= 80) {
            heal(1.0f);
            addExhaustion(6.0f);
            tickTimer = 0;
        }
    } else if (foodLevel <= 0) {
        if (++tickTimer >= 80) {
            if (health > 10.0f || difficulty == Difficulty::Hard || (health > 1.0f && difficulty == Difficulty::Normal))
                starve(1.0f);
            tickTimer = 0;
        }
    } else {
        tickTimer = 0;
    }
}

} // namespace mc
