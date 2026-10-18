// Goal.h -- vanilla mob AI goals and their selector (Goal, WrappedGoal,
// GoalSelector): goals run by priority, each claiming control flags (move, look,
// jump, target); a goal of a better priority takes over a flag from a running one.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace mc {

class Level;

enum GoalFlag : uint8_t {
    kGoalMove = 1 << 0,
    kGoalLook = 1 << 1,
    kGoalJump = 1 << 2,
    kGoalTarget = 1 << 3,
};

class Goal {
public:
    virtual ~Goal() = default;
    virtual bool canUse(Level& level) = 0;
    virtual bool canContinueToUse(Level& level) { return canUse(level); }
    virtual bool isInterruptable() const { return true; }
    virtual void start(Level& level) { (void)level; }
    virtual void stop(Level& level) { (void)level; }
    // Goals tick every other tick unless they say otherwise; their delays halve to match.
    virtual bool requiresUpdateEveryTick() const { return false; }
    virtual void tick(Level& level) { (void)level; }
    uint8_t flags = 0;

    int adjustedTickDelay(int ticks) const { return requiresUpdateEveryTick() ? ticks : reducedTickDelay(ticks); }
    static int reducedTickDelay(int ticks) { return (ticks + 1) / 2; } // Mth.positiveCeilDiv(ticks, 2)
};

class GoalSelector {
public:
    void add(int priority, std::unique_ptr<Goal> goal);
    // A goal owned elsewhere (one the mob swaps in and out, like the skeleton's attacks).
    void addShared(int priority, Goal* goal);
    template <class G, class... Args>
    G& emplace(int priority, Args&&... args) {
        auto g = std::make_unique<G>(std::forward<Args>(args)...);
        G& ref = *g;
        add(priority, std::move(g));
        return ref;
    }
    // Stops and removes it (the skeleton swapping its bow goal for a sword goal).
    void remove(Level& level, const Goal* goal);
    // Vanilla GoalSelector.tick: stops goals that can't go on, starts those that can and
    // whose flags are free or held by worse goals, then ticks the running ones.
    void tick(Level& level);
    // Only the running goals' ticks (every one, or just those that need every tick).
    void tickRunning(Level& level, bool all);
    bool running(const Goal* goal) const;
    // Stops every running goal (death, despawn).
    void stopAll(Level& level);
    size_t size() const { return goals_.size(); }

private:
    struct Wrapped {
        int priority;
        std::unique_ptr<Goal> owned;
        Goal* goal = nullptr;
        bool running = false;
    };
    void stop(Level& level, Wrapped& w);
    std::vector<std::unique_ptr<Wrapped>> goals_;
    std::array<Wrapped*, 4> locked_{}; // per flag bit
};

} // namespace mc
