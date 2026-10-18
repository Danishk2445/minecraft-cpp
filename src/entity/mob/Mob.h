// Mob.h -- the AI-driven living entity (vanilla Mob / PathfinderMob): attributes from
// its type, equipment, goals and target goals, the controls and navigation, the
// living tick (environment, AI, jumping, travel, pushing), despawning, death with
// loot and experience, and saving.
#pragma once

#include <array>
#include <optional>

#include "entity/LivingEntity.h"
#include "entity/ai/Controls.h"
#include "entity/ai/Goal.h"
#include "entity/ai/PathNavigation.h"
#include "entity/mob/EntityTypes.h"
#include "game/Item.h"

namespace mc {

class Player;

// Vanilla EntitySpawnReason, as far as it matters here.
enum class SpawnReason : uint8_t { Natural, ChunkGeneration, SpawnEgg, Command, Breeding };

// Vanilla SpawnGroupData: what the members of one spawned pack share.
struct SpawnGroup {
    int size = 0;              // members spawned so far (ageable: the first is never a baby)
    bool babies = true;        // AgeableMobGroupData.shouldSpawnBaby
    float babyChance = 0.05f;
    std::optional<bool> zombieBaby; // ZombieGroupData.isBaby
};

class Mob : public LivingEntity {
public:
    explicit Mob(const EntityTypeInfo& type);
    ~Mob() override;
    Mob(const Mob&) = delete;
    Mob& operator=(const Mob&) = delete;

    const EntityTypeInfo& typeInfo() const { return *type_; }
    std::string_view type() const override { return type_->id; }
    MobCategory category() const { return type_->category; }

    // Attributes (vanilla AttributeMap, base values from the type).
    float movementSpeed = 0.25f;
    float followRange = 16.0f;
    float attackDamage = 2.0f; // without the weapon's modifier
    // LivingEntity.speed (what the move control asks for) and the input it moves by.
    float speed = 0.0f;
    float xxa = 0.0f, yya = 0.0f, zza = 0.0f;
    bool jumping = false;

    // Equipment by EquipmentSlot (main hand, off hand, feet, legs, chest, head) and the
    // chance each drops on death (vanilla 0.085; above 1 it always drops, undamaged).
    std::array<ItemStack, 6> equipment{};
    std::array<float, 6> dropChances{0.085f, 0.085f, 0.085f, 0.085f, 0.085f, 0.085f};
    ItemStack& mainHand() { return equipment[0]; }
    const ItemStack& mainHand() const { return equipment[0]; }
    ItemStack& itemBySlot(EquipmentSlot s) { return equipment[size_t(s)]; }
    std::span<ItemStack> armorItems() override { return std::span<ItemStack>(equipment).subspan(2, 4); }
    void setItemSlot(EquipmentSlot slot, ItemStack stack);
    // The attack damage with the held weapon (vanilla ATTACK_DAMAGE attribute value).
    float attackDamageWithWeapon() const;

    // Vanilla persistenceRequired: never despawns (name tags, picked up items, spawn eggs... here
    // spawn eggs and summons don't set it: as in vanilla, only some things do).
    bool persistenceRequired = false;
    bool noAi = false; // vanilla NoAI: no goals or controls, just physics
    int noActionTime = 0;
    bool aggressive = false; // vanilla setAggressive: arms raised, bow drawn
    bool panicking = false;  // running its panic goal (vanilla isPanicking)
    // The entity it attacks (vanilla getTarget), by id.
    uint32_t targetId = 0;
    LivingEntity* target(const Level& level) const;
    void setTarget(const LivingEntity* t) { targetId = t ? t->id : 0; }
    // Using an item (a skeleton drawing its bow): ticks left of vanilla's 72000.
    int useItemRemaining = 0;
    bool usingItem() const { return useItemRemaining > 0; }
    int ticksUsingItem() const { return usingItem() ? 72000 - useItemRemaining : 0; }
    void startUsingItem() { useItemRemaining = 72000; }
    void stopUsingItem() { useItemRemaining = 0; }

    MoveControl moveControl;
    LookControl lookControl;
    JumpControl jumpControl;
    BodyRotationControl bodyRotation;
    PathNavigation navigation;
    Sensing sensing;
    GoalSelector goals, targets;

    // Path finding costs (vanilla setPathfindingMalus) and what it knows of itself.
    PathMalus malus = defaultMaluses();
    float pathfindingMalus(PathType t) const { return malus[size_t(t)]; }
    void setPathfindingMalus(PathType t, float m) { malus[size_t(t)] = m; }
    PathMob pathMob(const Level& level) const;
    // Vanilla getMaxFallDistance: 3, more with a target and health to spare.
    int maxFallDistance(const Level& level) const;
    // Vanilla PathfinderMob.getWalkTargetValue: how much it likes a spot (animals grass and
    // light, monsters the dark); below 0 it won't wander there.
    virtual float walkTargetValue(const Level& level, const BlockPos& p) const {
        (void)level, (void)p;
        return 0.0f;
    }
    // Head turning limits (vanilla getMaxHeadYRot, getHeadRotSpeed, getMaxHeadXRot).
    virtual float maxHeadYRot() const { return 75.0f; }
    virtual float headRotSpeed() const { return 10.0f; }
    virtual float maxHeadXRot() const { return 40.0f; }

    // One game tick (vanilla Mob.tick): environment, AI, movement; dead mobs fall over for
    // 20 ticks and vanish.
    void tick(Level& level) override;
    bool hurtServer(Level& level, const DamageSource& source, float amount) override;
    bool isPushable() const override { return alive(); }
    float eyeHeight() const override { return type_->eyeHeight * ageScale(); }
    virtual bool isBaby() const { return false; }
    float ageScale() const { return isBaby() ? 0.5f : 1.0f; }
    // Its size, its type's scaled for babies (vanilla refreshDimensions).
    void refreshDimensions();
    bool canBreatheUnderwater() const override { return type_->undead; }

    // Vanilla Mob.doHurtTarget: its melee hit (weapon and all).
    virtual bool doHurtTarget(Level& level, LivingEntity& target);
    // Vanilla isWithinMeleeAttackRange: its box grown by 0.828 sideways touches the target's.
    bool withinMeleeRange(const Entity& target) const;
    // Vanilla Entity.lookAt: turns the whole body toward `e` by at most these degrees.
    void lookAt(const Entity& e, float maxYaw, float maxPitch);
    // Vanilla hasLineOfSight: nothing solid between its eyes and the target's.
    bool hasLineOfSight(const Level& level, const Entity& e) const;

    // Spawning: vanilla finalizeSpawn (equipment, babies, variants) and the rules natural
    // spawns must pass (checkSpawnRules: the walk target value).
    virtual void finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group);
    virtual bool checkSpawnRules(const Level& level, SpawnReason reason) const;
    virtual int maxSpawnClusterSize() const { return 4; }
    // Vanilla removeWhenFarAway: animals stay, monsters go.
    virtual bool removeWhenFarAway(double distanceSq) const {
        (void)distanceSq;
        return true;
    }
    virtual bool despawnsInPeaceful() const { return false; }
    // Vanilla checkDespawn: beyond 128 blocks at once, beyond 32 now and then when idle.
    void checkDespawn(Level& level);

    // Right-clicked by the player holding `held` (vanilla mobInteract); true when it used
    // the item or did something.
    virtual bool interact(Level& level, Player& player, ItemStack& held);
    // Vanilla thunderHit: struck by lightning.
    virtual void thunderHit(Level& level);

    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;

protected:
    // The AI step (vanilla serverAiStep): sensing, target and goal selectors (every other
    // tick in full), navigation, then the controls.
    void serverAiStep(Level& level);
    virtual void customServerAiStep(Level& level) { (void)level; }
    // Before the AI each tick (vanilla aiStep's subclass parts: burning in daylight,
    // laying eggs, flapping).
    virtual void mobAiStep(Level& level) { (void)level; }
    // Vanilla LivingEntity.travel for mobs: walking, swimming and lava physics.
    virtual void travel(Level& level, glm::vec3 input);
    virtual double gravity() const { return 0.08; }
    // Vanilla onClimbable: ladders and vines (spiders also climb walls).
    virtual bool onClimbable(const Level& level) const;
    // Vanilla jumpFromGround.
    void jumpFromGround(const Level& level);
    // Vanilla isSunBurnTick: in daylight, bright, under the open sky, dry.
    bool isSunBurnTick(const Level& level);
    // Vanilla getLightLevelDependentMagicValue at the eyes: 0..1, above 0.5 is bright.
    float brightness(const Level& level) const;
    // Death (vanilla die and dropAllDeathLoot): loot, equipment, experience.
    virtual void die(Level& level);
    virtual void dropCustomDeathLoot(Level& level);
    virtual int experienceReward();
    virtual DeathContext deathContext() const;
    // Vanilla populateDefaultEquipmentSlots: random armor by local difficulty.
    void populateDefaultEquipment(Level& level);
    // Spawns a stack at its feet (vanilla spawnAtLocation).
    void spawnAtLocation(Level& level, const ItemStack& stack, double yOffset = 0.0);

    const EntityTypeInfo* type_;
    bool deathHandled_ = false;
    int noJumpDelay_ = 0;
    bool wasTouchingWater_ = false;
};

// A mob of the given type, or null for a type that isn't a mob ("minecraft:zombie").
std::unique_ptr<Mob> createMob(const EntityTypes& types, std::string_view id);

// Vanilla DifficultyInstance.getSpecialMultiplier at the world's state: 0 on a young
// Normal world, up to 1 on old Hard ones (chunks' inhabited time isn't tracked: 0).
float specialMultiplier(const Level& level);

} // namespace mc
