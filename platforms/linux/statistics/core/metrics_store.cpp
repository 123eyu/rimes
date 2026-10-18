#include "metrics_store.hpp"
#include <chrono>

namespace rimes::metrics {
Store::Store(const std::filesystem::path& root, bool writer) : files_(root / "stats") {
    if (writer)
        writer_ = std::make_unique<runtime::FileLock>(files_.Lock(false));
    for (auto* file : {&keys_file_, &activity_file_}) {
        try {
            file->bytes = files_.Read(file->name, kMaximumFileBytes);
            const auto value = file->bytes ? Json::parse(*file->bytes) : Json();
            if (file == &keys_file_)
                keys_ = KeyFrequency(value);
            else
                activity_ = TypingActivity(value);
        } catch (const std::exception& error) {
            file->issue = error.what();
        }
    }
}
double Store::Now() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
void Store::Consume(const Event& event, bool keys, bool activity) {
    if (!writer_)
        throw std::runtime_error("统计存储为只读");
    const auto day = LocalDay(event.timestamp);
    if (keys && keys_file_.issue.empty())
        keys_file_.dirty |= keys_.Record(event, day);
    if (activity && activity_file_.issue.empty())
        activity_file_.dirty |= activity_.Consume(event, day);
}
void Store::EndSession() { activity_.EndSession(); }
void Store::Save(File& file, Json value) {
    if (!file.dirty || !file.issue.empty())
        return;
    try {
        if (!writer_)
            throw std::runtime_error("统计存储为只读");
        if (files_.Read(file.name, kMaximumFileBytes) != file.bytes)
            throw std::runtime_error(
                "统计文件已被外部修改；已停止写入并保留原路径，请重新启动输入法后读取");
        value["updatedAt"] = Now();
        const auto bytes = value.dump(2) + '\n';
        if (bytes.size() > kMaximumFileBytes)
            throw std::runtime_error("统计文件已达到大小限制；已保留原记录");
        files_.Write(file.name, bytes);
        file.bytes = bytes;
        file.dirty = false;
    } catch (const std::exception& error) {
        file.issue = error.what();
    }
}
void Store::Flush() {
    if (keys_file_.dirty && keys_file_.issue.empty())
        Save(keys_file_, keys_.File());
    if (activity_file_.dirty && activity_file_.issue.empty())
        Save(activity_file_, activity_.File());
}
bool Store::Pending() const {
    return (keys_file_.dirty && keys_file_.issue.empty()) ||
           (activity_file_.dirty && activity_file_.issue.empty());
}
Json Store::Snapshot() const {
    return {{"formatVersion", 1},
            {"keys", keys_.File()},
            {"activity", activity_.File()},
            {"issues", {{"keys", keys_file_.issue}, {"activity", activity_file_.issue}}}};
}
Store::File& Store::Select(const std::string& collector) {
    if (!writer_)
        throw std::runtime_error("统计存储为只读");
    if (collector == "keys")
        return keys_file_;
    if (collector == "activity")
        return activity_file_;
    throw std::runtime_error("未知统计类别");
}
void Store::Clear(const std::string& collector, const std::optional<std::string>& day) {
    auto& file = Select(collector);
    if (!file.issue.empty())
        throw std::runtime_error("存储只读；请先备份并重建");
    if (day && (!DaySerial(*day) || collector != "keys"))
        throw std::runtime_error("无效的统计日期");
    if (collector == "keys")
        keys_.Clear(day, Now());
    else
        activity_.Clear(Now());
    file.dirty = true;
    Flush();
    if (!file.issue.empty())
        throw std::runtime_error(file.issue);
}
std::string Store::Repair(const std::string& collector) {
    auto& file = Select(collector);
    if (file.issue.empty())
        return {};
    const auto backup = files_.Preserve(file.name);
    if (collector == "keys")
        keys_ = KeyFrequency();
    else
        activity_ = TypingActivity();
    file.bytes.reset();
    file.issue.clear();
    file.dirty = true;
    Flush();
    if (!file.issue.empty())
        throw std::runtime_error(file.issue);
    return backup;
}
} // namespace rimes::metrics
