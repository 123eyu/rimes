#pragma once
#include "background_jobs.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
namespace rimes::runtime {
class LocalTranslationWorker {
public:
    using Complete = std::function<void(std::string, bool)>;
    ~LocalTranslationWorker();
    void CancelAll();
    void Shutdown();
    // Called on the owning event loop. Completion of work that ran is on a
    // worker thread; a request rejected by Run, or discarded by CancelAll or
    // Shutdown before it ran, completes on the thread that made that call.
    void Run(std::string text, std::string source, std::string target,
             std::shared_ptr<std::atomic<bool>> cancelled, Complete complete);
private:
    BackgroundJobs jobs_;
};
}
