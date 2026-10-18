#pragma once
#include "panel_window.hpp"
#include <functional>
namespace rimes::mailbox {
// Same passive 292 x 48 completion receipt as InboundToast on macOS.
class CompletionToast {
  public:
    explicit CompletionToast(std::function<void(const std::string&)> open);
    ~CompletionToast();
    void Show(const std::string& thread, const std::string& label, const std::string& theme);
    void Hide();
    const std::string& Target() const { return target_; }

  private:
    GtkWidget *window_, *label_;
    GtkCssProvider* css_;
    std::unique_ptr<desktop::PanelWindow> panel_;
    std::function<void(const std::string&)> open_;
    std::string target_;
    guint timer_ = 0;
};
} // namespace rimes::mailbox
