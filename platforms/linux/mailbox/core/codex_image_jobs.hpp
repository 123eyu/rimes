#pragma once
#include "codex_channel.hpp"
#include "mailbox_store.hpp"
#include <mutex>
namespace rimes::mailbox {
enum class ImagePhase { Idle, Connecting, Generating, Saving, Ready, Failed };
struct ImageState {
    ImagePhase phase = ImagePhase::Idle;
    std::string thread, message;
    bool Running() const {
        return phase == ImagePhase::Connecting || phase == ImagePhase::Generating ||
               phase == ImagePhase::Saving;
    }
};
std::string ReadCodexImage(const std::filesystem::path& root, const std::string& path);
// Fcitx-process owned. Buffer close/capture/source edits do not own this token.
// Explicit stop, plugin revocation and host shutdown cancel the independent job.
class CodexImageJobs {
  public:
    ~CodexImageJobs();
    bool Start(runtime::CodexRequest request, Commit authorize, std::function<void()> changed);
    void Cancel();
    void Shutdown();
    ImageState State() const;

  private:
    void Update(ImageState state, const std::function<void()>& changed);
    mutable std::mutex mutex_;
    ImageState state_;
    std::chrono::steady_clock::time_point started_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    runtime::BackgroundJobs workers_;
};
} // namespace rimes::mailbox
