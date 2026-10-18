// Mobs.cpp -- the mob factory: each type in data/entities.json to its class.
#include "entity/mob/Animals.h"
#include "entity/mob/Monsters.h"
#include "resources/ResourcePack.h"

namespace mc {

std::unique_ptr<Mob> createMob(const EntityTypes& types, std::string_view id) {
    const EntityTypeInfo* type = types.find(id);
    if (!type) return nullptr;
    const std::string_view name = shortId(type->id);
    if (name == "pig") return std::make_unique<Pig>(*type);
    if (name == "cow") return std::make_unique<Cow>(*type);
    if (name == "sheep") return std::make_unique<Sheep>(*type);
    if (name == "chicken") return std::make_unique<Chicken>(*type);
    if (name == "zombie") return std::make_unique<Zombie>(*type);
    if (name == "skeleton") return std::make_unique<Skeleton>(*type);
    if (name == "creeper") return std::make_unique<Creeper>(*type);
    if (name == "spider") return std::make_unique<Spider>(*type);
    return nullptr;
}

} // namespace mc
