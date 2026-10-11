#include "entity/model/BlockEntityModels.h"

#include <array>

#include <glm/gtc/matrix_transform.hpp>

#include "resources/ResourcePack.h"

namespace mc {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr uint8_t kNorthFace = 1u << 2; // Direction order D, U, N, S, W, E

const glm::vec3 kX(1, 0, 0), kY(0, 1, 0), kZ(0, 0, 1);

// Vanilla Direction.toYRot: south 0, west 90, north 180, east 270.
float yRotOf(Direction d) {
    switch (d) {
    case Direction::West: return 90.0f;
    case Direction::North: return 180.0f;
    case Direction::East: return 270.0f;
    default: return 0.0f;
    }
}

Direction facingOf(const BlockRegistry& reg, StateId s) {
    return directionFromName(reg.value(s, "facing")).value_or(Direction::North);
}

// RotationSegment.convertToDegrees: 16 rotations of 22.5 degrees.
float rotationDegrees(const BlockRegistry& reg, StateId s) { return float(reg.intValue(s, "rotation")) * 22.5f; }

std::string_view stripSuffix(std::string_view s, std::string_view suffix) {
    return s.ends_with(suffix) ? s.substr(0, s.size() - suffix.size()) : s;
}

// Turns a block-space model about the block center (vanilla: translate .5,
// rotate -yRot about Y, translate back).
glm::mat4 facingTransform(Direction facing) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f));
    m = glm::rotate(m, glm::radians(-yRotOf(facing)), kY);
    return glm::translate(m, glm::vec3(-0.5f));
}

// --- Layer definitions (vanilla LayerDefinitions, model units) ---

// ChestModel (64x64), closed. 0 single, 1 double left, 2 double right.
ModelDefinition chestModel(int variant) {
    ModelDefinition d;
    ModelPart& root = d.root;
    if (variant == 0) {
        root.addChild("bottom").box({0, 19}, {1, 0, 1}, {14, 10, 14});
        root.addChild("lid", {{0, 9, 1}}).box({0, 0}, {1, 0, 0}, {14, 5, 14});
        root.addChild("lock", {{0, 9, 1}}).box({0, 0}, {7, -2, 14}, {2, 4, 1});
    } else if (variant == 1) {
        root.addChild("bottom").box({0, 19}, {0, 0, 1}, {15, 10, 14});
        root.addChild("lid", {{0, 9, 1}}).box({0, 0}, {0, 0, 0}, {15, 5, 14});
        root.addChild("lock", {{0, 9, 1}}).box({0, 0}, {0, -2, 14}, {1, 4, 1});
    } else {
        root.addChild("bottom").box({0, 19}, {1, 0, 1}, {15, 10, 14});
        root.addChild("lid", {{0, 9, 1}}).box({0, 0}, {1, 0, 0}, {15, 5, 14});
        root.addChild("lock", {{0, 9, 1}}).box({0, 0}, {15, -2, 14}, {1, 4, 1});
    }
    return d;
}

// BedRenderer head and foot layers (64x64).
ModelDefinition bedModel(bool head) {
    ModelDefinition d;
    ModelPart& root = d.root;
    if (head) {
        root.addChild("main").box({0, 0}, {0, 0, 0}, {16, 16, 6});
        root.addChild("left_leg", {{0, 0, 0}, {kPi / 2, 0, kPi / 2}}).box({50, 6}, {0, 6, 0}, {3, 3, 3});
        root.addChild("right_leg", {{0, 0, 0}, {kPi / 2, 0, kPi}}).box({50, 18}, {-16, 6, 0}, {3, 3, 3});
    } else {
        root.addChild("main").box({0, 22}, {0, 0, 0}, {16, 16, 6});
        root.addChild("left_leg", {{0, 0, 0}, {kPi / 2, 0, 0}}).box({50, 0}, {0, 6, -16}, {3, 3, 3});
        root.addChild("right_leg", {{0, 0, 0}, {kPi / 2, 0, kPi * 3 / 2}}).box({50, 12}, {-16, 6, -16}, {3, 3, 3});
    }
    return d;
}

// SignRenderer.createSignLayer (64x32): the board, plus the stick when standing.
ModelDefinition signModel(bool standing) {
    ModelDefinition d;
    d.texHeight = 32;
    d.root.addChild("sign").box({0, 0}, {-12, -14, -1}, {24, 12, 2});
    if (standing) d.root.addChild("stick").box({0, 14}, {-1, -2, -1}, {2, 14, 2});
    return d;
}

// HangingSignRenderer.createHangingSignLayer (64x32).
enum class HangingAttachment { Wall, Ceiling, CeilingMiddle };
ModelDefinition hangingSignModel(HangingAttachment type) {
    ModelDefinition d;
    d.texHeight = 32;
    ModelPart& root = d.root;
    root.addChild("board").box({0, 12}, {-7, 0, -1}, {14, 10, 2});
    if (type == HangingAttachment::Wall) root.addChild("plank").box({0, 0}, {-8, -6, -2}, {16, 2, 4});
    if (type == HangingAttachment::Wall || type == HangingAttachment::Ceiling) {
        ModelPart& chains = root.addChild("normalChains");
        chains.addChild("chainL1", {{-5, -6, 0}, {0, -kPi / 4, 0}}).box({0, 6}, {-1.5f, 0, 0}, {3, 6, 0});
        chains.addChild("chainL2", {{-5, -6, 0}, {0, kPi / 4, 0}}).box({6, 6}, {-1.5f, 0, 0}, {3, 6, 0});
        chains.addChild("chainR1", {{5, -6, 0}, {0, -kPi / 4, 0}}).box({0, 6}, {-1.5f, 0, 0}, {3, 6, 0});
        chains.addChild("chainR2", {{5, -6, 0}, {0, kPi / 4, 0}}).box({6, 6}, {-1.5f, 0, 0}, {3, 6, 0});
    } else {
        root.addChild("vChains").box({14, 6}, {-6, -6, 0}, {12, 6, 0});
    }
    return d;
}

// BannerModel (pole and bar) and BannerFlagModel (64x64). The flag swings with
// game time in vanilla; it rests here at the swing's mean angle.
ModelDefinition bannerModel(bool standing) {
    ModelDefinition d;
    if (standing) d.root.addChild("pole").box({44, 0}, {-1, -42, -1}, {2, 42, 2});
    d.root.addChild("bar").box({0, 42}, {-10, standing ? -44.0f : -20.5f, standing ? -1.0f : 9.5f}, {20, 2, 2});
    return d;
}
ModelDefinition bannerFlagModel(bool standing) {
    ModelDefinition d;
    const PartPose pose{{0, standing ? -44.0f : -20.5f, standing ? 0.0f : 10.5f}, {-0.0125f * kPi, 0, 0}};
    d.root.addChild("flag", pose).box({0, 0}, {-10, 0, -2}, {20, 40, 1});
    return d;
}

// SkullModel: mob heads (64x32), humanoid heads with a hat layer (64x64).
ModelDefinition skullModel(bool humanoid) {
    ModelDefinition d;
    d.texHeight = humanoid ? 64 : 32;
    ModelPart& head = d.root.addChild("head");
    head.box({0, 0}, {-4, -8, -4}, {8, 8, 8});
    if (humanoid) head.addChild("hat").box({32, 0}, {-4, -8, -4}, {8, 8, 8}, 0.25f);
    return d;
}

// PiglinHeadModel (PiglinModel.addHead, 64x64), ears at their resting angle.
ModelDefinition piglinHeadModel() {
    ModelDefinition d;
    ModelPart& head = d.root.addChild("head");
    head.box({0, 0}, {-5, -8, -4}, {10, 8, 8})
        .box({31, 1}, {-2, -4, -5}, {4, 4, 1})
        .box({2, 4}, {2, -2, -5}, {1, 2, 1})
        .box({2, 0}, {-3, -2, -5}, {1, 2, 1});
    head.addChild("left_ear", {{4.5f, -6, 0}, {0, 0, -0.7f}}).box({51, 6}, {0, 0, -2}, {1, 5, 4});
    head.addChild("right_ear", {{-4.5f, -6, 0}, {0, 0, 0.7f}}).box({39, 6}, {-1, 0, -2}, {1, 5, 4});
    return d;
}

// DragonHeadModel (256x256), jaw at its resting angle. The head part is placed
// 5.99 units up and scaled by 0.75 (PartPose.offset(0, -7.99, 0).scaled(0.75)).
ModelDefinition dragonHeadModel() {
    ModelDefinition d;
    d.texWidth = d.texHeight = 256;
    ModelPart& head = d.root.addChild("head", {{0, -7.986666f * 0.75f, 0}, {0, 0, 0}, glm::vec3(0.75f)});
    head.box({176, 44}, {-6, -1, -24}, {12, 5, 16})   // upper lip
        .box({112, 30}, {-8, -8, -10}, {16, 16, 16})  // upper head
        .box({0, 0}, {-5, -12, -4}, {2, 4, 6}, 0, true) // scale
        .box({112, 0}, {-5, -3, -22}, {2, 2, 4}, 0, true) // nostril
        .box({0, 0}, {3, -12, -4}, {2, 4, 6})
        .box({112, 0}, {3, -3, -22}, {2, 2, 4});
    head.addChild("jaw", {{0, 4, -8}, {0.2f, 0, 0}}).box({176, 65}, {-6, 0, -16}, {12, 4, 16});
    return d;
}

// ShulkerModel shell (64x64), closed.
ModelDefinition shulkerBoxModel() {
    ModelDefinition d;
    d.root.addChild("lid", {{0, 24, 0}}).box({0, 0}, {-8, -16, -8}, {16, 12, 16});
    d.root.addChild("base", {{0, 24, 0}}).box({0, 28}, {-8, -8, -8}, {16, 8, 16});
    return d;
}

// BellRenderer body (32x32), at rest.
ModelDefinition bellModel() {
    ModelDefinition d;
    d.texWidth = d.texHeight = 32;
    ModelPart& body = d.root.addChild("bell_body", {{8, 12, 8}});
    body.box({0, 0}, {-3, -6, -3}, {6, 7, 6});
    body.addChild("bell_base", {{-8, -12, -8}}).box({0, 13}, {4, 4, 4}, {8, 2, 8});
    return d;
}

// DecoratedPotRenderer base layer (32x32) and side layer (16x16).
ModelDefinition decoratedPotBaseModel() {
    ModelDefinition d;
    d.texWidth = d.texHeight = 32;
    d.root.addChild("neck", {{0, 37, 16}, {kPi, 0, 0}})
        .box({0, 0}, {4, 17, 4}, {8, 3, 8}, -0.1f)
        .box({0, 5}, {5, 20, 5}, {6, 1, 6}, 0.2f);
    d.root.addChild("top", {{1, 16, 1}}).box({-14, 13}, {0, 0, 0}, {14, 0, 14});
    d.root.addChild("bottom", {{1, 0, 1}}).box({-14, 13}, {0, 0, 0}, {14, 0, 14});
    return d;
}
ModelDefinition decoratedPotSidesModel() {
    ModelDefinition d;
    d.texWidth = d.texHeight = 16;
    auto side = [&](const char* name, PartPose pose) {
        d.root.addChild(name, pose).box({1, 0}, {0, 0, 0}, {14, 16, 0}).onlyFaces(kNorthFace);
    };
    side("back", {{15, 16, 1}, {0, 0, kPi}});
    side("left", {{1, 16, 1}, {0, -kPi / 2, kPi}});
    side("right", {{15, 16, 15}, {0, kPi / 2, kPi}});
    side("front", {{1, 16, 15}, {kPi, 0, 0}});
    return d;
}

// ConduitRenderer shell (32x16), shown while the conduit is inactive.
ModelDefinition conduitShellModel() {
    ModelDefinition d;
    d.texWidth = 32;
    d.texHeight = 16;
    d.root.addChild("shell").box({0, 0}, {-3, -3, -3}, {6, 6, 6});
    return d;
}

constexpr std::array<std::string_view, 16> kDyes = {"white", "orange", "magenta", "light_blue", "yellow", "lime",
                                                    "pink",  "gray",   "light_gray", "cyan",     "purple", "blue",
                                                    "brown", "green",  "red",     "black"};
constexpr std::array<std::string_view, 12> kWoods = {"oak",    "spruce", "birch",    "jungle",   "acacia",  "dark_oak",
                                                     "mangrove", "cherry", "pale_oak", "bamboo", "crimson", "warped"};

enum class SkullKind { Skeleton, WitherSkeleton, Zombie, Creeper, Player, Piglin, Dragon, Count };
constexpr std::array<std::string_view, size_t(SkullKind::Count)> kSkullTextures = {
    "minecraft:entity/skeleton/skeleton", "minecraft:entity/skeleton/wither_skeleton", "minecraft:entity/zombie/zombie",
    "minecraft:entity/creeper/creeper",   "minecraft:entity/player/wide/steve",         "minecraft:entity/piglin/piglin",
    "minecraft:entity/enderdragon/dragon"};

std::optional<SkullKind> skullKind(std::string_view id) {
    id = stripSuffix(stripSuffix(stripSuffix(stripSuffix(id, "_wall_skull"), "_skull"), "_wall_head"), "_head");
    if (id == "skeleton") return SkullKind::Skeleton;
    if (id == "wither_skeleton") return SkullKind::WitherSkeleton;
    if (id == "zombie") return SkullKind::Zombie;
    if (id == "creeper") return SkullKind::Creeper;
    if (id == "player") return SkullKind::Player;
    if (id == "piglin") return SkullKind::Piglin;
    if (id == "dragon") return SkullKind::Dragon;
    return std::nullopt;
}

} // namespace

uint32_t dyeTextureColor(std::string_view dye) {
    static constexpr std::array<uint32_t, 16> kColors = {0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F,
                                                         0xF38BAA, 0x474F52, 0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA,
                                                         0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21};
    for (size_t i = 0; i < kDyes.size(); ++i)
        if (kDyes[i] == dye) return kColors[i];
    return kColors[0];
}

struct BlockEntityModels::Impl {
    ModelDefinition chest[3];
    ModelDefinition bedHead = bedModel(true), bedFoot = bedModel(false);
    ModelDefinition sign = signModel(true), wallSign = signModel(false);
    ModelDefinition hangingSign[3] = {hangingSignModel(HangingAttachment::Wall), hangingSignModel(HangingAttachment::Ceiling),
                                      hangingSignModel(HangingAttachment::CeilingMiddle)};
    ModelDefinition banner = bannerModel(true), wallBanner = bannerModel(false);
    ModelDefinition bannerFlag = bannerFlagModel(true), wallBannerFlag = bannerFlagModel(false);
    ModelDefinition mobSkull = skullModel(false), humanoidSkull = skullModel(true);
    ModelDefinition piglinHead = piglinHeadModel(), dragonHead = dragonHeadModel();
    ModelDefinition shulkerBox = shulkerBoxModel();
    ModelDefinition bell = bellModel();
    ModelDefinition potBase = decoratedPotBaseModel(), potSides = decoratedPotSidesModel();
    ModelDefinition conduitShell = conduitShellModel();

    const ModelDefinition& skull(SkullKind k) const {
        switch (k) {
        case SkullKind::Zombie:
        case SkullKind::Player: return humanoidSkull;
        case SkullKind::Piglin: return piglinHead;
        case SkullKind::Dragon: return dragonHead;
        default: return mobSkull;
        }
    }
};

BlockEntityModels::BlockEntityModels() : impl_(std::make_unique<Impl>()) {
    for (int i = 0; i < 3; ++i) impl_->chest[i] = chestModel(i);
}

BlockEntityModels::~BlockEntityModels() = default;

bool BlockEntityModels::handles(const Block& block) const {
    const std::string_view cls = block.className, id = shortId(block.id);
    return cls == "chest" || cls == "ender_chest" || cls == "bed" || cls == "sign" || cls == "wall_sign" ||
           cls == "hanging_sign" || cls == "wall_hanging_sign" || cls == "banner" || cls == "wall_banner" ||
           cls == "skull" || cls == "wall_skull" || cls == "bell" || cls == "shulker_box" || cls == "decorated_pot" ||
           id == "conduit";
}

void BlockEntityModels::pieces(const BlockRegistry& reg, StateId state, std::vector<BlockEntityPiece>& out) const {
    const Block& b = reg.blockOf(state);
    const std::string_view cls = b.className, id = shortId(b.id);
    const Impl& m = *impl_;
    auto add = [&](const ModelDefinition& model, std::string texture, const glm::mat4& transform) -> BlockEntityPiece& {
        BlockEntityPiece& p = out.emplace_back();
        p.model = &model;
        p.texture = std::move(texture);
        p.transform = transform;
        return p;
    };

    if (cls == "chest" || cls == "ender_chest") {
        const std::string_view type = reg.value(state, "type");
        const int variant = type == "left" ? 1 : type == "right" ? 2 : 0;
        const std::string base = id == "ender_chest" ? "ender" : id == "trapped_chest" ? "trapped" : "normal";
        add(m.chest[variant], "minecraft:entity/chest/" + base + (variant == 1 ? "_left" : variant == 2 ? "_right" : ""),
            facingTransform(facingOf(reg, state)));
    } else if (cls == "bed") {
        // BedRenderer.renderPiece: lie the model down, then turn it to face `facing`.
        glm::mat4 t = glm::translate(glm::mat4(1.0f), {0.0f, 0.5625f, 0.0f});
        t = glm::rotate(t, kPi / 2, kX);
        t = glm::translate(t, glm::vec3(0.5f));
        t = glm::rotate(t, glm::radians(180.0f + yRotOf(facingOf(reg, state))), kZ);
        t = glm::translate(t, glm::vec3(-0.5f));
        const bool head = reg.value(state, "part") == "head";
        add(head ? m.bedHead : m.bedFoot, "minecraft:entity/bed/" + std::string(stripSuffix(id, "_bed")), t);
    } else if (cls == "sign" || cls == "wall_sign") {
        // SignRenderer: centered, turned, wall signs pushed against the wall; scale 2/3 with y and z flipped.
        const bool standing = cls == "sign";
        glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f));
        t = glm::rotate(t, glm::radians(-(standing ? rotationDegrees(reg, state) : yRotOf(facingOf(reg, state)))), kY);
        if (!standing) t = glm::translate(t, {0.0f, -0.3125f, -0.4375f});
        t = glm::scale(t, {2.0f / 3.0f, -2.0f / 3.0f, -2.0f / 3.0f});
        const std::string wood(stripSuffix(stripSuffix(id, "_wall_sign"), "_sign"));
        add(standing ? m.sign : m.wallSign, "minecraft:entity/signs/" + wood, t);
    } else if (cls == "hanging_sign" || cls == "wall_hanging_sign") {
        // HangingSignRenderer.translateBase, then scale (1, -1, -1).
        const bool wall = cls == "wall_hanging_sign";
        const HangingAttachment type = wall                               ? HangingAttachment::Wall
                                       : reg.boolValue(state, "attached") ? HangingAttachment::CeilingMiddle
                                                                          : HangingAttachment::Ceiling;
        glm::mat4 t = glm::translate(glm::mat4(1.0f), {0.5f, 0.9375f, 0.5f});
        t = glm::rotate(t, glm::radians(-(wall ? yRotOf(facingOf(reg, state)) : rotationDegrees(reg, state))), kY);
        t = glm::translate(t, {0.0f, -0.3125f, 0.0f});
        t = glm::scale(t, {1.0f, -1.0f, -1.0f});
        const std::string wood(stripSuffix(stripSuffix(id, "_wall_hanging_sign"), "_hanging_sign"));
        add(m.hangingSign[size_t(type)], "minecraft:entity/signs/hanging/" + wood, t).doubleSided = true;
    } else if (cls == "banner" || cls == "wall_banner") {
        // BannerRenderer.renderBanner: pole and bar from banner_base, the flag from
        // the base pattern tinted with the banner's color (patterns: out of scope).
        const bool standing = cls == "banner";
        glm::mat4 t = glm::translate(glm::mat4(1.0f), {0.5f, 0.0f, 0.5f});
        t = glm::rotate(t, glm::radians(-(standing ? rotationDegrees(reg, state) : yRotOf(facingOf(reg, state)))), kY);
        t = glm::scale(t, {2.0f / 3.0f, -2.0f / 3.0f, -2.0f / 3.0f});
        add(standing ? m.banner : m.wallBanner, "minecraft:entity/banner_base", t);
        add(standing ? m.bannerFlag : m.wallBannerFlag, "minecraft:entity/banner/base", t).tint =
            dyeTextureColor(stripSuffix(stripSuffix(id, "_wall_banner"), "_banner"));
    } else if (cls == "skull" || cls == "wall_skull") {
        // SkullBlockRenderer.renderSkull: floor heads stand centered, wall heads sit
        // against the wall; scale (-1, -1, 1); the head turns by the rotation.
        const auto kind = skullKind(id);
        if (!kind) return;
        glm::mat4 t;
        float yRot;
        if (cls == "wall_skull") {
            const Direction f = facingOf(reg, state);
            const glm::ivec3 step = normal(f);
            t = glm::translate(glm::mat4(1.0f), {0.5f - float(step.x) * 0.25f, 0.25f, 0.5f - float(step.z) * 0.25f});
            yRot = yRotOf(opposite(f)); // RotationSegment.convertToSegment(facing.getOpposite())
        } else {
            t = glm::translate(glm::mat4(1.0f), {0.5f, 0.0f, 0.5f});
            yRot = rotationDegrees(reg, state);
        }
        t = glm::scale(t, {-1.0f, -1.0f, 1.0f});
        t = glm::rotate(t, glm::radians(yRot), kY); // head.yRot (the head parts pivot at the origin)
        add(m.skull(*kind), std::string(kSkullTextures[size_t(*kind)]), t);
    } else if (cls == "shulker_box") {
        // ShulkerBoxRenderer: centered, shrunk a hair, turned so the lid faces `facing`,
        // then y and z flipped with the model's 24-unit origin at the block bottom.
        glm::mat4 r(1.0f);
        switch (directionFromName(reg.value(state, "facing")).value_or(Direction::Up)) {
        case Direction::Down: r = glm::rotate(r, kPi, kX); break;
        case Direction::North: r = glm::rotate(glm::rotate(r, kPi / 2, kX), kPi, kZ); break;
        case Direction::South: r = glm::rotate(r, kPi / 2, kX); break;
        case Direction::West: r = glm::rotate(glm::rotate(r, kPi / 2, kX), kPi / 2, kZ); break;
        case Direction::East: r = glm::rotate(glm::rotate(r, kPi / 2, kX), -kPi / 2, kZ); break;
        default: break;
        }
        glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f));
        t = glm::scale(t, glm::vec3(0.9995f)) * r;
        t = glm::scale(t, {1.0f, -1.0f, -1.0f});
        t = glm::translate(t, {0.0f, -1.0f, 0.0f});
        const std::string_view color = stripSuffix(id, "_shulker_box");
        add(m.shulkerBox, color == "shulker_box" ? "minecraft:entity/shulker/shulker"
                                                 : "minecraft:entity/shulker/shulker_" + std::string(color),
            t);
    } else if (cls == "bell") {
        add(m.bell, "minecraft:entity/bell/bell_body", glm::mat4(1.0f)); // the frame is the block model
    } else if (cls == "decorated_pot") {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), {0.5f, 0.0f, 0.5f});
        t = glm::rotate(t, glm::radians(180.0f - yRotOf(facingOf(reg, state))), kY);
        t = glm::translate(t, {-0.5f, 0.0f, -0.5f});
        add(m.potBase, "minecraft:entity/decorated_pot/decorated_pot_base", t);
        add(m.potSides, "minecraft:entity/decorated_pot/decorated_pot_side", t); // no sherds
    } else if (id == "conduit") {
        add(m.conduitShell, "minecraft:entity/conduit/base", glm::translate(glm::mat4(1.0f), glm::vec3(0.5f)));
    }
}

std::vector<std::string> BlockEntityModels::textures() const {
    std::vector<std::string> t;
    for (const char* base : {"normal", "trapped"})
        for (const char* suffix : {"", "_left", "_right"}) t.push_back(std::string("minecraft:entity/chest/") + base + suffix);
    t.push_back("minecraft:entity/chest/ender");
    for (std::string_view dye : kDyes) {
        t.push_back("minecraft:entity/bed/" + std::string(dye));
        t.push_back("minecraft:entity/shulker/shulker_" + std::string(dye));
    }
    t.push_back("minecraft:entity/shulker/shulker");
    for (std::string_view wood : kWoods) {
        t.push_back("minecraft:entity/signs/" + std::string(wood));
        t.push_back("minecraft:entity/signs/hanging/" + std::string(wood));
    }
    t.push_back("minecraft:entity/banner_base");
    t.push_back("minecraft:entity/banner/base");
    for (std::string_view skull : kSkullTextures) t.emplace_back(skull);
    t.push_back("minecraft:entity/bell/bell_body");
    t.push_back("minecraft:entity/decorated_pot/decorated_pot_base");
    t.push_back("minecraft:entity/decorated_pot/decorated_pot_side");
    t.push_back("minecraft:entity/conduit/base");
    return t;
}

std::vector<ModelTextureUse> BlockEntityModels::textureUses() const {
    const Impl& m = *impl_;
    std::vector<ModelTextureUse> uses = {
        {"minecraft:entity/chest/normal", {&m.chest[0]}},
        {"minecraft:entity/chest/normal_left", {&m.chest[1]}},
        {"minecraft:entity/chest/normal_right", {&m.chest[2]}},
        {"minecraft:entity/chest/ender", {&m.chest[0]}},
        {"minecraft:entity/bed/red", {&m.bedHead, &m.bedFoot}},
        {"minecraft:entity/signs/oak", {&m.sign, &m.wallSign}},
        {"minecraft:entity/signs/hanging/oak", {&m.hangingSign[0], &m.hangingSign[1], &m.hangingSign[2]}},
        {"minecraft:entity/banner_base", {&m.banner, &m.wallBanner, &m.bannerFlag, &m.wallBannerFlag}},
        {"minecraft:entity/banner/base", {&m.bannerFlag, &m.wallBannerFlag}},
        {"minecraft:entity/shulker/shulker", {&m.shulkerBox}, false}, // also the shulker's head (M12)
        {"minecraft:entity/bell/bell_body", {&m.bell}},
        {"minecraft:entity/decorated_pot/decorated_pot_base", {&m.potBase}},
        {"minecraft:entity/decorated_pot/decorated_pot_side", {&m.potSides}},
        {"minecraft:entity/conduit/base", {&m.conduitShell}},
    };
    for (size_t k = 0; k < kSkullTextures.size(); ++k)
        uses.push_back({std::string(kSkullTextures[k]), {&m.skull(SkullKind(k))}, false});
    return uses;
}

} // namespace mc
