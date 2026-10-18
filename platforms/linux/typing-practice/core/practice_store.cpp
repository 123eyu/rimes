#include "practice_store.hpp"
#include <algorithm>
#include <set>

namespace rimes::practice {
namespace {
constexpr const char *kFile = "typing_tests.json";
void Sort(Json &rows) {
    std::sort(rows.begin(), rows.end(), [](const Json &a, const Json &b) {
        return a.at("completedAt") == b.at("completedAt")
                   ? a.at("id") < b.at("id")
                   : a.at("completedAt") > b.at("completedAt");
    });
}
} // namespace
Json HistoryStore::NormalizeFile(const Json &file) {
    if (!file.is_object() || file.at("version") != 1 || !file.at("results").is_array() ||
        file.at("results").size() > kMaximumResults)
        throw std::runtime_error("跟打成绩结构超出范围");
    Json rows = Json::array();
    std::set<std::string> identities;
    for (const auto &entry : file.at("results")) {
        auto row = NormalizeResult(entry);
        if (!identities.insert(row.at("id").get<std::string>()).second)
            throw std::runtime_error("跟打成绩身份重复");
        rows.push_back(std::move(row));
    }
    Sort(rows);
    return {{"version", 1}, {"results", rows}};
}
HistoryStore::HistoryStore(const std::filesystem::path &root, bool writer)
    : files_(root / "stats") {
    if (writer)
        writer_ = std::make_unique<runtime::FileLock>(files_.Lock(false, ".typing-tests.lock"));
    try {
        identity_ = files_.Identity(kFile);
        bytes_ = files_.Read(kFile, kMaximumHistoryBytes);
        if (identity_ != files_.Identity(kFile))
            throw std::runtime_error("成绩文件在读取时已改变");
        if (bytes_)
            file_ = NormalizeFile(Json::parse(*bytes_));
    } catch (const std::exception &error) {
        issue_ = error.what();
    }
    revision_ = runtime::Sha256(bytes_.value_or("") + "\n" + issue_ + "\n" + identity_);
}
Json HistoryStore::Snapshot() const {
    return {{"file", file_}, {"issue", issue_}, {"revision", revision_}};
}
void HistoryStore::Writable(const std::string *expected) const {
    if (!writer_)
        throw std::runtime_error("成绩存储为只读");
    if (expected && *expected != revision_)
        throw std::runtime_error("成绩记录已变化，请刷新后重试");
    if (!issue_.empty())
        throw std::runtime_error("成绩存储只读，请先备份并重建");
}
std::string HistoryStore::RetainedEncoding(Json results, std::size_t limit) {
    if (!results.is_array())
        throw std::runtime_error("无效的跟打成绩");
    Sort(results);
    if (results.size() > kMaximumResults)
        results.erase(results.begin() + kMaximumResults, results.end());
    auto encode = [&](std::size_t count) {
        return NormalizeFile(
                   {{"version", 1}, {"results", Json(results.begin(), results.begin() + count)}})
            .dump();
    };
    auto all = encode(results.size());
    if (all.size() <= limit)
        return all;
    if (results.empty())
        throw std::runtime_error("成绩文件超出范围");
    std::size_t low = 1, high = results.size() - 1;
    auto best = encode(low);
    if (best.size() > limit)
        throw std::runtime_error("单次成绩超过文件大小限制");
    while (low < high) {
        auto middle = low + (high - low + 1) / 2;
        auto candidate = encode(middle);
        if (candidate.size() <= limit) {
            low = middle;
            best = std::move(candidate);
        } else
            high = middle - 1;
    }
    return best;
}
void HistoryStore::Save(const std::string &bytes) {
    Writable();
    try {
        if (files_.Identity(kFile) != identity_ ||
            files_.Read(kFile, kMaximumHistoryBytes) != bytes_)
            throw std::runtime_error("成绩文件已被外部修改，原文件已保留");
        const auto next = NormalizeFile(Json::parse(bytes));
        if (bytes.size() > kMaximumHistoryBytes)
            throw std::runtime_error("成绩文件超出范围");
        files_.Write(kFile, bytes);
        bytes_ = bytes;
        file_ = next;
        identity_ = files_.Identity(kFile);
        revision_ = runtime::Sha256(bytes + "\n\n" + identity_);
    } catch (const std::exception &error) {
        issue_ = error.what();
        throw;
    }
}
bool HistoryStore::Add(const Json &value) {
    Writable();
    const auto result = NormalizeResult(value);
    for (const auto &row : file_.at("results"))
        if (row.at("id") == result.at("id"))
            return false;
    auto results = file_.at("results");
    results.push_back(result);
    Save(RetainedEncoding(results));
    return true;
}
void HistoryStore::Clear(const std::string &expected) {
    Writable(&expected);
    Save(Json({{"version", 1}, {"results", Json::array()}}).dump());
}
std::string HistoryStore::Repair(const std::string &expected) {
    if (!writer_)
        throw std::runtime_error("成绩存储为只读");
    if (expected != revision_)
        throw std::runtime_error("成绩记录已变化，请刷新后重试");
    if (issue_.empty())
        return {};
    if (files_.Identity(kFile) != identity_)
        throw std::runtime_error("成绩文件已被外部修改，原文件已保留");
    const auto backup = files_.Preserve(kFile);
    bytes_.reset();
    identity_ = files_.Identity(kFile);
    issue_.clear();
    Save(Json({{"version", 1}, {"results", Json::array()}}).dump());
    return backup;
}
} // namespace rimes::practice
