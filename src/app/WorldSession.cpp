#include "app/WorldSession.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <GLFW/glfw3.h>

#include "app/Client.h"
#include "core/Log.h"
#include "game/Item.h"
#include "game/Menu.h"
#include "game/Placement.h"
#include "gfx/Screenshot.h"
#include "render/BlockOverlay.h"
#include "render/ClientAssets.h"
#include "render/EntityRenderer.h"
#include "render/WorldRenderer.h"
#include "ui/Chat.h"
#include "ui/GuiGraphics.h"
#include "ui/Hud.h"
#include "ui/Screen.h"
#include "world/biome/Biome.h"
#include "world/block/BlockBehavior.h"
#include "world/block/BlockRegistry.h"
#include "world/gen/ChunkGenerator.h"
#include "world/gen/OverworldGenerator.h"
#include "world/gen/feature/BiomeDecorator.h"
#include "world/level/Environment.h"
#include "world/level/Level.h"
#include "world/light/LightEngine.h"
#include "world/storage/ChunkStorage.h"
#include "world/storage/WorldSave.h"

namespace mc {

WorldSession::WorldSession(Client& client, WorldParams params) : client_(client), params_(std::move(params)) {}

WorldSession::~WorldSession() {
    if (inventoryMenu_ && player_) inventoryMenu_->removed(*player_); // menus hand their items back
    save(true);
    // Jobs reference the level and renderer until they drain.
    if (client_.jobs) client_.jobs->waitIdle();
    while (client_.jobs && client_.jobs->drainMain(1000.0) > 0) client_.jobs->waitIdle();
    overlay_.reset();
    worldRenderer_.reset();
    player_.reset();
    level_.reset();
    overworld_.reset();
}

bool WorldSession::init() {
    const CliOptions& cli = client_.cli;
    BlockRegistry& registry = *client_.registry;
    std::string err;
    // A saved world keeps its settings; a new one takes them from the parameters.
    std::optional<WorldInfo> info;
    if (!params_.folder.empty()) {
        worldDir_ = cli.gameDir / "saves" / params_.folder;
        info = readWorldInfo(worldDir_);
        if (info) {
            params_.type = info->type;
            params_.seed = info->seed;
            if (!info->name.empty()) params_.name = info->name;
            params_.difficulty = difficultyFromName(info->difficulty).value_or(params_.difficulty);
            params_.cheats = info->cheats;
            params_.hardcore = info->hardcore;
            log::info("Loading world '{}' (type {}, seed {})", params_.folder, params_.type, params_.seed);
        } else {
            log::info("Creating world '{}' in {}", params_.folder, worldDir_.string());
        }
    }
    std::shared_ptr<const ChunkGenerator> generator;
    if (params_.type == "flat") {
        generator = std::make_shared<FlatGenerator>(registry);
    } else if (params_.type == "debug") {
        generator = std::make_shared<DebugGenerator>(registry);
    } else {
        auto decorator = std::make_shared<BiomeDecorator>();
        if (!decorator->load(cli.dataDir, registry, *client_.biomes, &err)) {
            int shown = 0;
            for (size_t start = 0; start < err.size() && shown < 30; ++shown) {
                const size_t end = err.find('\n', start);
                log::warn("features: {}", err.substr(start, end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
        overworld_ = std::make_shared<OverworldGenerator>(registry, *client_.biomes, params_.seed, std::move(decorator));
        generator = overworld_;
    }
    level_ = std::make_unique<Level>(registry, generator, *client_.jobs, params_.seed);
    if (!worldDir_.empty()) {
        level_->attachStorage(std::make_shared<ChunkStorage>(worldDir_, registry, client_.biomes.get()));
        if (info) level_->loadData(info->level);
    }
    level_->chunks().setLightEngine(std::make_shared<LightEngine>(registry));
    if (params_.commandLine) {
        if (cli.time) level_->setDayTime(*cli.time);
        level_->daylightCycle = !cli.freezeTime;
        showDebug = cli.debugOverlay;
    }
    worldRenderer_ = std::make_unique<WorldRenderer>(*client_.assets, registry,
                                                     params_.type == "default" ? client_.biomes.get() : nullptr);
    if (!worldRenderer_->init()) return false;
    worldRenderer_->mesherOptions().smoothLighting = client_.options.smoothLighting;
    worldRenderer_->gamma = client_.options.gamma;

    overlay_ = std::make_unique<BlockOverlay>();
    if (!overlay_->init(*client_.assets)) return false;
    hud_ = std::make_unique<Hud>();

    // The player stands at the spawn point; --pos places the camera (the eye) instead.
    player_ = std::make_unique<Player>();
    player_->setMode(gameModeFromName(params_.gameMode).value_or(GameMode::Survival));
    const std::optional<glm::dvec3> pos = params_.commandLine ? cli.pos : std::nullopt;
    if (pos) {
        glm::dvec3 eye = *pos;
        if (std::isnan(eye.y)) { // --pos X,~,Z: 12 blocks above the terrain
            const int x = int(std::floor(eye.x)), z = int(std::floor(eye.z));
            eye.y = (overworld_ ? overworld_->terrainTop(x, z) + 1 : generator->spawnPoint().y) + 12.0;
        }
        player_->pos = eye - glm::dvec3(0.0, player_->eyeHeight(), 0.0);
        // The float eye height leaves noise (y -60.0000000047 for an eye at -58.38): snap it off.
        player_->pos.y = std::round(player_->pos.y * 1e6) / 1e6;
    } else {
        player_->pos = generator->spawnPoint();
    }
    player_->oldPos = player_->pos;
    // A new player faces the spawn angle (0), level; the harness looks down a little by default.
    const glm::vec2 look = params_.commandLine ? cli.look.value_or(glm::vec2(-45.0f, 25.0f)) : glm::vec2(0.0f);
    player_->setRotation(look.x, look.y);
    if (params_.commandLine)
        perspective_ = cli.perspective == "back"    ? Perspective::ThirdPersonBack
                       : cli.perspective == "front" ? Perspective::ThirdPersonFront
                                                    : Perspective::FirstPerson;
    if (!worldDir_.empty())
        if (auto saved = readPlayerFile(worldDir_)) loadPlayer(*player_, *saved, *client_.items);
    camera_.position = player_->eyePos();
    camera_.yaw = player_->yaw;
    camera_.pitch = player_->pitch;
    eyeHeight_ = oldEyeHeight_ = player_->eyeHeight();
    camera_.farPlane = float(client_.options.renderDistance * 16 + 64) * 1.5f;
    inventoryMenu_ = std::make_unique<InventoryMenu>(player_->inventory);
    log::info("World: type {}, seed {}, render distance {}", generator->type(), params_.seed,
              client_.options.renderDistance);
    // Saved right away, so the world list shows it even if the game stops early.
    save(false);
    return true;
}

InventoryMenu& WorldSession::inventoryMenu() { return *inventoryMenu_; }

void WorldSession::save(bool wait) {
    if (worldDir_.empty() || !level_ || !player_) return;
    const int chunks = level_->chunks().saveAll();
    std::string err;
    WorldInfo info{params_.name, params_.seed, params_.type, level_->saveData()};
    info.gameMode = std::string(gameModeName(player_->mode()));
    info.difficulty = std::string(difficultyName(params_.difficulty));
    info.cheats = params_.cheats;
    info.hardcore = params_.hardcore;
    if (!writeWorldInfo(worldDir_, info, &err)) log::error("{}", err);
    if (!writePlayerFile(worldDir_, savePlayer(*player_), &err)) log::error("{}", err);
    if (wait)
        while (level_->chunks().savesPending()) client_.jobs->waitIdle();
    log::info("Saved world '{}' ({} chunks)", params_.folder, chunks);
}

bool WorldSession::viewReady() const { return level_->chunks().viewReady(); }

bool WorldSession::viewMeshed() const { return level_->chunks().viewMeshed() && worldRenderer_->idle(); }

float WorldSession::loadProgress() const {
    const auto st = level_->chunks().stats();
    const int r = client_.options.renderDistance;
    const float needed = float((2 * r + 1) * (2 * r + 1));
    return std::clamp(float(st.meshReady) / needed, 0.0f, 1.0f);
}

WorldLoadStatus WorldSession::loadStatus() const {
    WorldLoadStatus status;
    status.progress = loadProgress();
    const int r = client_.options.renderDistance;
    status.diameter = 2 * r + 1;
    status.stages.resize(size_t(status.diameter * status.diameter));
    const ChunkManager& chunks = level_->chunks();
    const ChunkPos center = ChunkPos::of(BlockPos::containing(camera_.position));
    for (int dz = -r; dz <= r; ++dz)
        for (int dx = -r; dx <= r; ++dx) {
            const ChunkPos p{center.x + dx, center.z + dz};
            status.stages[size_t((dz + r) * status.diameter + dx + r)] =
                chunks.isReady(p) ? 5 : uint8_t(chunks.stage(p));
        }
    return status;
}

void WorldSession::setPose(const glm::dvec3& eye, float yaw, float pitch) {
    player_->pos = player_->oldPos = eye - glm::dvec3(0.0, player_->eyeHeight(), 0.0);
    player_->setRotation(yaw, pitch);
    camera_.position = eye;
    camera_.yaw = yaw;
    camera_.pitch = pitch;
}

void WorldSession::renderScenery(const Camera& camera, glm::ivec2 size) {
    applyOptions();
    worldRenderer_->entities().begin(camera.position);
    worldRenderer_->render(camera, size, client_.options.renderDistance, *level_, 0.0f);
}

void WorldSession::captureIcon(glm::ivec2 fb) {
    if (hasIcon_ || worldDir_.empty() || !viewMeshed()) return;
    const std::filesystem::path path = worldDir_ / "icon.png";
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        hasIcon_ = true;
        return;
    }
    // The middle square, box-filtered down to 64x64.
    const Image shot = captureFramebuffer(fb.x, fb.y);
    const int side = std::min(fb.x, fb.y), x0 = (fb.x - side) / 2, y0 = (fb.y - side) / 2;
    Image icon(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int sx0 = x0 + x * side / 64, sx1 = std::max(sx0 + 1, x0 + (x + 1) * side / 64);
            const int sy0 = y0 + y * side / 64, sy1 = std::max(sy0 + 1, y0 + (y + 1) * side / 64);
            uint32_t sum[3] = {0, 0, 0};
            for (int sy = sy0; sy < sy1; ++sy)
                for (int sx = sx0; sx < sx1; ++sx)
                    for (int c = 0; c < 3; ++c) sum[c] += shot.px(sx, sy)[c];
            const uint32_t n = uint32_t((sx1 - sx0) * (sy1 - sy0));
            uint8_t* out = icon.px(x, y);
            for (int c = 0; c < 3; ++c) out[c] = uint8_t((sum[c] + n / 2) / n);
            out[3] = 255;
        }
    hasIcon_ = icon.savePng(path);
    if (hasIcon_) log::info("Saved the world icon {}", path.string());
}

void WorldSession::frame(double) {
    // Mouse look, scaled like vanilla's sensitivity option.
    if (client_.window.cursorCaptured() && !client_.screenOpen) {
        const glm::dvec2 d = client_.window.input().mouseDelta();
        const double k = client_.options.mouseDegreesPerCount();
        player_->yaw += float(d.x * k);
        const double dy = client_.options.invertMouse ? -d.y : d.y;
        player_->pitch = std::clamp(player_->pitch + float(dy * k), -90.0f, 90.0f);
    }
    ChunkManager& chunks = level_->chunks();
    chunks.setView(ChunkPos::of(BlockPos::containing(camera_.position)), client_.options.renderDistance);
    chunks.update();
    worldRenderer_->update(chunks, *client_.jobs);
}

std::string WorldSession::handleInput() {
    const Input& in = client_.window.input();
    if (in.keyPressed(GLFW_KEY_F5)) perspective_ = Perspective((int(perspective_) + 1) % 3);
    if (in.keyPressed(GLFW_KEY_F3)) showDebug = !showDebug;
    if (in.keyPressed(GLFW_KEY_ESCAPE)) return "pause";
    if (in.keyPressed(GLFW_KEY_E) && player_->mode() != GameMode::Spectator) return "inventory";
    if (in.keyPressed(GLFW_KEY_T)) return "chat";
    if (in.keyPressed(GLFW_KEY_SLASH)) return "command";
    // F swaps the main and off hand (not for spectators).
    if (in.keyPressed(GLFW_KEY_F) && player_->mode() != GameMode::Spectator)
        std::swap(player_->inventory.selectedStack(), player_->inventory.offhand);
    if (in.mousePressed(GLFW_MOUSE_BUTTON_LEFT) && !client_.window.cursorCaptured()) {
        client_.window.setCursorCaptured(true);
        return ""; // the capturing click is not an attack
    }
    if (!client_.window.cursorCaptured()) return "";
    attackClicks_ += in.mousePressed(GLFW_MOUSE_BUTTON_LEFT);
    useClicks_ += in.mousePressed(GLFW_MOUSE_BUTTON_RIGHT);
    pickClicks_ += in.mousePressed(GLFW_MOUSE_BUTTON_MIDDLE);
    int& selected = player_->inventory.selected;
    for (int i = 0; i < 9; ++i)
        if (in.keyPressed(GLFW_KEY_1 + i)) selected = i;
    if (const double scroll = in.scrollY(); scroll != 0.0) selected = ((selected - (scroll > 0 ? 1 : -1)) % 9 + 9) % 9;
    return "";
}

void WorldSession::runCommand(const std::string& line, const std::function<void(const std::string&)>& reply) {
    CommandSource src;
    src.level = level_.get();
    src.position = player_->pos;
    src.rotation = glm::vec2(player_->yaw, player_->pitch);
    src.permission = params_.cheats || params_.commandLine ? 4 : 0;
    src.seed = params_.seed;
    src.feedback = reply;
    src.getDifficulty = [this] { return params_.difficulty; };
    src.setDifficulty = [this](Difficulty d) { params_.difficulty = d; };
    src.clear = [this](const std::string* id, int max) {
        const Item* only = id ? client_.items->find(*id) : nullptr;
        if (id && !only) return -1;
        int removed = 0;
        for (int i = 0; i <= Inventory::kOffhandSlot && removed < max; ++i) {
            ItemStack& st = player_->inventory.slot(i);
            if (st.empty() || (only && st.item != only)) continue;
            const int n = std::min(st.count, max - removed);
            st.shrink(n);
            removed += n;
        }
        return removed;
    };
    src.teleport = [this](const glm::dvec3& p, std::optional<glm::vec2> rot) {
        player_->pos = player_->oldPos = p;
        player_->velocity = glm::dvec3(0.0);
        if (rot) player_->setRotation(rot->x, rot->y);
    };
    src.assertionFailed = [this](const std::string& msg) {
        log::error("assertion failed: {}", msg);
        assertionFailed_ = true;
    };
    src.give = [this](std::string_view id, int count) {
        const Item* item = client_.items->find(id);
        if (!item) return false;
        player_->inventory.add(*item, count);
        return true;
    };
    src.setGameMode = [this](std::string_view mode) {
        auto m = gameModeFromName(mode);
        if (!m) return false;
        player_->setMode(*m);
        return true;
    };
    std::string err;
    if (!client_.commands.execute(src, line, &err)) reply("§c" + err);
}

void WorldSession::tick() {
    ++tickCount_;
    // The player waits (vanilla: frozen) until the chunk around it is ready.
    const ChunkPos here = ChunkPos::of(BlockPos::containing(player_->pos));
    if (player_->noPhysics || level_->chunks().isReady(here)) {
        // Vanilla handles the mouse buttons before the entities tick.
        tickInteraction();
        player_->tick(*level_, readPlayerInput());
        particles_.tick(*level_);
    } else {
        player_->oldPos = player_->pos;
    }
    hud_->tick(*player_, [this](const ItemStack& st) {
        ScreenContext ctx;
        ctx.lang = client_.lang.get();
        ctx.items = client_.items.get();
        return ctx.itemName(st);
    });
    worldRenderer_->tickHands(*player_);
    // Vanilla Camera.tick: the eye height eases toward the player's (crouching).
    oldEyeHeight_ = eyeHeight_;
    eyeHeight_ += (player_->eyeHeight() - eyeHeight_) * 0.5f;
    attackClicks_ = useClicks_ = pickClicks_ = 0;
    level_->tick();
    if (tickCount_ % kAutosaveTicks == 0) save(false);
    worldRenderer_->tick();
}

// Vanilla Camera.setup: at the eyes, or for the F5 views up to 4 blocks behind them
// (in front of them, looking back) but short of the blocks in between.
void WorldSession::setupCamera(float partialTick) {
    const glm::dvec3 feet = glm::mix(player_->oldPos, player_->pos, double(partialTick));
    camera_.position = feet + glm::dvec3(0.0, oldEyeHeight_ + (eyeHeight_ - oldEyeHeight_) * partialTick, 0.0);
    camera_.yaw = player_->yaw;
    camera_.pitch = player_->pitch;
    camera_.fov = float(client_.options.fov);
    if (perspective_ == Perspective::FirstPerson) return;
    if (perspective_ == Perspective::ThirdPersonFront) {
        camera_.yaw += 180.0f;
        camera_.pitch = -camera_.pitch;
    }
    const glm::dvec3 forward(camera_.forward());
    camera_.position -= forward * cameraDistance(*level_, camera_.position, forward, 4.0);
}

void WorldSession::applyOptions() {
    const Options& o = client_.options;
    worldRenderer_->gamma = o.gamma;
    worldRenderer_->clouds = o.clouds;
    worldRenderer_->entities().shadows = o.entityShadows;
    if (worldRenderer_->mesherOptions().smoothLighting != o.smoothLighting) {
        worldRenderer_->mesherOptions().smoothLighting = o.smoothLighting;
        level_->chunks().markAllMeshesDirty(); // old meshes stay up until the new ones arrive
    }
}

void WorldSession::render(glm::ivec2 fb, float partialTick, const std::function<void()>& renderGui) {
    applyOptions();
    setupCamera(partialTick);
    updateTarget(partialTick);
    EntityRenderer& entities = worldRenderer_->entities();
    entities.begin(camera_.position);
    if (perspective_ != Perspective::FirstPerson)
        entities.submitPlayer(*player_, *level_, partialTick, skyDarkenLevel(level_->dayTime()));
    worldRenderer_->render(camera_, fb, client_.options.renderDistance, *level_, partialTick);
    worldRenderer_->renderParticles(particles_, *level_, camera_, fb, partialTick);
    if (target_ && player_->mode() != GameMode::Spectator) {
        const int stage = breaking_.active && breaking_.pos == target_->pos ? int(breaking_.progress * 10.0f) - 1 : -1;
        overlay_->render(camera_, fb, target_->pos, client_.registry->outlineShape(level_->getBlock(target_->pos)), stage);
    }
    if (perspective_ == Perspective::FirstPerson) worldRenderer_->renderHands(*player_, *level_, fb, partialTick);
    captureIcon(fb);
    renderHud(fb, partialTick, renderGui);
}

bool WorldSession::controlDown(int key, std::string_view action) const {
    if (!client_.screenOpen && client_.window.cursorCaptured() && client_.window.input().keyDown(key)) return true;
    const auto& hold = client_.cli.hold;
    return params_.commandLine && std::find(hold.begin(), hold.end(), action) != hold.end();
}

PlayerInput WorldSession::readPlayerInput() {
    PlayerInput in;
    const bool jumpDown = controlDown(GLFW_KEY_SPACE, "jump");
    // Vanilla double-tap: a second jump press within 7 ticks toggles creative flight.
    if (jumpTapTimer_ > 0) --jumpTapTimer_;
    if (jumpDown && !jumpWasDown_) {
        if (jumpTapTimer_ == 0) {
            jumpTapTimer_ = 7;
        } else {
            in.doubleTapJump = true;
            jumpTapTimer_ = 0;
        }
    }
    jumpWasDown_ = jumpDown;
    in.forward = float(controlDown(GLFW_KEY_W, "forward")) - float(controlDown(GLFW_KEY_S, "back"));
    in.strafe = float(controlDown(GLFW_KEY_A, "left")) - float(controlDown(GLFW_KEY_D, "right"));
    in.jump = jumpDown;
    in.sneak = controlDown(GLFW_KEY_LEFT_SHIFT, "sneak");
    in.sprint = controlDown(GLFW_KEY_LEFT_CONTROL, "sprint");
    return in;
}

void WorldSession::updateTarget(float partialTick) {
    target_.reset();
    // Vanilla block_interaction_range: 4.5, plus 0.5 in creative; from the eyes in every view.
    const double reach = player_->mode() == GameMode::Creative ? 5.0 : 4.5;
    const glm::dvec3 eye =
        glm::mix(player_->oldPos, player_->pos, double(partialTick)) + glm::dvec3(0.0, player_->eyeHeight(), 0.0);
    target_ = raycastBlocks(*level_, eye, player_->viewVector(), reach);
}

// Debris (or, mining, a chip off `face`) with the block's particle texture.
void WorldSession::spawnBlockParticles(const BlockPos& p, StateId s, std::optional<Direction> face) {
    const BlockRegistry& registry = *client_.registry;
    const auto choices = registry.modelChoices(s);
    if (choices.empty() || choices[0]->models.empty()) return;
    const Block& b = registry.blockOf(s);
    // Vanilla tints the particles like the block's first tint index (but not grass blocks).
    uint32_t tint = 0xFFFFFF;
    if (b.tint.source != TintSource::None && b.id != "minecraft:grass_block")
        tint = resolveTint(b.tint, 0, client_.assets->defaultBiomeColors());
    const int sprite = choices[0]->models[0]->particleSprite;
    if (face) particles_.addBlockHit(registry, s, p, *face, sprite, tint);
    else particles_.addBlockBreak(registry, s, p, sprite, tint);
}

void WorldSession::breakBlock(const BlockPos& p) {
    const BlockRegistry& registry = *client_.registry;
    const StateId s = level_->getBlock(p);
    spawnBlockParticles(p, s, std::nullopt);
    // A waterlogged block leaves its water behind.
    const bool waterlogged = registry.boolValue(s, "waterlogged") || registry.blockOf(s).className == "water_plant";
    level_->setBlockAndUpdate(p, waterlogged ? registry.defaultState("water") : BlockRegistry::kAirState);
}

bool WorldSession::placeBlock() {
    if (!target_) return false;
    const BlockRegistry& registry = *client_.registry;
    ItemStack& held = player_->inventory.selectedStack();
    if (held.empty() || !held.item->block) return false;
    const Block& block = *held.item->block;
    const bool consume = player_->mode() != GameMode::Creative;
    const StateId clicked = level_->getBlock(target_->pos);
    const double hitY = target_->point.y - double(target_->pos.y);
    // A slab clicked on its open half becomes a double slab.
    if (auto merged = mergeIntoClicked(registry, clicked, block, target_->face, hitY)) {
        if (!level_->setBlockAndUpdate(target_->pos, *merged)) return false;
        if (consume) player_->inventory.consumeSelected();
        return true;
    }
    // Clicking a replaceable block (grass, snow layer) places into it; otherwise against the face.
    const bool replace = (registry.info(clicked).flags & kReplaceable) && registry.info(clicked).block != block.index;
    PlaceContext ctx;
    ctx.pos = replace ? target_->pos : target_->pos.offset(target_->face);
    ctx.clickedFace = target_->face;
    ctx.hitPoint = target_->point;
    ctx.yaw = player_->yaw;
    ctx.pitch = player_->pitch;
    ctx.replacingClicked = replace;
    ctx.sneaking = player_->crouching;
    const StateId there = level_->getBlock(ctx.pos);
    if (!(registry.info(there).flags & (kReplaceable | kAir))) {
        // The neighbor may be a slab this one completes.
        auto merged = mergeIntoClicked(registry, there, block, opposite(target_->face), hitY);
        if (!merged || !level_->setBlockAndUpdate(ctx.pos, *merged)) return false;
        if (consume) player_->inventory.consumeSelected();
        return true;
    }
    const auto blocks = placementStates(registry, *level_, block, ctx);
    if (blocks.empty()) return false;
    // Never inside the player.
    const AABB body = player_->box().inflated(-1e-7, -1e-7, -1e-7);
    for (const auto& [p, s] : blocks)
        for (const AABB& b : registry.collisionShape(s))
            if (b.moved(glm::dvec3(p.vec())).intersects(body)) return false;
    for (const auto& [p, s] : blocks) level_->setBlockAndUpdate(p, s);
    if (consume) player_->inventory.consumeSelected();
    return true;
}

void WorldSession::tickInteraction() {
    const BlockRegistry& registry = *client_.registry;
    if (!target_ || player_->mode() == GameMode::Spectator) breaking_.active = false;
    if (destroyDelay_ > 0) --destroyDelay_;
    if (useDelay_ > 0) --useDelay_;
    const Input& in = client_.window.input();
    const bool captured = client_.window.cursorCaptured() && !client_.screenOpen;
    const bool attackHeld = (captured && in.mouseDown(GLFW_MOUSE_BUTTON_LEFT)) || controlDown(-1, "attack");
    const bool useHeld = (captured && in.mouseDown(GLFW_MOUSE_BUTTON_RIGHT)) || controlDown(-1, "use");
    // Pick block (creative): the target's block into the selected slot.
    if (pickClicks_ > 0 && target_ && player_->mode() == GameMode::Creative)
        if (const Item* item = client_.items->forBlock(registry.blockOf(level_->getBlock(target_->pos))))
            player_->inventory.pick(*item);
    // Use: on click, then every 4 ticks while held (vanilla rightClickDelay). Blocks that
    // react to a hand (doors, levers, ...) take the click unless sneaking with an item.
    if (target_ && player_->mode() != GameMode::Spectator && (useClicks_ > 0 || (useHeld && useDelay_ == 0))) {
        const StateId s = level_->getBlock(target_->pos);
        const bool bypass = player_->crouching && !player_->inventory.selectedStack().empty();
        const auto use = bypass ? std::nullopt : useBlock(registry, s, horizontalFacing(player_->yaw));
        // A successful use swings the arm.
        if (use) {
            if (use->state != s) level_->setBlockAndUpdate(target_->pos, use->state);
            if (use->tickDelay > 0) level_->scheduleTick(target_->pos, registry.info(use->state).block, use->tickDelay);
            player_->swing();
        } else if (placeBlock()) {
            player_->swing();
        }
        useDelay_ = 4;
    }
    // Attack: creative breaks at once (5-tick pause between blocks); survival accumulates progress.
    // A click swings even at nothing; holding on a block swings every tick (vanilla continueAttack).
    const bool attacking = attackClicks_ > 0 || attackHeld;
    if (attackClicks_ > 0 && player_->mode() != GameMode::Spectator) player_->swing();
    if (!attacking || !target_ || player_->mode() == GameMode::Spectator || player_->mode() == GameMode::Adventure) {
        breaking_.active = false;
        return;
    }
    player_->swing();
    const BlockPos p = target_->pos;
    const StateId s = level_->getBlock(p);
    const Block& block = registry.blockOf(s);
    if (player_->mode() == GameMode::Creative) {
        if (destroyDelay_ == 0 || attackClicks_ > 0) {
            breakBlock(p);
            destroyDelay_ = 5;
        }
        return;
    }
    if (destroyDelay_ > 0) return;
    if (!breaking_.active || breaking_.pos != p) breaking_ = {p, 0.0f, true};
    breaking_.progress += destroyProgressPerTick(block, *player_);
    spawnBlockParticles(p, s, target_->face);
    if (breaking_.progress >= 1.0f) {
        breakBlock(p);
        breaking_.active = false;
        destroyDelay_ = 5;
    }
}

void WorldSession::renderHud(glm::ivec2 fb, float, const std::function<void()>& renderGui) {
    GuiGraphics& gui = *client_.gui;
    gui.begin(fb, client_.guiScale());
    const bool eyeInWater = perspective_ == Perspective::FirstPerson &&
                            level_->fluid(BlockPos::containing(camera_.position)).kind == FluidKind::Water;
    ItemModelContext items;
    items.timeOfDay = worldRenderer_->environment().timeOfDay;
    items.holderPos = player_->pos;
    items.holderYaw = player_->yaw;
    items.spawnPos = glm::dvec3(level_->generator().spawnPoint());
    items.spin = float(tickCount_ % 200) / 200.0f;
    gui.setItemContext(items);
    // The crosshair hides under screens.
    hud_->render(gui, *player_, perspective_ == Perspective::FirstPerson && !client_.screenOpen, eyeInWater,
                 [&gui](const ItemStack& stack, float x, float y) {
                     gui.item(stack, x, y);
                     gui.itemDecorations(stack, x, y);
                 });
    // Vanilla Gui.render: the debug screen under the chat, and both under any screen.
    if (showDebug) renderDebugOverlay(gui);
    client_.chat->render(gui, client_.options, client_.chatTicks, client_.chatOpen);
    if (renderGui) renderGui();
    gui.end();
}

void WorldSession::renderDebugOverlay(GuiGraphics& gui) {
    const BlockRegistry& registry = *client_.registry;
    std::vector<std::string> lines;
    lines.push_back("Minecraft 1.21.8 (minecraft-cpp)");
    const glm::dvec3& p = camera_.position;
    const auto& r = worldRenderer_->chunkRenderer().stats();
    lines.push_back(std::format("{:.0f} fps  C: {}/{} sections, {} draws", fps, r.drawn, r.sections, r.draws));
    const auto st = level_->chunks().stats();
    lines.push_back(std::format("Chunks: {} loaded, {} lit, {} ready, {} jobs", st.entries, st.lit, st.meshReady, st.inFlight));
    lines.push_back("");
    lines.push_back(std::format("XYZ: {:.3f} / {:.5f} / {:.3f}", p.x, p.y, p.z));
    const BlockPos b = BlockPos::containing(p);
    lines.push_back(std::format("Block: {} {} {} [{} {} {}]", b.x, b.y, b.z, b.x & 15, b.y & 15, b.z & 15));
    lines.push_back(std::format("Chunk: {} {} {} in {} {} {}", b.x & 15, b.y & 15, b.z & 15, b.x >> 4, b.y >> 4, b.z >> 4));
    static const char* names[4] = {"south (Towards positive Z)", "west (Towards negative X)", "north (Towards negative Z)",
                                   "east (Towards positive X)"};
    const int facing = int(std::floor(std::fmod(std::fmod(camera_.yaw, 360.0f) + 360.0f + 45.0f, 360.0f) / 90.0f)) & 3;
    lines.push_back(std::format("Facing: {} ({:.1f} / {:.1f})", names[facing], std::remainder(camera_.yaw, 360.0f),
                                camera_.pitch));
    if (const Chunk* c = level_->chunks().chunk(ChunkPos::of(b))) {
        const uint8_t l = c->lightAt(b.x & 15, b.y, b.z & 15);
        lines.push_back(std::format("Client Light: {} ({} sky, {} block)", std::max(l >> 4, l & 15), l >> 4, l & 15));
        if (overworld_ && b.y >= kWorldMinY && b.y <= kWorldMaxY) {
            const Section& sec = c->sections[size_t(sectionIndexOfY(b.y))];
            const BiomeId id =
                sec.biomes[size_t(((((b.y - kWorldMinY) & 15) >> 2) << 4) | (((b.z & 15) >> 2) << 2) | ((b.x & 15) >> 2))];
            lines.push_back(std::format("Biome: {}", client_.biomes->get(id).id));
        }
    }
    const auto& env = worldRenderer_->environment();
    lines.push_back(std::format("Day {} Time {} (celestial {:.3f}, sky darken {:.2f})", level_->dayTime() / 24000,
                                level_->dayTime() % 24000, env.timeOfDay, env.skyDarken));
    lines.push_back(std::format("Mode: {}{}{}", gameModeName(player_->mode()), player_->flying ? ", flying" : "",
                                player_->onGround ? ", on ground" : ""));
    if (target_) {
        const BlockPos& t = target_->pos;
        lines.push_back("");
        lines.push_back(std::format("Targeted Block: {}, {}, {} ({})", t.x, t.y, t.z, directionName(target_->face)));
        const std::string desc = registry.describe(level_->getBlock(t));
        const size_t bracket = desc.find('[');
        lines.push_back(desc.substr(0, bracket));
        if (bracket != std::string::npos) {
            // One property per line, like vanilla.
            std::string props = desc.substr(bracket + 1, desc.size() - bracket - 2);
            for (size_t start = 0; start <= props.size();) {
                const size_t comma = props.find(',', start);
                std::string kv = props.substr(start, comma - start);
                if (const size_t eq = kv.find('='); eq != std::string::npos) kv = kv.substr(0, eq) + ": " + kv.substr(eq + 1);
                lines.push_back(kv);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        const float y = 2.0f + 9.0f * float(i);
        gui.fill(1, y - 1, 2.0f + float(gui.textWidth(lines[i])) + 1.0f, y + 8, 0x90505050);
        gui.text(lines[i], 2, y, 0xFFE0E0E0, false);
    }
}

std::string WorldSession::statusLine() const {
    const auto s = level_->chunks().stats();
    const auto& r = worldRenderer_->chunkRenderer().stats();
    return std::format("chunks {} (lit {}, ready {}, jobs {})  |  sections {}/{}  |  {:.0f} MB VRAM  |  {:.1f} {:.1f} {:.1f}",
                       s.entries, s.lit, s.meshReady, s.inFlight, r.drawn, r.sections,
                       double(worldRenderer_->chunkRenderer().gpuBytes()) / (1 << 20), camera_.position.x,
                       camera_.position.y, camera_.position.z);
}

} // namespace mc
