// BlockEntities.h -- data that lives with a block (vanilla BlockEntity): container
// slots (chests, barrels), furnaces (smelting, smoking, blasting), and the viewers that
// keep a chest or barrel open. The level owns them by position; menus share them.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/Json.h"
#include "core/Math.h"
#include "game/Inventory.h"
#include "game/Recipes.h"

namespace mc {

class Level;

class BlockEntity {
public:
    explicit BlockEntity(const BlockPos& p) : pos(p) {}
    virtual ~BlockEntity() = default;
    BlockPos pos;
    // Set when the level lets go of it (its block changed or its chunk unloaded); screens
    // still holding it close.
    bool removed = false;
    virtual std::string_view type() const = 0;  // "minecraft:chest"
    virtual bool ticks() const { return false; } // furnaces
    virtual void tick(Level& level) { (void)level; }
    virtual void save(Json& out) const = 0;
    virtual void load(const Json& in, const ItemRegistry& items) = 0;
    // What spills out when the block is broken (vanilla Containers.dropContents).
    virtual std::vector<ItemStack> contents() const { return {}; }
};

// Chests, trapped chests and barrels: 27 slots. Players looking inside keep it open
// (vanilla ContainerOpenersCounter): chest lids rise, barrels show their open top. Ender
// chests use only the lid (their slots belong to the player).
class ContainerBlockEntity : public BlockEntity {
public:
    ContainerBlockEntity(const BlockPos& p, std::string_view type) : BlockEntity(p), type_(type) {}
    std::string_view type() const override { return type_; }
    // Barrels show their open top while someone looks inside; chest lids swing (vanilla
    // ChestLidController: 0.1 of the way per tick).
    bool ticks() const override { return true; }
    void tick(Level& level) override;
    bool isChest() const { return type_ != "minecraft:barrel"; }
    float openness(float partialTick) const { return oldOpenness + (openness_ - oldOpenness) * partialTick; }
    bool lidMoving() const { return openness_ > 0.0f || oldOpenness > 0.0f; }
    void save(Json& out) const override;
    void load(const Json& in, const ItemRegistry& items) override;
    std::vector<ItemStack> contents() const override;

    std::array<ItemStack, 27> items{};
    int viewers = 0;
    float oldOpenness = 0.0f;

private:
    std::string type_;
    float openness_ = 0.0f;
};

// Furnaces, smokers and blast furnaces (vanilla AbstractFurnaceBlockEntity): 0 input, 1
// fuel, 2 output. Fuel burns while there is something it can cook; cooking takes the
// recipe's time and stops (and slowly unwinds) without fire. Experience gathers until the
// output is taken or the furnace is broken.
class FurnaceBlockEntity : public BlockEntity {
public:
    static constexpr int kInput = 0, kFuel = 1, kOutput = 2;

    FurnaceBlockEntity(const BlockPos& p, RecipeType kind, const RecipeBook* recipes);
    std::string_view type() const override;
    bool ticks() const override { return true; }
    void tick(Level& level) override;
    void save(Json& out) const override;
    void load(const Json& in, const ItemRegistry& items) override;
    std::vector<ItemStack> contents() const override;

    // The appliance's recipe for its input, if any.
    const Recipe* recipeFor(const ItemStack& input) const;
    // Fuel slots take fuel and buckets (for the lava bucket's return).
    static bool isFuel(const ItemStack& s) { return !s.empty() && s.item->fuelTicks > 0; }
    bool lit() const { return litTime > 0; }
    // Burn and cook progress, 0..1, for the screen's flame and arrow.
    float litProgress() const;
    float cookProgress() const;
    // The gathered experience as whole points: the fraction rounds up by chance (vanilla
    // createExperience); resets the store.
    int takeExperience(float random01);

    RecipeType kind;
    std::array<ItemStack, 3> items{};
    int litTime = 0, litDuration = 0;
    int cookingProgress = 0, cookingTotalTime = 200;
    float storedExperience = 0.0f;

private:
    bool canBurn(const Recipe* recipe) const;
    const RecipeBook* recipes_;
};

// Signs and hanging signs (vanilla SignBlockEntity): four lines on each side, each side's
// dye color and glow; waxed signs can't be edited.
class SignBlockEntity : public BlockEntity {
public:
    struct Side {
        std::array<std::string, 4> lines;
        std::string color = "black"; // a dye
        bool glowing = false;
        bool empty() const {
            for (const auto& l : lines)
                if (!l.empty()) return false;
            return true;
        }
    };
    SignBlockEntity(const BlockPos& p, bool hanging) : BlockEntity(p), hanging(hanging) {}
    std::string_view type() const override { return hanging ? "minecraft:hanging_sign" : "minecraft:sign"; }
    void save(Json& out) const override;
    void load(const Json& in, const ItemRegistry& items) override;
    // Vanilla getMaxTextLineWidth and getTextLineHeight.
    int maxLineWidth() const { return hanging ? 60 : 90; }
    int lineHeight() const { return hanging ? 9 : 10; }

    bool hanging;
    Side front, back;
    bool waxed = false;
};

// A dye's text color on signs (vanilla DyeColor.getTextColor), RGB.
uint32_t dyeTextColor(std::string_view dye);

// The block entity a block carries (null for plain blocks): "chest", "barrel", "furnace", ...
std::shared_ptr<BlockEntity> createBlockEntity(std::string_view blockId, const BlockPos& pos, const RecipeBook* recipes);

} // namespace mc
