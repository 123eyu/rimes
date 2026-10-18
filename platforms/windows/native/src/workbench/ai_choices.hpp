#pragma once
#include <array>
#include <string>
#include <string_view>

namespace rimes::windows::workbench {
struct AIModelChoice { const char* id; const wchar_t* title; };
// Same channel choices as macOS PluginConfigurationCatalog. Saved custom
// connector model IDs remain valid and appear as the current choice.
inline constexpr std::array<AIModelChoice, 6> kChatGPTModels = {{
    AIModelChoice{"gpt-6-astra", L"GPT-6 Astra"}, {"gpt-6-sol", L"GPT-6 Sol"},
    {"gpt-6-luna", L"GPT-6 Luna"}, {"gpt-5.6-sol", L"GPT-5.6 Sol"},
    {"gpt-5.6-terra", L"GPT-5.6 Terra"}, {"gpt-5.6-luna", L"GPT-5.6 Luna"}}};
inline constexpr std::array<std::string_view, 6> kChatGPTEfforts = {"low", "medium", "high", "xhigh", "max", "ultra"};
inline bool ValidAIModel(const std::string& model) {
  return !model.empty() && model.size() <= 256 &&
      model.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.:/") == std::string::npos;
}
inline bool ValidAIEffort(const std::string& model, std::string_view effort) {
  if (model.find("luna") != std::string::npos && effort == "ultra") return false;
  for (const auto value : kChatGPTEfforts) if (value == effort) return true;
  return false;
}
}
