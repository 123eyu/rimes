#include "provider.hpp"
#include "local_translation.hpp"
#include "translation_languages.hpp"
#include <iostream>
using namespace rimes::windows::workbench;
int wmain(int argc, wchar_t** argv) {
  if (argc < 3 || argc > 5) return 2;
  Settings settings; settings.target_language = Utf8(argv[1]);
  settings.source_language = argc >= 4 ? Utf8(argv[3]) : OppositeTranslationLanguage(settings.target_language);
  settings.ai_connector = "codex-cli"; // Offline translation must ignore AI credentials/routes.
  Generation job; job.translation = true; job.source = Utf8(argv[2]);
  std::string result, error;
  if (!PrepareLocalTranslation([] { return false; }, &error)) {
    std::cerr << error; return 1;
  }
  const int repeats = argc == 5 && std::wstring(argv[4]) == L"--benchmark" ? 3 : 1;
  for (int i = 0; i < repeats; ++i) {
  result.clear(); const auto start = GetTickCount64();
  const bool ok = GenerateText(settings, job, [&](const auto& value) { result += value; return true; },
      [] { return false; }, &error);
  if (!ok) { std::cerr << error; return 1; }
  if (repeats > 1) std::cout << "Request " << i + 1 << ": " << GetTickCount64() - start << " ms; ";
  std::cout << result << "\n";
  if (result.empty()) return 1;
  }
  return 0;
}
