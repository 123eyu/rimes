#include "scheme_import.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <miniz.h>
#include <stdexcept>
using namespace rimes::schemes;
namespace {
void Check(bool value, const char *label) {
  if (!value)
    throw std::runtime_error(label);
}
void Write(const fs::path &file, const std::string &text) {
  fs::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary);
  out << text;
}
void Zip(const fs::path &path,
         const std::map<std::string, std::string> &files) {
  mz_zip_archive archive{};
  Check(mz_zip_writer_init_heap(&archive, 0, 0) != 0, "zip init");
  std::vector<std::pair<std::string, std::string>> renames;
  for (const auto &[name, text] : files) {
    auto safe = name;
    for (auto &c : safe)
      if (c == ':' || c == '\\')
        c = '_';
    if (safe[0] == '/')
      safe[0] = '_';
    for (auto at = safe.find("../"); at != std::string::npos;
         at = safe.find("../"))
      safe.replace(at, 3, "__/");
    Check(mz_zip_writer_add_mem(&archive, safe.c_str(), text.data(),
                                text.size(), 0) != 0,
          "zip entry");
    if (safe != name)
      renames.emplace_back(safe, name);
  }
  void *data = nullptr;
  size_t size = 0;
  Check(mz_zip_writer_finalize_heap_archive(&archive, &data, &size) != 0,
        "zip finish");
  mz_zip_writer_end(&archive);
  std::string bytes(static_cast<char *>(data), size);
  mz_free(data);
  // Build hostile fixtures without asking the writer to emit unsafe names.
  for (const auto &[safe, name] : renames)
    for (auto at = bytes.find(safe); at != std::string::npos;
         at = bytes.find(safe, at + name.size()))
      bytes.replace(at, safe.size(), name);
  Write(path, bytes);
}
template <class F> void Reject(F operation) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception &) {
    rejected = true;
  }
  Check(rejected, "invalid input unexpectedly accepted");
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc >= 3 && std::string(argv[1]) == "inspect") {
      auto review = Inspect(fs::u8path(argv[2]));
      std::cout << "SHA256 " << review.source_digest << "\n";
      for (const auto &scheme : review.schemes)
        std::cout << scheme.id << "\t" << scheme.name << "\n";
      if (argc >= 5)
        Stage(review, {argv[4]}, fs::u8path(argv[3]));
      return 0;
    }
    auto root =
        fs::temp_directory_path() /
        ("rimes-scheme-test-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
      fs::path root;
      ~Cleanup() {
        std::error_code e;
        fs::remove_all(root, e);
      }
    } cleanup{root};
    fs::create_directories(root);
    {
      // Exercise a real >1 GiB ZIP without allocating the dictionary in memory.
      const std::uint64_t size = (1ULL << 30) + 4096;
      const auto large = root / "large.dict.yaml";
      {
        std::ofstream out(large, std::ios::binary);
        const std::string header = "---\nname: large\nversion: '1'\n...\nword\taa\n";
        out << header;
        const std::string block(65536, ' ');
        for (std::uint64_t written = header.size(); written < size;) {
          const auto count = std::min<std::uint64_t>(block.size(), size - written);
          out.write(block.data(), static_cast<std::streamsize>(count));
          written += count;
        }
        Check(out.good(), "large fixture write");
      }
      const auto zipPath = root / "large.zip";
      mz_zip_archive zip{};
      Check(mz_zip_writer_init_file(&zip, zipPath.string().c_str(), 0) != 0, "large ZIP init");
      const std::string schema = "schema: {schema_id: large, name: Large}\ntranslator: {dictionary: large}\n";
      Check(mz_zip_writer_add_mem(&zip, "large.schema.yaml", schema.data(), schema.size(), 0) != 0, "large schema");
      Check(mz_zip_writer_add_file(&zip, "large.dict.yaml", large.string().c_str(), nullptr, 0, 0) != 0, "stream large dictionary");
      Check(mz_zip_writer_finalize_archive(&zip) != 0, "large ZIP finalize");
      mz_zip_writer_end(&zip);
      fs::remove(large);
      Check(fs::file_size(zipPath) > (1ULL << 30), "fixture exceeds one GiB");
      const auto review = Inspect(zipPath);
      const auto destination = root / "large-stage";
      Stage(review, {"large"}, destination);
      Check(fs::file_size(destination / "large.dict.yaml") == size, "large file staged intact");
      fs::remove_all(destination);
      fs::remove(zipPath);
      Write(zipPath, "");
      fs::resize_file(zipPath, (2ULL << 30) + 1);
      Reject([&] { Inspect(zipPath); });
      fs::remove(zipPath);
    }
    const std::string schema = "schema:\n  schema_id: demo\n  name: "
                               "示例\ntranslator:\n  dictionary: dicts/demo\n";
    const std::string dictionary =
        "---\nname: demo\nversion: '1'\n...\n你好\tnihao\t100\n";
    const std::map<std::string, std::string> fixture = {
        {"上游-main/rime/demo.schema.yaml", schema},
        {"上游-main/rime/dicts/demo.dict.yaml", dictionary},
        {"上游-main/rime/default.custom.yaml",
         "patch:\n  schema_list:\n    - schema: demo\n"},
        {"上游-main/rime/lua/submodule/init.lua", "return {}"},
        {"__MACOSX/._demo.schema.yaml", "metadata"},
        {"上游-main/rime/build/cached.schema.yaml", "ignored"}};
    auto archive = root / "source.zip";
    Zip(archive, fixture);
    auto review = Inspect(archive);
    Check(review.schemes.size() == 1 && review.schemes[0].recommended,
          "original wrapper and recommendations");
    auto staged = root / "stage";
    Stage(review, {"demo"}, staged);
    Check(fs::is_regular_file(staged / "dicts/demo.dict.yaml") &&
              fs::is_regular_file(staged / "lua/submodule/init.lua"),
          "nested original resources preserved");
    Check(!fs::exists(staged / "default.custom.yaml") &&
              !fs::exists(staged / "build"),
          "preferences and build are isolated");
    Check(review.source_digest == SHA256(archive), "source immutable");
    Reject([&] { Stage(review, {"demo"}, staged); });
    Check(fs::is_regular_file(staged / "demo.schema.yaml"),
          "existing destination remains intact");
    for (const auto &name :
         std::vector<std::string>{"../escape.txt", "/absolute.txt",
                                  "C:/drive.txt", "one\\two.txt", "CON.txt"}) {
      auto files = fixture;
      files[name] = "x";
      Zip(root / "invalid.zip", files);
      Reject([&] { Inspect(root / "invalid.zip"); });
    }
    auto files = fixture;
    files["上游-main/rime/DEMO.SCHEMA.YAML"] = schema;
    Zip(root / "invalid.zip", files);
    Reject([&] { Inspect(root / "invalid.zip"); });
    files = fixture;
    files["résumé.txt"] = "a";
    files["re\xcc\x81sume\xcc\x81.txt"] = "b";
    Zip(root / "invalid.zip", files);
    Reject([&] { Inspect(root / "invalid.zip"); });
    files = fixture;
    files["independent/demo.schema.yaml"] = schema;
    Zip(root / "invalid.zip", files);
    Reject([&] { Inspect(root / "invalid.zip"); });
    files = fixture;
    files["上游-main/rime/rimes-package.yaml"] = "rimes_package: {format: 999}";
    Zip(root / "invalid.zip", files);
    Reject([&] { Inspect(root / "invalid.zip"); });
    files = fixture;
    files["上游-main/rime/demo.schema.yaml"] =
        schema + "punctuator: {__include: 'symbols.yaml:/punctuator'}\nother: "
                 "{__patch: ['/local?', 'missing:/optional?']}\n";
    Zip(root / "references.zip", files);
    Check(Inspect(root / "references.zip").schemes.size() == 1,
          "native optional references");
    Library library(root / "library");
    auto package = library.Install(
        review, {"demo"},
        [](const fs::path &path, const std::vector<std::string> &ids) {
          for (const auto &id : ids)
            Write(path / "build" / (id + ".schema.yaml"),
                  "synthetic compiled fixture");
        });
    Check(!library.Active() && library.Packages().size() == 1,
          "install does not activate");
    library.Activate(package.id, "demo");
    Check(library.Active() &&
              Library(library.Root()).Active()->schema == "demo",
          "selection survives cold reload");
    auto learning = library.Active()->user;
    Write(learning / "learned.txt", "keep");
    Reject([&] {
      library.Install(review, {"demo"}, [](const auto &, const auto &) {
        throw std::runtime_error("native deployment failed");
      });
    });
    Check(library.Active()->package.id == package.id &&
              library.Packages().size() == 1,
          "failed install leaves active package intact");
    Reject([&] { library.Remove(package.id); });
    library.Deactivate();
    library.Remove(package.id);
    Check(library.Packages().empty() && fs::exists(learning / "learned.txt"),
          "delete keeps learned data");
    auto text = root / "词库.tsv";
    Write(text, "\xef\xbb\xbf# public\n你好\tnihao\t100\n世界\tshijie\n");
    auto coded = InspectText(text, root / "generated.zip");
    Check(coded.source_digest == SHA256(text), "text source hash");
    Stage(coded, {coded.schemes[0].id}, root / "text-stage");
    for (const auto *bad : {"只有文字", "字\tabc\t-1", "字\tABC", "字\t拼音"}) {
      Write(root / "bad.txt", bad);
      Reject([&] { InspectText(root / "bad.txt", root / "bad.zip"); });
    }
    Write(text, "你好\tnihao\t100\r世界\tshijie\r");
    auto cr = InspectText(text, root / "cr.zip");
    Stage(cr, {cr.schemes[0].id}, root / "cr-stage");
    auto log = root / "deployment.log";
    Write(log, "E engine.cc:331] error creating processor: 'lua_processor'\n");
    Reject([&] { CheckDeploymentLog(log); });
    Write(log, "E lua_gears.cc:74] Lua error: missing context.spans\n");
    Reject([&] { CheckDeploymentLog(log); });
    Write(log, "I successfully loaded schema\n");
    CheckDeploymentLog(log);
    files = fixture;
    files["上游-main/rime/model.gram"] = "opaque model";
    Zip(root / "model.zip", files);
    Reject([&] { Inspect(root / "model.zip"); });
    Write(archive, "changed");
    Reject([&] { Stage(review, {"demo"}, root / "changed"); });
    Check(!fs::exists(root / "changed"),
          "changed input rejected before publication");
    std::cout
        << "PASS source ZIP, UTF-8 text, path/metadata bounds, immutable "
           "staging, atomic publication, selection, rollback and deletion\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << "\n";
    return 1;
  }
}
