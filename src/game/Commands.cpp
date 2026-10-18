#include "game/Commands.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>

#include "entity/LightningBolt.h"
#include "entity/PrimedTnt.h"
#include "entity/mob/Animals.h"
#include "entity/mob/Monsters.h"
#include "game/DyeColor.h"
#include "game/BlockEntities.h"
#include "world/block/BlockRegistry.h"
#include "world/level/Level.h"

namespace mc {
namespace {

bool parseDouble(std::string_view s, double& out) {
    if (s.empty()) return false;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

bool parseLong(std::string_view s, long long& out) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

std::optional<BlockPos> parseBlockPos(Commands::Args a, size_t i, const glm::dvec3& base) {
    if (a.size() < i + 3) return std::nullopt;
    const auto x = parseCoordinate(a[i], std::floor(base.x));
    const auto y = parseCoordinate(a[i + 1], std::floor(base.y));
    const auto z = parseCoordinate(a[i + 2], std::floor(base.z));
    if (!x || !y || !z) return std::nullopt;
    return BlockPos(int(std::floor(*x)), int(std::floor(*y)), int(std::floor(*z)));
}

} // namespace

std::optional<double> parseCoordinate(std::string_view token, double base) {
    double v = 0.0;
    if (token.starts_with('~')) {
        token.remove_prefix(1);
        if (token.empty()) return base;
        if (!parseDouble(token, v)) return std::nullopt;
        return base + v;
    }
    if (!parseDouble(token, v)) return std::nullopt;
    return v;
}

std::vector<std::string> tokenizeCommand(std::string_view line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        size_t j = i;
        int depth = 0; // spaces inside [...], {...} and quotes stay in the token
        char quote = 0;
        while (j < line.size() && (line[j] != ' ' || depth > 0 || quote)) {
            const char c = line[j];
            if (quote) {
                if (c == '\\' && j + 1 < line.size()) ++j;
                else if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '[' || c == '{') {
                ++depth;
            } else if (c == ']' || c == '}') {
                --depth;
            }
            ++j;
        }
        if (j > i) out.emplace_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

namespace {

struct SnbtParser {
    std::string_view s;
    size_t i = 0;

    void skip() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) ++i;
    }
    bool eat(char c) {
        skip();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    static bool bare(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '.' || c == '+';
    }
    std::optional<std::string> string() {
        skip();
        if (i >= s.size()) return std::nullopt;
        if (s[i] == '"' || s[i] == '\'') {
            const char q = s[i++];
            std::string out;
            while (i < s.size() && s[i] != q) {
                if (s[i] == '\\' && i + 1 < s.size()) ++i;
                out += s[i++];
            }
            if (i >= s.size()) return std::nullopt;
            ++i;
            return out;
        }
        const size_t start = i;
        while (i < s.size() && bare(s[i])) ++i;
        if (i == start) return std::nullopt;
        return std::string(s.substr(start, i - start));
    }
    std::optional<Json> value() {
        skip();
        if (i >= s.size()) return std::nullopt;
        if (s[i] == '{') {
            ++i;
            Json obj = Json::object();
            if (eat('}')) return obj;
            do {
                auto key = string();
                if (!key || !eat(':')) return std::nullopt;
                auto v = value();
                if (!v) return std::nullopt;
                obj[*key] = std::move(*v);
            } while (eat(','));
            if (!eat('}')) return std::nullopt;
            return obj;
        }
        if (s[i] == '[') {
            ++i;
            Json arr = Json::array();
            // Typed arrays ([I; 1, 2]) read like lists.
            if (i + 1 < s.size() && (s[i] == 'I' || s[i] == 'B' || s[i] == 'L') && s[i + 1] == ';') i += 2;
            if (eat(']')) return arr;
            do {
                auto v = value();
                if (!v) return std::nullopt;
                arr.push_back(std::move(*v));
            } while (eat(','));
            if (!eat(']')) return std::nullopt;
            return arr;
        }
        const bool quoted = s[i] == '"' || s[i] == '\'';
        auto text = string();
        if (!text) return std::nullopt;
        if (quoted) return Json(*text);
        if (*text == "true") return Json(true);
        if (*text == "false") return Json(false);
        // A number, maybe with a type suffix.
        std::string_view n = *text;
        if (!n.empty() && std::string_view("bBsSlLfFdD").find(n.back()) != std::string_view::npos &&
            n.size() > 1 && (std::isdigit(static_cast<unsigned char>(n[n.size() - 2])) || n[n.size() - 2] == '.'))
            n.remove_suffix(1);
        double d = 0.0;
        if (parseDouble(n, d)) {
            if (n.find('.') == std::string_view::npos && n.find('e') == std::string_view::npos) return Json(int64_t(d));
            return Json(d);
        }
        return Json(*text);
    }
};

// /summon of a mob: vanilla finalizes it (equipment, babies, colors) only without data;
// the data's keys (vanilla NBT names) set what they name.
bool summonMob(Level& level, const std::string& type, const glm::dvec3& at, const Json& data, std::string& err) {
    std::unique_ptr<Mob> mob = createMob(*level.entityTypes(), type);
    if (!mob) {
        err = "can't summon " + type;
        return false;
    }
    mob->random = Xoroshiro(int64_t(level.random().nextLong()));
    mob->pos = mob->oldPos = at;
    float yaw = 0.0f, pitch = 0.0f;
    if (const Json r = data.value("Rotation", Json::array()); r.is_array() && r.size() == 2) {
        yaw = r[0].get<float>();
        pitch = r[1].get<float>();
    }
    mob->setRotation(yaw, pitch);
    if (data.empty()) {
        SpawnGroup group;
        mob->finalizeSpawn(level, SpawnReason::Command, group);
    }
    auto flag = [&](const char* key) {
        const Json v = data.value(key, Json());
        return v.is_boolean() ? v.get<bool>() : v.is_number() && v.get<double>() != 0.0;
    };
    mob->noAi = flag("NoAI");
    mob->persistenceRequired = flag("PersistenceRequired") || mob->noAi;
    if (data.contains("Health")) mob->health = std::clamp(data["Health"].get<float>(), 0.0f, mob->maxHealth);
    if (auto* animal = dynamic_cast<Animal*>(mob.get()); animal && data.contains("Age")) animal->setAge(data["Age"].get<int>());
    if (auto* zombie = dynamic_cast<Zombie*>(mob.get()); zombie && data.contains("IsBaby")) zombie->setBaby(flag("IsBaby"));
    if (auto* sheep = dynamic_cast<Sheep*>(mob.get())) {
        if (const Json c = data.value("Color", Json()); c.is_number()) sheep->color = std::clamp(c.get<int>(), 0, 15);
        else if (c.is_string()) sheep->color = dyeColorIndex(c.get<std::string>()).value_or(0);
        if (data.contains("Sheared")) sheep->sheared = flag("Sheared");
    }
    if (auto* creeper = dynamic_cast<Creeper*>(mob.get())) {
        if (data.contains("powered")) creeper->powered = flag("powered");
        if (data.contains("ignited")) creeper->ignited = flag("ignited");
        if (data.contains("Fuse")) creeper->maxSwell = std::max(1, data["Fuse"].get<int>());
    }
    const std::string variant = data.value("variant", std::string());
    if (!variant.empty()) {
        const FarmVariant v = variant == "warm" || variant == "minecraft:warm" ? FarmVariant::Warm : FarmVariant::Temperate;
        if (auto* pig = dynamic_cast<Pig*>(mob.get())) pig->variant = v;
        if (auto* chicken = dynamic_cast<Chicken*>(mob.get())) chicken->variant = v;
    }
    // Vanilla 1.21.5 equipment:{mainhand:{id:...},head:{id:...},...}.
    if (const Json eq = data.value("equipment", Json::object()); eq.is_object() && level.itemRegistry()) {
        static constexpr std::pair<const char*, EquipmentSlot> kSlots[] = {
            {"mainhand", EquipmentSlot::MainHand}, {"offhand", EquipmentSlot::OffHand}, {"feet", EquipmentSlot::Feet},
            {"legs", EquipmentSlot::Legs},         {"chest", EquipmentSlot::Chest},     {"head", EquipmentSlot::Head}};
        for (const auto& [key, slot] : kSlots) {
            const Json item = eq.value(key, Json());
            const std::string id = item.is_object() ? item.value("id", std::string()) : std::string();
            if (id.empty()) continue;
            const Item* it = level.itemRegistry()->find(id);
            if (!it) {
                err = "unknown item " + id;
                return false;
            }
            mob->setItemSlot(slot, ItemStack{it, 1});
        }
        if (auto* skeleton = dynamic_cast<Skeleton*>(mob.get())) skeleton->reassessWeaponGoal(level);
    }
    level.addEntity(std::move(mob));
    return true;
}

} // namespace

std::optional<Json> parseSnbt(std::string_view text) {
    SnbtParser p{text};
    auto v = p.value();
    p.skip();
    if (!v || p.i != text.size()) return std::nullopt;
    return v;
}

Commands::Commands() {
    addBasics();
    const Handler teleport = [](CommandSource& src, Args a, std::string& err) {
        if (a.size() != 3 && a.size() != 5) {
            err = "expected 3 or 5 arguments";
            return false;
        }
        const auto x = parseCoordinate(a[0], src.position.x), y = parseCoordinate(a[1], src.position.y),
                   z = parseCoordinate(a[2], src.position.z);
        if (!x || !y || !z) {
            err = "bad coordinates";
            return false;
        }
        std::optional<glm::vec2> rot;
        if (a.size() == 5) {
            const auto yaw = parseCoordinate(a[3], src.rotation.x), pitch = parseCoordinate(a[4], src.rotation.y);
            if (!yaw || !pitch) {
                err = "bad rotation";
                return false;
            }
            rot = glm::vec2(float(*yaw), float(*pitch));
        }
        if (src.teleport) src.teleport({*x, *y, *z}, rot);
        if (src.feedback) src.feedback(std::format("Teleported {} to {:.2f}, {:.2f}, {:.2f}", src.name, *x, *y, *z));
        return true;
    };
    add("tp", "/tp <x> <y> <z> [yaw pitch]", teleport);
    add("teleport", "/teleport <x> <y> <z> [yaw pitch]", teleport);

    add("gamemode", "/gamemode <survival|creative|adventure|spectator>", [](CommandSource& src, Args a, std::string& err) {
        if (a.size() != 1) {
            err = "expected a game mode";
            return false;
        }
        if (!src.setGameMode || !src.setGameMode(a[0])) {
            err = "unknown game mode " + a[0];
            return false;
        }
        if (src.feedback) {
            std::string mode = a[0];
            if (!mode.empty()) mode[0] = char(std::toupper(static_cast<unsigned char>(mode[0])));
            src.feedback("Set own game mode to " + mode + " Mode");
        }
        return true;
    });
    add("assertblock", "/assertblock <x> <y> <z> <block>", [](CommandSource& src, Args a, std::string& err) {
        if (a.size() != 4 || !src.level) {
            err = "expected x y z block";
            return false;
        }
        const auto x = parseCoordinate(a[0], src.position.x), y = parseCoordinate(a[1], src.position.y),
                   z = parseCoordinate(a[2], src.position.z);
        if (!x || !y || !z) {
            err = "bad coordinates";
            return false;
        }
        const BlockPos p(int(std::floor(*x)), int(std::floor(*y)), int(std::floor(*z)));
        const BlockRegistry& reg = src.level->registry();
        const auto expected = reg.parse(a[3]);
        const StateId actual = src.level->getBlock(p);
        // A bare block id matches any of its states.
        const bool ok = expected && (a[3].find('[') == std::string::npos ? reg.info(actual).block == reg.info(*expected).block
                                                                       : actual == *expected);
        if (!ok) {
            const std::string msg = std::format("block at {} {} {} is {}, expected {}", p.x, p.y, p.z, reg.describe(actual), a[3]);
            if (src.assertionFailed) src.assertionFailed(msg);
            err = msg;
            return false;
        }
        return true;
    });
    add("give", "/give <item> [count]", [](CommandSource& src, Args a, std::string& err) {
        if (a.empty() || a.size() > 2) {
            err = "expected an item and an optional count";
            return false;
        }
        int count = 1;
        if (a.size() == 2 && (!std::from_chars(a[1].data(), a[1].data() + a[1].size(), count).ptr || count <= 0)) {
            err = "bad count " + a[1];
            return false;
        }
        if (!src.give || !src.give(a[0], count)) {
            err = "unknown item " + a[0];
            return false;
        }
        if (src.feedback) src.feedback("Gave " + std::to_string(count) + " " + a[0]);
        return true;
    });
    add("setblock", "/setblock <x> <y> <z> <block>[{nbt}]", [](CommandSource& src, Args a, std::string& err) {
        if (!src.level || a.size() != 4) {
            err = "expected 4 arguments";
            return false;
        }
        const auto pos = parseBlockPos(a, 0, src.position);
        if (!pos) {
            err = "bad coordinates";
            return false;
        }
        // The block, then its block entity's data in SNBT (vanilla BlockStateParser).
        const size_t brace = a[3].find('{');
        std::optional<Json> nbt;
        if (brace != std::string::npos) {
            nbt = parseSnbt(std::string_view(a[3]).substr(brace));
            if (!nbt || !nbt->is_object()) {
                err = "bad block entity data";
                return false;
            }
        }
        const auto state = src.level->registry().parse(a[3].substr(0, brace), &err);
        if (!state) return false;
        // Vanilla places with updates: the block's onPlace (fluids flow, sand falls) and its neighbors'.
        if (!src.level->setBlockAndUpdate(*pos, *state)) {
            err = "that position is not loaded";
            return false;
        }
        if (nbt && src.level->itemRegistry())
            if (auto be = src.level->blockEntity(*pos)) be->load(*nbt, *src.level->itemRegistry());
        if (src.feedback) src.feedback(std::format("Changed the block at {}, {}, {}", pos->x, pos->y, pos->z));
        return true;
    });

    add("fill", "/fill <x1> <y1> <z1> <x2> <y2> <z2> <block>", [](CommandSource& src, Args a, std::string& err) {
        if (!src.level || a.size() != 7) {
            err = "expected 7 arguments";
            return false;
        }
        const auto p1 = parseBlockPos(a, 0, src.position), p2 = parseBlockPos(a, 3, src.position);
        if (!p1 || !p2) {
            err = "bad coordinates";
            return false;
        }
        const auto state = src.level->registry().parse(a[6], &err);
        if (!state) return false;
        const BlockPos lo(std::min(p1->x, p2->x), std::min(p1->y, p2->y), std::min(p1->z, p2->z));
        const BlockPos hi(std::max(p1->x, p2->x), std::max(p1->y, p2->y), std::max(p1->z, p2->z));
        const long long volume = (long long)(hi.x - lo.x + 1) * (hi.y - lo.y + 1) * (hi.z - lo.z + 1);
        if (volume > 32768) {
            err = std::format("too many blocks in the specified area (maximum 32768, specified {})", volume);
            return false;
        }
        int changed = 0;
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z)
                for (int x = lo.x; x <= hi.x; ++x)
                    if (src.level->getBlock({x, y, z}) != *state && src.level->setBlockAndUpdate({x, y, z}, *state)) ++changed;
        if (src.feedback) src.feedback(std::format("Successfully filled {} block(s)", changed));
        return true;
    });

    add("time", "/time (set|add) <ticks|day|noon|night|midnight> | /time query daytime",
        [](CommandSource& src, Args a, std::string& err) {
            if (!src.level || a.empty()) {
                err = "expected arguments";
                return false;
            }
            if (a[0] == "query") {
                if (src.feedback) src.feedback(std::format("The time is {}", src.level->dayTime() % Level::kDayLength));
                return true;
            }
            if (a.size() != 2 || (a[0] != "set" && a[0] != "add")) {
                err = "usage: /time set|add <value>";
                return false;
            }
            long long t = 0;
            if (a[1] == "day") t = 1000;
            else if (a[1] == "noon") t = 6000;
            else if (a[1] == "night") t = 13000;
            else if (a[1] == "midnight") t = 18000;
            else if (!parseLong(a[1], t)) {
                err = "bad time value";
                return false;
            }
            src.level->setDayTime(a[0] == "set" ? t : src.level->dayTime() + t);
            if (src.feedback) src.feedback(std::format("Set the time to {}", src.level->dayTime()));
            return true;
        });
    // Vanilla WeatherCommand: a duration (ticks, or with t/s/d units), else the cycle's own.
    add("weather", "/weather (clear|rain|thunder) [duration]", [](CommandSource& src, Args a, std::string& err) {
        if (!src.level || a.empty() || a.size() > 2 || (a[0] != "clear" && a[0] != "rain" && a[0] != "thunder")) {
            err = "usage: /weather clear|rain|thunder [duration]";
            return false;
        }
        Level& level = *src.level;
        long long ticks = -1;
        if (a.size() == 2) {
            std::string_view v = a[1];
            long long unit = 1;
            if (v.ends_with('d')) unit = 24000;
            else if (v.ends_with('s')) unit = 20;
            if (v.ends_with('d') || v.ends_with('s') || v.ends_with('t')) v.remove_suffix(1);
            if (!parseLong(std::string(v), ticks) || ticks < 0) {
                err = "bad duration";
                return false;
            }
            ticks *= unit;
        }
        Xoroshiro& r = level.random();
        if (a[0] == "clear") {
            level.setWeather(ticks >= 0 ? int(ticks) : r.nextIntBetween(12000, 180000), 0, false, false);
            if (src.feedback) src.feedback("Set the weather to clear");
        } else if (a[0] == "rain") {
            level.setWeather(0, ticks >= 0 ? int(ticks) : r.nextIntBetween(12000, 24000), true, false);
            if (src.feedback) src.feedback("Set the weather to rain");
        } else {
            level.setWeather(0, ticks >= 0 ? int(ticks) : r.nextIntBetween(3600, 15600), true, true);
            if (src.feedback) src.feedback("Set the weather to rain & thunder");
        }
        return true;
    });
}

void Commands::addBasics() {
    add("help", "/help [command]", [this](CommandSource& src, Args a, std::string& err) {
        if (a.size() > 1) {
            err = "expected at most one command";
            return false;
        }
        if (a.size() == 1) {
            auto it = commands_.find(a[0]);
            if (it == commands_.end() || src.permission < it->second.permission) {
                err = "unknown command " + a[0];
                return false;
            }
            if (src.feedback) src.feedback(it->second.usage);
            return true;
        }
        if (src.feedback)
            for (const auto& [name, c] : commands_)
                if (src.permission >= c.permission) src.feedback(c.usage);
        return true;
    }, kAnyone);

    // Single player shows the seed to anyone (vanilla asks for operators on servers only).
    add("seed", "/seed", [](CommandSource& src, Args a, std::string& err) {
        if (!a.empty()) {
            err = "expected no arguments";
            return false;
        }
        if (src.feedback) src.feedback(std::format("Seed: [{}]", src.seed));
        return true;
    }, kAnyone);

    add("difficulty", "/difficulty [peaceful|easy|normal|hard]", [](CommandSource& src, Args a, std::string& err) {
        if (!src.getDifficulty || !src.setDifficulty || a.size() > 1) {
            err = "expected an optional difficulty";
            return false;
        }
        auto display = [](Difficulty d) {
            std::string n(difficultyName(d));
            n[0] = char(std::toupper(static_cast<unsigned char>(n[0])));
            return n;
        };
        const Difficulty now = src.getDifficulty();
        if (a.empty()) {
            if (src.feedback) src.feedback("The difficulty is " + display(now));
            return true;
        }
        const auto d = difficultyFromName(a[0]);
        if (!d) {
            err = "unknown difficulty " + a[0];
            return false;
        }
        if (*d == now) {
            err = "The difficulty did not change; it is already set to " + display(now);
            return false;
        }
        src.setDifficulty(*d);
        if (src.feedback) src.feedback("The difficulty has been set to " + display(*d));
        return true;
    });

    add("list", "/list", [](CommandSource& src, Args, std::string&) {
        if (src.feedback) src.feedback("There are 1 of a max of 8 players online: " + src.name);
        return true;
    }, kAnyone);

    auto joined = [](Args a) {
        std::string text;
        for (const std::string& w : a) text += (text.empty() ? "" : " ") + w;
        return text;
    };
    add("say", "/say <message>", [joined](CommandSource& src, Args a, std::string& err) {
        if (a.empty()) {
            err = "expected a message";
            return false;
        }
        if (src.feedback) src.feedback("[" + src.name + "] " + joined(a));
        return true;
    });
    add("me", "/me <action>", [joined](CommandSource& src, Args a, std::string& err) {
        if (a.empty()) {
            err = "expected an action";
            return false;
        }
        if (src.feedback) src.feedback("* " + src.name + " " + joined(a));
        return true;
    }, kAnyone);

    add("clear", "/clear [item] [maxCount]", [](CommandSource& src, Args a, std::string& err) {
        if (!src.clear || a.size() > 2) {
            err = "expected an optional item and count";
            return false;
        }
        long long max = -1;
        if (a.size() == 2 && (!parseLong(a[1], max) || max < 0)) {
            err = "bad count " + a[1];
            return false;
        }
        const int removed = src.clear(a.empty() ? nullptr : &a[0], max < 0 ? std::numeric_limits<int>::max() : int(max));
        if (removed < 0) {
            err = "unknown item " + a[0];
            return false;
        }
        if (removed == 0) {
            err = "No items were found on player " + src.name;
            return false;
        }
        if (src.feedback) src.feedback(std::format("Removed {} item(s) from player {}", removed, src.name));
        return true;
    });

    add("summon", "/summon <entity> [x y z] [nbt]", [](CommandSource& src, Args a, std::string& err) {
        if (a.empty() || (a.size() != 1 && a.size() != 4 && a.size() != 5)) {
            err = "expected an entity, then optionally a position and SNBT data";
            return false;
        }
        glm::dvec3 at = src.position;
        if (a.size() >= 4) {
            const auto x = parseCoordinate(a[1], src.position.x), y = parseCoordinate(a[2], src.position.y),
                       z = parseCoordinate(a[3], src.position.z);
            if (!x || !y || !z) {
                err = "bad coordinates";
                return false;
            }
            at = {*x, *y, *z};
        }
        Json data = Json::object();
        if (a.size() == 5) {
            auto parsed = parseSnbt(a[4]);
            if (!parsed || !parsed->is_object()) {
                err = "bad SNBT data " + a[4];
                return false;
            }
            data = std::move(*parsed);
        }
        std::string type = a[0];
        if (type.starts_with("minecraft:")) type.erase(0, 10);
        if (src.level && (type == "lightning_bolt" || type == "tnt")) { // the level's own entities
            Entity& e = type == "tnt" ? static_cast<Entity&>(PrimedTnt::prime(*src.level, BlockPos::containing(at)))
                                      : LightningBolt::strike(*src.level, BlockPos::containing(at), false);
            e.pos = e.oldPos = at;
            if (auto* tnt = dynamic_cast<PrimedTnt*>(&e)) tnt->fuse = data.value("fuse", PrimedTnt::kFuse);
            if (src.feedback) src.feedback("Summoned new " + type);
            return true;
        }
        if (src.level && src.level->entityTypes() && src.level->entityTypes()->find(type)) {
            if (!summonMob(*src.level, type, at, data, err)) return false;
            if (src.feedback) src.feedback("Summoned new " + type);
            return true;
        }
        if (!src.summon || !src.summon(type, at, data, err)) {
            if (err.empty()) err = "can't summon " + a[0];
            return false;
        }
        if (src.feedback) src.feedback("Summoned new " + type);
        return true;
    });

    // Vanilla /item replace block <pos> container.<slot> with <item> [count], and
    // /item replace entity @s <slot> with <item> [count] for the player's own slots.
    add("item", "/item replace (block <x> <y> <z> container.<slot> | entity @s <slot>) with <item> [count]",
        [](CommandSource& src, Args a, std::string& err) {
            if (a.size() >= 6 && a[0] == "replace" && a[1] == "entity") {
                if (a[2] != "@s" || a[4] != "with" || a.size() > 7) {
                    err = "expected replace entity @s <slot> with <item> [count]";
                    return false;
                }
                // Vanilla SlotArgument names: armor.*, weapon.*, hotbar.N, inventory.N.
                std::optional<int> slot;
                const std::string_view name = a[3];
                long long n = 0;
                if (name == "armor.feet") slot = 36;
                else if (name == "armor.legs") slot = 37;
                else if (name == "armor.chest") slot = 38;
                else if (name == "armor.head") slot = 39;
                else if (name == "weapon.offhand") slot = 40;
                else if (name == "weapon.mainhand" || name == "weapon") slot = -1; // the selected one
                else if (name.starts_with("hotbar.") && parseLong(name.substr(7), n) && n >= 0 && n < 9) slot = int(n);
                else if (name.starts_with("inventory.") && parseLong(name.substr(10), n) && n >= 0 && n < 27)
                    slot = 9 + int(n);
                long long count = 1;
                if (!slot || (a.size() == 7 && !parseLong(a[6], count))) {
                    err = "unknown slot or bad count";
                    return false;
                }
                if (!src.replacePlayerSlot || !src.replacePlayerSlot(*slot, a[5], int(count))) {
                    err = "unknown item " + a[5];
                    return false;
                }
                if (src.feedback) src.feedback(std::format("Replaced {} with {}", a[3], a[5]));
                return true;
            }
            if (a.size() < 8 || a.size() > 9 || a[0] != "replace" || a[1] != "block" || a[6] != "with" ||
                !a[5].starts_with("container.")) {
                err = "expected replace block <x> <y> <z> container.<slot> with <item> [count]";
                return false;
            }
            const auto pos = parseBlockPos(a, 2, src.position);
            long long slot = 0, count = 1;
            if (!pos || !parseLong(std::string_view(a[5]).substr(10), slot) || (a.size() == 9 && !parseLong(a[8], count))) {
                err = "bad position, slot or count";
                return false;
            }
            if (!src.replaceBlockSlot || !src.replaceBlockSlot(*pos, int(slot), a[7], int(count))) {
                err = "that block has no such slot, or the item is unknown";
                return false;
            }
            if (src.feedback) src.feedback(std::format("Replaced a slot at {}, {}, {} with {}", pos->x, pos->y, pos->z, a[7]));
            return true;
        });

    add("kill", "/kill [@s|@e[type=<entity>]]", [](CommandSource& src, Args a, std::string& err) {
        // @e (optionally of one type, or every type but one): every entity, mobs dying
        // properly (with their drops), others just removed.
        if (!a.empty() && a[0].starts_with("@e")) {
            std::string filter;
            bool negate = false;
            if (a[0].size() > 2) {
                const std::string_view sel = std::string_view(a[0]).substr(2);
                if (!sel.starts_with("[type=") || !sel.ends_with("]")) {
                    err = "expected @e or @e[type=<entity>]";
                    return false;
                }
                filter = std::string(sel.substr(6, sel.size() - 7));
                if (filter.starts_with("!")) {
                    negate = true;
                    filter.erase(0, 1);
                }
                if (!filter.starts_with("minecraft:")) filter = "minecraft:" + filter;
            }
            auto matches = [&](std::string_view type) { return filter.empty() || ((type == filter) != negate); };
            int killed = 0;
            if (src.level) {
                std::vector<Entity*> targets;
                for (const auto& e : src.level->entities())
                    if (!e->removed && matches(e->type())) targets.push_back(e.get());
                for (Entity* e : targets) {
                    if (auto* living = dynamic_cast<LivingEntity*>(e)) {
                        if (!living->alive()) continue;
                        living->hurtServer(*src.level, DamageType::GenericKill, 3.4e38f);
                    } else {
                        e->removed = true;
                    }
                    ++killed;
                }
            }
            if (matches("minecraft:player") && src.kill) {
                src.kill();
                ++killed;
            }
            if (killed == 0) {
                err = "No entity was found";
                return false;
            }
            if (src.feedback) src.feedback(killed == 1 ? "Killed 1 entity" : std::format("Killed {} entities", killed));
            return true;
        }
        if (!a.empty() && a[0] != "@s" && a[0] != "@p" && a[0] != src.name) {
            err = "expected @s, @p, @e or @e[type=<entity>]";
            return false;
        }
        if (!src.kill) return false;
        src.kill();
        if (src.feedback) src.feedback("Killed " + src.name);
        return true;
    });

    // /experience (/xp) add|set|query <target> ...: the target is always the player.
    const Handler experience = [](CommandSource& src, Args a, std::string& err) {
        if (!src.addExperience || !src.experience || a.size() < 2) {
            err = "expected add|set|query, a target and an amount";
            return false;
        }
        if (a[0] == "query") {
            const bool levels = a.size() > 2 && a[2] == "levels";
            if (src.feedback)
                src.feedback(std::format("{} has {} experience {}", src.name, src.experience(levels), levels ? "levels" : "points"));
            return true;
        }
        long long amount = 0;
        if ((a[0] != "add" && a[0] != "set") || a.size() < 3 || !parseLong(a[2], amount)) {
            err = "expected add|set <target> <amount> [points|levels]";
            return false;
        }
        const bool levels = a.size() > 3 && a[3] == "levels";
        if (a[0] == "set") {
            if (amount < 0) {
                err = "the amount can't be negative";
                return false;
            }
            src.addExperience(-src.experience(levels) + int(amount), levels);
        } else {
            src.addExperience(int(amount), levels);
        }
        if (src.feedback)
            src.feedback(std::format("{} {} experience {} {} {}", a[0] == "add" ? "Gave" : "Set", amount,
                                     levels ? "levels" : "points", a[0] == "add" ? "to" : "on", src.name));
        return true;
    };
    add("experience", "/experience (add|set|query) <targets> <amount> [points|levels]", experience);
    add("xp", "/xp (add|set|query) <targets> <amount> [points|levels]", experience);

    // The rules that exist so far, with vanilla's names: switches, and randomTickSpeed.
    std::string ruleUsage = "/gamerule <";
    for (const std::string_view name : Level::gameRuleNames()) ruleUsage += std::string(name) + "|";
    ruleUsage.back() = '>';
    add("gamerule", ruleUsage + " [value]", [](CommandSource& src, Args a, std::string& err) {
        if (!src.level || a.empty() || a.size() > 2) {
            err = "expected a rule and an optional value";
            return false;
        }
        const auto value = src.level->gameRule(a[0]);
        if (!value) {
            err = "unknown game rule " + a[0];
            return false;
        }
        if (a.size() == 1) {
            if (src.feedback) src.feedback(std::format("Gamerule {} is currently set to: {}", a[0], *value));
            return true;
        }
        if (!src.level->setGameRule(a[0], a[1], &err)) return false;
        if (src.feedback) src.feedback(std::format("Gamerule {} is now set to: {}", a[0], *src.level->gameRule(a[0])));
        return true;
    });
}

void Commands::add(std::string name, std::string usage, Handler handler, int permission) {
    commands_[std::move(name)] = {std::move(usage), std::move(handler), permission};
}

bool Commands::execute(CommandSource& source, std::string_view line, std::string* error) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '/')) line.remove_prefix(1);
    const auto tokens = tokenizeCommand(line);
    std::string err;
    if (tokens.empty()) {
        if (error) *error = "empty command";
        return false;
    }
    auto it = commands_.find(tokens[0]);
    // Operator commands don't exist for others (vanilla: unknown command).
    if (it == commands_.end() || source.permission < it->second.permission) {
        if (error) *error = std::format("Unknown or incomplete command '{}'", tokens[0]);
        return false;
    }
    const bool ok = it->second.handler(source, Args(tokens).subspan(1), err);
    if (!ok && error) *error = std::format("{}: {} (usage: {})", tokens[0], err, it->second.usage);
    return ok;
}

std::vector<std::string> Commands::names(int permission) const {
    std::vector<std::string> out;
    for (const auto& [n, c] : commands_)
        if (permission >= c.permission) out.push_back(n);
    return out;
}

std::string Commands::usage(const std::string& name) const {
    auto it = commands_.find(name);
    return it == commands_.end() ? std::string() : it->second.usage;
}

} // namespace mc
