#include "provider.hpp"
#include <iostream>
using namespace rimes::windows::workbench;
int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 2;
  Settings settings; settings.target_language = Utf8(argv[1]);
  settings.ai_connector = "codex-cli"; // Offline translation must ignore AI credentials/routes.
  Generation job; job.translation = true; job.source = Utf8(argv[2]);
  std::string result, error;
  const bool ok = GenerateText(settings, job, [&](const auto& value) { result += value; return true; },
      [] { return false; }, &error);
  if (!ok) { std::cerr << error; return 1; }
  std::cout << result; return result.empty() ? 1 : 0;
}
