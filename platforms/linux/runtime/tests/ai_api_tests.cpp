#include "ai_api.hpp"
#include <iostream>
#include <stdexcept>
using namespace rimes::runtime;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string base = argv[1];
    auto configuration = [&](const std::string& path) {
        return RequestConfiguration{"fixture", {}, {{"baseURL", base + path}, {"model", "fixture-model"}}, "fixture-credential"};
    };
    for (const auto& [path, expected] : std::vector<std::pair<std::string, AIError>>{
        {"/fragmented", AIError::None}, {"/auth", AIError::Authentication}, {"/limited", AIError::RateLimited},
        {"/redirect", AIError::Network}, {"/incomplete", AIError::Incomplete}, {"/malformed", AIError::Protocol},
        {"/tool", AIError::Protocol}, {"/oversized", AIError::TooLarge}, {"/provider", AIError::Protocol},
        {"/timeout", AIError::Timeout}, {"/cancel", AIError::Cancelled}}) {
        std::atomic<bool> cancelled{false};
        std::thread cancel;
        if (path == "/cancel") cancel = std::thread([&] { std::this_thread::sleep_for(std::chrono::milliseconds(40)); cancelled.store(true); });
        int updates = 0, completions = 0;
        AIUpdate final;
        const auto start = std::chrono::steady_clock::now();
        RunAIRequest("原文🙂", configuration(path), cancelled, [&](AIUpdate event) {
            if (event.finished) { final = event; ++completions; }
            else { Check(event.text.find("fixture-credential") == event.text.npos, "credential leaked in preview"); ++updates; }
        }, path == "/timeout" ? std::chrono::milliseconds(80) : std::chrono::seconds(3));
        if (cancel.joinable()) cancel.join();
        Check(completions == 1 && final.error == expected, ("unexpected completion for " + path).c_str());
        if (expected == AIError::None) {
            Check(updates > 0 && final.text == "First 🙂.第二段。" && final.blocks.size() == 2, "stream did not preserve logical output");
            Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "DONE waited for HTTP EOF");
        } else Check(final.text.empty() && final.blocks.empty(), "failed request became deliverable");
    }
    for (int scenario = 0; scenario < 3; ++scenario) {
        auto config = configuration("/trap");
        if (scenario == 0) config.credential = "bad\r\nInjected: header";
        if (scenario == 1) config.provider["baseURL"] = "http://remote.invalid/v1";
        if (scenario == 2) config.provider["model"] = "";
        std::atomic<bool> cancelled{false};
        AIUpdate result;
        RunAIRequest("原文🙂", config, cancelled, [&](AIUpdate event) { result = std::move(event); });
        Check(result.error == AIError::Configuration, "invalid configuration reached network");
    }
    {
        AIWorker worker;
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        std::atomic<unsigned> callbacks{0};
        worker.Run("原文🙂", configuration("/cancel"), cancelled, [&](AIUpdate) { ++callbacks; });
        worker.Shutdown();
        const auto final_callbacks = callbacks.load();
        worker.Shutdown(); // teardown is explicitly idempotent
        Check(cancelled->load() && final_callbacks == 1 && callbacks.load() == final_callbacks,
              "shutdown returned before cancellation callback finished");
    }
    std::cout << "AI HTTP: native contract, fragmented stream, early DONE, cancellation, timeout, auth, limits, redirect, size guards and joined shutdown passed\n";
}
