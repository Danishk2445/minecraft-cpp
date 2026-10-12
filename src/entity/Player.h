// Player.h -- the local player's movement (vanilla LocalPlayer/Player/LivingEntity
// aiStep and travel): walking, sprinting, sneaking with edge guard, jumping,
// ladders, swimming, lava, creative flight and spectator noclip.
#pragma once

#include <optional>
#include <vector>

#include "entity/LivingEntity.h"
#include "game/Damage.h"
#include "game/FoodData.h"
#include "game/Inventory.h"

namespace mc {

enum class GameMode : uint8_t { Survival, Creative, Adventure, Spectator };
std::string_view gameModeName(GameMode m);
std::optional<GameMode> gameModeFromName(std::string_view name);

struct PlayerInput {
    float forward = 0.0f; // -1..1 (W/S)
    float strafe = 0.0f;  // -1..1 (A/D), positive = left like vanilla xxa
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    bool doubleTapJump = false; // toggles creative flight
};

class Player : public LivingEntity {
public:
    Player();

    GameMode mode() const { return mode_; }
    void setMode(GameMode m);
    bool flying = false;
    bool sprinting = false;
    bool crouching = false;
    float walkSpeed = 0.1f;   // movement_speed attribute
    float flyingSpeed = 0.05f;
    Inventory inventory;
    std::array<ItemStack, 27> enderItems{}; // the ender chest's contents, shared by every ender chest
    // Survival (vanilla LivingEntity and Player): health, hunger, air, fire and damage.
    float health = 20.0f, maxHealth = 20.0f;
    FoodData food;
    int airSupply = 300, maxAirSupply = 300;
    int remainingFireTicks = 0;
    int invulnerableTime = 0; // after a hit: 20 ticks, the second half blocks weaker hits
    float lastHurt = 0.0f;
    int hurtTime = 0, hurtDuration = 10; // the red flash and camera shake
    int deathTime = 0;                    // ticks since dying
    DamageType lastDamage = DamageType::Generic;
    float lastFallDistance = 0.0f; // of the last fall that hurt, for the death message
    float armorToughness = 0.0f;
    // Creative and spectator players take only damage that bypasses invulnerability.
    bool invulnerable() const { return mode_ == GameMode::Creative || mode_ == GameMode::Spectator; }
    bool dead() const { return health <= 0.0f; }
    // Takes damage (vanilla hurtServer and actuallyHurt): invulnerability frames, armor and
    // its wear, hunger. True when it hurt.
    bool hurt(DamageType type, float amount);
    void heal(float amount);
    void causeFoodExhaustion(float amount) {
        if (!invulnerable()) food.addExhaustion(amount);
    }
    // Vanilla canEat: when hungry, for foods eaten any time, and always in creative.
    bool canEat(bool alwaysEdible) const { return invulnerable() || alwaysEdible || food.needsFood(); }
    // Beds: where the player respawns (a bed's head), the bed it sleeps in, and how long it
    // has slept (vanilla sleepCounter: 100 is deep sleep).
    std::optional<BlockPos> respawnPos;
    std::optional<BlockPos> sleepingPos;
    int sleepTimer = 0;
    bool sleeping() const { return sleepingPos.has_value(); }
    // Using an item (eating, drinking): the item and the ticks left (vanilla useItemRemaining).
    const Item* useItem = nullptr;
    int useItemRemaining = 0;
    bool useOffhand = false;
    bool usingItem() const { return useItemRemaining > 0 && useItem; }
    void stopUsingItem() {
        useItem = nullptr;
        useItemRemaining = 0;
    }
    void igniteForTicks(int ticks);
    // After movement, once per tick: landing, air, fire and lava, the blocks it is in, the
    // void, hunger and regeneration (vanilla baseTick, FoodData.tick and Peaceful healing).
    void survivalTick(const BlockGetter& level, Difficulty difficulty, bool naturalRegeneration);
    // The death message's translation key ("death.attack.drown", "death.fell.accident.generic").
    std::string_view deathMessageKey() const;
    // Back to full health and food, nothing burning or lost (vanilla respawn).
    void respawn();
    int experienceLevel = 0;
    float experienceProgress = 0.0f; // toward the next level, 0..1
    int totalExperience = 0;
    int score = 0;       // experience gained this life, shown on the death screen
    int takeXpDelay = 0; // ticks before the next orb can be picked up
    int armorValue = 0;
    bool alive() const { return health > 0.0f; }
    // Vanilla getXpNeededForNextLevel: 7 + 2L up to level 15, 37 + 5(L-15) to 30, then 112 + 9(L-30).
    int xpNeededForNextLevel() const;
    // Vanilla giveExperiencePoints and giveExperienceLevels.
    void giveExperiencePoints(int points);
    void giveExperienceLevels(int levels);
    // Attack strength (vanilla attackStrengthTicker): ticks since the last attack, a swing at
    // nothing or a change of weapon. A full-strength attack takes 20 / attack speed ticks.
    int attackStrengthTicker = 0;
    float attackStrengthDelay() const;
    // Vanilla getAttackStrengthScale: 0..1 of the way to full strength.
    float attackStrengthScale(float adjustTicks) const;
    void resetAttackStrengthTicker() { attackStrengthTicker = 0; }
    // The first-person hands lag behind the view (vanilla LocalPlayer xBob/yBob).
    float xBob = 0.0f, yBob = 0.0f, xBobO = 0.0f, yBobO = 0.0f;
    // Stacks thrown out of menus or the hotbar, waiting for the world to spawn them as
    // item entities (menus have no level at hand).
    std::vector<ItemStack> droppedItems;
    void drop(const ItemStack& stack) {
        if (!stack.empty()) droppedItems.push_back(stack);
    }

    // One game tick: input, movement and collision, then the body and limb animation.
    // (The session ticks the player itself; the level doesn't own it.)
    void tick(const BlockGetter& level, const PlayerInput& input);
    using Entity::tick;
    float eyeHeight() const { return crouching ? 1.27f : 1.62f; }
    glm::dvec3 eyePos() const { return pos + glm::dvec3(0.0, eyeHeight(), 0.0); }
    bool onClimbable(const BlockGetter& level) const;

private:
    bool eyeInWater(const BlockGetter& level) const;
    bool inWall(const BlockGetter& level) const;
    void updateArmor();
    void travel(const BlockGetter& level, glm::vec3 input, bool jumping);
    void moveRelative(float speed, const glm::vec3& input);
    glm::dvec3 backOffFromEdge(const BlockGetter& level, glm::dvec3 delta) const;

    GameMode mode_ = GameMode::Survival;
    int jumpDelay_ = 0;
    const Item* lastMainHandItem_ = nullptr; // a different item resets the attack strength
};

} // namespace mc
