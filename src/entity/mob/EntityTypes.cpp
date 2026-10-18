#include "entity/mob/EntityTypes.h"

#include "core/Json.h"
#include "game/DyeColor.h"
#include "game/Item.h"
#include "world/biome/Biome.h"

namespace mc {

int mobCap(MobCategory c) {
    switch (c) {
    case MobCategory::Monster: return 70;
    case MobCategory::Creature: return 10;
    case MobCategory::Misc: return 0;
    }
    return 0;
}

bool EntityTypes::load(const std::filesystem::path& file, const ItemRegistry& items, const BiomeRegistry& biomes,
                       std::string* error) {
    items_ = &items;
    const std::optional<Json> data = loadJsonFile(file, error);
    if (!data) return false;
    std::vector<std::string> problems;
    auto item = [&](const std::string& id) {
        const Item* it = items.find(id);
        if (!it) problems.push_back("unknown item " + id);
        return it;
    };
    // (Each section is bound first: a range-for over a temporary's items() would dangle.)
    const Json typesJson = data->value("types", Json::object()), setsJson = data->value("spawn_sets", Json::object()),
               biomesJson = data->value("biome_spawns", Json::object()), lootJson = data->value("loot", Json::object());
    for (const auto& [id, j] : typesJson.items()) {
        EntityTypeInfo t;
        t.id = id;
        const std::string category = j.value("category", std::string("misc"));
        t.category = category == "monster" ? MobCategory::Monster : category == "creature" ? MobCategory::Creature : MobCategory::Misc;
        if (const Json size = j.value("size", Json::array()); size.is_array() && size.size() == 2) {
            t.width = size[0].get<float>();
            t.height = size[1].get<float>();
        }
        t.eyeHeight = j.value("eye_height", t.height * 0.85f);
        t.maxHealth = j.value("max_health", t.maxHealth);
        t.movementSpeed = j.value("movement_speed", t.movementSpeed);
        t.followRange = j.value("follow_range", t.followRange);
        t.attackDamage = j.value("attack_damage", t.attackDamage);
        t.armor = j.value("armor", t.armor);
        t.undead = j.value("undead", false);
        t.food = j.value("food", std::string());
        types_.push_back(std::move(t));
    }
    // Spawn sets by name, then each biome's.
    std::unordered_map<std::string, std::vector<SpawnerData>> sets;
    for (const auto& [name, list] : setsJson.items()) {
        if (!list.is_array()) continue;
        auto& out = sets[name];
        for (const Json& e : list) {
            if (!e.is_array() || e.size() != 4) continue;
            const EntityTypeInfo* t = find(e[0].get<std::string>());
            if (!t) {
                problems.push_back("unknown entity " + e[0].get<std::string>() + " in spawn set " + name);
                continue;
            }
            out.push_back({t, e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
        }
    }
    biomes_.assign(biomes.size(), {sets["none"], sets["common"], 0.1f});
    for (const auto& [name, j] : biomesJson.items()) {
        if (name.starts_with("_")) continue;
        const auto biome = biomes.find(name);
        if (!biome) {
            problems.push_back("unknown biome " + name);
            continue;
        }
        BiomeSpawns& b = biomes_[*biome];
        auto set = [&](const char* key, std::vector<SpawnerData>& out) {
            if (!j.contains(key)) return;
            const std::string s = j[key].get<std::string>();
            if (!sets.contains(s)) problems.push_back("unknown spawn set " + s);
            else out = sets[s];
        };
        set("creature", b.creature);
        set("monster", b.monster);
        b.creatureProbability = j.value("creature_probability", b.creatureProbability);
    }
    // Loot.
    for (const auto& [id, entries] : lootJson.items()) {
        if (id.starts_with("_") || !entries.is_array()) continue;
        auto& out = loot_[id];
        for (const Json& e : entries) {
            LootEntry l;
            if (e.contains("item")) {
                l.items.push_back(item(e["item"].get<std::string>()));
            } else if (e.contains("one_of")) {
                for (const Json& i : e["one_of"]) l.items.push_back(item(i.get<std::string>()));
            } else if (e.contains("tag")) {
                const std::string tag = e["tag"].get<std::string>();
                for (size_t i = 0; i < items.size(); ++i)
                    if (items.inTag(items.get(i), tag)) l.items.push_back(&items.get(i));
                if (l.items.empty()) problems.push_back("empty item tag " + tag);
            }
            l.wool = e.value("wool", false);
            if (const Json c = e.value("count", Json::array()); c.is_array() && c.size() == 2) {
                l.min = c[0].get<int>();
                l.max = c[1].get<int>();
            }
            if (e.contains("cooked")) {
                const Json& c = e["cooked"];
                if (c.is_string() && !l.items.empty()) l.cooked[l.items[0]] = item(c.get<std::string>());
                else if (c.is_object())
                    for (const auto& [raw, cooked] : c.items()) l.cooked[item(raw)] = item(cooked.get<std::string>());
            }
            l.chance = e.value("chance", 1.0f);
            l.playerKill = e.value("player_kill", false);
            l.killer = e.value("killer", std::string());
            out.push_back(std::move(l));
        }
    }
    for (std::string_view color : kDyeColors) wool_.push_back(item(std::string(color) + "_wool"));
    if (!problems.empty()) {
        if (error) *error = file.string() + ": " + problems.front();
        return false;
    }
    return true;
}

const EntityTypeInfo* EntityTypes::find(std::string_view id) const {
    const std::string full = id.starts_with("minecraft:") ? std::string(id) : "minecraft:" + std::string(id);
    for (const EntityTypeInfo& t : types_)
        if (t.id == full) return &t;
    return nullptr;
}

const std::vector<SpawnerData>& EntityTypes::spawns(BiomeId biome, MobCategory category) const {
    static const std::vector<SpawnerData> kNone;
    if (biome >= biomes_.size()) return kNone;
    if (category == MobCategory::Monster) return biomes_[biome].monster;
    if (category == MobCategory::Creature) return biomes_[biome].creature;
    return kNone;
}

float EntityTypes::creatureProbability(BiomeId biome) const {
    return biome < biomes_.size() ? biomes_[biome].creatureProbability : 0.1f;
}

std::vector<ItemStack> EntityTypes::loot(const EntityTypeInfo& type, const DeathContext& death, Xoroshiro& random) const {
    std::vector<ItemStack> out;
    const auto it = loot_.find(type.id);
    if (it == loot_.end()) return out;
    for (const LootEntry& e : it->second) {
        if (e.playerKill && !death.killedByPlayer) continue;
        if (!e.killer.empty() && e.killer != death.killerType) continue;
        if (e.chance < 1.0f && random.nextFloat() >= e.chance) continue;
        const Item* item = nullptr;
        int count = 1;
        if (e.wool) {
            if (death.woolColor < 0 || size_t(death.woolColor) >= wool_.size()) continue;
            item = wool_[size_t(death.woolColor)];
        } else {
            if (e.items.empty()) continue;
            item = e.items[size_t(random.nextInt(int(e.items.size())))];
            count = e.min + random.nextInt(e.max - e.min + 1); // UniformGenerator.getInt
        }
        if (!item || count <= 0) continue;
        if (death.onFire)
            if (auto c = e.cooked.find(item); c != e.cooked.end()) item = c->second;
        out.push_back({item, count});
    }
    return out;
}

} // namespace mc
