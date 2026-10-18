#include "application_update.hpp"
#include "provider.hpp"
#include "rimes_version.hpp"
#include "../../../../../Shared/AppUpdate/manifest.hpp"
#include <bcrypt.h>
#include <winhttp.h>
#include <shellapi.h>
#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace rimes::windows::workbench {
namespace {
using rimes::updates::Asset;
using Json = nlohmann::json;
std::string Hash(const std::string& bytes) {
  std::array<unsigned char, 32> digest{};
  if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                 reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                 static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size())) < 0)
    throw std::runtime_error("Plugin checksum unavailable");
  std::string text; constexpr char hex[] = "0123456789abcdef";
  for (const auto byte : digest) { text += hex[byte >> 4]; text += hex[byte & 15]; }
  return text;
}
struct Close { void operator()(void* handle) const { if (handle) WinHttpCloseHandle(handle); } };
using Internet = std::unique_ptr<void, Close>;
std::string Download(const std::string& source, std::size_t limit, const std::function<void(unsigned)>& progress, const std::atomic<bool>& cancelled) {
  auto url = Wide(source);
  Internet session(WinHttpOpen(L"RIMES Update Installer/1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) throw std::runtime_error("Update download unavailable");
  WinHttpSetTimeouts(session.get(), 10000, 10000, 20000, 20000);
  for (int attempt = 0; attempt < 4; ++attempt) {
    URL_COMPONENTS parts{sizeof(parts)}; parts.dwHostNameLength = parts.dwUrlPathLength =
        parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts))
      throw std::runtime_error("Invalid plugin URL");
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    if (parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT || parts.dwUserNameLength || parts.dwPasswordLength ||
        (host != L"github.com" && host != L"objects.githubusercontent.com" && host != L"release-assets.githubusercontent.com"))
      throw std::runtime_error("Invalid plugin download recipient");
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
    if (!connection) throw std::runtime_error("Update connection failed");
    Internet request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) throw std::runtime_error("Update connection failed");
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr))
      throw std::runtime_error("Update download failed; please retry");
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr))
      throw std::runtime_error("Update download status unavailable");
    if (status >= 300 && status < 400) {
      std::array<wchar_t,8192> location{}; size = static_cast<DWORD>(location.size() * sizeof(wchar_t));
      if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_LOCATION, nullptr, location.data(), &size, nullptr))
        throw std::runtime_error("Update redirect unavailable");
      url = location.data(); continue;
    }
    if (status != 200) throw std::runtime_error("Update package is not available for download");
    std::string bytes; std::array<char,8192> buffer{}; DWORD count = 0;
    const auto started = GetTickCount64();
    for (;;) {
      if (cancelled) throw std::runtime_error("Cancelled");
      if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count))
        throw std::runtime_error("Update download interrupted");
      if (!count) return bytes;
      if (bytes.size() + count > limit || GetTickCount64() - started > 600000)
        throw std::runtime_error("Update download exceeds size or time limit");
      bytes.append(buffer.data(), count);
      if (progress) progress(static_cast<unsigned>(100ull * bytes.size() / limit));
    }
  }
  throw std::runtime_error("Too many plugin redirects");
}
std::filesystem::path Root() {
  wchar_t local[32768]{};
  const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
  if (!count || count >= 32768) throw std::runtime_error("Local application data unavailable");
  const auto root = std::filesystem::path(local) / L"RIMES" / L"application-updates";
  for (auto p = root; !p.empty(); p = p == p.root_path() ? std::filesystem::path{} : p.parent_path()) {
    const auto attributes = GetFileAttributesW(p.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Update storage cannot use reparse points");
  }
  std::filesystem::create_directories(root);
  return root;
}
std::string Read(const std::filesystem::path& path, std::size_t limit) {
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > limit)
    throw std::runtime_error("Update file unavailable");
  std::ifstream stream(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(stream)), {});
  if (stream.bad() || bytes.size() > limit) throw std::runtime_error("Update file read failed");
  return bytes;
}
void Write(const std::filesystem::path& path, const std::string& bytes) {
  auto temporary = path; temporary += L".partial";
  const auto attributes = GetFileAttributesW(temporary.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Unsafe update file");
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); stream.close();
  if (!stream || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot save update");
}
struct State {
  std::mutex mutex;
  std::atomic<bool> cancelled{false};
  bool busy = false, available = false, downloaded = false, launch = false;
  unsigned progress = 0;
  Asset asset;
  std::wstring message = std::wstring(L"当前版本 / Current: ") + kProductVersionWide;
};
struct View { HWND window = nullptr, message = nullptr, check = nullptr, download = nullptr, install = nullptr; std::shared_ptr<State> state = std::make_shared<State>(); };
HWND currentWindow = nullptr;
void Start(const std::shared_ptr<State>& state, unsigned action) {
  {
    std::lock_guard lock(state->mutex);
    if (state->busy || (action != 1 && !state->available) || (action == 3 && !state->downloaded)) return;
    state->busy = true; state->progress = 0;
    state->message = action == 1 ? L"正在检查更新 / Checking…" : action == 2 ? L"正在下载 / Downloading…" : L"正在验证安装包 / Verifying…";
  }
  std::thread([state, action] {
    try {
      const auto root = Root(), package = root / L"XSIME-update.exe", metadata = root / L"candidate.json";
      if (action == 1) {
        const auto bytes = Download(updates::kFeed, 1024 * 1024, {}, state->cancelled);
        const auto asset = updates::Parse(bytes, "windows");
        const auto newer = updates::Version(asset.version) > updates::Version(kProductVersion);
        Write(metadata, bytes);
        std::lock_guard lock(state->mutex);
        state->asset = asset; state->available = newer; state->downloaded = false;
        state->message = newer ? L"发现新版本 / Available: " + Wide(asset.version) : L"已是最新版本 / Up to date";
      } else if (action == 2) {
        Asset asset;
        { std::lock_guard lock(state->mutex); asset = state->asset; }
        const auto bytes = Download(asset.url, static_cast<std::size_t>(asset.size), [state](unsigned progress) {
          std::lock_guard lock(state->mutex); state->progress = progress;
          state->message = L"正在下载 / Downloading… " + std::to_wstring(progress) + L"%";
        }, state->cancelled);
        updates::Verify(asset, bytes.size(), Hash(bytes));
        Write(package, bytes);
        std::lock_guard lock(state->mutex);
        state->downloaded = true; state->message = L"下载及校验完成。点击安装 / Download verified. Ready to install.";
      } else {
        Asset asset;
        { std::lock_guard lock(state->mutex); asset = state->asset; }
        const auto bytes = Read(package, updates::kMaximumPackage);
        updates::Verify(asset, bytes.size(), Hash(bytes));
        Write(root / L"pending.json", Json{{"version", asset.version}}.dump());
        std::lock_guard lock(state->mutex);
        state->launch = true; state->message = L"等待系统安装完成。重新打开设置确认；如提示注销，请先保存工作。";
      }
      std::lock_guard lock(state->mutex); state->busy = false;
    } catch (const std::exception& error) {
      std::lock_guard lock(state->mutex); state->busy = false; state->message = L"更新失败，可重试 / Update failed: " + Wide(error.what());
    }
  }).detach();
}
void Refresh(View* view) {
  bool launch = false;
  {
    std::lock_guard lock(view->state->mutex);
    SetWindowTextW(view->message, view->state->message.c_str());
    EnableWindow(view->check, !view->state->busy);
    EnableWindow(view->download, !view->state->busy && view->state->available);
    EnableWindow(view->install, !view->state->busy && view->state->downloaded);
    launch = view->state->launch; view->state->launch = false;
  }
  if (launch) {
    try {
      const auto package = Root() / L"XSIME-update.exe";
      if (reinterpret_cast<INT_PTR>(ShellExecuteW(view->window, L"open", package.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        throw std::runtime_error("Could not open installer");
    } catch (const std::exception& error) { std::lock_guard lock(view->state->mutex); view->state->message = Wide(error.what()); }
  }
}
LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* view = reinterpret_cast<View*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    view = static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    view->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
  }
  if (!view) return DefWindowProcW(hwnd, message, wparam, lparam);
  if (message == WM_CREATE) {
    const auto dpi = GetDpiForWindow(hwnd);
    auto child = [&](const wchar_t* type, const wchar_t* title, int x, int y, int width, int height, unsigned id) {
      auto h = CreateWindowExW(0, type, title, WS_CHILD | WS_VISIBLE | (id ? WS_TABSTOP | BS_PUSHBUTTON : 0),
          MulDiv(x, static_cast<int>(dpi), 96), MulDiv(y, static_cast<int>(dpi), 96), MulDiv(width, static_cast<int>(dpi), 96), MulDiv(height, static_cast<int>(dpi), 96),
          hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
      SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return h;
    };
    view->message = child(L"STATIC", L"", 20, 20, 550, 120, 0);
    view->check = child(L"BUTTON", L"检查更新 / Check", 20, 160, 170, 36, 1);
    view->download = child(L"BUTTON", L"下载 / Download", 205, 160, 170, 36, 2);
    view->install = child(L"BUTTON", L"安装 / Install", 390, 160, 170, 36, 3);
    try {
      const auto root = Root();
      if (std::filesystem::exists(root / L"pending.json")) {
        const auto target = Json::parse(Read(root / L"pending.json", 4096)).at("version").get<std::string>();
        if (updates::Version(kProductVersion) >= updates::Version(target)) {
          view->state->message = std::wstring(L"更新完成 / Updated to ") + kProductVersionWide + L"\n如安装器要求注销或重启，请完成该步骤以更新应用中已加载的输入法。";
          std::filesystem::remove(root / L"pending.json");
        } else view->state->message += L"\n安装尚未完成或需注销后生效，可重试。";
      }
      if (std::filesystem::exists(root / L"candidate.json")) {
        const auto asset = updates::Parse(Read(root / L"candidate.json", 1024 * 1024), "windows");
        if (updates::Version(asset.version) > updates::Version(kProductVersion)) {
          view->state->asset = asset; view->state->available = true;
          view->state->downloaded = std::filesystem::is_regular_file(root / L"XSIME-update.exe");
        }
      }
    } catch (...) { view->state->message += L"\n请重新检查更新。"; }
    SetTimer(hwnd, 1, 200, nullptr); Refresh(view); return 0;
  }
  if (message == WM_TIMER) { Refresh(view); return 0; }
  if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
    const auto id = LOWORD(wparam);
    if (id == 3 && MessageBoxW(hwnd, L"安装器会关闭旧版输入法。请保存尚未发送的 Buffer 内容；若系统提示注销，保存工作后再注销。\n\n继续安装？", L"安装更新", MB_YESNO | MB_ICONINFORMATION) != IDYES) return 0;
    if (id >= 1 && id <= 3) Start(view->state, id);
    Refresh(view); return 0;
  }
  if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
  if (message == WM_NCDESTROY) {
    view->state->cancelled = true; KillTimer(hwnd, 1); currentWindow = nullptr;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); delete view;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
} // namespace
void ShowApplicationUpdate(HWND owner) {
  if (currentWindow) { ShowWindow(currentWindow, SW_SHOWNORMAL); SetForegroundWindow(currentWindow); return; }
  WNDCLASSW klass{}; klass.lpfnWndProc = Procedure; klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpszClassName = L"XSIMEApplicationUpdate"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&klass);
  auto* view = new View;
  const auto dpi = static_cast<int>(owner ? GetDpiForWindow(owner) : 96);
  currentWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, klass.lpszClassName, L"XSIME 应用更新 / Software update",
      WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(600, dpi, 96), MulDiv(260, dpi, 96),
      owner, nullptr, klass.hInstance, view);
  if (currentWindow) ShowWindow(currentWindow, SW_SHOW);
}
} // namespace rimes::windows::workbench
