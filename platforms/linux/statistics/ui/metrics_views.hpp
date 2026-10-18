#pragma once
#include "desktop.hpp"
#include <optional>

namespace rimes::desktop {
struct MetricSample {
    std::string id, label, detail;
    std::optional<double> value;
};
const Json& MetricsContract();
class MetricsGraphic {
  public:
    enum class Kind { Card, Trend, Keyboard, Calendar };
    MetricsGraphic(Kind kind, std::string theme, int height);
    ~MetricsGraphic();
    GtkWidget* Widget() const { return widget_; }
    void Card(std::string title, std::string value, std::string unit, int icon);
    void Trend(std::vector<MetricSample> samples, std::string unit, std::string axis,
               std::string empty, std::string selected);
    void Keyboard(const Json& counts);
    void Calendar(const Json& days, const std::string& selected);
    std::function<void(const std::string&)> on_select;
    std::string Selection() const { return selected_; }

  private:
    Kind kind_;
    std::string theme_, title_, value_, unit_, axis_, empty_, selected_, hovered_;
    int icon_ = 0;
    GtkWidget* widget_;
    Json counts_ = Json::object();
    std::vector<MetricSample> samples_;
    std::vector<std::string> days_;
    int monday_ = 0;
    struct Hit {
        double x, y, w, h;
        std::string id, detail;
    };
    std::vector<Hit> hits_;
    void Draw(cairo_t* cr);
    void Select(const std::string& id);
    void Describe(const std::string& title, const std::string& detail);
};
} // namespace rimes::desktop
