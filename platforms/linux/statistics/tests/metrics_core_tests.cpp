#include "metrics_core.hpp"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>

using namespace rimes::metrics;
void Check(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
Json WithoutUpdate(Json value) {
    value.erase("updatedAt");
    return value;
}
int main(int argc, char** argv) {
    try {
        Check(argc == 2, "native fixture path required");
        std::ifstream input(argv[1]);
        Json fixture;
        input >> fixture;
        std::size_t observations = 0;
        for (const auto& entry : fixture.at("cases")) {
            KeyFrequency keys;
            TypingActivity activity;
            bool collect_keys = true, collect_activity = true;
            for (const auto& value : entry.at("events")) {
                const auto kind = value.at("kind").get<std::string>();
                if (kind == "boundary")
                    activity.EndSession();
                else if (kind == "restart") {
                    keys = KeyFrequency(keys.File());
                    activity = TypingActivity(activity.File());
                } else if (kind == "preferences") {
                    collect_keys = value.at("keys");
                    collect_activity = value.at("activity");
                    activity.EndSession();
                } else if (kind == "clear-day")
                    keys.Clear(value.at("day").get<std::string>(), 0);
                else if (kind == "clear-activity")
                    activity.Clear(0);
                else {
                    Event event;
                    event.timestamp = value.at("timestamp");
                    if (kind == "key") {
                        event.key = value.at("key");
                        event.repeat = value.at("repeat");
                        event.shortcut = value.at("shortcut");
                        if (collect_keys)
                            keys.Record(event, value.at("day"));
                    } else if (kind == "commit") {
                        event.kind = Event::Kind::Commit;
                        event.characters = value.at("characters");
                    } else {
                        event.kind = Event::Kind::Chord;
                        event.chord_duration = value.at("chordDuration");
                        event.handled_releases = value.at("handledReleases");
                    }
                    if (collect_activity)
                        activity.Consume(event, value.at("day"));
                    ++observations;
                }
            }
            Check(WithoutUpdate(keys.File()) == entry.at("keys"),
                  "native key aggregates: " + entry.at("id").get<std::string>());
            Check(WithoutUpdate(activity.File()) == entry.at("activity"),
                  "native activity aggregates: " + entry.at("id").get<std::string>());
            for (const auto& snapshot : entry.at("snapshots"))
                Check(keys.Snapshot(snapshot.at("dayKey")) == snapshot,
                      "native tie/order snapshot differs");
        }
        Check(DaySerial("1970-01-01") == 0 && DaySerial("2024-02-29") && !DaySerial("2023-02-29") &&
                  !DaySerial("1900-02-29") && DaySerial("2000-02-29") && !DaySerial("2026-2-03") &&
                  !DaySerial("2026-01-00"),
              "Gregorian calendar validation");
        for (int day = -719162; day <= 2932896; day += 83)
            Check(DaySerial(DayFromSerial(day)) == day, "Gregorian round trip");
        Check(DayRange("2024-03-01", 3) ==
                  std::vector<std::string>{"2024-02-28", "2024-02-29", "2024-03-01"},
              "calendar range");
        setenv("TZ", "America/New_York", 1);
        tzset();
        Check(LocalDay(1710053999) == "2024-03-10" && LocalDay(1710054000) == "2024-03-10",
              "DST local day");
        Check(DayRange("2024-03-11", 3) ==
                  std::vector<std::string>{"2024-03-09", "2024-03-10", "2024-03-11"},
              "DST range does not skip a day");
        Check(LocalDay(std::numeric_limits<double>::infinity()).empty() && LocalDay(-1).empty(),
              "invalid local time");
        TypingActivity activity;
        Event key;
        key.key = "KeyA";
        key.timestamp = 1;
        Check(activity.Consume(key, "2026-01-01"), "first key");
        key.timestamp = 11;
        activity.Consume(key, "2026-01-01");
        key.timestamp = 22;
        activity.Consume(key, "2026-01-01");
        Check(activity.File().at("sessions").size() == 2 &&
                  activity.File().at("days").at("2026-01-01").at("activeSeconds") == 10,
              "exact inactivity boundary");
        for (int n = 0; n != 510; ++n) {
            key.timestamp += 11;
            activity.Consume(key, "2026-01-01");
        }
        Check(activity.File().at("sessions").size() == 500 &&
                  activity.File().at("days").at("2026-01-01").at("sessionCount") == 512,
              "bounded sessions keep daily total");
        auto limited = activity.File();
        limited["days"]["2026-01-01"]["keyCount"] = kMaximumCounter;
        TypingActivity saturated(limited);
        saturated.Consume(key, "2026-01-01");
        Check(saturated.File().at("days").at("2026-01-01").at("keyCount") == kMaximumCounter,
              "saturating activity counters");
        const auto before = saturated.File();
        key.timestamp = std::numeric_limits<double>::quiet_NaN();
        Check(!saturated.Consume(key, "2026-01-01") && saturated.File() == before,
              "invalid timestamps never mutate history");
        Check(!ValidActivityFile(Json::array()), "non-object store rejected");
        for (const auto& wrong :
             {Json(-1), Json(1000000001LL), Json(2.5), Json("2"), Json(nullptr)}) {
            auto corrupt = limited;
            corrupt["days"]["2026-01-01"]["keyCount"] = wrong;
            Check(!ValidActivityFile(corrupt), "invalid counters rejected");
        }
        auto corrupt = limited;
        corrupt["version"] = 2;
        Check(!ValidActivityFile(corrupt), "future versions preserved");
        corrupt = limited;
        corrupt["sessions"][0]["endedAt"] = -1;
        Check(!ValidActivityFile(corrupt), "invalid sessions rejected");
        KeyFrequency keys;
        key.timestamp = 1;
        for (int n = 0; n != 512; ++n) {
            key.key = "fixture-" + std::to_string(n);
            Check(keys.Record(key, "2026-01-01"), "512 distinct keys");
        }
        key.key = "overflow";
        Check(!keys.Record(key, "2026-01-01"), "bounded key cardinality");
        Check(!keys.Record(key, "2200-01-01"), "bounded calendar span");
        std::cout
            << "PASS: " << observations
            << " native observations, independent consent, restart/clear, "
               "repeats/shortcuts/navigation, session/calendar boundaries and bounded counters\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
