#pragma once
#include "chord_core.hpp"
#include <filesystem>
#include <map>

namespace rimes::chord {
// Only generated schema and schema-list files are owned by this transaction.
// Dictionaries, userdbs and draft profiles are never renamed or removed.
class DeploymentFiles {
  public:
    explicit DeploymentFiles(const std::filesystem::path& root);
    ~DeploymentFiles();
    DeploymentFiles(const DeploymentFiles&) = delete;
    struct File {
        std::optional<std::string> bytes;
        unsigned mode = 0600;
    };
    struct Snapshot {
        std::map<std::string, File> before, prepared;
    };
    Snapshot Prepare(const Profile& profile, bool enabled, const Json& default_schemas);
    void Restore(const Snapshot& snapshot);

  private:
    int fd_ = -1;
    std::filesystem::path root_;
    void Verify() const;
    File Read(const std::string& name) const;
    void Write(const std::string& name, const File& value, const File& expected) const;
};
} // namespace rimes::chord
