#include "terminal.hpp"

#include <algorithm>
#include <array>
#include <sstream>

#include "store.hpp"

namespace rimes::windows::peripherals {
TerminalScreen::TerminalScreen(unsigned c, unsigned r) : columns_(c), rows_(r) {
  Resize(c, r);
}
void TerminalScreen::Resize(unsigned c, unsigned r) {
  columns_ = std::clamp(c, 20U, 240U);
  rows_ = std::clamp(r, 5U, 100U);
  cells_.resize(rows_);
  for (auto& line : cells_) line.resize(columns_, L' ');
  x_ = (std::min)(x_, columns_ - 1);
  y_ = (std::min)(y_, rows_ - 1);
  top_ = 0;
  bottom_ = rows_ - 1;
}
void TerminalScreen::NewLine() {
  if (y_ == bottom_) {
    for (unsigned row = top_; row < bottom_; ++row)
      cells_[row] = cells_[row + 1];
    cells_[bottom_].assign(columns_, L' ');
  } else
    y_ = (std::min)(y_ + 1, rows_ - 1);
}
void TerminalScreen::Put(wchar_t c) {
  const bool wide =
      (c >= 0x2e80 && c <= 0xa4cf) || (c >= 0xac00 && c <= 0xd7a3) ||
      (c >= 0xf900 && c <= 0xfaff) || (c >= 0xfe10 && c <= 0xfe6f) ||
      (c >= 0xff01 && c <= 0xff60) || (c >= 0xffe0 && c <= 0xffe6);
  if (x_ >= columns_ || (wide && x_ + 1 >= columns_)) {
    x_ = 0;
    NewLine();
  }
  cells_[y_][x_++] = c;
  // Keep common East Asian text aligned with the ConPTY cell grid.
  if (wide && x_ < columns_) cells_[y_][x_++] = L'\0';
}
std::string TerminalScreen::CSI(char final) {
  std::vector<unsigned> p;
  unsigned n = 0;
  for (auto c : sequence_) {
    if (c >= '0' && c <= '9')
      n = (std::min)(10000U, n * 10 + c - '0');
    else if (c == ';') {
      p.push_back(n);
      n = 0;
    }
  }
  p.push_back(n);
  auto arg = [&](unsigned i, unsigned def = 1) {
    return i < p.size() && p[i] ? p[i] : def;
  };
  const auto count = arg(0);
  switch (final) {
    case 'H':
    case 'f':
      y_ = (std::min)(arg(0) - 1, rows_ - 1);
      x_ = (std::min)(arg(1) - 1, columns_ - 1);
      break;
    case 'A':
      y_ -= (std::min)(y_, count);
      break;
    case 'B':
      y_ = (std::min)(y_ + count, rows_ - 1);
      break;
    case 'C':
      x_ = (std::min)(x_ + count, columns_ - 1);
      break;
    case 'D':
      x_ -= (std::min)(x_, count);
      break;
    case 'E':
      y_ = (std::min)(y_ + count, rows_ - 1);
      x_ = 0;
      break;
    case 'F':
      y_ -= (std::min)(y_, count);
      x_ = 0;
      break;
    case 'G':
      x_ = (std::min)(count - 1, columns_ - 1);
      break;
    case 'd':
      y_ = (std::min)(count - 1, rows_ - 1);
      break;
    case 'J':
      for (unsigned row = 0; row < rows_; ++row)
        for (unsigned col = 0; col < columns_; ++col)
          if (p[0] >= 2 ||
              (p[0] == 0 && (row > y_ || (row == y_ && col >= x_))) ||
              (p[0] == 1 && (row < y_ || (row == y_ && col <= x_))))
            cells_[row][col] = L' ';
      break;
    case 'K':
      for (unsigned col = 0; col < columns_; ++col)
        if (p[0] == 2 || (p[0] == 0 && col >= x_) || (p[0] == 1 && col <= x_))
          cells_[y_][col] = L' ';
      break;
    case 'X':
      for (unsigned col = x_; col < (std::min)(x_ + count, columns_); ++col)
        cells_[y_][col] = L' ';
      break;
    case 'P':
      if (x_ < columns_) {
        cells_[y_].erase(x_, (std::min)(count, columns_ - x_));
        cells_[y_].resize(columns_, L' ');
      }
      break;
    case '@':
      if (x_ < columns_) {
        cells_[y_].insert(x_, (std::min)(count, columns_ - x_), L' ');
        cells_[y_].resize(columns_);
      }
      break;
    case 's':
      saved_x_ = x_;
      saved_y_ = y_;
      break;
    case 'u':
      x_ = (std::min)(saved_x_, columns_ - 1);
      y_ = (std::min)(saved_y_, rows_ - 1);
      break;
    case 'r':
      top_ = (std::min)(arg(0) - 1, rows_ - 1);
      bottom_ = std::clamp(arg(1, rows_) - 1, top_, rows_ - 1);
      x_ = y_ = 0;
      break;
    case 'S':
      for (unsigned i = 0; i < (std::min)(count, rows_); ++i) {
        const auto save = y_;
        y_ = bottom_;
        NewLine();
        y_ = save;
      }
      break;
    case 'n':
      if (p[0] == 6)
        return "\x1b[" + std::to_string(y_ + 1) + ";" +
               std::to_string((std::min)(x_ + 1, columns_)) + "R";
      if (p[0] == 5) return "\x1b[0n";
      break;
    case 'c':
      return "\x1b[?1;0c";
    default:
      break;
  }
  return {};
}
std::string TerminalScreen::Feed(const std::string& bytes) {
  std::string responses;
  for (unsigned char c : bytes) {
    if (mode_ == 3) {
      if (c == 7)
        mode_ = 0;
      else if (c == 27)
        mode_ = 4;
      continue;
    }
    if (mode_ == 4) {
      mode_ = c == '\\' ? 0 : 3;
      continue;
    }
    if (mode_ == 1) {
      mode_ = 0;
      if (c == '[') {
        mode_ = 2;
        sequence_.clear();
      } else if (c == ']' || c == 'P' || c == '^' || c == '_')
        mode_ = 3;
      else if (c == '7') {
        saved_x_ = x_;
        saved_y_ = y_;
      } else if (c == '8') {
        x_ = (std::min)(saved_x_, columns_ - 1);
        y_ = (std::min)(saved_y_, rows_ - 1);
      } else if (c == 'c') {
        for (auto& line : cells_) line.assign(columns_, L' ');
        x_ = y_ = 0;
      }
      continue;
    }
    if (mode_ == 2) {
      if (c >= 0x40 && c <= 0x7e) {
        responses += CSI(static_cast<char>(c));
        mode_ = 0;
      } else if (sequence_.size() < 128)
        sequence_ += static_cast<char>(c);
      else
        mode_ = 0;
      continue;
    }
    if (c == 27) {
      mode_ = 1;
      utf8_.clear();
      continue;
    }
    if (c == '\r') {
      x_ = 0;
      continue;
    }
    if (c == '\n') {
      NewLine();
      continue;
    }
    if (c == '\b') {
      if (x_) --x_;
      continue;
    }
    if (c == '\t') {
      x_ = (std::min)((x_ / 8 + 1) * 8, columns_ - 1);
      continue;
    }
    if (c < 32 || c == 127) continue;
    if (c < 128 && utf8_.empty()) {
      Put(c);
      continue;
    }
    utf8_ += static_cast<char>(c);
    const unsigned char first = utf8_[0];
    const unsigned length = first >= 0xf0   ? 4
                            : first >= 0xe0 ? 3
                            : first >= 0xc0 ? 2
                                            : 1;
    if (utf8_.size() >= length) {
      try {
        for (auto w : workbench::Wide(utf8_)) Put(w);
      } catch (...) {
        Put(L'\xfffd');
      }
      utf8_.clear();
    }
  }
  return responses;
}
std::wstring TerminalScreen::Text() const {
  std::wstring result;
  for (const auto& line : cells_) {
    for (const auto c : line)
      if (c) result += c;
    result += L"\r\n";
  }
  return result;
}
Terminal::Terminal() = default;
Terminal::~Terminal() { End(); }
void Terminal::Start(const std::wstring& executable, std::wstring arguments,
                     const std::wstring& directory) {
  if (process_) throw std::runtime_error("此终端已经启动。");
  if (!std::filesystem::is_regular_file(executable) ||
      !std::filesystem::is_directory(directory))
    throw std::runtime_error("终端程序或工作目录不可用。");
  job_ = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                        &limits, sizeof(limits))) {
    End();
    throw std::runtime_error("无法管理终端子进程生命周期。");
  }
  HANDLE read = nullptr, write = nullptr;
  if (!CreatePipe(&read, &input_, nullptr, 0) ||
      !CreatePipe(&output_, &write, nullptr, 0)) {
    if (read) CloseHandle(read);
    if (write) CloseHandle(write);
    End();
    throw std::runtime_error("无法创建终端通道。");
  }
  auto hr = CreatePseudoConsole({100, 28}, read, write, 0, &console_);
  CloseHandle(read);
  CloseHandle(write);
  if (FAILED(hr)) {
    End();
    throw std::runtime_error("Windows 伪控制台不可用。");
  }
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::vector<std::byte> bytes(size);
  auto attributes =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes.data());
  if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
    End();
    throw std::runtime_error("无法初始化终端进程。");
  }
  STARTUPINFOEXW info{};
  info.StartupInfo.cb = sizeof(info);
  info.lpAttributeList = attributes;
  PROCESS_INFORMATION process{};
  // A redirected Broker/test console otherwise leaks its standard handles
  // into the child instead of connecting them to the pseudoconsole.
  info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  std::wstring command = L"\"" + executable + L"\" " + arguments;
  const bool ok =
      UpdateProcThreadAttribute(attributes, 0,
                                PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console_,
                                sizeof(console_), nullptr, nullptr) &&
      CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                     FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED,
                     nullptr, directory.c_str(), &info.StartupInfo, &process);
  DeleteProcThreadAttributeList(attributes);
  if (!ok) {
    End();
    throw std::runtime_error("启动终端失败。");
  }
  process_ = process.hProcess;
  if (!AssignProcessToJobObject(job_, process_) ||
      ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
    TerminateProcess(process_, ERROR_PROCESS_ABORTED);
    CloseHandle(process.hThread);
    End();
    throw std::runtime_error("无法启动受管理的终端会话。");
  }
  CloseHandle(process.hThread);
  stopping_ = false;
  reader_ = std::jthread([this] {
    std::array<char, 8192> buffer{};
    DWORD count = 0;
    while (::ReadFile(output_, buffer.data(), static_cast<DWORD>(buffer.size()),
                      &count, nullptr) &&
           count) {
      std::string response;
      {
        std::lock_guard lock(mutex_);
        response = screen_.Feed(std::string(buffer.data(), count));
      }
      if (!response.empty()) Input(std::move(response));
    }
  });
  writer_ = std::jthread([this] {
    for (;;) {
      std::string bytes;
      {
        std::unique_lock lock(mutex_);
        event_.wait(lock, [&] { return stopping_ || !pending_.empty(); });
        if (stopping_) return;
        bytes = std::move(pending_.front());
        pending_.pop_front();
        queued_ -= bytes.size();
      }
      DWORD written = 0;
      if (!WriteFile(input_, bytes.data(), static_cast<DWORD>(bytes.size()),
                     &written, nullptr))
        return;
    }
  });
}
bool Terminal::Input(std::string bytes) {
  std::lock_guard lock(mutex_);
  if (!process_ || stopping_ || bytes.size() + queued_ > 64 * 1024)
    return false;
  queued_ += bytes.size();
  pending_.push_back(std::move(bytes));
  event_.notify_one();
  return true;
}
void Terminal::Resize(unsigned columns, unsigned rows) {
  COORD size{};
  {
    std::lock_guard lock(mutex_);
    screen_.Resize(columns, rows);
    size = {static_cast<SHORT>(screen_.Columns()),
            static_cast<SHORT>(screen_.Rows())};
  }
  if (console_) ResizePseudoConsole(console_, size);
}
std::wstring Terminal::Output() {
  std::lock_guard lock(mutex_);
  return screen_.Text();
}
bool Terminal::Running() const {
  return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}
void Terminal::End() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    pending_.clear();
    queued_ = 0;
    event_.notify_all();
  }
  // Reader keeps draining while ConPTY emits its final frame during close.
  if (job_) {
    CloseHandle(job_);
    job_ = nullptr;
  }
  if (console_) {
    ClosePseudoConsole(console_);
    console_ = nullptr;
  }
  if (writer_.joinable()) {
    CancelSynchronousIo(writer_.native_handle());
    writer_.join();
  }
  if (reader_.joinable()) {
    CancelSynchronousIo(reader_.native_handle());
    reader_.join();
  }
  for (auto* handle : {&input_, &output_, &process_})
    if (*handle) {
      CloseHandle(*handle);
      *handle = nullptr;
    }
}
}  // namespace rimes::windows::peripherals
