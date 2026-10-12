#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rimes::schemes {
namespace fs = std::filesystem;
struct Scheme {
  std::string id, name;
  std::vector<std::string> dependencies;
  bool recommended = false;
};
struct Review {
  fs::path archive;
  std::string source_name, source_digest, archive_digest, prefix;
  std::vector<Scheme> schemes;
  std::vector<std::string> resources, warnings;
  std::uint64_t expanded_bytes = 0;
  std::vector<std::string>
  DeploymentOrder(const std::vector<std::string> &selected) const;
};
struct Package {
  std::string id, name, source_digest;
  std::vector<Scheme> schemes;
};
struct Selection {
  Package package;
  std::string schema;
  fs::path resources, user;
};
bool ValidID(const std::string &value);
// Reject native Lua/component load errors before publishing a package.
void CheckDeploymentLog(const fs::path &file);
std::string SHA256(const fs::path &file);
Review Inspect(const fs::path &archive);
Review InspectText(const fs::path &text, const fs::path &generated_zip);
void Stage(const Review &review, const std::vector<std::string> &selected,
           const fs::path &destination);
// The deploy callback uses an isolated engine process. It must finish and close
// all sessions before returning; no live input engine is reused for
// compilation.
using Deploy =
    std::function<void(const fs::path &, const std::vector<std::string> &)>;
class Library {
public:
  explicit Library(fs::path root) : root_(std::move(root)) {}
  const fs::path &Root() const { return root_; }
  std::vector<Package> Packages() const;
  std::optional<Selection> Active() const;
  Package Install(const Review &, const std::vector<std::string> &,
                  const Deploy &);
  void Activate(const std::string &package, const std::string &schema);
  void Deactivate();
  void Remove(const std::string &package);

private:
  fs::path root_;
};
} // namespace rimes::schemes
