#pragma once
#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <thread>
namespace rimes::runtime {
class LocalTranslationWorker {
public:
    using Complete = std::function<void(std::string, bool)>;
    ~LocalTranslationWorker();
    void CancelAll();
    // Called on the owning event loop. Completion runs on a worker thread.
    void Run(std::string text, std::string source, std::string target,
             std::shared_ptr<std::atomic<bool>> cancelled, Complete complete);
private:
    struct Job { std::shared_ptr<std::atomic<bool>> cancelled, finished; std::thread thread; };
    std::list<Job> jobs_;
};
}
