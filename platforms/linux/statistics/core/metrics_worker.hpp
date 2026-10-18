#pragma once
#include "metrics_store.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace rimes::metrics {
// Key routing only enqueues value measurements. Loading, aggregation, locking,
// serialization and fsync all belong to this single bounded worker.
class Worker {
  public:
    explicit Worker(std::filesystem::path user_root);
    ~Worker();
    void Observe(Event event, bool keys, bool activity);
    void EndSession();
    // Completion is called on the worker. false means the bounded queue is busy.
    bool Request(Json command, std::function<void(Json)> completion);

  private:
    struct Task {
        enum class Kind { Event, Boundary, Request } kind = Kind::Boundary;
        Event event;
        bool keys = false, activity = false;
        Json command;
        std::function<void(Json)> completion;
    };
    bool Enqueue(Task task);
    void Loop(const std::filesystem::path& root);
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Task> queue_;
    bool stopping_ = false, overflow_boundary_ = false;
    std::uint64_t dropped_ = 0;
    std::thread thread_;
};
} // namespace rimes::metrics
