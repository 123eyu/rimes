#include "buffer_protocol.hpp"
#include "configuration.hpp"
#include "mailbox_store.hpp"
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
namespace mb = rimes::mailbox;
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
        command["plugin"] = "builtin.codex-cli";
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
    char directory[] = "/tmp/xsime-codex-fcitx-XXXXXX";
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
        std::filesystem::create_symlink(std::filesystem::absolute(argv[4]), root / "bin/codex");
        setenv("PATH", ((root / "bin").string() + ":/usr/bin:/bin").c_str(), 1);
        rt::PrivateDirectory home(root / "ai/codex-home");
        home.Write("fixture-mode", "text-hold");
        rt::ConfigurationStore configuration(root / "configuration");
        auto config = configuration.Load();
        config["plugins"]["builtin.codex-cli"] = {
            {"model", "gpt-6-sol"}, {"effort", "high"}, {"skill", ""}};
        config["gatewayEnabled"] = false;
        configuration.Save(config, config.at("revision"));
        const auto bundled = rimes::plugins::BundledPackages();
        rimes::plugins::PackageStore packages(root / "plugins", rimes::plugins::OfficialCatalog(),
                                              bundled, rimes::plugins::CompiledAdapters(),
                                              RIMES_PRODUCT_VERSION);
        for (const auto* id : {"builtin.codex-cli", "builtin.mailbox.inbox"}) {
            const auto installed = packages.Inspect(id);
            if (!installed.installed)
                packages.Install(id, bundled.at(id), installed.grant);
            packages.Enable(id, true, packages.Inspect(id).grant);
        }
        fcitx::setupTestingEnvironment(argv[1], {argv[2]}, {argv[3]});
        char name[] = "codex-fcitx-e2e", disabled[] = "--disable=all",
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
                frontend->call<fcitx::ITestFrontend::createInputContext>("codex-fixture");
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
                                Command(root, "select_plugin", "builtin.codex-cli");
                                Command(root, "paste", "原文🙂");
                                Wait([&] { return Snapshot(root).can_generate; },
                                     "Codex adapter unavailable");
                                Check(!home.Read("last-input.json", 65536),
                                      "CLI received text before explicit Generate");
                                Check(Snapshot(root).plugin_options.size() == 3 &&
                                          !Snapshot(root).live_expand,
                                      "native Codex toolbar or single exchange missing");
                                Command(root, "configure_plugin", "low", "effort");
                                Wait(
                                    [&] {
                                        return configuration.Load()
                                                   .at("plugins")
                                                   .at("builtin.codex-cli")
                                                   .at("effort") == "low";
                                    },
                                    "toolbar effort not persisted");
                                Command(root, "configure_plugin", "invalid-model", "model");
                                Command(root, "configure_plugin", "high", "effort");
                                Wait(
                                    [&] {
                                        return configuration.Load()
                                                       .at("plugins")
                                                       .at("builtin.codex-cli")
                                                       .at("effort") == "high" &&
                                               Snapshot(root).can_generate;
                                    },
                                    "toolbar effort not restored");
                                Check(configuration.Load()
                                              .at("plugins")
                                              .at("builtin.codex-cli")
                                              .at("model") == "gpt-6-sol",
                                      "invalid toolbar model accepted");
                                Command(root, "generate");
                                Wait([&] { return !Snapshot(root).results.empty(); },
                                     "Codex preview missing");
                                auto preview = Snapshot(root);
                                Check(!preview.can_deliver &&
                                          preview.results[0].text == "公开🙂。" &&
                                          !preview.results[0].ready,
                                      "partial JSON or reasoning became deliverable");
                                home.Write("release-text", "release");
                                Wait(
                                    [&] {
                                        return Snapshot(root).phase ==
                                               rimes::buffer::DerivedPhase::Ready;
                                    },
                                    "Codex completion missing");
                                const auto ready = Snapshot(root);
                                Check(ready.staged_text == "原文🙂" &&
                                          ready.result_blocks.size() == 2 &&
                                          ready.results[0].text == "公开🙂。第二句。",
                                      "Codex semantic result/source differs");
                                main([&] {
                                    for (const auto& block : ready.result_blocks)
                                        frontend->call<fcitx::ITestFrontend::pushCommitExpectation>(
                                            block.text);
                                });
                                Command(root, "send_all");
                                Wait([&] { return !Snapshot(root).visible; },
                                     "Codex native delivery did not close");
                                Check(Snapshot(root).staged_text.empty(),
                                      "final delivery did not consume source");
                                home.Remove("release-text");
                                home.Remove("text-started");
                                main([&] { Key(frontend, uuid, "Control+Shift+B"); });
                                Command(root, "paste", "取消后的原文");
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
                                          Snapshot(root).staged_text == "取消后的原文",
                                      "late text restored closed Buffer");
                                main([&] { Key(frontend, uuid, "Control+Shift+B"); });
                                Wait([&] { return Snapshot(root).visible; },
                                     "image workbench did not reopen");
                                Command(root, "configure_plugin", "imagegen", "skill");
                                home.Write("fixture-mode", "image-hold");
                                Wait([&] { return Snapshot(root).mailbox_image; },
                                     "image Skill config did not reach host");

                                Wait([&] { return Snapshot(root).can_generate; },
                                     "image source unavailable");
                                Command(root, "generate");
                                Wait([&] { return home.Read("image-started", 32).has_value(); },
                                     "image did not start");
                                Command(root, "close");
                                Wait([&] { return !Snapshot(root).visible; },
                                     "image Buffer did not close");
                                mb::Store store(root / "mailbox");
                                Check(!store.RecoverInterrupted(),
                                      "Mailbox reopening killed live image");
                                main([&] {
                                    frontend->call<fcitx::ITestFrontend::pushCommitExpectation>(
                                        "你好");
                                    for (const auto* key : {"n", "i", "h", "a", "o", "space"})
                                        Key(frontend, uuid, key);
                                });
                                home.Write("release-image", "release");
                                Wait(
                                    [&] {
                                        return Snapshot(root).phase ==
                                               rimes::buffer::DerivedPhase::Ready;
                                    },
                                    "background image did not complete");
                                const auto image = Snapshot(root);
                                Check(!image.visible && !image.can_deliver &&
                                          image.results.empty() && !image.mailbox_thread.empty() &&
                                          image.staged_text == "取消后的原文",
                                      "image became inline text or consumed source");
                                const auto thread = store.Get(image.mailbox_thread);
                                Check(
                                    thread.at("messages").size() == 2 &&
                                        thread.at("generation").at("phase") == "succeeded" &&
                                        store
                                            .Image(thread.at("messages").back().at("imageFileName"))
                                            .has_value(),
                                    "image not owned by Mailbox");
                                std::cout << "Actual Fcitx Codex: explicit request, semantic "
                                             "stream, native commitString, close cancellation, "
                                             "late result rejection, live image recovery, "
                                             "background Mailbox PNG and ordinary typing passed\n";
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
