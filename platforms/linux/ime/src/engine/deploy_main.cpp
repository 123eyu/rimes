#include <rime_api.h>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    // The parent supplies an already locked descriptor, not a new independent
    // lock acquisition. Validate it against the exact user directory inode.
    struct stat inherited{}, expected{};
    const auto lock = std::filesystem::path(argv[2]) / ".rimes-engine.lock";
    if (fstat(3, &inherited) != 0 || lstat(lock.c_str(), &expected) != 0 ||
        !S_ISREG(inherited.st_mode) || !S_ISREG(expected.st_mode) ||
        inherited.st_dev != expected.st_dev || inherited.st_ino != expected.st_ino ||
        inherited.st_uid != geteuid() || inherited.st_nlink != 1 ||
        flock(3, LOCK_EX | LOCK_NB) != 0) return 3;
    auto* api = rime_get_api();
    if (!api || !api->setup || !api->initialize || !api->start_maintenance ||
        !api->join_maintenance_thread || !api->finalize ||
        !api->create_session || !api->destroy_session) return 4;
    // A killed compiler may leave a build directory whose artifacts are only
    // partly written. Keep a marker until successful completion so the next
    // worker requests a full maintenance check, even if build/ already exists.
    const auto pending = std::filesystem::path(argv[2]) / ".rimes-deployment-pending";
    const bool interrupted = std::filesystem::exists(pending);
    const int marker = open(pending.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (marker < 0) return 6;
    struct stat marker_info{};
    const bool safe_marker = fstat(marker, &marker_info) == 0 &&
        S_ISREG(marker_info.st_mode) && marker_info.st_uid == geteuid() &&
        marker_info.st_nlink == 1;
    close(marker);
    if (!safe_marker) return 6;
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = argv[1];
    traits.user_data_dir = argv[2];
    traits.log_dir = argv[3];
    traits.distribution_name = "RIMES";
    traits.distribution_code_name = "rimes";
    traits.distribution_version = RIMES_DISTRIBUTION_VERSION;
    traits.app_name = "rime.rimes.linux.deploy";
    traits.min_log_level = 2;
    api->setup(&traits);
    api->initialize(&traits);
    std::atomic<bool> failed{false};
    if (api->set_notification_handler) {
        api->set_notification_handler([](void* context, RimeSessionId,
            const char* type, const char* value) {
            if (type && value && std::strcmp(type, "deploy") == 0 &&
                std::strcmp(value, "failure") == 0)
                static_cast<std::atomic<bool>*>(context)->store(true);
        }, &failed);
    }
    api->start_maintenance(argv[4][0] == '1' || interrupted ? True : False);
    api->join_maintenance_thread();
    const auto session = api->create_session();
    if (session) api->destroy_session(session);
    if (api->set_notification_handler) api->set_notification_handler(nullptr, nullptr);
    api->finalize();
    if (!session || failed.load()) return 5;
    return unlink(pending.c_str()) == 0 ? 0 : 6;
}
