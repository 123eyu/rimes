#include "chord_learning.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rimes::chord {
namespace {
void Check(bool value) {
    if (!value)
        throw std::runtime_error("并击学习进度文件无效；原文件已保留");
}
std::int64_t Count(const Json& value) {
    Check(value.is_number_integer() && value >= 0 &&
          value <= std::numeric_limits<std::int64_t>::max());
    return value.get<std::int64_t>();
}
std::int64_t Increment(std::int64_t count) {
    return count == std::numeric_limits<std::int64_t>::max() ? count : count + 1;
}
} // namespace
ProgressStore::ProgressStore(const std::filesystem::path& root, const std::string& schema)
    : files_(root / "learning"), schema_(schema), filename_(schema + "_progress.json") {
    Check(!schema.empty() && schema.size() <= 96 &&
          schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos);
}
bool ProgressStore::ValidID(const std::string& id) const {
    const auto prefix = schema_ + ".rule.";
    return id.compare(0, prefix.size(), prefix) == 0 && id.size() == prefix.size() + 16 &&
           id.substr(prefix.size()).find_first_not_of("0123456789abcdefABCDEF") ==
               std::string::npos;
}
void ProgressStore::Validate(const Json& value) const {
    Check(value.is_object() && value.at("version") == 1 && value.at("schemaID") == schema_ &&
          value.at("updatedAt").is_number() && std::isfinite(value.at("updatedAt").get<double>()) &&
          value.at("items").is_object() && value.at("items").size() <= 4096);
    for (const auto& [id, entry] : value.at("items").items()) {
        Check(ValidID(id));
        const auto attempts = Count(entry.at("attempts")),
                   correct = Count(entry.at("correctAttempts")),
                   streak = Count(entry.at("currentStreak")), best = Count(entry.at("bestStreak"));
        Check(correct <= attempts && streak <= correct && best >= streak && best <= correct &&
              entry.at("updatedAt").is_number() &&
              std::isfinite(entry.at("updatedAt").get<double>()));
    }
}
Json ProgressStore::Load() const {
    const auto bytes = files_.Read(filename_, 1048576);
    auto value = bytes ? Json::parse(*bytes)
                       : Json{{"version", 1},
                              {"schemaID", schema_},
                              {"items", Json::object()},
                              {"updatedAt", 0}};
    Validate(value);
    return value;
}
void ProgressStore::Save(const Json& value) const {
    Validate(value);
    const auto bytes = value.dump(2) + '\n';
    Check(bytes.size() <= 1048576);
    files_.Write(filename_, bytes);
}
Json ProgressStore::Record(const std::string& mapping, bool correct, double timestamp) {
    Check(ValidID(mapping) && std::isfinite(timestamp));
    const auto lock = files_.Lock(false);
    auto value = Load();
    auto& items = value["items"];
    Check(items.contains(mapping) || items.size() < 4096);
    auto entry = items.value(mapping, Json{{"attempts", 0},
                                           {"correctAttempts", 0},
                                           {"currentStreak", 0},
                                           {"bestStreak", 0},
                                           {"updatedAt", timestamp}});
    entry["attempts"] = Increment(Count(entry.at("attempts")));
    if (correct) {
        entry["correctAttempts"] = Increment(Count(entry.at("correctAttempts")));
        entry["currentStreak"] = Increment(Count(entry.at("currentStreak")));
        entry["bestStreak"] =
            std::max(Count(entry.at("bestStreak")), Count(entry.at("currentStreak")));
    } else
        entry["currentStreak"] = 0;
    entry["updatedAt"] = timestamp;
    items[mapping] = entry;
    value["updatedAt"] = timestamp;
    Save(value);
    return value;
}
Json ProgressStore::Clear(double timestamp) {
    const auto lock = files_.Lock(false);
    auto value = Load();
    value["items"] = Json::object();
    value["updatedAt"] = timestamp;
    Save(value);
    return value;
}
Json CourseProgress(const Course& course, const Json& items) {
    Json result = {{"total", course.mappings.size()},
                   {"attempted", 0},
                   {"mastered", 0},
                   {"attempts", 0.0},
                   {"correct", 0.0}};
    for (const auto& entry : course.mappings)
        if (items.contains(entry.id)) {
            const auto& item = items.at(entry.id);
            result["attempted"] = result["attempted"].get<unsigned>() + (item.at("attempts") != 0);
            result["mastered"] = result["mastered"].get<unsigned>() +
                                 (item.at("bestStreak").get<std::uint64_t>() >= 3);
            result["attempts"] =
                result["attempts"].get<double>() + item.at("attempts").get<double>();
            result["correct"] =
                result["correct"].get<double>() + item.at("correctAttempts").get<double>();
        }
    return result;
}
} // namespace rimes::chord
