#include "buffer_protocol.hpp"
#include "configuration.hpp"
#include "official_catalog.hpp"
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include <fstream>
#include <future>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <testfrontend_public.h>
#include <thread>
#include <unistd.h>
namespace rt = rimes::runtime;
using namespace std::chrono_literals;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Wait(F condition, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + 6s;
    while (!condition()) {
        Check(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(10ms);
    }
}
void Key(fcitx::AddonInstance* frontend, const fcitx::ICUUID& uuid, const char* name) {
    frontend->call<fcitx::ITestFrontend::keyEvent>(uuid, fcitx::Key(name), false);
    frontend->call<fcitx::ITestFrontend::keyEvent>(uuid, fcitx::Key(name), true);
}
void Command(const std::filesystem::path& root, const std::string& op, const std::string& text = "",
             const std::string& field = "") {
    // Match the real UI's persistent transport. Closing immediately after send
    // races the host's initial snapshot and can discard an unread command.
    struct Connection {
        int fd = -1;
        ~Connection() { if (fd >= 0) close(fd); }
    };
    static thread_local Connection connection;
    if (connection.fd < 0) {
        connection.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        Check(connection.fd >= 0, "Buffer socket missing");
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto path = (root / "buffer.sock").string();
        Check(path.size() < sizeof(address.sun_path), "Buffer path too long");
        std::copy(path.begin(), path.end(), address.sun_path);
        Check(connect(connection.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
              "Buffer connection failed");
    }
    const auto fd = connection.fd;
    char pending[16384];
    while (recv(fd, pending, sizeof(pending), MSG_DONTWAIT) > 0) {}
    rt::Json command{{"v", 1}, {"op", op}};
    if (!text.empty())
        command[op == "select_plugin" ? "id" : "text"] = text;
    if (op == "configure_plugin") {
        command["plugin"] = "builtin.claude-code-cli";
        command["id"] = field;
        command["value"] = text;
        command.erase("text");
    }
    std::string frame;
    const bool written =
        rimes::buffer::EncodeFrame(command.dump(), &frame) &&
        send(fd, frame.data(), frame.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(frame.size());
    Check(written, "Buffer command failed");
}
rimes::buffer::Snapshot Snapshot(const std::filesystem::path& root) {
    std::ifstream stream(root / "buffer.json");
    const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
    rimes::buffer::Snapshot snapshot;
    Check(rimes::buffer::ParseSnapshot(bytes, &snapshot), "Buffer snapshot malformed");
    return snapshot;
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 5)
        return 2;
    char directory[] = "/tmp/xsime-claude-fcitx-XXXXXX";
    if (!mkdtemp(directory))
        return 1;
    const std::filesystem::path root(directory);
    int result = 0;
    try {
        for (const auto* name : {"RIMES_USER_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME",
                                 "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"})
            setenv(name, directory, 1);
        setenv("RIMES_LOG_DIR", (root / "log").c_str(), 1);
        setenv("RIMES_BUFFER_SOCKET", (root / "buffer.sock").c_str(), 1);
        setenv("RIMES_BUFFER_DUMP", (root / "buffer.json").c_str(), 1);
        setenv("RIMES_CAPSULE_SOCKET", (root / "capsule.sock").c_str(), 1);
        setenv("RIMES_BUFFER_HEADLESS", "1", 1);
        setenv("RIMES_CAPSULE_HEADLESS", "1", 1);
        setenv("RIMES_BUFFER_AUTO_CAPTURE", "0", 1);
        (void)rt::PrivateDirectory(root / "bin");
        std::filesystem::create_symlink(std::filesystem::absolute(argv[4]), root / "bin/claude");
        setenv("PATH", ((root / "bin").string() + ":/usr/bin:/bin").c_str(), 1);
        rt::PrivateDirectory home(root / "home");
        setenv("HOME", home.Path().c_str(), 1);
        home.Write("fixture-mode", "text-hold");
        rt::ConfigurationStore configuration(root / "configuration");
        auto config = configuration.Load();
        config["plugins"]["builtin.claude-code-cli"] = {{"model", "claude-opus-5-5"},
                                                        {"effort", "high"}};
        config["gatewayEnabled"] = false;
        configuration.Save(config, config.at("revision"));
        const auto bundled = rimes::plugins::BundledPackages();
        rimes::plugins::PackageStore packages(root / "plugins", rimes::plugins::OfficialCatalog(),
                                              bundled, rimes::plugins::CompiledAdapters(),
                                              RIMES_PRODUCT_VERSION);
        for (const auto* id : {"builtin.claude-code-cli"}) {
            const auto installed = packages.Inspect(id);
            if (!installed.installed)
                packages.Install(id, bundled.at(id), installed.grant);
            packages.Enable(id, true, packages.Inspect(id).grant);
        }
        fcitx::setupTestingEnvironment(argv[1], {argv[2]}, {argv[3]});
        char name[] = "claude-fcitx-e2e", disabled[] = "--disable=all",
             enabled[] = "--enable=testfrontend,testui,rimes";
        char* args[] = {name, disabled, enabled};
        fcitx::Instance instance(3, args);
        instance.addonManager().registerDefaultLoader(nullptr);
        fcitx::EventDispatcher dispatcher;
        dispatcher.attach(&instance.eventLoop());
        std::thread driver;
        std::unique_ptr<fcitx::EventSourceTime> timer;
        auto main = [&](std::function<void()> action) {
            auto promise = std::make_shared<std::promise<void>>();
            auto done = promise->get_future();
            dispatcher.schedule([action, promise] {
                try {
                    action();
                    promise->set_value();
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            });
            Check(done.wait_for(2s) == std::future_status::ready, "Fcitx input loop blocked");
            done.get();
        };
        dispatcher.schedule([&] {
            auto& manager = instance.inputMethodManager();
            Check(manager.entry("rimes"), "rimes addon missing");
            if (!manager.groupCount()) {
                manager.addEmptyGroup("Default");
                manager.setGroupOrder({"Default"});
            }
            fcitx::InputMethodGroup group("Default");
            group.setDefaultLayout("us");
            group.inputMethodList().emplace_back(fcitx::InputMethodGroupItem("rimes"));
            group.setDefaultInputMethod("rimes");
            manager.setGroup(group);
            manager.setCurrentGroup("Default");
            auto* frontend = instance.addonManager().addon("testfrontend", true);
            Check(frontend, "frontend missing");
            const auto uuid =
                frontend->call<fcitx::ITestFrontend::createInputContext>("claude-fixture");
            auto* ic = instance.inputContextManager().findByUUID(uuid);
            Check(ic, "input context missing");
            instance.setCurrentInputMethod(ic, "rimes", true);
            ic->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
            ic->focusIn();
            const auto deadline = std::chrono::steady_clock::now() + 180s;
            timer = instance.eventLoop().addTimeEvent(
                CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 50000, 0,
                [&, frontend, uuid, ic, deadline](fcitx::EventSourceTime* event, uint64_t) {
                    try {
                        auto* engine = instance.inputMethodEngine(ic);
                        Check(std::chrono::steady_clock::now() < deadline,
                              "addon deployment deadline");
                        if (!engine ||
                            engine->subMode(*manager.entry("rimes"), *ic) == "Deploying") {
                            event->setTime(fcitx::now(CLOCK_MONOTONIC) + 50000);
                            event->setOneShot();
                            return true;
                        }
                        driver = std::thread([&, frontend, uuid] {
                            try {
                                main([&] { Key(frontend, uuid, "Control+Shift+B"); });
                                Command(root, "select_plugin", "builtin.claude-code-cli");
                                Command(root, "paste", "公开原文🙂");
                                Wait([&] { return Snapshot(root).can_generate; },
                                     "Claude adapter unavailable");
                                Check(!home.Read("inference-started", 128),
                                      "CLI received text before explicit Generate");
                                Check(Snapshot(root).plugin_options.size() == 2 &&
                                          !Snapshot(root).live_expand,
                                      "native Claude toolbar or single exchange missing");
                                Command(root, "configure_plugin", "low", "effort");
                                Wait(
                                    [&] {
                                        return configuration.Load()
                                                   .at("plugins")
                                                   .at("builtin.claude-code-cli")
                                                   .at("effort") == "low";
                                    },
                                    "toolbar effort not persisted");
                                Command(root, "configure_plugin", "invalid-model", "model");
                                Command(root, "configure_plugin", "high", "effort");
                                Wait(
                                    [&] {
                                        return configuration.Load()
                                                       .at("plugins")
                                                       .at("builtin.claude-code-cli")
                                                       .at("effort") == "high" &&
                                               Snapshot(root).can_generate;
                                    },
                                    "toolbar effort not restored");
                                Check(configuration.Load()
                                              .at("plugins")
                                              .at("builtin.claude-code-cli")
                                              .at("model") == "claude-opus-5-5",
                                      "invalid toolbar model accepted");
                                Command(root, "generate");
                                Wait([&] { return !Snapshot(root).results.empty(); },
                                     "Claude preview missing");
                                auto preview = Snapshot(root);
                                Check(!preview.can_deliver &&
                                          preview.results[0].text == "公开🙂。" &&
                                          !preview.results[0].ready,
                                      "partial JSON or reasoning became deliverable");
                                Check(!preview.message.empty() && preview.message.find("PRIVATE-") == std::string::npos,
                                      "public activity did not reach Buffer");
                                home.Write("release-text", "release");
                                Wait(
                                    [&] {
                                        return Snapshot(root).phase ==
                                               rimes::buffer::DerivedPhase::Ready;
                                    },
                                    "Claude completion missing");
                                const auto ready = Snapshot(root);
                                Check(ready.staged_text == "公开原文🙂" &&
                                          ready.result_blocks.size() == 2 &&
                                          ready.results[0].text == "公开🙂。第二句。",
                                      "Claude semantic result/source differs");
                                main([&] {
                                    for (const auto& block : ready.result_blocks)
                                        frontend->call<fcitx::ITestFrontend::pushCommitExpectation>(
                                            block.text);
                                });
                                Command(root, "send_all");
                                Wait([&] { return !Snapshot(root).visible; },
                                     "Claude native delivery did not close");
                                Check(Snapshot(root).staged_text.empty(),
                                      "final delivery did not consume source");
                                home.Remove("release-text");
                                home.Remove("text-started");
                                main([&] { Key(frontend, uuid, "Control+Shift+B"); });
                                Command(root, "paste", "取消后的公开原文");
                                Wait([&] { return Snapshot(root).can_generate; },
                                     "second request unavailable");
                                Command(root, "generate");
                                Wait([&] { return home.Read("text-started", 32).has_value(); },
                                     "second request did not start");
                                Command(root, "close");
                                Wait([&] { return !Snapshot(root).visible; },
                                     "Buffer did not close");
                                home.Write("release-text", "release");
                                std::this_thread::sleep_for(200ms);
                                Check(Snapshot(root).results.empty() &&
                                          Snapshot(root).staged_text == "取消后的公开原文",
                                      "late text restored closed Buffer");
                                // A changed source invalidates the request before a late result.
                                home.Remove("release-text");
                                home.Remove("text-started");
                                main([&] { Key(frontend, uuid, "Control+Shift+B"); });
                                Wait([&] { return Snapshot(root).can_generate; },
                                     "reopen unavailable");
                                Command(root, "generate");
                                Wait([&] { return home.Read("text-started", 32).has_value(); },
                                     "edit request missing");
                                Command(root, "paste", "追加公开内容");
                                Wait(
                                    [&] {
                                        return Snapshot(root).staged_text.find("追加公开内容") !=
                                               std::string::npos;
                                    },
                                    "source edit missing");
                                home.Write("release-text", "release");
                                Wait(
                                    [&] {
                                        return Snapshot(root).phase !=
                                               rimes::buffer::DerivedPhase::Running;
                                    },
                                    "source edit did not cancel");
                                Check(!Snapshot(root).can_deliver,
                                      "stale source became deliverable");

                                home.Remove("release-text");
                                home.Remove("text-started");
                                Wait([&] { return Snapshot(root).can_generate; },
                                     "focus request unavailable");
                                Command(root, "generate");
                                Wait([&] { return home.Read("text-started", 32).has_value(); },
                                     "focus request missing");
                                main([&] { ic->focusOut(); });
                                home.Write("release-text", "release");
                                Wait([&] { return !Snapshot(root).can_deliver; },
                                     "lost focus remained deliverable");
                                std::this_thread::sleep_for(200ms);
                                Check(Snapshot(root).results.empty(),
                                      "late completion restored lost focus");
                                main([&] { ic->focusIn(); });

                                home.Remove("release-text");
                                home.Remove("text-started");
                                main([&] { Key(frontend, uuid, "Control+Shift+B"); });
                                Wait(
                                    [&] {
                                        return Snapshot(root).visible &&
                                               Snapshot(root).can_generate;
                                    },
                                    "revoke request unavailable");
                                Command(root, "generate");
                                Wait([&] { return home.Read("text-started", 32).has_value(); },
                                     "revoke request missing");
                                packages.Enable("builtin.claude-code-cli", false,
                                                packages.Inspect("builtin.claude-code-cli").grant);
                                Wait([&] { return !Snapshot(root).can_generate; },
                                     "plugin disable not observed");
                                home.Write("release-text", "release");
                                std::this_thread::sleep_for(200ms);
                                Check(!Snapshot(root).can_deliver && Snapshot(root).results.empty(),
                                      "revoked plugin delivered stale reply");
                                Command(root, "close");
                                Wait([&] { return !Snapshot(root).visible; },
                                     "closed Buffer remained visible");
                                main([&] {
                                    frontend->call<fcitx::ITestFrontend::pushCommitExpectation>(
                                        "你好");
                                    for (const auto* key : {"n", "i", "h", "a", "o", "space"})
                                        Key(frontend, uuid, key);
                                });
                                std::cout << "Actual Fcitx Claude: explicit request, model/effort "
                                             "controls, semantic stream, native commitString, "
                                             "source edits, close/focus cancellation, plugin "
                                             "revocation and ordinary typing passed\n";
                            } catch (const std::exception& error) {
                                std::cerr << error.what() << '\n';
                                result = 1;
                            }
                            dispatcher.schedule([&] { instance.exit(); });
                        });
                    } catch (const std::exception& error) {
                        std::cerr << error.what() << '\n';
                        result = 1;
                        instance.exit();
                    }
                    return true;
                });
        });
        try {
            instance.exec();
        } catch (const fcitx::InstanceQuietQuit&) {
        }
        if (driver.joinable())
            driver.join();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    if (!result)
        std::filesystem::remove_all(root);
    return result;
}
