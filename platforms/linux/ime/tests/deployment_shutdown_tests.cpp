#include "engine/rime_engine.hpp"
#include "engine/rime_user_lock.hpp"
#include "engine/deployment_process.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace rimes::linuxime;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

void Require(bool condition, const std::string& reason) {
    if (!condition) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}

int main(int argc, char** argv) {
    // This executable doubles as a deterministic slow/failing deploy worker.
    if (argc == 5) {
        std::string mode;
        std::ifstream(fs::path(argv[1]) / "mode") >> mode;
        if (mode == "fail") return 7;
        std::signal(SIGTERM, SIG_IGN);
        std::ofstream(fs::path(argv[3]) / "worker.pid") << getpid();
        for (;;) pause();
    }
    char pattern[] = "/tmp/rimes-deployment-shutdown-XXXXXX";
    const char* made = mkdtemp(pattern);
    Require(made != nullptr, "temporary directory");
    const fs::path root(made);
    const auto shared = root / "shared";
    const auto user = root / "user";
    const auto logs = root / "logs";
    for (const auto& path : {shared, user, logs}) fs::create_directories(path);
    std::ofstream(shared / "default.yaml") << "schema_list: []\n";
    std::ofstream(shared / "mode") << "slow\n";

    RimeEngineOptions options;
    options.shared_data_dir = shared;
    options.user_data_dir = user;
    options.log_dir = logs;
    options.wait_for_maintenance = false;
    options.deployment_helper = fs::canonical("/proc/self/exe");
    RimeEngine engine;
    for (int cycle = 0; cycle < 3; ++cycle) {
        std::string error;
        fs::remove(logs / "worker.pid");
        Require(engine.Start(options, &error), error);
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        pid_t pid = 0;
        while (pid <= 0 && std::chrono::steady_clock::now() < deadline) {
            std::ifstream(logs / "worker.pid") >> pid;
            std::this_thread::sleep_for(10ms);
        }
        Require(pid > 0, "slow worker started");
        RimeUserLock competitor;
        Require(!competitor.Acquire(user, &error), "writer lock held during deployment");
        Require(engine.IsDeploying() && !engine.IsHealthy(), "pending deployment state");
        const auto start = std::chrono::steady_clock::now();
        engine.Stop();
        Require(std::chrono::steady_clock::now() - start < 1500ms,
                "shutdown exceeded 1.5 seconds for a worker ignoring SIGTERM");
        Require(kill(pid, 0) < 0 && errno == ESRCH, "worker was terminated and reaped");
        Require(competitor.Acquire(user, &error), "replacement can acquire writer lock after shutdown");
    }
    std::ofstream(shared / "mode") << "fail\n";
    std::string error;
    Require(engine.Start(options, &error), error);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (engine.IsDeploying() && std::chrono::steady_clock::now() < deadline) {
        Require(!engine.PollMaintenance(&error), "failed deployment must not become healthy");
        std::this_thread::sleep_for(10ms);
    }
    Require(!engine.IsDeploying() && !engine.IsHealthy() &&
            error.find("worker failed") != std::string::npos, "worker failure surfaced");
    engine.Stop();
    options.deployment_helper = root / "missing-worker";
    Require(!engine.Start(options, &error), "missing worker is rejected");
    RimeUserLock replacement;
    Require(replacement.Acquire(user, &error), "spawn failure releases writer lock");
    replacement.Release();
    fs::remove_all(root);
    std::cout << "ok: bounded shutdown, child reaping, restart, writer exclusion and failure reporting\n";
}
