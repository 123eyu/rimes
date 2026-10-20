#include "rime_options.hpp"
#include <cstring>
#include <stdexcept>
#include <iostream>
namespace {
struct Config { std::map<std::string, std::string> fields; };
Config fixture;
Config next_fixture;
std::map<std::string, bool> applied;
int closed = 0;
bool Open(const char* schema, Config* out) {
  if (std::string(schema) == "fixture") *out = fixture;
  else if (std::string(schema) == "next") *out = next_fixture;
  else return false;
  return true;
}
bool Close(Config*) { ++closed; return true; }
bool Get(Config* config, const char* path, char* out, std::size_t capacity) {
  const auto found = config->fields.find(path);
  if (found == config->fields.end() || found->second.size() >= capacity) return false;
  std::memcpy(out, found->second.c_str(), found->second.size() + 1); return true;
}
void Set(int session, const char* name, int value) {
  if (session != 42) throw std::runtime_error("wrong session");
  applied[name] = value != 0;
}
bool GetOption(int session, const char* name) {
  if (session != 42) throw std::runtime_error("wrong session");
  return applied[name];
}
bool GetInt(Config* config, const char* path, int* value) {
  const auto found = config->fields.find(path);
  if (found == config->fields.end()) return false;
  *value = std::stoi(found->second); return true;
}
struct Api {
  decltype(&Open) schema_open = Open;
  decltype(&Close) config_close = Close;
  decltype(&Get) config_get_string = Get;
  decltype(&Set) set_option = Set;
  decltype(&GetOption) get_option = GetOption;
  decltype(&GetInt) config_get_int = GetInt;
};
void Check(bool valid) { if (!valid) throw std::runtime_error("input habits contract failed"); }
void Add(int index, const std::string& name) {
  const auto root = "switches/@" + std::to_string(index);
  fixture.fields[root + "/name"] = name;
  fixture.fields[root + "/states/@0"] = "关闭";
  fixture.fields[root + "/states/@1"] = "开启";
}
}
int main() {
  Api api;
  Add(0, "rimes_wubi_manual_commit"); Add(1, "_temporary");
  Add(2, "ascii_mode"); Add(3, "rimes_wubi_manual_commit");
  Add(4, "ternary"); fixture.fields["switches/@4/states/@2"] = "第三态";
  Add(5, "missing_label"); fixture.fields.erase("switches/@5/states/@1");
  Add(6, "rimes_wubi_fifth_code"); Add(7, "../unsafe");
  const auto options = xsime::input::Switches<Config>(&api, "fixture");
  Check(options.size() == 2 && closed == 1);
  Check(options[0].name == "rimes_wubi_manual_commit" && options[1].name == "rimes_wubi_fifth_code");
  Check(xsime::input::Switches<Config>(&api, "missing").empty() && closed == 1);
  xsime::input::Apply<Config>(&api, 42, "fixture", {{"rimes_wubi_manual_commit", true}, {"rimes_wubi_fifth_code", false}, {"ascii_mode", true}, {"unknown", true}});
  Check(applied.size() == 2 && applied.at("rimes_wubi_manual_commit") && !applied.at("rimes_wubi_fifth_code"));
  Check(xsime::input::Quote("a\n\"\\") == R"("a\u000a\"\\")");
  applied.clear();
  xsime::input::SessionOverrides session;
  session.Update<Config>(&api, 42, "fixture", {{"rimes_wubi_manual_commit", true}});
  Check(applied.at("rimes_wubi_manual_commit"));
  session.Update<Config>(&api, 42, "fixture", {});
  Check(!applied.at("rimes_wubi_manual_commit")); // reset to inherited value
  session.Update<Config>(&api, 42, "fixture", {{"rimes_wubi_manual_commit", true}});
  next_fixture = fixture;
  session.Update<Config>(&api, 42, "next", {});
  Check(!applied.at("rimes_wubi_manual_commit")); // native switch, no reset
  session.Update<Config>(&api, 42, "next", {{"rimes_wubi_fifth_code", true}});
  session.Restore(&api, 42);
  Check(!applied.at("rimes_wubi_fifth_code")); // host-controlled switch
  session = {};
  session.Update<Config>(&api, 42, "fixture", {{"rimes_wubi_manual_commit", true}});
  next_fixture.fields["switches/@0/reset"] = "1";
  session.Update<Config>(&api, 42, "next", {});
  Check(applied.at("rimes_wubi_manual_commit")); // explicit new reset wins
  next_fixture.fields["switches/@20/name"] = "hidden";
  next_fixture.fields["switches/@20/reset"] = "0";
  next_fixture.fields["switches/@21/options/@0"] = "group_a";
  next_fixture.fields["switches/@21/options/@1"] = "group_b";
  next_fixture.fields["switches/@21/reset"] = "1";
  const auto resets = xsime::input::ResetNames<Config>(&api, "next");
  Check(resets.count("hidden") && resets.count("group_a") && resets.count("group_b"));
  std::cout << "Scheme capability contracts passed\n";
}
