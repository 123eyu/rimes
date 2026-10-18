#pragma once
#include <Windows.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rimes::windows::peripherals {
// Bounded VT screen. OSC/DCS payloads are ignored, including clipboard and
// hyperlink requests; process output cannot perform host operations.
class TerminalScreen {
 public:
  TerminalScreen(unsigned columns = 100, unsigned rows = 28);
  void Resize(unsigned columns, unsigned rows);
  std::string Feed(const std::string& bytes);
  std::wstring Text() const;
  unsigned Columns() const { return columns_; }
  unsigned Rows() const { return rows_; }

 private:
  unsigned columns_, rows_, x_ = 0, y_ = 0, saved_x_ = 0, saved_y_ = 0;
  unsigned top_ = 0, bottom_ = 0;
  std::vector<std::wstring> cells_;
  std::string sequence_, utf8_;
  int mode_ = 0;
  void Put(wchar_t);
  void NewLine();
  std::string CSI(char);
};
class Terminal {
 public:
  Terminal();
  ~Terminal();
  void Start(const std::wstring& executable, std::wstring arguments,
             const std::wstring& directory);
  bool Input(std::string);
  void Resize(unsigned columns, unsigned rows);
  std::wstring Output();
  bool Running() const;
  void End();

 private:
  HPCON console_ = nullptr;
  HANDLE process_ = nullptr, input_ = nullptr, output_ = nullptr,
         job_ = nullptr;
  mutable std::mutex mutex_;
  std::condition_variable event_;
  std::deque<std::string> pending_;
  std::size_t queued_ = 0;
  bool stopping_ = false;
  TerminalScreen screen_;
  std::jthread reader_, writer_;
};
}  // namespace rimes::windows::peripherals
