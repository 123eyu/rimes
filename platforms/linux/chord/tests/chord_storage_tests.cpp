#include "chord_deployment.hpp"
#include "chord_learning.hpp"
#include "chord_store.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace rimes::chord;
namespace fs = std::filesystem;
void Check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
template <class F> void Fails(F action, const char* reason) {
    bool failed = false;
    try {
        action();
    } catch (...) {
        failed = true;
    }
    Check(failed, reason);
}
std::string Read(const fs::path& path) {
    std::ifstream stream(path);
    return {(std::istreambuf_iterator<char>(stream)), {}};
}
int main() {
    char directory[] = "/tmp/xsime-chord-store-XXXXXX";
    if (!mkdtemp(directory))
        return 1;
    const fs::path root(directory);
    try {
        Store store(root);
        auto profile = store.Import(Profile::BuiltIn().Value().dump());
        Check(IsUUID(profile.id) && !profile.IsPreset(), "import must assign fresh identity");
        auto copy = profile;
        copy.name = "词库不变";
        store.Save(profile, "");
        const auto revision = store.Revision(profile.id);
        Fails([&] { store.Save(copy, ""); }, "stale editor overwrote draft");
        store.Activate(profile);
        store.Save(copy, revision);
        Check(Store(root).Active().profile.Value() == profile.Value(),
              "draft save or restart changed effective map");
        Fails([&] { store.Remove(profile.id, store.Revision(profile.id)); },
              "removed active profile");
        store.Activate(Profile::BuiltIn());
        store.Remove(profile.id, store.Revision(profile.id));
        Fails([&] { (void)store.Draft(profile.id); }, "removed draft remained live");
        Fails([&] { store.Save(Profile::BuiltIn(), ""); }, "built-in was writable");
        Fails([&] { (void)store.Import(Profile::NativeProfiles().front().Value().dump()); },
              "native preset imported as custom");
        std::ofstream(root / "learning.userdb") << "learning sentinel";
        std::ofstream(root / "user-dictionary.txt") << "dictionary sentinel";
        const std::string original =
            "# Keep exact original on failed apply\npatch:\n  menu/page_size: 8\n  schema_list:\n  "
            "  - schema: rime_ice\n    - schema: english\n";
        std::ofstream(root / "default.custom.yaml") << original;
        chmod((root / "default.custom.yaml").c_str(), 0644);
        DeploymentFiles files(root);
        auto snapshot = files.Prepare(profile, true, Json::array({"rime_ice", "english"}));
        Check(Read(root / "default.custom.yaml").find(profile.SchemaID()) != std::string::npos,
              "custom schema absent from deployed list");
        Check(Read(root / (profile.SchemaID() + ".schema.yaml")) == profile.SchemaYAML(),
              "schema compiler bytes lost");
        files.Restore(snapshot);
        Check(Read(root / "default.custom.yaml") == original &&
                  !fs::exists(root / (profile.SchemaID() + ".schema.yaml")),
              "rollback did not restore exact files");
        struct stat status {};
        Check(stat((root / "default.custom.yaml").c_str(), &status) == 0 &&
                  (status.st_mode & 0777) == 0644,
              "rollback lost original mode");
        snapshot = files.Prepare(profile, true, Json::array({"rime_ice"}));
        std::ofstream(root / "default.custom.yaml") << "external edit\n";
        Fails([&] { files.Restore(snapshot); }, "rollback overwrote concurrent edit");
        Check(Read(root / "default.custom.yaml") == "external edit\n", "external edit was lost");
        fs::remove(root / "default.custom.yaml");
        fs::create_symlink(root / "user-dictionary.txt", root / "default.custom.yaml");
        Fails([&] { (void)files.Prepare(profile, true, Json::array({"rime_ice"})); },
              "deployment followed symlink");
        Check(Read(root / "learning.userdb") == "learning sentinel" &&
                  Read(root / "user-dictionary.txt") == "dictionary sentinel",
              "deployment changed user learning or dictionary");
        fs::remove(root / "default.custom.yaml");
        auto corrupt = root / "chord-keymaps/active-profile.json";
        const auto course = Curriculum(profile).front();
        const auto id = course.mappings.front().id;
        ProgressStore progress(root, profile.SchemaID());
        for (int index = 0; index < 3; ++index)
            (void)progress.Record(id, true, 100 + index);
        auto recorded = progress.Record(id, false, 103);
        Check(recorded.at("items").at(id).at("bestStreak") == 3 &&
                  recorded.at("items").at(id).at("currentStreak") == 0,
              "mastery lost after a mistake");
        Check(ProgressStore(root, profile.SchemaID()).Load() == recorded,
              "progress did not survive reopen");
        ProgressStore second(root, profile.SchemaID());
        (void)second.Record(id, true, 104);
        Check(progress.Record(id, true, 105).at("items").at(id).at("attempts") == 6,
              "two windows lost progress");
        Check(CourseProgress(course, progress.Load().at("items")).at("mastered") == 1,
              "course mastery aggregate");
        Fails([&] { (void)progress.Record("../escape", true, 106); },
              "invalid learning identity accepted");
        const auto progress_path = root / "learning" / (profile.SchemaID() + "_progress.json");
        const auto good_progress = Read(progress_path);
        Check(good_progress.find("\"output\"") == std::string::npos &&
                  good_progress.find("\"keys\"") == std::string::npos,
              "learning leaked output text");
        Check(progress.Clear(107).at("items").empty(), "clear progress");
        std::ofstream(progress_path) << "broken";
        Fails([&] { (void)progress.Record(id, true, 108); }, "corrupt learning overwritten");
        Check(Read(progress_path) == "broken", "corrupt progress was replaced");
        std::ofstream(corrupt) << "{broken";
        Check(!Store(root).Active().error.empty() && Read(corrupt) == "{broken",
              "corrupt snapshot silently overwritten");
        fs::remove(corrupt);
        fs::create_symlink(root / "learning.userdb", corrupt);
        Check(!Store(root).Active().error.empty(), "active snapshot followed symlink");
        Check(store.Profiles().size() == 3, "default/native profile list changed");
        fs::remove_all(root);
        std::cout << "PASS: draft/applied separation, restart, revisions, import identity, exact "
                     "deployment rollback, concurrent edit, symlink and user-data preservation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
