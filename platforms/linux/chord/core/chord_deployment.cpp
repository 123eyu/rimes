#include "chord_deployment.hpp"
#include "private_files.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

namespace rimes::chord {
namespace {
constexpr std::size_t kMaximumBytes = 4 * 1024 * 1024;
struct FD {
    int value;
    ~FD() {
        if (value >= 0)
            close(value);
    }
};
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void CheckFile(const struct stat& st) {
    Check(S_ISREG(st.st_mode) && st.st_uid == geteuid() && st.st_nlink == 1 && st.st_size >= 0 &&
              static_cast<std::size_t>(st.st_size) <= kMaximumBytes && (st.st_mode & 0022) == 0,
          "并击部署文件类型、权限或大小不受支持");
}
bool Same(const DeploymentFiles::File& a, const DeploymentFiles::File& b) {
    return a.bytes == b.bytes && (!a.bytes || a.mode == b.mode);
}
void BoundedYAML(const YAML::Node& node, int depth, std::size_t& remaining) {
    Check(depth <= 24 && remaining > 0, "方案列表层级或节点数过多");
    --remaining;
    if (node.IsMap())
        for (const auto& entry : node) {
            BoundedYAML(entry.first, depth + 1, remaining);
            BoundedYAML(entry.second, depth + 1, remaining);
        }
    if (node.IsSequence())
        for (const auto& entry : node)
            BoundedYAML(entry, depth + 1, remaining);
}
} // namespace
DeploymentFiles::DeploymentFiles(const std::filesystem::path& root) : root_(root) {
    Check(root.is_absolute(), "Rime 数据目录必须是绝对路径");
    fd_ = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    Check(fd_ >= 0, "无法打开 Rime 数据目录");
    try {
        Verify();
    } catch (...) {
        close(fd_);
        fd_ = -1;
        throw;
    }
}
DeploymentFiles::~DeploymentFiles() {
    if (fd_ >= 0)
        close(fd_);
}
void DeploymentFiles::Verify() const {
    struct stat fd {
    }, path{};
    Check(fstat(fd_, &fd) == 0 && lstat(root_.c_str(), &path) == 0 && S_ISDIR(path.st_mode) &&
              fd.st_dev == path.st_dev && fd.st_ino == path.st_ino && fd.st_uid == geteuid() &&
              (fd.st_mode & 0022) == 0,
          "Rime 数据目录已变化或权限不安全");
}
DeploymentFiles::File DeploymentFiles::Read(const std::string& name) const {
    Verify();
    Check(name.find('/') == std::string::npos && name != "." && name != "..", "部署文件名无效");
    FD file{openat(fd_, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
    if (file.value < 0 && errno == ENOENT)
        return {};
    Check(file.value >= 0, "无法读取部署文件");
    struct stat st {};
    Check(fstat(file.value, &st) == 0, "无法检查部署文件");
    CheckFile(st);
    std::string bytes(static_cast<std::size_t>(st.st_size), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = read(file.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        Check(count > 0, "部署文件读取不完整");
        offset += static_cast<std::size_t>(count);
    }
    char extra;
    Check(read(file.value, &extra, 1) == 0, "部署文件在读取时发生变化");
    struct stat after {};
    Check(fstat(file.value, &after) == 0 && after.st_size == st.st_size &&
              after.st_mtim.tv_sec == st.st_mtim.tv_sec &&
              after.st_mtim.tv_nsec == st.st_mtim.tv_nsec,
          "部署文件在读取时发生变化");
    return {std::move(bytes), static_cast<unsigned>(st.st_mode & 0777)};
}
void DeploymentFiles::Write(const std::string& name, const File& value,
                            const File& expected) const {
    Check(Same(Read(name), expected), "部署文件已由其他程序修改，未覆盖该文件");
    if (!value.bytes) {
        Check(unlinkat(fd_, name.c_str(), 0) == 0 || errno == ENOENT, "无法移除未应用的方案文件");
        Check(fsync(fd_) == 0, "无法保存方案目录");
        return;
    }
    Check(value.bytes->size() <= kMaximumBytes, "生成方案超过大小限制");
    const auto temporary = ".chord-" + runtime::RandomID();
    FD file{
        openat(fd_, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    Check(file.value >= 0, "无法创建方案暂存文件");
    try {
        std::size_t offset = 0;
        while (offset < value.bytes->size()) {
            const auto count =
                write(file.value, value.bytes->data() + offset, value.bytes->size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            Check(count > 0, "无法写入方案文件");
            offset += static_cast<std::size_t>(count);
        }
        Check(fchmod(file.value, value.mode) == 0 && fsync(file.value) == 0, "无法保存方案文件");
        Check(Same(Read(name), expected), "部署文件已由其他程序修改，未覆盖该文件");
        Check(renameat(fd_, temporary.c_str(), fd_, name.c_str()) == 0 && fsync(fd_) == 0,
              "无法发布方案文件");
    } catch (...) {
        unlinkat(fd_, temporary.c_str(), 0);
        throw;
    }
}
DeploymentFiles::Snapshot DeploymentFiles::Prepare(const Profile& profile, bool enabled,
                                                   const Json& default_schemas) {
    Snapshot snapshot;
    snapshot.before["default.custom.yaml"] = Read("default.custom.yaml");
    auto config = snapshot.before.at("default.custom.yaml").bytes
                      ? YAML::Load(*snapshot.before.at("default.custom.yaml").bytes)
                      : YAML::Node(YAML::NodeType::Map);
    if (!config || config.IsNull())
        config = YAML::Node(YAML::NodeType::Map);
    std::size_t remaining = 10000;
    BoundedYAML(config, 0, remaining);
    Check(config.IsMap(), "default.custom.yaml 必须是映射");
    if (config["patch"] && !config["patch"].IsMap())
        throw std::runtime_error("default.custom.yaml 的 patch 格式无效");
    auto previous = config["patch"]["schema_list"];
    if (previous && !previous.IsSequence())
        throw std::runtime_error("现有 schema_list 不是列表，未修改方案");
    YAML::Node list(YAML::NodeType::Sequence);
    if (previous) {
        for (const auto& item : previous) {
            Check(item.IsMap() && item["schema"] && item["schema"].IsScalar(),
                  "现有 schema_list 记录无效");
            if (!IsChordSchema(item["schema"].as<std::string>()))
                list.push_back(item);
        }
    }
    if (list.size() == 0)
        for (const auto& id : default_schemas) {
            Check(id.is_string(), "默认方案列表无效");
            if (!IsChordSchema(id.get<std::string>())) {
                YAML::Node item;
                item["schema"] = id.get<std::string>();
                list.push_back(item);
            }
        }
    if (enabled) {
        YAML::Node item;
        item["schema"] = profile.SchemaID();
        list.push_back(item);
    }
    config["patch"]["schema_list"] = list;
    snapshot.prepared["default.custom.yaml"] = {YAML::Dump(config) + '\n',
                                                snapshot.before.at("default.custom.yaml").mode};
    if (!profile.IsPreset()) {
        const auto name = profile.SchemaID() + ".schema.yaml";
        snapshot.before[name] = Read(name);
        snapshot.prepared[name] = {profile.SchemaYAML(), 0600};
    }
    std::vector<std::string> written;
    try {
        for (const auto& [name, data] : snapshot.prepared) {
            written.push_back(name);
            Write(name, data, snapshot.before.at(name));
        }
    } catch (...) {
        const auto failure = std::current_exception();
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            if (Same(Read(*it), snapshot.before.at(*it)))
                continue;
            Write(*it, snapshot.before.at(*it), snapshot.prepared.at(*it));
        }
        std::rethrow_exception(failure);
    }
    return snapshot;
}
void DeploymentFiles::Restore(const Snapshot& snapshot) {
    // Validate every file before the first restoration, including a user edit
    // made while the deployment worker was running.
    for (const auto& [name, data] : snapshot.prepared)
        Check(Same(Read(name), data), "部署期间文件被外部修改，无法安全回退");
    for (const auto& [name, data] : snapshot.before)
        Write(name, data, snapshot.prepared.at(name));
}
} // namespace rimes::chord
