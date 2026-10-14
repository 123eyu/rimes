#include "engine/rime_engine.hpp"
#include "engine/rime_key.hpp"
#include "engine/rime_paths.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>

namespace {

void Die(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void FinishAsyncDeploy(rimes::linuxime::RimeEngine& engine,
                       std::mutex& mutex,
                       std::condition_variable& ready_cv,
                       bool& notified,
                       const char* label) {
    // deploy/success can notify before is_maintenance_mode() clears. Keep
    // waiting through later watcher notifies until PollMaintenance succeeds.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    std::string error;
    while (std::chrono::steady_clock::now() < deadline) {
        if (engine.PollMaintenance(&error)) {
            if (engine.IsDeploying()) {
                Die(std::string(label) + ": engine stayed in deploying after ready");
            }
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        ready_cv.wait_for(lock, std::chrono::milliseconds(200),
                          [&]() { return notified; });
        notified = false;
    }
    Die(std::string(label) + ": deploy-ready notify never made the engine usable: " +
        error);
}

void TypeAscii(rimes::linuxime::RimeEngine& engine,
               rimes::linuxime::RimeEngine::SessionId session,
               std::string_view keys,
               rimes::linuxime::EngineSnapshot* last) {
    for (const char key : keys) {
        std::string error;
        if (!engine.ProcessKey(session, static_cast<std::int32_t>(key), 0, last, &error)) {
            Die("process_key failed: " + error);
        }
        if (!last->handled) {
            Die(std::string("librime did not handle key '") + key + "'");
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: rimes-engine-smoke <shared-data-dir> <user-data-dir> [log-dir]\n";
        return EXIT_FAILURE;
    }

    rimes::linuxime::RimeEngineOptions options;
    options.shared_data_dir = argv[1];
    options.user_data_dir = argv[2];
    options.log_dir = argc >= 4 ? argv[3] : options.user_data_dir / "log";
    options.full_maintenance_check = true;
    options.wait_for_maintenance = false;

    std::string error;
    if (!rimes::linuxime::LooksLikeSharedData(options.shared_data_dir)) {
        Die("shared data directory has no default.yaml");
    }
    if (!rimes::linuxime::EnsureDirectory(options.user_data_dir, &error) ||
        !rimes::linuxime::EnsureDirectory(options.log_dir, &error)) {
        Die(error);
    }

    const bool first_deploy =
        !std::filesystem::exists(options.user_data_dir / "build");
    rimes::linuxime::RimeEngine engine;
    bool notified = false;
    std::mutex notify_mutex;
    std::condition_variable notify_cv;
    engine.SetDeployReadyCallback([&]() {
        std::lock_guard<std::mutex> lock(notify_mutex);
        notified = true;
        notify_cv.notify_all();
    });

    const auto start_at = std::chrono::steady_clock::now();
    if (!engine.Start(options, &error)) {
        Die("engine start failed: " + error);
    }
    const auto start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start_at)
                              .count();
    if (first_deploy && start_ms > 3000) {
        Die("first-run Start blocked the caller for " + std::to_string(start_ms) +
            " ms");
    }
    // Reject a competing owner before it initializes librime or deploys.
    rimes::linuxime::RimeEngine replacement;
    if (replacement.Start(options, &error) ||
        error.find("locked by another RIMES process") == std::string::npos) {
        Die("concurrent engine was not rejected by the user directory lock");
    }
    replacement.Stop();
    std::cout << "ok: competing user-directory owner rejected before deploy\n";
    if (engine.IsDeploying()) {
        std::cout << "ok: Start returned in " << start_ms
                  << " ms while deploying\n";
    }
    FinishAsyncDeploy(engine, notify_mutex, notify_cv, notified, "first-run");
    std::cout << "ok: engine ready after async deploy (" << start_ms << " ms to return)\n";

    const auto session = engine.CreateSession(&error);
    if (session == 0) {
        Die("session create failed: " + error);
    }

    rimes::linuxime::EngineSnapshot snapshot;

    // nihao + Space => 你好
    TypeAscii(engine, session, "nihao", &snapshot);
    if (snapshot.candidates.empty()) {
        Die("nihao produced no candidates");
    }
    bool saw_nihao = false;
    for (const auto& candidate : snapshot.candidates) {
        if (candidate.text == "你好") {
            saw_nihao = true;
        }
    }
    if (!saw_nihao) {
        Die("candidate page after nihao did not include 你好");
    }
    if (!engine.ProcessKey(session, rimes::linuxime::kSpace, 0, &snapshot, &error)) {
        Die("space failed: " + error);
    }
    if (snapshot.commit_text != "你好") {
        Die("expected commit 你好, got '" + snapshot.commit_text + "'");
    }
    if (snapshot.composing || !snapshot.preedit.empty()) {
        Die("composition remained after Space commit");
    }
    std::cout << "ok: nihao + Space => 你好\n";

    // Number selection: type ni, pick candidate 2 if present.
    TypeAscii(engine, session, "ni", &snapshot);
    if (snapshot.candidates.size() < 2) {
        Die("expected at least two candidates for ni");
    }
    const std::string second = snapshot.candidates[1].text;
    if (!engine.SelectCandidate(session, 1, &snapshot, &error)) {
        Die("select candidate 2 failed: " + error);
    }
    if (snapshot.commit_text != second) {
        Die("number selection committed '" + snapshot.commit_text + "', expected '" +
            second + "'");
    }
    std::cout << "ok: number selection committed a non-first candidate\n";

    // Paging: type a syllable with many pages, page down, then up.
    TypeAscii(engine, session, "a", &snapshot);
    const int first_page = snapshot.page_no;
    const auto first_text =
        snapshot.candidates.empty() ? std::string() : snapshot.candidates.front().text;
    if (!engine.ProcessKey(session, rimes::linuxime::kPageDown, 0, &snapshot, &error)) {
        Die("page down failed: " + error);
    }
    if (snapshot.page_no <= first_page && snapshot.is_last_page &&
        snapshot.candidates.empty()) {
        Die("page down did not advance the menu");
    }
    const bool page_changed =
        snapshot.page_no != first_page ||
        (!snapshot.candidates.empty() && snapshot.candidates.front().text != first_text);
    if (!page_changed && !snapshot.is_last_page) {
        Die("page down left the first page unchanged");
    }
    if (!engine.ProcessKey(session, rimes::linuxime::kPageUp, 0, &snapshot, &error)) {
        Die("page up failed: " + error);
    }
    std::cout << "ok: paging\n";

    // Escape cancels.
    TypeAscii(engine, session, "nihao", &snapshot);
    if (!engine.ProcessKey(session, rimes::linuxime::kEscape, 0, &snapshot, &error) &&
        !engine.ClearComposition(session, &snapshot, &error)) {
        Die("escape/clear failed: " + error);
    }
    if (!snapshot.commit_text.empty()) {
        Die("escape must not commit");
    }
    if (snapshot.composing || !snapshot.preedit.empty()) {
        // Some schemas swallow Escape as handled without clearing until we ask.
        engine.ClearComposition(session, &snapshot, &error);
    }
    if (snapshot.composing || !snapshot.preedit.empty()) {
        Die("composition remained after Escape");
    }
    std::cout << "ok: Escape cancels\n";

    struct WubiCase { const char* schema; const char* code; const char* text; const char* hint; };
    for (const auto& item : {WubiCase{"wubi86", "wq", "你", ""},
                            WubiCase{"wubi86", "zni", "你", "wq"},
                            WubiCase{"wubi86", "znihao", "你好", "wqvb"},
                            WubiCase{"lingxi_wubi", "wq", "你", ""},
                            WubiCase{"lingxi_wubi", "nihao", "你好", "wqvb"},
                            WubiCase{"lingxi_wubi", "zni", "你", "wq"}}) {
        if (!engine.SelectSchema(session, item.schema, &error)) Die("Wubi schema selection: " + error);
        TypeAscii(engine, session, item.code, &snapshot);
        int found = -1;
        for (std::size_t i = 0; i < snapshot.candidates.size(); ++i) {
            const auto& candidate = snapshot.candidates[i];
            if (candidate.text != item.text) continue;
            found = static_cast<int>(i);
            if (*item.hint && (candidate.comment.size() < 3 || candidate.comment.front() != '(' ||
                candidate.comment.back() != ')' || candidate.comment.find(item.hint) == std::string::npos))
                Die("Wubi code hint: " + candidate.comment);
            break;
        }
        if (found < 0 || (std::string(item.code) == "wq" && found != 0)) Die("Wubi candidate/order");
        if (!engine.SelectCandidate(session, found, &snapshot, &error) || snapshot.commit_text != item.text)
            Die("Wubi commit must contain only candidate text");
        std::cout << "ok: Wubi " << item.schema << '/' << item.code << '\n';
    }

    for (const auto* schema : {"wubi86", "lingxi_wubi"}) {
        for (const auto* code : {"voni", "wqiy", "zfiao", "fiao"}) {
            if (std::string(code) == "fiao" && std::string(schema) == "wubi86") continue;
            if (!engine.SelectSchema(session, schema, &error)) Die("Wubi schema: " + error);
            engine.ClearComposition(session, &snapshot, &error);
            int typed = 0;
            for (const char* key = code; *key; ++key) {
                TypeAscii(engine, session, std::string(1, *key), &snapshot); ++typed;
                const std::string expected = std::string(code) == "voni" && typed == 4 ? "灵犀" : "";
                if (snapshot.commit_text != expected) Die("Four-key unique commit: " + std::string(schema) + '/' + code);
            }
        }
    }
    engine.DestroySession(session, &error);

    // Later fcitx5 starts can still run a short rebuild. Dropping build/
    // forces librime to compile again — same stuck-Deploying failure mode.
    std::error_code remove_error;
    std::filesystem::remove_all(options.user_data_dir / "build", remove_error);
    if (remove_error) {
        Die("could not clear build/ for rebuild: " + remove_error.message());
    }

    notified = false;
    engine.SetDeployReadyCallback([&]() {
        std::lock_guard<std::mutex> lock(notify_mutex);
        notified = true;
        notify_cv.notify_all();
    });
    const auto rebuild_at = std::chrono::steady_clock::now();
    if (!engine.StartBackgroundMaintenance(true, &error)) {
        Die("rebuild start failed: " + error);
    }
    const auto rebuild_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - rebuild_at)
                                .count();
    if (rebuild_ms > 3000) {
        Die("rebuild StartBackgroundMaintenance blocked the caller for " +
            std::to_string(rebuild_ms) + " ms");
    }
    if (!engine.IsDeploying()) {
        Die("deleting build/ did not start a background rebuild");
    }
    FinishAsyncDeploy(engine, notify_mutex, notify_cv, notified, "rebuild");
    const auto rebuilt = engine.CreateSession(&error);
    if (rebuilt == 0) {
        Die("session create failed after rebuild: " + error);
    }
    TypeAscii(engine, rebuilt, "nihao", &snapshot);
    if (!engine.ProcessKey(rebuilt, rimes::linuxime::kSpace, 0, &snapshot, &error)) {
        Die("rebuild space failed: " + error);
    }
    if (snapshot.commit_text != "你好") {
        Die("rebuild expected commit 你好, got '" + snapshot.commit_text + "'");
    }
    engine.DestroySession(rebuilt, &error);
    engine.Stop();
    std::cout << "ok: restart-triggered rebuild became usable (" << rebuild_ms
              << " ms to return)\n";

    std::cout << "RIMES engine smoke passed\n";
    return EXIT_SUCCESS;
}
