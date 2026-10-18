#pragma once
#include "chord_store.hpp"
#include "engine/rime_engine.hpp"
#include <memory>

namespace fcitx {
class RimesIme;
class ChordService {
  public:
    using Json = rimes::chord::Json;
    ChordService(RimesIme& ime, const rimes::linuxime::RimeEngineOptions& options, bool imported);
    ~ChordService();
    void Enable(bool enabled);
    void Configure(const Json& configuration);
    void EngineReady();
    bool Enabled() const;
    bool Busy() const;
    const rimes::chord::Profile& Profile() const;
    std::uint64_t Revision() const;
    std::uint64_t DurationMicros() const;
    std::string SelectedSchema() const;
    std::string FallbackSchema() const;
    void ObserveSchema(const std::string& schema);
    Json Command(const Json& request);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace fcitx
