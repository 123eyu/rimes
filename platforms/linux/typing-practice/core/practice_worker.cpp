#include "practice_worker.hpp"

namespace rimes::practice {
HistoryWorker::HistoryWorker(std::filesystem::path root) : thread_([this, root] { Loop(root); }) {}
HistoryWorker::~HistoryWorker() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    thread_.join();
}
bool HistoryWorker::Request(Json command, std::function<void(Json)> done) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (stopping_ || tasks_.size() >= 32)
            return false;
        tasks_.push_back({std::move(command), std::move(done)});
    }
    changed_.notify_one();
    return true;
}
void HistoryWorker::Loop(const std::filesystem::path &root) {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> guard(mutex_);
            changed_.wait(guard, [this] { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty())
                return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        Json reply;
        try {
            const std::string op = task.command.at("op");
            HistoryStore store(root, op != "status");
            std::string backup;
            if (op == "add")
                store.Add(task.command.at("result"));
            else if (op == "clear")
                store.Clear(task.command.at("revision"));
            else if (op == "repair")
                backup = store.Repair(task.command.at("revision"));
            else if (op != "status")
                throw std::runtime_error("未知成绩操作");
            reply = store.Snapshot();
            reply["ok"] = true;
            reply["backup"] = backup;
        } catch (const std::exception &error) {
            reply = {{"ok", false}, {"message", error.what()}};
        }
        if (task.done) {
            try {
                task.done(std::move(reply));
            } catch (...) {
            }
        }
    }
}
} // namespace rimes::practice
