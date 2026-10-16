#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool CheckPackageAccess(HANDLE token, const wchar_t* package, bool expected) {
  // Probe the actual restriction rather than relying on the unsupported
  // TokenIsLessPrivilegedAppContainer query. No real object ACL is changed.
  const auto sddl = std::wstring(L"O:SYG:SYD:P(A;;0x1;;;WD)(A;;0x1;;;") + package + L")";
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
      sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return false;
  GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE,
                          FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
  PRIVILEGE_SET privileges{};
  DWORD bytes = sizeof(privileges), granted = 0;
  BOOL allowed = FALSE;
  const BOOL checked = AccessCheck(descriptor, token, FILE_READ_DATA, &mapping,
                                   &privileges, &bytes, &granted, &allowed);
  LocalFree(descriptor);
  return checked && (allowed != FALSE) == expected &&
      ((granted & FILE_READ_DATA) != 0) == expected;
}

bool VerifySandboxPolicy(HANDLE process, bool lpac) {
  HANDLE primary = nullptr, impersonation = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY | TOKEN_DUPLICATE, &primary)) return false;
  DWORD container = 0, size = 0;
  const bool valid = GetTokenInformation(primary, TokenIsAppContainer,
      &container, sizeof(container), &size) && container &&
      DuplicateToken(primary, SecurityImpersonation, &impersonation);
  CloseHandle(primary);
  if (!valid) return false;
  const bool matched = CheckPackageAccess(impersonation, L"AC", !lpac) &&
                       CheckPackageAccess(impersonation, L"S-1-15-2-2", true);
  CloseHandle(impersonation);
  return matched;
}

bool AddCapability(const wchar_t* name, std::vector<std::vector<std::byte>>& bytes) {
  PSID* groups = nullptr;
  PSID* capabilities = nullptr;
  DWORD group_count = 0, capability_count = 0;
  if (!DeriveCapabilitySidsFromName(name, &groups, &group_count,
                                   &capabilities, &capability_count)) return false;
  bool valid = capability_count != 0;
  for (DWORD i = 0; i < capability_count; ++i) {
    const auto length = GetLengthSid(capabilities[i]);
    bytes.emplace_back(length);
    valid = CopySid(length, bytes.back().data(), capabilities[i]) && valid;
    LocalFree(capabilities[i]);
  }
  for (DWORD i = 0; i < group_count; ++i) LocalFree(groups[i]);
  LocalFree(groups);
  LocalFree(capabilities);
  return valid;
}
}  // namespace

// Creates a disposable AppContainer or LPAC. Only this test's
// staging directory receives its exact package SID; no machine ACL is changed.
int wmain(int argc, wchar_t** argv) {
  if (argc != 2 && argc != 3) return 2;
  const bool lpac = argc == 3 && std::wstring(argv[2]) == L"--lpac";
  if (argc == 3 && !lpac) return 2;
  GUID guid{};
  if (FAILED(CoCreateGuid(&guid))) return 3;
  wchar_t id[40]{};
  StringFromGUID2(guid, id, 40);
  const auto name = L"Rimes.InputTest." + std::wstring(id + 1, 36);
  PSID sid = nullptr;
  const auto created = CreateAppContainerProfile(name.c_str(), name.c_str(),
      L"Isolated RIMES input regression", nullptr, 0, &sid);
  if (FAILED(created)) {
    std::cerr << "CreateAppContainerProfile failed: " << std::hex << created << '\n';
    return 4;
  }
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const auto directory = std::filesystem::path(temp) / name;
  const auto exe = directory / L"RimesSandboxProbe.exe";
  int result = 5;
  HANDLE process = nullptr;
  HANDLE thread = nullptr;
  PPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
  try {
    if (!std::filesystem::create_directory(directory)) throw 5;
    std::filesystem::copy_file(argv[1], exe);
    PACL old_acl = nullptr, acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(directory.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &old_acl, nullptr, &descriptor) != ERROR_SUCCESS) throw 6;
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    auto error = SetEntriesInAclW(1, &access, old_acl, &acl);
    if (error == ERROR_SUCCESS) error = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(directory.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
    if (acl) LocalFree(acl);
    if (descriptor) LocalFree(descriptor);
    if (error != ERROR_SUCCESS) throw 6;
    std::vector<std::vector<std::byte>> capability_bytes;
    std::vector<SID_AND_ATTRIBUTES> capability_entries;
    if (lpac) {
      // The minimal Win32/COM LPAC setup used by Microsoft's sandbox harness.
      // These do not grant our pipe access or modify machine-wide ACLs.
      // https://github.com/microsoft/SandboxSecurityTools/blob/main/LaunchAppContainer/LaunchSandboxMSRC.bat
      if (!AddCapability(L"registryRead", capability_bytes) ||
          !AddCapability(L"lpacCom", capability_bytes)) throw 14;
      for (auto& value : capability_bytes)
        capability_entries.push_back({value.data(), SE_GROUP_ENABLED});
    }
    SIZE_T bytes = 0;
    const DWORD attribute_count = lpac ? 2 : 1;
    InitializeProcThreadAttributeList(nullptr, attribute_count, 0, &bytes);
    std::vector<std::byte> buffer(bytes);
    attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
    if (!InitializeProcThreadAttributeList(attributes, attribute_count, 0, &bytes)) throw 7;
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = sid;
    capabilities.Capabilities = capability_entries.data();
    capabilities.CapabilityCount = static_cast<DWORD>(capability_entries.size());
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
        &capabilities, sizeof(capabilities), nullptr, nullptr)) {
      DeleteProcThreadAttributeList(attributes); attributes = nullptr; throw 7;
    }
    DWORD package_policy = PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT;
    if (lpac && !UpdateProcThreadAttribute(attributes, 0,
        PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY,
        &package_policy, sizeof(package_policy), nullptr, nullptr)) {
      DeleteProcThreadAttributeList(attributes); attributes = nullptr; throw 7;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION child{};
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    const BOOL started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr,
        FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
        nullptr, directory.c_str(), &startup.StartupInfo, &child);
    const auto start_error = GetLastError();
    DeleteProcThreadAttributeList(attributes); attributes = nullptr;
    if (!started) {
      std::cerr << "AppContainer launch failed: " << start_error << '\n'; throw 8;
    }
    process = child.hProcess;
    thread = child.hThread;
    if (!VerifySandboxPolicy(process, lpac)) {
      std::cerr << "Sandbox token did not enforce the expected package access boundary\n";
      throw 12;
    }
    if (ResumeThread(thread) == static_cast<DWORD>(-1)) throw 13;
    CloseHandle(thread); thread = nullptr;
    if (WaitForSingleObject(process, 30000) != WAIT_OBJECT_0) {
      TerminateProcess(process, 9); WaitForSingleObject(process, 5000); throw 9;
    }
    DWORD exit = 10;
    GetExitCodeProcess(process, &exit);
    result = static_cast<int>(exit);
    std::cout << (lpac ? "LPAC" : "AppContainer") << " input probe exit=" << exit << '\n';
  } catch (int code) { result = code; }
    catch (...) { result = 11; }
  if (thread) CloseHandle(thread);
  if (process) {
    if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
      TerminateProcess(process, static_cast<UINT>(result));
      WaitForSingleObject(process, 5000);
    }
    CloseHandle(process);
  }
  // Delete only the exact two paths created above, never a recursive tree.
  DeleteFileW(exe.c_str());
  RemoveDirectoryW(directory.c_str());
  FreeSid(sid);
  DeleteAppContainerProfile(name.c_str());
  return result;
}
