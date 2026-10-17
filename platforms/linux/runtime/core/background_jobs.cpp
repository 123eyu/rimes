#include "background_jobs.hpp"
#include <algorithm>

namespace rimes::runtime {
BackgroundJobs::~BackgroundJobs() { Shutdown(); }
bool BackgroundJobs::Run(std::shared_ptr<std::atomic<bool>> cancelled, std::function<void()> work) {
    std::lock_guard lock(mutex_);
    if (stopping_ || !cancelled || cancelled->load() || !work) return false;
    pending_.remove_if([](const auto& job) { return job.cancelled->load(); });
    const auto active = std::count_if(active_.begin(), active_.end(), [](const auto& flag) { return flag && !flag->load(); });
    if (pending_.size() + active >= kLimit) return false;
    workers_.reserve(kLimit);
    while (workers_.size() < kLimit) workers_.emplace_back([this, index = workers_.size()] { Loop(index); });
    pending_.push_back({std::move(cancelled), std::move(work)});
    changed_.notify_one();
    return true;
}
void BackgroundJobs::CancelAll() {
    std::lock_guard lock(mutex_);
    for (auto& job : pending_) job.cancelled->store(true);
    for (auto& flag : active_) if (flag) flag->store(true);
    pending_.clear();
}
void BackgroundJobs::Shutdown() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        for (auto& job : pending_) job.cancelled->store(true);
        for (auto& flag : active_) if (flag) flag->store(true);
        pending_.clear();
    }
    changed_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
    workers_.clear();
}
void BackgroundJobs::Loop(std::size_t index) {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_) return;
            job = std::move(pending_.front()); pending_.pop_front();
            if (job.cancelled->load()) continue;
            active_[index] = job.cancelled;
        }
        // A caller's completion must not terminate an input-method process or
        // consume a worker permanently if it unexpectedly throws.
        try { job.work(); } catch (...) {}
        { std::lock_guard lock(mutex_); active_[index].reset(); }
    }
}
}
