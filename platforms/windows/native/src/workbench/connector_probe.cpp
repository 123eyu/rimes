// Explicit, opt-in setup and live verification. Never part of CTest; stdin,
// provider responses and credentials are never printed or written to logs.
#include "provider.hpp"
#include "codex_cli.hpp"
#include "official_plugins.hpp"
#include "official_features.hpp"
#include <iostream>

using namespace rimes::windows::workbench;
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  const std::wstring mode(argv[1]);
  Settings settings; std::string error;
  if (!LoadSettings(&settings, &error)) { std::cerr << "Settings unavailable\n"; return 1; }
  if (mode == L"--configure") {
    auto found = std::find_if(settings.connections.begin(), settings.connections.end(),
        [](const auto& p) { return p.id == "local-deepseek"; });
    ConnectionProfile profile{"local-deepseek", "DeepSeek", "openai-compatible", "https://api.deepseek.com", "deepseek-flash", ""};
    if (found == settings.connections.end()) settings.connections.push_back(profile); else *found = profile;
    auto codex = std::find_if(settings.connections.begin(), settings.connections.end(),
        [](const auto& p) { return p.kind == "codex-cli"; });
    if (codex == settings.connections.end()) settings.connections.push_back({"codex", "Codex", "codex-cli", "", "", ""});
    SelectConnection(&settings, "local-deepseek");
    if (!SaveSettings(settings, &error)) { std::cerr << "Could not save configuration\n"; return 1; }
    std::cout << "DeepSeek selected; independent Codex connector retained\n";
    return 0;
  }
  if (mode == L"--install-plugins") {
    OfficialPluginStore store;
    for (const auto* id : {rimes::windows::official::kAI, rimes::windows::official::kCodex}) {
      if (store.Grant(id).empty() && (!store.Install(id, &error) || !store.Enable(id, true, &error))) {
        std::cerr << "Plugin install/enable failed: " << error << '\n'; return 1;
      }
    }
    std::cout << "AI and ChatGPT plugins enabled\n"; return 0;
  }
  if (mode == L"--codex") { settings.ai_connector = "codex-cli"; settings.codex_path.clear(); settings.codex_model.clear(); }
  else if (mode == L"--deepseek") {
    settings.ai_connector = "openai-compatible"; settings.base_url = "https://api.deepseek.com";
    settings.active_connection = "local-deepseek"; settings.model = "deepseek-flash";
  } else return 2;
  Generation job; job.source = "Reply with exactly RIMES_CONNECTOR_OK";
  job.instruction = "Return only the requested text.";
  std::string answer;
  const bool ok = GenerateText(settings, job, [&](const auto& chunk) { answer += chunk; return true; }, [] { return false; }, &error);
  const bool pass = ok && answer.find("RIMES_CONNECTOR_OK") != std::string::npos;
  std::cout << (mode == L"--codex" ? "Codex" : "DeepSeek") << " synthetic generation: " << (pass ? "PASS" : "FAIL") << '\n';
  if (!pass) std::cerr << error << '\n';
  return pass ? 0 : 1;
}
