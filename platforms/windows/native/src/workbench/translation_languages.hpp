#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace rimes::windows::workbench {
struct TranslationLanguage {
  const char* id;
  const char* name;
  const wchar_t* title;
  const wchar_t* locale;
};
inline constexpr std::array kTranslationLanguages{
    TranslationLanguage{"zh", "Chinese", L"简体中文", L"zh-CN"},
    TranslationLanguage{"en", "English", L"英语", L"en-US"},
    TranslationLanguage{"ja", "Japanese", L"日语", L"ja-JP"},
    TranslationLanguage{"ko", "Korean", L"韩语", L"ko-KR"},
    TranslationLanguage{"fr", "French", L"法语", L"fr-FR"},
    TranslationLanguage{"de", "German", L"德语", L"de-DE"},
    TranslationLanguage{"es", "Spanish", L"西班牙语", L"es-ES"},
    TranslationLanguage{"it", "Italian", L"意大利语", L"it-IT"},
    TranslationLanguage{"pt", "Portuguese", L"葡萄牙语", L"pt-BR"},
    TranslationLanguage{"ru", "Russian", L"俄语", L"ru-RU"}};
inline std::string TranslationLanguageID(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  value = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
  if (value == "中文" || value == "简体中文" || value == "zh-cn" || value == "zh-hans") return "zh";
  if (value == "英语" || value == "英文") return "en";
  for (const auto& language : kTranslationLanguages) {
    auto name = std::string(language.name);
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == language.id || value == name || value.starts_with(std::string(language.id) + "-"))
      return language.id;
  }
  return value; // Keep custom AI languages in existing settings.
}
inline bool LocalTranslationLanguage(std::string_view id) { return id == "zh" || id == "en"; }
inline std::string OppositeTranslationLanguage(const std::string& value) {
  return TranslationLanguageID(value) == "zh" ? "English" : "Chinese";
}
inline std::string NormalizeTranslation(const std::string& output, const std::string& source,
                                        const std::string& language) {
  const auto first = output.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  auto text = output.substr(first, output.find_last_not_of(" \t\r\n") - first + 1);
  const auto start = source.find_first_not_of(" \t\r\n");
  if (start != std::string::npos) text.insert(0, source.substr(0, start));
  const auto end = source.find_last_not_of(" \t\r\n");
  if (end != std::string::npos && end + 1 < source.size()) text += source.substr(end + 1);
  else {
    const auto id = TranslationLanguageID(language);
    if (id != "zh" && id != "ja" && id != "th" && id != "lo" && id != "km" && id != "my") text += ' ';
  }
  return text;
}
}  // namespace rimes::windows::workbench
