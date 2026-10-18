#include "metrics_worker.hpp"
#include <chrono>

namespace rimes::metrics {
Worker::Worker(std::filesystem::path root) : thread_([this, root] { Loop(root); }) {}
Worker::~Worker() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    thread_.join();
}
bool Worker::Enqueue(Task task) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (stopping_)
            return false;
        if (queue_.size() >= 8192) {
            if (task.kind != Task::Kind::Request) {
                ++dropped_;
                overflow_boundary_ = true;
            }
            return false;
        }
        queue_.push_back(std::move(task));
    }
    changed_.notify_one();
    return true;
}
void Worker::Observe(Event event, bool keys, bool activity) {
    if (!keys && !activity)
        return;
    Task task;
    task.kind = Task::Kind::Event;
    task.event = std::move(event);
    task.keys = keys;
    task.activity = activity;
    Enqueue(std::move(task));
}
void Worker::EndSession() { Enqueue(Task{}); }
bool Worker::Request(Json command, std::function<void(Json)> completion) {
    Task task;
    task.kind = Task::Kind::Request;
    task.command = std::move(command);
    task.completion = std::move(completion);
    return Enqueue(std::move(task));
}
void Worker::Loop(const std::filesystem::path& root) {
    std::unique_ptr<Store> store;
    std::string issue;
    try {
        store = std::make_unique<Store>(root, true);
    } catch (const std::exception& error) {
        issue = error.what();
    }
    std::uint64_t revision = 1;
    auto next_flush = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    for (;;) {
        std::deque<Task> batch;
        bool stopping, boundary;
        std::uint64_t dropped;
        {
            std::unique_lock<std::mutex> guard(mutex_);
            changed_.wait_until(guard, next_flush, [this] { return stopping_ || !queue_.empty(); });
            batch.swap(queue_);
            stopping = stopping_;
            boundary = overflow_boundary_;
            overflow_boundary_ = false;
            dropped = dropped_;
        }
        if (boundary && store) {
            store->EndSession();
            ++revision;
        }
        for (auto& task : batch) {
            Json response;
            try {
                if (!store)
                    throw std::runtime_error(issue);
                if (task.kind == Task::Kind::Event) {
                    store->Consume(task.event, task.keys, task.activity);
                    ++revision;
                } else if (task.kind == Task::Kind::Boundary) {
                    store->EndSession();
                    ++revision;
                } else {
                    if (!task.command.is_object())
                        throw std::runtime_error("无效的统计请求");
                    const auto op = task.command.at("op").get<std::string>();
                    std::string backup;
                    if (op == "clear") {
                        const auto day = task.command.contains("day")
                                             ? std::optional<std::string>(
                                                   task.command.at("day").get<std::string>())
                                             : std::nullopt;
                        store->Clear(task.command.at("collector"), day);
                        ++revision;
                    } else if (op == "repair") {
                        backup = store->Repair(task.command.at("collector"));
                        ++revision;
                    } else if (op == "flush") {
                        store->Flush();
                        ++revision;
                    } else if (op != "status")
                        throw std::runtime_error("未知统计操作");
                    response = {{"ok", true}, {"revision", revision}, {"dropped", dropped}};
                    if (op == "status" &&
                        task.command.value("knownRevision", std::uint64_t(0)) == revision)
                        response["unchanged"] = true;
                    else
                        response["snapshot"] = store->Snapshot();
                    if (!backup.empty())
                        response["backup"] = backup;
                }
            } catch (const std::exception& error) {
                response = {{"ok", false}, {"message", error.what()}};
            }
            if (task.completion) {
                try {
                    task.completion(std::move(response));
                } catch (...) {
                }
            }
        }
        if (store && (stopping || std::chrono::steady_clock::now() >= next_flush)) {
            if (store->Pending()) {
                store->Flush();
                // A save can transition either collector into read-only state.
                ++revision;
            }
            next_flush = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        } else if (!store)
            next_flush = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        if (stopping)
            break;
    }
}
} // namespace rimes::metrics
