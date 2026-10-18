#pragma once
#include "desktop.hpp"
#include <memory>
namespace rimes::desktop {
void StatisticsCall(const Json& command, GCancellable* cancel, std::function<void(Json)> done);
class StatisticsPane {
  public:
    StatisticsPane(GtkWindow* parent, std::string page, std::string theme, bool rendering,
                   const Json& fixture = Json::object(), std::function<void()> changed = {});
    ~StatisticsPane();
    GtkWidget* Widget() const;
    Json Geometry() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rimes::desktop
