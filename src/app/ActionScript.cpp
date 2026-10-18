#include "app/ActionScript.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <sstream>

#include "app/Client.h"
#include "app/WorldSession.h"
#include "core/Log.h"
#include "entity/ItemEntity.h"
#include "entity/path/PathFinder.h"
#include "game/ContainerMenus.h"
#include "game/Item.h"
#include "game/Recipes.h"
#include "game/Tools.h"
#include "world/level/Level.h"
#include "world/level/Raycast.h"

namespace mc {
namespace {

using Status = ActionScript::Status;

constexpr double kReach = 4.5; // vanilla block_interaction_range in survival

std::optional<int> parseInt(std::string_view s) {
    int v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size()) return std::nullopt;
    return v;
}

std::string withNamespace(const std::string& id) {
    if (id.empty() || id[0] == '#') return id.size() > 1 && id.find(':') == std::string::npos ? "#minecraft:" + id.substr(1) : id;
    return id.find(':') == std::string::npos ? "minecraft:" + id : id;
}

glm::dvec3 center(const BlockPos& p) { return glm::dvec3(p.x, p.y, p.z) + 0.5; }

double horizontalDistance(const glm::dvec3& a, const glm::dvec3& b) { return glm::length(glm::dvec2(a.x - b.x, a.z - b.z)); }

} // namespace

std::optional<std::vector<ScriptInstruction>> parseActionScript(std::string_view text, std::string* error) {
    std::vector<ScriptInstruction> out;
    std::vector<size_t> open; // loop heads waiting for their "end"
    std::istringstream in{std::string(text)};
    std::string lineText;
    int line = 0;
    const auto problem = [&](const std::string& what) {
        if (error) *error = std::format("line {}: {}", line, what);
        return std::nullopt;
    };
    while (std::getline(in, lineText)) {
        ++line;
        ScriptInstruction ins;
        ins.line = line;
        std::istringstream words(lineText);
        std::string w;
        while (words >> w) {
            // A comment: a line starting with '#', or a lone '#' ("#planks" is a tag).
            if (w == "#" || (ins.words.empty() && w[0] == '#')) break;
            ins.words.push_back(w);
        }
        if (ins.words.empty()) continue;
        const std::string& a = ins.words[0];
        const size_t n = ins.words.size();
        const auto count = [&](size_t i) { return i < n ? parseInt(ins.words[i]) : std::nullopt; };
        if (a == "repeat") {
            if (n != 2 || !count(1) || *count(1) < 0) return problem("repeat N");
            open.push_back(out.size());
        } else if (a == "until") {
            if (n != 3 || !count(2)) return problem("until ITEM COUNT");
            open.push_back(out.size());
        } else if (a == "end") {
            if (open.empty() || n != 1) return problem("end without repeat or until");
            ins.jump = int(open.back());
            out[open.back()].jump = int(out.size());
            open.pop_back();
        } else if (a == "find") {
            if (n < 2 || n > 3 || (n == 3 && !count(2))) return problem("find BLOCK|#TAG [RADIUS]");
        } else if (a == "mine" || a == "goto") {
            if (n != 1) return problem(a + " takes no arguments");
        } else if (a == "craft") {
            if (n < 2 || n > 3 || (n == 3 && !count(2))) return problem("craft ITEM|#TAG [COUNT]");
        } else if (a == "place") {
            if (n != 2) return problem("place ITEM");
        } else if (a == "smelt") {
            if (n != 5 || !count(2) || !count(4)) return problem("smelt ITEM COUNT FUEL FUELCOUNT");
        } else if (a == "expect") {
            if (n != 3 || !count(2)) return problem("expect ITEM|#TAG COUNT");
        } else if (a != "log") {
            return problem("unknown action '" + a + "'");
        }
        out.push_back(std::move(ins));
    }
    if (!open.empty()) {
        line = out[open.back()].line;
        return problem("loop without end");
    }
    return out;
}

// --- Tasks ---

struct ActionScript::Task {
    virtual ~Task() = default;
    virtual Status step(ActionScript& s) = 0;
    std::string error;
    int ticks = 0;
    Status failWith(std::string why) {
        error = std::move(why);
        return Status::Failed;
    }
};

namespace {

using Task = ActionScript::Task;

// Turns toward a point on the ground and walks, jumping onto steps and over bumps.
void steer(ActionScript& s, const glm::dvec3& to) {
    Player& p = s.world().player();
    const glm::dvec3 d = to - p.pos;
    if (std::abs(d.x) + std::abs(d.z) > 1e-6) p.yaw = float(glm::degrees(std::atan2(-d.x, d.z)));
    p.pitch = 0.0f;
    s.hold(KeyAction::Forward);
    s.hold(KeyAction::Jump, p.onGround && (p.horizontalCollision || to.y > p.pos.y + 0.5));
}

// Breaks one block in reach: looking at it, attack held, until it is gone.
struct BreakTask : Task {
    BlockPos pos;
    uint16_t block = 0;
    int limit = 0;
    bool harvest = false; // the drops matter: fail unless the tool can harvest it
    bool covered = false; // failed because it can't be seen from here
    BreakTask(ActionScript& s, const BlockPos& p, bool harvest) : pos(p), harvest(harvest) {
        block = s.registry().info(s.world().level().getBlock(p)).block;
    }
    Status step(ActionScript& s) override {
        Level& level = s.world().level();
        const StateId now = level.getBlock(pos);
        if (s.registry().info(now).block != block) {
            s.hold(KeyAction::Attack, false);
            return Status::Done;
        }
        if (ticks++ == 0) {
            if (!s.selectToolFor(now) && harvest)
                return failWith(std::format("nothing in the inventory harvests {}", s.registry().describe(now)));
            const float perTick = destroyProgressPerTick(now, s.world().player(), s.registry());
            if (perTick <= 0.0f) return failWith(std::format("{} can't be broken", s.registry().describe(now)));
            limit = int(std::ceil(1.0f / perTick)) + 40;
        }
        if (ticks > limit) return failWith(std::format("{} didn't break", s.registry().describe(now)));
        const std::optional<glm::dvec3> at = s.reachPoint(pos);
        if (!at) {
            covered = true; // something (fallen gravel, say) came in between
            const glm::dvec3 e = s.eye();
            const std::optional<BlockHit> hit =
                raycastBlocks(level, e, glm::normalize(center(pos) - e), 6.0, RayFluid::None, RayShape::Collider);
            const std::string blocker = hit ? std::format("{} at {} {} {}", s.registry().describe(level.getBlock(hit->pos)),
                                                          hit->pos.x, hit->pos.y, hit->pos.z)
                                            : "nothing";
            return failWith(std::format("{} at {} {} {} is out of reach (eye {:.2f} {:.2f} {:.2f}; the way there hits {})",
                                        s.registry().describe(now), pos.x, pos.y, pos.z, e.x, e.y, e.z, blocker));
        }
        s.lookAt(*at);
        s.hold(KeyAction::Attack);
        return Status::Running;
    }
};

// Blocks a right click uses instead of placing against (menus, doors, beds, ...).
bool reactsToUse(std::string_view id) {
    constexpr std::string_view kUsed[] = {"crafting_table", "chest", "barrel", "furnace", "smoker", "door", "gate",
                                          "button", "lever", "bed", "cake", "sign", "anvil", "bell", "note_block",
                                          "repeater", "comparator", "jukebox", "flower_pot", "cauldron", "composter",
                                          "lectern", "loom", "grindstone", "stonecutter", "cartography_table",
                                          "smithing_table", "brewing_stand", "enchanting_table", "beacon", "campfire"};
    for (std::string_view u : kUsed)
        if (id.find(u) != std::string_view::npos) return true;
    return false;
}

bool solidFloor(const BlockRegistry& reg, Level& level, const BlockPos& p) {
    return (reg.info(level.getBlock(p)).flags & kBlocksMotion) != 0;
}

bool collides(const BlockRegistry& reg, Level& level, const BlockPos& p) {
    return (reg.info(level.getBlock(p)).flags & kHasCollision) != 0;
}

// One step of a staircase dug toward a goal: the cells ahead cleared (top first), then the
// player walks (or jumps) into them, maybe dropping a block or two to the floor.
struct TunnelStep : Task {
    BlockPos next;  // the cell walked (or jumped) into
    BlockPos land;  // where the feet end up: `next`, or below it after a short drop
    std::vector<BlockPos> clear;
    bool up = false;
    std::unique_ptr<BreakTask> breaking;
    bool moving = false;
    bool stuck = false; // couldn't get into the cleared cells: try another way
    int moveTicks = 0;
    Status step(ActionScript& s) override {
        Player& p = s.world().player();
        if (!moving) {
            if (breaking) {
                const Status st = breaking->step(s);
                if (st == Status::Running) return st;
                // Covered over (gravel falling in front): clear again from the top.
                if (st == Status::Failed && !breaking->covered) return failWith(breaking->error);
                log::debug("Script: tunnel break {} {} {}: {}", breaking->pos.x, breaking->pos.y, breaking->pos.z,
                           st == Status::Failed ? breaking->error : "done");
                breaking.reset();
            }
            // Falling blocks settle first.
            for (Entity* e : s.world().level().entitiesIn(AABB(center(next), center(next)).inflated(2.0, 4.0, 2.0)))
                if (e->type() == "minecraft:falling_block") return Status::Running;
            for (const BlockPos& c : clear)
                if (collides(s.registry(), s.world().level(), c)) {
                    if (++ticks > 24)
                        return failWith(std::format("the way keeps filling in ({} at {} {} {})",
                                                    s.registry().describe(s.world().level().getBlock(c)), c.x, c.y, c.z));
                    breaking = std::make_unique<BreakTask>(s, c, false);
                    return step(s);
                }
            moving = true;
        }
        const BlockPos feet = BlockPos::containing(p.pos);
        if (feet.x == land.x && feet.z == land.z && feet.y <= next.y && p.onGround &&
            horizontalDistance(p.pos, center(land)) < 0.3) {
            s.releaseAll();
            return Status::Done;
        }
        if (++moveTicks > 100) {
            stuck = true;
            return failWith(std::format("stuck stepping to {} {} {}", next.x, next.y, next.z));
        }
        steer(s, glm::dvec3(next.x + 0.5, next.y, next.z + 0.5));
        if (up && p.onGround) s.hold(KeyAction::Jump);
        return Status::Running;
    }
};

// Plans the next staircase step from the player's feet toward `goal`: down, level or up,
// along the axis with the most to go first. Never into lava, never next to it when a block
// has to be broken (nor next to water then: it would pour in), never back into a cell in
// `visited`, never off more than a 2-block drop.
std::unique_ptr<TunnelStep> planTunnel(ActionScript& s, const BlockPos& goal, const std::vector<BlockPos>& visited) {
    const BlockRegistry& reg = s.registry();
    Level& level = s.world().level();
    const BlockPos f = BlockPos::containing(s.world().player().pos);
    const glm::ivec3 d = goal.vec() - f.vec();
    std::vector<glm::ivec3> dirs;
    const glm::ivec3 ax(d.x > 0 ? 1 : -1, 0, 0), az(0, 0, d.z > 0 ? 1 : -1);
    if (std::abs(d.x) >= std::abs(d.z)) {
        if (d.x != 0) dirs.push_back(ax);
        if (d.z != 0) dirs.push_back(az);
    } else {
        if (d.z != 0) dirs.push_back(az);
        if (d.x != 0) dirs.push_back(ax);
    }
    for (const glm::ivec3 o : {glm::ivec3(1, 0, 0), glm::ivec3(0, 0, 1), glm::ivec3(-1, 0, 0), glm::ivec3(0, 0, -1)})
        if (std::find(dirs.begin(), dirs.end(), o) == dirs.end()) dirs.push_back(o);
    const int want = d.y < 0 ? -1 : d.y > 1 ? 1 : 0;
    const auto lava = [&](const BlockPos& c) { return level.fluid(c).kind == FluidKind::Lava; };
    const auto fluidAround = [&](const BlockPos& c) {
        for (Direction dir : kDirections)
            if (!level.fluid(c.offset(dir)).empty()) return true;
        return false;
    };
    std::vector<int> verticals{want};
    for (int v : {0, -1, 1})
        if (v != want) verticals.push_back(v);
    for (int vertical : verticals) {
        for (const glm::ivec3 o : dirs) {
            auto t = std::make_unique<TunnelStep>();
            const BlockPos ahead(f.vec() + o);
            if (vertical < 0) {
                t->next = ahead.below();
                t->clear = {ahead.above(), ahead, t->next};
            } else if (vertical > 0) {
                t->next = ahead.above();
                t->clear = {f.above(2), ahead.above(2), t->next};
                t->up = true;
                if (!solidFloor(reg, level, ahead)) continue; // nothing to step up onto
            } else {
                t->next = ahead;
                t->clear = {ahead.above(), ahead};
            }
            bool bad = false;
            for (const BlockPos& c : t->clear)
                bad = bad || lava(c) || (collides(reg, level, c) && fluidAround(c)) ||
                      std::find(s.placed.begin(), s.placed.end(), c) != s.placed.end();
            if (bad) continue;
            // The floor: here, or a short drop onto something solid (not into a fluid).
            t->land = t->next;
            for (int drop = 0; !t->up && !solidFloor(reg, level, t->land.below()); ++drop) {
                if (drop == 2 || !level.fluid(t->land.below()).empty()) {
                    bad = true;
                    break;
                }
                t->land = t->land.below();
            }
            if (bad || std::find(visited.begin(), visited.end(), t->land) != visited.end()) continue;
            return t;
        }
    }
    return nullptr;
}

// A walking path (vanilla PathFinder for a 0.6 x 1.8 mob) and steering along it.
std::optional<Path> walkingPath(ActionScript& s, const BlockPos& to, int reachRange) {
    const Player& p = s.world().player();
    PathMob mob;
    mob.pos = p.pos;
    mob.onGround = p.onGround;
    const BlockPos targets[] = {to};
    return findPath(s.world().level(), mob, targets, 48.0f, reachRange, 6000);
}

struct PathFollower {
    Path path;
    glm::dvec3 lastPos{0.0};
    int still = 0;
    explicit PathFollower(Path p) : path(std::move(p)) {}
    // Walks toward the next node; false once stuck (no progress for two seconds).
    bool step(ActionScript& s) {
        Player& p = s.world().player();
        while (!path.done()) {
            const glm::dvec3 n = path.nextEntityPos(p.width);
            if (horizontalDistance(p.pos, n) < 0.4 && std::abs(p.pos.y - n.y) < 1.0) path.advance();
            else break;
        }
        if (path.done()) return true;
        steer(s, path.nextEntityPos(p.width));
        if (glm::distance(p.pos, lastPos) > 0.5) {
            lastPos = p.pos;
            still = 0;
        } else if (++still > 40) {
            return false;
        }
        return true;
    }
    bool done() const { return path.done(); }
};

// Gets within reach of a block: along a walking path when there is one, else digging
// toward it.
struct ApproachTask : Task {
    BlockPos goal;
    bool planned = false, tunnel = false;
    std::optional<PathFollower> walk;
    std::unique_ptr<TunnelStep> digging;
    std::vector<BlockPos> visited; // feet cells dug through, not to step back into
    int jams = 0;
    explicit ApproachTask(const BlockPos& g) : goal(g) {}
    Status step(ActionScript& s) override {
        Player& p = s.world().player();
        if (s.reachPoint(goal)) {
            s.releaseAll();
            return Status::Done;
        }
        if (++ticks > 6000)
            return failWith(std::format("couldn't get to {} {} {} (at {:.1f} {:.1f} {:.1f})", goal.x, goal.y, goal.z, p.pos.x,
                                        p.pos.y, p.pos.z));
        if (!planned) {
            planned = true;
            std::optional<Path> path = walkingPath(s, goal, 3);
            // A path that ends with the target in reach is as good as one that "reached" it.
            const auto endsInReach = [&] {
                const PathPoint& e = path->endNode();
                return glm::distance(glm::dvec3(e.x + 0.5, e.y + 1.62, e.z + 0.5), center(goal)) <= kReach - 0.5;
            };
            tunnel = !path || (!path->reached && !endsInReach());
            if (!tunnel) walk.emplace(std::move(*path));
            log::debug("Script: from {:.1f} {:.1f} {:.1f}: {}", p.pos.x, p.pos.y, p.pos.z, tunnel ? "digging" : "walking");
        }
        if (!tunnel) {
            if (!walk->step(s)) log::debug("Script: stuck at {:.1f} {:.1f} {:.1f} on the path: digging", p.pos.x, p.pos.y, p.pos.z);
            else if (!walk->done()) return Status::Running;
            tunnel = true; // at the end of the path and still out of reach, or stuck: dig on
            s.releaseAll();
        }
        if (!digging) {
            visited.push_back(BlockPos::containing(p.pos));
            digging = planTunnel(s, goal, visited);
            if (!digging) return failWith("every way toward the target is blocked by water, lava or a drop");
            log::debug("Script: dig from {:.1f} {:.1f} {:.1f} to {} {} {}", p.pos.x, p.pos.y, p.pos.z, digging->next.x,
                       digging->next.y, digging->next.z);
        }
        const Status st = digging->step(s);
        if (st == Status::Failed) {
            if (!digging->stuck || ++jams > 8) return failWith(digging->error);
            log::debug("Script: {}: another way", digging->error);
            visited.push_back(digging->land);
            s.releaseAll();
            digging.reset();
        }
        if (st == Status::Done) digging.reset();
        return Status::Running;
    }
};

// Walks over the item drops around a spot (they can be picked up after 10 ticks) along
// walking paths, digging toward those no path leads to.
struct CollectTask : Task {
    glm::dvec3 around;
    std::vector<uint32_t> skipped;
    uint32_t chasing = 0;
    int chaseTicks = 0;
    std::optional<PathFollower> walk;
    std::unique_ptr<TunnelStep> digging;
    std::vector<BlockPos> visited;
    explicit CollectTask(const glm::dvec3& at) : around(at) {}
    Status step(ActionScript& s) override {
        if (++ticks < 12) return Status::Running;
        Player& p = s.world().player();
        Level& level = s.world().level();
        const ItemEntity* best = nullptr;
        double bestD = 1e9;
        for (Entity* e : level.entitiesIn(AABB(around, around).inflated(5.0, 5.0, 5.0))) {
            const auto* item = dynamic_cast<const ItemEntity*>(e);
            if (!item || item->removed || std::find(skipped.begin(), skipped.end(), item->id) != skipped.end()) continue;
            const double d = glm::distance(item->pos, p.pos);
            if (d < bestD) bestD = d, best = item;
        }
        if (!best || ticks > 1200) {
            s.releaseAll();
            return Status::Done;
        }
        const BlockPos cell = BlockPos::containing(best->pos);
        if (best->id != chasing) {
            chasing = best->id;
            chaseTicks = 0;
            walk.reset();
            digging.reset();
            visited.clear();
            if (std::optional<Path> path = walkingPath(s, cell, 1); path && path->reached) walk.emplace(std::move(*path));
            log::debug("Script: drop at {:.1f} {:.1f} {:.1f}: {}", best->pos.x, best->pos.y, best->pos.z,
                       walk ? "walking" : "digging");
        }
        const auto giveUp = [&] {
            skipped.push_back(best->id); // leave it
            s.releaseAll();
            return Status::Running;
        };
        if (++chaseTicks > 400) return giveUp();
        if (walk) {
            if (!walk->step(s)) return giveUp();
            if (walk->done()) steer(s, best->pos);
            return Status::Running;
        }
        // No way there: dig toward it a step at a time.
        if (!digging) {
            visited.push_back(BlockPos::containing(p.pos));
            digging = planTunnel(s, cell, visited);
            if (!digging) return giveUp();
        }
        const Status st = digging->step(s);
        if (st == Status::Failed) return giveUp();
        if (st == Status::Done) digging.reset();
        return Status::Running;
    }
};

struct MineTask : Task {
    BlockPos pos;
    uint16_t block = 0;
    std::unique_ptr<Task> sub;
    int phase = 0, retries = 0;
    MineTask(ActionScript& s, const BlockPos& p) : pos(p) { block = s.registry().info(s.world().level().getBlock(p)).block; }
    Status step(ActionScript& s) override {
        if (!sub) {
            if (phase == 0) {
                sub = std::make_unique<ApproachTask>(pos);
            } else if (phase == 1) {
                // Dug through on the way (its drop is underfoot): nothing left to break.
                if (s.registry().info(s.world().level().getBlock(pos)).block != block) phase = 2;
                else sub = std::make_unique<BreakTask>(s, pos, true);
            }
            if (phase == 2) sub = std::make_unique<CollectTask>(center(pos));
        }
        const Status st = sub->step(s);
        if (st == Status::Failed) {
            // Hidden while breaking it (something fell in between): get to it again.
            const auto* breaking = dynamic_cast<const BreakTask*>(sub.get());
            if (!breaking || !breaking->covered || ++retries > 4) return failWith(sub->error);
            sub.reset();
            phase = 0;
            return Status::Running;
        }
        if (st == Status::Done) {
            sub.reset();
            if (++phase == 3) return Status::Done;
        }
        return Status::Running;
    }
};

// Opens a block's menu by using it, does `clicks` one a tick, and closes it.
struct MenuSession {
    enum class Stage { Opening, Open, Closing, Closed } stage = Stage::Opening;
    int waited = 0;
    bool waitOpen(ActionScript& s, bool wantInventory, const BlockPos* block, std::string& error) {
        WorldSession& w = s.world();
        if (wantInventory) {
            if (w.screenOpen()) return true;
            if (waited % 5 == 0) w.requestScreen("inventory"); // again if another request took the turn
        } else {
            if (waited == 0) {
                const std::optional<glm::dvec3> at = s.reachPoint(*block);
                if (!at) {
                    error = "the block is out of reach";
                    return false;
                }
                s.lookAt(*at);
                w.useClick();
            }
            if (w.openMenu() && w.screenOpen()) return true;
        }
        if (++waited > 40) error = "the menu didn't open";
        return false;
    }
};

// The nearest block with this id within `radius` of the eyes.
std::optional<BlockPos> nearestBlock(ActionScript& s, std::string_view id, int radius) {
    const Block* block = s.registry().find(id);
    if (!block) return std::nullopt;
    const BlockPos f = BlockPos::containing(s.eye());
    std::optional<BlockPos> best;
    double bestD = 1e18;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dz = -radius; dz <= radius; ++dz)
            for (int dx = -radius; dx <= radius; ++dx) {
                const BlockPos p = f.offset(dx, dy, dz);
                if (s.registry().info(s.world().level().getBlock(p)).block != block->index) continue;
                const double d = glm::distance(center(p), s.eye());
                if (d < bestD) bestD = d, best = p;
            }
    return best;
}

// A grid filled from inventory slots: which slot feeds each cell.
struct CraftPlan {
    const Recipe* recipe = nullptr;
    std::vector<std::pair<int, int>> cells; // (grid cell, inventory slot)
};

std::optional<CraftPlan> planCraft(ActionScript& s, const std::string& spec, int gridSize) {
    const Inventory& inv = s.world().player().inventory;
    const RecipeBook& book = *s.world().client().recipes;
    for (const Recipe& r : book.recipes()) {
        if (!r.crafting() || !s.itemMatches(r.result.item, spec)) continue;
        if (r.type == RecipeType::Shaped ? (r.width > gridSize || r.height > gridSize)
                                         : int(r.ingredients.size()) > gridSize * gridSize)
            continue;
        std::array<int, Inventory::kMainSize> left{};
        for (int i = 0; i < Inventory::kMainSize; ++i) left[size_t(i)] = inv.main[size_t(i)].count;
        CraftPlan plan{&r, {}};
        bool ok = true;
        for (size_t k = 0; k < r.ingredients.size() && ok; ++k) {
            const Ingredient& ing = r.ingredients[k];
            if (ing.empty()) continue;
            const int cell = r.type == RecipeType::Shaped ? int(k / size_t(r.width)) * gridSize + int(k % size_t(r.width)) : int(k);
            ok = false;
            for (int i = 0; i < Inventory::kMainSize; ++i)
                if (left[size_t(i)] > 0 && ing.test(inv.main[size_t(i)])) {
                    --left[size_t(i)];
                    plan.cells.emplace_back(cell, i);
                    ok = true;
                    break;
                }
        }
        if (ok) return plan;
    }
    return std::nullopt;
}

struct CraftTask : Task {
    std::string spec;
    int goal = 0;
    int gridSize = 2;
    std::optional<BlockPos> table;
    std::unique_ptr<ApproachTask> approach;
    MenuSession menu;
    struct Click {
        int slot, button;
        ClickType type;
    };
    std::vector<Click> clicks;
    size_t nextClick = 0;
    int deposits = 0;
    CraftTask(ActionScript& s, std::string sp, int count) : spec(std::move(sp)) { goal = s.countItems(spec) + count; }
    Menu* currentMenu(ActionScript& s) {
        return gridSize == 2 ? static_cast<Menu*>(&s.world().inventoryMenu()) : s.world().openMenu();
    }
    Status step(ActionScript& s) override {
        if (++ticks > 2400) return failWith("crafting took too long");
        if (menu.stage == MenuSession::Stage::Opening) {
            if (ticks == 1) {
                s.releaseAll();
                if (!planCraft(s, spec, 2)) {
                    if (!planCraft(s, spec, 3)) return failWith("no recipe for " + spec + " from what the inventory holds");
                    gridSize = 3;
                    // The nearest crafting table, gone up to if needed.
                    table = nearestBlock(s, "crafting_table", 6);
                    if (!table) return failWith("crafting " + spec + " needs a crafting table nearby");
                    if (!s.reachPoint(*table)) approach = std::make_unique<ApproachTask>(*table);
                }
            }
            if (approach) {
                const Status st = approach->step(s);
                if (st == Status::Failed) return failWith(approach->error);
                if (st == Status::Running) return st;
                approach.reset();
                s.releaseAll();
            }
            if (!menu.waitOpen(s, gridSize == 2, table ? &*table : nullptr, error)) {
                return error.empty() ? Status::Running : Status::Failed;
            }
            if (gridSize == 3 && !dynamic_cast<CraftingMenu*>(s.world().openMenu())) return failWith("not a crafting menu");
            menu.stage = MenuSession::Stage::Open;
        }
        if (menu.stage == MenuSession::Stage::Open) {
            Menu& m = *currentMenu(s);
            if (nextClick < clicks.size()) {
                const Click& c = clicks[nextClick++];
                s.click(m, c.slot, c.button, c.type);
                return Status::Running;
            }
            // The result on the cursor goes onto a stack of the same item with room, else an
            // empty slot (chosen now: drops picked up meanwhile take slots too).
            if (!m.carried().empty()) {
                const ItemStack& held = m.carried();
                const Inventory& inventory = s.world().player().inventory;
                int to = -1;
                for (int pass = 0; pass < 2 && to < 0; ++pass)
                    for (int i = 0; i < Inventory::kMainSize && to < 0; ++i) {
                        const ItemStack& st = inventory.main[size_t(i)];
                        if (pass == 0 ? (!st.empty() && st.sameItem(held) && st.count + held.count <= st.maxStack()) : st.empty())
                            to = i;
                    }
                if (to < 0 || ++deposits > 4) return failWith("no room for " + held.item->id);
                s.click(m, ActionScript::menuSlotOf(m, to), 0, ClickType::Pickup);
                return Status::Running;
            }
            deposits = 0;
            clicks.clear();
            nextClick = 0;
            if (s.countItems(spec) >= goal) {
                s.world().requestScreen("close");
                menu.stage = MenuSession::Stage::Closing;
                return Status::Running;
            }
            // One craft: one of each ingredient into its cell (pick the stack up, drop one
            // with the right button, put the rest back), then the result to the inventory.
            const std::optional<CraftPlan> plan = planCraft(s, spec, gridSize);
            if (!plan) return failWith("ran out of ingredients for " + spec);
            const int gridStart = gridSize == 2 ? InventoryMenu::kCraftStart : CraftingMenu::kGridStart;
            for (const auto& [cell, inv] : plan->cells) {
                const int from = ActionScript::menuSlotOf(m, inv);
                clicks.push_back({from, 0, ClickType::Pickup});
                clicks.push_back({gridStart + cell, 1, ClickType::Pickup});
                clicks.push_back({from, 0, ClickType::Pickup});
            }
            clicks.push_back({0, 0, ClickType::Pickup}); // the result (slot 0 in both menus) onto the cursor
            return Status::Running;
        }
        if (!s.world().screenOpen()) return Status::Done;
        if (++menu.waited > 60) return failWith("the menu didn't close");
        return Status::Running;
    }
};

// Places a block from the inventory in a free cell near the player: against the cell's
// floor if the player can see its top, else any side of a solid neighbor it can see.
struct PlaceTask : Task {
    std::string spec;
    std::optional<BlockPos> cell;
    std::vector<BlockPos> tried;
    uint16_t block = 0;
    int wait = 0, niches = 0;
    std::unique_ptr<BreakTask> niche;
    explicit PlaceTask(std::string sp) : spec(std::move(sp)) {}
    Status step(ActionScript& s) override {
        Level& level = s.world().level();
        const BlockRegistry& reg = s.registry();
        Player& p = s.world().player();
        if (cell) {
            if (reg.info(level.getBlock(*cell)).block == block) {
                s.placed.push_back(*cell);
                return Status::Done;
            }
            if (++wait < 6) return Status::Running;
            cell.reset(); // try elsewhere
            if (tried.size() >= 6) return failWith("the block didn't go down");
        }
        if (niche) {
            const Status st = niche->step(s);
            if (st == Status::Running) return st;
            if (st == Status::Failed) return failWith(niche->error);
            niche.reset();
        }
        s.releaseAll();
        if (!p.onGround && ++ticks < 40) return Status::Running; // settle first
        if (!s.selectItem(spec)) return failWith("no " + spec + " to place");
        const Item* item = p.inventory.selectedStack().item;
        if (!item->block) return failWith(item->id + " isn't a block");
        block = item->block->index;
        const BlockPos f = BlockPos::containing(p.pos);
        std::vector<BlockPos> cells;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -3; dz <= 3; ++dz)
                for (int dx = -3; dx <= 3; ++dx) cells.push_back(f.offset(dx, dy, dz));
        std::stable_sort(cells.begin(), cells.end(), [&](const BlockPos& a, const BlockPos& b) {
            return glm::distance(center(a), p.pos) < glm::distance(center(b), p.pos);
        });
        constexpr Direction kFaces[] = {Direction::Down, Direction::North, Direction::South,
                                        Direction::West, Direction::East, Direction::Up};
        int free = 0, clear = 0, faces = 0, near = 0;
        for (const BlockPos& c : cells) {
            const StateInfo& info = reg.info(level.getBlock(c));
            if (!(info.flags & (kAir | kReplaceable)) || !info.fluid.empty()) continue;
            if (std::find(tried.begin(), tried.end(), c) != tried.end()) continue;
            ++free;
            if (AABB(c.x, c.y, c.z, c.x + 1.0, c.y + 1.0, c.z + 1.0).intersects(p.box().inflated(0.05, 0.05, 0.05))) continue;
            ++clear;
            for (Direction dir : kFaces) {
                const BlockPos n = c.offset(dir);
                if (!solidFloor(reg, level, n) || reactsToUse(reg.blockOf(level.getBlock(n)).id)) continue;
                ++faces;
                // The middle of the neighbor's face toward the cell.
                const glm::dvec3 at = center(c) + glm::dvec3(normal(dir)) * 0.499;
                const double dist = glm::distance(at, s.eye());
                if (dist > kReach - 0.2) continue;
                ++near;
                const std::optional<BlockHit> hit = raycastBlocks(level, s.eye(), glm::normalize(at - s.eye()), dist + 0.1);
                if (!hit || hit->pos != n || hit->face != opposite(dir)) continue;
                s.lookAt(at);
                s.world().useClick();
                cell = c;
                tried.push_back(c);
                wait = 0;
                return Status::Running;
            }
        }
        // No room (a narrow tunnel): dig a niche beside the feet, as a player would.
        if (!niche && ++niches <= 4) {
            const BlockPos feet = BlockPos::containing(p.pos);
            for (Direction dir : {Direction::North, Direction::South, Direction::West, Direction::East}) {
                const BlockPos c = feet.offset(dir);
                if (!collides(reg, level, c) || !solidFloor(reg, level, c.below()) ||
                    std::find(s.placed.begin(), s.placed.end(), c) != s.placed.end() || !s.reachPoint(c))
                    continue;
                bool wet = false;
                for (Direction d : kDirections) wet = wet || !level.fluid(c.offset(d)).empty();
                if (wet) continue;
                niche = std::make_unique<BreakTask>(s, c, false);
                log::debug("Script: digging a niche at {} {} {} for {}", c.x, c.y, c.z, spec);
                return Status::Running;
            }
        }
        return failWith(std::format("no free spot to place {} ({} free cells, {} clear of the player, {} sides, {} in reach)",
                                    spec, free, clear, faces, near));
    }
};

// Fills a furnace in reach, waits for the output and takes it.
struct SmeltTask : Task {
    std::string input, fuel;
    int count = 0, fuelCount = 0;
    std::optional<BlockPos> furnace;
    std::unique_ptr<ApproachTask> approach;
    int phase = 0; // 0 open to fill, 1 filling, 2 closing, 3 waiting, 4 open to take, 5 taking, 6 closing
    MenuSession menu;
    std::vector<std::tuple<int, int, ClickType>> clicks;
    size_t nextClick = 0;
    int target = 0;
    SmeltTask(std::string in, int n, std::string f, int fn) : input(std::move(in)), fuel(std::move(f)), count(n), fuelCount(fn) {}
    std::shared_ptr<FurnaceBlockEntity> entity(ActionScript& s) {
        return std::dynamic_pointer_cast<FurnaceBlockEntity>(s.world().level().blockEntity(*furnace));
    }
    // Moves `n` of the first stack matching `spec` into menu slot `to`.
    bool fill(ActionScript& s, Menu& m, const std::string& spec, int n, int to) {
        const Inventory& inv = s.world().player().inventory;
        int left = n;
        for (int i = 0; i < Inventory::kMainSize && left > 0; ++i) {
            const ItemStack& st = inv.main[size_t(i)];
            if (st.empty() || !s.itemMatches(st.item, spec)) continue;
            const int from = ActionScript::menuSlotOf(m, i);
            const int take = std::min(left, st.count);
            clicks.emplace_back(from, 0, ClickType::Pickup);
            if (take == st.count) {
                clicks.emplace_back(to, 0, ClickType::Pickup);
            } else {
                for (int k = 0; k < take; ++k) clicks.emplace_back(to, 1, ClickType::Pickup);
                clicks.emplace_back(from, 0, ClickType::Pickup);
            }
            left -= take;
        }
        return left == 0;
    }
    Status step(ActionScript& s) override {
        WorldSession& w = s.world();
        if (++ticks > count * 200 + 2400) return failWith("smelting took too long");
        if (!furnace) {
            s.releaseAll();
            furnace = nearestBlock(s, "furnace", 6);
            if (!furnace) return failWith("no furnace nearby");
            if (s.countItems(input) < count) return failWith(std::format("fewer than {} {}", count, input));
            if (!s.reachPoint(*furnace)) approach = std::make_unique<ApproachTask>(*furnace);
        }
        if (approach) {
            const Status st = approach->step(s);
            if (st == Status::Failed) return failWith(approach->error);
            if (st == Status::Running) return st;
            approach.reset();
            s.releaseAll();
        }
        switch (phase) {
        case 0:
        case 4:
            if (!menu.waitOpen(s, false, &*furnace, error)) return error.empty() ? Status::Running : Status::Failed;
            if (!dynamic_cast<FurnaceMenu*>(w.openMenu())) return failWith("not a furnace menu");
            menu = {};
            clicks.clear();
            nextClick = 0;
            if (phase == 0) {
                target = entity(s)->items[FurnaceBlockEntity::kOutput].count + count;
                if (!fill(s, *w.openMenu(), input, count, FurnaceMenu::kInput) ||
                    !fill(s, *w.openMenu(), fuel, fuelCount, FurnaceMenu::kFuel))
                    return failWith("not enough to fill the furnace");
            } else {
                clicks.emplace_back(FurnaceMenu::kOutput, 0, ClickType::QuickMove);
            }
            ++phase;
            return Status::Running;
        case 1:
        case 5:
            if (!w.openMenu()) return failWith("the furnace menu closed");
            if (nextClick < clicks.size()) {
                const auto& [slot, button, type] = clicks[nextClick++];
                s.click(*w.openMenu(), slot, button, type);
                return Status::Running;
            }
            w.requestScreen("close");
            ++phase;
            return Status::Running;
        case 2:
        case 6:
            if (w.screenOpen()) return Status::Running;
            if (phase == 6) return Status::Done;
            phase = 3;
            return Status::Running;
        case 3:
            if (entity(s)->items[FurnaceBlockEntity::kOutput].count >= target) phase = 4;
            return Status::Running;
        }
        return Status::Running;
    }
};

// Waits for the chunks around the player, then the nearest matching block.
struct FindTask : Task {
    std::string spec;
    int radius = 32;
    FindTask(std::string sp, int r) : spec(std::move(sp)), radius(r) {}
    Status step(ActionScript& s) override {
        Level& level = s.world().level();
        const BlockRegistry& reg = s.registry();
        const BlockPos f = BlockPos::containing(s.world().player().pos);
        // Every chunk in range ready first, so the same world gives the same answer.
        bool ready = true;
        for (int cz = (f.z - radius) >> 4; cz <= (f.z + radius) >> 4 && ready; ++cz)
            for (int cx = (f.x - radius) >> 4; cx <= (f.x + radius) >> 4 && ready; ++cx)
                ready = level.chunks().isReady({cx, cz});
        if (!ready && ++ticks < 600) return Status::Running;
        std::optional<TagId> tag;
        const Block* block = nullptr;
        if (spec[0] == '#') {
            tag = reg.tag(spec.substr(1));
            if (!tag) return failWith("unknown block tag " + spec);
        } else if (!(block = reg.find(spec))) {
            return failWith("unknown block " + spec);
        }
        std::optional<BlockPos> best;
        double bestD = 1e18;
        const glm::dvec3 from = s.eye();
        const int y0 = std::max(kWorldMinY, f.y - radius), y1 = std::min(kWorldMaxY, f.y + radius);
        for (int y = y0; y <= y1; ++y)
            for (int z = f.z - radius; z <= f.z + radius; ++z)
                for (int x = f.x - radius; x <= f.x + radius; ++x) {
                    const BlockPos p(x, y, z);
                    const StateId st = level.getBlock(p);
                    if (tag ? !reg.hasTag(st, *tag) : reg.info(st).block != block->index) continue;
                    const glm::dvec3 d = center(p) - from;
                    const double dd = glm::dot(d, d);
                    if (dd < bestD) bestD = dd, best = p;
                }
        if (!best) return failWith(std::format("no {} within {} blocks", spec, radius));
        s.target = best;
        log::info("Script: found {} at {} {} {}", s.registry().describe(level.getBlock(*best)), best->x, best->y, best->z);
        return Status::Done;
    }
};

} // namespace

// --- The script ---

ActionScript::ActionScript(WorldSession& world, std::vector<ScriptInstruction> program)
    : world_(world), program_(std::move(program)), loopCounts_(program_.size(), -1) {
    startTick_ = world_.ticks();
}

ActionScript::~ActionScript() = default;

const BlockRegistry& ActionScript::registry() const { return *world_.client().registry; }

glm::dvec3 ActionScript::eye() const {
    const Player& p = world_.player();
    return p.pos + glm::dvec3(0.0, p.eyeHeight(), 0.0);
}

void ActionScript::lookAt(const glm::dvec3& point) {
    Player& p = world_.player();
    const glm::dvec3 d = point - eye();
    p.yaw = float(glm::degrees(std::atan2(-d.x, d.z)));
    p.pitch = float(glm::degrees(std::atan2(-d.y, std::sqrt(d.x * d.x + d.z * d.z))));
    world_.refreshTarget();
}

std::optional<glm::dvec3> ActionScript::reachPoint(const BlockPos& p) const {
    Level& level = world_.level();
    const glm::dvec3 from = eye();
    // The middle of each face toward the eyes, nearest first; the ray there must meet this
    // block first (plants and other blocks without collision in between don't count: they
    // break at the first hit).
    std::vector<std::pair<double, glm::dvec3>> faces;
    for (Direction d : kDirections) {
        const glm::dvec3 n = glm::dvec3(normal(d));
        const glm::dvec3 c = center(p) + n * 0.499;
        if (glm::dot(from - c, n) <= 0.0) continue;
        faces.emplace_back(glm::distance(from, c), c);
    }
    std::sort(faces.begin(), faces.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [dist, c] : faces) {
        if (dist > kReach - 0.05) continue;
        const std::optional<BlockHit> hit =
            raycastBlocks(level, from, glm::normalize(c - from), dist + 0.1, RayFluid::None, RayShape::Collider);
        if (hit && hit->pos == p) return c;
    }
    return std::nullopt;
}

bool ActionScript::itemMatches(const Item* item, const std::string& spec) const {
    if (!item) return false;
    return spec[0] == '#' ? world_.client().items->inTag(*item, spec) : item->id == spec;
}

int ActionScript::countItems(const std::string& spec) const {
    const Inventory& inv = world_.player().inventory;
    int n = 0;
    for (const ItemStack& st : inv.main)
        if (!st.empty() && itemMatches(st.item, spec)) n += st.count;
    if (!inv.offhand.empty() && itemMatches(inv.offhand.item, spec)) n += inv.offhand.count;
    return n;
}

int ActionScript::menuSlotOf(const Menu& menu, int index) {
    for (const Slot& s : menu.slots)
        if (s.playerInventory && s.containerSlot == index) return s.index;
    return -1;
}

void ActionScript::click(Menu& menu, int slot, int button, ClickType type) {
    menu.clicked(slot, button, type, world_.player());
}

// Into the hotbar (a number-key swap in the inventory menu) and selected.
static void bringToHand(ActionScript& s, int index) {
    Inventory& inv = s.world().player().inventory;
    if (index >= Inventory::kHotbarSize) {
        int hotbar = inv.selected;
        for (int i = 0; i < Inventory::kHotbarSize; ++i)
            if (inv.main[size_t(i)].empty()) {
                hotbar = i;
                break;
            }
        InventoryMenu& menu = s.world().inventoryMenu();
        s.click(menu, ActionScript::menuSlotOf(menu, index), hotbar, ClickType::Swap);
        index = hotbar;
    }
    inv.selected = index;
}

bool ActionScript::selectToolFor(StateId state) {
    const Inventory& inv = world_.player().inventory;
    const BlockRegistry& reg = registry();
    // Harvests it, then fastest; bare hands (or a non-tool) over wearing a tool for nothing.
    int best = -1;
    std::tuple<bool, float, bool> bestKey{canHarvest(ItemStack{}, state, reg), toolSpeed(ItemStack{}, state, reg), true};
    for (int i = 0; i < Inventory::kMainSize; ++i) {
        const ItemStack& st = inv.main[size_t(i)];
        if (st.empty()) continue;
        const std::tuple<bool, float, bool> key{canHarvest(st, state, reg), toolSpeed(st, state, reg), !st.damageable()};
        if (key > bestKey) bestKey = key, best = i;
    }
    if (best >= 0) {
        bringToHand(*this, best);
    } else if (!inv.selectedStack().empty() && inv.selectedStack().damageable()) {
        // Bare-handed: an empty hotbar slot, or one holding no tool.
        for (int i = 0; i < Inventory::kHotbarSize; ++i)
            if (inv.main[size_t(i)].empty() || !inv.main[size_t(i)].damageable()) {
                world_.player().inventory.selected = i;
                break;
            }
    }
    return std::get<0>(bestKey);
}

bool ActionScript::selectItem(const std::string& spec) {
    const Inventory& inv = world_.player().inventory;
    for (int i = 0; i < Inventory::kMainSize; ++i)
        if (!inv.main[size_t(i)].empty() && itemMatches(inv.main[size_t(i)].item, spec)) {
            bringToHand(*this, i);
            return true;
        }
    return false;
}

void ActionScript::fail(const std::string& why) {
    const int line = pc_ < program_.size() ? program_[pc_].line : 0;
    log::error("Script failed at line {} after {} ticks: {}", line, world_.ticks() - startTick_, why);
    releaseAll();
    failed_ = done_ = true;
    task_.reset();
}

void ActionScript::startNext() {
    // Control flow and instant actions run until one takes ticks.
    for (int guard = 0; guard < 10000; ++guard) {
        if (pc_ >= program_.size()) {
            log::info("Script: done in {} ticks", world_.ticks() - startTick_);
            releaseAll();
            done_ = true;
            return;
        }
        const ScriptInstruction& ins = program_[pc_];
        const std::vector<std::string>& w = ins.words;
        const std::string& a = w[0];
        const auto arg = [&](size_t i) { return withNamespace(w[i]); };
        const auto num = [&](size_t i) { return *parseInt(w[i]); };
        if (a == "repeat") {
            int& left = loopCounts_[pc_];
            if (left < 0) left = num(1);
            if (left-- == 0) {
                left = -1;
                pc_ = size_t(ins.jump) + 1;
            } else {
                ++pc_;
            }
            continue;
        }
        if (a == "until") {
            int& rounds = loopCounts_[pc_];
            if (countItems(arg(1)) >= num(2)) {
                rounds = -1;
                pc_ = size_t(ins.jump) + 1;
                continue;
            }
            if (++rounds > 200) return fail(std::format("{} {} not reached in 200 rounds", w[1], w[2]));
            ++pc_;
            continue;
        }
        if (a == "end") {
            pc_ = size_t(ins.jump);
            continue;
        }
        if (a == "log") {
            std::string text;
            for (size_t i = 1; i < w.size(); ++i) text += (i > 1 ? " " : "") + w[i];
            log::info("Script: {}", text);
            ++pc_;
            continue;
        }
        if (a == "expect") {
            const int have = countItems(arg(1));
            if (have < num(2)) return fail(std::format("expected {} {}, have {}", num(2), w[1], have));
            log::info("Script: line {}: have {} {}", ins.line, have, w[1]);
            ++pc_;
            continue;
        }
        log::info("Script: line {}: {}", ins.line, [&] {
            std::string s;
            for (const std::string& x : w) s += (s.empty() ? "" : " ") + x;
            return s;
        }());
        if (a == "find") {
            task_ = std::make_unique<FindTask>(arg(1), w.size() > 2 ? num(2) : 32);
        } else if (a == "mine" || a == "goto") {
            if (!target) return fail(a + " without a target (find one first)");
            if (a == "mine") task_ = std::make_unique<MineTask>(*this, *target);
            else task_ = std::make_unique<ApproachTask>(*target);
        } else if (a == "craft") {
            task_ = std::make_unique<CraftTask>(*this, arg(1), w.size() > 2 ? num(2) : 1);
        } else if (a == "place") {
            task_ = std::make_unique<PlaceTask>(arg(1));
        } else if (a == "smelt") {
            task_ = std::make_unique<SmeltTask>(arg(1), num(2), arg(3), num(4));
        }
        return;
    }
    fail("the script loops without acting");
}

void ActionScript::tick() {
    if (done_) return;
    if (world_.player().dead()) return fail("the player died");
    if (!task_) {
        // A screen left open (one an action opened by mistake) closes first.
        if (world_.screenOpen()) {
            world_.requestScreen("close");
            return;
        }
        startNext();
        if (done_ || !task_) return;
    }
    const Status st = task_->step(*this);
    if (st == Status::Failed) return fail(task_->error);
    if (st == Status::Done) {
        task_.reset();
        releaseAll();
        ++pc_;
    }
}

} // namespace mc
