#pragma once
#include "chord_core.hpp"
#include "private_files.hpp"
#include <filesystem>

namespace rimes::chord {
std::string NewProfileID();
struct ActiveProfile {
    Profile profile;
    std::string revision, error;
};
// Drafts never become effective by saving or restarting. Only the IME publishes
// an applied snapshot after real librime deployment and mapping verification.
class Store {
  public:
    explicit Store(const std::filesystem::path& root);
    ActiveProfile Active() const;
    std::vector<Profile> Profiles() const;
    Profile Draft(const std::string& id) const;
    std::string Revision(const std::string& id) const;
    void Save(const Profile& profile, const std::string& expected_revision);
    void Remove(const std::string& id, const std::string& expected_revision);
    Profile Import(std::string_view bytes) const;
    std::string Export(const Profile& profile) const;
    void Activate(const Profile& profile);
    Json State() const;
    void SaveState(const Json& state);

  private:
    runtime::PrivateDirectory files_;
    std::vector<std::string> Names() const;
    static std::string FileName(const std::string& id);
};
} // namespace rimes::chord
