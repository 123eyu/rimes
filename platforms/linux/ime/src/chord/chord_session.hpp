#pragma once
#include "chord_core.hpp"
#include "engine/rime_engine.hpp"
#include <functional>

namespace rimes::linuxime {
chord::Context ChordContext(const EngineSnapshot& snapshot);
class ChordSession {
  public:
    // The owner supplies a fresh exact-focus/grant validator for every step.
    // No deferred event can mutate the engine first and check authority later.
    using Valid = std::function<bool()>;
    bool Stage(std::int32_t key, const chord::Profile& profile, const EngineSnapshot& before);
    std::optional<EngineSnapshot> Flush(RimeEngine& engine, RimeEngine::SessionId session,
                                        const chord::Profile& current, const Valid& valid);
    bool Pending() const { return batch_.Pending(); }
    std::string Keys() const { return batch_.Keys(); }
    void BreakPairing() { pairing_.Reset(); }
    void Cancel();
    std::size_t LastBatchCount() const { return last_batch_count_; }

  private:
    chord::Batch batch_;
    chord::Pairing pairing_;
    std::optional<chord::Profile> profile_;
    chord::Context base_;
    std::size_t last_batch_count_ = 0;
};
} // namespace rimes::linuxime
