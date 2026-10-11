#include "render/ClientAssets.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "gfx/GL.h"

namespace mc {

bool ClientAssets::load(const std::filesystem::path& packRoot) {
    Stopwatch sw;
    pack_ = std::make_unique<ResourcePack>(packRoot);

    AtlasStats stats;
    // Block textures plus the entity textures block entities draw with.
    blockEntityModels_ = std::make_unique<BlockEntityModels>();
    static constexpr std::string_view kAtlases[] = {"blocks", "chests", "beds", "signs", "shulker_boxes", "decorated_pot"};
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

BiomeColors ClientAssets::defaultBiomeColors() const {
    BiomeColors c;
    c.grass = grass_.sample(0.8, 0.4, c.grass);
    c.foliage = foliage_.sample(0.8, 0.4, c.foliage);
    c.dryFoliage = dryFoliage_.sample(0.8, 0.4, c.dryFoliage);
    return c;
}

} // namespace mc
