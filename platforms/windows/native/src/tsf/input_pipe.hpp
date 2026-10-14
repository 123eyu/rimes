#pragma once

#include <Windows.h>
#include <aclapi.h>

namespace rimes::windows::tsf {

inline bool ReadAppContainer(bool* sandboxed) noexcept {
  if (!sandboxed) return false;
  *sandboxed = false;
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  DWORD value = 0, size = 0;
  const bool ok = GetTokenInformation(token, TokenIsAppContainer, &value,
                                     sizeof(value), &size) != FALSE;
  CloseHandle(token);
  if (ok) *sandboxed = value != 0;
  return ok;
}

// The sandbox cannot open the desktop Broker's process token. Authenticate
// the kernel pipe object's explicit owner and server session instead. This is
// used only on the input-only endpoint; desktop clients retain token checks.
inline bool VerifyInputPipe(HANDLE pipe, PSID expected_user, DWORD session,
                            DWORD* process) noexcept {
  if (!expected_user || !IsValidSid(expected_user) || !process) return false;
  *process = 0;
  ULONG peer = 0, peer_session = 0;
  if (!GetNamedPipeServerProcessId(pipe, &peer) || !peer ||
      !GetNamedPipeServerSessionId(pipe, &peer_session) || peer_session != session)
    return false;
  PSID owner = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const auto result = GetSecurityInfo(pipe, SE_KERNEL_OBJECT,
      OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor);
  const bool valid = result == ERROR_SUCCESS && owner && IsValidSid(owner) &&
                     EqualSid(owner, expected_user);
  if (descriptor) LocalFree(descriptor);
  if (valid) *process = peer;
  return valid;
}

}  // namespace rimes::windows::tsf
