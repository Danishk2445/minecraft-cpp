#include "entity/ai/Goal.h"

#include <algorithm>

namespace mc {

void GoalSelector::add(int priority, std::unique_ptr<Goal> goal) {
    Goal* raw = goal.get();
    goals_.push_back(std::make_unique<Wrapped>(Wrapped{priority, std::move(goal), raw}));
}

void GoalSelector::addShared(int priority, Goal* goal) {
    goals_.push_back(std::make_unique<Wrapped>(Wrapped{priority, nullptr, goal}));
}

void GoalSelector::stop(Level& level, Wrapped& w) {
    if (!w.running) return;
    w.running = false;
    w.goal->stop(level);
}

void GoalSelector::remove(Level& level, const Goal* goal) {
    for (auto& w : goals_)
        if (w->goal == goal) stop(level, *w);
    for (Wrapped*& l : locked_)
        if (l && l->goal == goal) l = nullptr;
    std::erase_if(goals_, [&](const std::unique_ptr<Wrapped>& w) { return w->goal == goal; });
}

bool GoalSelector::running(const Goal* goal) const {
    for (const auto& w : goals_)
        if (w->goal == goal) return w->running;
    return false;
}

void GoalSelector::stopAll(Level& level) {
    for (auto& w : goals_) stop(level, *w);
    locked_.fill(nullptr);
}

void GoalSelector::tick(Level& level) {
    for (auto& w : goals_)
        if (w->running && !w->goal->canContinueToUse(level)) stop(level, *w);
    for (Wrapped*& l : locked_)
        if (l && !l->running) l = nullptr;
    for (auto& wp : goals_) {
        Wrapped& w = *wp;
        if (w.running) continue;
        // Every flag it needs is free, or held by an interruptible goal of a worse priority.
        bool free = true;
        for (int bit = 0; bit < 4; ++bit) {
            if (!(w.goal->flags & (1 << bit))) continue;
            const Wrapped* holder = locked_[size_t(bit)];
            if (holder && !(holder->goal->isInterruptable() && w.priority < holder->priority)) free = false;
        }
        if (!free || !w.goal->canUse(level)) continue;
        for (int bit = 0; bit < 4; ++bit) {
            if (!(w.goal->flags & (1 << bit))) continue;
            if (Wrapped* holder = locked_[size_t(bit)]) stop(level, *holder);
            locked_[size_t(bit)] = &w;
        }
        w.running = true;
        w.goal->start(level);
    }
    tickRunning(level, true);
}

void GoalSelector::tickRunning(Level& level, bool all) {
    for (auto& w : goals_)
        if (w->running && (all || w->goal->requiresUpdateEveryTick())) w->goal->tick(level);
}

} // namespace mc
