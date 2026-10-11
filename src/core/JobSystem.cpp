#include "core/JobSystem.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <pthread.h>

#include "core/Log.h"

namespace mc {

JobSystem::JobSystem(int workers) {
    if (workers < 0) {
        const int hw = int(std::thread::hardware_concurrency());
        workers = std::max(1, hw - 2);
    }
    threads_.reserve(size_t(workers));
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this, i] { workerLoop(unsigned(i)); });
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        while (!queue_.empty()) queue_.pop(); // unstarted jobs are dropped
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
}

void JobSystem::submit(int priority, Job job) {
    {
        std::lock_guard lock(mutex_);
        queue_.push(Entry{priority, nextSeq_++, std::move(job)});
    }
    cv_.notify_one();
}

void JobSystem::postMain(Job job) {
    std::lock_guard lock(mainMutex_);
    mainQueue_.push_back(std::move(job));
}

bool JobSystem::popJob(Job& out) {
    if (queue_.empty()) return false;
    out = std::move(queue_.top().job);
    queue_.pop();
    return true;
}

void JobSystem::workerLoop(unsigned index) {
    const std::string name = std::format("worker-{}", index);
    log::setThreadName(name);
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            popJob(job);
            ++running_;
        }
        try {
            job();
        } catch (const std::exception& e) {
            log::error("job threw: {}", e.what());
        }
        job = nullptr; // destroy captures outside the lock
        {
            std::lock_guard lock(mutex_);
            --running_;
        }
        idleCv_.notify_all();
    }
}

size_t JobSystem::drainMain(double budgetMs) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    auto elapsedMs = [&] { return std::chrono::duration<double, std::milli>(clock::now() - start).count(); };

    if (threads_.empty()) {
        // Inline mode: run queued jobs here, each followed by any callbacks it produced.
        for (;;) {
            Job job;
            {
                std::lock_guard lock(mutex_);
                if (!popJob(job)) break;
            }
            job();
            if (elapsedMs() > budgetMs) break;
        }
    }

    size_t ran = 0;
    std::vector<Job> batch;
    while (elapsedMs() <= budgetMs) {
        {
            std::lock_guard lock(mainMutex_);
            if (mainQueue_.empty()) break;
            batch.swap(mainQueue_);
        }
        size_t i = 0;
        for (; i < batch.size(); ++i) {
            batch[i]();
            ++ran;
            if (elapsedMs() > budgetMs) {
                ++i;
                break;
            }
        }
        if (i < batch.size()) {
            // Budget exhausted: requeue the unrun tail in front of newer callbacks.
            std::lock_guard lock(mainMutex_);
            mainQueue_.insert(mainQueue_.begin(), std::make_move_iterator(batch.begin() + long(i)),
                              std::make_move_iterator(batch.end()));
        }
        batch.clear();
    }
    return ran;
}

void JobSystem::waitIdle() {
    for (;;) {
        drainMain();
        std::unique_lock lock(mutex_);
        if (queue_.empty() && running_ == 0) {
            lock.unlock();
            std::lock_guard mainLock(mainMutex_);
            if (mainQueue_.empty()) return;
            continue;
        }
        idleCv_.wait_for(lock, std::chrono::milliseconds(2));
    }
}

size_t JobSystem::queuedJobs() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
}

} // namespace mc
