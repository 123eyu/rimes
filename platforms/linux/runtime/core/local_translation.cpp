#include "local_translation.hpp"
#include "configuration.hpp"
#include "process.hpp"
#include "translation_command.hpp"
namespace rimes::runtime {
LocalTranslationWorker::~LocalTranslationWorker() {
    CancelAll();
    for (auto& job : jobs_) if (job.thread.joinable()) job.thread.join();
}
void LocalTranslationWorker::CancelAll() { for (auto& job : jobs_) job.cancelled->store(true); }
void LocalTranslationWorker::Run(std::string text, std::string source, std::string target,
    std::shared_ptr<std::atomic<bool>> cancelled, Complete complete) {
    for (auto at = jobs_.begin(); at != jobs_.end();) {
        if (at->finished->load()) { at->thread.join(); at = jobs_.erase(at); }
        else ++at;
    }
    if (jobs_.size() >= 4) { complete("", false); return; }
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto work = [text = std::move(text), source = std::move(source), target = std::move(target), cancelled, complete = std::move(complete), finished] {
        try {
            const auto input = Json{{"op", "translate"}, {"text", text}, {"source", source}, {"target", target}}.dump();
            // Own Python directly: nesting the CLI helper would create another
            // process group and race the two cancellation grace periods.
            const auto result = RunProcess(TranslationCommand(), input, *cancelled, std::chrono::minutes(2));
            const auto response = result.success ? Json::parse(result.output) : Json::object();
            const bool valid = result.success && response.is_object() && response.size() == 1 && response.contains("text") && response["text"].is_string() && !cancelled->load();
            complete(valid ? response["text"].get<std::string>() : "", valid);
        } catch (const std::exception&) { complete("", false); }
        finished->store(true);
    };
    // Reserve ownership before starting so allocation failure cannot destroy a
    // joinable temporary thread. No worker accesses the owner object itself.
    jobs_.push_back({cancelled, finished, {}});
    try { jobs_.back().thread = std::thread(std::move(work)); }
    catch (...) { jobs_.pop_back(); throw; }
}
}
