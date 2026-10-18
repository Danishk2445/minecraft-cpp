// ActionScript.h -- --script FILE: a run played through the game's own controls, for the
// end-to-end test (wood, stone, iron, diamonds). Each line is an action carried out over
// ticks as a player would: turning to look, holding forward or jump, the attack and use
// buttons, and clicks on menu slots. Nothing is given or teleported.
//
//   find BLOCK|#TAG [RADIUS]   the nearest such block (default within 32) becomes the target
//   mine                       get within reach of the target (walking, else digging a
//                              staircase toward it), break it with the best tool, and pick
//                              up its drops
//   goto                       get within reach of the target (walking, else digging)
//   craft ITEM|#TAG [COUNT]    craft (at least COUNT) through slot clicks: in the inventory's
//                              2x2 grid, or at the nearest crafting table within 6 blocks
//   place ITEM                 put the block down in a free cell near the player (digging
//                              a niche in a narrow tunnel)
//   smelt ITEM COUNT FUEL N    fill the nearest furnace within 6 blocks (N fuel items), wait
//                              for the output and take it
//   expect ITEM|#TAG COUNT     fail unless the inventory holds at least COUNT
//   repeat N / until ITEM|#TAG COUNT ... end    loops (until: while fewer than COUNT)
//   log TEXT
//
// A line starting with '#', or a lone '#' after the arguments, starts a comment ("#planks"
// and "#minecraft:logs" are tags).
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "game/KeyBindings.h"
#include "game/Menu.h"
#include "world/block/BlockRegistry.h"

namespace mc {

class WorldSession;
struct Item;

struct ScriptInstruction {
    int line = 0;
    std::vector<std::string> words; // the action and its arguments
    int jump = -1;                  // loops: the matching "end" (for repeat/until) or start (for end)
};

// Parses a script; null with the problem (and its line) in `error`.
std::optional<std::vector<ScriptInstruction>> parseActionScript(std::string_view text, std::string* error);

class ActionScript {
public:
    ActionScript(WorldSession& world, std::vector<ScriptInstruction> program);
    ~ActionScript();

    // Once a tick, before the player acts: the next step of the current action.
    void tick();
    bool done() const { return done_; }
    bool failed() const { return failed_; }
    // The controls the script holds down this tick.
    bool held(KeyAction a) const { return held_[size_t(a)]; }

    // A step of an action: running, finished, or failed (with why).
    enum class Status { Running, Done, Failed };
    struct Task;

    // What tasks use.
    WorldSession& world() { return world_; }
    const BlockRegistry& registry() const;
    glm::dvec3 eye() const;
    // Turns to look at a point (and updates what the crosshair targets).
    void lookAt(const glm::dvec3& point);
    void hold(KeyAction a, bool down = true) { held_[size_t(a)] = down; }
    void releaseAll() { held_.fill(false); }
    // Whether `p` can be hit from where the player stands: the point to look at.
    std::optional<glm::dvec3> reachPoint(const BlockPos& p) const;
    // Puts the inventory's best item for breaking `state` in hand; false when it can't
    // harvest the block.
    bool selectToolFor(StateId state);
    // Puts an item matching `spec` in hand (moving it to the hotbar if needed); false if none.
    bool selectItem(const std::string& spec);
    bool itemMatches(const Item* item, const std::string& spec) const;
    int countItems(const std::string& spec) const;
    // The menu slot showing inventory slot `index` (0-35), or -1.
    static int menuSlotOf(const Menu& menu, int index);
    void click(Menu& menu, int slot, int button, ClickType type);
    void fail(const std::string& why);

    std::optional<BlockPos> target; // the last find
    std::vector<BlockPos> placed;   // blocks the script put down: digging goes around them

private:
    void startNext();

    WorldSession& world_;
    std::vector<ScriptInstruction> program_;
    size_t pc_ = 0;
    std::vector<int> loopCounts_; // per instruction: iterations left (repeat) or done (until)
    std::unique_ptr<Task> task_;
    std::array<bool, kKeyActionCount> held_{};
    bool done_ = false, failed_ = false;
    long long startTick_ = 0;
};

} // namespace mc
