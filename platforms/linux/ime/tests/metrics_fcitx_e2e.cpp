#include "chord_core.hpp"
#include "configuration.hpp"
#include "metrics_protocol.hpp"
#include "metrics_store.hpp"
#include "official_catalog.hpp"
#include "statistics/metrics_service.hpp"
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include <future>
#include <gio/gio.h>
#include <iostream>
#include <testfrontend_public.h>
#include <thread>
#include <unistd.h>

namespace mt = rimes::metrics;
namespace rt = rimes::runtime;
using namespace std::chrono_literals;
void Check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
mt::Json Call(const mt::Json& command, bool chord = false) {
    GError* error = nullptr;
    auto* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    g_clear_error(&error);
    Check(bus, "requires private D-Bus session");
    auto* reply = g_dbus_connection_call_sync(
        bus, chord ? "org.scholay.XSIME.Chord" : mt::kBusName,
        chord ? "/org/scholay/XSIME/Chord" : mt::kBusPath,
        chord ? "org.scholay.XSIME.Chord1" : mt::kBusInterface, "Command",
        g_variant_new("(s)", command.dump().c_str()), G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, nullptr, &error);
    g_object_unref(bus);
    if (!reply) {
        std::string message = error ? error->message : "service unavailable";
        g_clear_error(&error);
        throw std::runtime_error(message);
    }
    const char* bytes = nullptr;
    g_variant_get(reply, "(&s)", &bytes);
    auto result = mt::Json::parse(bytes);
    g_variant_unref(reply);
    Check(result.value("ok", false), result.value("message", "command failed"));
    return result;
}
mt::Json Status() { return Call({{"op", "status"}}); }
void Ready(const std::function<bool()>& condition) {
    const auto until = std::chrono::steady_clock::now() + 240s;
    std::string error;
    while (std::chrono::steady_clock::now() < until) {
        try {
            if (condition())
                return;
        } catch (const std::exception& e) {
            error = e.what();
        }
        std::this_thread::sleep_for(30ms);
    }
    throw std::runtime_error("service deadline: " + error);
}
std::int64_t Sum(const mt::Json& snapshot, const std::string& metric) {
    std::int64_t count = 0;
    for (const auto& day : snapshot.at("activity").at("days"))
        count += day.at(metric).get<std::int64_t>();
    return count;
}
std::int64_t Keys(const mt::Json& snapshot, const std::string& key = {}) {
    std::int64_t count = 0;
    for (const auto& day : snapshot.at("keys").at("days"))
        for (const auto& entry : day.at("keys").items())
            if (key.empty() || entry.key() == key)
                count += entry.value().get<std::int64_t>();
    return count;
}
int main(int argc, char** argv) {
    if (argc != 4)
        return 2;
    char temporary[] = "/tmp/xsime-metrics-fcitx-XXXXXX";
    if (!mkdtemp(temporary))
        return 1;
    const std::filesystem::path root(temporary);
    int result = 0;
    try {
        Check(fcitx::MetricsKeyID(fcitx::Key(FcitxKey_z, fcitx::KeyStates(), 24)) == "KeyQ",
              "physical Q follows translated symbol");
        Check(fcitx::MetricsKeyID(fcitx::Key(FcitxKey_Q)) == "KeyQ", "canonical shifted symbol");
        Check(fcitx::MetricsKeyID(fcitx::Key(FcitxKey_Shift_R)) == "RightShift", "modifier ID");
        Check(fcitx::MetricsKeyID(fcitx::Key(static_cast<fcitx::KeySym>(0x01004f60))).empty(),
              "raw text used as key ID");
        for (const auto* name : {"RIMES_USER_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME",
                                 "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"})
            setenv(name, temporary, 1);
        setenv("RIMES_LOG_DIR", (root / "log").c_str(), 1);
        setenv("RIMES_BUFFER_HEADLESS", "1", 1);
        setenv("RIMES_CAPSULE_HEADLESS", "1", 1);
        setenv("RIMES_BUFFER_AUTO_CAPTURE", "0", 1);
        rt::ConfigurationStore config_store(root / "configuration");
        auto config = config_store.Load();
        config["gatewayEnabled"] = false;
        config_store.Save(config, config.at("revision"));
        const auto bundled = rimes::plugins::BundledPackages();
        rimes::plugins::PackageStore packages(root / "plugins", rimes::plugins::OfficialCatalog(),
                                              bundled, rimes::plugins::CompiledAdapters(),
                                              RIMES_PRODUCT_VERSION);
        auto enable = [&](const std::string& id, bool value) {
            auto installed = packages.Inspect(id);
            if (!installed.installed)
                packages.Install(id, bundled.at(id), installed.grant);
            packages.Enable(id, value, packages.Inspect(id).grant);
        };
        auto consent = [&](bool keys, bool activity) {
            auto value = config_store.Load();
            value["plugins"][mt::kPluginID] = {{"recordsKeyFrequency", keys},
                                               {"recordsTypingActivity", activity}};
            config_store.Save(value, value.at("revision"));
            Ready([&] {
                const auto s = Status();
                return s.at("recordsKeyFrequency") == keys &&
                       s.at("recordsTypingActivity") == activity;
            });
        };
        fcitx::setupTestingEnvironment(argv[1], {argv[2]}, {argv[3]});
        char name[] = "metrics-fcitx-e2e", disabled[] = "--disable=all",
             enabled[] = "--enable=testfrontend,testui,rimes";
        char* args[] = {name, disabled, enabled};
        fcitx::Instance instance(3, args);
        instance.addonManager().registerDefaultLoader(nullptr);
        fcitx::EventDispatcher dispatcher;
        dispatcher.attach(&instance.eventLoop());
        std::thread driver;
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
            Check(done.wait_for(3s) == std::future_status::ready,
                  "statistics blocked Fcitx input loop");
            done.get();
        };
        dispatcher.schedule([&] {
            auto& manager = instance.inputMethodManager();
            Check(manager.entry("rimes"), "addon missing");
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
            Check(frontend, "test frontend missing");
            const auto first =
                frontend->call<fcitx::ITestFrontend::createInputContext>("metrics-test-editor");
            const auto own =
                frontend->call<fcitx::ITestFrontend::createInputContext>("rimes-settings");
            auto* ic = instance.inputContextManager().findByUUID(first);
            auto* settings = instance.inputContextManager().findByUUID(own);
            for (auto* target : {ic, settings}) {
                instance.setCurrentInputMethod(target, "rimes", true);
                target->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
            }
            ic->focusIn();
            driver = std::thread([&, frontend, first, own, ic, settings] {
                auto key = [&](const fcitx::ICUUID& uuid, fcitx::Key key, bool release = false) {
                    frontend->call<fcitx::ITestFrontend::keyEvent>(uuid, key, release);
                };
                auto tap = [&](const fcitx::ICUUID& uuid, const std::string& name) {
                    key(uuid, fcitx::Key(name));
                    key(uuid, fcitx::Key(name), true);
                };
                auto word = [&](const fcitx::ICUUID& uuid) {
                    for (char c : std::string("nihao"))
                        tap(uuid, std::string(1, c));
                    frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("你好");
                    tap(uuid, "space");
                };
                auto snapshot = [&] { return Status().at("snapshot"); };
                auto clear = [&] {
                    Call({{"op", "clear"}, {"collector", "keys"}});
                    Call({{"op", "clear"}, {"collector", "activity"}});
                };
                try {
                    Ready([&] {
                        auto s = Call({{"op", "status"}}, true);
                        return !s.at("busy").get<bool>() && s.at("phase") != "starting";
                    });
                    Check(!Status().at("enabled").get<bool>(),
                          "opening statistics enabled collection");
                    main([&] { word(first); });
                    Check(Keys(snapshot()) == 0 && Sum(snapshot(), "committedCharacterCount") == 0,
                          "disabled plugin collected");
                    enable(mt::kPluginID, true);
                    Ready([&] { return Status().at("enabled").get<bool>(); });
                    main([&] { word(first); });
                    auto s = snapshot();
                    Check(Keys(s) == 6 && Sum(s, "keyCount") == 6 &&
                              Sum(s, "committedCharacterCount") == 2,
                          "ordinary Rime counts differ: " + s.dump());
                    main([&] {
                        key(first, fcitx::Key(FcitxKey_a, fcitx::KeyStates(), 38));
                        key(first, fcitx::Key(FcitxKey_a, fcitx::KeyState::Repeat, 38));
                        key(first, fcitx::Key(FcitxKey_a, fcitx::KeyStates(), 38), true);
                        tap(first, "Escape");
                    });
                    s = snapshot();
                    Check(Keys(s, "KeyA") == 3 && Sum(s, "keyCount") == 8,
                          "repeat double-counted activity");
                    main([&] { tap(first, "Control+Left"); });
                    s = snapshot();
                    Check(Keys(s) == 10 && Keys(s, "ArrowLeft") == 1 && Sum(s, "keyCount") == 8,
                          "shortcut/navigation admission differs");
                    const auto before = s;
                    main([&] {
                        ic->setCapabilityFlags(fcitx::CapabilityFlag::Password);
                        tap(first, "q");
                        ic->setCapabilityFlags(fcitx::CapabilityFlag::Sensitive);
                        tap(first, "q");
                        ic->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
                        ic->focusOut();
                        settings->focusIn();
                        word(own);
                        settings->focusOut();
                        ic->focusIn();
                    });
                    Check(snapshot() == before, "protected or companion input collected");
                    consent(false, true);
                    main([&] { word(first); });
                    s = snapshot();
                    Check(Keys(s) == Keys(before) && Sum(s, "committedCharacterCount") == 4,
                          "independent key consent");
                    consent(true, false);
                    main([&] { word(first); });
                    auto after = snapshot();
                    Check(Keys(after) == Keys(s) + 6 && after.at("activity") == s.at("activity"),
                          "independent activity consent");
                    enable(mt::kPluginID, false);
                    Ready([&] { return !Status().at("enabled").get<bool>(); });
                    main([&] { word(first); });
                    Check(snapshot() == after, "disable retained observer");
                    enable(mt::kPluginID, true);
                    Ready([&] { return Status().at("enabled").get<bool>(); });
                    Check(!Status().at("recordsTypingActivity").get<bool>(),
                          "reenable reset consent");
                    consent(true, true);
                    clear();
                    main([&] {
                        tap(first, "Control+Shift+b");
                        for (char c : std::string("nihao"))
                            tap(first, std::string(1, c));
                        tap(first, "space");
                    });
                    Check(Sum(snapshot(), "committedCharacterCount") == 2,
                          "Buffer capture not counted once");
                    main([&] {
                        frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("你好");
                        tap(first, "Return");
                    });
                    Check(Sum(snapshot(), "committedCharacterCount") == 2,
                          "Buffer resend counted again");
                    enable("builtin.fly-chord-learning", true);
                    Ready([&] {
                        auto status = Call({{"op", "status"}}, true);
                        return status.at("enabled").get<bool>() && !status.at("busy").get<bool>();
                    });
                    Call({{"op", "makeCurrent"}}, true);
                    clear();
                    auto batch = [&](const std::string& text) {
                        main([&] {
                            for (char c : text)
                                key(first, fcitx::Key(std::string(1, c)));
                            for (char c : text)
                                key(first, fcitx::Key(std::string(1, c)), true);
                        });
                        std::this_thread::sleep_for(180ms);
                    };
                    batch("dv");
                    batch("i");
                    main([&] {
                        frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("你");
                        tap(first, "space");
                    });
                    s = snapshot();
                    Check(Keys(s) == 4 && Sum(s, "chordCount") == 2 &&
                              Sum(s, "committedCharacterCount") == 1,
                          "timed batches counted synthetic replay: " + s.dump());
                    main([&] {
                        tap(first, "q");
                        ic->setCapabilityFlags(fcitx::CapabilityFlag::Password);
                    });
                    std::this_thread::sleep_for(180ms);
                    Check(Sum(snapshot(), "chordCount") == 2, "canceled chord counted");
                    main([&] { ic->setCapabilityFlags(fcitx::CapabilityFlag::Preedit); });
                    // The original native Yoyo scheme uses physical releases,
                    // unlike Fly's timed replay. Its successful batch still
                    // contributes exactly one chord and no synthetic keys.
                    const auto native = rimes::chord::Contract().at("nativeTraces").at(0);
                    const auto state = Call({{"op", "status"}}, true);
                    Call({{"op", "apply"},
                          {"revision", state.at("activeRevision")},
                          {"profile", native.at("profile")}},
                         true);
                    Ready([&] { return !Call({{"op", "status"}}, true).at("busy").get<bool>(); });
                    clear();
                    main([&] {
                        frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("吃");
                        tap(first, "q");
                    });
                    s = snapshot();
                    Check(Keys(s) == 1 && Sum(s, "chordCount") == 1 &&
                              Sum(s, "committedCharacterCount") == 1,
                          "native physical batch differs: " + s.dump());
                    const auto old_keys = snapshot().at("keys");
                    Call({{"op", "clear"}, {"collector", "activity"}});
                    Check(snapshot().at("keys") == old_keys && Sum(snapshot(), "chordCount") == 0,
                          "clear affected other collector");
                    Call({{"op", "flush"}});
                    const auto disk = mt::Store(root).Snapshot();
                    Check(Keys(disk) == Keys(snapshot()) && Sum(disk, "keyCount") == 0,
                          "explicit flush did not persist");
                    std::cout << "PASS: physical mapping, real Rime commits, repeat, protected and "
                                 "companion input, independent consent, disable/reenable, Buffer "
                                 "capture/resend, accepted/canceled chord batches, clear and "
                                 "persistence\n";
                } catch (const std::exception& error) {
                    std::cerr << "FAIL: " << error.what() << " (artifacts " << root << ")\n";
                    result = 1;
                }
                dispatcher.schedule([&] { instance.exit(); });
            });
        });
        try {
            instance.exec();
        } catch (const fcitx::InstanceQuietQuit&) {
        }
        if (driver.joinable())
            driver.join();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    if (!result)
        std::filesystem::remove_all(root);
    return result;
}
