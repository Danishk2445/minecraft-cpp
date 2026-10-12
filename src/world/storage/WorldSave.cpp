#include "world/storage/WorldSave.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <format>
#include <fstream>

#include "entity/Player.h"
#include "game/Item.h"
#include "resources/FontSet.h"

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
    w.gameMode = j->value("gameMode", std::string("survival"));
    w.difficulty = j->value("difficulty", std::string("normal"));
    w.cheats = j->value("cheats", false);
    w.hardcore = j->value("hardcore", false);
    w.lastPlayed = j->value("lastPlayed", int64_t(0));
    w.level = j->value("level", Json::object());
    return w;
}

std::vector<WorldSummary> listWorlds(const std::filesystem::path& savesDir) {
    std::vector<WorldSummary> out;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(savesDir, ec); !ec && it != std::filesystem::end(it); it.increment(ec)) {
        if (!it->is_directory()) continue;
        if (auto info = readWorldInfo(it->path())) {
            WorldSummary w;
            w.folder = it->path().filename().string();
            w.info = std::move(*info);
            w.hasIcon = std::filesystem::exists(it->path() / "icon.png");
            out.push_back(std::move(w));
        }
    }
    std::sort(out.begin(), out.end(), [](const WorldSummary& a, const WorldSummary& b) {
        return a.info.lastPlayed != b.info.lastPlayed ? a.info.lastPlayed > b.info.lastPlayed : a.folder < b.folder;
    });
    return out;
}

std::string uniqueWorldFolder(const std::filesystem::path& savesDir, std::string_view name) {
    std::string base;
    for (char c : name) {
        const bool illegal = static_cast<unsigned char>(c) < 32 || std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos;
        base += illegal ? '_' : c;
    }
    while (!base.empty() && (base.back() == '.' || base.back() == ' ')) base.pop_back();
    if (base.empty()) base = "World";
    // Names Windows reserves get a leading underscore.
    static constexpr std::string_view kReserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                                                     "LPT1", "LPT2", "LPT3"};
    std::string upper = base;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    for (std::string_view r : kReserved)
        if (upper == r) base = "_" + base;
    std::string candidate = base;
    for (int n = 1; std::filesystem::exists(savesDir / candidate); ++n) candidate = std::format("{} ({})", base, n);
    return candidate;
}

int64_t seedFromString(std::string_view text, uint64_t randomSeed) {
    size_t start = 0, end = text.size();
    while (start < end && text[start] == ' ') ++start;
    while (end > start && text[end - 1] == ' ') --end;
    text = text.substr(start, end - start);
    if (text.empty()) return int64_t(randomSeed);
    int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec == std::errc() && ptr == text.data() + text.size()) return value;
    // Java String.hashCode over UTF-16 code units: s[0]*31^(n-1) + ... + s[n-1].
    int32_t h = 0;
    for (char32_t c : decodeUtf8(text)) {
        if (c >= 0x10000) {
            const char32_t v = c - 0x10000;
            h = int32_t(uint32_t(h) * 31u + uint32_t(0xD800 + (v >> 10)));
            h = int32_t(uint32_t(h) * 31u + uint32_t(0xDC00 + (v & 0x3FF)));
        } else {
            h = int32_t(uint32_t(h) * 31u + uint32_t(c));
        }
    }
    return int64_t(h);
}

bool deleteWorld(const std::filesystem::path& savesDir, const std::string& folder, std::string* error) {
    if (folder.empty() || folder == "." || folder == ".." || folder.find('/') != std::string::npos) {
        if (error) *error = "bad world folder";
        return false;
    }
    std::error_code ec;
    std::filesystem::remove_all(savesDir / folder, ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

bool writeWorldInfo(const std::filesystem::path& worldDir, const WorldInfo& info, std::string* error) {
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const Json j = {{"version", 1},           {"name", info.name},     {"seed", info.seed},
                    {"type", info.type},      {"gameMode", info.gameMode}, {"difficulty", info.difficulty},
                    {"cheats", info.cheats},  {"hardcore", info.hardcore}, {"lastPlayed", now},
                    {"level", info.level}};
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
