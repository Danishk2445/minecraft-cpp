// MobRenderer.cpp -- EntityRenderer's mobs (M12): vanilla PigRenderer, CowRenderer,
// SheepRenderer, ChickenRenderer, ZombieRenderer, SkeletonRenderer, CreeperRenderer,
// SpiderRenderer with their layers; armor and held items for humanoids; burning
// entities' flames; arrows.
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "entity/Arrow.h"
#include "entity/Player.h"
#include "entity/mob/Animals.h"
#include "entity/mob/Monsters.h"
#include "entity/model/HumanoidModel.h"
#include "entity/model/MobModels.h"
#include "game/DyeColor.h"
#include "game/Item.h"
#include "render/ClientAssets.h"
#include "render/EntityRenderer.h"
#include "resources/ItemDefinitions.h"
#include "resources/ResourcePack.h"
#include "world/level/Level.h"

namespace mc {
namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;

// Vanilla OverlayTexture: the red of a hurt mob (30%), or white by a progress 0..1 (up to
// 75%, in fifteenths).
uint32_t whiteOverlay(float progress) {
    const int u = int(progress * 15.0f);
    const uint32_t a = uint32_t(float(u) / 15.0f * 0.75f * 255.0f + 0.5f);
    return a == 0 ? 0u : (a << 24) | 0xFFFFFFu;
}

// Vanilla Sheep.getColor: white wool a light gray, the dyes darkened to three quarters.
uint32_t woolTint(int color) {
    if (color == 0) return 0xE6E6E6;
    const uint32_t c = kDyeTextureColors[size_t(color)];
    auto ch = [&](int shift) { return uint32_t(std::floor(float((c >> shift) & 255) * 0.75f)); };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// The equipment asset of a worn item (vanilla equippable.asset_id).
std::string_view armorMaterial(std::string_view id) {
    id = shortId(id);
    if (id == "turtle_helmet") return "turtle_scute";
    const size_t underscore = id.find('_');
    const std::string_view m = id.substr(0, underscore);
    if (m == "golden") return "gold";
    if (m == "leather" || m == "chainmail" || m == "iron" || m == "diamond" || m == "netherite") return m;
    return {};
}

} // namespace

void EntityRenderer::initMobModels() {
    auto add = [&](const std::string& name, ModelDefinition d) { mobModels_[name] = {d, d}; };
    add("pig", pigModel());
    add("pig_baby", babyModel(pigModel(), false, 4.0f, 4.0f, {"head"}));
    add("cow", cowModel());
    add("cow_baby", babyModel(cowModel(), false, 8.0f, 6.0f, {"head"}));
    add("sheep", sheepModel());
    add("sheep_baby", babyModel(sheepModel(), false, 8.0f, 4.0f, {"head"}));
    add("sheep_wool", sheepWoolModel());
    add("sheep_wool_baby", babyModel(sheepWoolModel(), false, 8.0f, 4.0f, {"head"}));
    add("chicken", chickenModel());
    add("chicken_baby", babyModel(chickenModel(), false, 5.0f, 2.0f, {"head"}));
    add("zombie", humanoidModel(0.0f, 64, 64));
    add("zombie_baby", babyModel(humanoidModel(0.0f, 64, 64), true, 16.0f, 0.0f, {"head"}));
    add("skeleton", skeletonModel());
    add("creeper", creeperModel());
    add("creeper_armor", creeperModel(2.0f));
    add("spider", spiderModel());
    // Vanilla createBaseArmorMesh: the outer layer (helmet, chestplate, boots) 1.0 out, the
    // inner (leggings) 0.5, legs 0.1 tighter.
    for (const auto& [name, grow] : {std::pair{"armor_outer", 1.0f}, std::pair{"armor_inner", 0.5f}}) {
        ModelDefinition a = humanoidModel(grow, 64, 32);
        a.root.addChild("right_leg", {{-1.9f, 12, 0}}).box({0, 16}, {-2, 0, -2}, {4, 12, 4}, grow - 0.1f);
        a.root.addChild("left_leg", {{1.9f, 12, 0}}).box({0, 16}, {-2, 0, -2}, {4, 12, 4}, grow - 0.1f, true);
        add(name, a);
        add(std::string(name) + "_baby", babyModel(a, true, 16.0f, 0.0f, {"head"}));
    }
}

ModelDefinition& EntityRenderer::posedModel(const std::string& name) {
    MobModel& m = mobModels_.at(name);
    resetPose(m.posed.root, m.rest.root);
    return m.posed;
}

void EntityRenderer::submitArmor(std::span<const ItemStack> armor, const ModelPart& body, bool baby,
                                 const glm::dvec3& origin, const glm::mat4& transform, glm::vec2 light, uint32_t overlay) {
    // Vanilla HumanoidArmorLayer: chest, legs, feet, head; each slot shows its parts.
    static constexpr int kOrder[4] = {2, 1, 0, 3}; // indices into feet..head
    for (const int slot : kOrder) {
        if (size_t(slot) >= armor.size() || armor[size_t(slot)].empty()) continue;
        const ItemStack& piece = armor[size_t(slot)];
        if (piece.item->equipment != EquipmentSlot(int(EquipmentSlot::Feet) + slot)) continue;
        const std::string_view material = armorMaterial(piece.item->id);
        if (material.empty()) continue;
        const bool legs = slot == 1;
        ModelDefinition& m = posedModel(std::string(legs ? "armor_inner" : "armor_outer") + (baby ? "_baby" : ""));
        copyHumanoidPose(m.root, body);
        for (ModelPart& p : m.root.children) p.visible = false;
        auto show = [&](const char* name) { m.root.find(name)->visible = true; };
        if (slot == 3) show("head");
        if (slot == 2) show("body"), show("right_arm"), show("left_arm");
        if (slot == 1) show("body"), show("right_leg"), show("left_leg");
        if (slot == 0) show("right_leg"), show("left_leg");
        const std::string base = std::string("minecraft:entity/equipment/") + (legs ? "humanoid_leggings/" : "humanoid/");
        // Leather: the undyed brown under its overlay.
        const bool leather = material == "leather";
        submitModel(m.root, m.texWidth, m.texHeight, base + std::string(material), origin, transform, light,
                    leather ? 0xA06540u : 0xFFFFFFu, 1.0f, overlay);
        if (leather)
            submitModel(m.root, m.texWidth, m.texHeight, base + "leather_overlay", origin, transform, light, 0xFFFFFF, 1.0f,
                        overlay);
    }
}

void EntityRenderer::submitHeldItem(const ItemStack& stack, const ModelPart& arm, bool left, float armShift,
                                    const glm::dvec3& origin, const glm::mat4& transform, glm::vec2 light, float holderYaw,
                                    int useDuration) {
    if (stack.empty()) return;
    // ItemInHandLayer: from the arm (thin arms shifted to their side), turned to hang from the hand.
    PartPose pose = arm.pose;
    pose.offset.x += left ? -armShift : armShift;
    glm::mat4 t = transform * poseMatrix(pose);
    t = glm::rotate(t, glm::radians(-90.0f), glm::vec3(1, 0, 0));
    t = glm::rotate(t, glm::radians(180.0f), glm::vec3(0, 1, 0));
    t = glm::translate(t, glm::vec3((left ? -1.0f : 1.0f) / 16.0f, 0.125f, -0.625f));
    ItemModelContext ctx;
    ctx.display = left ? DisplayContext::ThirdPersonLeft : DisplayContext::ThirdPersonRight;
    ctx.count = stack.count;
    ctx.maxStack = stack.maxStack();
    ctx.holderPos = origin;
    ctx.holderYaw = holderYaw;
    ctx.usingItem = useDuration > 0;
    ctx.useDuration = useDuration;
    layers_.clear();
    assets_->itemDefinitions().evaluate(stack.item->id, ctx, &assets_->grassColormap(), layers_);
    itemQuads_.clear();
    buildItemQuads(layers_, ctx.display, assets_->blockEntityModels(), assets_->blockAtlas(), itemQuads_);
    submitItem(itemQuads_, origin, t, light);
}

void EntityRenderer::submitFlame(const glm::dvec3& feet, float width, float height) {
    // Vanilla renderFlame: layers of fire_0 / fire_1 facing the camera, 1.4 of the width
    // across, stacked up the body, each a little smaller and nearer.
    const Atlas& atlas = assets_->blockAtlas();
    const SpriteInfo* sprites[2] = {&atlas.sprite("minecraft:block/fire_0"), &atlas.sprite("minecraft:block/fire_1")};
    const float f = width * 1.4f;
    glm::vec2 right(-cameraRight_.x, -cameraRight_.z);
    if (glm::dot(right, right) < 1e-8f) right = {1.0f, 0.0f};
    right = glm::normalize(right);
    const glm::vec3 xAxis(right.x, 0.0f, right.y), zAxis(-right.y, 0.0f, right.x), yAxis(0.0f, 1.0f, 0.0f);
    const glm::vec3 base(feet - cameraPos_);
    float half = 0.5f, rise = 0.0f, depth = 0.0f, left = height / f;
    const float z0 = 0.3f - float(int(left)) * 0.02f;
    const size_t first = vertices_.size();
    const uint32_t color = 0xFFFFFFFFu;
    for (int i = 0; left > 0.0f; ++i) {
        const SpriteInfo& s = *sprites[i % 2];
        float u0 = s.u0, u1 = s.u1;
        if (i / 2 % 2 == 0) std::swap(u0, u1);
        auto at = [&](float x, float y) {
            return base + (xAxis * x + yAxis * y + zAxis * (z0 + depth)) * f;
        };
        const Vertex v[4] = {{at(-half, -rise), {u1, s.v1}, color, {15, 15}, 0},
                             {at(half, -rise), {u0, s.v1}, color, {15, 15}, 0},
                             {at(half, 1.4f - rise), {u0, s.v0}, color, {15, 15}, 0},
                             {at(-half, 1.4f - rise), {u1, s.v0}, color, {15, 15}, 0}};
        for (int k : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[k]);
        left -= 0.45f;
        rise -= 0.45f;
        half *= 0.9f;
        depth -= 0.03f;
    }
    addBatch(&assets_->blockAtlasTexture(), first, Pass::Cutout);
}

void EntityRenderer::submitMob(const Mob& mob, const Level& level, float pt, int skyDarken) {
    const std::string_view type = shortId(mob.type());
    const glm::dvec3 feet = glm::mix(mob.oldPos, mob.pos, double(pt));
    HumanoidPose hp = humanoidPoseOf(mob, pt);
    if (mob.isBaby()) hp.walkPosition *= 3.0f; // vanilla: babies' legs go three times as fast
    MobPose mp;
    mp.ageInTicks = hp.ageInTicks;
    mp.headYaw = hp.headYaw;
    mp.headPitch = hp.headPitch;
    mp.walkPosition = hp.walkPosition;
    mp.walkSpeed = hp.walkSpeed;
    mp.attackTime = hp.attackTime;
    mp.aggressive = mob.aggressive;
    mp.ageScale = mob.ageScale();
    // Lit at the eyes; burning mobs glow.
    const uint8_t l = level.lightAt(BlockPos::containing(feet + glm::dvec3(0.0, mob.eyeHeight(), 0.0)));
    glm::vec2 light(float(l & 15), float(l >> 4));
    if (mob.onFire()) light.x = 15.0f;
    // Vanilla setupRotations, scale and the -1.501 drop; the dead fall over (spiders onto
    // their backs).
    const float deathTime = mob.deathTime > 0 ? float(mob.deathTime) + pt : 0.0f;
    const bool spider = type == "spider";
    glm::vec3 scale(1.0f);
    uint32_t overlay = (mob.hurtTime > 0 || mob.deathTime > 0) ? kHurtOverlay : 0u;
    const auto* creeper = dynamic_cast<const Creeper*>(&mob);
    if (creeper) {
        // Vanilla CreeperRenderer.scale: it swells and trembles, flashing white.
        float f = creeper->swelling(pt);
        const float wobble = 1.0f + std::sin(f * 100.0f) * f * 0.01f;
        f = std::clamp(f, 0.0f, 1.0f);
        f *= f;
        f *= f;
        scale = glm::vec3((1.0f + f * 0.4f) * wobble, (1.0f + f * 0.1f) / wobble, (1.0f + f * 0.4f) * wobble);
        const float s = creeper->swelling(pt);
        const float white = int(s * 10.0f) % 2 == 0 ? 0.0f : std::clamp(s, 0.5f, 1.0f);
        if (!overlay) overlay = whiteOverlay(white);
    }
    glm::mat4 m = glm::rotate(glm::mat4(1.0f), (180.0f - hp.bodyYaw) * kDegToRad, glm::vec3(0, 1, 0));
    if (deathTime > 0.0f) m = glm::rotate(m, deathFlipDegrees(deathTime) * (spider ? 2.0f : 1.0f) * kDegToRad, glm::vec3(0, 0, 1));
    m = glm::scale(m, glm::vec3(-1.0f, -1.0f, 1.0f) * scale);
    m = glm::translate(m, glm::vec3(0.0f, -1.501f, 0.0f));
    const bool baby = mob.isBaby();
    auto texture = [](std::string_view path) { return "minecraft:entity/" + std::string(path); };
    float shadow = 0.5f;
    if (type == "pig") {
        const auto& pig = static_cast<const Pig&>(mob);
        ModelDefinition& d = posedModel(baby ? "pig_baby" : "pig");
        animateQuadruped(d.root, mp);
        submitModel(d.root, d.texWidth, d.texHeight,
                    texture(pig.variant == FarmVariant::Warm ? "pig/warm_pig" : "pig/temperate_pig"), feet, m, light,
                    0xFFFFFF, 1.0f, overlay);
        shadow = 0.7f;
    } else if (type == "cow") {
        ModelDefinition& d = posedModel(baby ? "cow_baby" : "cow");
        animateQuadruped(d.root, mp);
        submitModel(d.root, d.texWidth, d.texHeight, texture("cow/temperate_cow"), feet, m, light, 0xFFFFFF, 1.0f, overlay);
        shadow = 0.7f;
    } else if (type == "sheep") {
        const auto& sheep = static_cast<const Sheep&>(mob);
        const float eatPos = sheep.headEatPosition(pt), eatAngle = sheep.headEatAngle(pt);
        ModelDefinition& d = posedModel(baby ? "sheep_baby" : "sheep");
        animateQuadruped(d.root, mp);
        animateSheepHead(d.root, eatPos, eatAngle, mp.ageScale);
        submitModel(d.root, d.texWidth, d.texHeight, texture("sheep/sheep"), feet, m, light, 0xFFFFFF, 1.0f, overlay);
        // Dyed sheep show their color underneath (1.21.5 SheepWoolUndercoatLayer), and the
        // wool over it until sheared.
        if (sheep.color != 0)
            submitModel(d.root, d.texWidth, d.texHeight, texture("sheep/sheep_wool_undercoat"), feet, m, light,
                        woolTint(sheep.color), 1.0f, overlay);
        if (!sheep.sheared) {
            ModelDefinition& w = posedModel(baby ? "sheep_wool_baby" : "sheep_wool");
            animateQuadruped(w.root, mp);
            animateSheepHead(w.root, eatPos, eatAngle, mp.ageScale);
            submitModel(w.root, w.texWidth, w.texHeight, texture("sheep/sheep_wool"), feet, m, light, woolTint(sheep.color),
                        1.0f, overlay);
        }
        shadow = 0.7f;
    } else if (type == "chicken") {
        const auto& chicken = static_cast<const Chicken&>(mob);
        ModelDefinition& d = posedModel(baby ? "chicken_baby" : "chicken");
        const float flap = chicken.oFlap + (chicken.flap - chicken.oFlap) * pt;
        const float flapSpeed = chicken.oFlapSpeed + (chicken.flapSpeed - chicken.oFlapSpeed) * pt;
        animateChicken(d.root, mp, flap, flapSpeed);
        submitModel(d.root, d.texWidth, d.texHeight,
                    texture(chicken.variant == FarmVariant::Warm ? "chicken/warm_chicken" : "chicken/temperate_chicken"),
                    feet, m, light, 0xFFFFFF, 1.0f, overlay);
        shadow = 0.3f;
    } else if (type == "zombie" || type == "skeleton") {
        const bool skeleton = type == "skeleton";
        const auto* sk = dynamic_cast<const Skeleton*>(&mob);
        const bool bow = sk && sk->holdingBow();
        hp.rightArmItem = !mob.mainHand().empty();
        hp.leftArmItem = !mob.equipment[1].empty();
        hp.bowDrawn = skeleton && bow && mob.aggressive;
        ModelDefinition& d = posedModel(skeleton ? "skeleton" : (baby ? "zombie_baby" : "zombie"));
        animateHumanoid(d.root, hp);
        if (skeleton) animateSkeleton(d.root, mp, bow, hp.rightArmItem);
        else animateZombie(d.root, mp);
        submitModel(d.root, d.texWidth, d.texHeight, texture(skeleton ? "skeleton/skeleton" : "zombie/zombie"), feet, m,
                    light, 0xFFFFFF, 1.0f, overlay);
        auto& eq = const_cast<Mob&>(mob).equipment;
        submitArmor(std::span<const ItemStack>(eq).subspan(2, 4), d.root, baby, feet, m, light, overlay);
        const int using_ = mob.ticksUsingItem();
        submitHeldItem(mob.mainHand(), *d.root.find("right_arm"), false, skeleton ? 1.0f : 0.0f, feet, m, light, mob.yaw,
                       using_);
        submitHeldItem(mob.equipment[1], *d.root.find("left_arm"), true, skeleton ? 1.0f : 0.0f, feet, m, light, mob.yaw, 0);
    } else if (creeper) {
        ModelDefinition& d = posedModel("creeper");
        animateCreeper(d.root, mp);
        submitModel(d.root, d.texWidth, d.texHeight, texture("creeper/creeper"), feet, m, light, 0xFFFFFF, 1.0f, overlay);
        if (creeper->powered) {
            // Vanilla CreeperPowerLayer: the energy swirl, scrolling, added on half bright.
            ModelDefinition& a = posedModel("creeper_armor");
            animateCreeper(a.root, mp);
            const size_t first = vertices_.size();
            submitModel(a.root, a.texWidth, a.texHeight, texture("creeper/creeper_armor"), feet, m, {15.0f, 15.0f},
                        0x808080, 1.0f, 0);
            const float scroll = std::fmod(hp.ageInTicks * 0.01f, 1.0f);
            for (size_t i = first; i < vertices_.size(); ++i) vertices_[i].uv += glm::vec2(scroll, scroll);
            if (!batches_.empty() && batches_.back().first <= first) batches_.back().pass = Pass::Additive;
        }
    } else if (spider) {
        ModelDefinition& d = posedModel("spider");
        animateSpider(d.root, mp);
        submitModel(d.root, d.texWidth, d.texHeight, texture("spider/spider"), feet, m, light, 0xFFFFFF, 1.0f, overlay);
        // Vanilla SpiderEyesLayer: glowing eyes, added on full bright.
        const size_t first = vertices_.size();
        submitModel(d.root, d.texWidth, d.texHeight, texture("spider_eyes"), feet, m, {15.0f, 15.0f}, 0xFFFFFF, 1.0f, 0);
        if (vertices_.size() > first && !batches_.empty()) batches_.back().pass = Pass::Additive;
        shadow = 0.8f;
    }
    if (mob.onFire()) submitFlame(feet, mob.width, mob.height);
    submitShadow(level, feet, shadow * mob.ageScale(), skyDarken);
}

void EntityRenderer::submitArrow(const Arrow& arrow, const Level& level, float pt) {
    // Vanilla ArrowRenderer: two crossed fletchings and the tail, 0.05625 to the texel.
    const glm::dvec3 at = glm::mix(arrow.oldPos, arrow.pos, double(pt));
    const uint8_t l = level.lightAt(BlockPos::containing(at));
    const glm::vec2 light(float(l & 15), float(l >> 4));
    const float yaw = rotLerp(pt, arrow.oldYaw, arrow.yaw), pitch = arrow.oldPitch + (arrow.pitch - arrow.oldPitch) * pt;
    glm::mat4 m = glm::rotate(glm::mat4(1.0f), (yaw - 90.0f) * kDegToRad, glm::vec3(0, 1, 0));
    m = glm::rotate(m, pitch * kDegToRad, glm::vec3(0, 0, 1));
    const float shake = float(arrow.shakeTime) - pt;
    if (shake > 0.0f) m = glm::rotate(m, -std::sin(shake * 3.0f) * shake * kDegToRad, glm::vec3(0, 0, 1));
    m = glm::rotate(m, 45.0f * kDegToRad, glm::vec3(1, 0, 0));
    m = glm::scale(m, glm::vec3(0.05625f));
    m = glm::translate(m, glm::vec3(-4.0f, 0.0f, 0.0f));
    const Texture& tex = texture("minecraft:entity/projectiles/arrow");
    const size_t first = vertices_.size();
    const glm::vec3 base(at - cameraPos_);
    const uint32_t color = 0xFFFFFFFFu;
    auto quad = [&](const glm::mat4& t, const glm::vec3 (&p)[4], const glm::vec2 (&uv)[4]) {
        Vertex v[4];
        for (int k = 0; k < 4; ++k) v[k] = {base + glm::vec3(t * glm::vec4(p[k], 1.0f)), uv[k], color, light, 0};
        for (int k : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[k]);
    };
    // The tail, seen from behind and in front.
    quad(m, {{-7, -2, -2}, {-7, -2, 2}, {-7, 2, 2}, {-7, 2, -2}},
         {{0.0f, 0.15625f}, {0.15625f, 0.15625f}, {0.15625f, 0.3125f}, {0.0f, 0.3125f}});
    quad(m, {{-7, 2, -2}, {-7, 2, 2}, {-7, -2, 2}, {-7, -2, -2}},
         {{0.0f, 0.15625f}, {0.15625f, 0.15625f}, {0.15625f, 0.3125f}, {0.0f, 0.3125f}});
    glm::mat4 r = m;
    for (int k = 0; k < 4; ++k) {
        r = glm::rotate(r, 90.0f * kDegToRad, glm::vec3(1, 0, 0));
        quad(r, {{-8, -2, 0}, {8, -2, 0}, {8, 2, 0}, {-8, 2, 0}}, {{0.0f, 0.0f}, {0.5f, 0.0f}, {0.5f, 0.15625f}, {0.0f, 0.15625f}});
    }
    addBatch(&tex, first, Pass::Cutout);
}

} // namespace mc
