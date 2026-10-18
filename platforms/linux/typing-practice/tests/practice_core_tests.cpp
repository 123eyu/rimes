#include "practice_protocol.hpp"
#include "typing_practice_core.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace rimes::practice;
namespace {
void Compare(const Json &actual, const Json &expected, const std::string &path) {
    if (actual.is_number() && expected.is_number()) {
        const double a = actual, b = expected;
        if (std::isfinite(a) && std::abs(a - b) <= 1e-9 * std::max(1., std::abs(b)))
            return;
    } else if (actual.is_object() && expected.is_object() && actual.size() == expected.size()) {
        for (const auto &[key, value] : expected.items()) {
            if (!actual.contains(key))
                throw std::runtime_error(path + " missing " + key);
            Compare(actual.at(key), value, path + "/" + key);
        }
        return;
    } else if (actual.is_array() && expected.is_array() && actual.size() == expected.size()) {
        for (std::size_t i = 0; i < actual.size(); ++i)
            Compare(actual[i], expected[i], path + "/" + std::to_string(i));
        return;
    } else if (actual == expected)
        return;
    throw std::runtime_error(path + " actual=" + actual.dump() + " expected=" + expected.dump());
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Native fixture path required");
        std::ifstream input(argv[1]);
        Json fixture;
        input >> fixture;
        const Json begin = {{"ok", true},
                            {"token", "session"},
                            {"context",
                             {{"schemaID", "rimes"},
                              {"inputSourceID", "fcitx5.rimes"},
                              {"chordCountingAvailable", false}}}};
        Compare(ValidReply("begin", begin), true, "valid service begin");
        for (const Json &invalid : {Json(), Json::array(), Json{{"ok", "true"}},
                                    Json{{"ok", true}, {"context", begin.at("context")}}})
            Compare(ValidReply("begin", invalid), false, "malformed begin");
        for (const auto *field : {"schemaID", "inputSourceID", "chordCountingAvailable"}) {
            auto invalid = begin;
            invalid["context"][field] = Json::array();
            Compare(ValidReply("begin", invalid), false, "typed context");
        }
        const Json state = {{"ok", true},
                            {"chords", 1},
                            {"backspaces", Json::array({{{"time", 1}, {"code", 22}}})},
                            {"reason", ""}};
        Compare(ValidReply("state", state), true, "valid service state");
        for (const Json bad : {Json(-1), Json(1.5), Json("1"), Json(1000001)}) {
            auto invalid = state;
            invalid["chords"] = bad;
            Compare(ValidReply("state", invalid), false, "bounded service counter");
        }
        auto invalid = state;
        invalid["backspaces"][0]["code"] = 65536;
        Compare(ValidReply("finish", invalid), false, "bounded keycode");
        std::size_t observations = 0;
        for (const auto &test : fixture.at("cases")) {
            Session session(test.at("article"), test.at("context"),
                            "00000000-0000-4000-8000-000000000001");
            for (const auto &step : test.at("steps")) {
                const auto &event = step.at("event");
                const std::string op = event.at("op");
                const double time = event.at("at");
                const std::string path =
                    test.at("name").get<std::string>() + "/" + std::to_string(observations++);
                if (op == "start")
                    session.Start(time);
                else if (op == "key")
                    session.Key(time, event.value("repeat", false), event.value("backspace", false),
                                event.value("composing", false));
                else if (op == "chord")
                    session.Chord(time);
                else if (op == "upgrade")
                    session.UpgradeBackspace();
                else if (op == "cancel")
                    session.Cancel();
                else if (op == "reason")
                    session.Mark(event.at("reason"));
                else if (op == "commit") {
                    std::optional<std::pair<int, int>> range;
                    if (event.contains("range"))
                        range = {event.at("range")[0], event.at("range")[1]};
                    Compare(session.Commit(event.at("text"), time, range), step.at("accepted"),
                            path + "/accepted");
                } else if (op == "finish") {
                    auto result = session.Finish(time, 1700000000);
                    Json actual;
                    if (result) {
                        actual = *result;
                        actual.erase("id");
                        actual["isComparable"] =
                            actual.at("isComplete") == true && actual.at("practiceReasons").empty();
                        actual["displaySpeed"] =
                            Rates(actual.at("metrics"))
                                .at(actual.at("language") == "english" ? "wordsPerMinute"
                                                                       : "effectiveCPM");
                    }
                    Compare(actual, step.at("result"), path + "/result");
                }
                Compare(session.Snapshot(time), step.at("snapshot"), path + "/snapshot");
            }
        }
        for (const auto &test : fixture.at("validation"))
            Compare(ValidResult(test.at("value")), test.at("valid"),
                    "validation/" + test.at("name").get<std::string>());
        std::cout << "PASS: " << fixture.at("cases").size() << " actual native cases and "
                  << observations << " observations\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
