#pragma once

#include <Windows.h>
#include <array>
#include <string>
#include <cwctype>
#include "../core/buffer_keys.hpp"

namespace rimes::windows::workbench {
struct BufferHotkeyChoice {
  unsigned modifiers;
  const wchar_t* label;
};
inline constexpr std::array<BufferHotkeyChoice, 14> kBufferHotkeyChoices{{
    {MOD_CONTROL | MOD_SHIFT, L"Ctrl+Shift+"},
    {MOD_CONTROL | MOD_ALT, L"Ctrl+Alt+"},
    {MOD_ALT | MOD_SHIFT, L"Alt+Shift+"},
    {MOD_CONTROL, L"Ctrl+"}, {MOD_ALT, L"Alt+"},
    {MOD_CONTROL | MOD_ALT | MOD_SHIFT, L"Ctrl+Alt+Shift+"},
    {MOD_WIN, L"Win+"}, {MOD_WIN | MOD_SHIFT, L"Win+Shift+"},
    {MOD_WIN | MOD_CONTROL, L"Win+Ctrl+"}, {MOD_WIN | MOD_ALT, L"Win+Alt+"},
    {MOD_WIN | MOD_CONTROL | MOD_SHIFT, L"Win+Ctrl+Shift+"},
    {MOD_WIN | MOD_ALT | MOD_SHIFT, L"Win+Alt+Shift+"},
    {MOD_WIN | MOD_CONTROL | MOD_ALT, L"Win+Ctrl+Alt+"},
    {MOD_WIN | MOD_CONTROL | MOD_ALT | MOD_SHIFT, L"Win+Ctrl+Alt+Shift+"}}};

inline int BufferHotkeyChoiceIndex(unsigned modifiers) {
  for (std::size_t i = 0; i < kBufferHotkeyChoices.size(); ++i)
    if (kBufferHotkeyChoices[i].modifiers == modifiers) return static_cast<int>(i);
  return -1;
}
inline bool ValidBufferHotkeyModifiers(unsigned modifiers) {
  return BufferHotkeyChoiceIndex(modifiers) >= 0;
}
inline std::wstring ShortcutKeyTitle(unsigned key) {
  if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
    return std::wstring(1, static_cast<wchar_t>(key));
  if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
  switch (key) {
    case VK_RETURN: return L"Enter";
    case VK_TAB: return L"Tab";
    case VK_SPACE: return L"Space";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_UP: return L"Up"; case VK_DOWN: return L"Down";
    case VK_LEFT: return L"Left"; case VK_RIGHT: return L"Right";
    case VK_HOME: return L"Home"; case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp"; case VK_NEXT: return L"PageDown";
    default: return L"Key" + std::to_wstring(key);
  }
}
inline unsigned ParseShortcutKey(std::wstring text) {
  for (auto& c : text) c = static_cast<wchar_t>(towupper(c));
  for (unsigned key = 1; key < 256; ++key) {
    if (!core::ValidShortcut(key, MOD_CONTROL | MOD_SHIFT)) continue;
    auto title = ShortcutKeyTitle(key);
    for (auto& c : title) c = static_cast<wchar_t>(towupper(c));
    if (title == text) return key;
  }
  return 0;
}
inline std::wstring ShortcutTitle(unsigned key, unsigned modifiers) {
  const auto index = BufferHotkeyChoiceIndex(modifiers);
  return std::wstring(index >= 0 ? kBufferHotkeyChoices[static_cast<std::size_t>(index)].label : L"") +
      ShortcutKeyTitle(key);
}
}  // namespace rimes::windows::workbench
