#pragma once
#include "private_files.hpp"
#include "typing_practice_core.hpp"
#include <memory>

namespace rimes::practice {
// Short-lived transaction, called off the GTK/input threads. This lock is
// independent of passive metrics, which share the native stats directory.
class HistoryStore {
  public:
    explicit HistoryStore(const std::filesystem::path &root, bool writer = false);
    Json Snapshot() const;
    bool Add(const Json &result);
    void Clear(const std::string &expected_revision);
    std::string Repair(const std::string &expected_revision);
    static Json NormalizeFile(const Json &file);
    static std::string RetainedEncoding(Json results,
                                        std::size_t maximum_bytes = kMaximumHistoryBytes);

  private:
    runtime::PrivateDirectory files_;
    std::unique_ptr<runtime::FileLock> writer_;
    Json file_ = {{"version", 1}, {"results", Json::array()}};
    std::optional<std::string> bytes_;
    std::string issue_, revision_, identity_;
    void Writable(const std::string *expected_revision = nullptr) const;
    void Save(const std::string &bytes);
};
} // namespace rimes::practice
