#pragma once
#include "chord_core.hpp"
#include "private_files.hpp"

namespace rimes::chord {
// Same opaque IDs and counters as macOS. No input text or application identity.
class ProgressStore {
  public:
    ProgressStore(const std::filesystem::path& root, const std::string& schema);
    Json Load() const;
    Json Record(const std::string& mapping, bool correct, double timestamp);
    Json Clear(double timestamp);

  private:
    runtime::PrivateDirectory files_;
    std::string schema_, filename_;
    void Validate(const Json& value) const;
    bool ValidID(const std::string& id) const;
    void Save(const Json& value) const;
};
Json CourseProgress(const Course& course, const Json& items);
} // namespace rimes::chord
