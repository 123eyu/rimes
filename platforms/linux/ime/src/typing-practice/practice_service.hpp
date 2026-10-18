#pragma once
#include <fcitx/event.h>
#include <memory>
namespace fcitx {
class RimesIme;
class PracticeService {
  public:
    explicit PracticeService(RimesIme &ime);
    ~PracticeService();
    void Enable(bool enabled);
    void Boundary(const std::string &reason = "interrupted");
    void Key(KeyEvent &event, bool composing);
    void Chord(InputContext *ic);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace fcitx
