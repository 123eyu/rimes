#pragma once
#include <cstdlib>
#include <string>

namespace rimes::branding {
inline const char* Name() {
  const char* locale = std::getenv("LC_ALL");
  if (!locale || !*locale) locale = std::getenv("LC_MESSAGES");
  if (!locale || !*locale) locale = std::getenv("LANG");
  const std::string language = locale ? locale : "C";
  if (language.rfind("zh", 0) != 0) return "XSIME";
  return language.find("TW") != std::string::npos || language.find("HK") != std::string::npos ||
         language.find("MO") != std::string::npos || language.find("Hant") != std::string::npos
      ? "犀術IME" : "犀术IME";
}
inline std::string Title(const char* suffix) { return std::string(Name()) + suffix; }
}
