#pragma once
#include "metrics_worker.hpp"
#include <fcitx/event.h>
#include <memory>

namespace fcitx {
class RimesIme;
std::string MetricsKeyID(const Key& key);
class MetricsService {
  public:
    explicit MetricsService(RimesIme& ime);
    ~MetricsService();
    void Enable(bool enabled);
    void Configure(const rimes::metrics::Json& value);
    void Suspend();
    bool EnabledFor(InputContext* ic) const;
    void KeyPress(KeyEvent& event);
    void Commit(InputContext* ic, std::int64_t count);
    void Chord(InputContext* ic, double duration, int releases);
    void Focus(InputContext* ic);
    void Forget(InputContext* ic);
    void EndSession();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace fcitx
