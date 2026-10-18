#include "chord_protocol.hpp"
#include "chord_store.hpp"
#include "configuration.hpp"
#include "official_catalog.hpp"
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fstream>
#include <future>
#include <gio/gio.h>
#include <iostream>
#include <testfrontend_public.h>
#include <thread>
#include <unistd.h>

namespace rt = rimes::runtime;
namespace ch = rimes::chord;
using namespace std::chrono_literals;
void Check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
ch::Json Call(const ch::Json& command) {
    GError* error = nullptr;
    auto* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    g_clear_error(&error);
    Check(bus, "test requires a private dbus-run-session");
    auto* reply = g_dbus_connection_call_sync(
        bus, ch::kBusName, ch::kBusPath, ch::kBusInterface, "Command",
        g_variant_new("(s)", command.dump().c_str()), G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, nullptr, &error);
    g_object_unref(bus);
    if (!reply) {
        std::string message = error ? error->message : "Chord service unavailable";
        g_clear_error(&error);
        throw std::runtime_error(message);
    }
    const char* value = nullptr;
    g_variant_get(reply, "(&s)", &value);
    const std::string bytes(value);
    g_variant_unref(reply);
    auto result = ch::Json::parse(bytes);
    Check(result.value("ok", false), result.value("message", std::string("Chord command failed")));
    return result;
}
ch::Json Ready(bool enabled) {
    const auto deadline = std::chrono::steady_clock::now() + 240s;
    std::string reason;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            auto result = Call({{"op", "status"}});
            if (!result.at("busy").get<bool>() && result.at("phase") != "starting" &&
                result.at("enabled") == enabled) {
                Check(!enabled || result.at("available").get<bool>(),
                      result.value("message", std::string("unavailable")));
                return result;
            }
            reason = result.dump();
        } catch (const std::exception& error) {
            reason = error.what();
        }
        std::this_thread::sleep_for(50ms);
    }
    throw std::runtime_error("Chord service deadline: " + reason);
}
int main(int argc, char** argv) {
    if (argc != 4)
        return 2;
    char temporary[] = "/tmp/xsime-chord-fcitx-XXXXXX";
    if (!mkdtemp(temporary))
        return 1;
    const std::filesystem::path root(temporary);
    int result = 0;
    try {
        for (const auto* name : {"RIMES_USER_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME",
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
        auto enable = [&](bool enabled) {
            auto installed = packages.Inspect(ch::kPluginID);
            if (!installed.installed)
                packages.Install(ch::kPluginID, bundled.at(ch::kPluginID), installed.grant);
            packages.Enable(ch::kPluginID, enabled, packages.Inspect(ch::kPluginID).grant);
        };
        enable(true);
        fcitx::setupTestingEnvironment(argv[1], {argv[2]}, {argv[3]});
        char name[] = "chord-fcitx-e2e", disabled[] = "--disable=all",
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
            Check(done.wait_for(3s) == std::future_status::ready, "Fcitx input loop blocked");
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
            Check(frontend, "testfrontend missing");
            const auto first = frontend->call<fcitx::ITestFrontend::createInputContext>("first");
            const auto second = frontend->call<fcitx::ITestFrontend::createInputContext>("second");
            auto* ic = instance.inputContextManager().findByUUID(first);
            auto* other = instance.inputContextManager().findByUUID(second);
            for (auto* input : {ic, other}) {
                instance.setCurrentInputMethod(input, "rimes", true);
                input->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
            }
            ic->focusIn();
            driver = std::thread([&, frontend, first, second, ic, other] {
                auto key = [&](const fcitx::ICUUID& uuid, const std::string& name, bool release) {
                    frontend->call<fcitx::ITestFrontend::keyEvent>(uuid, fcitx::Key(name), release);
                };
                auto tap = [&](const fcitx::ICUUID& uuid, const std::string& name) {
                    key(uuid, name, false);
                    key(uuid, name, true);
                };
                auto batch = [&](const std::string& keys) {
                    main([&] {
                        for (char c : keys)
                            key(first, std::string(1, c), false);
                        for (char c : keys)
                            key(first, std::string(1, c), true);
                    });
                    std::this_thread::sleep_for(180ms);
                };
                auto expect = [&](const std::string& text) {
                    main([&] {
                        const auto list = ic->inputPanel().candidateList();
                        Check(list && list->size() > 0, "no candidates for " + text);
                        const auto first_candidate = list->candidate(0).text().toStringForCommit();
                        Check(first_candidate == text,
                              "candidate differs: " + first_candidate + " expected " + text);
                        frontend->call<fcitx::ITestFrontend::pushCommitExpectation>(text);
                        tap(first, "space");
                    });
                };
                try {
                    auto status = Ready(true);
                    Call({{"op", "makeCurrent"}});
                    batch("dv");
                    batch("i");
                    expect("你");
                    batch("q");
                    batch("km");
                    expect("穷");
                    main([&] {
                        tap(first, "q");
                        Check(ic->inputPanel().clientPreedit().empty(),
                              "pending batch leaked client preedit");
                        ic->focusOut();
                        other->focusIn();
                    });
                    std::this_thread::sleep_for(180ms);
                    main([&] {
                        Check(other->inputPanel().clientPreedit().empty() &&
                                  ic->inputPanel().clientPreedit().empty(),
                              "focus cancellation leaked text");
                        other->focusOut();
                        ic->focusIn();
                    });
                    ch::Store store(root);
                    auto custom =
                        ch::Profile::Parse(ch::Contract().at("compilers").at(1).at("profile"));
                    store.Save(custom, "");
                    Check(store.Active().profile.id == "builtin.flyyao",
                          "draft changed live profile");
                    status = Call({{"op", "status"}});
                    Call({{"op", "apply"},
                          {"revision", status.at("activeRevision")},
                          {"profile", custom.Value()}});
                    Ready(true);
                    Check(store.Active().profile.Value() == custom.Value(),
                          "verified custom profile not published");
                    batch("cd");
                    batch("ae");
                    expect("你好");
                    main([&] { tap(first, "q"); });
                    enable(false);
                    Ready(false);
                    main([&] {
                        for (char c : std::string("nihao"))
                            tap(first, std::string(1, c));
                    });
                    expect("你好");
                    enable(true);
                    Ready(true);
                    Call({{"op", "makeCurrent"}});
                    batch("cd");
                    expect("你");
                    // Corrupt only the generated mapping in the schema compiler input
                    // while preserving the last verified snapshot: invalid apply is rejected.
                    auto invalid = custom.Value();
                    invalid["mappings"][0]["output"] = "BAD";
                    status = Call({{"op", "status"}});
                    bool rejected = false;
                    try {
                        Call({{"op", "apply"},
                              {"revision", status.at("activeRevision")},
                              {"profile", invalid}});
                    } catch (...) {
                        rejected = true;
                    }
                    Check(rejected && store.Active().profile.Value() == custom.Value(),
                          "invalid apply replaced active snapshot");
                    auto read = [](const std::filesystem::path& path) {
                        std::ifstream stream(path);
                        return std::string((std::istreambuf_iterator<char>(stream)), {});
                    };
                    const auto before_deploy = read(root / "default.custom.yaml");
                    auto broken = custom;
                    broken.id = "00000000-0000-4000-8000-000000000003";
                    const std::string patch =
                        "patch:\n  chord_composer/algebra:\n    - 'xform/.*/bad/'\n";
                    const auto patch_path = root / (broken.SchemaID() + ".custom.yaml");
                    std::ofstream(patch_path) << patch;
                    status = Call({{"op", "status"}});
                    Call({{"op", "apply"},
                          {"revision", status.at("activeRevision")},
                          {"profile", broken.Value()}});
                    status = Ready(true);
                    Check(status.at("phase") == "rolled-back" &&
                              store.Active().profile.Value() == custom.Value(),
                          "actual post-deploy mapping failure did not restore old profile");
                    Check(read(root / "default.custom.yaml") == before_deploy &&
                              read(patch_path) == patch &&
                              !std::filesystem::exists(root / (broken.SchemaID() + ".schema.yaml")),
                          "rollback did not preserve exact user files");
                    batch("cd");
                    expect("你");
                    main([&] {
                        tap(first, "q");
                        ic->setCapabilityFlags(fcitx::CapabilityFlag::Password);
                    });
                    std::this_thread::sleep_for(180ms);
                    main([&] {
                        Check(ic->inputPanel().clientPreedit().empty() &&
                                  ic->inputPanel().preedit().empty(),
                              "protected field retained staged keys");
                        ic->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
                        key(first, "c", false);
                        key(first, "d", false);
                        key(first, "c", true);
                        key(first, "d", true);
                        frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("你");
                        tap(first, "space");
                    });
                    for (const auto& fixture : ch::Contract().at("nativeTraces")) {
                        const auto profile = ch::Profile::Parse(fixture.at("profile"));
                        status = Call({{"op", "status"}});
                        Call({{"op", "apply"},
                              {"revision", status.at("activeRevision")},
                              {"profile", profile.Value()}});
                        Ready(true);
                        for (const auto& trace : fixture.at("traces")) {
                            main([&] {
                                tap(first, "Escape");
                                const auto commit = trace.at("commit").get<std::string>();
                                if (!commit.empty())
                                    frontend->call<fcitx::ITestFrontend::pushCommitExpectation>(
                                        commit);
                                for (const auto& value : trace.at("chords")) {
                                    const auto keys = value.get<std::string>();
                                    for (char c : keys)
                                        key(first, c == ' ' ? "space" : std::string(1, c), false);
                                    for (auto c = keys.rbegin(); c != keys.rend(); ++c)
                                        key(first, *c == ' ' ? "space" : std::string(1, *c), true);
                                }
                                if (trace.contains("firstCandidate")) {
                                    const auto list = ic->inputPanel().candidateList();
                                    Check(list && list->size() &&
                                              list->candidate(0).text().toStringForCommit() ==
                                                  trace.at("firstCandidate"),
                                          "native Fcitx candidate differs: " + profile.SchemaID());
                                }
                                tap(first, "Escape");
                            });
                        }
                        const auto second_key = profile.native_schema == "yoyo-yx" ? "w" : "f";
                        main([&] {
                            key(first, "q", false);
                            key(first, second_key, false);
                            key(first, "q", false);
                        });
                        std::this_thread::sleep_for(180ms);
                        main([&] {
                            // No commit expectation before the last release: an early
                            // timed settlement or duplicate press would fail the frontend.
                            key(first, "q", true);
                            frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("车");
                            key(first, second_key, true);
                            key(first, "q", false);
                            Check(ic->inputPanel().clientPreedit().empty(),
                                  "held native chord exposed a client preedit");
                            ic->focusOut();
                            other->focusIn();
                        });
                        std::this_thread::sleep_for(180ms);
                        main([&] {
                            other->focusOut();
                            ic->focusIn();
                            Check(ic->inputPanel().clientPreedit().empty(),
                                  "native held key leaked after focus-out");
                            frontend->call<fcitx::ITestFrontend::pushCommitExpectation>("吃");
                            tap(first, "q");
                        });
                    }
                    std::cout << "PASS: actual Fcitx timed batches, cross-batch pairing, focus "
                                 "cancellation, custom deployment, draft isolation, "
                                 "disable/reenable, protected fields, immediate settlement, "
                                 "post-deploy rollback, ordinary fallback and native physical "
                                 "chord traces\n";
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
