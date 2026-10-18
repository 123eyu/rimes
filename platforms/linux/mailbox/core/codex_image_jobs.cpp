#include "capsule_hash.hpp"
#include "capsule_text.hpp"
#include "codex_image_jobs.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace rimes::mailbox {
namespace {
struct FD {
    int value;
    ~FD() {
        if (value >= 0)
            close(value);
    }
};
template <class F> void RetryStorage(const F& action) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    for (;;) {
        try {
            action();
            return;
        } catch (const runtime::StorageBusy&) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
} // namespace
std::string ReadCodexImage(const std::filesystem::path& root, const std::string& path) {
    const auto images = root / "ai/codex-home/generated_images";
    const std::filesystem::path candidate(path);
    const auto relative = candidate.lexically_relative(images);
    if (!candidate.is_absolute() || candidate.lexically_normal() != candidate || relative.empty() ||
        relative.is_absolute() || candidate.extension() != ".png")
        throw std::runtime_error("Invalid image path");
    for (const auto& part : relative)
        if (part == ".." || part == ".")
            throw std::runtime_error("Invalid image path");
    // Walk every component with no-follow, including the generated root. The
    // server's savedPath is untrusted and can never read an arbitrary user file.
    FD directory{open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (directory.value < 0)
        throw std::runtime_error("Image directory unavailable");
    for (const auto& part : candidate.parent_path().relative_path()) {
        const auto next =
            openat(directory.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0)
            throw std::runtime_error("Unsafe image directory");
        close(directory.value);
        directory.value = next;
    }
    FD file{openat(directory.value, candidate.filename().c_str(),
                   O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC)};
    struct stat info {};
    if (file.value < 0 || fstat(file.value, &info) || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || info.st_nlink != 1 || info.st_size < 24 ||
        info.st_size > 24 * 1048576)
        throw std::runtime_error("Invalid generated image");
    std::string bytes;
    std::array<char, 65536> block{};
    for (;;) {
        const auto count = read(file.value, block.data(), block.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 || bytes.size() + static_cast<std::size_t>(count) > 24 * 1048576)
            throw std::runtime_error("Image read failed");
        if (!count)
            break;
        bytes.append(block.data(), static_cast<std::size_t>(count));
    }
    if (bytes.size() < 24 || bytes.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)))
        throw std::runtime_error("Invalid PNG");
    return bytes;
}
CodexImageJobs::~CodexImageJobs() { Shutdown(); }
void CodexImageJobs::Cancel() {
    std::lock_guard lock(mutex_);
    if (cancelled_)
        cancelled_->store(true);
}
void CodexImageJobs::Shutdown() {
    Cancel();
    workers_.Shutdown();
}
ImageState CodexImageJobs::State() const {
    std::lock_guard lock(mutex_);
    auto value = state_;
    const auto seconds = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::steady_clock::now() - started_)
                                            .count());
    if (value.phase == ImagePhase::Connecting)
        value.message = "正在向 ChatGPT 提交图片任务 · " + seconds + " 秒";
    else if (value.phase == ImagePhase::Generating)
        value.message =
            "任务已提交 · 生成中 " + seconds + " 秒。可以关闭 Buffer；完成后 Mailbox 会通知你。";
    else if (value.phase == ImagePhase::Saving)
        value.message = "图片已生成，正在保存到 Mailbox…";
    else if (value.phase == ImagePhase::Ready)
        value.message = "图片已生成，点击右侧按钮到 Mailbox 查看。";
    return value;
}
void CodexImageJobs::Update(ImageState state, const std::function<void()>& changed) {
    {
        std::lock_guard lock(mutex_);
        state_ = std::move(state);
    }
    if (changed) {
        try {
            changed();
        } catch (...) {
        }
    }
}
bool CodexImageJobs::Start(runtime::CodexRequest request, Commit authorize,
                           std::function<void()> changed) {
    if (!authorize || request.mode != runtime::CodexMode::Image || request.skill != "imagegen" ||
        capsule::TrimText(request.text).empty())
        return false;
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard lock(mutex_);
        if (state_.Running())
            return false;
        cancelled_ = cancelled;
        started_ = std::chrono::steady_clock::now();
        state_ = {ImagePhase::Connecting, "", "正在提交图片任务"};
    }
    const auto finish_cancelled = [this, changed] {
        Update({ImagePhase::Failed, "", "图片任务已取消"}, changed);
    };
    const bool accepted = workers_.Run(
        cancelled,
        [this, request = std::move(request), authorize, changed, cancelled] {
            const auto generation = capsule::NewUuidV4();
            std::string thread;
            try {
                runtime::PrivateDirectory owner(request.root / "mailbox/generations" / generation);
                const auto owned = owner.Lock(false);
                Commit gate = [authorize, cancelled](const auto& publish) {
                    authorize([&] {
                        if (!cancelled->load())
                            publish();
                    });
                };
                Store store(request.root / "mailbox", {}, gate);
                RetryStorage([&] {
                    const auto row = store.BeginGeneration(
                        {{"kind", "codexCLI"},
                         {"displayName", "ChatGPT"},
                         {"model", request.model},
                         {"replyCapability", "aiContinuation"}},
                        "图片 · " + capsule::CharacterPrefix(request.text, 60), request.text,
                        generation, "inbox");
                    thread = row.at("id");
                });
                Update({ImagePhase::Connecting, thread, "正在向 ChatGPT 提交图片任务"}, changed);
                runtime::CodexEvent final;
                runtime::RunCodex(request, *cancelled, [&](runtime::CodexEvent event) {
                    if (event.finished)
                        final = std::move(event);
                    else if (event.image_started)
                        Update({ImagePhase::Generating, thread, "图片生成中；可以关闭 Buffer"},
                               changed);
                });
                if (final.error != runtime::CodexError::None)
                    throw std::runtime_error(runtime::CodexErrorMessage(final.error));
                if (cancelled->load() || !final.image_started || final.image_path.empty())
                    throw std::runtime_error("图片任务已取消或未返回产物");
                Update({ImagePhase::Saving, thread, "正在保存图片到 Mailbox"}, changed);
                const auto bytes = ReadCodexImage(request.root, final.image_path);
                RetryStorage(
                    [&] { store.CompleteGeneration(thread, generation, "图片已生成。", bytes); });
                Update({ImagePhase::Ready, thread, "图片已存入 Mailbox"}, changed);
            } catch (const std::exception&) {
                // The durable generation owns its failure marker even when the
                // provider grant was revoked. No partial image/text is published.
                const std::string message =
                    cancelled->load() ? "图片任务已取消" : "图片生成未完成，请检查连接器后重试";
                if (!thread.empty()) {
                    try {
                        RetryStorage([&] {
                            Store(request.root / "mailbox")
                                .FailGeneration(thread, generation, message);
                        });
                    } catch (...) {
                    }
                }
                Update({ImagePhase::Failed, thread, message}, changed);
            }
        },
        finish_cancelled);
    if (!accepted) {
        Update({ImagePhase::Failed, "", "后台繁忙，请稍后重试"}, changed);
        return false;
    }
    if (changed) {
        try {
            changed();
        } catch (...) {
        }
    }
    return true;
}
} // namespace rimes::mailbox
