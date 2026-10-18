#include "app/WorldSession.h"
#include "app/ActionScript.h"
#include "core/Fs.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <GLFW/glfw3.h>

#include "app/Client.h"
#include "core/Log.h"
#include "game/Item.h"
#include "game/Menu.h"
#include "game/Placement.h"
#include "game/BlockEntities.h"
#include "game/ContainerMenus.h"
#include "game/ItemUse.h"
#include "world/block/Tints.h"
#include "world/sim/Fluids.h"
#include "world/sim/Plants.h"
#include "world/sim/Simulation.h"
#include "world/sim/Weathering.h"
#include "game/Loot.h"
#include "game/Tools.h"
#include "entity/Arrow.h"
#include "entity/ExperienceOrb.h"
#include "entity/mob/Monsters.h"
#include "game/Combat.h"
#include "entity/DisplayTicks.h"
#include "entity/FallingBlockEntity.h"
#include "entity/LightningBolt.h"
#include "entity/PrimedTnt.h"
#include "entity/ItemEntity.h"
#include "entity/Physics.h"
#include "gfx/Screenshot.h"
#include "render/BlockOverlay.h"
#include "render/ClientAssets.h"
#include "entity/model/BlockEntityModels.h"
#include "ui/Font.h"
#include "render/EntityRenderer.h"
#include "render/WorldRenderer.h"
#include "ui/Chat.h"
#include "ui/GuiGraphics.h"
#include "ui/Hud.h"
#include "ui/Screen.h"
#include "ui/ScreenOverlays.h"
#include "world/biome/Biome.h"
#include "world/block/BlockBehavior.h"
#include "world/block/BlockRegistry.h"
#include "world/gen/ChunkGenerator.h"
#include "world/gen/OverworldGenerator.h"
#include "world/gen/feature/BiomeDecorator.h"
#include "world/level/Environment.h"
#include "world/level/Level.h"
#include "world/level/NaturalSpawner.h"
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
    // World generation's features: the overworld decorates with them, and saplings and bone
    // meal grow them in every world type.
    decorator_ = std::make_shared<BiomeDecorator>();
    if (!decorator_->load(cli.dataDir, registry, *client_.biomes, &err)) {
        int shown = 0;
        for (size_t start = 0; start < err.size() && shown < 30; ++shown) {
            const size_t end = err.find('\n', start);
            log::warn("features: {}", err.substr(start, end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    std::shared_ptr<const ChunkGenerator> generator;
    if (params_.type == "flat") {
        auto flat = std::make_shared<FlatGenerator>(registry);
        flat->biome = client_.biomes->require("plains");
        generator = flat;
    } else if (params_.type == "debug") {
        generator = std::make_shared<DebugGenerator>(registry);
    } else {
        overworld_ = std::make_shared<OverworldGenerator>(registry, *client_.biomes, params_.seed, decorator_);
        generator = overworld_;
    }
    level_ = std::make_unique<Level>(registry, generator, *client_.jobs, params_.seed);
    level_->setWorldgen(decorator_.get(), client_.biomes.get());
    if (!worldDir_.empty()) {
        level_->attachStorage(std::make_shared<ChunkStorage>(worldDir_, registry, client_.biomes.get()));
        if (info) level_->loadData(info->level);
    }
    level_->chunks().setLightEngine(std::make_shared<LightEngine>(registry));
    if (params_.commandLine) {
        if (cli.time) level_->setDayTime(*cli.time);
        level_->daylightCycle = !cli.freezeTime;
        for (const auto& [rule, value] : cli.gameRules) {
            std::string err;
            if (!level_->setGameRule(rule, value, &err)) log::warn("--gamerule {}={}: {}", rule, value, err);
        }
        if (!cli.weather.empty()) {
            level_->setWeather(0, 1000000, cli.weather != "clear", cli.weather == "thunder");
            level_->settleWeather();
            level_->doWeatherCycle = false;
        }
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
    // Particle sprites (particles/<type>.json), stitched into the block atlas.
    const Atlas& atlas = client_.assets->blockAtlas();
    for (size_t t = 0; t < size_t(ParticleType::Count); ++t) {
        const std::string_view name = particleDefinition(ParticleType(t));
        if (name.empty()) continue;
        const ResourcePack& pack = client_.assets->pack();
        std::vector<int> sprites;
        if (const auto j = pack.json(pack.file("minecraft", std::format("particles/{}.json", name))))
            for (const Json& tex : j->value("textures", Json::array()))
                if (const int s = atlas.find(canonicalId(tex.get<std::string>())); s != atlas.missing) sprites.push_back(s);
        if (sprites.empty()) log::warn("particles: no sprites for {}", name);
        particles_.setSprites(ParticleType(t), std::move(sprites));
    }

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
        settleSpawn_ = true; // on top of what the decorated chunk holds, once it is ready
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
        if (auto saved = readPlayerFile(worldDir_)) {
            loadPlayer(*player_, *saved, *client_.items);
            settleSpawn_ = false;
        }
    camera_.position = player_->eyePos();
    camera_.yaw = player_->yaw;
    camera_.pitch = player_->pitch;
    eyeHeight_ = oldEyeHeight_ = player_->eyeHeight();
    camera_.farPlane = float(client_.options.renderDistance * 16 + 64) * 1.5f;
    inventoryMenu_ = std::make_unique<InventoryMenu>(player_->inventory, client_.recipes.get());
    level_->setPlayer(player_.get());
    level_->setItems(client_.items.get());
    level_->setEntityTypes(client_.entityTypes.get());
    level_->setLoot(client_.loot.get());
    level_->setRecipes(client_.recipes.get());
    level_->difficulty = params_.difficulty;
    log::info("World: type {}, seed {}, render distance {}", generator->type(), params_.seed,
              client_.options.renderDistance);
    if (params_.commandLine && !cli.script.empty()) {
        const std::optional<std::string> text = fs::readText(cli.script);
        std::string problem = text ? "" : "can't read the file";
        auto program = text ? parseActionScript(*text, &problem) : std::nullopt;
        if (!program) {
            log::error("--script {}: {}", cli.script.string(), problem);
            return false;
        }
        script_ = std::make_unique<ActionScript>(*this, std::move(*program));
    }
    // Saved right away, so the world list shows it even if the game stops early.
    save(false);
    return true;
}

bool WorldSession::scriptRunning() const { return script_ && !script_->done(); }

glm::dvec3 WorldSession::spawnOnTop() const {
    // Vanilla PlayerRespawnLogic.getOverworldRespawnPos: on the highest block of the spawn
    // column that entities stand on (tree tops too), unless a fluid comes first; else up
    // from the generator's height to the first space the player fits.
    const glm::dvec3 spawn = level_->generator().spawnPoint();
    const int x = int(std::floor(spawn.x)), z = int(std::floor(spawn.z));
    const BlockRegistry& registry = *client_.registry;
    for (int y = kWorldMaxY; y >= kWorldMinY; --y) {
        const StateInfo& info = registry.info(level_->getBlock({x, y, z}));
        if (!info.fluid.empty()) break;
        if (info.flags & kBlocksMotion) return {spawn.x, double(y + 1), spawn.z};
    }
    glm::dvec3 feet = spawn;
    for (int y = int(spawn.y); y < kWorldMaxY; ++y) {
        feet.y = double(y);
        if (noCollision(*level_, player_->boxAt(feet))) break;
    }
    return feet;
}

bool WorldSession::screenOpen() const { return client_.screenOpen; }

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
bool WorldSession::viewDrawn() const { return viewMeshed() && worldRenderer_->chunkRenderer().occlusionSettled(); }

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
    player_->velocity = glm::dvec3(0.0);
    player_->setRotation(yaw, pitch);
    camera_.position = eye;
    camera_.yaw = yaw;
    camera_.pitch = pitch;
}

void WorldSession::renderScenery(const Camera& camera, glm::ivec2 size) {
    applyOptions();
    const glm::vec3 right = camera.right();
    worldRenderer_->entities().begin(camera.position, right, glm::cross(right, camera.forward()));
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
    const KeyBindings& keys = client_.options.keys;
    const auto pressed = [&](KeyAction a) { return in.pressed(keys[a]); };
    if (pressed(KeyAction::TogglePerspective)) perspective_ = Perspective((int(perspective_) + 1) % 3);
    if (in.keyPressed(GLFW_KEY_F3)) showDebug = !showDebug;
    if (in.keyPressed(GLFW_KEY_ESCAPE)) return "pause";
    if (pressed(KeyAction::Inventory) && player_->mode() != GameMode::Spectator) return "inventory";
    if (pressed(KeyAction::Chat)) return "chat";
    if (pressed(KeyAction::Command)) return "command";
    // The off hand swap (F) is not for spectators.
    if (pressed(KeyAction::SwapOffhand) && player_->mode() != GameMode::Spectator)
        std::swap(player_->inventory.selectedStack(), player_->inventory.offhand);
    // Q throws one of the selected stack, Ctrl+Q all of it (vanilla keyDrop), swinging the arm.
    if (pressed(KeyAction::Drop) && player_->mode() != GameMode::Spectator) {
        ItemStack& held = player_->inventory.selectedStack();
        if (!held.empty()) {
            player_->drop(held.split((in.mods() & GLFW_MOD_CONTROL) ? held.count : 1));
            player_->swing();
        }
    }
    if (in.mousePressed(GLFW_MOUSE_BUTTON_LEFT) && !client_.window.cursorCaptured()) {
        client_.window.setCursorCaptured(true);
        return ""; // the capturing click is not an attack
    }
    if (!client_.window.cursorCaptured()) return "";
    attackClicks_ += pressed(KeyAction::Attack);
    useClicks_ += pressed(KeyAction::Use);
    pickClicks_ += pressed(KeyAction::PickItem);
    int& selected = player_->inventory.selected;
    for (int i = 0; i < 9; ++i)
        if (pressed(hotbarAction(i))) selected = i;
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
    src.summon = [this](std::string_view type, const glm::dvec3& at, const Json& data, std::string& error) {
        if (type == "item") {
            // {Item: {id: "minecraft:diamond", count: 3}}
            const Json item = data.value("Item", Json::object());
            const Item* it = client_.items->find(item.value("id", std::string()));
            if (!it) {
                error = "the item entity needs Item:{id:...}";
                return false;
            }
            ItemEntity& e = ItemEntity::spawn(*level_, at, ItemStack{it, std::clamp(int(item.value("count", 1)), 1, 99)});
            e.velocity = glm::dvec3(0.0); // summoned items keep still
            e.pickupDelay = int(data.value("PickupDelay", 0));
            return true;
        }
        if (type == "experience_orb") {
            auto orb = std::make_unique<ExperienceOrb>();
            orb->pos = orb->oldPos = at;
            orb->value = std::max(1, int(data.value("Value", 1)));
            level_->addEntity(std::move(orb));
            return true;
        }
        return false;
    };
    src.kill = [this] { player_->hurt(DamageType::GenericKill, 3.4e38f); };
    src.replaceBlockSlot = [this](const BlockPos& p, int slot, std::string_view id, int count) {
        const Item* item = client_.items->find(id);
        auto be = level_->blockEntity(p);
        if (!item || !be) return false;
        ItemStack stack{item, std::clamp(count, 1, item->maxStack)};
        if (auto* c = dynamic_cast<ContainerBlockEntity*>(be.get()); c && slot >= 0 && slot < int(c->items.size()))
            c->items[size_t(slot)] = stack;
        else if (auto* f = dynamic_cast<FurnaceBlockEntity*>(be.get()); f && slot >= 0 && slot < 3)
            f->items[size_t(slot)] = stack;
        else
            return false;
        return true;
    };
    src.replacePlayerSlot = [this](int slot, std::string_view id, int count) {
        ItemStack& st = slot < 0 ? player_->inventory.selectedStack() : player_->inventory.slot(slot);
        if (id == "air" || id == "minecraft:air") {
            st = ItemStack{};
            return true;
        }
        const Item* item = client_.items->find(id);
        if (!item) return false;
        st = ItemStack{item, std::clamp(count, 1, item->maxStack)};
        return true;
    };
    src.addExperience = [this](int amount, bool levels) {
        if (levels) player_->giveExperienceLevels(amount);
        else player_->giveExperiencePoints(amount);
    };
    src.experience = [this](bool levels) {
        return levels ? player_->experienceLevel
                      : int(std::lround(player_->experienceProgress * float(player_->xpNeededForNextLevel())));
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
        if (std::exchange(settleSpawn_, false)) player_->pos = player_->oldPos = spawnOnTop();
        // A --script plays as the player would: its turns, keys and clicks come first.
        if (script_ && client_.harnessRunning && !script_->done()) {
            script_->tick();
            if (script_->failed()) assertionFailed_ = true;
        }
        // Vanilla handles the mouse buttons before the entities tick; the dead don't act.
        if (!player_->dead() && !player_->sleeping()) tickInteraction();
        player_->inRain = level_->isRainingAt(BlockPos::containing(player_->pos)) ||
                          level_->isRainingAt(BlockPos::containing(player_->pos + glm::dvec3(0.0, player_->height, 0.0)));
        if (player_->sleeping()) {
            player_->oldPos = player_->pos;
            tickSleep();
        } else {
            player_->tick(*level_, player_->dead() ? PlayerInput{} : readPlayerInput());
            // Vanilla LivingEntity.pushEntities: the player shoves the mobs it walks into.
            if (player_->isPushable())
                for (Entity* e : level_->entitiesIn(player_->box(), player_.get()))
                    if (e->isPushable()) pushApart(*player_, *e);
        }
        if (player_->mode() != GameMode::Spectator)
            player_->survivalTick(*level_, params_.difficulty, level_->naturalRegeneration);
        if (const auto landed = std::exchange(player_->landedOnFarmland, std::nullopt))
            sim::trampleFarmland(*level_, landed->first, landed->second);
        if (player_->dead() && !deathHandled_) die();
        // Vanilla Gui.updateVignetteBrightness: 1% a tick toward the darkness at the eyes.
        const float darkness = std::clamp(1.0f - eyeBrightness(), 0.0f, 1.0f);
        vignette_ = vignetteSettled_ || !params_.commandLine ? vignette_ + (darkness - vignette_) * 0.01f : darkness;
        vignetteSettled_ = true;
        checkOpenMenu();
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
    // Vanilla AbstractClientPlayer.getFieldOfViewModifier, eased by half each tick.
    {
        float target = 1.0f;
        if (player_->flying) target *= 1.1f;
        target *= ((player_->sprinting ? 1.3f : 1.0f) + 1.0f) / 2.0f;
        if (player_->usingItem() && player_->useItem->id == "minecraft:bow") {
            const float f = std::min(float(72000 - player_->useItemRemaining) / 20.0f, 1.0f);
            target *= 1.0f - f * f * 0.15f;
        }
        // The FOV Effects scale (harness runs: none unless asked, so their framing stays put).
        const float scale = params_.commandLine ? client_.cli.fovEffects.value_or(0.0f) : client_.options.fovEffectScale;
        target = 1.0f + (target - 1.0f) * scale;
        oldFovModifier_ = fovModifier_;
        fovModifier_ = std::clamp(fovModifier_ + (target - fovModifier_) * 0.5f, 0.1f, 1.5f);
    }
    // Vanilla Camera.tick: the eye height eases toward the player's (crouching).
    oldEyeHeight_ = eyeHeight_;
    eyeHeight_ += (player_->eyeHeight() - eyeHeight_) * 0.5f;
    attackClicks_ = useClicks_ = pickClicks_ = 0;
    // Stacks thrown out of menus or with Q fly from the player's eyes.
    for (const ItemStack& thrown : std::exchange(player_->droppedItems, {}))
        ItemEntity::dropFromPlayer(*level_, *player_, thrown);
    level_->difficulty = params_.difficulty;
    level_->tick();
    // What happened in the world, shown (vanilla levelEvent).
    for (const LevelEvent& e : level_->takeEvents()) {
        switch (e.type) {
        case LevelEventType::BlockBroken: spawnBlockParticles(e.pos, e.state, std::nullopt); break;
        case LevelEventType::LavaFizz: // vanilla levelEvent 1501
            for (int i = 0; i < 8; ++i) {
                const double x = particles_.random().nextDouble(), z = particles_.random().nextDouble();
                particles_.add(ParticleType::LargeSmoke, glm::dvec3(e.pos.x + x, e.pos.y + 1.2, e.pos.z + z));
            }
            break;
        case LevelEventType::BoneMeal: spawnBoneMealParticles(e.pos, e.data); break;
        case LevelEventType::Explosion:
            // Vanilla ServerExplosion: power 2 and up throws an emitter of blasts, else one blast.
            particles_.add(e.data >= 2 ? ParticleType::ExplosionEmitter : ParticleType::Explosion, e.point,
                           glm::dvec3(1.0, 0.0, 0.0));
            break;
        case LevelEventType::EntityPoof:
        case LevelEventType::Hearts:
        case LevelEventType::Crit:
        case LevelEventType::DamageIndicator:
        case LevelEventType::Sweep: spawnEntityParticles(e); break;
        }
    }
    tickClientParticles();
    // What the player picked up flies to it for 3 ticks (vanilla ItemPickupParticle).
    for (auto& pickup : pickups_) ++pickup.life;
    std::erase_if(pickups_, [](const PickupAnimation& a) { return a.life >= 3; });
    for (Level::Pickup& p : level_->takePickups()) pickups_.push_back({std::move(p.entity), p.count, 0});
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
    // Vanilla GameRenderer.getFov: the eased FOV modifier (flying, sprinting, drawing a bow),
    // narrower in water and lava, and while dying.
    float fov = float(client_.options.fov) * (oldFovModifier_ + (fovModifier_ - oldFovModifier_) * partialTick);
    if (player_->dead()) fov /= (1.0f - 500.0f / (float(player_->deathTime) + partialTick + 500.0f)) * 2.0f + 1.0f;
    const float effects = params_.commandLine ? client_.cli.fovEffects.value_or(0.0f) : client_.options.fovEffectScale;
    if (player_->eyeInWater(*level_) || level_->fluid(BlockPos::containing(player_->eyePos())).kind == FluidKind::Lava)
        fov *= 1.0f + (0.85714287f - 1.0f) * effects;
    camera_.fov = fov;
    // Vanilla Camera.setup: lying in bed (in first person), looking along it from the pillow,
    // 0.3 above the eyes.
    if (player_->sleeping() && perspective_ == Perspective::FirstPerson) {
        camera_.position = glm::mix(player_->oldPos, player_->pos, double(partialTick)) + glm::dvec3(0.0, 0.2 + 0.3, 0.0);
        camera_.pitch = 0.0f;
    }
    // Vanilla bobHurt: the view tips over when dying and jerks sideways when hurt.
    camera_.roll = 0.0f;
    if (player_->dead()) camera_.roll += 40.0f - 8000.0f / (std::min(float(player_->deathTime) + partialTick, 20.0f) + 200.0f);
    if (const float t = float(player_->hurtTime) - partialTick; t >= 0.0f) {
        float f = t / float(player_->hurtDuration);
        f = std::sin(f * f * f * f * 3.1415927f);
        camera_.roll -= f * 14.0f;
    }
    // Vanilla bobView (the View Bobbing option; harness runs keep still unless asked).
    const bool bobbing = params_.commandLine ? client_.cli.viewBobbing.value_or(false) : client_.options.viewBobbing;
    camera_.bob = bobbing ? viewBob(*player_, partialTick) : glm::mat4(1.0f);
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
    worldRenderer_->caveCulling = client_.cli.caveCulling;
    worldRenderer_->fancyGraphics = o.fancyGraphics();
    worldRenderer_->entities().shadows = o.entityShadows;
    // Options the chunk meshes bake in: changing one remeshes everything (the old meshes
    // stay up until the new ones arrive).
    MesherOptions& mesher = worldRenderer_->mesherOptions();
    if (mesher.smoothLighting != o.smoothLighting || mesher.biomeBlend != o.biomeBlend ||
        mesher.fastLeaves != !o.fancyGraphics()) {
        mesher.smoothLighting = o.smoothLighting;
        mesher.biomeBlend = o.biomeBlend;
        mesher.fastLeaves = !o.fancyGraphics();
        level_->chunks().markAllMeshesDirty();
    }
    worldRenderer_->setMipmapLevels(o.mipmapLevels);
    particles_.mode = o.particles;
}

void WorldSession::render(glm::ivec2 fb, float partialTick, const std::function<void()>& renderGui) {
    applyOptions();
    setupCamera(partialTick);
    particles_.camera = camera_.position; // the Particles option and its 32-block cut-off
    updateTarget(partialTick);
    EntityRenderer& entities = worldRenderer_->entities();
    const glm::vec3 right = camera_.right();
    entities.begin(camera_.position, right, glm::cross(right, camera_.forward()));
    const int skyDarken = level_->skyDarken();
    if (perspective_ != Perspective::FirstPerson) entities.submitPlayer(*player_, *level_, partialTick, skyDarken);
    submitEntities(partialTick, skyDarken);
    // Beyond the level: lightning's flash and the fluid around the camera (vanilla
    // Camera.getFluidInCamera, LocalPlayer.getWaterVision).
    WorldRenderer::View& view = worldRenderer_->view;
    view.skyFlash = skyFlashTime_ > 0 ? float(skyFlashTime_) - partialTick : 0.0f;
    view.fluid = EnvironmentInputs::CameraFluid::None;
    const BlockPos camBlock = BlockPos::containing(camera_.position);
    if (const FluidState f = level_->fluid(camBlock); !f.empty()) {
        const double height = level_->fluid(camBlock.above()).kind == f.kind ? 1.0
                                                                               : double(f.source() || f.falling ? 8 : 8 - f.level) / 9.0;
        if (camera_.position.y < camBlock.y + height)
            view.fluid = f.kind == FluidKind::Water ? EnvironmentInputs::CameraFluid::Water : EnvironmentInputs::CameraFluid::Lava;
    }
    const float t = float(waterVisionTime_);
    view.waterVision = !player_->eyeInWater(*level_) ? 0.0f
                       : t >= 600.0f               ? 1.0f
                                                   : std::clamp(t / 100.0f, 0.0f, 1.0f) * 0.6f +
                                       (t < 100.0f ? 0.0f : std::clamp((t - 100.0f) / 500.0f, 0.0f, 1.0f)) * 0.39999998f;
    view.spectator = player_->mode() == GameMode::Spectator;
    view.waterFogColor = 0x050533;
    view.closerWaterFog = false;
    if (client_.biomes) {
        const Biome& biome = client_.biomes->get(level_->biomeAt(camBlock));
        view.waterFogColor = biome.waterFogColor;
        view.closerWaterFog = biome.id == "minecraft:swamp" || biome.id == "minecraft:mangrove_swamp";
    }
    worldRenderer_->render(camera_, fb, client_.options.renderDistance, *level_, partialTick);
    worldRenderer_->renderParticles(particles_, *level_, camera_, fb, partialTick);
    worldRenderer_->renderWeather(*level_, camera_, fb, tickCount_, partialTick);
    if (target_ && player_->mode() != GameMode::Spectator) {
        const int stage = breaking_.active && breaking_.pos == target_->pos ? int(breaking_.progress * 10.0f) - 1 : -1;
        const StateId s = level_->getBlock(target_->pos);
        Shape shape = client_.registry->outlineShape(s);
        const glm::dvec3 offset = client_.registry->shapeOffset(s, target_->pos);
        for (AABB& b : shape) b = b.moved(offset);
        overlay_->render(camera_, fb, target_->pos, shape, stage);
    }
    if (perspective_ == Perspective::FirstPerson && !player_->sleeping())
        worldRenderer_->renderHands(*player_, *level_, fb, partialTick,
                                    glm::rotate(glm::mat4(1.0f), glm::radians(camera_.roll), glm::vec3(0, 0, 1)) * camera_.bob);
    captureIcon(fb);
    renderHud(fb, partialTick, renderGui);
}

void WorldSession::message(const std::string& key) {
    ScreenContext ctx;
    ctx.lang = client_.lang.get();
    hud_->setOverlayMessage(ctx.translate(key));
}

bool WorldSession::useBed(const BlockPos& pos, StateId state) {
    // Vanilla BedBlock.useWithoutItem and ServerPlayer.startSleepInBed.
    const BlockRegistry& registry = *client_.registry;
    if (registry.blockOf(state).className != "bed") return false;
    BlockPos head = pos;
    StateId headState = state;
    const Direction facing = directionFromName(registry.value(state, "facing")).value_or(Direction::South);
    if (registry.value(state, "part") != "head") {
        head = pos.offset(facing);
        headState = level_->getBlock(head);
        if (registry.blockOf(headState).className != "bed") return true;
    }
    if (registry.boolValue(headState, "occupied")) {
        message("block.minecraft.bed.occupied");
        return true;
    }
    // Within 3 blocks across and 2 up or down of the head or the foot.
    auto near = [&](const BlockPos& b) {
        const glm::dvec3 c(b.x + 0.5, b.y, b.z + 0.5);
        return std::abs(player_->pos.x - c.x) <= 3.0 && std::abs(player_->pos.y - c.y) <= 2.0 &&
               std::abs(player_->pos.z - c.z) <= 3.0;
    };
    if (!near(head) && !near(head.offset(opposite(facing)))) {
        message("block.minecraft.bed.too_far_away");
        return true;
    }
    if (registry.info(level_->getBlock(head.offset(0, 1, 0))).flags & kOpaqueCube) {
        message("block.minecraft.bed.obstructed");
        return true;
    }
    if (player_->respawnPos != head) {
        player_->respawnPos = head;
        message("block.minecraft.set_spawn");
    }
    // Night or a thunderstorm: the sky darkened by 4 or more (vanilla isDarkOutside).
    if (level_->skyDarken() < 4) {
        message("block.minecraft.bed.no_sleep");
        return true;
    }
    // Monsters within 8 blocks across and 5 up or down keep a survival player up.
    if (player_->mode() != GameMode::Creative) {
        const glm::dvec3 c(head.x + 0.5, head.y, head.z + 0.5);
        for (Entity* e : level_->entitiesIn(AABB(c.x - 8.0, c.y - 5.0, c.z - 8.0, c.x + 8.0, c.y + 5.0, c.z + 8.0)))
            if (dynamic_cast<Monster*>(e)) {
                message("block.minecraft.bed.not_safe");
                return true;
            }
    }
    player_->sleepingPos = head;
    player_->sleepTimer = 0;
    player_->stopUsingItem();
    player_->pos = player_->oldPos = glm::dvec3(head.x + 0.5, head.y + 0.6875, head.z + 0.5);
    player_->velocity = glm::dvec3(0.0);
    // Facing the foot of the bed (vanilla Direction.toYRot - 180).
    const float facingYaw = facing == Direction::West ? 90.0f : facing == Direction::North ? 180.0f
                            : facing == Direction::East ? 270.0f : 0.0f;
    player_->yaw = player_->oldYaw = facingYaw - 180.0f;
    for (const BlockPos& part : {head, head.offset(opposite(facing))})
        if (auto occupied = registry.with(level_->getBlock(part), "occupied", "true")) level_->setBlock(part, *occupied);
    pendingScreen_ = "sleep";
    return true;
}

void WorldSession::wakeUp() {
    if (!player_->sleeping()) return;
    const BlockRegistry& registry = *client_.registry;
    const BlockPos head = *player_->sleepingPos;
    player_->sleepingPos.reset();
    player_->sleepTimer = 0;
    const StateId headState = level_->getBlock(head);
    if (registry.blockOf(headState).className == "bed") {
        const Direction facing = directionFromName(registry.value(headState, "facing")).value_or(Direction::South);
        for (const BlockPos& part : {head, head.offset(opposite(facing))})
            if (auto free = registry.with(level_->getBlock(part), "occupied", "false")) level_->setBlock(part, *free);
    }
    player_->pos = player_->oldPos = standUpPosition(head);
    if (pendingScreen_.empty()) pendingScreen_ = "close";
}

glm::dvec3 WorldSession::standUpPosition(const BlockPos& head) const {
    // Vanilla BedBlock.findStandUpPosition, simplified: a free spot on solid ground around
    // the head or the foot, else on top of the bed.
    const BlockRegistry& registry = *client_.registry;
    const StateId headState = level_->getBlock(head);
    const Direction facing = directionFromName(registry.value(headState, "facing")).value_or(Direction::South);
    Player probe;
    for (const BlockPos& part : {head, head.offset(opposite(facing))})
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                const BlockPos at = part.offset(dx, 0, dz);
                if (registry.blockOf(level_->getBlock(at)).className == "bed") continue;
                probe.pos = glm::dvec3(at.x + 0.5, at.y, at.z + 0.5);
                if (noCollision(*level_, probe.box()) && !noCollision(*level_, probe.box().moved({0.0, -0.5, 0.0})))
                    return probe.pos;
            }
    return glm::dvec3(head.x + 0.5, head.y + 0.5625, head.z + 0.5);
}

void WorldSession::tickSleep() {
    if (!player_->sleeping()) return;
    // The bed must still be there, and the night (or the storm) still dark.
    if (client_.registry->blockOf(level_->getBlock(*player_->sleepingPos)).className != "bed" || level_->skyDarken() < 4) {
        wakeUp();
        return;
    }
    player_->velocity = glm::dvec3(0.0);
    if (++player_->sleepTimer < 100) return;
    // Vanilla ServerLevel.tick: everyone (here, the one player) slept long enough: morning.
    if (level_->daylightCycle) {
        const int64_t t = level_->dayTime() + Level::kDayLength;
        level_->setDayTime(t - t % Level::kDayLength);
    }
    wakeUp();
    // Vanilla resetWeatherCycle: the rain is slept through too.
    if (level_->doWeatherCycle && level_->isRaining()) level_->setWeather(0, 0, false, false);
}

bool WorldSession::eatCake(const BlockPos& pos, StateId state) {
    // Vanilla CakeBlock.eat: a slice (2 food, 0.4 saturation) when hungry; the last one
    // takes the cake.
    const BlockRegistry& registry = *client_.registry;
    if (shortId(registry.blockOf(state).id) != "cake" || !player_->canEat(false)) return false;
    player_->food.eat(2, 0.4f);
    const int bites = registry.intValue(state, "bites");
    if (bites < 6) level_->setBlockAndUpdate(pos, *registry.with(state, "bites", std::to_string(bites + 1)));
    else level_->setBlockAndUpdate(pos, BlockRegistry::kAirState);
    return true;
}

void WorldSession::spawnBoneMealParticles(const BlockPos& pos, int count) {
    const BlockRegistry& reg = *client_.registry;
    Xoroshiro& r = particles_.random();
    // Vanilla ParticleUtils.spawnParticles: around a block, optionally only over ground.
    auto spawn = [&](const BlockPos& at, int n, double xzSpread, double ySpread, bool inAir) {
        for (int i = 0; i < n; ++i) {
            const double vx = r.nextGaussian() * 0.02, vy = r.nextGaussian() * 0.02, vz = r.nextGaussian() * 0.02;
            const double g = 0.5 - xzSpread;
            const double x = at.x + g + r.nextDouble() * xzSpread * 2.0;
            const double y = at.y + r.nextDouble() * ySpread;
            const double z = at.z + g + r.nextDouble() * xzSpread * 2.0;
            if (inAir || !(reg.info(level_->getBlock(BlockPos::containing({x, y, z}).below())).flags & kAir))
                particles_.add(ParticleType::HappyVillager, {x, y, z}, {vx, vy, vz});
        }
    };
    const StateId s = level_->getBlock(pos);
    const std::string_view id = shortId(reg.blockOf(s).id);
    if (id == "water") {
        spawn(pos, count * 3, 3.0, 1.0, false);
    } else if (id == "grass_block" || id == "moss_block") { // they spread their plants around
        spawn(pos.above(), count * 3, 3.0, 1.0, false);
    } else {
        double height = 1.0;
        if (!(reg.info(s).flags & kAir)) {
            height = 0.0;
            for (const AABB& b : reg.outlineShape(s)) height = std::max(height, b.max.y);
        }
        spawn(pos, count, 0.5, height, true);
    }
}

void WorldSession::tickClientParticles() {
    const BlockRegistry& reg = *client_.registry;
    const BlockPos center = BlockPos::containing(camera_.position);
    animateTick(*level_, center, particles_, [&](const BlockPos& p, StateId s) {
        const BiomeColors colors = client_.biomes ? client_.biomes->colors(level_->biomeAt(p), p.x, p.z) : BiomeColors{};
        return resolveTint(reg.blockOf(s).tint, 0, colors);
    });
    tickRainParticles(*level_, center, tickCount_, particles_);
    // Lit TNT smokes from its top (vanilla PrimedTnt.tick on the client); critical arrows
    // trail sparks (AbstractArrow.tick).
    for (const auto& e : level_->entities()) {
        if (e->removed) continue;
        if (dynamic_cast<const PrimedTnt*>(e.get())) particles_.add(ParticleType::Smoke, e->pos + glm::dvec3(0.0, 0.5, 0.0));
        if (const auto* a = dynamic_cast<const Arrow*>(e.get()); a && a->crit && !a->inGround)
            for (int i = 0; i < 4; ++i)
                particles_.add(ParticleType::Crit, a->oldPos + (a->pos - a->oldPos) * (double(i) / 4.0),
                               glm::dvec3(-a->velocity.x, -a->velocity.y + 0.2, -a->velocity.z));
    }
    // Critical hits throw sparks around the target for 3 ticks (vanilla TrackingEmitter).
    for (CritEmitter& c : critEmitters_) {
        Xoroshiro& r = particles_.random();
        for (int i = 0; i < 16; ++i) {
            const double dx = r.nextFloat() * 2.0f - 1.0f, dy = r.nextFloat() * 2.0f - 1.0f, dz = r.nextFloat() * 2.0f - 1.0f;
            if (dx * dx + dy * dy + dz * dz > 1.0) continue;
            particles_.add(ParticleType::Crit,
                           {c.pos.x + c.size.x * dx / 4.0, c.pos.y + c.size.y * (0.5 + dy / 4.0), c.pos.z + c.size.x * dz / 4.0},
                           {dx, dy + 0.2, dz});
        }
        ++c.life;
    }
    std::erase_if(critEmitters_, [](const CritEmitter& c) { return c.life >= 3; });
    // Vanilla Entity.doWaterSplashEffect: bubbles and a splash where the player falls in.
    if (player_->inWater && !wasInWater_ && !player_->noPhysics) {
        Xoroshiro& r = particles_.random();
        const double w = double(player_->width), y = std::floor(player_->pos.y) + 1.0;
        const glm::dvec3 v = player_->velocity;
        const int n = int(1.0f + player_->width * 20.0f);
        for (int i = 0; i < n; ++i) {
            const double dx = (r.nextDouble() * 2.0 - 1.0) * w, dz = (r.nextDouble() * 2.0 - 1.0) * w;
            particles_.add(ParticleType::Bubble, {player_->pos.x + dx, y, player_->pos.z + dz},
                           {v.x, v.y - r.nextDouble() * 0.2, v.z});
        }
        for (int i = 0; i < n; ++i) {
            const double dx = (r.nextDouble() * 2.0 - 1.0) * w, dz = (r.nextDouble() * 2.0 - 1.0) * w;
            particles_.add(ParticleType::Splash, {player_->pos.x + dx, y, player_->pos.z + dz}, v);
        }
    }
    wasInWater_ = player_->inWater;
    // A bolt flashes the sky while it strikes; the flash fades the tick after.
    if (skyFlashTime_ > 0) --skyFlashTime_;
    for (const auto& e : level_->entities())
        if (const auto* bolt = dynamic_cast<const LightningBolt*>(e.get()); bolt && !bolt->removed && bolt->life >= 0)
            skyFlashTime_ = 2;
    // Vanilla LocalPlayer: the eyes adjust under water over 30 seconds (spectators faster).
    if (player_->eyeInWater(*level_))
        waterVisionTime_ = std::min(waterVisionTime_ + (player_->mode() == GameMode::Spectator ? 10 : 1), 600);
    else
        waterVisionTime_ = std::max(waterVisionTime_ - 10, 0);
}

bool WorldSession::useHeldItemOn(const BlockPos& pos, Direction face) {
    ItemStack& held = player_->inventory.selectedStack();
    if (!held.empty() && spawnEggType(*held.item)) {
        if (!spawnFromEgg(*level_, *held.item, pos, face)) return false;
        if (player_->mode() != GameMode::Creative) held.shrink(1);
        return true;
    }
    if (!held.empty() && shortId(held.item->id) == "bone_meal") {
        if (!sim::applyBoneMeal(*level_, pos, face)) return false;
        if (player_->mode() != GameMode::Creative) held.shrink(1);
        return true;
    }
    const auto result = useItemOn(*level_, *client_.items, held, pos, face, horizontalFacing(player_->yaw));
    if (!result) return false;
    if (!level_->setBlockAndUpdate(result->pos, result->state)) return false;
    if (result->primeTnt) PrimedTnt::prime(*level_, result->pos);
    for (const ItemStack& drop : result->drops) ItemEntity::popResource(*level_, result->pos.offset(face), drop);
    if (player_->mode() != GameMode::Creative) {
        const Item* item = held.item;
        if (result->consume) held.shrink(1);
        if (held.hurt(result->wear)) spawnItemBreak(*item);
    }
    return true;
}

void WorldSession::startUsingItem() {
    // The main hand first, then the off hand (vanilla tries both).
    for (const bool offhand : {false, true}) {
        ItemStack& stack = offhand ? player_->inventory.offhand : player_->inventory.selectedStack();
        if (stack.empty()) continue;
        if (equipFromHand(stack) || useBucket(stack)) {
            player_->swing(offhand);
            return;
        }
        // Vanilla BowItem.use: drawn with arrows to shoot (creative players need none).
        if (stack.item->id == "minecraft:bow") {
            if (!player_->invulnerable() && !findArrows()) continue;
            player_->useItem = stack.item;
            player_->useItemRemaining = 72000;
            player_->useOffhand = offhand;
            return;
        }
        if (!stack.item->isFood() || !player_->canEat(stack.item->alwaysEdible)) continue;
        player_->useItem = stack.item;
        player_->useItemRemaining = stack.item->useTicks;
        player_->useOffhand = offhand;
        return;
    }
}

bool WorldSession::useBucket(ItemStack& stack) {
    // Vanilla BucketItem.use: an empty bucket aims at fluid sources (and waterlogged blocks),
    // a full one at blocks.
    const std::string_view id = shortId(stack.item->id);
    const FluidKind content = id == "water_bucket" ? FluidKind::Water : id == "lava_bucket" ? FluidKind::Lava : FluidKind::None;
    if (id != "bucket" && content == FluidKind::None) return false;
    if (player_->mode() == GameMode::Adventure || player_->mode() == GameMode::Spectator) return false;
    const BlockRegistry& reg = *client_.registry;
    const double reach = player_->mode() == GameMode::Creative ? 5.0 : 4.5;
    const auto hit = raycastBlocks(*level_, player_->eyePos(), player_->viewVector(), reach,
                                   content == FluidKind::None ? RayFluid::SourceOnly : RayFluid::None);
    if (!hit) return false;
    const StateId s = level_->getBlock(hit->pos);
    const bool creative = player_->mode() == GameMode::Creative;
    if (content == FluidKind::None) {
        // BucketPickup: a source block, or the water a block holds.
        const sim::Fluid f = sim::fluidOf(reg, s);
        const Item* filled = nullptr;
        if (reg.blockOf(s).className == "liquid" && f.source) {
            level_->setBlockAndUpdate(hit->pos, BlockRegistry::kAirState);
            filled = client_.items->find(f.kind == FluidKind::Lava ? "lava_bucket" : "water_bucket");
        } else if (reg.boolValue(s, "waterlogged")) {
            level_->setBlockAndUpdate(hit->pos, *reg.with(s, "waterlogged", "false"));
            filled = client_.items->find("water_bucket");
        }
        if (!filled) return false;
        // ItemUtils.createFilledResult: creative keeps the empty bucket and gets one filled
        // bucket at most; otherwise one bucket fills.
        ItemStack full{filled, 1};
        if (creative) {
            bool have = false;
            for (int i = 0; i <= Inventory::kOffhandSlot; ++i) have |= player_->inventory.slot(i).item == filled;
            if (!have) player_->inventory.add(full);
        } else if (stack.count == 1) {
            stack = full;
        } else {
            stack.shrink(1);
            if (!player_->inventory.add(full)) player_->drop(full);
        }
        return true;
    }
    // Into a block that holds water, else beside the face that was hit.
    const bool intoBlock = content == FluidKind::Water && !reg.value(s, "waterlogged").empty();
    if (!emptyBucket(intoBlock ? hit->pos : hit->pos.offset(hit->face), content, &*hit)) return false;
    if (!creative) stack = ItemStack{client_.items->find("bucket"), 1};
    return true;
}

bool WorldSession::emptyBucket(const BlockPos& pos, FluidKind content, const BlockHit* hit) {
    // Vanilla BucketItem.emptyContents: into air, a block the fluid replaces (anything not
    // solid, washed away with its drops), or a block that holds water.
    const BlockRegistry& reg = *client_.registry;
    const StateId s = level_->getBlock(pos);
    const StateInfo& info = reg.info(s);
    const bool waterloggable = !reg.value(s, "waterlogged").empty();
    const bool replaceable = (info.flags & kReplaceable) || !(info.flags & kBlocksMotion);
    const bool canPlace = (info.flags & kAir) || replaceable ||
                          (waterloggable && content == FluidKind::Water && !reg.boolValue(s, "waterlogged"));
    if (!canPlace) return hit && emptyBucket(hit->pos.offset(hit->face), content, nullptr);
    if (waterloggable && content == FluidKind::Water) {
        if (reg.boolValue(s, "waterlogged")) return false;
        level_->setBlockAndUpdate(pos, *reg.with(s, "waterlogged", "true"));
        level_->scheduleFluidTick(pos, sim::fluidTickType(FluidKind::Water, true), sim::fluidTickDelay(FluidKind::Water));
        return true;
    }
    if (replaceable && !(info.flags & kAir) && info.fluid.empty()) level_->destroyBlock(pos, true);
    const StateId source = sim::liquidBlock(reg, content, 8, false);
    if (level_->getBlock(pos) == source) return true;
    return level_->setBlockAndUpdate(pos, source) || sim::fluidOf(reg, s).source;
}

bool WorldSession::equipFromHand(ItemStack& stack) {
    // Vanilla Equippable.swapWithEquipmentSlot: armor (not heads or pumpkins, which aren't
    // swappable) goes on, and the hand takes what was worn; a creative player keeps theirs.
    const EquipmentSlot slot = stack.item->equipment;
    if (slot == EquipmentSlot::MainHand || slot == EquipmentSlot::OffHand) return false;
    const std::string_view id = shortId(stack.item->id);
    if (id.ends_with("_head") || id.ends_with("_skull") || id == "carved_pumpkin") return false;
    ItemStack& worn = player_->inventory.armor[size_t(int(slot) - int(EquipmentSlot::Feet))];
    if (!worn.empty() && worn.sameItem(stack)) return false; // the same thing: nothing to swap (vanilla FAIL)
    const bool creative = player_->mode() == GameMode::Creative;
    ItemStack putOn = stack;
    putOn.count = 1;
    if (stack.count <= 1) {
        ItemStack taken = std::exchange(worn, putOn);
        if (!taken.empty()) stack = taken;
        else if (!creative) stack = {};
    } else {
        ItemStack taken = std::exchange(worn, putOn);
        if (!creative) stack.shrink(1);
        if (!taken.empty() && !player_->inventory.add(taken)) player_->drop(taken);
    }
    return true;
}

void WorldSession::continueUsingItem(bool held) {
    ItemStack& stack = player_->useOffhand ? player_->inventory.offhand : player_->inventory.selectedStack();
    if (stack.empty() || stack.item != player_->useItem) {
        player_->stopUsingItem(); // the item left the hand
        return;
    }
    if (stack.item->id == "minecraft:bow") {
        if (held) --player_->useItemRemaining;
        else releaseBow(stack);
        return;
    }
    if (!held) {
        player_->stopUsingItem(); // released
        return;
    }
    auto crumbs = [&](int count) {
        ItemModelContext ctx;
        ctx.display = DisplayContext::Ground;
        std::vector<ItemLayer> layers;
        client_.assets->itemDefinitions().evaluate(stack.item->id, ctx, &client_.assets->grassColormap(), layers);
        for (const ItemLayer& l : layers)
            if (l.model) {
                particles_.addItemParticles(player_->eyePos(), player_->yaw, player_->pitch, l.model->particleSprite, count);
                break;
            }
    };
    // Vanilla Consumable.shouldEmitParticlesAndSounds: past the first 21.875%, every 4 ticks.
    const int total = stack.item->useTicks, remaining = player_->useItemRemaining;
    if (total - remaining > int(float(total) * 0.21875f) && remaining % 4 == 0) crumbs(5);
    if (--player_->useItemRemaining > 0) return;
    // Done: the food counts, one is used up, and a bowl or bottle is left.
    crumbs(16);
    const Item* item = stack.item;
    player_->food.eat(item->nutrition, item->saturation);
    player_->stopUsingItem();
    if (player_->mode() == GameMode::Creative) return;
    stack.shrink(1);
    if (!item->remainder) return;
    ItemStack left{item->remainder, 1};
    if (stack.empty()) stack = left;
    else if (!player_->inventory.add(left)) player_->drop(left);
}

bool WorldSession::openBlockMenu(const BlockPos& pos, StateId state) {
    if (openMenu_ && !openMenu_->closed()) return false; // its screen still uses it
    const Block& block = client_.registry->blockOf(state);
    const std::string_view id = shortId(block.id);
    std::unique_ptr<Menu> menu;
    MenuAccess access{pos, block.index};
    ScreenContext ctx;
    ctx.lang = client_.lang.get();
    const BlockRegistry& registry = *client_.registry;
    // Vanilla ChestBlock.isChestBlockedAt: a solid block on top keeps the lid shut.
    auto blocked = [&](const BlockPos& p) { return (registry.info(level_->getBlock(p.offset(0, 1, 0))).flags & kOpaqueCube) != 0; };
    if (id == "crafting_table") {
        auto m = std::make_unique<CraftingMenu>(player_->inventory, client_.recipes.get());
        m->access = access;
        menu = std::move(m);
    } else if (id == "chest" || id == "trapped_chest") {
        if (blocked(pos)) return true; // used, but nothing opens
        std::vector<std::shared_ptr<ContainerBlockEntity>> halves;
        auto here = std::dynamic_pointer_cast<ContainerBlockEntity>(level_->blockEntity(pos));
        if (!here) return false;
        // A double chest: the neighbor along the facing's clockwise side (left) or
        // counter-clockwise side (right); the right half shows first.
        const std::string_view type = registry.value(state, "type");
        if (type == "left" || type == "right") {
            const Direction facing = directionFromName(registry.value(state, "facing")).value_or(Direction::North);
            const Direction side = type == "left" ? rotateYClockwise(facing) : opposite(rotateYClockwise(facing));
            const BlockPos other = pos.offset(side);
            const StateId otherState = level_->getBlock(other);
            auto there = std::dynamic_pointer_cast<ContainerBlockEntity>(level_->blockEntity(other));
            if (there && registry.info(otherState).block == block.index && registry.value(otherState, "type") != type) {
                if (blocked(other)) return true;
                halves = type == "right" ? std::vector{here, there} : std::vector{there, here};
            }
        }
        if (halves.empty()) halves = {here};
        auto m = std::make_unique<ChestMenu>(player_->inventory, halves);
        m->access = access;
        m->title = ctx.translate(halves.size() == 2 ? "container.chestDouble" : "container.chest");
        menu = std::move(m);
    } else if (id == "barrel") {
        auto here = std::dynamic_pointer_cast<ContainerBlockEntity>(level_->blockEntity(pos));
        if (!here) return false;
        auto m = std::make_unique<ChestMenu>(player_->inventory, std::vector{here});
        m->access = access;
        m->title = ctx.translate("container.barrel");
        menu = std::move(m);
    } else if (id == "ender_chest") {
        if (blocked(pos)) return true;
        auto m = std::make_unique<ChestMenu>(player_->inventory, std::vector<std::shared_ptr<ContainerBlockEntity>>{},
                                             player_->enderItems,
                                             std::dynamic_pointer_cast<ContainerBlockEntity>(level_->blockEntity(pos)));
        m->access = access;
        m->title = ctx.translate("container.enderchest");
        menu = std::move(m);
    } else if (id == "furnace" || id == "smoker" || id == "blast_furnace") {
        auto furnace = std::dynamic_pointer_cast<FurnaceBlockEntity>(level_->blockEntity(pos));
        if (!furnace) return false;
        auto m = std::make_unique<FurnaceMenu>(player_->inventory, furnace, level_.get());
        m->access = access;
        menu = std::move(m);
    }
    if (!menu) return false;
    openMenu_ = std::move(menu);
    menuAccess_ = access;
    pendingScreen_ = "container";
    return true;
}

void WorldSession::checkOpenMenu() {
    // Vanilla AbstractSignEditScreen.isValid: the sign is still there and within reach
    // plus 4 (canInteractWithBlock), measured from the eyes to the block.
    if (editingSign_) {
        const BlockPos& p = editingSign_->pos;
        const glm::dvec3 eye = player_->pos + glm::dvec3(0.0, player_->eyeHeight(), 0.0);
        const glm::dvec3 nearest = glm::clamp(eye, glm::dvec3(p.x, p.y, p.z), glm::dvec3(p.x + 1, p.y + 1, p.z + 1));
        const double range = (player_->mode() == GameMode::Creative ? 5.0 : 4.5) + 4.0;
        if (editingSign_->removed || glm::dot(eye - nearest, eye - nearest) >= range * range || player_->dead()) {
            // Once: the editor closes (if it is open; a stale sign must not close other screens).
            editingSign_.reset();
            if (pendingScreen_.empty()) pendingScreen_ = "close";
        }
    }
    if (openMenu_ && openMenu_->closed()) openMenu_.reset();
    if (!openMenu_ || !menuAccess_) return;
    // Vanilla stillValid: the block is still there and the player within 8 blocks of it.
    const glm::dvec3 center = glm::dvec3(menuAccess_->pos.x, menuAccess_->pos.y, menuAccess_->pos.z) + 0.5;
    const glm::dvec3 d = player_->pos - center;
    // The menu's screen closes, unless another screen (death) is about to replace it anyway.
    if (client_.registry->info(level_->getBlock(menuAccess_->pos)).block != menuAccess_->block ||
        glm::dot(d, d) > 64.0 || player_->dead())
        if (pendingScreen_.empty()) pendingScreen_ = "close";
}

void WorldSession::die() {
    deathHandled_ = true;
    ScreenContext ctx;
    ctx.lang = client_.lang.get();
    // "Player was slain by Zombie": the attacker named by its type.
    std::string killer;
    if (!player_->lastAttackerType.empty()) {
        std::string key = "entity." + player_->lastAttackerType;
        std::replace(key.begin(), key.end(), ':', '.');
        killer = ctx.translate(key);
    }
    client_.chat->addMessage(ctx.translate(std::string(player_->deathMessageKey()), {"Player", killer}), client_.chatTicks);
    breaking_.active = false;
    // What menus hold (the cursor, crafting grids) goes back first, to drop with the rest.
    if (openMenu_) openMenu_->returnItems(*player_);
    inventoryMenu().returnItems(*player_);
    if (!level_->keepInventory) {
        // Everything scatters around the body, and up to 100 points of experience (7 per level).
        for (int i = 0; i <= Inventory::kOffhandSlot; ++i)
            ItemEntity::dropFromPlayer(*level_, *player_, std::exchange(player_->inventory.slot(i), ItemStack{}), true);
        ExperienceOrb::award(*level_, player_->pos, std::min(player_->experienceLevel * 7, 100));
        player_->experienceLevel = 0;
        player_->experienceProgress = 0.0f;
    }
    // A menu opened this very tick never gets its screen: close it here (its viewers too).
    // One with a screen closes when the death screen replaces it.
    if (pendingScreen_ == "container" && openMenu_) {
        openMenu_->removed(*player_);
        openMenu_.reset();
    }
    pendingScreen_ = "death";
}

void WorldSession::respawnPlayer() {
    // The bed, if it is still there; else the world spawn, on top of whatever stands there now.
    bool atBed = false;
    if (player_->respawnPos) {
        if (client_.registry->blockOf(level_->getBlock(*player_->respawnPos)).className == "bed") {
            player_->pos = standUpPosition(*player_->respawnPos);
            atBed = true;
        } else {
            player_->respawnPos.reset();
            message("block.minecraft.spawn.not_valid");
        }
    }
    if (!atBed) player_->pos = spawnOnTop();
    player_->oldPos = player_->pos;
    const bool keep = level_->keepInventory;
    const int levels = player_->experienceLevel;
    const float progress = player_->experienceProgress;
    player_->respawn();
    if (keep) {
        player_->experienceLevel = levels;
        player_->experienceProgress = progress;
    }
    if (params_.hardcore) player_->setMode(GameMode::Spectator); // Spectate World
    deathHandled_ = false;
}

void WorldSession::submitEntities(float partialTick, int skyDarken) {
    EntityRenderer& out = worldRenderer_->entities();
    // Opening and closing chests leave the chunk mesh and draw here with their lids raised
    // (vanilla ChestRenderer: -(1 - (1 - t)^3) x 90 degrees about the back hinge).
    std::unordered_set<BlockPos> moving;
    std::vector<BlockEntityPiece> pieces;
    for (const auto& [pos, be] : level_->blockEntities()) {
        const auto* chest = dynamic_cast<const ContainerBlockEntity*>(be.get());
        if (!chest || !chest->isChest() || !chest->lidMoving()) continue;
        moving.insert(pos);
        const StateId state = level_->getBlock(pos);
        float open = chest->openness(partialTick);
        // A double chest opens as one: the more open half leads.
        if (const std::string_view type = client_.registry->value(state, "type"); type == "left" || type == "right") {
            const Direction facing = directionFromName(client_.registry->value(state, "facing")).value_or(Direction::North);
            const Direction side = type == "left" ? rotateYClockwise(facing) : opposite(rotateYClockwise(facing));
            if (auto other = level_->blockEntities().find(pos.offset(side)); other != level_->blockEntities().end())
                if (const auto* half = dynamic_cast<const ContainerBlockEntity*>(other->second.get())) {
                    open = std::max(open, half->openness(partialTick));
                    moving.insert(other->first);
                }
        }
        open = 1.0f - open;
        open = 1.0f - open * open * open;
        pieces.clear();
        client_.assets->blockEntityModels().pieces(*client_.registry, state, pieces);
        const uint8_t l = level_->lightAt(pos);
        for (const BlockEntityPiece& piece : pieces) {
            ModelDefinition posed = *piece.model;
            for (const char* part : {"lid", "lock"})
                if (ModelPart* p = posed.root.find(part)) p->pose.rotation.x = -open * 1.5707964f;
            out.submitModel(posed.root, posed.texWidth, posed.texHeight, piece.texture, glm::dvec3(pos.x, pos.y, pos.z),
                            piece.transform, glm::vec2(float(l & 15), float(l >> 4)), piece.tint);
        }
    }
    // A chest whose partner keeps it moving draws through its own entry above as well.
    worldRenderer_->setDynamicBlockEntities(std::move(moving), level_->chunks());
    // Sign text (vanilla SignRenderer.renderSignText): through the sign's pose, offset and
    // scaled to 1/96 (hanging signs 1/64), lines 10 (9) apart, centered; dark unless glowing.
    std::vector<Font::GlyphQuad> glyphs;
    const Font& font = client_.gui->font();
    for (const auto& [pos, be] : level_->blockEntities()) {
        const auto* sign = dynamic_cast<const SignBlockEntity*>(be.get());
        if (!sign || (sign->front.empty() && sign->back.empty())) continue;
        const glm::dvec3 d = glm::dvec3(pos.x, pos.y, pos.z) - camera_.position;
        if (glm::dot(d, d) > 64.0 * 64.0) continue;
        const StateId state = level_->getBlock(pos);
        const std::string& cls = client_.registry->blockOf(state).className;
        glm::mat4 base(1.0f);
        if (sign->hanging) {
            base = glm::translate(base, {0.5f, 0.9375f, 0.5f});
            base = glm::rotate(base, glm::radians(-signYaw(state)), glm::vec3(0, 1, 0));
            base = glm::translate(base, {0.0f, -0.3125f, 0.0f});
        } else {
            base = glm::translate(base, glm::vec3(0.5f));
            base = glm::rotate(base, glm::radians(-signYaw(state)), glm::vec3(0, 1, 0));
            if (cls == "wall_sign") base = glm::translate(base, {0.0f, -0.3125f, -0.4375f});
        }
        const glm::vec3 offset = sign->hanging ? glm::vec3(0.0f, -0.32f, 0.073f) : glm::vec3(0.0f, 0.33333334f, 0.046666667f);
        const float scale = (sign->hanging ? 0.9f : 0.6666667f) / 64.0f; // getSignTextRenderScale
        const uint8_t l = level_->lightAt(pos);
        for (const bool front : {true, false}) {
            const SignBlockEntity::Side& side = front ? sign->front : sign->back;
            if (side.empty()) continue;
            glm::mat4 m = front ? base : glm::rotate(base, 3.1415927f, glm::vec3(0, 1, 0));
            m = glm::translate(m, offset);
            m = glm::scale(m, {scale, -scale, scale});
            // getDarkColor: the dye at 0.4 brightness (cream for glowing black, its outline).
            const uint32_t textColor = dyeTextColor(side.color);
            uint32_t dark = uint32_t(float((textColor >> 16) & 0xFF) * 0.4f) << 16 |
                            uint32_t(float((textColor >> 8) & 0xFF) * 0.4f) << 8 | uint32_t(float(textColor & 0xFF) * 0.4f);
            if (side.glowing && side.color == "black") dark = 0xF0EBCC;
            // Glowing text is full-bright with an outline in the dark color (always for black,
            // else within 16 blocks); plain text is the dark color in the block's light.
            const bool outline = side.glowing && (side.color == "black" || glm::dot(d + 0.5, d + 0.5) < 16.0 * 16.0);
            const glm::vec2 light = side.glowing ? glm::vec2(15.0f) : glm::vec2(float(l & 15), float(l >> 4));
            const int lineHeight = sign->lineHeight(), top = 4 * lineHeight / 2;
            std::array<std::string, 4> lines;
            for (size_t i = 0; i < 4; ++i) {
                // Each line shows only what fits the sign (the first piece of a split).
                const auto pieces = font.split(side.lines[i], float(sign->maxLineWidth()));
                if (!pieces.empty()) lines[i] = pieces.front();
            }
            const glm::dvec3 origin(pos.x, pos.y, pos.z);
            if (outline) {
                glyphs.clear();
                for (int ox = -1; ox <= 1; ++ox)
                    for (int oy = -1; oy <= 1; ++oy)
                        if (ox != 0 || oy != 0)
                            for (int i = 0; i < 4; ++i)
                                font.glyphQuads(lines[size_t(i)], float(-font.width(lines[size_t(i)]) / 2 + ox),
                                                float(i * lineHeight - top + oy), 0xFF000000u | dark, glyphs);
                out.submitText(glyphs, font.texture(), origin, m, light);
                // The text itself in front of its outline (vanilla draws it with a polygon offset).
                m = glm::translate(m, {0.0f, 0.0f, 0.1f});
            }
            glyphs.clear();
            for (int i = 0; i < 4; ++i)
                font.glyphQuads(lines[size_t(i)], float(-font.width(lines[size_t(i)]) / 2), float(i * lineHeight - top),
                                0xFF000000u | (side.glowing ? textColor : dark), glyphs);
            out.submitText(glyphs, font.texture(), origin, m, light);
        }
    }
    // Vanilla shouldRenderAtSqrDistance: 4 x box size x 64 blocks (items 64, orbs 128),
    // scaled by the Entity Distance option.
    const double viewScale = client_.options.entityDistance;
    auto near = [&](const Entity& e, double limit) {
        const glm::dvec3 d = e.pos - camera_.position;
        return glm::dot(d, d) < limit * limit * viewScale * viewScale;
    };
    for (const auto& e : level_->entities()) {
        if (e->removed) continue;
        if (const auto* item = dynamic_cast<const ItemEntity*>(e.get()); item && near(*item, 64.0))
            out.submitGroundItem(*item, *level_, partialTick, skyDarken);
        else if (const auto* orb = dynamic_cast<const ExperienceOrb*>(e.get()); orb && near(*orb, 128.0))
            out.submitExperienceOrb(*orb, *level_, partialTick, skyDarken);
        else if (const auto* block = dynamic_cast<const FallingBlockEntity*>(e.get()); block && near(*block, 160.0))
            out.submitFallingBlock(*block, *level_, partialTick, skyDarken);
        else if (const auto* tnt = dynamic_cast<const PrimedTnt*>(e.get()); tnt && near(*tnt, 160.0))
            out.submitPrimedTnt(*tnt, *level_, partialTick, skyDarken);
        else if (const auto* bolt = dynamic_cast<const LightningBolt*>(e.get())) // seen from anywhere
            out.submitLightning(*bolt, partialTick);
        else if (const auto* mob = dynamic_cast<const Mob*>(e.get());
                 mob && near(*mob, double(mob->width * 2.0f + mob->height) / 3.0 * 64.0))
            out.submitMob(*mob, *level_, partialTick, skyDarken);
        else if (const auto* arrow = dynamic_cast<const Arrow*>(e.get()); arrow && near(*arrow, 64.0))
            out.submitArrow(*arrow, *level_, partialTick);
    }
    // Pickups fly to the player's middle, easing in over 3 ticks (vanilla ItemPickupParticle).
    const glm::dvec3 target = glm::mix(player_->oldPos, player_->pos, double(partialTick)) + glm::dvec3(0.0, 0.5, 0.0);
    for (const PickupAnimation& a : pickups_) {
        float f = (float(a.life) + partialTick) / 3.0f;
        f *= f;
        const glm::dvec3 at = glm::mix(a.entity->pos, target, double(f));
        if (auto* item = dynamic_cast<ItemEntity*>(a.entity.get())) {
            ItemEntity shown = *item;
            shown.stack.count = a.count;
            out.submitGroundItem(shown, *level_, 0.0f, skyDarken, &at);
        } else if (const auto* orb = dynamic_cast<const ExperienceOrb*>(a.entity.get())) {
            out.submitExperienceOrb(*orb, *level_, 0.0f, skyDarken, &at);
        }
    }
}

bool WorldSession::controlDown(KeyAction action) const {
    if (!client_.screenOpen && client_.window.cursorCaptured() &&
        client_.window.input().down(client_.options.keys[action]))
        return true;
    if (!client_.screenOpen && script_ && script_->held(action)) return true;
    // --hold names the action without vanilla's "key." ("forward", "attack").
    const auto& hold = client_.cli.hold;
    const std::string_view name = std::string_view(keyMappingInfo(action).name).substr(4);
    return params_.commandLine && client_.harnessRunning && std::find(hold.begin(), hold.end(), name) != hold.end();
}

PlayerInput WorldSession::readPlayerInput() {
    PlayerInput in;
    const bool jumpDown = controlDown(KeyAction::Jump);
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
    in.forward = float(controlDown(KeyAction::Forward)) - float(controlDown(KeyAction::Back));
    in.strafe = float(controlDown(KeyAction::Left)) - float(controlDown(KeyAction::Right));
    in.jump = jumpDown;
    in.sneak = controlDown(KeyAction::Sneak);
    in.sprint = controlDown(KeyAction::Sprint);
    // Eating slows to a fifth and stops sprinting (vanilla LocalPlayer.aiStep).
    if (player_->usingItem()) {
        in.forward *= 0.2f;
        in.strafe *= 0.2f;
        in.sprint = false;
    }
    return in;
}

void WorldSession::updateTarget(float partialTick) {
    target_.reset();
    entityTarget_ = 0;
    // Vanilla GameRenderer.pick: block_interaction_range 4.5 and entity_interaction_range 3
    // (both 5 in creative), from the eyes in every view; an entity before the block wins.
    const bool creative = player_->mode() == GameMode::Creative;
    const double blockReach = creative ? 5.0 : 4.5, entityReach = creative ? 5.0 : 3.0;
    const double pick = std::max(blockReach, entityReach);
    const glm::dvec3 eye =
        glm::mix(player_->oldPos, player_->pos, double(partialTick)) + glm::dvec3(0.0, player_->eyeHeight(), 0.0);
    const glm::dvec3 view = player_->viewVector();
    const std::optional<BlockHit> block = raycastBlocks(*level_, eye, view, pick);
    const double blockSq = block ? block->distance * block->distance : pick * pick;
    if (player_->mode() != GameMode::Spectator) {
        double best = blockSq;
        const Entity* hit = nullptr;
        const AABB area = player_->box().expandedTowards(view * pick).inflated(1.0, 1.0, 1.0);
        for (const Entity* e : level_->entitiesIn(area, player_.get())) {
            if (!e->isPickable()) continue;
            const AABB b = e->box();
            if (b.contains(eye)) {
                hit = e;
                best = 0.0;
                break;
            }
            if (auto t = clipBox(b, eye, view, pick); t && t->first * t->first < best) {
                best = t->first * t->first;
                hit = e;
            }
        }
        if (hit && best < blockSq) {
            if (best <= entityReach * entityReach) entityTarget_ = hit->id;
            return; // the entity hides the block; out of reach, nothing is targeted
        }
    }
    if (block && block->distance <= blockReach) target_ = block;
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

std::vector<ItemStack> WorldSession::blockDrops(StateId state, const ItemStack& held) {
    const bool shears = !held.empty() && held.item->tool == ToolKind::Shears;
    return client_.loot->drops(state, shears, level_->random());
}

void WorldSession::spawnItemBreak(const Item& item) {
    ItemModelContext ctx;
    ctx.display = DisplayContext::Ground;
    std::vector<ItemLayer> layers;
    client_.assets->itemDefinitions().evaluate(item.id, ctx, &client_.assets->grassColormap(), layers);
    for (const ItemLayer& l : layers)
        if (l.model) {
            particles_.addItemParticles(player_->eyePos(), player_->yaw, player_->pitch, l.model->particleSprite, 5);
            break;
        }
}

void WorldSession::breakBlock(const BlockPos& p) {
    const BlockRegistry& registry = *client_.registry;
    const StateId s = level_->getBlock(p);
    spawnBlockParticles(p, s, std::nullopt);
    // Vanilla preventDropFromBottomPart: in creative the lower half of a door or tall plant
    // goes quietly when its upper half is broken.
    if (player_->mode() == GameMode::Creative && registry.value(s, "half") == "upper") {
        const BlockPos below = p.offset(0, -1, 0);
        const StateId lower = level_->getBlock(below);
        if (registry.info(lower).block == registry.info(s).block && registry.value(lower, "half") == "lower")
            level_->setBlock(below, registry.boolValue(lower, "waterlogged") ? registry.defaultState("water")
                                                                             : BlockRegistry::kAirState);
    }
    // A waterlogged block leaves its water behind.
    const bool waterlogged = registry.boolValue(s, "waterlogged") || registry.blockOf(s).className == "water_plant";
    // Containers spill what they hold; furnaces give up the experience they gathered.
    if (auto be = level_->blockEntity(p)) {
        for (const ItemStack& stack : be->contents()) ItemEntity::spill(*level_, p, stack);
        if (auto* furnace = dynamic_cast<FurnaceBlockEntity*>(be.get()))
            if (const int xp = furnace->takeExperience(level_->random().nextFloat()); xp > 0)
                ExperienceOrb::award(*level_, glm::dvec3(p.x, p.y, p.z) + 0.5, xp);
        if (auto* container = dynamic_cast<ContainerBlockEntity*>(be.get())) container->items = {};
        if (auto* furnace = dynamic_cast<FurnaceBlockEntity*>(be.get())) furnace->items = {};
    }
    if (!level_->setBlockAndUpdate(p, waterlogged ? registry.defaultState("water") : BlockRegistry::kAirState)) return;
    if (player_->mode() == GameMode::Creative) return;
    // Survival: the drops (when the right tool, if any, was used), then the tool's wear.
    player_->causeFoodExhaustion(0.005f);
    ItemStack& held = player_->inventory.selectedStack();
    if (canHarvest(held, s, registry)) {
        for (const ItemStack& drop : blockDrops(s, held)) ItemEntity::popResource(*level_, p, drop);
        if (const int xp = client_.loot->experience(s, level_->random()); xp > 0)
            ExperienceOrb::award(*level_, glm::dvec3(p.x, p.y, p.z) + 0.5, xp);
    }
    const Item* tool = held.item;
    if (held.hurt(miningWear(held, s, registry))) spawnItemBreak(*tool);
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
    // A new sign asks for its text (vanilla SignItem: openTextEdit on the front).
    if (const std::string& cls = registry.blockOf(blocks.front().second).className;
        cls == "sign" || cls == "wall_sign" || cls == "hanging_sign" || cls == "wall_hanging_sign")
        editSign(blocks.front().first, true);
    return true;
}

void WorldSession::stopEditingSign() { editingSign_.reset(); }

void WorldSession::editSign(const BlockPos& pos, bool front) {
    auto sign = std::dynamic_pointer_cast<SignBlockEntity>(level_->blockEntity(pos));
    if (!sign || sign->waxed) return;
    editingSign_ = sign;
    editingFront_ = front;
    pendingScreen_ = "sign";
}

float WorldSession::signYaw(StateId state) const {
    // Vanilla SignBlock.getYRotationDegrees: standing signs by rotation (16 steps), the
    // others by facing.
    const BlockRegistry& registry = *client_.registry;
    if (registry.blockOf(state).className == "sign" || registry.blockOf(state).className == "hanging_sign")
        return float(registry.intValue(state, "rotation")) * 22.5f;
    const Direction facing = directionFromName(registry.value(state, "facing")).value_or(Direction::South);
    return facing == Direction::West ? 90.0f : facing == Direction::North ? 180.0f : facing == Direction::East ? 270.0f : 0.0f;
}

bool WorldSession::useSign(const BlockPos& pos, StateId state) {
    const std::string& cls = client_.registry->blockOf(state).className;
    if (cls != "sign" && cls != "wall_sign" && cls != "hanging_sign" && cls != "wall_hanging_sign") return false;
    auto sign = std::dynamic_pointer_cast<SignBlockEntity>(level_->blockEntity(pos));
    // Only builders write on signs (vanilla mayBuild: not in adventure or spectator).
    if (!sign || player_->mode() == GameMode::Adventure || player_->mode() == GameMode::Spectator) return false;
    if (sign->waxed) return true; // nothing changes a waxed sign
    // Vanilla isFacingFrontText: the front faces the player within 90 degrees, measured from
    // the sign's hitbox center (a wall sign's is against its wall).
    glm::dvec2 center(0.5);
    if (cls == "wall_sign") {
        const Direction facing = directionFromName(client_.registry->value(state, "facing")).value_or(Direction::South);
        const glm::ivec3 n = normal(facing);
        center -= 0.4375 * glm::dvec2(n.x, n.z);
    }
    const double dx = player_->pos.x - (pos.x + center.x), dz = player_->pos.z - (pos.z + center.y);
    const float toPlayer = float(std::atan2(dz, dx) * 180.0 / 3.14159265358979) - 90.0f;
    const bool front = std::abs(wrapDegrees(toPlayer - signYaw(state))) <= 90.0f;
    SignBlockEntity::Side& side = front ? sign->front : sign->back;
    ItemStack& held = player_->inventory.selectedStack();
    const std::string_view item = held.empty() ? std::string_view() : shortId(held.item->id);
    // Sign applicators (vanilla SignApplicator): dyes color the side's text, glow ink lights
    // it, an ink sac puts it out (all only on a side with text), honeycomb waxes the sign.
    // One that changes nothing falls through to editing, like an empty hand.
    bool applied = false;
    if (item.ends_with("_dye") && !side.empty()) {
        const std::string color(item.substr(0, item.size() - 4));
        applied = side.color != color;
        side.color = color;
    } else if (item == "glow_ink_sac" && !side.empty()) {
        applied = !side.glowing;
        side.glowing = true;
    } else if (item == "ink_sac" && !side.empty()) {
        applied = side.glowing;
        side.glowing = false;
    } else if (item == "honeycomb") {
        applied = sign->waxed = true;
    }
    if (applied) {
        if (player_->mode() != GameMode::Creative) held.shrink(1);
        return true;
    }
    editSign(pos, front);
    return true;
}

void WorldSession::tickInteraction() {
    const BlockRegistry& registry = *client_.registry;
    // Nothing reaches the world while a screen is open (vanilla handles its keys only then).
    if (client_.screenOpen) {
        breaking_.active = false;
        return;
    }
    if (!target_ || player_->mode() == GameMode::Spectator) breaking_.active = false;
    if (destroyDelay_ > 0) --destroyDelay_;
    if (useDelay_ > 0) --useDelay_;
    const bool attackHeld = controlDown(KeyAction::Attack);
    const bool useHeld = controlDown(KeyAction::Use);
    // Pick block (creative): the target's block into the selected slot.
    if (pickClicks_ > 0 && target_ && player_->mode() == GameMode::Creative)
        if (const Item* item = client_.items->forBlock(registry.blockOf(level_->getBlock(target_->pos))))
            player_->inventory.pick(*item);
    // Use (vanilla Minecraft.startUseItem): on click, then every 4 ticks while held
    // (rightClickDelay). In order: the block's menu, the block's own reaction (doors, levers,
    // a cake), the item on the block (tools, placing), then the item by itself (food).
    // Sneaking with an item in hand skips the block's part. Eating holds the button. A mob
    // under the crosshair takes the click first (feeding, shearing, spawn eggs).
    if (player_->usingItem()) {
        continueUsingItem(useHeld);
    } else if (player_->mode() != GameMode::Spectator && (useClicks_ > 0 || (useHeld && useDelay_ == 0))) {
        useDelay_ = 4;
        bool done = false;
        if (Entity* e = level_->entity(entityTarget_)) {
            done = useOnEntity(*e); // it swings the hand itself
        } else if (target_) {
            const StateId s = level_->getBlock(target_->pos);
            const bool bypass = player_->crouching && !player_->inventory.selectedStack().empty();
            const auto use = bypass ? std::nullopt : useBlock(registry, s, horizontalFacing(player_->yaw));
            if (!bypass && useBed(target_->pos, s)) {
                done = true;
            } else if (!bypass && useSign(target_->pos, s)) {
                done = true;
            } else if (!bypass && openBlockMenu(target_->pos, s)) {
                done = true;
            } else if (use) {
                if (use->state != s) level_->setBlockAndUpdate(target_->pos, use->state);
                if (use->tickDelay > 0) level_->scheduleTick(target_->pos, registry.info(use->state).block, use->tickDelay);
                done = true;
            } else if (!bypass && eatCake(target_->pos, s)) {
                done = true;
            } else if (useHeldItemOn(target_->pos, target_->face)) {
                done = true;
            } else if (placeBlock()) {
                done = true;
            }
            if (done) player_->swing();
        }
        if (!done) startUsingItem();
    }
    tickAttack(attackHeld);
    // Vanilla counts the miss pause down after handling the keys.
    if (missTime_ > 0) --missTime_;
}

void WorldSession::tickAttack(bool attackHeld) {
    const BlockRegistry& registry = *client_.registry;
    // Attack (vanilla startAttack per click, continueAttack while held): creative breaks at
    // once (5-tick pause between blocks); survival accumulates progress. Nothing while using
    // an item, or for 10 ticks after swinging at nothing outside creative (missTime). A click
    // swings even at nothing, which also resets the attack strength; holding on a block
    // swings every tick.
    const bool blocked = missTime_ > 0 || player_->usingItem() || player_->mode() == GameMode::Spectator;
    // A click on an entity attacks it (vanilla Player.attack), never a held button.
    if (Entity* e = level_->entity(entityTarget_)) {
        breaking_.active = false;
        if (attackClicks_ > 0 && !blocked) {
            const ItemStack before = player_->inventory.selectedStack();
            playerAttack(*level_, *player_, *e);
            if (!before.empty() && player_->inventory.selectedStack().empty()) spawnItemBreak(*before.item);
            player_->swing();
        }
        return;
    }
    if (attackClicks_ > 0 && !blocked) {
        if (!target_) {
            if (player_->mode() != GameMode::Creative) missTime_ = 10;
            player_->resetAttackStrengthTicker();
        }
        player_->swing();
    }
    const bool attacking = attackClicks_ > 0 || attackHeld;
    if (!attacking || blocked || !target_ || player_->mode() == GameMode::Adventure) {
        breaking_.active = false;
        return;
    }
    player_->swing();
    const BlockPos p = target_->pos;
    const StateId s = level_->getBlock(p);
    if (player_->mode() == GameMode::Creative) {
        if (destroyDelay_ == 0 || attackClicks_ > 0) {
            breakBlock(p);
            destroyDelay_ = 5;
        }
        return;
    }
    if (destroyDelay_ > 0) return;
    const bool started = !breaking_.active || breaking_.pos != p;
    if (started) breaking_ = {p, 0.0f, true};
    breaking_.progress += destroyProgressPerTick(s, *player_, registry);
    spawnBlockParticles(p, s, target_->face);
    if (breaking_.progress >= 1.0f) {
        breakBlock(p);
        breaking_.active = false;
        // Vanilla: a block broken on the first tick (startDestroyBlock) leaves no pause.
        destroyDelay_ = started ? 0 : 5;
    }
}

bool WorldSession::useOnEntity(Entity& e) {
    auto* mob = dynamic_cast<Mob*>(&e);
    if (!mob || !mob->alive()) return false;
    // Vanilla Player.interactOn, the main hand then the off hand; creative players keep what
    // the interaction would have used up.
    for (const bool offhand : {false, true}) {
        ItemStack& held = offhand ? player_->inventory.offhand : player_->inventory.selectedStack();
        const ItemStack before = held;
        if (!mob->interact(*level_, *player_, held)) continue;
        if (player_->mode() == GameMode::Creative && held.item == before.item) held = before;
        player_->swing(offhand);
        return true;
    }
    return false;
}

ItemStack* WorldSession::findArrows() {
    // Vanilla getProjectile: arrows in the off hand, then the main hand, then the inventory.
    const ItemRegistry& items = *client_.items;
    auto isArrow = [&](const ItemStack& s) { return !s.empty() && items.inTag(*s.item, "arrows"); };
    if (isArrow(player_->inventory.offhand)) return &player_->inventory.offhand;
    if (isArrow(player_->inventory.selectedStack())) return &player_->inventory.selectedStack();
    for (int i = 0; i < Inventory::kMainSize; ++i)
        if (isArrow(player_->inventory.slot(i))) return &player_->inventory.slot(i);
    return nullptr;
}

void WorldSession::releaseBow(ItemStack& bow) {
    // Vanilla BowItem.releaseUsing: the draw's power (full after a second), an arrow at
    // 3x that from the eyes, critical at full power; the bow wears and the arrow is used.
    const int charge = 72000 - player_->useItemRemaining;
    player_->stopUsingItem();
    float power = float(charge) / 20.0f;
    power = std::min((power * power + power * 2.0f) / 3.0f, 1.0f);
    if (power < 0.1f) return;
    const bool creative = player_->mode() == GameMode::Creative;
    ItemStack* ammo = findArrows();
    if (!ammo && !creative) return;
    auto arrow = std::make_unique<Arrow>();
    arrow->owner = player_->id;
    arrow->pos = arrow->oldPos = glm::dvec3(player_->pos.x, player_->eyePos().y - 0.1, player_->pos.z);
    arrow->shootFromRotation(*player_, player_->pitch, player_->yaw, power * 3.0f, 1.0f, level_->random());
    arrow->crit = power == 1.0f;
    arrow->pickup = creative ? Arrow::Pickup::CreativeOnly : Arrow::Pickup::Allowed;
    arrow->pickupItem = ItemStack{ammo ? ammo->item : client_.items->find("arrow"), 1};
    level_->addEntity(std::move(arrow));
    if (creative) return;
    const Item* bowItem = bow.item;
    if (bow.hurt(1)) {
        bow = ItemStack{};
        spawnItemBreak(*bowItem);
    }
    if (ammo) ammo->shrink(1);
}

void WorldSession::spawnEntityParticles(const LevelEvent& e) {
    Xoroshiro& r = particles_.random();
    const double w = double(e.data & 0xFFFF) / 100.0, h = double(e.data >> 16) / 100.0;
    // Vanilla getRandomX/Y/Z: anywhere across the box.
    auto randomIn = [&] {
        return glm::dvec3(e.point.x + w * (2.0 * r.nextDouble() - 1.0), e.point.y + h * r.nextDouble(),
                          e.point.z + w * (2.0 * r.nextDouble() - 1.0));
    };
    switch (e.type) {
    case LevelEventType::EntityPoof: // vanilla makePoofParticles
        for (int i = 0; i < 20; ++i) {
            const glm::dvec3 v(r.nextGaussian() * 0.02, r.nextGaussian() * 0.02, r.nextGaussian() * 0.02);
            particles_.add(ParticleType::Poof, randomIn() - v * 10.0, v);
        }
        break;
    case LevelEventType::Hearts:
        for (int i = 0; i < int(e.state); ++i) {
            const glm::dvec3 v(r.nextGaussian() * 0.02, r.nextGaussian() * 0.02, r.nextGaussian() * 0.02);
            particles_.add(ParticleType::Heart, randomIn() + glm::dvec3(0.0, 0.5, 0.0), v);
        }
        break;
    case LevelEventType::Crit: critEmitters_.push_back({e.point, glm::dvec2(w, h), 0}); break;
    case LevelEventType::DamageIndicator:
        for (int i = 0; i < int(e.state); ++i) {
            const glm::dvec3 at(e.point.x + r.nextGaussian() * 0.1, e.point.y + h * 0.5, e.point.z + r.nextGaussian() * 0.1);
            particles_.add(ParticleType::DamageIndicator, at,
                           {r.nextGaussian() * 0.2, r.nextGaussian() * 0.2, r.nextGaussian() * 0.2});
        }
        break;
    case LevelEventType::Sweep: particles_.add(ParticleType::SweepAttack, e.point); break;
    default: break;
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
    hud_->attackIndicator = client_.options.attackIndicator;
    // Vanilla renderCrosshair's full indicator: a living target and a weapon worth waiting for.
    const auto* aimed = dynamic_cast<const LivingEntity*>(level_->entity(entityTarget_));
    hud_->attackReady = aimed && aimed->alive() && player_->attackStrengthScale(0.0f) >= 1.0f &&
                        player_->attackStrengthDelay() > 5.0f;
    hud_->hardcore = params_.hardcore;
    // What covers the view: vanilla ScreenEffectRenderer in first person out of bed (a block
    // the eyes are in, water, flames), then Gui.renderCameraOverlays (the vignette with
    // Fancy graphics, a carved pumpkin's blur in first person).
    ScreenOverlays overlays;
    if (perspective_ == Perspective::FirstPerson && !player_->sleeping()) {
        if (!player_->noPhysics) overlays.inWall = viewBlockingSprite();
        if (player_->mode() != GameMode::Spectator) {
            if (player_->eyeInWater(*level_)) {
                overlays.underwater = true;
                overlays.underwaterBrightness = eyeBrightness();
                overlays.yaw = player_->yaw;
                overlays.pitch = player_->pitch;
            }
            if (player_->onFire() && !player_->fireImmune())
                overlays.fire = &client_.assets->blockAtlas().sprite("minecraft:block/fire_1");
        }
        const ItemStack& head = player_->inventory.armor[3];
        overlays.pumpkin = !head.empty() && head.item->id == "minecraft:carved_pumpkin";
    }
    if (client_.options.fancyGraphics()) overlays.vignette = vignette_;
    renderScreenOverlays(gui, overlays, client_.assets->blockAtlasTexture());
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

const SpriteInfo* WorldSession::viewBlockingSprite() const {
    const BlockRegistry& registry = *client_.registry;
    const glm::dvec3 eye = player_->eyePos();
    for (int i = 0; i < 8; ++i) {
        const double x = player_->pos.x + (double(i & 1) - 0.5) * player_->width * 0.8;
        const double y = eye.y + (double((i >> 1) & 1) - 0.5) * 0.1;
        const double z = player_->pos.z + (double((i >> 2) & 1) - 0.5) * player_->width * 0.8;
        const StateId s = level_->getBlock(BlockPos::containing(glm::dvec3(x, y, z)));
        if (!(registry.info(s).flags & kOpaqueCube)) continue;
        const auto choices = registry.modelChoices(s);
        if (choices.empty() || choices[0]->models.empty()) continue;
        return &client_.assets->blockAtlas().sprite(choices[0]->models[0]->particleSprite);
    }
    return nullptr;
}

float WorldSession::eyeBrightness() const {
    const uint8_t packed = level_->lightAt(BlockPos::containing(player_->eyePos()));
    return lightBrightness(std::max(int(packed >> 4) - level_->skyDarken(), int(packed & 15)));
}

void WorldSession::renderDebugOverlay(GuiGraphics& gui) {
    const BlockRegistry& registry = *client_.registry;
    std::vector<std::string> lines;
    lines.push_back("Minecraft 1.21.8 (minecraft-cpp)");
    const glm::dvec3& p = camera_.position;
    const auto& r = worldRenderer_->chunkRenderer().stats();
    lines.push_back(std::format("{:.0f} fps  C: {}/{} sections ({} occluded), {} draws", fps, r.drawn, r.sections,
                                r.occluded, r.draws));
    const auto st = level_->chunks().stats();
    lines.push_back(std::format("Chunks: {} loaded, {} lit, {} ready, {} jobs", st.entries, st.lit, st.meshReady, st.inFlight));
    // Vanilla's entity line, and the mob caps (vanilla F3's "SC" spawn counts).
    lines.push_back(std::format("E: {}  M: {}/{}  A: {}/{}", level_->entities().size(),
                                spawner::mobCount(*level_, MobCategory::Monster), spawner::localCap(*level_, MobCategory::Monster),
                                spawner::mobCount(*level_, MobCategory::Creature),
                                spawner::localCap(*level_, MobCategory::Creature)));
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
    if (const Entity* e = level_->entity(entityTarget_)) {
        lines.push_back("");
        lines.push_back("Targeted Entity");
        lines.push_back(std::string(e->type()));
        if (const auto* mob = dynamic_cast<const Mob*>(e))
            lines.push_back(std::format("Health: {:.1f}/{:.0f}{}", mob->health, mob->maxHealth, mob->isBaby() ? " (baby)" : ""));
    }
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
    return std::format("chunks {} (lit {}, ready {}, jobs {})  |  sections {}/{} ({} occluded), {} vertices  |  {:.0f} MB "
                       "VRAM  |  {:.1f} {:.1f} {:.1f}",
                       s.entries, s.lit, s.meshReady, s.inFlight, r.drawn, r.sections, r.occluded, r.vertices,
                       double(worldRenderer_->chunkRenderer().gpuBytes()) / (1 << 20), camera_.position.x,
                       camera_.position.y, camera_.position.z);
}

} // namespace mc
