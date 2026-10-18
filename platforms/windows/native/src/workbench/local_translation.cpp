#include "local_translation.hpp"
#include "codex_cli.hpp"
#include "translation_languages.hpp"
#include <filesystem>

namespace rimes::windows::workbench {
namespace {
std::filesystem::path Root() {
  wchar_t value[32768]{};
  const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", value, 32768);
  if (!length || length >= 32768) throw std::runtime_error("Local app data unavailable");
  return std::filesystem::path(value) / L"RIMES/translation/ct2-4.6-argos-1.9-v1";
}
std::filesystem::path Resources() {
  wchar_t value[32768]{};
  const auto length = GetModuleFileNameW(nullptr, value, 32768);
  if (!length || length >= 32768) throw std::runtime_error("Resource directory unavailable");
  return std::filesystem::path(value).parent_path() / L"translation";
}
}
bool LocalTranslationReady() {
  try { return std::filesystem::is_regular_file(Root() / L"ready.txt") &&
      std::filesystem::is_regular_file(Root() / L"python/python.exe") &&
      std::filesystem::is_regular_file(Root() / L"models/translate-zh_en-1_9/model/model.bin") &&
      std::filesystem::is_regular_file(Root() / L"models/translate-en_zh-1_9/model/model.bin"); }
  catch (...) { return false; }
}
bool PrepareLocalTranslation(const std::function<bool()>& cancelled, std::string* error) {
  try {
    const auto root = Root(), resources = Resources();
    if (!LocalTranslationReady()) {
      wchar_t system[MAX_PATH]{};
      if (!GetSystemDirectoryW(system, MAX_PATH)) return false;
      const auto shell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
      if (!RunLocalTextProcess(shell, {L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass",
          L"-File", (resources / L"Install-LocalTranslation.ps1").wstring(), L"-Root", root.wstring()},
          resources, {}, [](const auto&) { return true; }, cancelled, error, 600000) || !LocalTranslationReady()) {
        if (error) *error = "本地翻译资源下载失败，将在后台重试。请检查网络连接。";
        return false;
      }
    }
    return true;
  } catch (...) {
    if (error) *error = "本地翻译资源准备失败，将在后台重试。";
    return false;
  }
}
bool GenerateLocalTranslation(const Settings& settings, const Generation& job,
    const std::function<bool(const std::string&)>& chunk,
    const std::function<bool()>& cancelled, std::string* error) {
  try {
    const auto root = Root(), resources = Resources();
    // Never run installers or resource downloads as a text-generation job.
    if (!LocalTranslationReady()) {
      if (error) *error = "本地翻译暂不可用。";
      return false;
    }
    const auto input = core::Json{{"text", job.source}, {"source", TranslationLanguageID(settings.source_language)},
                                 {"target", TranslationLanguageID(settings.target_language)}}.dump();
    return RunWarmLocalTextProcess(root / L"python/python.exe",
        {L"-I", (resources / L"translate.py").wstring(), root.wstring(), L"--worker"}, root, input,
        [&](const std::string& output) {
          const auto response = core::Json::parse(output);
          if (response.contains("error")) { if (error) *error = response.at("error").get<std::string>(); return false; }
          const auto text = response.at("text").get<std::string>();
          if (text.empty() || text.size() > Model::kLimit || text.find('\0') != std::string::npos) return false;
          (void)Wide(text);
          return chunk(text);
        }, cancelled, error);
  } catch (...) {
    if (error) *error = "本地翻译资源不可用，原文已保留。";
    return false;
  }
}
}
