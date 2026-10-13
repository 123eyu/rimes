#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace rimes::windows::broker {

std::wstring FormatWindowsError(DWORD error_code);

// Maintenance objects and the desktop pipe remain private to the user. The
// separate input-only endpoint also admits AppContainers, authenticated by
// user SID and session before any frame is handled.
class UserSecurityContext {
 public:
  UserSecurityContext() = default;
  ~UserSecurityContext();

  UserSecurityContext(const UserSecurityContext&) = delete;
  UserSecurityContext& operator=(const UserSecurityContext&) = delete;

  bool Initialize(std::wstring* error);

  [[nodiscard]] SECURITY_ATTRIBUTES* attributes() { return &attributes_; }
  [[nodiscard]] SECURITY_ATTRIBUTES* input_attributes() { return &input_attributes_; }
  [[nodiscard]] const std::wstring& sid_string() const { return sid_string_; }
  [[nodiscard]] PSID sid() const {
    return sid_bytes_.empty()
               ? nullptr
               : const_cast<std::byte*>(sid_bytes_.data());
  }
  [[nodiscard]] DWORD session_id() const { return session_id_; }
  [[nodiscard]] const std::wstring& pipe_name() const { return pipe_name_; }
  [[nodiscard]] const std::wstring& mutex_name() const { return mutex_name_; }

  // Defense in depth after the pipe DACL and PIPE_REJECT_REMOTE_CLIENTS:
  // verify the connected client process belongs to this SID and session.
  bool VerifyConnectedPipeClient(HANDLE pipe, std::wstring* error) const;

 private:
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
  PSECURITY_DESCRIPTOR input_descriptor_ = nullptr;
  SECURITY_ATTRIBUTES attributes_{};
  SECURITY_ATTRIBUTES input_attributes_{};
  std::vector<std::byte> sid_bytes_;
  std::wstring sid_string_;
  DWORD session_id_ = 0;
  std::wstring pipe_name_;
  std::wstring mutex_name_;
};

}  // namespace rimes::windows::broker
