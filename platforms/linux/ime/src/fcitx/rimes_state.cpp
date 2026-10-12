#include "rimes_state.hpp"

#include <cstdint>
#include <string>

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

void RimesState::activate() { (void)EnsureSession(); }

void RimesState::reset() {
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
    ime_->applySnapshot(ic_, snapshot);
    UpdateUI(snapshot);
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
    } else if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
        panel.setClientPreedit(preedit);
        panel.setPreedit(Text());
    } else {
        panel.setClientPreedit(Text());
        panel.setPreedit(preedit);
    }

    if (!snapshot.candidates.empty()) {
        panel.setCandidateList(std::make_unique<RimesCandidateList>(ime_, ic_, snapshot));
    }

    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

}  // namespace fcitx
