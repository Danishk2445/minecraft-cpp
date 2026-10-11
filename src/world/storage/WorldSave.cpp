#include "world/storage/WorldSave.h"

#include <format>
#include <fstream>

#include "entity/Player.h"
#include "game/Item.h"

namespace mc {

bool writeFileAtomic(const std::filesystem::path& path, const std::string& text, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << text;
        if (!f) {
            if (error) *error = "cannot write " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec && error) *error = std::format("cannot replace {}: {}", path.string(), ec.message());
    return !ec;
}

std::optional<WorldInfo> readWorldInfo(const std::filesystem::path& worldDir, std::string* error) {
    auto j = loadJsonFile(worldDir / "level.json", error);
    if (!j || !j->is_object()) return std::nullopt;
    WorldInfo w;
    w.name = j->value("name", worldDir.filename().string());
    w.seed = j->value("seed", int64_t(0));
    w.type = j->value("type", std::string("default"));
    w.level = j->value("level", Json::object());
    return w;
}

bool writeWorldInfo(const std::filesystem::path& worldDir, const WorldInfo& info, std::string* error) {
    const Json j = {{"version", 1}, {"name", info.name}, {"seed", info.seed}, {"type", info.type}, {"level", info.level}};
    return writeFileAtomic(worldDir / "level.json", j.dump(2) + "\n", error);
}

Json savePlayer(const Player& p) {
    Json inv = Json::array();
    for (size_t i = 0; i < p.inventory.main.size(); ++i) {
        const ItemStack& s = p.inventory.main[i];
        if (!s.empty()) inv.push_back({{"slot", i}, {"id", s.item->id}, {"count", s.count}});
    }
    return {{"pos", {p.pos.x, p.pos.y, p.pos.z}},
            {"rotation", {p.yaw, p.pitch}},
            {"mode", gameModeName(p.mode())},
            {"flying", p.flying},
            {"selected", p.inventory.selected},
            {"inventory", std::move(inv)}};
}

void loadPlayer(Player& p, const Json& j, const ItemRegistry& items) {
    if (auto pos = j.find("pos"); pos != j.end() && pos->is_array() && pos->size() == 3)
        p.pos = p.oldPos = {(*pos)[0].get<double>(), (*pos)[1].get<double>(), (*pos)[2].get<double>()};
    if (auto rot = j.find("rotation"); rot != j.end() && rot->is_array() && rot->size() == 2) {
        p.setRotation((*rot)[0].get<float>(), (*rot)[1].get<float>());
    }
    if (auto m = gameModeFromName(j.value("mode", std::string("survival")))) p.setMode(*m);
    p.flying = j.value("flying", false) && p.mode() != GameMode::Survival && p.mode() != GameMode::Adventure;
    p.inventory = Inventory{};
    p.inventory.selected = std::clamp(j.value("selected", 0), 0, Inventory::kHotbarSize - 1);
    for (const Json& s : j.value("inventory", Json::array())) {
        const int slot = s.value("slot", -1);
        const Item* item = items.find(s.value("id", std::string()));
        if (slot < 0 || slot >= Inventory::kMainSize || !item) continue;
        p.inventory.main[size_t(slot)] = {item, std::clamp(s.value("count", 1), 1, item->maxStack)};
    }
}

std::optional<Json> readPlayerFile(const std::filesystem::path& worldDir) {
    auto j = loadJsonFile(worldDir / "playerdata" / "player.json");
    if (!j || !j->is_object()) return std::nullopt;
    return j;
}

bool writePlayerFile(const std::filesystem::path& worldDir, const Json& j, std::string* error) {
    return writeFileAtomic(worldDir / "playerdata" / "player.json", j.dump(2) + "\n", error);
}

} // namespace mc
