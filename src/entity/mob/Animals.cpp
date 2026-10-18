#include "entity/mob/Animals.h"

#include <cmath>
#include <unordered_set>

#include "entity/ExperienceOrb.h"
#include "entity/ItemEntity.h"
#include "entity/Player.h"
#include "entity/ai/Goals.h"
#include "game/DyeColor.h"
#include "resources/ResourcePack.h"
#include "world/biome/Biome.h"
#include "world/level/Level.h"

namespace mc {
namespace {

double distanceSq(const Entity& a, const Entity& b) {
    const glm::dvec3 d = a.pos - b.pos;
    return glm::dot(d, d);
}

// Vanilla BreedGoal: in love, finds a partner in love within 8 blocks, walks to it, and
// after 3 seconds close by makes a baby.
class BreedGoal : public Goal {
public:
    BreedGoal(Animal& animal, double speed) : animal_(animal), speed_(speed) { flags = kGoalMove | kGoalLook; }
    bool canUse(Level& level) override {
        if (animal_.inLove <= 0) return false;
        partner_ = freePartner(level);
        return partner_ != 0;
    }
    bool canContinueToUse(Level& level) override {
        const Animal* p = partner(level);
        return p && p->alive() && p->inLove > 0 && loveTime_ < 60 && !p->panicking;
    }
    void stop(Level& level) override {
        (void)level;
        partner_ = 0;
        loveTime_ = 0;
    }
    void tick(Level& level) override {
        Animal* p = partner(level);
        if (!p) return;
        animal_.lookControl.setLookAt(*p, 10.0f, animal_.maxHeadXRot());
        animal_.navigation.moveTo(level, *p, speed_);
        ++loveTime_;
        if (loveTime_ >= adjustedTickDelay(60) && distanceSq(animal_, *p) < 9.0) animal_.spawnChildFromBreeding(level, *p);
    }

private:
    Animal* partner(Level& level) const { return dynamic_cast<Animal*>(level.entity(partner_)); }
    uint32_t freePartner(Level& level) const {
        uint32_t best = 0;
        double bestDistance = std::numeric_limits<double>::max();
        for (Entity* e : level.entitiesIn(animal_.box().inflated(8.0, 8.0, 8.0), &animal_)) {
            auto* other = dynamic_cast<Animal*>(e);
            if (!other || !animal_.canMate(*other) || other->panicking) continue;
            const double d = distanceSq(animal_, *other);
            if (d < bestDistance) {
                best = other->id;
                bestDistance = d;
            }
        }
        return best;
    }
    Animal& animal_;
    double speed_;
    uint32_t partner_ = 0;
    int loveTime_ = 0;
};

// Vanilla FollowParentGoal: a baby keeps within 3 blocks of the nearest grown-up.
class FollowParentGoal : public Goal {
public:
    FollowParentGoal(Animal& animal, double speed) : animal_(animal), speed_(speed) {}
    bool canUse(Level& level) override {
        if (animal_.age >= 0) return false;
        uint32_t best = 0;
        double bestDistance = std::numeric_limits<double>::max();
        for (Entity* e : level.entitiesIn(animal_.box().inflated(8.0, 4.0, 8.0), &animal_)) {
            auto* a = dynamic_cast<Animal*>(e);
            if (!a || a->type() != animal_.type() || a->age < 0) continue;
            const double d = distanceSq(animal_, *a);
            if (!(d > bestDistance)) {
                best = a->id;
                bestDistance = d;
            }
        }
        if (!best || bestDistance < 9.0) return false;
        parent_ = best;
        return true;
    }
    bool canContinueToUse(Level& level) override {
        if (animal_.age >= 0) return false;
        const auto* p = dynamic_cast<Animal*>(level.entity(parent_));
        if (!p || !p->alive()) return false;
        const double d = distanceSq(animal_, *p);
        return !(d < 9.0) && !(d > 256.0);
    }
    void start(Level& level) override {
        (void)level;
        timeToRecalcPath_ = 0;
    }
    void stop(Level& level) override {
        (void)level;
        parent_ = 0;
    }
    void tick(Level& level) override {
        if (--timeToRecalcPath_ > 0) return;
        timeToRecalcPath_ = adjustedTickDelay(10);
        if (Entity* p = level.entity(parent_)) animal_.navigation.moveTo(level, *p, speed_);
    }

private:
    Animal& animal_;
    double speed_;
    uint32_t parent_ = 0;
    int timeToRecalcPath_ = 0;
};

// Vanilla EatBlockGoal: now and then a sheep lowers its head and eats the short grass it
// stands in or the grass block under it (with mob griefing), growing its wool back.
class EatBlockGoal : public Goal {
public:
    explicit EatBlockGoal(Sheep& sheep) : sheep_(sheep) { flags = kGoalMove | kGoalLook | kGoalJump; }
    bool canUse(Level& level) override {
        if (sheep_.random.nextInt(sheep_.isBaby() ? 50 : 1000) != 0) return false;
        const BlockPos feet = BlockPos::containing(sheep_.pos);
        return is(level, feet, "short_grass") || is(level, feet.offset(0, -1, 0), "grass_block");
    }
    bool canContinueToUse(Level& level) override {
        (void)level;
        return sheep_.eatAnimationTick > 0;
    }
    void start(Level& level) override {
        (void)level;
        sheep_.eatAnimationTick = adjustedTickDelay(40);
        sheep_.eatAnimation = 40; // vanilla entity event 10
        sheep_.navigation.stop();
    }
    void stop(Level& level) override {
        (void)level;
        sheep_.eatAnimationTick = 0;
    }
    void tick(Level& level) override {
        sheep_.eatAnimationTick = std::max(0, sheep_.eatAnimationTick - 1);
        if (sheep_.eatAnimationTick != adjustedTickDelay(4)) return;
        const BlockPos feet = BlockPos::containing(sheep_.pos);
        if (is(level, feet, "short_grass")) {
            if (level.mobGriefing) level.destroyBlock(feet, false);
            sheep_.ate();
        } else if (const BlockPos below = feet.offset(0, -1, 0); is(level, below, "grass_block")) {
            if (level.mobGriefing) {
                level.levelEvent(LevelEventType::BlockBroken, below, level.getBlock(below));
                level.setBlockAndUpdate(below, level.registry().defaultState("dirt"));
            }
            sheep_.ate();
        }
    }

private:
    static bool is(const Level& level, const BlockPos& p, std::string_view id) {
        return shortId(level.registry().blockOf(level.getBlock(p)).id) == id;
    }
    Sheep& sheep_;
};

const std::unordered_set<std::string_view>& warmBiomes() {
    static const std::unordered_set<std::string_view> k = {
        "minecraft:desert",  "minecraft:warm_ocean",     "minecraft:jungle",         "minecraft:sparse_jungle",
        "minecraft:bamboo_jungle", "minecraft:savanna",  "minecraft:savanna_plateau", "minecraft:windswept_savanna",
        "minecraft:badlands", "minecraft:eroded_badlands", "minecraft:wooded_badlands"};
    return k;
}

const std::unordered_set<std::string_view>& coldBiomes() {
    static const std::unordered_set<std::string_view> k = {
        "minecraft:snowy_plains",  "minecraft:ice_spikes",     "minecraft:frozen_peaks",   "minecraft:jagged_peaks",
        "minecraft:snowy_slopes",  "minecraft:frozen_ocean",   "minecraft:deep_frozen_ocean", "minecraft:grove",
        "minecraft:frozen_river",  "minecraft:snowy_taiga",    "minecraft:snowy_beach",    "minecraft:cold_ocean",
        "minecraft:deep_cold_ocean", "minecraft:old_growth_pine_taiga", "minecraft:old_growth_spruce_taiga",
        "minecraft:taiga", "minecraft:windswept_forest", "minecraft:windswept_gravelly_hills", "minecraft:windswept_hills",
        "minecraft:stony_peaks"};
    return k;
}

// The child's wool (vanilla getOffspringColor): what the two dyes craft into, else either.
int mixedWool(int a, int b, Xoroshiro& random) {
    struct Mix {
        int a, b, out;
    };
    static constexpr Mix kMixes[] = {{0, 15, 7}, {0, 7, 8}, {14, 4, 1}, {14, 0, 6}, {11, 0, 3},
                                     {13, 0, 5}, {11, 13, 9}, {11, 14, 10}, {10, 6, 2}};
    for (const Mix& m : kMixes)
        if ((m.a == a && m.b == b) || (m.a == b && m.b == a)) return m.out;
    return random.nextBool() ? a : b;
}

} // namespace

FarmVariant farmVariantAt(const Level& level, const BlockPos& p) {
    if (!level.biomes()) return FarmVariant::Temperate;
    const std::string& id = level.biomes()->get(level.biomeAt(p)).id;
    if (warmBiomes().contains(id)) return FarmVariant::Warm;
    if (coldBiomes().contains(id)) return FarmVariant::Cold;
    return FarmVariant::Temperate;
}

// Animal

Animal::Animal(const EntityTypeInfo& type) : Mob(type) {
    setPathfindingMalus(PathType::DangerFire, 16.0f);
    setPathfindingMalus(PathType::DamageFire, -1.0f);
}

void Animal::addAnimalGoals(double panicSpeed, double temptSpeed, double followSpeed, bool eatsGrass) {
    const std::string& food = type_->food;
    const bool pig = type() == "minecraft:pig";
    goals.emplace<FloatGoal>(0, *this);
    goals.emplace<PanicGoal>(1, *this, panicSpeed);
    // Vanilla: pigs breed at 3, tempt at 4 (twice: carrots on sticks and their food).
    goals.emplace<BreedGoal>(pig ? 3 : 2, *this, 1.0);
    if (pig) goals.emplace<TemptGoal>(4, *this, temptSpeed, "pig_tempt_items");
    goals.emplace<TemptGoal>(pig ? 4 : 3, *this, temptSpeed, food);
    goals.emplace<FollowParentGoal>(pig ? 5 : 4, *this, followSpeed);
    int next = pig ? 6 : 5;
    if (eatsGrass) goals.emplace<EatBlockGoal>(next++, static_cast<Sheep&>(*this));
    goals.emplace<WaterAvoidingRandomStrollGoal>(next++, *this, 1.0);
    goals.emplace<LookAtPlayerGoal>(next++, *this, 6.0f);
    goals.emplace<RandomLookAroundGoal>(next, *this);
}

void Animal::setAge(int a) {
    const bool wasBaby = isBaby();
    age = a;
    if (wasBaby != isBaby()) refreshDimensions();
}

void Animal::ageUp(int seconds) {
    int a = age + seconds * 20;
    if (a > 0) a = 0;
    setAge(a);
}

bool Animal::isFood(const Level& level, const ItemStack& stack) const {
    return !stack.empty() && level.itemRegistry() && !type_->food.empty() &&
           level.itemRegistry()->inTag(*stack.item, type_->food);
}

void Animal::setInLove(Level& level) {
    inLove = 600;
    level.levelEvent(LevelEventType::Hearts, BlockPos::containing(pos), 7, int(width * 100.0f) | (int(height * 100.0f) << 16),
                     pos);
}

bool Animal::canMate(const Animal& other) const {
    return &other != this && other.type() == type() && inLove > 0 && other.inLove > 0;
}

std::unique_ptr<Animal> Animal::breedOffspring(Level& level, Animal& partner) {
    (void)partner;
    const EntityTypes* types = level.entityTypes();
    if (!types) return nullptr;
    std::unique_ptr<Mob> m = createMob(*types, type());
    return std::unique_ptr<Animal>(dynamic_cast<Animal*>(m.release()));
}

void Animal::spawnChildFromBreeding(Level& level, Animal& partner) {
    std::unique_ptr<Animal> child = breedOffspring(level, partner);
    if (!child) return;
    child->setAge(kBabyAge);
    child->pos = child->oldPos = pos;
    child->setRotation(0.0f, 0.0f);
    child->random = Xoroshiro(int64_t(level.random().nextLong()));
    setAge(6000);
    partner.setAge(6000);
    inLove = partner.inLove = 0;
    level.levelEvent(LevelEventType::Hearts, BlockPos::containing(pos), 7, int(width * 100.0f) | (int(height * 100.0f) << 16),
                     pos);
    if (level.doMobLoot) ExperienceOrb::award(level, pos, random.nextInt(7) + 1);
    level.addEntity(std::move(child));
}

float Animal::walkTargetValue(const Level& level, const BlockPos& p) const {
    if (shortId(level.registry().blockOf(level.getBlock(p.offset(0, -1, 0))).id) == "grass_block") return 10.0f;
    const float f = float(level.rawBrightness(p, level.skyDarken())) / 15.0f;
    return f / (4.0f - 3.0f * f) - 0.5f;
}

bool Animal::checkSpawnRules(const Level& level, SpawnReason reason) const {
    (void)reason;
    // Vanilla checkAnimalSpawnRules: on grass, in the light (raw brightness above 8).
    const BlockPos feet = BlockPos::containing(pos);
    static thread_local const BlockRegistry* cached = nullptr;
    static thread_local TagId spawnable = 0;
    if (cached != &level.registry()) {
        cached = &level.registry();
        spawnable = level.registry().tag("animals_spawnable_on").value_or(TagId(0));
    }
    return level.hasTag(feet.offset(0, -1, 0), spawnable) && level.rawBrightness(feet, 0) > 8;
}

bool Animal::hurtServer(Level& level, const DamageSource& source, float amount) {
    const bool hurt = Mob::hurtServer(level, source, amount);
    if (hurt) inLove = 0;
    return hurt;
}

bool Animal::interact(Level& level, Player& player, ItemStack& held) {
    if (isFood(level, held)) {
        if (age == 0 && canFallInLove()) {
            if (!player.invulnerable()) held.shrink(1);
            setInLove(level);
            return true;
        }
        if (isBaby()) {
            if (!player.invulnerable()) held.shrink(1);
            ageUp(int(float((-age) / 20) * 0.1f)); // vanilla getSpeedUpSecondsWhenFeeding (int division)
            return true;
        }
    }
    return Mob::interact(level, player, held);
}

void Animal::finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) {
    // Vanilla AgeableMob.finalizeSpawn: after the first of a group, 5% are babies.
    if (group.babies && group.size > 0 && random.nextFloat() <= group.babyChance) setAge(kBabyAge);
    ++group.size;
    Mob::finalizeSpawn(level, reason, group);
}

void Animal::mobAiStep(Level& level) {
    // Vanilla AgeableMob.aiStep: babies grow, parents rest; Animal.aiStep: love fades.
    if (age < 0) setAge(age + 1);
    else if (age > 0) setAge(age - 1);
    if (age != 0) inLove = 0;
    if (inLove > 0 && --inLove % 10 == 0)
        level.levelEvent(LevelEventType::Hearts, BlockPos::containing(pos), 1,
                         int(width * 100.0f) | (int(height * 100.0f) << 16), pos);
}

int Animal::experienceReward() { return 1 + random.nextInt(3); }

void Animal::saveExtra(Json& out) const {
    Mob::saveExtra(out);
    out["age"] = age;
    out["in_love"] = inLove;
}

void Animal::loadExtra(const Json& in, const ItemRegistry& items) {
    Mob::loadExtra(in, items);
    setAge(in.value("age", 0));
    inLove = in.value("in_love", 0);
}

// Pig

Pig::Pig(const EntityTypeInfo& type) : Animal(type) { addAnimalGoals(1.25, 1.2, 1.1, false); }

std::unique_ptr<Animal> Pig::breedOffspring(Level& level, Animal& partner) {
    std::unique_ptr<Animal> child = Animal::breedOffspring(level, partner);
    if (auto* p = dynamic_cast<Pig*>(child.get()); p && dynamic_cast<Pig*>(&partner))
        p->variant = random.nextBool() ? variant : static_cast<Pig&>(partner).variant;
    return child;
}

void Pig::finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) {
    variant = farmVariantAt(level, BlockPos::containing(pos)) == FarmVariant::Warm ? FarmVariant::Warm : FarmVariant::Temperate;
    Animal::finalizeSpawn(level, reason, group);
}

void Pig::saveExtra(Json& out) const {
    Animal::saveExtra(out);
    out["variant"] = variant == FarmVariant::Warm ? "warm" : "temperate";
}

void Pig::loadExtra(const Json& in, const ItemRegistry& items) {
    Animal::loadExtra(in, items);
    variant = in.value("variant", std::string()) == "warm" ? FarmVariant::Warm : FarmVariant::Temperate;
}

// Cow

Cow::Cow(const EntityTypeInfo& type) : Animal(type) { addAnimalGoals(2.0, 1.25, 1.25, false); }

std::unique_ptr<Animal> Cow::breedOffspring(Level& level, Animal& partner) { return Animal::breedOffspring(level, partner); }

bool Cow::interact(Level& level, Player& player, ItemStack& held) {
    // Milking (vanilla ItemUtils.createFilledResult): the bucket becomes milk; creative
    // players keep the bucket and get a milk bucket if they have none.
    const ItemRegistry* items = level.itemRegistry();
    if (!held.empty() && held.item->id == "minecraft:bucket" && !isBaby() && items) {
        const Item* milk = items->find("milk_bucket");
        if (!milk) return false;
        if (player.invulnerable()) {
            bool has = false;
            for (int i = 0; i <= Inventory::kOffhandSlot; ++i) has = has || player.inventory.slot(i).item == milk;
            if (!has) player.inventory.add(*milk, 1);
        } else if (held.count == 1) {
            held = ItemStack{milk, 1};
        } else {
            held.shrink(1);
            ItemStack m{milk, 1};
            if (!player.inventory.add(m)) player.drop(m);
        }
        return true;
    }
    return Animal::interact(level, player, held);
}

// Sheep

Sheep::Sheep(const EntityTypeInfo& type) : Animal(type) { addAnimalGoals(1.25, 1.1, 1.1, true); }

int Sheep::randomColor(Xoroshiro& random, FarmVariant climate) {
    // Weights 5, 5, 5, 3 for the uncommon ones and 82 for the climate's own color.
    static constexpr int kWarm[4] = {7, 8, 0, 15}, kCold[4] = {8, 7, 0, 12}, kTemperate[4] = {15, 7, 8, 12};
    const int common = climate == FarmVariant::Warm ? 12 : climate == FarmVariant::Cold ? 15 : 0;
    const int* uncommon = climate == FarmVariant::Warm ? kWarm : climate == FarmVariant::Cold ? kCold : kTemperate;
    static constexpr int kWeights[4] = {5, 5, 5, 3};
    int roll = random.nextInt(100);
    for (int i = 0; i < 4; ++i) {
        if (roll < kWeights[i]) return uncommon[i];
        roll -= kWeights[i];
    }
    return random.nextInt(500) == 0 ? 6 : common; // pink, rarely
}

void Sheep::shear(Level& level) {
    const ItemRegistry* items = level.itemRegistry();
    const Item* wool = items ? items->find(std::string(kDyeColors[size_t(color)]) + "_wool") : nullptr;
    if (wool) {
        const int count = 1 + random.nextInt(3);
        for (int i = 0; i < count; ++i) {
            ItemEntity& e = ItemEntity::spawn(level, pos + glm::dvec3(0.0, 1.0, 0.0), ItemStack{wool, 1});
            e.pickupDelay = 10;
            e.velocity += glm::dvec3((random.nextFloat() - random.nextFloat()) * 0.1f, random.nextFloat() * 0.05f,
                                     (random.nextFloat() - random.nextFloat()) * 0.1f);
        }
    }
    sheared = true;
}

void Sheep::ate() {
    sheared = false;
    if (isBaby()) ageUp(60);
}

bool Sheep::interact(Level& level, Player& player, ItemStack& held) {
    if (!held.empty() && held.item->tool == ToolKind::Shears) {
        if (!readyForShearing()) return false;
        shear(level);
        if (held.hurt(1)) held = ItemStack{};
        return true;
    }
    // Dye (vanilla DyeItem.interactLivingEntity).
    if (!held.empty())
        if (const auto dye = dyeOfItem(held.item->id); dye && alive() && !sheared && color != *dye) {
            color = *dye;
            held.shrink(1);
            return true;
        }
    return Animal::interact(level, player, held);
}

std::unique_ptr<Animal> Sheep::breedOffspring(Level& level, Animal& partner) {
    std::unique_ptr<Animal> child = Animal::breedOffspring(level, partner);
    if (auto* s = dynamic_cast<Sheep*>(child.get()))
        if (auto* other = dynamic_cast<Sheep*>(&partner)) s->color = mixedWool(color, other->color, level.random());
    return child;
}

void Sheep::finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) {
    color = randomColor(random, farmVariantAt(level, BlockPos::containing(pos)));
    Animal::finalizeSpawn(level, reason, group);
}

void Sheep::mobAiStep(Level& level) {
    Animal::mobAiStep(level);
    // The grazing animation runs a tick at a time (vanilla's client counts it down so).
    if (eatAnimation > 0) --eatAnimation;
}

DeathContext Sheep::deathContext() const {
    DeathContext d = Animal::deathContext();
    d.woolColor = sheared ? -1 : color;
    return d;
}

float Sheep::headEatPosition(float pt) const {
    const int t = eatAnimation;
    if (t <= 0) return 0.0f;
    if (t >= 4 && t <= 36) return 1.0f;
    return t < 4 ? (float(t) - pt) / 4.0f : -(float(t - 40) - pt) / 4.0f;
}

float Sheep::headEatAngle(float pt) const {
    constexpr float kPi = 3.14159265358979f;
    const int t = eatAnimation;
    if (t > 4 && t <= 36) {
        const float f = (float(t - 4) - pt) / 32.0f;
        return kPi / 5.0f + 0.21991149f * std::sin(f * 28.7f);
    }
    return t > 0 ? kPi / 5.0f : pitch * kPi / 180.0f;
}

void Sheep::saveExtra(Json& out) const {
    Animal::saveExtra(out);
    out["color"] = kDyeColors[size_t(color)];
    out["sheared"] = sheared;
}

void Sheep::loadExtra(const Json& in, const ItemRegistry& items) {
    Animal::loadExtra(in, items);
    color = dyeColorIndex(in.value("color", std::string("white"))).value_or(0);
    sheared = in.value("sheared", false);
}

// Chicken

Chicken::Chicken(const EntityTypeInfo& type) : Animal(type) {
    setPathfindingMalus(PathType::Water, 0.0f);
    addAnimalGoals(1.4, 1.0, 1.1, false);
    eggTime = random.nextInt(6000) + 6000;
}

std::unique_ptr<Animal> Chicken::breedOffspring(Level& level, Animal& partner) {
    std::unique_ptr<Animal> child = Animal::breedOffspring(level, partner);
    if (auto* c = dynamic_cast<Chicken*>(child.get()); c && dynamic_cast<Chicken*>(&partner))
        c->variant = random.nextBool() ? variant : static_cast<Chicken&>(partner).variant;
    return child;
}

void Chicken::finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) {
    variant = farmVariantAt(level, BlockPos::containing(pos)) == FarmVariant::Warm ? FarmVariant::Warm : FarmVariant::Temperate;
    eggTime = random.nextInt(6000) + 6000;
    Animal::finalizeSpawn(level, reason, group);
}

void Chicken::mobAiStep(Level& level) {
    Animal::mobAiStep(level);
    // Vanilla Chicken.aiStep: the wings flap while airborne, and falling is slowed.
    oFlap = flap;
    oFlapSpeed = flapSpeed;
    flapSpeed = std::clamp(flapSpeed + (onGround ? -1.0f : 4.0f) * 0.3f, 0.0f, 1.0f);
    if (!onGround && flapping < 1.0f) flapping = 1.0f;
    flapping *= 0.9f;
    if (!onGround && velocity.y < 0.0) velocity.y *= 0.6;
    flap += flapping * 2.0f;
    // An egg every 5 to 10 minutes (brown from warm chickens).
    if (alive() && !isBaby() && --eggTime <= 0) {
        if (const ItemRegistry* items = level.itemRegistry())
            if (const Item* egg = items->find(variant == FarmVariant::Warm ? "brown_egg" : "egg"))
                spawnAtLocation(level, ItemStack{egg, 1});
        eggTime = random.nextInt(6000) + 6000;
    }
}

void Chicken::saveExtra(Json& out) const {
    Animal::saveExtra(out);
    out["variant"] = variant == FarmVariant::Warm ? "warm" : "temperate";
    out["egg_time"] = eggTime;
}

void Chicken::loadExtra(const Json& in, const ItemRegistry& items) {
    Animal::loadExtra(in, items);
    variant = in.value("variant", std::string()) == "warm" ? FarmVariant::Warm : FarmVariant::Temperate;
    eggTime = in.value("egg_time", eggTime);
}

} // namespace mc
