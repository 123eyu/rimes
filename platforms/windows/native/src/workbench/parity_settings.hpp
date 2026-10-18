#pragma once
#include "../ui/settings_layout.hpp"
#include "provider.hpp"
#include "connector_ui.hpp"
#include <memory>

namespace rimes::windows::workbench {
// Real native settings content embedded in the shared macOS-shaped shell.
// The shell persists validated edits automatically.
class ParitySettings {
public:
  ParitySettings();
  ~ParitySettings();
  void Create(HWND parent, const Settings &settings, ConnectorUiServices services = {});
  void Layout(ui::SettingsPage page, int subpage, const ui::DipRect &body,
              unsigned dpi, HFONT font);
  void PaintFields(HDC dc, ui::ThemeId theme, unsigned dpi) const;
  bool Command(unsigned id, unsigned notification);
  bool Tick();
  bool DrawControl(const DRAWITEMSTRUCT&, ui::ThemeId, unsigned dpi, HFONT font) const;
  bool Scroll(UINT message, WPARAM value);
  bool Read(Settings *settings, std::string *error);
  const std::map<std::string, std::wstring> &Secrets() const;
  void Saved();
  std::vector<HWND> VisibleControls() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace rimes::windows::workbench
