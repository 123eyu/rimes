#pragma once
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "../core/control.hpp"

namespace rimes::windows::workbench {
struct PluginView {
  std::string id, name, version, grant;
  bool installed = false, enabled = false, bundled = false;
  std::string kind = "buffer";
  bool can_install = true;
};
// Host-owned verified package store. Only the Broker writes; private user data
// and credentials are never part of this directory or the uninstall operation.
class OfficialPluginStore {
 public:
  explicit OfficialPluginStore(std::filesystem::path root = {});
  std::vector<PluginView> Entries();
  std::string Grant(const std::string& id);
  std::string LoadBufferSelection();
  bool SaveBufferSelection(const std::string& id, std::string* error);
  core::Json Package(const std::string& id);
  bool Install(const std::string& id, std::string* error);
  bool InstallData(const std::string& id, const std::string& bytes,
                   const std::string& expected_grant, std::string* error);
  bool Enable(const std::string& id, bool enabled, std::string* error);
  bool Uninstall(const std::string& id, std::string* error);
  static std::string BundledData(const std::string& id);
 private:
  std::filesystem::path root_;
  core::Json catalog_;
  std::recursive_mutex mutex_;
  bool ready_ = false;
  const core::Json& Entry(const std::string& id) const;
  core::Json State(const std::string& id);
  std::filesystem::path Path(const std::string& name) const;
  core::Json Verified(const std::string& id, const std::string& bytes) const;
  void WriteState(const std::string& id, bool installed, bool enabled, bool bundled);
};
}  // namespace rimes::windows::workbench
