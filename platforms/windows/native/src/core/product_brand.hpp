#pragma once
#include <Windows.h>
#include <string>

namespace rimes::branding {
// Display only. No CLSID, registry ownership, protocol or filesystem changes.
inline const wchar_t* Name() {
  const LANGID language = GetUserDefaultUILanguage();
  if (PRIMARYLANGID(language) != LANG_CHINESE) return L"XSIME";
  const WORD sublanguage = SUBLANGID(language);
  return (sublanguage == SUBLANG_CHINESE_TRADITIONAL ||
          sublanguage == SUBLANG_CHINESE_HONGKONG ||
          sublanguage == SUBLANG_CHINESE_MACAU) ? L"犀術IME" : L"犀术IME";
}
inline std::wstring Text(std::wstring text) {
  const std::wstring name = Name();
  size_t position = 0;
  while ((position = text.find(L"{brand}", position)) != std::wstring::npos) {
    text.replace(position, 7, name);
    position += name.size();
  }
  return text;
}
}
