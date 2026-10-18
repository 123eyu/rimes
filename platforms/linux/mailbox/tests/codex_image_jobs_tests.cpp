#include "capsule_hash.hpp"
#include "codex_image_jobs.hpp"
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace rimes::runtime;
using namespace rimes::mailbox;
using namespace std::chrono_literals;
namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Wait(F condition) {
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    while (!condition()) {
        Check(std::chrono::steady_clock::now() < deadline, "image worker deadline");
        std::this_thread::sleep_for(10ms);
    }
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const auto root = std::filesystem::temp_directory_path() / ("rimes-image-jobs-" + RandomID());
    (void)PrivateDirectory(root);
    setenv("RIMES_USER_DIR", root.c_str(), 1);
    try {
        CodexRequest request;
        request.mode = CodexMode::Image;
        request.skill = "imagegen";
        request.root = root;
        request.executable = std::filesystem::absolute(argv[1]);
        request.model = "gpt-6-sol";
        request.effort = "high";
        request.text = "一张公开测试图片";
        request.timeout = 3s;
        PrivateDirectory home(root / "ai/codex-home");
        Store store(root / "mailbox");
        CodexImageJobs jobs;
        std::atomic<bool> grant{true};
        const Commit authorize = [&](const auto& publish) {
            if (grant.load())
                publish();
        };
        home.Write("fixture-mode", "image-hold");
        Check(jobs.Start(request, authorize, [] {}), "image job rejected");
        Wait([&] { return home.Read("image-started", 32).has_value(); });
        Check(!jobs.Start(request, authorize, [] {}), "second concurrent image accepted");
        const auto running = jobs.State();
        Check(running.Running() && !running.thread.empty(), "durable generation missing");
        Check(!store.RecoverInterrupted(), "opening Mailbox interrupted a live Fcitx image task");
        Check(store.Get(running.thread).at("generation").at("phase") == "generating",
              "live owner not retained");
        home.Write("release-image", "release");
        Wait([&] { return !jobs.State().Running(); });
        Check(jobs.State().phase == ImagePhase::Ready, "image not saved");
        const auto completed = store.Get(running.thread);
        Check(completed.at("unread") == true && Module(completed) == "inbox" &&
                  completed.at("messages").size() == 2,
              "image did not enter Mailbox inbox");
        const auto name = completed.at("messages").back().at("imageFileName").get<std::string>();
        const auto owned = store.Image(name);
        Check(owned.has_value(), "Mailbox image missing");
        std::filesystem::remove(root / "ai/codex-home/generated_images/public.png");
        Check(store.Image(name) == owned, "Mailbox retained provider-owned image path");
        bool stale = false;
        try {
            store.CompleteGeneration(running.thread, completed.at("generation").at("id"), "late");
        } catch (...) {
            stale = true;
        }
        Check(stale && store.Get(running.thread) == completed, "late image overwrote completion");

        home.Remove("image-started");
        home.Remove("release-image");
        Check(jobs.Start(request, authorize, [] {}), "second image could not start");
        Wait([&] { return home.Read("image-started", 32).has_value(); });
        const auto revoked = jobs.State().thread;
        grant.store(false);
        home.Write("release-image", "release");
        Wait([&] { return !jobs.State().Running(); });
        Check(jobs.State().phase == ImagePhase::Failed &&
                  store.Get(revoked).at("messages").size() == 1,
              "revoked result persisted");
        grant.store(true);
        home.Write("fixture-mode", "image-escape");
        Check(jobs.Start(request, authorize, [] {}), "escape test did not start");
        Wait([&] { return !jobs.State().Running(); });
        Check(jobs.State().phase == ImagePhase::Failed,
              "provider savedPath escaped private image root");
        for (const auto& bad :
             {root / "ai/codex-home/generated_images/../auth.json", root / "outside.png"}) {
            bool rejected = false;
            try {
                (void)ReadCodexImage(root, bad.string());
            } catch (...) {
                rejected = true;
            }
            Check(rejected, "invalid image path accepted");
        }
        const auto directory = root / "ai/codex-home/generated_images";
        std::filesystem::create_symlink(directory / "public.png", directory / "linked.png");
        bool rejected = false;
        try {
            (void)ReadCodexImage(root, (directory / "linked.png").string());
        } catch (...) {
            rejected = true;
        }
        Check(rejected, "generated image symlink followed");
        home.Write("fixture-mode", "image-hold");
        home.Remove("image-started");
        home.Remove("release-image");
        Check(jobs.Start(request, authorize, [] {}), "cancel image did not start");
        Wait([&] { return home.Read("image-started", 32).has_value(); });
        const auto cancelled = jobs.State().thread;
        jobs.Cancel();
        Wait([&] { return !jobs.State().Running(); });
        Check(jobs.State().phase == ImagePhase::Failed &&
                  store.Get(cancelled).at("messages").size() == 1,
              "cancelled image became an attachment");
        jobs.Shutdown();
        const auto abandoned = store.BeginGeneration({{"kind", "codexCLI"},
                                                      {"displayName", "ChatGPT"},
                                                      {"replyCapability", "aiContinuation"}},
                                                     "图片 · 已中断", "公开请求",
                                                     rimes::capsule::NewUuidV4(), "inbox");
        Check(store.RecoverInterrupted() &&
                  store.Get(abandoned.at("id")).at("generation").at("phase") == "failed",
              "unowned image generation not recovered");
        std::filesystem::remove_all(root);
        std::cout << "Mailbox-owned Codex images: live owner recovery, durable prompt/result, "
                     "independent PNG, stale completion, revoked grant, path confinement and "
                     "cancellation passed\n";
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
}
