#include "practice_store.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace rimes::practice;
namespace fs = std::filesystem;
namespace {
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string Read(const fs::path &path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), {}};
}
template <class F> void Reject(F f, const char *message) {
    bool blocked = false;
    try {
        f();
    } catch (...) {
        blocked = true;
    }
    Check(blocked, message);
}
} // namespace
int main(int argc, char **argv) {
    char pattern[] = "/tmp/rimes-practice-store-XXXXXX";
    const fs::path root = mkdtemp(pattern);
    struct Cleanup {
        fs::path root;
        ~Cleanup() { fs::remove_all(root); }
    } cleanup{root};
    try {
        Check(argc == 2, "fixture required");
        auto fixture = Json::parse(Read(argv[1]));
        auto result = fixture.at("validation")[0].at("value");
        result["text"] = "never store editor contents";
        result["context"]["application"] = "never store app identity";
        rimes::runtime::PrivateDirectory files(root / "stats");
        auto passive_lock = files.Lock(false); // Passive telemetry owns this unrelated lock.
        files.Write("key_frequency.json", "keep keys");
        files.Write("typing_speed.json", "keep daily activity");
        {
            HistoryStore writer(root, true);
            Check(writer.Add(result), "first result saved");
            Check(!writer.Add(result), "retry is idempotent");
            Reject([&] { HistoryStore second(root, true); }, "one practice writer");
            Check(HistoryStore(root).Snapshot().at("file").at("results").size() == 1,
                  "concurrent reader");
        }
        auto snapshot = HistoryStore(root).Snapshot();
        Check(snapshot.at("issue") == "" && snapshot.at("file").at("results").size() == 1,
              "restart preserved");
        Check(Read(root / "stats/typing_tests.json").find("never store") == std::string::npos,
              "unknown payload stripped");
        struct stat mode {};
        Check(!stat((root / "stats/typing_tests.json").c_str(), &mode) &&
                  (mode.st_mode & 0777) == 0600,
              "private result file");
        Check(!stat((root / "stats").c_str(), &mode) && (mode.st_mode & 0777) == 0700,
              "private directory");
        {
            HistoryStore writer(root, true);
            Reject([&] { writer.Clear("old-revision"); }, "stale clear rejected");
            files.Write("typing_tests.json", "external edit");
            result["id"] = "00000000-0000-4000-8000-000000000002";
            Reject([&] { writer.Add(result); }, "external edits preserved");
            Check(Read(root / "stats/typing_tests.json") == "external edit",
                  "did not overwrite external edit");
        }
        {
            HistoryStore writer(root, true);
            auto current = writer.Snapshot();
            Check(current.at("issue") != "", "corruption read-only");
            Reject([&] { writer.Add(result); }, "corruption cannot collect");
            const auto backup = writer.Repair(current.at("revision"));
            Check(Read(root / "stats" / backup) == "external edit", "exact backup");
            Check(writer.Add(result), "repair restores explicit result save");
            writer.Clear(writer.Snapshot().at("revision"));
        }
        Check(Read(root / "stats/key_frequency.json") == "keep keys" &&
                  Read(root / "stats/typing_speed.json") == "keep daily activity",
              "practice clear isolated");
        files.Remove("typing_tests.json");
        std::ofstream(root / "outside") << "private outside";
        fs::create_symlink(root / "outside", root / "stats/typing_tests.json");
        {
            HistoryStore writer(root, true);
            Check(writer.Snapshot().at("issue") != "", "symlink blocked");
            auto backup = writer.Repair(writer.Snapshot().at("revision"));
            Check(fs::is_symlink(root / "stats" / backup) &&
                      Read(root / "outside") == "private outside",
                  "symlink itself preserved");
        }
        files.Write("typing_tests.json", std::string(kMaximumHistoryBytes + 1, 'x'));
        const auto oversized_revision = HistoryStore(root).Snapshot().at("revision");
        files.Write("typing_tests.json", std::string(kMaximumHistoryBytes + 1, 'y'));
        {
            HistoryStore writer(root, true);
            Check(writer.Snapshot().at("issue") != "", "oversize blocked");
            Reject([&] { writer.Repair(oversized_revision); }, "changed oversized source rejected");
            auto backup = writer.Repair(writer.Snapshot().at("revision"));
            Check(fs::file_size(root / "stats" / backup) == kMaximumHistoryBytes + 1,
                  "oversize original preserved");
        }
        files.Write("typing_tests.json", "old broken contents");
        {
            HistoryStore writer(root, true);
            const auto revision = writer.Snapshot().at("revision");
            files.Write("typing_tests.json", "new contents after opening dialog");
            Reject([&] { writer.Repair(revision); }, "stale repair must not move a new file");
            Check(Read(root / "stats/typing_tests.json") == "new contents after opening dialog",
                  "stale repair moved new contents");
        }
        {
            HistoryStore writer(root, true);
            writer.Repair(writer.Snapshot().at("revision"));
        }
        Json rows = Json::array();
        for (int i = 0; i < 520; ++i) {
            auto row = result;
            char id[64];
            snprintf(id, sizeof id, "00000000-0000-4000-8000-%012x", i);
            row["id"] = id;
            row["completedAt"] = 1700000000 + i;
            rows.push_back(std::move(row));
        }
        auto retained = Json::parse(HistoryStore::RetainedEncoding(rows));
        Check(retained.at("results").size() == 500 &&
                  retained.at("results")[0].at("completedAt") == 1700000519,
              "newest count bound");
        const auto one = HistoryStore::RetainedEncoding(Json::array({rows.back()}));
        auto bounded = Json::parse(HistoryStore::RetainedEncoding(rows, one.size() * 3));
        Check(bounded.at("results").size() >= 1 && bounded.at("results").size() <= 3,
              "byte budget keeps newest prefix");
        Reject([&] { HistoryStore::RetainedEncoding(rows, 10); },
               "single oversized record rejected");
        auto duplicate = Json{{"version", 1}, {"results", Json::array({result, result})}};
        Reject([&] { HistoryStore::NormalizeFile(duplicate); }, "duplicate IDs invalid");
        Check(HistoryStore(root).Snapshot().at("file").at("results").empty(),
              "clear/repair remains empty after restart");
        std::cout << "PASS: private practice history, independent writer, restart, idempotence, "
                     "bounds, stale/unsafe files, backup and clear isolation\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
