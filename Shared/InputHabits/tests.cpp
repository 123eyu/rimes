#include "rime_options.hpp"
#include <cstring>
#include <stdexcept>
#include <iostream>
namespace {
struct Config { std::map<std::string, std::string> fields; };
Config fixture;
std::map<std::string, bool> applied;
int closed = 0;
bool Open(const char* schema, Config* out) {
  if (std::string(schema) != "fixture") return false;
  *out = fixture; return true;
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
struct Api {
  decltype(&Open) schema_open = Open;
  decltype(&Close) config_close = Close;
  decltype(&Get) config_get_string = Get;
  decltype(&Set) set_option = Set;
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
  std::cout << "Scheme capability contracts passed\n";
}
