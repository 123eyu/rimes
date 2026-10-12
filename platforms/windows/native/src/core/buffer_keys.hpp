#pragma once

#include "broker_protocol.hpp"

namespace rimes::windows::core {
// Settings use Win32 MOD_* bits (Alt=1, Control=2, Shift=4, Win=8), not
// KeyModifiers wire bits. Keep the conversion shared by prediction/execution.
inline constexpr std::uint32_t ShortcutModifiers(std::uint32_t modifiers) noexcept {
  return ((modifiers & 1U) ? 4U : 0U) | (modifiers & 2U) |
         ((modifiers & 4U) ? 1U : 0U) | (modifiers & 8U);
}
inline constexpr bool ValidShortcut(std::uint32_t key, std::uint32_t modifiers,
                                     bool send = false) noexcept {
  const bool supported = (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') ||
      (key >= 0x70 && key <= 0x87) || key == 0x0d || key == 0x20 ||
      (key >= 0x21 && key <= 0x28) || key == 0x2e || key == 0x08 || key == 0x09 ||
      (key >= 0xba && key <= 0xc0) || (key >= 0xdb && key <= 0xde);
  if (!supported || (modifiers & ~15U)) return false;
  if (!(modifiers & 11U))
    return send && !modifiers && (key == 0x0d || (key >= 0x75 && key <= 0x7b));
  // Preserve editing and OS navigation; never turn a common editing gesture
  // or the secure-attention chord into a send/global action.
  if (modifiers == 2U && (key == 'A' || key == 'C' || key == 'V' ||
                         key == 'X' || key == 'Z' || key == 'Y')) return false;
  if ((modifiers & 1U) && (key == 0x09 || key == 0x73 || key == 0x20)) return false;
  if ((modifiers & 3U) == 3U && key == 0x2e) return false;
  return true;
}
// Lock keys do not change a shortcut; extra modifiers (including AltGr) do.
inline constexpr bool IsBufferSendChord(std::uint32_t key,
                                        std::uint32_t modifiers,
                                        std::uint32_t send_key = 0x0d,
                                        std::uint32_t send_modifiers = 5U) noexcept {
  constexpr auto locks = static_cast<std::uint32_t>(KeyModifiers::kCapsLock) |
                         static_cast<std::uint32_t>(KeyModifiers::kNumLock);
  return key == send_key && (modifiers & ~locks) == ShortcutModifiers(send_modifiers);
}
inline constexpr bool HasHostCommandModifier(std::uint32_t modifiers) noexcept {
  constexpr auto commands = static_cast<std::uint32_t>(KeyModifiers::kControl) |
      static_cast<std::uint32_t>(KeyModifiers::kAlt) |
      static_cast<std::uint32_t>(KeyModifiers::kWindows) |
      static_cast<std::uint32_t>(KeyModifiers::kAltGr);
  return (modifiers & commands) != 0;
}
}  // namespace rimes::windows::core
