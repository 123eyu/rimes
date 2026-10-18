#pragma once
#include "desktop.hpp"
#include <memory>

namespace rimes::desktop {
class ChordPane {
  public:
    ChordPane(GtkWindow* parent, std::string page, bool rendering,
              std::function<void()> configuration_changed = {},
              const Json& fixture_status = Json::object());
    ~ChordPane();
    GtkWidget* Widget() const;
    bool CanLeave();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
void ChordCall(const Json& command, GCancellable* cancel, std::function<void(Json)> completion);
} // namespace rimes::desktop
