#include "session/protected_input.hpp"
#include "rimes_state.hpp"
#include "chord/chord_service.hpp"
#include "statistics/metrics_service.hpp"

#include <cstdint>
#include <string>
#include <utility>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterface.h>

#include "engine/rime_key.hpp"
#include "rimes_candidate.hpp"

namespace fcitx {
namespace {

FCITX_DEFINE_LOG_CATEGORY(rimes_log, "rimes");

}  // namespace

RimesState::RimesState(RimesIme* ime, InputContext* ic) : ime_(ime), ic_(ic) {}

RimesState::~RimesState() {
    chord_timer_.reset();
    if (session_ != 0) {
        ime_->engine().DestroySession(session_);
        session_ = 0;
    }
}

rimes::linuxime::RimeEngine::SessionId RimesState::EnsureSession() {
    if (session_ != 0) {
        return session_;
    }
    if (!ime_->engine().IsHealthy()) {
        return 0;
    }
    std::string error;
    session_ = ime_->engine().CreateSession(&error);
    if (session_ == 0) {
        FCITX_LOGC(rimes_log, Error) << "session create failed";
    }
    if (session_ != 0 && !ime_->initialSchema().empty() &&
        !ime_->engine().SelectSchema(session_, ime_->initialSchema(), &error)) {
        ime_->engine().DestroySession(session_); session_ = 0;
        FCITX_LOGC(rimes_log, Error) << "imported schema could not be selected";
    }
    if (session_ != 0 && ime_->initialSchema().empty() && ime_->chord()) {
        const auto schema = ime_->chord()->SelectedSchema();
        if (!schema.empty()) (void)ime_->engine().SelectSchema(session_, schema, &error);
    }
    if (session_ != 0) {
        (void)ime_->engine().Refresh(session_, &snapshot_);
        schema_id_ = snapshot_.schema_id;
    }
    return session_;
}

std::int32_t RimesState::ModifierMask(const KeyEvent& event) const {
    const auto states = event.rawKey().states();
    std::int32_t mask = 0;
    if (states.test(KeyState::Shift)) {
        mask |= rimes::linuxime::kShiftMask;
    }
    if (states.test(KeyState::CapsLock)) {
        mask |= rimes::linuxime::kLockMask;
    }
    if (states.test(KeyState::Ctrl)) {
        mask |= rimes::linuxime::kControlMask;
    }
    if (states.test(KeyState::Alt)) {
        mask |= rimes::linuxime::kMod1Mask;
    }
    if (states.test(KeyState::Super)) {
        mask |= rimes::linuxime::kSuperMask | rimes::linuxime::kSuper2Mask;
    }
    if (event.isRelease()) {
        mask |= rimes::linuxime::kReleaseMask;
    }
    return mask;
}

void RimesState::keyEvent(KeyEvent& event) {
    if (ime_->engine().IsDeploying()) {
        return;
    }
    if (!ime_->engine().IsHealthy()) {
        return;
    }
    const auto session = EnsureSession();
    if (session == 0) {
        return;
    }

    if (HandleCandidateKey(event)) return;

    if (!event.isRelease()) {
        int select_index = -1;
        if (ime_->initialSchema().empty() && composing_ &&
            rimes::linuxime::IsCandidateSelectDigit(static_cast<std::int32_t>(event.key().sym()),
                                                    &select_index)) {
            selectCandidate(select_index);
            event.filterAndAccept();
            return;
        }
        if (ime_->initialSchema().empty() && composing_ && event.key().check(FcitxKey_Escape)) {
            reset();
            event.filterAndAccept();
            return;
        }
        if (ime_->initialSchema().empty() && composing_ && event.key().check(FcitxKey_Page_Down)) {
            page(true);
            event.filterAndAccept();
            return;
        }
        if (ime_->initialSchema().empty() && composing_ && event.key().check(FcitxKey_Page_Up)) {
            page(false);
            event.filterAndAccept();
            return;
        }
    }

    rimes::linuxime::EngineSnapshot snapshot;
    std::string error;
    const auto keysym = static_cast<std::int32_t>(event.rawKey().sym());
    if (!ime_->engine().ProcessKey(session, keysym, ModifierMask(event), &snapshot, &error)) {
        FCITX_LOGC(rimes_log, Error) << "process_key failed";
        return;
    }
    applySnapshot(snapshot);
    ime_->buffer().AfterRime(ic_, event, snapshot.handled, composing_, snapshot.preedit,
                             snapshot.raw_input);
    if (snapshot.handled) {
        event.filterAndAccept();
    }
}

void RimesState::activate() {
    cancelChord(false);
    (void)EnsureSession();
}

void RimesState::retireSession() {
    reset();
    if (session_) {
        ime_->engine().DestroySession(session_);
        session_ = 0;
    }
    snapshot_ = {};
    schema_id_.clear();
    pending_schema_.clear();
    composing_ = false;
}

void RimesState::cancelChord(bool fallback) {
    metrics_chord_start_ = 0;
    metrics_native_keys_ = metrics_native_releases_ = 0;
    ++chord_epoch_;
    chord_.Cancel();
    chord_down_.clear();
    chord_lease_.reset();
    if (chord_timer_)
        chord_timer_->setEnabled(false);
    // Release native composer state inside this IC's private session without
    // delivering any result into a displaced or protected input field.
    if (session_ && !native_chord_down_.empty()) {
        rimes::linuxime::EngineSnapshot discarded;
        for (const auto& [physical, key] : native_chord_down_) {
            (void)physical;
            (void)ime_->engine().ProcessKey(session_, key, rimes::linuxime::kReleaseMask,
                                            &discarded);
        }
        (void)ime_->engine().ClearComposition(session_, &snapshot_);
    }
    native_chord_down_.clear();
    if (fallback && session_ && ime_->chord() && rimes::chord::IsChordSchema(schema_id_) &&
        (!ime_->chord()->Enabled() || schema_id_ != ime_->chord()->Profile().SchemaID())) {
        (void)ime_->engine().ClearComposition(session_, &snapshot_);
        (void)ime_->engine().SelectSchema(session_, ime_->chord()->FallbackSchema());
        (void)ime_->engine().Refresh(session_, &snapshot_);
    }
    if (fallback && session_ && ime_->initialSchema().empty() && ime_->chord() &&
        ime_->chord()->Enabled()) {
        const auto desired = ime_->chord()->SelectedSchema();
        if (rimes::chord::IsChordSchema(desired) && snapshot_.schema_id != desired) {
            if (snapshot_.composing || !snapshot_.raw_input.empty())
                pending_schema_ = desired;
            else {
                (void)ime_->engine().SelectSchema(session_, desired);
                (void)ime_->engine().Refresh(session_, &snapshot_);
                pending_schema_.clear();
            }
        }
    }
    if (session_)
        UpdateUI(snapshot_);
}

bool RimesState::ChordAuthority(std::uint64_t epoch, std::uint64_t revision,
                                const rimes::plugins::Lease& lease) {
    return epoch == chord_epoch_ && ime_->chord() && ime_->chord()->Enabled() &&
           revision == ime_->chord()->Revision() && ic_->hasFocus() &&
           ime_->instance()->inputMethod(ic_) == "rimes" &&
           !ProtectedInput(ic_->capabilityFlags()) && ime_->acceptsPluginResult(lease) &&
           epoch == chord_epoch_ && revision == ime_->chord()->Revision();
}
void RimesState::PublishChord(const rimes::linuxime::EngineSnapshot& snapshot) {
    applySnapshot(snapshot);
    KeyEvent update(ic_, Key(), false);
    ime_->buffer().AfterRime(ic_, update, true, composing(), snapshot.preedit, snapshot.raw_input);
}
void RimesState::ObserveNativeRelease(bool handled, bool last) {
    if (handled) ++metrics_native_releases_;
    if (!last) return;
    if (metrics_chord_start_ && metrics_native_keys_ > 0 &&
        metrics_native_releases_ == metrics_native_keys_ && ime_->metrics())
        ime_->metrics()->Chord(ic_, double(now(CLOCK_MONOTONIC) - metrics_chord_start_) / 1e6,
                               metrics_native_releases_);
    metrics_chord_start_ = 0;
    metrics_native_keys_ = metrics_native_releases_ = 0;
}
void RimesState::FlushChord() {
    if (chord_timer_)
        chord_timer_->setEnabled(false);
    if (!chord_.Pending() || !chord_lease_ || !ime_->chord())
        return;
    const auto lease = *chord_lease_;
    const auto epoch = batch_epoch_, revision = chord_revision_;
    const auto result = chord_.Flush(
        ime_->engine(), session_, ime_->chord()->Profile(),
        [this, lease, epoch, revision] { return ChordAuthority(epoch, revision, lease); });
    if (result && ChordAuthority(epoch, revision, lease)) {
        if (chord_.LastBatchCount() && metrics_chord_start_ && ime_->metrics())
            ime_->metrics()->Chord(ic_, double(now(CLOCK_MONOTONIC) - metrics_chord_start_) / 1e6,
                                   static_cast<int>(chord_.LastBatchCount()));
        metrics_chord_start_ = 0;
        PublishChord(*result);
    } else
        cancelChord(false);
}
bool RimesState::prepareChord(KeyEvent& event) {
    auto* service = ime_->chord();
    if (!service)
        return false;
    if (!service->Enabled() || !ime_->engine().IsHealthy() || !ic_->hasFocus() ||
        ProtectedInput(ic_->capabilityFlags())) {
        if (chord_.Pending() || !chord_down_.empty() || !native_chord_down_.empty() ||
            rimes::chord::IsChordSchema(schema_id_))
            cancelChord(true);
        return false;
    }
    if (chord_revision_ && chord_revision_ != service->Revision())
        cancelChord(false);
    if (!EnsureSession())
        return false;
    const auto lease = ime_->plugins().Authorize(rimes::chord::kPluginID, "input.chord");
    if (!lease || !ime_->acceptsPluginResult(*lease) || !session_) {
        cancelChord(true);
        return false;
    }
    const auto& profile = service->Profile();
    const auto key = static_cast<std::int32_t>(event.rawKey().sym());
    const auto physical =
        event.rawKey().code() ? event.rawKey().code() : static_cast<std::uint32_t>(key);
    const auto mask = ModifierMask(event);
    const bool plain = (mask & (rimes::linuxime::kShiftMask | rimes::linuxime::kControlMask |
                                rimes::linuxime::kMod1Mask | rimes::linuxime::kSuperMask |
                                rimes::linuxime::kSuper2Mask)) == 0;
    const bool active = !snapshot_.ascii_mode && schema_id_ == profile.SchemaID();
    if (event.isRelease()) {
        if (chord_down_.erase(physical)) {
            event.filterAndAccept();
            return true;
        }
        const auto held = native_chord_down_.find(physical);
        if (held == native_chord_down_.end())
            return false;
        const auto release_key = held->second;
        native_chord_down_.erase(held);
        if (chord_lease_ && ChordAuthority(batch_epoch_, chord_revision_, *chord_lease_)) {
            rimes::linuxime::EngineSnapshot result;
            if (ime_->engine().ProcessKey(session_, release_key, rimes::linuxime::kReleaseMask,
                                          &result)) {
                ObserveNativeRelease(result.handled, native_chord_down_.empty());
                PublishChord(result);
            } else ObserveNativeRelease(false, native_chord_down_.empty());
        } else
            cancelChord(false);
        event.filterAndAccept();
        return true;
    }
    const bool native = !profile.native_schema.empty();
    const bool chord_key =
        active && plain && (profile.KeyHalf(key).has_value() || (native && key == ' '));
    if (!chord_key) {
        FlushChord();
        chord_.BreakPairing();
        // A shortcut or modifier is a native batch boundary too. Settle it
        // synchronously while its exact target is still valid.
        if (!native_chord_down_.empty()) {
            auto held = std::move(native_chord_down_);
            native_chord_down_.clear();
            std::size_t remaining = held.size();
            for (const auto& [code, release_key] : held) {
                (void)code;
                --remaining;
                if (!chord_lease_ || !ChordAuthority(batch_epoch_, chord_revision_, *chord_lease_))
                    break;
                rimes::linuxime::EngineSnapshot result;
                if (ime_->engine().ProcessKey(session_, release_key, rimes::linuxime::kReleaseMask,
                                              &result)) {
                    ObserveNativeRelease(result.handled, remaining == 0);
                    PublishChord(result);
                } else ObserveNativeRelease(false, remaining == 0);
            }
            metrics_chord_start_ = 0;
            metrics_native_keys_ = metrics_native_releases_ = 0;
        }
        return false;
    }
    chord_lease_ = lease;
    chord_revision_ = service->Revision();
    batch_epoch_ = chord_epoch_;
    if (native) {
        if (native_chord_down_.emplace(physical, key).second) {
            if (native_chord_down_.size() == 1) {
                metrics_chord_start_ = now(CLOCK_MONOTONIC);
                metrics_native_keys_ = metrics_native_releases_ = 0;
            }
            rimes::linuxime::EngineSnapshot result;
            if (!ime_->engine().ProcessKey(session_, key, 0, &result)) {
                native_chord_down_.erase(physical);
                cancelChord(false);
                return false;
            }
            PublishChord(result);
            if (!result.handled) {
                native_chord_down_.erase(physical);
                return false;
            }
            ++metrics_native_keys_;
        }
        event.filterAndAccept();
        return true;
    }
    if (chord_down_.emplace(physical, key).second && chord_.Stage(key, profile, snapshot_)) {
        if (!metrics_chord_start_) metrics_chord_start_ = now(CLOCK_MONOTONIC);
        const auto deadline = now(CLOCK_MONOTONIC) + service->DurationMicros();
        if (!chord_timer_)
            chord_timer_ = ime_->instance()->eventLoop().addTimeEvent(
                CLOCK_MONOTONIC, deadline, 1000, [this](EventSourceTime*, std::uint64_t) {
                    FlushChord();
                    return true;
                });
        else {
            chord_timer_->setTime(deadline);
            chord_timer_->setOneShot();
        }
    }
    // Pending raw keys are a server/workbench preview only. Sending a fake
    // marked string to GTK/Qt clients would leak it on focus-out.
    UpdateUI(snapshot_);
    const auto preview = snapshot_.preedit + chord_.Keys();
    Text pending;
    pending.append(preview, TextFormatFlag::Underline);
    ic_->inputPanel().setCandidateList(nullptr);
    ic_->inputPanel().setPreedit(pending);
    ime_->buffer().AfterRime(ic_, event, true, true, preview, snapshot_.raw_input);
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    event.filterAndAccept();
    return true;
}

void RimesState::reset() {
    cancelChord(false);
    if (matrix_) matrix_->hide();
    navigation_.Reset({}); ++candidate_epoch_;
    if (session_ == 0) {
        return;
    }
    rimes::linuxime::EngineSnapshot snapshot;
    std::string error;
    ime_->engine().ClearComposition(session_, &snapshot, &error);
    applySnapshot(snapshot);
}

void RimesState::deactivate(const InputContextEvent& event) {
    if (session_ == 0) {
        return;
    }
    // Focus-out already commits the client preedit in the frontend
    // (GTK/Qt). Extra commit_composition here produced "ni hao你好".
    // Match stock fcitx5-rime: only commit on an explicit IM switch.
    if (event.type() == EventType::InputContextSwitchInputMethod) {
        rimes::linuxime::EngineSnapshot snapshot;
        std::string error;
        ime_->engine().CommitComposition(session_, &snapshot, &error);
        applySnapshot(snapshot);
    }
    reset();
}

void RimesState::selectCandidate(int index) {
    FlushChord(); chord_.BreakPairing();
    const auto session = EnsureSession();
    if (session == 0) {
        return;
    }
    rimes::linuxime::EngineSnapshot snapshot;
    std::string error;
    if (!ime_->engine().SelectCandidate(session, index, &snapshot, &error)) {
        return;
    }
    applySnapshot(snapshot);
}

void RimesState::page(bool next) {
    FlushChord(); chord_.BreakPairing();
    if (navigation_.expanded()) { MoveMatrixRow(next ? 1 : -1); return; }
    const auto session = EnsureSession();
    if (session == 0) {
        return;
    }
    rimes::linuxime::EngineSnapshot snapshot;
    std::string error;
    const auto key = next ? rimes::linuxime::kPageDown : rimes::linuxime::kPageUp;
    if (!ime_->engine().ProcessKey(session, key, 0, &snapshot, &error)) {
        return;
    }
    applySnapshot(snapshot);
}

void RimesState::applySnapshot(const rimes::linuxime::EngineSnapshot& snapshot) {
    if (schema_id_ != snapshot.schema_id || snapshot_.ascii_mode != snapshot.ascii_mode) {
        chord_.Cancel(); chord_down_.clear();
        if (chord_timer_) chord_timer_->setEnabled(false);
        if (ime_->chord()) ime_->chord()->ObserveSchema(snapshot.schema_id);
    }
    snapshot_ = snapshot; navigation_.Reset(snapshot);
    ime_->applySnapshot(ic_, snapshot);
    if (!pending_schema_.empty() && !snapshot.composing && snapshot.raw_input.empty() && ime_->chord() && ime_->chord()->Enabled()) {
        const auto desired = std::exchange(pending_schema_, {});
        (void)ime_->engine().SelectSchema(session_, desired);
        (void)ime_->engine().Refresh(session_, &snapshot_);
    }
    UpdateUI(snapshot_);
}

void RimesState::refreshCandidates() { UpdateUI(snapshot_); }

std::optional<rimes::linuxime::EngineSnapshot> RimesState::StepCandidatePage(int delta) {
    rimes::linuxime::EngineSnapshot next;
    std::string error;
    if (!ime_->engine().ProcessKey(session_, delta < 0 ? rimes::linuxime::kPageUp : rimes::linuxime::kPageDown, 0, &next, &error)) return std::nullopt;
    const bool navigation = next.commit_text.empty() && next.raw_input == snapshot_.raw_input && next.schema_id == snapshot_.schema_id && !next.candidates.empty();
    snapshot_ = next;
    return navigation ? std::optional<rimes::linuxime::EngineSnapshot>(next) : std::nullopt;
}
void RimesState::MoveMatrixRow(int delta) {
    if (!navigation_.MoveRow(delta, [this](int direction) { return StepCandidatePage(direction); })) {
        applySnapshot(snapshot_); return;
    }
    UpdateUI(snapshot_);
}
void RimesState::SelectMatrixIndex(std::size_t index) {
    if (!navigation_.SelectVisibleIndex(index, [this](int direction) { return StepCandidatePage(direction); })) {
        applySnapshot(snapshot_); return;
    }
    selectCandidate(static_cast<int>(navigation_.column()));
}
bool RimesState::HandleCandidateKey(KeyEvent& event) {
    const auto key = static_cast<std::uint32_t>(event.rawKey().sym());
    if (event.isRelease()) {
        if (owned_keys_.erase(key)) { event.filterAndAccept(); return true; }
        return false;
    }
    const auto modifiers = ModifierMask(event) & ~rimes::linuxime::kLockMask;
    if (modifiers || navigation_.empty() || !ic_->hasFocus() || ProtectedInput(ic_->capabilityFlags())) return false;
    const bool expanded = navigation_.expanded();
    const bool arrow = key == FcitxKey_Left || key == FcitxKey_Right || key == FcitxKey_Up || key == FcitxKey_Down;
    const bool paging = expanded && (key == FcitxKey_Page_Up || key == FcitxKey_Page_Down || key == '-' || key == '=');
    const bool number = expanded && key >= '1' && key <= '9';
    const bool collapse = expanded && key == FcitxKey_Escape;
    if (!arrow && !paging && !number && !collapse && key != FcitxKey_space) return false;
    owned_keys_.insert(key); event.filterAndAccept();
    if (key == FcitxKey_Left || key == FcitxKey_Right) {
        navigation_.MoveColumn(key == FcitxKey_Left ? -1 : 1);
        snapshot_.highlighted = static_cast<int>(navigation_.column());
        UpdateUI(snapshot_);
    } else if (arrow || paging) {
        MoveMatrixRow(key == FcitxKey_Up || key == FcitxKey_Page_Up || key == '-' ? -1 : 1);
    } else if (collapse) {
        if (!navigation_.CollapseToAnchor([this](int direction) { return StepCandidatePage(direction); })) applySnapshot(snapshot_);
        else { snapshot_.highlighted = static_cast<int>(navigation_.column()); UpdateUI(snapshot_); }
    } else if (number) {
        if (matrix_visible_) {
            if (auto index = matrix_->digit(key - '1')) SelectMatrixIndex(*index);
        } else if (auto selection = navigation_.Selected(key - '1')) {
            selectCandidate(static_cast<int>(selection->index));
        }
    } else {
        selectCandidate(static_cast<int>(navigation_.column()));
    }
    return true;
}

void RimesState::UpdateUI(const rimes::linuxime::EngineSnapshot& snapshot) {
    composing_ = snapshot.composing || !snapshot.preedit.empty();
    if (!snapshot.schema_id.empty()) {
        schema_id_ = snapshot.schema_id;
    }

    auto& panel = ic_->inputPanel();
    panel.reset();

    Text preedit;
    if (!snapshot.preedit.empty()) {
        preedit.append(snapshot.preedit, TextFormatFlag::Underline);
        preedit.setCursor(static_cast<int>(snapshot.caret_utf8));
    }
    const bool capturing = ime_->buffer().model().captures(
        std::to_string(reinterpret_cast<std::uintptr_t>(ic_)));
    // Project real preedit into the workbench. Do not put U+200B in the host
    // client preedit — GTK, VTE and Gecko commit that guard on focus-out.
    if (capturing) {
        panel.setClientPreedit(Text());
        panel.setPreedit(Text());
    } else if (!native_chord_down_.empty()) {
        // A held native chord is still provisional. GTK/Qt commit their client
        // preedit on focus-out before the engine can discard the held keys.
        panel.setClientPreedit(Text());
        panel.setPreedit(preedit);
    } else if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
        panel.setClientPreedit(preedit);
        panel.setPreedit(Text());
    } else {
        panel.setClientPreedit(Text());
        panel.setPreedit(preedit);
    }

    ++candidate_epoch_;
    if (matrix_) matrix_->hide();
    matrix_visible_ = false;
    if (!snapshot.candidates.empty()) {
        panel.setCandidateList(std::make_unique<RimesCandidateList>(ime_, ic_, snapshot));
    }

    if (navigation_.expanded()) {
        if (!matrix_) matrix_ = std::make_unique<CandidateMatrixWindow>(ime_->instance());
        const auto epoch = candidate_epoch_;
        matrix_visible_ = matrix_->show(ic_, navigation_,
            [this, epoch](std::size_t index) {
                if (epoch == candidate_epoch_ && ic_->hasFocus()) SelectMatrixIndex(index);
            }, [this, epoch](int delta) {
                if (epoch == candidate_epoch_ && ic_->hasFocus()) MoveMatrixRow(delta);
            });
        if (matrix_visible_) {
            // Keep native client preedit, suppress only the stock candidate popup.
            panel.setCustomInputPanelCallback([](InputContext*) {});
        }
    }
    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

}  // namespace fcitx
