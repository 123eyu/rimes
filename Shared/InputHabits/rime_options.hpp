// Copyright 2026 XSIME contributors. SPDX-License-Identifier: Apache-2.0
#pragma once
#include <map>
#include <string>
#include <vector>
#include <set>
#include <utility>

namespace xsime::input {
struct Switch {
  std::string name, off, on;
};
inline std::string Quote(const std::string& value) {
  std::string out = "\"";
  const char* hex = "0123456789abcdef";
  for (unsigned char ch : value) {
    if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
    else if (ch < 32) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
    else out += static_cast<char>(ch);
  }
  return out + "\"";
}
inline std::string JSON(const std::vector<Switch>& options) {
  std::string out = "[";
  for (const auto& option : options) {
    if (out.size() > 1) out += ',';
    out += "{\"name\":" + Quote(option.name) + ",\"off\":" + Quote(option.off) + ",\"on\":" + Quote(option.on) + "}";
  }
  return out + "]";
}
inline bool ValidName(const std::string& value) {
  return !value.empty() && value.size() <= 96 && value.front() != '_' &&
      value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string::npos;
}
inline bool ConfigurableName(const std::string& value) {
  // Language/script buttons already own these options on the native clients.
  // Avoid two settings that silently overwrite each other on keyboard activation.
  return ValidName(value) && value != "ascii_mode" && value != "simplification" && value != "traditional" && value != "traditionalization";
}
// Read only actual binary switches, never infer capabilities from a schema's
// filename, and never change the user's YAML or the bridge's ABI layout.
template<class Config, class Api>
std::vector<Switch> ReadSwitches(Api* api, Config& config) {
  std::vector<Switch> result;
  if (!api || !api->config_get_string) return result;
  std::set<std::string> names;
  auto text = [&](const std::string& key) {
    char value[512]{};
    return api->config_get_string(&config, key.c_str(), value, sizeof(value)) ? std::string(value) : std::string();
  };
  for (int i = 0; i < 128; ++i) {
    const auto path = "switches/@" + std::to_string(i);
    Switch item{text(path + "/name"), text(path + "/states/@0"), text(path + "/states/@1")};
    if (ConfigurableName(item.name) && !item.off.empty() && !item.on.empty() && text(path + "/states/@2").empty() && names.insert(item.name).second)
      result.push_back(std::move(item));
  }
  return result;
}
template<class Config, class Api>
std::vector<Switch> Switches(Api* api, const std::string& schema) {
  if (!api || !api->schema_open || !api->config_close) return {};
  Config config{};
  if (!api->schema_open(schema.c_str(), &config)) return {};
  try {
    auto result = ReadSwitches(api, config);
    api->config_close(&config);
    return result;
  } catch (...) { api->config_close(&config); throw; }
}
// Rime also allows hidden switches and multi-option groups. Their explicit
// reset values still take precedence over an override from the previous scheme.
template<class Config, class Api>
std::set<std::string> ResetNames(Api* api, const std::string& schema) {
  std::set<std::string> names;
  if (!api || !api->schema_open || !api->config_close || !api->config_get_int || !api->config_get_string) return names;
  Config config{};
  if (!api->schema_open(schema.c_str(), &config)) return names;
  try {
    for (int i = 0; i < 128; ++i) {
      const auto path = "switches/@" + std::to_string(i);
      int reset = 0;
      if (!api->config_get_int(&config, (path + "/reset").c_str(), &reset) || reset < 0) continue;
      char name[512]{};
      if (api->config_get_string(&config, (path + "/name").c_str(), name, sizeof(name))) names.insert(name);
      for (int j = 0; j < 128; ++j) {
        if (!api->config_get_string(&config, (path + "/options/@" + std::to_string(j)).c_str(), name, sizeof(name))) break;
        names.insert(name);
      }
    }
    api->config_close(&config);
    return names;
  } catch (...) { api->config_close(&config); throw; }
}
// A scheme can omit reset, in which case librime carries the option into the
// next scheme. Remember only overrides we own and restore them before switching.
// Call with an empty composition and under the client's engine lock/queue.
struct SessionOverrides {
  std::string schema;
  std::map<std::string, bool> baseline;
  std::set<std::string> applied;
  template<class Api, class Session> void Restore(Api* api, Session session) {
    for (const auto& name : applied) api->set_option(session, name.c_str(), baseline.at(name) ? 1 : 0);
    applied.clear();
  }
  template<class Config, class Api, class Session>
  void Update(Api* api, Session session, const std::string& current,
              const std::map<std::string, bool>& values) {
    if (schema != current) {
      // Native Rime switcher selections bypass the host's SelectSchema method.
      // Undo carried values, preserving resets already applied by the new schema.
      const auto resets = ResetNames<Config>(api, current);
      for (const auto& name : applied)
        if (!resets.count(name)) api->set_option(session, name.c_str(), baseline.at(name) ? 1 : 0);
      schema = current; baseline.clear(); applied.clear();
      for (const auto& option : Switches<Config>(api, current)) baseline[option.name] = api->get_option(session, option.name.c_str()) != 0;
    }
    for (const auto& [name, original] : baseline) {
      const auto found = values.find(name);
      if (found != values.end()) { api->set_option(session, name.c_str(), found->second ? 1 : 0); applied.insert(name); }
      else if (applied.erase(name)) api->set_option(session, name.c_str(), original ? 1 : 0);
    }
  }
};
template<class Config, class Api, class Session>
void Apply(Api* api, Session session, const std::string& schema,
           const std::map<std::string, bool>& overrides) {
  if (overrides.empty() || !api || !api->set_option) return;
  for (const auto& item : Switches<Config>(api, schema)) {
    const auto found = overrides.find(item.name);
    if (found != overrides.end()) api->set_option(session, item.name.c_str(), found->second ? 1 : 0);
  }
}
} // namespace xsime::input
