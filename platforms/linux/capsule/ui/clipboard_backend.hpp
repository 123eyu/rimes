#pragma once
#include "clipboard_desktop.hpp"
#include <mutex>
#include <optional>
#include <thread>

namespace rimes::capsule {
struct ClipboardBackendState {
    DesktopClipboard::Capture capture;
    DesktopClipboard::Status status;
    std::atomic<bool> stop{false}, allowed{false}, protected_input{true}, available{false};
    std::atomic<std::uint64_t> epoch{1}, write_epoch{1};
    std::mutex mutex;
    std::optional<ClipboardArchive> restore;
    std::function<void(bool)> restore_complete;
    std::uint64_t restore_epoch = 0, restore_write_epoch = 0;
    int wake = -1;
    bool Current(std::uint64_t value) const {
        return !stop.load() && !protected_input.load() && allowed.load() && epoch.load() == value;
    }
};
class ClipboardBackend {
  public:
    explicit ClipboardBackend(ClipboardBackendState& state) : state_(state) {}
    virtual ~ClipboardBackend() = default;
    virtual void Tick() = 0;
    virtual int SocketFd() const = 0;
    virtual bool Busy() const = 0;
    virtual bool Restore(ClipboardArchive archive) = 0;

  protected:
    ClipboardBackendState& state_;
};
std::unique_ptr<ClipboardBackend> X11Clipboard(ClipboardBackendState& state);
std::unique_ptr<ClipboardBackend> WaylandClipboard(ClipboardBackendState& state);
} // namespace rimes::capsule
