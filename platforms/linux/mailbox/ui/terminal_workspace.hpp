#pragma once
#include "terminal_session.hpp"
namespace rimes::mailbox {
class TerminalWorkspace {
  public:
    TerminalWorkspace(GtkWindow* parent, std::function<void(const std::string&)> open_receipt,
                      std::function<void()> changed);
    ~TerminalWorkspace();
    GtkWidget* Widget() const;
    std::string Launch(const std::string& kind, const std::filesystem::path& directory,
                       const std::filesystem::path& executable,
                       const std::vector<std::string>& arguments = {});
    bool Select(const std::string& session);
    void SetEnabled(bool enabled);
    void Suspend(); // Cancel pending UI actions, preserving the PTY processes.
    void Refresh(const Snapshot& snapshot);
    void Preview(const std::filesystem::path& path);
    void Collect();
    Json Inspect() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rimes::mailbox
