#pragma once
#include "practice_editor.hpp"
#include <memory>

namespace rimes::desktop {
class PracticePane {
  public:
    PracticePane(GtkWindow *parent, std::string page, std::string theme, bool rendering,
                 const Json &fixture = Json::object(), std::function<double()> clock = {},
                 std::function<Json()> context = {});
    ~PracticePane();
    GtkWidget *Widget() const;
    Json Geometry() const;
    Json Snapshot() const;
    PracticeEditor &Editor();
    // Interrupt and preserve aggregate scores before the settings shell detaches.
    bool CanLeave();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rimes::desktop
