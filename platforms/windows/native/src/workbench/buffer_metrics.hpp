#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace rimes::windows::workbench {
// BufferLiveTypingMetrics.swift: one burst, six seconds of silence between
// bursts; physical repeats, pasted text and generated output are not typing.
struct BufferLiveMetrics {
  unsigned keys = 0, characters = 0, backspaces = 0;
  std::optional<std::uint64_t> first, last;
  void Reset() { *this = {}; }
  void Event(std::uint64_t now) {
    if (last && now > *last + 6000) Reset();
    if (!first) first = now;
    last = last ? (std::max)(*last, now) : now;
  }
  void Key(std::uint64_t now, bool repeat, bool backspace) {
    if (repeat) return;
    Event(now); ++keys; if (backspace) ++backspaces;
  }
  void Commit(std::uint64_t now, unsigned count) {
    if (!count) return;
    Event(now); characters += count;
  }
  std::string Line() const {
    std::string line;
    const auto append = [&](const std::string& value) {
      if (!line.empty()) line += "  ·  ";
      line += value;
    };
    const double seconds = first && last ? (*last - *first) / 1000.0 : 0;
    char value[64]{};
    if (seconds >= 1 && characters) {
      std::snprintf(value, sizeof(value), "%.0f 字/分", characters * 60.0 / seconds); append(value);
    }
    if (characters) {
      std::snprintf(value, sizeof(value), "码长 %.2f", double(keys) / characters); append(value);
    }
    if (seconds >= 1 && keys) {
      std::snprintf(value, sizeof(value), "击键 %.1f/秒", keys / seconds); append(value);
    }
    if (backspaces) append("回删 " + std::to_string(backspaces));
    return line.empty() ? "— 字/分  ·  码长 —  ·  击键 —/秒" : line;
  }
};
}
