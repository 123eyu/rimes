#pragma once
#include "mailbox_store.hpp"
#include <vte/vte.h>
namespace rimes::mailbox {
// Owned by the workspace, not by the currently mounted terminal widget.
class TerminalSession {
  public:
    TerminalSession(std::string kind, std::filesystem::path directory,
                    std::function<void()> changed);
    ~TerminalSession();
    void Start(const std::filesystem::path& executable, const std::vector<std::string>& arguments);
    void Stop();
    GtkWidget* Widget() const;
    Json Inspect() const;
    const std::string& ID() const;
    const std::filesystem::path& Directory() const;
    void SetInputEnabled(bool enabled);

  private:
    struct State;
    std::shared_ptr<State> state_;
};
std::filesystem::path TerminalExecutable(const std::string& kind);
} // namespace rimes::mailbox
