#include "render/ClientAssets.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "gfx/GL.h"
#include "resources/TextureMeta.h"

namespace mc {

bool ClientAssets::load(const std::filesystem::path& packRoot) {
    Stopwatch sw;
    pack_ = std::make_unique<ResourcePack>(packRoot);

    AtlasStats stats;
    // Block textures plus the entity textures block entities draw with.
    blockEntityModels_ = std::make_unique<BlockEntityModels>();
    static constexpr std::string_view kAtlases[] = {"blocks", "chests", "beds", "signs", "shulker_boxes", "decorated_pot",
                                                    "particles"};
    auto atlas = buildAtlas(*pack_, kAtlases, blockEntityModels_->textures(), kBlockMipLevels, &stats);
    for (const auto& e : stats.errors) log::warn("atlas: {}", e);
    if (!atlas) {
        log::error("Could not build the block atlas");
        return false;
    }
    blockAtlas_ = std::move(*atlas);
    log::info("Block atlas: {}x{}, {} sprites ({} animated), {} mip levels", blockAtlas_.width, blockAtlas_.height,
              blockAtlas_.sprites.size(), blockAtlas_.animations.size(), blockAtlas_.mipLevels);

    blockAtlasTex_.create(blockAtlas_.width, blockAtlas_.height, blockAtlas_.mipLevels + 1);
    for (int l = 0; l <= blockAtlas_.mipLevels; ++l) blockAtlasTex_.upload(l, 0, 0, blockAtlas_.levels[size_t(l)]);
    glObjectLabel(GL_TEXTURE, blockAtlasTex_.id(), -1, "block atlas");
    animator_ = std::make_unique<AtlasAnimator>(blockAtlas_);

    AtlasStats guiStats;
    auto gui = buildAtlas(*pack_, "gui", 0, &guiStats);
    for (const auto& e : guiStats.errors) log::warn("gui atlas: {}", e);
    if (!gui) {
        log::error("Could not build the GUI sprite atlas");
        return false;
    }
    guiAtlas_ = std::move(*gui);
    guiAtlasTex_.create(guiAtlas_.width, guiAtlas_.height, 1);
    guiAtlasTex_.upload(0, 0, 0, guiAtlas_.levels[0]);
    glObjectLabel(GL_TEXTURE, guiAtlasTex_.id(), -1, "gui atlas");
    log::info("GUI atlas: {}x{}, {} sprites", guiAtlas_.width, guiAtlas_.height, guiAtlas_.sprites.size());

    models_ = std::make_unique<ModelStore>(*pack_);
    baker_ = std::make_unique<ModelBaker>(*models_, blockAtlas_);
    blockModels_.load(*pack_, *models_, *baker_);
    for (const auto& e : blockModels_.errors()) log::warn("blockstates: {}", e);
    for (const auto& e : models_->errors()) log::warn("models: {}", e);
    size_t shown = 0;
    for (const auto& e : baker_->errors())
        if (shown++ < 20) log::warn("baking: {}", e);
    log::info("Baked {} block models for {} blockstate files ({} model errors, {} missing textures)",
              baker_->bakedCount(), blockModels_.definitions().size(), baker_->errors().size(),
              baker_->missingTextureCount());

    std::string itemProblems;
    itemDefinitions_.load(*pack_, *baker_, &itemProblems);
    for (size_t start = 0; start < itemProblems.size();) {
        const size_t end = itemProblems.find('\n', start);
        log::warn("items: {}", itemProblems.substr(start, end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    log::info("Item definitions: {}", itemDefinitions_.size());

    auto loadMap = [&](const char* name) {
        auto img = Image::loadPng(pack_->file("minecraft", std::string("textures/colormap/") + name + ".png"));
        if (!img) log::warn("missing colormap {}", name);
        return img ? Colormap(std::move(*img)) : Colormap();
    };
    grass_ = loadMap("grass");
    foliage_ = loadMap("foliage");
    dryFoliage_ = loadMap("dry_foliage");
    log::info("Assets loaded in {:.0f} ms", sw.millis());
    return true;
}

void ClientAssets::tick() {
    if (!animator_) return;
    animator_->tick([&](const SpriteInfo& s, int level, const Image& texels) {
        blockAtlasTex_.upload(level, s.x >> level, s.y >> level, texels);
    });
}

const Texture& ClientAssets::texture(std::string_view id) const {
    auto it = textures_.find(std::string(id));
    if (it == textures_.end()) {
        std::unique_ptr<Texture> tex;
        std::string err;
        const auto path = pack_->resource(ResourceLocation::parse(id), "textures", ".png");
        if (auto img = Image::loadPng(path, &err)) {
            tex = std::make_unique<Texture>();
            tex->create(img->width, img->height, 1);
            tex->upload(0, 0, 0, *img);
            // The mcmeta's "blur" asks for linear filtering (the vignette, the pumpkin blur).
            auto metaPath = path;
            metaPath += ".mcmeta";
            if (std::filesystem::exists(metaPath))
                if (auto j = pack_->json(metaPath, &err); j && parseTextureMeta(*j).blur)
                    tex->setFilter(GL_LINEAR, GL_LINEAR);
        } else {
            log::warn("texture {}: {}", id, err);
        }
        it = textures_.emplace(std::string(id), std::move(tex)).first;
    }
    if (it->second) return *it->second;
    if (!missingTex_.valid()) {
        const Image checker = missingTextureImage();
        missingTex_.create(checker.width, checker.height, 1);
        missingTex_.upload(0, 0, 0, checker);
    }
    return missingTex_;
}

BiomeColors ClientAssets::defaultBiomeColors() const {
    BiomeColors c;
    c.grass = grass_.sample(0.8, 0.4, c.grass);
    c.foliage = foliage_.sample(0.8, 0.4, c.foliage);
    c.dryFoliage = dryFoliage_.sample(0.8, 0.4, c.dryFoliage);
    return c;
}

} // namespace mc
