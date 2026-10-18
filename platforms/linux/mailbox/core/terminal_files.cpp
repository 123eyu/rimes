#include "capsule_resources.hpp"
#include "capsule_rich_text.hpp"
#include "capsule_text.hpp"
#include "native_terminal_contract.hpp"
#include "process.hpp"
#include "terminal_files.hpp"
#include <array>
#include <dirent.h>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <unicode/coll.h>
#include <unicode/locid.h>
#include <unistd.h>
namespace rimes::mailbox {
const Json& TerminalContract() {
    static const auto contract = Json::parse(native::kTerminalContract);
    return contract;
}
TerminalFile ReadTerminalFile(const std::filesystem::path& path,
                              const std::atomic<bool>& cancelled) {
    std::array<char, 4096> self{};
    const auto count = readlink("/proc/self/exe", self.data(), self.size() - 1);
    if (count <= 0)
        throw std::runtime_error("文件预览程序不可用");
    auto helper =
        std::filesystem::path(std::string(self.data(), count)).parent_path() / "rimes-mailbox-file";
    if (access(helper.c_str(), X_OK))
        helper =
            std::filesystem::path(std::string(self.data(), count)).parent_path().parent_path() /
            "libexec/rimes/rimes-mailbox-file";
    if (access(helper.c_str(), X_OK))
        helper = RIMES_MAILBOX_FILE_PATH;
    const auto result = runtime::RunProcess({helper.string(), path.string()}, "", cancelled,
                                            std::chrono::seconds(8), 40 * 1048576);
    if (cancelled.load())
        throw std::runtime_error("文件预览已取消");
    if (!result.success)
        throw std::runtime_error("文件无法预览，或已超过时间 / 内存限制");
    const auto value = Json::parse(result.output);
    if (value.contains("error"))
        throw std::runtime_error(value.at("error"));
    TerminalFile file{path, value.at("body"), ""};
    file.png = capsule::Base64Decode(value.at("png").get<std::string>()).value_or("");
    if (file.body.empty() ||
        file.png.size() >
            TerminalContract().at("limits").at("maximumFileBytes").get<std::size_t>() ||
        file.body.size() >
            TerminalContract().at("limits").at("maximumTextBytes").get<std::size_t>())
        throw std::runtime_error("文件超过收取上限");
    return file;
}
std::vector<std::filesystem::path> TerminalFiles(const std::filesystem::path& directory,
                                                 const std::atomic<bool>& cancelled) {
    const int fd = capsule::OpenResource(directory);
    struct stat info {};
    if (fd < 0)
        throw std::runtime_error("工作目录无法读取");
    if (fstat(fd, &info) || !S_ISDIR(info.st_mode)) {
        close(fd);
        throw std::runtime_error("工作目录无法读取");
    }
    auto* raw = fdopendir(fd);
    if (!raw) {
        close(fd);
        throw std::runtime_error("工作目录无法读取");
    }
    const auto close_directory = [](DIR* directory) { closedir(directory); };
    std::unique_ptr<DIR, decltype(close_directory)> entries(raw, close_directory);
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Collator> collator(
        icu::Collator::createInstance(icu::Locale::getDefault(), status));
    if (U_FAILURE(status) || !collator)
        throw std::runtime_error("文件排序不可用");
    collator->setAttribute(UCOL_NUMERIC_COLLATION, UCOL_ON, status);
    collator->setStrength(icu::Collator::SECONDARY);
    if (U_FAILURE(status))
        throw std::runtime_error("文件排序不可用");
    const auto less = [&](const std::filesystem::path& a, const std::filesystem::path& b) {
        UErrorCode compared = U_ZERO_ERROR;
        const auto result =
            collator->compareUTF8(a.filename().string(), b.filename().string(), compared);
        return U_FAILURE(compared) || result == UCOL_EQUAL ? a < b : result == UCOL_LESS;
    };
    std::set<std::filesystem::path, decltype(less)> files(less);
    std::size_t visited = 0;
    // Bounded scan even for huge/generated working trees. Never follow links or
    // recurse. The explicit chooser can reach files outside this small index.
    while (!cancelled.load() && visited++ < 10000) {
        const auto* entry = readdir(raw);
        if (!entry)
            break;
        if (entry->d_name[0] == '.')
            continue;
        try {
            capsule::CharacterCount(entry->d_name);
        } catch (...) {
            continue;
        }
        struct stat file {};
        if (!fstatat(fd, entry->d_name, &file, AT_SYMLINK_NOFOLLOW) && S_ISREG(file.st_mode)) {
            files.insert(directory / entry->d_name);
            if (files.size() >
                TerminalContract().at("limits").at("maximumFiles").get<std::size_t>())
                files.erase(std::prev(files.end()));
        }
    }
    if (cancelled.load())
        throw std::runtime_error("文件列表已取消");
    return {files.begin(), files.end()};
}
} // namespace rimes::mailbox
