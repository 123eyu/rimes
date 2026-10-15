#include "scheme_import.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <miniz.h>
#include <picosha2.h>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
#ifdef _WIN32
#include <Windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <glib.h>
#endif

namespace rimes::schemes {
namespace {
constexpr std::uint64_t kArchive = 256ULL << 20, kFile = 128ULL << 20,
                        kTotal = 512ULL << 20;
constexpr std::size_t kConfig = 512U << 10;
[[noreturn]] void Fail(const std::string &message) {
  throw std::runtime_error(message);
}
bool Ends(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool Starts(const std::string &s, const std::string &prefix) {
  return s.compare(0, prefix.size(), prefix) == 0;
}
std::string Lower(std::string s) {
  for (auto &c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}
std::vector<std::string> Split(const std::string &s, char delimiter) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i)
    if (i == s.size() || s[i] == delimiter) {
      out.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  return out;
}
std::string PathKey(const std::string &value) {
  if (value.find('\0') != std::string::npos)
    Fail("路径包含空字符");
#ifdef _WIN32
  int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                   static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0)
    Fail("文件名须为 UTF-8");
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), wide.data(), length);
  int count = NormalizeString(NormalizationC, wide.data(), length, nullptr, 0);
  if (count <= 0)
    Fail("无法规范化文件名");
  std::wstring composed(static_cast<std::size_t>(count), L'\0');
  count = NormalizeString(NormalizationC, wide.data(), length, composed.data(),
                          count);
  if (count <= 0)
    Fail("无法规范化文件名");
  int folded =
      LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, composed.data(),
                    count, nullptr, 0, nullptr, nullptr, 0);
  if (folded <= 0)
    Fail("无法比较文件名");
  std::wstring result(static_cast<std::size_t>(folded), L'\0');
  LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, composed.data(), count,
                result.data(), folded, nullptr, nullptr, 0);
  return std::string(reinterpret_cast<const char *>(result.data()),
                     result.size() * sizeof(wchar_t));
#elif defined(__APPLE__)
  auto string = CFStringCreateWithBytes(
      nullptr, reinterpret_cast<const UInt8 *>(value.data()),
      static_cast<CFIndex>(value.size()), kCFStringEncodingUTF8, false);
  if (!string)
    Fail("文件名须为 UTF-8");
  auto copy = CFStringCreateMutableCopy(nullptr, 0, string);
  CFRelease(string);
  CFStringNormalize(copy, kCFStringNormalizationFormC);
  CFStringLowercase(copy, nullptr);
  std::vector<char> bytes(
      static_cast<std::size_t>(CFStringGetMaximumSizeForEncoding(
          CFStringGetLength(copy), kCFStringEncodingUTF8)) +
      1);
  bool ok =
      CFStringGetCString(copy, bytes.data(), static_cast<CFIndex>(bytes.size()),
                         kCFStringEncodingUTF8);
  CFRelease(copy);
  if (!ok)
    Fail("无法比较文件名");
  return bytes.data();
#else
  if (!g_utf8_validate(value.data(), static_cast<gssize>(value.size()),
                       nullptr))
    Fail("文件名须为 UTF-8");
  char *normalized = g_utf8_normalize(
      value.c_str(), static_cast<gssize>(value.size()), G_NORMALIZE_NFC);
  char *folded = g_utf8_strdown(normalized, -1);
  g_free(normalized);
  std::string result(folded);
  g_free(folded);
  return result;
#endif
}
void SafePath(std::string path, bool directory = false) {
  if (directory && Ends(path, "/"))
    path.pop_back();
  if (path.empty() || path.size() > 1024 || path[0] == '/' ||
      path.find_first_of("\\:\0", 0, 3) != std::string::npos)
    Fail("不安全的 ZIP 路径：" + path);
  PathKey(path);
  for (const auto &part : Split(path, '/')) {
    if (part.empty() || part == "." || part == ".." || part.size() > 255 ||
        part.back() == '.' || part.back() == ' ' ||
        std::any_of(part.begin(), part.end(),
                    [](unsigned char c) { return c < 32 || c == 127; }))
      Fail("不安全的 ZIP 路径：" + path);
    auto device = Lower(part.substr(0, part.find('.')));
    if (device == "con" || device == "aux" || device == "nul" ||
        device == "prn" ||
        (device.size() == 4 &&
         (Starts(device, "com") || Starts(device, "lpt")) && device[3] >= '1' &&
         device[3] <= '9'))
      Fail("Windows 保留路径不能导入：" + path);
  }
}
bool ResourceID(const std::string &value) {
  if (value.empty() || value.size() > 256)
    return false;
  for (const auto &part : Split(value, '/'))
    if (!ValidID(part) || part == ".")
      return false;
  return true;
}
bool Ignored(const std::string &path) {
  for (auto part : Split(Lower(path), '/'))
    if (Starts(part, ".") || part == "__macosx" || part == "build" ||
        part == "sync" || part.find(".userdb") != std::string::npos)
      return true;
  return false;
}
bool Allowed(const std::string &path) {
  static const std::set<std::string> global = {
      "default.custom.yaml",  "installation.yaml",   "user.yaml",
      "hamster.yaml",         "hamster.custom.yaml", "weasel.yaml",
      "weasel.custom.yaml",   "squirrel.yaml",       "squirrel.custom.yaml",
      "trime.yaml",           "trime.custom.yaml",   "ibus_rime.yaml",
      "ibus_rime.custom.yaml"};
  auto lower = Lower(path);
  if (Ignored(path) || global.count(lower))
    return false;
  if (path.find('/') == std::string::npos)
    return Ends(lower, ".yaml") || Ends(lower, ".txt") || lower == "rime.lua";
  if (Starts(lower, "lua/"))
    return Ends(lower, ".lua") || Ends(lower, ".txt") || Ends(lower, ".json") ||
           Ends(lower, ".reverse.bin");
  if (Starts(lower, "opencc/"))
    return Ends(lower, ".json") || Ends(lower, ".txt") || Ends(lower, ".ocd") ||
           Ends(lower, ".ocd2") || Ends(lower, ".lua");
  return ResourceID(path) && (Ends(lower, ".dict.yaml") || Ends(lower, ".txt"));
}
struct Entry {
  std::string name;
  mz_uint index = 0;
  std::uint64_t size = 0;
  bool directory = false;
};
class Archive {
public:
  mz_zip_archive zip{};
  std::vector<Entry> entries;
  std::FILE *file = nullptr;
  explicit Archive(const fs::path &path) {
    if (!fs::is_regular_file(path) || fs::file_size(path) > kArchive)
      Fail("请选择不超过 256 MB 的 Rime ZIP");
#ifdef _WIN32
    file = _wfopen(path.c_str(), L"rb");
#else
    file = std::fopen(path.c_str(), "rb");
#endif
    if (!file)
      Fail("无法打开 ZIP");
    if (!mz_zip_reader_init_cfile(&zip, file, 0, 0)) {
      std::fclose(file);
      file = nullptr;
      Fail("ZIP 目录损坏或格式不支持");
    }
    try {
      if (zip.m_total_files > 4096)
        Fail("ZIP 超过 4096 个条目");
      std::set<std::string> seen;
      std::uint64_t total = 0;
      for (mz_uint i = 0; i < zip.m_total_files; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
          Fail("无法读取 ZIP 目录");
        auto count = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
        if (count < 2 || count > 1026)
          Fail("ZIP 文件名过长");
        std::vector<char> name(count);
        mz_zip_reader_get_filename(&zip, i, name.data(), count);
        Entry entry{std::string(name.data(), count - 1), i, stat.m_uncomp_size,
                    stat.m_is_directory != 0};
        SafePath(entry.name, entry.directory);
        auto key = entry.name;
        if (Ends(key, "/"))
          key.pop_back();
        if (!seen.insert(PathKey(key)).second)
          Fail("ZIP 路径重复：" + entry.name);
        if (stat.m_is_encrypted || !stat.m_is_supported ||
            ((stat.m_external_attr >> 16) & 0170000) == 0120000)
          Fail("不支持加密或符号链接 ZIP");
        if (entry.size > kFile ||
            entry.size > std::max<std::uint64_t>(1, stat.m_comp_size) * 100 ||
            total > kTotal - entry.size)
          Fail("ZIP 超过文件大小、总量或压缩比限制");
        total += entry.size;
        entries.push_back(entry);
      }
    } catch (...) {
      mz_zip_reader_end(&zip);
      std::fclose(file);
      file = nullptr;
      throw;
    }
  }
  ~Archive() {
    mz_zip_reader_end(&zip);
    if (file)
      std::fclose(file);
  }
  const Entry *Find(const std::string &path) const {
    for (const auto &entry : entries)
      if (entry.name == path)
        return &entry;
    return nullptr;
  }
  std::string Read(const Entry &entry, std::size_t limit = kConfig,
                   bool header = false) {
    struct State {
      std::string value;
      std::size_t limit;
      bool header, done = false, overflow = false;
    };
    State state{{}, limit, header};
    auto append = [](void *opaque, mz_uint64, const void *input,
                     size_t size) -> size_t {
      auto &s = *static_cast<State *>(opaque);
      auto bytes = static_cast<const char *>(input);
      for (size_t i = 0; i < size; ++i) {
        if (s.value.size() >= s.limit) {
          s.overflow = true;
          return 0;
        }
        s.value += bytes[i];
        if (s.header && bytes[i] == '\n' &&
            (Ends(s.value, "\n...\n") || Ends(s.value, "\n...\r\n"))) {
          s.done = true;
          return 0;
        }
      }
      return size;
    };
    bool ok = mz_zip_reader_extract_to_callback(&zip, entry.index, append,
                                                &state, 0) != 0;
    if (state.overflow)
      Fail("配置或词典头超过 512 KB：" + entry.name);
    if (!ok && !state.done)
      Fail("ZIP 内容或 CRC 损坏：" + entry.name);
    return state.value;
  }
  void Extract(const Entry &entry, const fs::path &target) {
    fs::create_directories(target.parent_path());
    std::ofstream out(target, std::ios::binary);
    if (!out)
      Fail("无法写入暂存文件");
    struct State {
      std::ofstream *out;
      std::uint64_t bytes = 0, limit;
    };
    State state{&out, 0, entry.size};
    auto append = [](void *opaque, mz_uint64, const void *data,
                     size_t size) -> size_t {
      auto &s = *static_cast<State *>(opaque);
      if (size > s.limit - s.bytes)
        return 0;
      s.out->write(static_cast<const char *>(data),
                   static_cast<std::streamsize>(size));
      s.bytes += size;
      return *s.out ? size : 0;
    };
    if (!mz_zip_reader_extract_to_callback(&zip, entry.index, append, &state,
                                           0) ||
        state.bytes != entry.size)
      Fail("资源展开或 CRC 校验失败：" + entry.name);
  }
};
std::string Scalar(const YAML::Node &node) {
  return node.IsScalar() ? node.Scalar() : "";
}
YAML::Node Member(const YAML::Node &node, const std::string &key) {
  if (!node.IsDefined() || !node.IsMap())
    return YAML::Node(YAML::NodeType::Null);
  auto value = node[key];
  return value.IsDefined() ? value : YAML::Node(YAML::NodeType::Null);
}
std::vector<std::string> Strings(const YAML::Node &node) {
  std::vector<std::string> out;
  if (node.IsSequence())
    for (const auto &value : node)
      out.push_back(Scalar(value));
  return out;
}
void Reference(std::string value) {
  if (Ends(value, "?"))
    value.pop_back();
  auto colon = value.find(':');
  auto file = colon == std::string::npos ? "" : value.substr(0, colon);
  if ((!file.empty() && !ResourceID(file)) ||
      value.find('\\') != std::string::npos)
    Fail("不安全的配置引用：" + value);
  for (const auto &part : Split(value, '/'))
    if (part == "..")
      Fail("不安全的配置引用：" + value);
}
void Validate(const YAML::Node &node, const std::string &path, int depth,
              std::size_t &remaining, bool duplicates = false) {
  if (depth > 64 || remaining-- == 0)
    Fail("YAML 层级或引用展开过多");
  if (node.IsMap()) {
    std::set<std::string> seen;
    for (const auto &item : node) {
      if (!item.first.IsScalar())
        Fail("YAML 键须为普通文本");
      auto key = item.first.Scalar(),
           leaf = key.substr(key.find_last_of('/') == std::string::npos
                                 ? 0
                                 : key.find_last_of('/') + 1);
      if (!seen.insert(key).second && !duplicates)
        Fail("YAML 配置键重复：" + key);
      auto value = Scalar(item.second);
      if (!value.empty()) {
        if ((leaf == "dictionary" || leaf == "user_dict" || leaf == "prism" ||
             leaf == "import_preset") &&
            !ResourceID(value))
          Fail("不安全的词典/配置引用：" + value);
        if (leaf == "__include" || leaf == "__patch")
          Reference(value);
        if (Starts(path, "opencc") && leaf == "file")
          SafePath(Starts(value, "./") ? value.substr(2) : value);
      }
      if (leaf == "__patch" && item.second.IsSequence())
        for (const auto &v : item.second)
          if (v.IsScalar())
            Reference(v.Scalar());
      if (leaf == "import_tables")
        for (const auto &v : Strings(item.second))
          if (!ResourceID(v))
            Fail("不安全的词典引用：" + v);
      Validate(item.second, path + "/" + key, depth + 1, remaining, duplicates);
    }
  } else if (node.IsSequence())
    for (const auto &value : node)
      Validate(value, path, depth + 1, remaining, duplicates);
}
YAML::Node Parse(const std::string &text, const std::string &path) {
  if (text.size() > kConfig)
    Fail("配置超过 512 KB：" + path);
  try {
    auto nodes = YAML::LoadAll(text);
    if (nodes.size() != 1 || !nodes[0].IsMap())
      Fail("配置须包含一个 YAML 对象：" + path);
    std::size_t budget = 40000;
    Validate(nodes[0], Starts(path, "opencc/") ? "opencc" : "", 0, budget,
             path == "symbols.yaml");
    return nodes[0];
  } catch (const YAML::Exception &e) {
    Fail("YAML 无法解析：" + path + "（" + e.what() + "）");
  }
}
std::string ReadFile(const fs::path &path, std::uintmax_t limit = kConfig) {
  if (!fs::is_regular_file(path) || fs::is_symlink(path) ||
      fs::file_size(path) > limit)
    Fail("文件缺失或超过大小限制");
  std::ifstream input(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(input)), {});
  if (!input.eof() && input.fail())
    Fail("无法读取文件");
  return text;
}
void Write(const fs::path &path, const std::string &text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.close();
  if (!out)
    Fail("无法保存文件");
}
std::string NewID() {
  std::random_device random;
  std::array<unsigned char, 32> data{};
  for (auto &byte : data)
    byte = static_cast<unsigned char>(random());
  return picosha2::hash256_hex_string(data).substr(0, 32);
}
bool PackageID(const std::string &value) {
  return value.size() == 32 &&
         value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
void AtomicWrite(const fs::path &path, const std::string &text) {
  auto temporary = path;
  temporary += "." + NewID() + ".tmp";
  try {
    Write(temporary, text);
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      Fail("无法原子保存方案选择");
#else
    fs::rename(temporary, path);
#endif
  } catch (...) {
    std::error_code error;
    fs::remove(temporary, error);
    throw;
  }
}
Package ReadPackage(const fs::path &path) {
  if (!PackageID(path.filename().string()) || fs::is_symlink(path))
    Fail("方案目录无效");
  auto data = Parse(ReadFile(path / "package.yaml"), "package.yaml");
  Package package{Scalar(data["id"]),
                  Scalar(data["name"]),
                  Scalar(data["source_digest"]),
                  {}};
  if (package.id != path.filename().string() || !data["schemes"].IsSequence())
    Fail("方案记录无效");
  for (const auto &value : data["schemes"]) {
    Scheme schema{Scalar(value["id"]), Scalar(value["name"]), {}, false};
    if (!ValidID(schema.id))
      Fail("方案记录无效");
    package.schemes.push_back(schema);
  }
  if (package.schemes.empty() || package.schemes.size() > 32)
    Fail("方案记录无效");
  return package;
}
YAML::Node PackageNode(const Package &package) {
  YAML::Node data;
  data["format"] = 1;
  data["id"] = package.id;
  data["name"] = package.name;
  data["source_digest"] = package.source_digest;
  for (const auto &schema : package.schemes) {
    YAML::Node item;
    item["id"] = schema.id;
    item["name"] = schema.name;
    data["schemes"].push_back(item);
  }
  return data;
}
bool Compiled(const fs::path &root, const std::string &schema) {
  const auto path = root / "build" / (schema + ".schema.yaml");
  return ValidID(schema) && !fs::is_symlink(root) &&
         !fs::is_symlink(root / "build") && !fs::is_symlink(path) &&
         fs::is_regular_file(path) && fs::file_size(path) > 0;
}
} // namespace

bool ValidID(const std::string &value) {
  return !value.empty() && value.size() <= 100 && value != "." &&
         value.find("..") == std::string::npos &&
         value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRS"
                                 "TUVWXYZ0123456789_.-") == std::string::npos;
}
std::string SHA256(const fs::path &file) {
  std::ifstream in(file, std::ios::binary);
  if (!in)
    Fail("无法读取文件指纹");
  picosha2::hash256_one_by_one hasher;
  std::array<char, 32768> block{};
  while (in) {
    in.read(block.data(), static_cast<std::streamsize>(block.size()));
    hasher.process(block.begin(), block.begin() + in.gcount());
  }
  if (!in.eof())
    Fail("读取文件指纹失败");
  hasher.finish();
  return picosha2::get_hash_hex_string(hasher);
}
std::vector<std::string>
Review::DeploymentOrder(const std::vector<std::string> &selected) const {
  if (selected.empty() || selected.size() > 32 ||
      std::set<std::string>(selected.begin(), selected.end()).size() !=
          selected.size())
    Fail("请选择不重复的输入方案");
  std::vector<std::string> order;
  std::set<std::string> visited;
  std::function<void(const std::string &)> visit = [&](const std::string &id) {
    if (!visited.insert(id).second)
      return;
    auto scheme =
        std::find_if(schemes.begin(), schemes.end(),
                     [&](const Scheme &value) { return value.id == id; });
    if (scheme == schemes.end())
      Fail("缺少依赖方案：" + id);
    for (const auto &dependency : scheme->dependencies)
      visit(dependency);
    order.push_back(id);
  };
  for (const auto &id : selected)
    visit(id);
  return order;
}
Review Inspect(const fs::path &path) {
  Archive archive(path);
  Review review;
  review.archive = path;
  review.source_name = path.stem().u8string();
  review.source_digest = SHA256(path);
  review.archive_digest = review.source_digest;
  std::set<std::string> parents;
  for (const auto &entry : archive.entries) {
    review.expanded_bytes += entry.size;
    if (!entry.directory && Ends(entry.name, ".schema.yaml") &&
        !Ignored(entry.name)) {
      auto slash = entry.name.find_last_of('/');
      parents.insert(slash == std::string::npos ? ""
                                                : entry.name.substr(0, slash));
    }
  }
  std::vector<std::string> roots;
  for (const auto &parent : parents)
    if (std::none_of(parents.begin(), parents.end(), [&](const auto &other) {
          return other != parent &&
                 (other.empty() || Starts(parent, other + "/"));
        }))
      roots.push_back(parent);
  if (roots.size() != 1)
    Fail(roots.empty() ? "ZIP 没有 Rime 源方案" : "ZIP 包含多套独立方案根目录");
  review.prefix = roots[0].empty() ? "" : roots[0] + "/";
  std::map<std::string, YAML::Node> configurations;
  for (const auto &entry : archive.entries)
    if (!entry.directory && Starts(entry.name, review.prefix)) {
      auto relative = entry.name.substr(review.prefix.size());
      if (!Allowed(relative))
        continue;
      review.resources.push_back(relative);
      if (Ends(relative, ".yaml") ||
          (Starts(relative, "opencc/") && Ends(relative, ".json"))) {
        bool dictionary = Ends(relative, ".dict.yaml");
        auto content = archive.Read(entry, kConfig, dictionary);
        if (dictionary) { // tab-only empty lines occur in exported Rime
                          // dictionary headers
          std::istringstream lines(content);
          std::string line, clean;
          while (std::getline(lines, line)) {
            if (line.find_first_not_of("\t\r ") == std::string::npos)
              line.clear();
            clean += line + '\n';
          }
          content = std::move(clean);
        }
        auto node = Parse(content, relative);
        if (dictionary) {
          if (!ResourceID(Scalar(node["name"])))
            Fail("词典 name 无效：" + relative);
        } else
          configurations[relative] = node;
      } else if (Ends(relative, ".lua")) {
        auto lua = archive.Read(entry, 1U << 20);
        if (!lua.empty())
          PathKey(lua);
      }
    }
  std::vector<std::string> preferred;
  auto schemaList = [&](const YAML::Node &node) {
    if (node.IsSequence())
      for (const auto &value : node)
        preferred.push_back(Scalar(Member(value, "schema")));
  };
  schemaList(Member(configurations["default.yaml"], "schema_list"));
  if (const auto *custom =
          archive.Find(review.prefix + "default.custom.yaml")) {
    auto patch =
        Member(Parse(archive.Read(*custom), "default.custom.yaml"), "patch");
    auto list = Member(patch, "schema_list");
    if (list.IsDefined() && !list.IsNull()) {
      preferred.clear();
      schemaList(list);
    }
  }
  auto manifest = Member(configurations["rimes-package.yaml"], "rimes_package");
  if (manifest.IsMap()) {
    if (Scalar(Member(manifest, "format")) != "1")
      Fail("方案包清单版本不受支持，请更新犀术IME");
    if (Member(manifest, "recommended_schemes").IsSequence())
      preferred = Strings(manifest["recommended_schemes"]);
  }
  for (const auto &[name, node] : configurations)
    if (Ends(name, ".schema.yaml") && name.find('/') == std::string::npos) {
      auto schema = Member(node, "schema");
      auto id = Scalar(Member(schema, "schema_id"));
      if (!ValidID(id) || name != id + ".schema.yaml")
        Fail("方案 ID 与文件名不一致：" + name);
      auto patch = Member(configurations[id + ".custom.yaml"], "patch");
      auto title = Member(patch, "schema/name");
      if (!title.IsScalar())
        title = Member(schema, "name");
      auto dependencies = Member(patch, "schema/dependencies");
      if (!dependencies.IsSequence())
        dependencies = Member(schema, "dependencies");
      auto overrideID = Scalar(Member(patch, "schema/schema_id"));
      if (!overrideID.empty() && overrideID != id)
        Fail("补丁不能修改方案 ID");
      review.schemes.push_back({id, Scalar(title).empty() ? id : Scalar(title),
                                Strings(dependencies),
                                std::find(preferred.begin(), preferred.end(),
                                          id) != preferred.end()});
    }
  if (review.schemes.empty() || review.schemes.size() > 32)
    Fail("一套方案须有 1–32 个方案文件");
  if (std::any_of(
          archive.entries.begin(), archive.entries.end(),
          [](const auto &entry) { return Ends(Lower(entry.name), ".gram"); }))
    Fail("这个 ZIP 包含 .gram "
         "语言模型。五端统一导入暂不支持模型，请选择上游提供的基础方案包。");
  review.warnings.push_back(
      "原 ZIP 不变；不导入全局偏好、其他输入法布局、编译缓存和用户数据库。");
  return review;
}
void Stage(const Review &review, const std::vector<std::string> &selected,
           const fs::path &destination) {
  review.DeploymentOrder(selected);
  if (SHA256(review.archive) != review.archive_digest)
    Fail("源文件在检查后发生变化，请重新检查");
  if (fs::exists(destination) || !fs::create_directories(destination))
    Fail("无法创建独立暂存目录");
  try {
    Archive archive(review.archive);
    for (const auto &path : review.resources) {
      SafePath(path);
      if (!Allowed(path) || path == "default.yaml")
        continue;
      const auto *entry = archive.Find(review.prefix + path);
      if (!entry)
        Fail("归档资源缺失");
      archive.Extract(*entry, destination / fs::u8path(path));
    }
    YAML::Node defaults;
    defaults["config_version"] = "1";
    for (const auto &id : selected) {
      YAML::Node schema;
      schema["schema"] = id;
      defaults["schema_list"].push_back(schema);
    }
    if (const auto *entry = archive.Find(review.prefix + "default.yaml")) {
      auto original = Parse(archive.Read(*entry), "default.yaml");
      for (const auto *key : {"key_binder", "ascii_composer", "recognizer",
                              "punctuator", "menu", "speller"})
        if (original[key])
          defaults[key] = original[key];
    }
    Write(destination / "default.yaml", YAML::Dump(defaults));
    if (SHA256(review.archive) != review.archive_digest)
      Fail("源文件在展开时发生变化");
  } catch (...) {
    std::error_code error;
    fs::remove_all(destination, error);
    throw;
  }
}
Review InspectText(const fs::path &text_file, const fs::path &generated_zip) {
  auto text = ReadFile(text_file, kFile);
  if (Starts(text, "\xef\xbb\xbf"))
    text.erase(0, 3);
  auto digest = SHA256(text_file), id = "text_" + digest.substr(0, 16);
  std::string rows;
  std::set<char> alphabet;
  std::size_t line_number = 0;
  // Normalize legacy Mac CR and Windows CRLF exactly as the mobile importers.
  std::string normalized;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      normalized += '\n';
      if (i + 1 < text.size() && text[i + 1] == '\n')
        ++i;
    } else
      normalized += text[i];
  }
  std::istringstream input(normalized);
  std::string line;
  while (std::getline(input, line)) {
    ++line_number;
    if (Ends(line, "\r"))
      line.pop_back();
    if (line.empty() || Starts(line, "#"))
      continue;
    auto columns = Split(line, '\t');
    bool valid = columns.size() >= 2 && columns.size() <= 3;
    if (valid)
      valid = !columns[0].empty() && columns[0].size() <= 4096 &&
              !columns[1].empty() && columns[1].size() <= 256 &&
              columns[1].find_first_not_of(
                  "abcdefghijklmnopqrstuvwxyz0123456789;'.,-/= ") ==
                  std::string::npos &&
              columns[1].find_first_not_of(' ') != std::string::npos &&
              std::none_of(columns[0].begin(), columns[0].end(),
                           [](unsigned char c) { return c < 32 || c == 127; });
    if (valid && columns.size() == 3)
      valid = !columns[2].empty() && columns[2].size() <= 10 &&
              columns[2].find_first_not_of("0123456789") == std::string::npos &&
              std::stoull(columns[2]) <= 4294967295ULL;
    if (!valid)
      Fail("第 " + std::to_string(line_number) +
           " 行格式不符：须提供词条、编码和可选整数权重，并用制表符分隔");
    PathKey(columns[0]);
    for (char c : columns[1])
      if (c != ' ')
        alphabet.insert(c);
    rows += line + '\n';
  }
  if (rows.empty())
    Fail("文本词库没有带编码的有效词条");
  YAML::Node schema;
  schema["schema"]["schema_id"] = id;
  schema["schema"]["name"] = text_file.stem().u8string();
  schema["schema"]["version"] = "1";
  for (const auto *processor :
       {"ascii_composer", "recognizer", "key_binder", "speller", "punctuator",
        "selector", "navigator", "express_editor"})
    schema["engine"]["processors"].push_back(processor);
  for (const auto *segmentor : {"ascii_segmentor", "abc_segmentor",
                                "punct_segmentor", "fallback_segmentor"})
    schema["engine"]["segmentors"].push_back(segmentor);
  schema["engine"]["translators"].push_back("punct_translator");
  schema["engine"]["translators"].push_back("table_translator");
  schema["speller"]["alphabet"] = std::string(alphabet.begin(), alphabet.end());
  schema["speller"]["delimiter"] = " ";
  schema["translator"]["dictionary"] = id;
  schema["translator"]["enable_sentence"] = false;
  schema["translator"]["enable_user_dict"] = true;
  schema["translator"]["enable_completion"] = true;
  schema["menu"]["page_size"] = 9;
  auto yaml = YAML::Dump(schema),
       dictionary =
           "---\nname: " + id + "\nversion: '1'\nsort: by_weight\n...\n" + rows;
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_heap(&zip, 0, 0))
    Fail("无法准备文本词库");
  void *data = nullptr;
  size_t size = 0;
  bool ok = mz_zip_writer_add_mem(&zip, (id + ".schema.yaml").c_str(),
                                  yaml.data(), yaml.size(), 0) &&
            mz_zip_writer_add_mem(&zip, (id + ".dict.yaml").c_str(),
                                  dictionary.data(), dictionary.size(), 0) &&
            mz_zip_writer_finalize_heap_archive(&zip, &data, &size);
  mz_zip_writer_end(&zip);
  if (!ok) {
    if (data)
      mz_free(data);
    Fail("无法准备文本词库");
  }
  try {
    Write(generated_zip, std::string(static_cast<char *>(data), size));
  } catch (...) {
    mz_free(data);
    throw;
  }
  mz_free(data);
  auto review = Inspect(generated_zip);
  review.source_digest = digest;
  review.source_name = text_file.stem().u8string();
  review.warnings.push_back(
      "已为带编码的文本词库生成独立码表方案；原文件未修改。");
  return review;
}
void CheckDeploymentLog(const fs::path &file) {
  if (!fs::exists(file))
    return;
  if (fs::file_size(file) > (8U << 20))
    Fail("方案产生过多错误日志，未启用该包");
  std::ifstream stream(file);
  std::string line;
  while (std::getline(stream, line)) {
    auto lower = Lower(line);
    const bool lua = lower.find("lua_gears") != std::string::npos ||
                     lower.find("types.cc:") != std::string::npos ||
                     lower.find("lua_translation") != std::string::npos;
    if ((lua && (lower.find("error") != std::string::npos ||
                 lower.find("failed") != std::string::npos)) ||
        lower.find("component not found") != std::string::npos ||
        lower.find("error creating ") != std::string::npos)
      Fail("方案使用的 Lua API 或组件不可用，请升级 Rime 运行库及 Lua "
           "模块；原方案未改变。\n" +
           line.substr(0, 800));
  }
}
std::vector<Package> Library::Packages() const {
  std::vector<Package> packages;
  std::error_code error;
  auto directory = root_ / "packages";
  if (fs::is_symlink(directory, error) || !fs::is_directory(directory, error))
    return packages;
  for (const auto &path : fs::directory_iterator(directory))
    try {
      packages.push_back(ReadPackage(path.path()));
    } catch (...) {
    }
  std::sort(packages.begin(), packages.end(),
            [](const auto &a, const auto &b) { return a.name < b.name; });
  return packages;
}
std::optional<Selection> Library::Active() const {
  try {
    auto node = Parse(ReadFile(root_ / "active.yaml", 4096), "active.yaml");
    auto id = Scalar(node["package"]), schema = Scalar(node["schema"]);
    if (!PackageID(id) || !ValidID(schema) ||
        fs::is_symlink(root_ / "packages"))
      return {};
    auto resources = root_ / "packages" / id;
    auto package = ReadPackage(resources);
    if (!Compiled(resources, schema) ||
        std::none_of(package.schemes.begin(), package.schemes.end(),
                     [&](const Scheme &s) { return s.id == schema; }))
      return {};
    return Selection{package, schema, resources, root_ / "learning" / id};
  } catch (...) {
    return {};
  }
}
Package Library::Install(const Review &review,
                         const std::vector<std::string> &selected,
                         const Deploy &deploy) {
  if (Packages().size() >= 32)
    Fail("最多保留 32 个方案包，请先删除不用的方案");
  auto id = NewID();
  auto staged = root_ / ".staging" / id;
  Stage(review, selected, staged);
  try {
    deploy(staged, review.DeploymentOrder(selected));
    std::uint64_t bytes = 0;
    std::vector<fs::path> discard;
    for (auto it = fs::recursive_directory_iterator(staged);
         it != fs::recursive_directory_iterator(); ++it) {
      if (it->is_symlink())
        Fail("编译结果包含链接");
      auto name = it->path().filename().string();
      if (Ends(name, ".userdb") || Ends(name, ".userdb.txt")) {
        discard.push_back(it->path());
        it.disable_recursion_pending();
        continue;
      }
      if (it->is_regular_file())
        bytes += it->file_size();
      if (bytes > kTotal)
        Fail("部署结果超过 512 MB");
    }
    for (const auto &path : discard)
      fs::remove_all(path);
    Package package{id, review.source_name, review.source_digest, {}};
    for (const auto &schema : review.schemes)
      if (std::find(selected.begin(), selected.end(), schema.id) !=
          selected.end()) {
        if (!Compiled(staged, schema.id))
          Fail("没有生成可用方案：" + schema.id);
        package.schemes.push_back(schema);
      }
    Write(staged / "package.yaml", YAML::Dump(PackageNode(package)));
    fs::create_directories(root_ / "packages");
    fs::rename(staged, root_ / "packages" / id);
    return package;
  } catch (...) {
    std::error_code error;
    fs::remove_all(staged, error);
    throw;
  }
}
void Library::Activate(const std::string &id, const std::string &schema) {
  if (!PackageID(id) || !ValidID(schema))
    Fail("方案标识无效");
  auto package = ReadPackage(root_ / "packages" / id);
  if (!Compiled(root_ / "packages" / id, schema) ||
      std::none_of(package.schemes.begin(), package.schemes.end(),
                   [&](const Scheme &s) { return s.id == schema; }))
    Fail("方案未部署完成");
  auto learning = root_ / "learning" / id;
  fs::create_directories(learning);
  if (!fs::exists(learning / "build")) {
    auto temporary = learning / (".build-" + NewID());
    try {
      fs::copy(root_ / "packages" / id / "build", temporary,
               fs::copy_options::recursive);
      fs::rename(temporary, learning / "build");
    } catch (...) {
      std::error_code error;
      fs::remove_all(temporary, error);
      throw;
    }
  }
  if (!Compiled(learning, schema))
    Fail("已部署缓存不完整，请重新导入该方案");
  YAML::Node node;
  node["package"] = id;
  node["schema"] = schema;
  AtomicWrite(root_ / "active.yaml", YAML::Dump(node));
}
void Library::Deactivate() {
  std::error_code error;
  if (!fs::remove(root_ / "active.yaml", error) && error)
    Fail("无法恢复内置方案：" + error.message());
}
void Library::Remove(const std::string &id) {
  if (!PackageID(id))
    Fail("方案标识无效");
  if (auto active = Active(); active && active->package.id == id)
    Fail("请先恢复内置方案并重启输入法，再删除正在使用的方案包");
  ReadPackage(root_ / "packages" / id);
  fs::remove_all(root_ / "packages" / id);
}
} // namespace rimes::schemes
