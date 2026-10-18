#pragma once
#include "model.hpp"
#include <algorithm>

namespace rimes::windows::workbench {
// BufferAutoSendClock.swift: age every live block, pause without adding hidden
// time, and reset replaced text. Only acknowledged consumption removes a block.
class BufferAutoSendClock {
 public:
  struct Entry { std::string text; std::uint64_t age = 0; std::optional<std::uint64_t> last; };
  void Reset() { entries_.clear(); }
  void Pause() { for (auto& [id, entry] : entries_) entry.last.reset(); }
  void Synchronize(const std::deque<Block>& blocks) {
    std::erase_if(entries_, [&](const auto& item) {
      return std::none_of(blocks.begin(), blocks.end(), [&](const auto& block) { return item.first == block.id; });
    });
    for (const auto& block : blocks) {
      auto& entry = entries_[block.id];
      if (entry.text != block.text) entry = {block.text};
    }
  }
  bool Tick(const std::deque<Block>& blocks, std::uint64_t now, bool enabled, unsigned lifetime_ms) {
    Synchronize(blocks);
    if (!enabled || !lifetime_ms) { Pause(); return false; }
    for (auto& [id, entry] : entries_) {
      const auto elapsed = entry.last && now >= *entry.last ? now - *entry.last : 0;
      entry.age = (std::min)(std::uint64_t(lifetime_ms), entry.age + (std::min)(elapsed, std::uint64_t(lifetime_ms)));
      entry.last = now;
    }
    return !blocks.empty() && entries_.at(blocks.front().id).age >= lifetime_ms;
  }
  std::uint64_t Age(std::uint64_t id) const {
    const auto found = entries_.find(id); return found == entries_.end() ? 0 : found->second.age;
  }
 private:
  std::map<std::uint64_t, Entry> entries_;
};
}
