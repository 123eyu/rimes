#pragma once
#include "desktop.hpp"
#include <atomic>
#include <memory>

namespace rimes::desktop {
struct PracticeConnection;
// Explicit practice only. No text/candidates or external Buffer lease.
class PracticeTelemetry {
  public:
    PracticeTelemetry();
    ~PracticeTelemetry();
    void Begin(std::function<void(Json)> done);
    void Read(bool finish, std::function<void(Json)> done);
    void End();
    bool Active() const { return !token_.empty(); }

  private:
    GCancellable *cancel_;
    std::shared_ptr<PracticeConnection> connection_;
    std::string token_;
    std::uint64_t generation_ = 0;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    void Call(Json command, std::function<void(Json)> done);
};
} // namespace rimes::desktop
