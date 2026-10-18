#include "metrics_worker.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace rimes::metrics;
namespace fs = std::filesystem;
void Check(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void Reject(F call, const std::string& reason) {
    bool rejected = false;
    try {
        call();
    } catch (...) {
        rejected = true;
    }
    Check(rejected, reason);
}
std::string Read(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}
Json Request(Worker& worker, const Json& command) {
    auto promise = std::make_shared<std::promise<Json>>();
    auto future = promise->get_future();
    Check(worker.Request(command, [promise](Json value) { promise->set_value(std::move(value)); }),
          "worker rejected command");
    Check(future.wait_for(std::chrono::seconds(6)) == std::future_status::ready, "worker deadline");
    auto result = future.get();
    Check(result.at("ok"), result.value("message", std::string("worker failure")));
    return result;
}
int main() {
    char temporary[] = "/tmp/xsime-statistics-store-XXXXXX";
    if (!mkdtemp(temporary))
        return 1;
    const fs::path root(temporary);
    try {
        setenv("TZ", "UTC", 1);
        tzset();
        Event key;
        key.key = "KeyA";
        key.timestamp = 1720000000;
        const auto day = LocalDay(key.timestamp);
        {
            Store writer(root, true);
            Reject([&] { Store competing(root, true); },
                   "second writer cannot overwrite active aggregates");
            writer.Consume(key, true, true);
            key.timestamp += 2;
            writer.Consume(key, true, true);
            writer.Flush();
            Store reader(root);
            Check(reader.Snapshot().at("keys").at("days").at(day).at("keys").at("KeyA") == 2,
                  "reader sees atomic snapshot");
            Reject([&] { reader.Clear("keys"); }, "reader cannot clear writer memory");
            struct stat mode {};
            Check(stat((root / "stats").c_str(), &mode) == 0 && (mode.st_mode & 0777) == 0700,
                  "private directory mode");
            Check(stat((root / "stats/key_frequency.json").c_str(), &mode) == 0 &&
                      (mode.st_mode & 0777) == 0600,
                  "private data mode");
        }
        {
            Store restarted(root, true);
            key.timestamp += 2;
            restarted.Consume(key, false, true);
            restarted.Flush();
            Check(restarted.Snapshot().at("activity").at("days").at(day).at("activeSeconds") == 2,
                  "restart cannot bridge idle time");
            Check(restarted.Snapshot().at("activity").at("days").at(day).at("sessionCount") == 2,
                  "restart starts a new session");
            restarted.Clear("keys", day);
            Check(restarted.Snapshot().at("keys").at("days").empty() &&
                      !restarted.Snapshot().at("activity").at("days").empty(),
                  "clear keys preserves independent activity");
            rimes::runtime::PrivateDirectory files(root / "stats");
            const std::string changed = "externally changed; must remain intact\n";
            files.Write("typing_speed.json", changed);
            key.timestamp += 1;
            restarted.Consume(key, false, true);
            restarted.Flush();
            Check(!restarted.Snapshot().at("issues").at("activity").get<std::string>().empty() &&
                      Read(root / "stats/typing_speed.json") == changed,
                  "external edits never overwritten");
            Reject([&] { restarted.Clear("activity"); }, "clear does not discard corrupt file");
            const auto backup = restarted.Repair("activity");
            Check(!backup.empty() && Read(root / "stats" / backup) == changed,
                  "explicit repair keeps exact original bytes");
            Check(ValidActivityFile(Json::parse(Read(root / "stats/typing_speed.json"))),
                  "repaired store valid");
        }
        const auto unsafe = root / "unsafe";
        fs::create_directory(unsafe);
        chmod(unsafe.c_str(), 0700);
        rimes::runtime::PrivateDirectory files(unsafe / "stats");
        files.Write("key_frequency.json", "not-json");
        {
            Store store(unsafe, true);
            store.Consume(key, true, true);
            store.Flush();
            Check(Read(unsafe / "stats/key_frequency.json") == "not-json",
                  "corrupt collector remains read-only");
            Check(!store.Snapshot().at("activity").at("days").empty(),
                  "healthy collector remains independent");
            const auto backup = store.Repair("keys");
            Check(Read(unsafe / "stats" / backup) == "not-json", "corrupt backup");
        }
        files.Remove("key_frequency.json");
        const auto outside = root / "outside.txt";
        std::ofstream(outside) << "external untouched";
        fs::create_symlink(outside, unsafe / "stats/key_frequency.json");
        {
            Store store(unsafe, true);
            Check(!store.Snapshot().at("issues").at("keys").get<std::string>().empty(),
                  "symlink rejected");
            const auto backup = store.Repair("keys");
            Check(fs::is_symlink(unsafe / "stats" / backup) &&
                      Read(outside) == "external untouched",
                  "repair preserves symlink without touching target");
        }
        files.Write("typing_speed.json", std::string(kMaximumFileBytes + 1, 'x'));
        {
            Store store(unsafe, true);
            Check(!store.Snapshot().at("issues").at("activity").get<std::string>().empty(),
                  "oversized store rejected");
            const auto backup = store.Repair("activity");
            Check(fs::file_size(unsafe / "stats" / backup) == kMaximumFileBytes + 1,
                  "oversized bytes preserved without reading");
        }
        const auto asynchronous = root / "worker";
        {
            Worker worker(asynchronous);
            for (int n = 0; n != 1000; ++n) {
                key.timestamp += .001;
                worker.Observe(key, true, true);
            }
            worker.EndSession();
            key.timestamp += 1;
            worker.Observe(key, false, true);
            auto value = Request(worker, {{"op", "flush"}});
            Check(value.at("snapshot").at("keys").at("days").at(day).at("keys").at("KeyA") == 1000,
                  "ordered background aggregation");
            Check(value.at("snapshot").at("activity").at("days").at(day).at("keyCount") == 1001,
                  "independent collection flags");
            auto cleared = Request(worker, {{"op", "clear"}, {"collector", "keys"}});
            Check(cleared.at("snapshot").at("keys").at("days").empty(), "serialized clear");
            // Destruction must flush the final admitted measurement.
            key.timestamp += 1;
            worker.Observe(key, true, false);
        }
        Store reader(asynchronous);
        Check(reader.Snapshot().at("keys").at("days").at(day).at("keys").at("KeyA") == 1,
              "shutdown drains and saves queue");
        fs::remove_all(root);
        std::cout
            << "PASS: single writer, private atomic files, independent collectors, restart, clear, "
               "exact backups, symlink/oversize protection and ordered background persistence\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
