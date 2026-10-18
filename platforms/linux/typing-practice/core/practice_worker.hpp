#pragma once
#include "practice_store.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace rimes::practice {
// Each accepted write drains before shutdown. No file I/O in GTK or key callbacks.
class HistoryWorker {
  public:
    explicit HistoryWorker(std::filesystem::path root);
    ~HistoryWorker();
    bool Request(Json command, std::function<void(Json)> done = {});

  private:
    struct Task {
        Json command;
        std::function<void(Json)> done;
    };
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Task> tasks_;
    bool stopping_ = false;
    std::thread thread_;
    void Loop(const std::filesystem::path &root);
};
} // namespace rimes::practice
