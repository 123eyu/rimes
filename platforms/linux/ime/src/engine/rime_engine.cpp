#include "rime_engine.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef __GLIBC__
#include <malloc.h>
#endif

#include <rime_api.h>

#include "rime_key.hpp"
#include "rime_paths.hpp"
#include "rime_user_lock.hpp"
#include "deployment_process.hpp"

namespace rimes::linuxime {
namespace {

void SetError(std::string* error, std::string_view message) noexcept {
    if (error == nullptr) {
        return;
    }
    try {
        error->assign(message);
    } catch (...) {
    }
}

std::string DefaultLabel(int index) {
    return std::to_string((index + 1) % 10);
}

}  // namespace

class RimeEngine::Impl final {
public:
    bool Start(const RimeEngineOptions& options, std::string* error) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (healthy_ || external_pending_) {
                return true;
            }

            if (!LooksLikeSharedData(options.shared_data_dir)) {
                SetError(error, "shared data directory has no default.yaml");
                return false;
            }
            if (!EnsureDirectory(options.user_data_dir, error) ||
                !EnsureDirectory(options.log_dir, error)) {
                return false;
            }

            api_ = rime_get_api();
            if (api_ == nullptr || api_->setup == nullptr ||
                api_->initialize == nullptr || api_->finalize == nullptr ||
                api_->start_maintenance == nullptr ||
                api_->join_maintenance_thread == nullptr ||
                api_->create_session == nullptr || api_->destroy_session == nullptr ||
                api_->process_key == nullptr || api_->get_commit == nullptr ||
                api_->get_context == nullptr) {
                SetError(error, "librime API is incomplete");
                return false;
            }

            if (!user_lock_.Acquire(options.user_data_dir, error)) {
                return false;
            }

            shared_data_dir_ = options.shared_data_dir.string();
            user_data_dir_ = options.user_data_dir.string();
            log_dir_ = options.log_dir.string();

            deployment_helper_ = options.deployment_helper;
            if (deployment_helper_.empty()) {
                deployment_helper_ = std::filesystem::exists(RIMES_INSTALLED_DEPLOY_HELPER)
                    ? RIMES_INSTALLED_DEPLOY_HELPER : RIMES_BUILD_DEPLOY_HELPER;
            }
            if (options.wait_for_maintenance) InitializeRuntimeLocked();

            const bool full_check =
                options.full_maintenance_check ||
                !std::filesystem::exists(options.user_data_dir / "build");
            if (!RunMaintenanceLocked(full_check, options.wait_for_maintenance, error)) {
                StopLocked();
                return false;
            }
            if (deploying_) {
                return true;
            }
            if (!FinishReadyLocked(error)) {
                StopLocked();
                return false;
            }
            return true;
        } catch (...) {
            try {
                std::lock_guard<std::mutex> lock(mutex_);
                StopLocked();
            } catch (...) {
            }
            SetError(error, "exception while starting librime");
            return false;
        }
    }

    void Stop() noexcept {
        try {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                watch_stop_ = true;
                watch_cv_.notify_all();
            }
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                deploy_ready_ = nullptr;
            }
            if (watch_thread_.joinable()) {
                watch_thread_.join();
            }
            std::lock_guard<std::mutex> lock(mutex_);
            StopLocked();
        } catch (...) {
        }
    }

    void SetDeployReadyCallback(RimeEngine::DeployReadyCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        deploy_ready_ = std::move(callback);
    }

    bool IsHealthy() const noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            return healthy_ && api_ != nullptr;
        } catch (...) {
            return false;
        }
    }

    bool IsDeploying() const noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            return deploying_ && !healthy_;
        } catch (...) {
            return false;
        }
    }

    bool PollMaintenance(std::string* error) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (healthy_ && api_ != nullptr) {
                return true;
            }
            if (external_pending_) {
                if (!deployment_.Poll()) return false;
                external_pending_ = false;
                deploying_ = false;
                if (deployment_.ExitCode() != 0) {
                    SetError(error, "librime deployment worker failed; inspect deployment logs");
                    return false;
                }
                InitializeRuntimeLocked();
            }
            if (!initialized_ || api_ == nullptr) {
                SetError(error, "librime is not initialized");
                return false;
            }
            return FinishReadyLocked(error);
        } catch (...) {
            SetError(error, "exception while polling librime maintenance");
            return false;
        }
    }

    bool RunMaintenance(bool full_check, std::string* error) noexcept {
        return StartMaintenance(full_check, true, error);
    }

    bool StartBackgroundMaintenance(bool full_check, std::string* error) noexcept {
        return StartMaintenance(full_check, false, error);
    }

    bool StartMaintenance(bool full_check, bool wait, std::string* error) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || api_ == nullptr) {
                SetError(error, "librime is not initialized");
                return false;
            }
            if (!sessions_.empty()) {
                SetError(error, "librime maintenance requires all sessions to close");
                return false;
            }
            if (!RunMaintenanceLocked(full_check, wait, error)) {
                return false;
            }
            if (wait && !FinishReadyLocked(error)) {
                return false;
            }
            return true;
        } catch (...) {
            SetError(error, "exception while running librime maintenance");
            return false;
        }
    }

    SessionId CreateSession(std::string* error) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!healthy_ || api_ == nullptr) {
                SetError(error, "librime is not initialized");
                return 0;
            }
            const RimeSessionId session = api_->create_session();
            if (session == 0) {
                SetError(error, "librime session creation failed");
                return 0;
            }
            sessions_.insert(session);
            if (api_->select_schema != nullptr) {
                api_->select_schema(session, kDefaultSchemaId);
            }
            if (api_->set_option != nullptr) {
                api_->set_option(session, "ascii_mode", False);
            }
            return session;
        } catch (...) {
            SetError(error, "exception while creating a librime session");
            return 0;
        }
    }

    bool DestroySession(SessionId session, std::string* error) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!healthy_ || api_ == nullptr || session == 0 ||
                sessions_.erase(session) != 1) {
                SetError(error, "unknown or inactive librime session");
                return false;
            }
            api_->destroy_session(session);
            return true;
        } catch (...) {
            SetError(error, "exception while destroying a librime session");
            return false;
        }
    }

    bool SelectSchema(SessionId session,
                      std::string_view schema_id,
                      std::string* error) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!HasSessionLocked(session, error) || api_->select_schema == nullptr) {
                return false;
            }
            schema_storage_.assign(schema_id);
            if (api_->select_schema(session, schema_storage_.c_str()) == 0) {
                SetError(error, "librime rejected schema selection");
                return false;
            }
            return true;
        } catch (...) {
            SetError(error, "exception while selecting a schema");
            return false;
        }
    }

    bool SetOption(SessionId session,
                   std::string_view option,
                   bool value,
                   std::string* error) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!HasSessionLocked(session, error) || api_->set_option == nullptr) {
                return false;
            }
            option_storage_.assign(option);
            api_->set_option(session, option_storage_.c_str(), value ? True : False);
            return true;
        } catch (...) {
            SetError(error, "exception while setting a rime option");
            return false;
        }
    }

    bool ProcessKey(SessionId session,
                    std::int32_t keycode,
                    std::int32_t modifiers,
                    EngineSnapshot* output,
                    std::string* error) noexcept {
        if (output == nullptr) {
            SetError(error, "engine snapshot output is null");
            return false;
        }
        *output = EngineSnapshot{};
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!HasSessionLocked(session, error)) {
                return false;
            }
            output->handled = api_->process_key(session, keycode, modifiers) != 0;
            return CollectSnapshotLocked(session, output, error);
        } catch (...) {
            *output = EngineSnapshot{};
            SetError(error, "exception while processing a librime key event");
            return false;
        }
    }

    bool SelectCandidate(SessionId session,
                         int index_on_page,
                         EngineSnapshot* output,
                         std::string* error) noexcept {
        if (output == nullptr) {
            SetError(error, "engine snapshot output is null");
            return false;
        }
        *output = EngineSnapshot{};
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!HasSessionLocked(session, error)) {
                return false;
            }
            if (index_on_page < 0 || api_->select_candidate_on_current_page == nullptr) {
                SetError(error, "candidate index is out of range");
                return false;
            }
            output->handled =
                api_->select_candidate_on_current_page(
                    session, static_cast<size_t>(index_on_page)) != 0;
            return CollectSnapshotLocked(session, output, error);
        } catch (...) {
            *output = EngineSnapshot{};
            SetError(error, "exception while selecting a candidate");
            return false;
        }
    }

    bool ClearComposition(SessionId session,
                          EngineSnapshot* output,
                          std::string* error) noexcept {
        if (output == nullptr) {
            SetError(error, "engine snapshot output is null");
            return false;
        }
        *output = EngineSnapshot{};
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!HasSessionLocked(session, error)) {
                return false;
            }
            if (api_->clear_composition != nullptr) {
                api_->clear_composition(session);
            }
            output->handled = true;
            return CollectSnapshotLocked(session, output, error);
        } catch (...) {
            *output = EngineSnapshot{};
            SetError(error, "exception while clearing composition");
            return false;
        }
    }

    bool CommitComposition(SessionId session,
                           EngineSnapshot* output,
                           std::string* error) noexcept {
        if (output == nullptr) {
            SetError(error, "engine snapshot output is null");
            return false;
        }
        *output = EngineSnapshot{};
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!HasSessionLocked(session, error)) {
                return false;
            }
            if (api_->commit_composition != nullptr) {
                output->handled = api_->commit_composition(session) != 0;
            }
            return CollectSnapshotLocked(session, output, error);
        } catch (...) {
            *output = EngineSnapshot{};
            SetError(error, "exception while committing composition");
            return false;
        }
    }

    bool Refresh(SessionId session,
                 EngineSnapshot* output,
                 std::string* error) noexcept {
        if (output == nullptr) {
            SetError(error, "engine snapshot output is null");
            return false;
        }
        *output = EngineSnapshot{};
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!HasSessionLocked(session, error)) {
                return false;
            }
            output->handled = true;
            return CollectSnapshotLocked(session, output, error);
        } catch (...) {
            *output = EngineSnapshot{};
            SetError(error, "exception while refreshing engine state");
            return false;
        }
    }

private:
    bool HasSessionLocked(SessionId session, std::string* error) const noexcept {
        if (!healthy_ || api_ == nullptr || session == 0 ||
            sessions_.find(session) == sessions_.end()) {
            SetError(error, "unknown or inactive librime session");
            return false;
        }
        return true;
    }

    void InitializeRuntimeLocked() {
        RIME_STRUCT(RimeTraits, traits);
        traits.shared_data_dir = shared_data_dir_.c_str();
        traits.user_data_dir = user_data_dir_.c_str();
        traits.distribution_name = "RIMES";
        traits.distribution_code_name = "rimes";
        traits.distribution_version = RIMES_DISTRIBUTION_VERSION;
        traits.app_name = "rime.rimes.linux";
        traits.min_log_level = 2;
        traits.log_dir = log_dir_.c_str();
        // glog setup is process-wide and aborts if initialized twice.
        static bool setup_done = false;
        if (!setup_done) {
            api_->setup(&traits);
            setup_done = true;
        }
        api_->initialize(&traits);
        initialized_ = true;
    }

    bool RunMaintenanceLocked(bool full_check, bool wait, std::string* error) {
        if (!wait) {
            // librime has no cancellable deployment API. Keep its compiler in
            // a child so shutdown can terminate it without killing Fcitx or
            // leaving a thread accessing a destroyed engine. Runtime and worker
            // never open the user dictionaries at the same time.
            healthy_ = false;
            if (initialized_) {
                api_->finalize();
                initialized_ = false;
            }
            if (!deployment_.Start(deployment_helper_, shared_data_dir_,
                    user_data_dir_, log_dir_, full_check, user_lock_.Descriptor(), error))
                return false;
            external_pending_ = true;
            deploying_ = true;
            worker_notified_ = false;
            if (!StartWatchLocked()) {
                deployment_.Stop();
                external_pending_ = deploying_ = false;
                SetError(error, "could not start deployment watcher");
                return false;
            }
            return true;
        }
        api_->start_maintenance(full_check ? True : False);
        api_->join_maintenance_thread();
        deploying_ = false;
        return true;
    }

    bool FinishReadyLocked(std::string* error) noexcept {
        const RimeSessionId smoke = api_->create_session();
        if (smoke == 0) {
            SetError(error, "librime smoke session creation failed");
            return false;
        }
        api_->destroy_session(smoke);
        healthy_ = true;
        deploying_ = false;
        ReleaseBuildHeap();
        return true;
    }

    static void ReleaseBuildHeap() noexcept {
#ifdef __GLIBC__
        malloc_trim(0);
#endif
    }

    bool CollectSnapshotLocked(SessionId session,
                               EngineSnapshot* output,
                               std::string* error) noexcept {
        if (api_->get_commit != nullptr && api_->free_commit != nullptr) {
            RIME_STRUCT(RimeCommit, commit);
            if (api_->get_commit(session, &commit) != 0) {
                const bool copied =
                    CopyBoundedUtf8(commit.text, kMaxTextBytes, &output->commit_text, error);
                api_->free_commit(&commit);
                if (!copied) {
                    return false;
                }
            }
        }

        if (api_->get_status != nullptr && api_->free_status != nullptr) {
            RIME_STRUCT(RimeStatus, status);
            if (api_->get_status(session, &status) != 0) {
                output->composing = status.is_composing != 0;
                output->ascii_mode = status.is_ascii_mode != 0;
                const bool copied =
                    CopyBoundedUtf8(status.schema_id, 128, &output->schema_id, error);
                api_->free_status(&status);
                if (!copied) {
                    return false;
                }
            }
        }

        if (api_->get_context == nullptr || api_->free_context == nullptr) {
            return true;
        }

        RIME_STRUCT(RimeContext, context);
        if (api_->get_context(session, &context) == 0) {
            return true;
        }

        bool ok = CopyBoundedUtf8(context.composition.preedit, kMaxTextBytes,
                                  &output->preedit, error);
        if (ok) {
            if (context.composition.cursor_pos < 0 ||
                static_cast<std::size_t>(context.composition.cursor_pos) >
                    output->preedit.size()) {
                SetError(error, "librime caret is outside the preedit");
                ok = false;
            } else {
                output->caret_utf8 =
                    static_cast<std::size_t>(context.composition.cursor_pos);
                if (context.composition.sel_start < 0 || context.composition.sel_end < 0 ||
                    static_cast<std::size_t>(context.composition.sel_start) > output->preedit.size() ||
                    static_cast<std::size_t>(context.composition.sel_end) > output->preedit.size()) {
                    SetError(error, "librime selection is outside the preedit");
                    ok = false;
                } else {
                    output->selection_start_utf8 = context.composition.sel_start;
                    output->selection_end_utf8 = context.composition.sel_end;
                }
            }
        }

        if (ok && context.menu.num_candidates < 0) {
            SetError(error, "librime returned a negative candidate count");
            ok = false;
        }
        const int count = ok ? context.menu.num_candidates : 0;
        if (ok && count > static_cast<int>(kMaxCandidateCount)) {
            SetError(error, "librime returned too many candidates");
            ok = false;
        }
        if (ok && count > 0) {
            if (context.menu.candidates == nullptr || context.menu.page_size <= 0) {
                SetError(error, "librime returned inconsistent candidate metadata");
                ok = false;
            } else {
                output->page_no = context.menu.page_no;
                output->page_size = context.menu.page_size;
                output->highlighted = context.menu.highlighted_candidate_index;
                output->is_last_page = context.menu.is_last_page != 0;
                output->candidates.resize(static_cast<std::size_t>(count));
            }
        }

        for (int index = 0; ok && index < count; ++index) {
            EngineCandidate& candidate = output->candidates[static_cast<std::size_t>(index)];
            ok = CopyBoundedUtf8(context.menu.candidates[index].text, kMaxTextBytes,
                                 &candidate.text, error) &&
                 CopyBoundedUtf8(context.menu.candidates[index].comment, kMaxTextBytes,
                                 &candidate.comment, error);
            if (!ok) {
                break;
            }
            if (RIME_STRUCT_HAS_MEMBER(context, context.select_labels) &&
                context.select_labels != nullptr &&
                context.select_labels[index] != nullptr) {
                ok = CopyBoundedUtf8(context.select_labels[index], 16, &candidate.label,
                                     error);
            } else if (context.menu.select_keys != nullptr &&
                       context.menu.select_keys[index] != '\0') {
                candidate.label.assign(1, context.menu.select_keys[index]);
            } else {
                candidate.label = DefaultLabel(index);
            }
        }

        api_->free_context(&context);
        if (!ok) {
            *output = EngineSnapshot{};
            return false;
        }
        if (api_->get_input != nullptr) {
            const char* raw = api_->get_input(session);
            if (raw != nullptr && raw[0] != '\0') {
                if (!CopyBoundedUtf8(raw, kMaxTextBytes, &output->raw_input, error)) {
                    *output = EngineSnapshot{};
                    return false;
                }
            }
        }
        output->raw_caret_utf8 = api_->get_caret_pos != nullptr ? api_->get_caret_pos(session)
                                                               : output->raw_input.size();
        if (output->raw_caret_utf8 > output->raw_input.size()) {
            SetError(error, "librime caret is outside the raw input");
            *output = EngineSnapshot{};
            return false;
        }
        if (!output->preedit.empty()) {
            output->composing = true;
        }
        return true;
    }

    bool StartWatchLocked() noexcept {
        watch_stop_ = false;
        if (!watch_thread_.joinable()) {
            try { watch_thread_ = std::thread([this] { WatchMaintenance(); }); }
            catch (...) { return false; }
        }
        watch_cv_.notify_all();
        return true;
    }

    void WatchMaintenance() noexcept {
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!watch_stop_) {
                watch_cv_.wait(lock, [this] {
                    return watch_stop_ || (external_pending_ && !worker_notified_);
                });
                if (watch_stop_) return;
                if (!deployment_.Poll()) {
                    watch_cv_.wait_for(lock, std::chrono::milliseconds(20),
                                       [this] { return watch_stop_; });
                    continue;
                }
                worker_notified_ = true;
                lock.unlock();
                NotifyDeployReady();
                lock.lock();
            }
        } catch (...) {}
    }

    void NotifyDeployReady() noexcept {
        RimeEngine::DeployReadyCallback callback;
        try {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = deploy_ready_;
        } catch (...) { return; }
        if (callback) {
            try { callback(); } catch (...) {}
        }
    }

    void StopLocked() noexcept {
        healthy_ = false;
        deployment_.Stop();
        external_pending_ = false;
        if (api_ != nullptr) {
            for (const SessionId session : sessions_) {
                api_->destroy_session(session);
            }
            sessions_.clear();
            if (initialized_ && api_->finalize != nullptr) {
                api_->finalize();
            }
        }
        deploying_ = false;
        initialized_ = false;
        worker_notified_ = false;
        api_ = nullptr;
        shared_data_dir_.clear();
        user_data_dir_.clear();
        log_dir_.clear();
        ReleaseBuildHeap();
        user_lock_.Release();
    }

    RimeUserLock user_lock_;
    mutable std::mutex mutex_;
    std::mutex callback_mutex_;
    RimeApi* api_ = nullptr;
    bool initialized_ = false;
    bool healthy_ = false;
    bool deploying_ = false;
    bool watch_stop_ = false;
    bool external_pending_ = false;
    bool worker_notified_ = false;
    std::condition_variable watch_cv_;
    DeploymentProcess deployment_;
    std::filesystem::path deployment_helper_;
    std::thread watch_thread_;
    RimeEngine::DeployReadyCallback deploy_ready_;
    std::string shared_data_dir_;
    std::string user_data_dir_;
    std::string log_dir_;
    std::string schema_storage_;
    std::string option_storage_;
    std::unordered_set<SessionId> sessions_;
};

RimeEngine::RimeEngine() : impl_(std::make_unique<Impl>()) {}

RimeEngine::~RimeEngine() { Stop(); }

bool RimeEngine::Start(const RimeEngineOptions& options, std::string* error) noexcept {
    return impl_->Start(options, error);
}

void RimeEngine::Stop() noexcept { impl_->Stop(); }

void RimeEngine::SetDeployReadyCallback(DeployReadyCallback callback) {
    impl_->SetDeployReadyCallback(std::move(callback));
}

bool RimeEngine::IsHealthy() const noexcept { return impl_->IsHealthy(); }

bool RimeEngine::IsDeploying() const noexcept { return impl_->IsDeploying(); }

bool RimeEngine::PollMaintenance(std::string* error) noexcept {
    return impl_->PollMaintenance(error);
}

bool RimeEngine::RunMaintenance(bool full_check, std::string* error) noexcept {
    return impl_->RunMaintenance(full_check, error);
}

bool RimeEngine::StartBackgroundMaintenance(bool full_check,
                                            std::string* error) noexcept {
    return impl_->StartBackgroundMaintenance(full_check, error);
}

RimeEngine::SessionId RimeEngine::CreateSession(std::string* error) noexcept {
    return impl_->CreateSession(error);
}

bool RimeEngine::DestroySession(SessionId session, std::string* error) noexcept {
    return impl_->DestroySession(session, error);
}

bool RimeEngine::SelectSchema(SessionId session,
                              std::string_view schema_id,
                              std::string* error) noexcept {
    return impl_->SelectSchema(session, schema_id, error);
}

bool RimeEngine::SetOption(SessionId session,
                           std::string_view option,
                           bool value,
                           std::string* error) noexcept {
    return impl_->SetOption(session, option, value, error);
}

bool RimeEngine::ProcessKey(SessionId session,
                            std::int32_t keycode,
                            std::int32_t modifiers,
                            EngineSnapshot* output,
                            std::string* error) noexcept {
    return impl_->ProcessKey(session, keycode, modifiers, output, error);
}

bool RimeEngine::SelectCandidate(SessionId session,
                                 int index_on_page,
                                 EngineSnapshot* output,
                                 std::string* error) noexcept {
    return impl_->SelectCandidate(session, index_on_page, output, error);
}

bool RimeEngine::ClearComposition(SessionId session,
                                  EngineSnapshot* output,
                                  std::string* error) noexcept {
    return impl_->ClearComposition(session, output, error);
}

bool RimeEngine::CommitComposition(SessionId session,
                                   EngineSnapshot* output,
                                   std::string* error) noexcept {
    return impl_->CommitComposition(session, output, error);
}

bool RimeEngine::Refresh(SessionId session,
                         EngineSnapshot* output,
                         std::string* error) noexcept {
    return impl_->Refresh(session, output, error);
}

}  // namespace rimes::linuxime
