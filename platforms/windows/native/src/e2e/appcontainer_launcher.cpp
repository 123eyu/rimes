#include <Windows.h>
#include <aclapi.h>
#include <userenv.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// Creates one disposable, capability-free AppContainer. Only this test's
// staging directory receives its exact package SID; no machine ACL is changed.
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
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
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<std::byte> buffer(bytes);
    attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) throw 7;
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = sid;
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
        &capabilities, sizeof(capabilities), nullptr, nullptr)) {
      DeleteProcThreadAttributeList(attributes); attributes = nullptr; throw 7;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION child{};
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    const BOOL started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr,
        FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
        nullptr, directory.c_str(), &startup.StartupInfo, &child);
    const auto start_error = GetLastError();
    DeleteProcThreadAttributeList(attributes); attributes = nullptr;
    if (!started) {
      std::cerr << "AppContainer launch failed: " << start_error << '\n'; throw 8;
    }
    process = child.hProcess;
    CloseHandle(child.hThread);
    if (WaitForSingleObject(process, 30000) != WAIT_OBJECT_0) {
      TerminateProcess(process, 9); WaitForSingleObject(process, 5000); throw 9;
    }
    DWORD exit = 10;
    GetExitCodeProcess(process, &exit);
    result = static_cast<int>(exit);
    std::cout << "Capability-free AppContainer input probe exit=" << exit << '\n';
  } catch (int code) { result = code; }
    catch (...) { result = 11; }
  if (process) CloseHandle(process);
  // Delete only the exact two paths created above, never a recursive tree.
  DeleteFileW(exe.c_str());
  RemoveDirectoryW(directory.c_str());
  FreeSid(sid);
  DeleteAppContainerProfile(name.c_str());
  return result;
}
