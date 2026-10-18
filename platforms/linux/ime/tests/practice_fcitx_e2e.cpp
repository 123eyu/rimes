#include "chord_core.hpp"
#include "configuration.hpp"
#include "official_catalog.hpp"
#include "practice_protocol.hpp"
#include "practice_telemetry.hpp"
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

using Json = rimes::runtime::Json;
namespace rt = rimes::runtime;
namespace pt = rimes::practice;
using namespace std::chrono_literals;
void Check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
Json Call(Json command, int channel = 0, GDBusConnection *supplied = nullptr) {
    GError *error = nullptr;
    struct Connection {
        GDBusConnection *value = nullptr;
        ~Connection() {
            if (value)
                g_object_unref(value);
        }
    };
    static Connection held;
    if (!supplied && !held.value)
        held.value = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    auto *bus = supplied     ? G_DBUS_CONNECTION(g_object_ref(supplied))
                : held.value ? G_DBUS_CONNECTION(g_object_ref(held.value))
                             : nullptr;
    g_clear_error(&error);
    Check(bus, "private bus required");
    const char *names[] = {pt::kBusName, "org.scholay.XSIME.Chord", "org.scholay.XSIME.Statistics"};
    const char *paths[] = {pt::kBusPath, "/org/scholay/XSIME/Chord",
                           "/org/scholay/XSIME/Statistics"};
    const char *interfaces[] = {pt::kBusInterface, "org.scholay.XSIME.Chord1",
                                "org.scholay.XSIME.Statistics1"};
    auto *reply = g_dbus_connection_call_sync(
        bus, names[channel], paths[channel], interfaces[channel], "Command",
        g_variant_new("(s)", command.dump().c_str()), G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, nullptr, &error);
    g_object_unref(bus);
    if (!reply) {
        const std::string message = error ? error->message : "service unavailable";
        g_clear_error(&error);
        throw std::runtime_error(message);
    }
    const char *bytes = nullptr;
    g_variant_get(reply, "(&s)", &bytes);
    const auto value = Json::parse(bytes);
    g_variant_unref(reply);
    return value;
}
Json OK(Json command, int channel = 0) {
    auto value = Call(std::move(command), channel);
    Check(value.value("ok", false), value.value("message", "command failed"));
    return value;
}
void Ready(const std::function<bool()> &ready) {
    const auto until = std::chrono::steady_clock::now() + 240s;
    std::string error;
    while (std::chrono::steady_clock::now() < until) {
        try {
            if (ready())
                return;
        } catch (const std::exception &e) {
            error = e.what();
        }
        std::this_thread::sleep_for(30ms);
    }
    throw std::runtime_error("practice deadline: " + error);
}
int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    char temporary[] = "/tmp/xsime-practice-fcitx-XXXXXX";
    if (!mkdtemp(temporary))
        return 1;
    const std::filesystem::path root(temporary);
    int result = 0;
    try {
        for (const auto *name : {"RIMES_USER_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME",
                                 "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"})
            setenv(name, temporary, 1);
        setenv("RIMES_LOG_DIR", (root / "log").c_str(), 1);
        setenv("RIMES_BUFFER_HEADLESS", "1", 1);
        setenv("RIMES_CAPSULE_HEADLESS", "1", 1);
        setenv("RIMES_BUFFER_AUTO_CAPTURE", "0", 1);
        rt::ConfigurationStore configuration(root / "configuration");
        auto config = configuration.Load();
        config["gatewayEnabled"] = false;
        configuration.Save(config, config.at("revision"));
        const auto bundled = rimes::plugins::BundledPackages();
        rimes::plugins::PackageStore packages(root / "plugins", rimes::plugins::OfficialCatalog(),
                                              bundled, rimes::plugins::CompiledAdapters(),
                                              RIMES_PRODUCT_VERSION);
        auto enable = [&](const std::string &id, bool enabled) {
            auto installed = packages.Inspect(id);
            if (!installed.installed)
                packages.Install(id, bundled.at(id), installed.grant);
            packages.Enable(id, enabled, packages.Inspect(id).grant);
        };
        enable(pt::kPluginID, false);
        fcitx::setupTestingEnvironment(argv[1], {argv[2]}, {argv[3]});
        char name[] = "practice-fcitx-e2e", disabled[] = "--disable=all",
             enabled[] = "--enable=testfrontend,testui,rimes";
        char *args[] = {name, disabled, enabled};
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
            Check(done.wait_for(3s) == std::future_status::ready, "practice blocked input loop");
            done.get();
        };
        dispatcher.schedule([&] {
            auto &manager = instance.inputMethodManager();
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
            auto *frontend = instance.addonManager().addon("testfrontend", true);
            Check(frontend, "frontend missing");
            const auto first =
                frontend->call<fcitx::ITestFrontend::createInputContext>("rimes-settings");
            const auto second =
                frontend->call<fcitx::ITestFrontend::createInputContext>("rimes-settings");
            const auto external =
                frontend->call<fcitx::ITestFrontend::createInputContext>("external-editor");
            auto *own = instance.inputContextManager().findByUUID(first);
            auto *other = instance.inputContextManager().findByUUID(second);
            auto *outside = instance.inputContextManager().findByUUID(external);
            for (auto *ic : {own, other, outside}) {
                instance.setCurrentInputMethod(ic, "rimes", true);
                ic->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
            }
            own->focusIn();
            driver = std::thread([&, frontend, first, own, other, outside] {
                auto key = [&](fcitx::Key value, bool release = false, int time = 0) {
                    fcitx::KeyEvent event(own, value, release, time);
                    own->keyEvent(event);
                };
                auto tap = [&](const std::string &value) {
                    key(fcitx::Key(value));
                    key(fcitx::Key(value), true);
                };
                std::string token;
                auto begin = [&] {
                    auto response = OK({{"op", "begin"}});
                    token = response.at("token");
                    return response;
                };
                auto state = [&] { return OK({{"op", "state"}, {"token", token}}); };
                auto batch = [&](const std::string &value) {
                    main([&] {
                        for (char c : value)
                            key(fcitx::Key(std::string(1, c)));
                        for (char c : value)
                            key(fcitx::Key(std::string(1, c)), true);
                    });
                    std::this_thread::sleep_for(180ms);
                };
                try {
                    Ready([&] {
                        auto s = OK({{"op", "status"}}, 1);
                        return !s.at("busy").get<bool>() && s.at("phase") != "starting";
                    });
                    Check(!Call({{"op", "begin"}}).value("ok", false),
                          "disabled practice granted a lease");
                    enable(pt::kPluginID, true);
                    enable("builtin.statistics", true);
                    Ready([&] { return Call({{"op", "begin"}}).value("ok", false); });
                    auto start = begin();
                    Check(!start.at("context").at("chordCountingAvailable").get<bool>(),
                          "ordinary schema invented chord availability");
                    main([&] {
                        tap("t");
                        key(fcitx::Key(FcitxKey_BackSpace, fcitx::KeyStates(), 22), false, 1234);
                        key(fcitx::Key(FcitxKey_BackSpace, fcitx::KeyStates(), 22), true, 1234);
                        tap("Escape");
                    });
                    auto snapshot = state();
                    Check(snapshot.at("chords") == 0 && snapshot.at("backspaces").size() == 1 &&
                              snapshot.at("backspaces")[0].at("time") == 1234 &&
                              snapshot.at("backspaces")[0].at("code") == 22,
                          "actual composition backspace not correlated");
                    const auto stats = OK({{"op", "status"}}, 2).at("snapshot");
                    Check(stats.at("keys").at("days").empty() &&
                              stats.at("activity").at("days").empty(),
                          "practice entered daily metrics");
                    // Another same-uid bus peer cannot read or finish this token.
                    GError *error = nullptr;
                    auto *peer = g_dbus_connection_new_for_address_sync(
                        g_getenv("DBUS_SESSION_BUS_ADDRESS"),
                        static_cast<GDBusConnectionFlags>(
                            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                        nullptr, nullptr, &error);
                    Check(peer, "peer connection unavailable");
                    Check(!Call({{"op", "state"}, {"token", token}}, 0, peer).value("ok", false),
                          "token accepted a different sender");
                    g_dbus_connection_close_sync(peer, nullptr, nullptr);
                    g_object_unref(peer);
                    g_clear_error(&error);
                    main([&] {
                        own->focusOut();
                        other->focusIn();
                    });
                    Check(state().at("reason") == "focusLost",
                          "same application field inherited lease");
                    main([&] {
                        other->focusOut();
                        outside->focusIn();
                    });
                    Check(!Call({{"op", "begin"}}).value("ok", false),
                          "external editor armed practice");
                    main([&] {
                        outside->focusOut();
                        own->focusIn();
                    });
                    enable("builtin.fly-chord-learning", true);
                    Ready([&] {
                        auto s = OK({{"op", "status"}}, 1);
                        return s.at("enabled").get<bool>() && !s.at("busy").get<bool>();
                    });
                    OK({{"op", "makeCurrent"}}, 1);
                    main([&] { tap("Escape"); });
                    start = begin();
                    Check(start.at("context").at("chordCountingAvailable") &&
                              start.at("context").contains("keymapRevision"),
                          "chord context omitted identity");
                    batch("dv");
                    batch("i");
                    main([&] {
                        frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("你");
                        tap("space");
                    });
                    Check(state().at("chords") == 2,
                          "accepted timed batches not counted exactly once");
                    const auto finished = OK({{"op", "finish"}, {"token", token}});
                    batch("d");
                    Check(finished.at("chords") == 2 && state().at("chords") == 2,
                          "finished practice continued collecting");
                    main([&] { tap("Escape"); });
                    begin();
                    main([&] {
                        tap("q");
                        own->setCapabilityFlags(fcitx::CapabilityFlag::Password);
                    });
                    std::this_thread::sleep_for(180ms);
                    Check(state().at("chords") == 0 &&
                              !state().at("reason").get<std::string>().empty(),
                          "protected cancellation counted a chord");
                    Check(!Call({{"op", "begin"}}).value("ok", false), "protected context armed");
                    main([&] {
                        own->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
                        tap("Escape");
                    });
                    begin();
                    enable(pt::kPluginID, false);
                    Ready([&] { return !state().at("reason").get<std::string>().empty(); });
                    batch("d");
                    Check(state().at("chords") == 0, "disabled plugin retained observer");
                    enable(pt::kPluginID, true);
                    main([&] { tap("Escape"); });
                    Ready([&] { return Call({{"op", "begin"}}).value("ok", false); });
                    begin();
                    const auto before_config = state();
                    const auto native = rimes::chord::Contract().at("nativeTraces").at(0);
                    const auto chord_state = OK({{"op", "status"}}, 1);
                    OK({{"op", "apply"},
                        {"revision", chord_state.at("activeRevision")},
                        {"profile", native.at("profile")}},
                       1);
                    Ready([&] { return !OK({{"op", "status"}}, 1).at("busy").get<bool>(); });
                    Check(!state().at("reason").get<std::string>().empty() &&
                              state().at("chords") == before_config.at("chords"),
                          "configuration change crossed comparison identity");
                    main([&] { tap("Escape"); });
                    begin();
                    main([&] {
                        frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("吃");
                        tap("q");
                    });
                    Check(state().at("chords") == 1, "native Yoyo release not counted");
                    // Stop polling as a crashed UI would. The finite lease expires.
                    std::this_thread::sleep_for(3200ms);
                    Check(!state().at("reason").get<std::string>().empty(),
                          "abandoned lease did not expire");
                    Check(OK({{"op", "status"}}, 2).at("snapshot") == stats,
                          "practice polluted passive statistics");
                    // Exercise the production asynchronous client on its own
                    // GLib context, including the final read and release.
                    auto *client_context = g_main_context_new();
                    g_main_context_push_thread_default(client_context);
                    {
                        rimes::desktop::PracticeTelemetry client;
                        auto await =
                            [&](const std::function<void(std::function<void(Json)>)> &request) {
                                std::optional<Json> response;
                                request([&](Json value) { response = std::move(value); });
                                Ready([&] {
                                    while (g_main_context_iteration(client_context, FALSE)) {
                                    }
                                    return response.has_value();
                                });
                                Check(response->value("ok", false),
                                      "production telemetry client failed");
                                return *response;
                            };
                        const auto context = await([&](auto done) { client.Begin(done); });
                        Check(client.Active() && context.at("context").at("chordCountingAvailable"),
                              "production client failed to freeze context");
                        main([&] {
                            frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("吃");
                            tap("q");
                        });
                        Check(await([&](auto done) { client.Read(false, done); }).at("chords") == 1,
                              "production client lost its sender between reads");
                        Check(await([&](auto done) { client.Read(true, done); }).at("chords") == 1,
                              "production client did not drain final count");
                        client.End();
                        Check(!client.Active(), "production client retained ended token");
                    }
                    g_main_context_pop_thread_default(client_context);
                    g_main_context_unref(client_context);
                    std::cout
                        << "PASS: real Fcitx ordinary/chord identities, composition backspace, "
                           "sender/context-bound lease, daily isolation, accepted Fly/Yoyo "
                           "batches, finish barrier, focus/protection/revocation/configuration and "
                           "abandoned-UI expiry, production asynchronous telemetry client\n";
                } catch (const std::exception &error) {
                    std::cerr << "FAIL: " << error.what() << " (artifacts " << root << ")\n";
                    result = 1;
                }
                dispatcher.schedule([&] { instance.exit(); });
            });
        });
        try {
            instance.exec();
        } catch (const fcitx::InstanceQuietQuit &) {
        }
        if (driver.joinable())
            driver.join();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    if (!result)
        std::filesystem::remove_all(root);
    return result;
}
