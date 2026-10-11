// JobSystem.h -- priority thread pool plus a main-thread callback queue.
//
// Workers never touch live world state. A job captures immutable snapshots,
// computes, then posts its result back with postMain(); the main thread
// integrates results in drainMain(). With zero workers (--threads 0) queued
// jobs run inline inside drainMain(), which keeps everything deterministic and
// single-threaded for debugging.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "core/UniqueFunction.h"

namespace mc {

class JobSystem {
public:
    using Job = UniqueFunction<void()>;

    // workers < 0 picks hardware_concurrency - 2 (at least 1).
    explicit JobSystem(int workers = -1);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Lower priority values run first; equal priorities run in submission order.
    void submit(int priority, Job job);

    // Thread-safe: queue a callback for the main thread.
    void postMain(Job job);

    // Main thread only. Runs queued main-thread callbacks until the budget is
    // spent (inline mode also runs worker jobs here). Returns callbacks run.
    size_t drainMain(double budgetMs = 1e30);

    // Blocks until no job is queued or running and all callbacks have drained.
    // Main thread only (it drains callbacks while waiting). Intended for tests and tools.
    void waitIdle();

    unsigned workerCount() const { return unsigned(threads_.size()); }
    size_t queuedJobs() const;

private:
    struct Entry {
        int priority;
        uint64_t seq;
        mutable Job job; // priority_queue only exposes const top()
        bool operator<(const Entry& o) const {
            // std::priority_queue is a max-heap: invert so low priority values pop first.
            return priority != o.priority ? priority > o.priority : seq > o.seq;
        }
    };

    void workerLoop(unsigned index);
    bool popJob(Job& out);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idleCv_;
    std::priority_queue<Entry> queue_;
    uint64_t nextSeq_ = 0;
    unsigned running_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> threads_;

    std::mutex mainMutex_;
    std::vector<Job> mainQueue_;
};

} // namespace mc
