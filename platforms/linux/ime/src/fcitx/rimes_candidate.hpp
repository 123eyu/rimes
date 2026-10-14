#pragma once

#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>

#include "engine/rime_snapshot.hpp"

namespace fcitx {

class RimesIme;

class RimesCandidateWord final : public CandidateWord {
public:
    RimesCandidateWord(RimesIme* ime, int index, Text text, const std::string& comment);
    void select(InputContext* inputContext) const override;

private:
    RimesIme* ime_;
    int index_;
};

class RimesCandidateList final : public CandidateList, public PageableCandidateList {
public:
    RimesCandidateList(RimesIme* ime,
                       InputContext* ic,
                       const rimes::linuxime::EngineSnapshot& snapshot);

    const Text& label(int idx) const override;
    const CandidateWord& candidate(int idx) const override;
    int size() const override;
    int cursorIndex() const override;
    CandidateLayoutHint layoutHint() const override;

    bool hasPrev() const override { return has_prev_; }
    bool hasNext() const override { return has_next_; }
    void prev() override;
    void next() override;
    bool usedNextBefore() const override { return true; }

private:
    void checkIndex(int idx) const;

    RimesIme* ime_;
    InputContext* ic_;
    std::vector<Text> labels_;
    std::vector<std::unique_ptr<CandidateWord>> words_;
    int cursor_ = -1;
    bool has_prev_ = false;
    bool has_next_ = false;
};

}  // namespace fcitx
