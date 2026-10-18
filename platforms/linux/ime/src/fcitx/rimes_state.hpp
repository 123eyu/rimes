#pragma once

#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>

#include "engine/rime_engine.hpp"
#include "rimes_ime.hpp"
#include "candidate_navigation.hpp"
#include "candidate_matrix_window.hpp"
#include "chord/chord_session.hpp"
#include <map>
#include <set>

namespace fcitx {

class RimesState : public InputContextProperty {
public:
    RimesState(RimesIme* ime, InputContext* ic);
    ~RimesState() override;

    void keyEvent(KeyEvent& event);
    bool prepareChord(KeyEvent& event);
    void cancelChord(bool fallback);
    void retireSession();
    void activate();
    void reset();
    void deactivate(const InputContextEvent& event);
    void selectCandidate(int index);
    void page(bool next);
    void refreshCandidates();
    void applySnapshot(const rimes::linuxime::EngineSnapshot& snapshot);
    std::string schemaId() const { return schema_id_; }
    bool composing() const { return composing_ || chord_.Pending() || !native_chord_down_.empty(); }

private:
    rimes::linuxime::RimeEngine::SessionId EnsureSession();
    std::int32_t ModifierMask(const KeyEvent& event) const;
    bool HandleCandidateKey(KeyEvent& event);
    std::optional<rimes::linuxime::EngineSnapshot> StepCandidatePage(int delta);
    void SelectMatrixIndex(std::size_t index);
    void MoveMatrixRow(int delta);
    void UpdateUI(const rimes::linuxime::EngineSnapshot& snapshot);
    void FlushChord();
    void PublishChord(const rimes::linuxime::EngineSnapshot& snapshot);
    void ObserveNativeRelease(bool handled, bool last);
    bool ChordAuthority(std::uint64_t epoch, std::uint64_t revision,
                        const rimes::plugins::Lease& lease);

    rimes::linuxime::ChordSession chord_;
    std::unique_ptr<EventSourceTime> chord_timer_;
    std::map<std::uint32_t, std::int32_t> native_chord_down_, chord_down_;
    std::optional<rimes::plugins::Lease> chord_lease_;
    std::uint64_t chord_epoch_ = 0, chord_revision_ = 0, batch_epoch_ = 0;
    std::uint64_t metrics_chord_start_ = 0;
    int metrics_native_keys_ = 0, metrics_native_releases_ = 0;

    CandidateNavigation navigation_;
    std::unique_ptr<CandidateMatrixWindow> matrix_;
    rimes::linuxime::EngineSnapshot snapshot_;
    std::set<std::uint32_t> owned_keys_;
    std::uint64_t candidate_epoch_ = 0;
    bool matrix_visible_ = false;
    RimesIme* ime_;
    InputContext* ic_;
    rimes::linuxime::RimeEngine::SessionId session_ = 0;
    std::string schema_id_, pending_schema_;
    bool composing_ = false;
};

}  // namespace fcitx
