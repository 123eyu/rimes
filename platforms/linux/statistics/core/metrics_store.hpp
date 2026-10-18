#pragma once
#include "metrics_core.hpp"
#include "private_files.hpp"
#include <memory>

namespace rimes::metrics {
// A writer owns the profile lock for its lifetime. UI readers never write the
// files. The worker serializes recording, clear/recovery and flush operations.
class Store {
  public:
    explicit Store(const std::filesystem::path& user_root, bool writer = false);
    void Consume(const Event& event, bool keys, bool activity);
    void EndSession();
    void Flush();
    bool Pending() const;
    Json Snapshot() const;
    void Clear(const std::string& collector, const std::optional<std::string>& day = {});
    std::string Repair(const std::string& collector);
    static double Now();

  private:
    runtime::PrivateDirectory files_;
    std::unique_ptr<runtime::FileLock> writer_;
    KeyFrequency keys_;
    TypingActivity activity_;
    struct File {
        std::string name, issue;
        std::optional<std::string> bytes;
        bool dirty = false;
    } keys_file_{"key_frequency.json", {}, {}, false},
        activity_file_{"typing_speed.json", {}, {}, false};
    void Save(File& file, Json value);
    File& Select(const std::string& collector);
};
} // namespace rimes::metrics
