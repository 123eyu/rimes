#include "chord_store.hpp"
#include <algorithm>
#include <stdexcept>

namespace rimes::chord {
namespace {
std::string Digest(const std::optional<std::string>& value) {
    return value ? runtime::Sha256(*value) : "";
}
} // namespace
std::string NewProfileID() {
    auto value = runtime::RandomID();
    value[12] = '4';
    value[16] = '8';
    return value.substr(0, 8) + '-' + value.substr(8, 4) + '-' + value.substr(12, 4) + '-' +
           value.substr(16, 4) + '-' + value.substr(20);
}
Store::Store(const std::filesystem::path& root) : files_(root / "chord-keymaps") {}
std::string Store::FileName(const std::string& id) {
    if (!IsUUID(id) ||
        std::any_of(id.begin(), id.end(), [](char ch) { return ch >= 'A' && ch <= 'F'; }))
        throw std::runtime_error("键位 ID 无效");
    return id + ".json";
}
ActiveProfile Store::Active() const {
    ActiveProfile result{Profile::BuiltIn(), "", ""};
    try {
        const auto data = files_.Read("active-profile.json", kMaximumProfileBytes);
        if (data) {
            result.profile = Profile::Decode(*data);
            if (result.profile.id == "builtin.flyyao")
                result.profile = Profile::BuiltIn();
            result.revision = runtime::Sha256(*data);
        }
    } catch (const std::exception&) {
        result.error = "已应用的键位快照损坏或不可读；已暂停并击，请重新应用键位。原文件未覆盖。";
    }
    return result;
}
std::vector<std::string> Store::Names() const {
    std::vector<std::string> result;
    for (const auto& entry : std::filesystem::directory_iterator(files_.Path())) {
        const auto name = entry.path().filename().string();
        if (entry.path().extension() != ".json" || name == "active-profile.json" ||
            name == "state.json")
            continue;
        if (result.size() >= kMaximumProfiles || FileName(entry.path().stem().string()) != name)
            throw std::runtime_error("自定义键位数量或文件名无效");
        // Reading through the pinned descriptor rechecks directory identity,
        // ownership, permissions and symlinks after filesystem enumeration.
        if (!files_.Read(name, kMaximumProfileBytes))
            throw std::runtime_error("键位文件在读取时发生变化");
        result.push_back(name);
    }
    return result;
}
std::vector<Profile> Store::Profiles() const {
    const auto lock = files_.Lock(false);
    std::vector<Profile> customs;
    for (const auto& name : Names()) {
        const auto bytes = files_.Read(name, kMaximumProfileBytes);
        if (!bytes)
            throw std::runtime_error("键位文件在读取时发生变化");
        auto profile = Profile::Decode(*bytes, false);
        if (profile.IsPreset() || FileName(profile.id) != name)
            throw std::runtime_error("键位文件名与身份不匹配");
        customs.push_back(std::move(profile));
    }
    std::sort(customs.begin(), customs.end(), [](const auto& a, const auto& b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
    auto result = Profile::NativeProfiles();
    result.insert(result.begin(), Profile::BuiltIn());
    result.insert(result.end(), customs.begin(), customs.end());
    return result;
}
Profile Store::Draft(const std::string& id) const {
    if (id == "builtin.flyyao")
        return Profile::BuiltIn();
    for (auto& profile : Profile::NativeProfiles())
        if (profile.id == id)
            return profile;
    const auto value = files_.Read(FileName(id), kMaximumProfileBytes);
    if (!value)
        throw std::runtime_error("未找到键位草稿");
    const auto profile = Profile::Decode(*value, false);
    if (profile.IsPreset() || profile.id != id)
        throw std::runtime_error("键位文件名与身份不匹配");
    return profile;
}
std::string Store::Revision(const std::string& id) const {
    return Digest(files_.Read(FileName(id), kMaximumProfileBytes));
}
std::string Store::Export(const Profile& profile) const {
    const auto data = Profile::Parse(profile.Value(), false).Value().dump(2) + '\n';
    if (data.size() > kMaximumProfileBytes)
        throw std::runtime_error("键位文件超过 2 MiB 限制");
    return data;
}
void Store::Save(const Profile& profile, const std::string& expected_revision) {
    const auto normalized = Profile::Parse(profile.Value(), false);
    if (normalized.IsPreset())
        throw std::runtime_error("内置方案只读；请先复制键位");
    const auto lock = files_.Lock(false);
    const auto before = files_.Read(FileName(normalized.id), kMaximumProfileBytes);
    if (Digest(before) != expected_revision)
        throw std::runtime_error("键位草稿已变化，请重新载入后编辑");
    const auto names = Names();
    if (!before && names.size() >= kMaximumProfiles)
        throw std::runtime_error("最多保存 128 个自定义键位方案");
    files_.Write(FileName(normalized.id), Export(normalized));
}
void Store::Remove(const std::string& id, const std::string& expected_revision) {
    const auto lock = files_.Lock(false);
    const auto filename = FileName(id);
    if (Active().profile.id == id)
        throw std::runtime_error("请先应用其他键位，再删除当前方案");
    const auto before = files_.Read(filename, kMaximumProfileBytes);
    if (!before || Digest(before) != expected_revision)
        throw std::runtime_error("键位草稿已变化，请重新载入");
    files_.Remove(filename);
}
Profile Store::Import(std::string_view bytes) const {
    auto profile = Profile::Decode(bytes, false);
    if (!profile.native_schema.empty())
        throw std::runtime_error("原生并击方案已内置，请直接选择");
    profile.id = NewProfileID();
    return Profile::Parse(profile.Value(), false);
}
void Store::Activate(const Profile& profile) {
    const auto normalized = Profile::Parse(profile.Value());
    const auto lock = files_.Lock(false);
    files_.Write("active-profile.json", Export(normalized));
}
Json Store::State() const {
    const auto data = files_.Read("state.json", 16384);
    Json result =
        data ? Json::parse(*data)
             : Json{{"selectedSchema", ""}, {"lastOrdinarySchema", "rime_ice"}, {"recovery", ""}};
    for (auto key : {"selectedSchema", "lastOrdinarySchema", "recovery"})
        if (!result.contains(key) || !result[key].is_string())
            throw std::runtime_error("并击运行配置损坏");
    return result;
}
void Store::SaveState(const Json& state) {
    for (auto key : {"selectedSchema", "lastOrdinarySchema", "recovery"})
        if (!state.contains(key) || !state[key].is_string())
            throw std::runtime_error("并击运行配置无效");
    if (state.dump().size() > 16384)
        throw std::runtime_error("并击运行配置过大");
    const auto lock = files_.Lock(false);
    files_.Write("state.json", state.dump(2) + '\n');
}
} // namespace rimes::chord
