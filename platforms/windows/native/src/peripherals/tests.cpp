#include <richedit.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <iostream>

#include "capture.hpp"
#include "messages.hpp"
#include "store.hpp"
#include "terminal.hpp"
#include "window.hpp"
#include "interaction.hpp"

using namespace rimes::windows;
using namespace rimes::windows::peripherals;
namespace {
struct Canvas {
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap = nullptr;
  HGDIOBJ old = nullptr;
  void* pixels = nullptr;
  int width, height;
  Canvas(int w, int h) : width(w), height(h) {
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    old = SelectObject(dc, bitmap);
  }
  ~Canvas() {
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
  }
  std::vector<std::uint32_t> Pixels() const {
    GdiFlush();
    const auto* first = static_cast<const std::uint32_t*>(pixels);
    return {first, first + width * height};
  }
  void Save(const std::filesystem::path& path, IWICImagingFactory* factory) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICBitmap> source;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    auto require = [](HRESULT h) {
      if (FAILED(h)) throw std::runtime_error("snapshot export failed");
    };
    require(factory->CreateBitmapFromHBITMAP(bitmap, nullptr,
                                             WICBitmapIgnoreAlpha, &source));
    require(factory->CreateStream(&stream));
    require(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    require(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    require(encoder->CreateNewFrame(&frame, nullptr));
    require(frame->Initialize(nullptr));
    require(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat24bppBGR;
    require(frame->SetPixelFormat(&format));
    require(frame->WriteSource(source.Get(), nullptr));
    require(frame->Commit());
    require(encoder->Commit());
  }
};
void Check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template <class F>
void Reject(F action, const char* message) {
  bool failed = false;
  try {
    action();
  } catch (...) {
    failed = true;
  }
  Check(failed, message);
}
void Chords(RevealGate& gate, const std::vector<std::string>& groups) {
  for (const auto& s : groups) {
    for (auto c : s) gate.Key(c, true);
    for (auto c : s) gate.Key(c, false);
  }
}
void FixtureImage(Store& store) {
  constexpr int width = 240, height = 150, bytes = width * height * 4;
  BITMAPFILEHEADER file{0x4d42, 54 + bytes, 0, 0, 54};
  BITMAPINFOHEADER bitmap{
      sizeof(BITMAPINFOHEADER), width, height, 1, 32, BI_RGB, bytes};
  std::string picture(reinterpret_cast<const char*>(&file), sizeof(file));
  picture.append(reinterpret_cast<const char*>(&bitmap), sizeof(bitmap));
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      picture += static_cast<char>(90 + x / 3);
      picture += static_cast<char>(110 + y / 2);
      picture += static_cast<char>(55 + x / 5);
      picture += static_cast<char>(255);
    }
  store.ImportData(Module::Temporary, "本地图片示例", ".bmp", picture);
}
void Storage(const std::filesystem::path& root) {
  Store store(root);
  FixtureImage(store);
  auto note = store.Save({{"module", "notes"},
                          {"kind", "bullet"},
                          {"title", "测试笔记"},
                          {"body", "- 用户名：isaac\n- [ ] 待办"}});
  Check(store.List(Module::Notes, "isaac").size() == 1, "search body");
  auto next = note;
  next["body"] = "更新";
  next = store.Save(next, 1);
  Reject([&] { store.Save(note, 1); }, "stale edit must not overwrite");
  Reject([&] { store.Erase(note.at("id"), 1); }, "stale deletion must fail");
  Check(store.Get(note.at("id")).at("body") == "更新",
        "original preserved on stale save");
  store.Enable(Module::Notes, false);
  Check(store.List(Module::Notes).empty(), "disabled modules hidden");
  store.Enable(Module::Notes, true);
  Check(store.List(Module::Notes).size() == 1, "reenable keeps data");
  AtomicWrite(root / L"records" / L"damaged.json", "invalid");
  Check(store.List(Module::Notes).size() == 1,
        "damaged record does not hide valid notes");
  Reject([&] { store.Get("../../settings"); }, "path traversal rejected");
  auto secret = store.Save({{"module", "passwords"},
                            {"title", "密码测试"},
                            {"body", "SENSITIVE-FIXTURE"},
                            {"summary", "SENSITIVE-FIXTURE"}});
  Check(!secret.contains("body") && !secret.contains("summary"),
        "no secret plaintext metadata");
  Check(ReadFile(root / L"records" /
                 (workbench::Wide(secret.at("id")) + L".json"))
                .find("SENSITIVE-FIXTURE") == std::string::npos,
        "password encrypted on disk");
  Check(Unseal(secret.at("sealed")) == "SENSITIVE-FIXTURE", "DPAPI round trip");
  Reject(
      [&] {
        store.ImportData(Module::Passwords, "secret", ".txt",
                         "SENSITIVE-FIXTURE");
      },
      "password import must not create a plaintext asset");
  Check(store.List(Module::Passwords, "SENSITIVE").empty(),
        "password not searchable by body");
  RevealGate gate(root);
  Chords(gate, {"A", "B", "C", "D"});
  Check(!gate.Authorized(), "wrong chord never unlocks");
  Chords(gate, {"RH", "WO", "CVN", "QU"});
  Check(gate.Authorized(), "default physical chord validates");
  gate.Change({"AB", "CD", "EF", "GH"}, {"BA", "DC", "FE", "HG"});
  Check(!gate.Authorized(), "changing credential revokes reveal");
  Chords(gate, {"RH", "WO", "CVN", "QU"});
  Check(!gate.Authorized(), "custom credential rejects public default");
  Chords(gate, {"AB", "CD", "EF", "GH"});
  Check(gate.Authorized(), "custom salted credential works");
  gate.Reset();
  Check(!gate.Authorized(), "focus reset revokes gate");
  AtomicWrite(root / L"reveal-credential.json", "broken");
  Chords(gate, {"RH", "WO", "CVN", "QU"});
  Check(!gate.Authorized(), "damaged custom credential fails closed");
  auto attachment = root / L"fixture.txt";
  AtomicWrite(attachment, "manual terminal result");
  auto inbox = store.Import(Module::Inbox, attachment, "session-identity");
  std::filesystem::remove(attachment);
  Check(ReadFile(store.Asset(inbox)) == "manual terminal result",
        "inbox assets survive original deletion");
  Check(inbox.at("terminalSessionID") == "session-identity",
        "explicit terminal identity retained");
  inbox["archived"] = true;
  inbox = store.Save(inbox, 1);
  Check(store.List(Module::Inbox).empty() &&
            store.List(Module::Inbox, "", "archived").size() == 1,
        "archive filter persists");
  Reject([&] { store.Asset(Json{{"asset", "../settings.json"}}); },
         "asset traversal rejected");
  Reject(
      [&] {
        store.ImportData(Module::Inbox, "bad", ".txt",
                         std::string(300 * 1024, 'x'));
      },
      "oversized text import rejected");
  Check(store.CollectClipboard(), "clipboard history automatically records by default like macOS");
  store.SetCollectClipboard(false);
  Check(!Store(root).CollectClipboard(), "explicit pause survives restart");
  store.SetCollectClipboard(true);
  const std::string binary("\0binary\xff", 8);
  auto file =
      store.ImportData(Module::Temporary, "clipboard file", ".bin", binary);
  Check(file.at("kind") == "document" && ReadFile(store.Asset(file)) == binary,
        "file clipboard history preserves bytes instead of decoding as text");
}
void CapsulePreferences(const std::filesystem::path& root) {
  Store store(root);
  Check(store.CapsuleSelection() == Module::Temporary,
        "first Capsule invocation defaults to Recent clipboard history");
  store.Preferences({{"clipboard", false},
                     {"capsule_manager_client_size", Json::array({900, 600})}});
  store.RememberCapsuleSelection(Module::Notes);
  Check(Store(root).CapsuleSelection() == Module::Notes &&
            !store.CollectClipboard() &&
            store.Preferences().at("capsule_manager_client_size") ==
                Json::array({900, 600}),
        "Capsule selection survives restart without replacing other preferences");
  const auto before = ReadFile(root / L"preferences.json");
  store.RememberCapsuleSelection(Module::Chat);
  Check(ReadFile(root / L"preferences.json") == before,
        "Mailbox cannot replace the remembered Capsule module");
  for (const auto& invalid : {Json("missing"), Json("chat"), Json(4), Json(nullptr)}) {
    auto prefs = store.Preferences();
    prefs["capsule_last_module"] = invalid;
    store.Preferences(prefs);
    Check(store.CapsuleSelection() == Module::Temporary,
          "invalid or non-Capsule saved selections fall back to Recent");
  }
  store.RememberCapsuleSelection(Module::Notes);
  store.Enable(Module::Notes, false);
  Check(store.CapsuleSelection() == Module::Temporary,
        "a disabled remembered module falls back to Recent");
  store.Enable(Module::Temporary, false);
  Check(store.CapsuleSelection() == Module::Capture,
        "a disabled Recent module falls back to the first enabled module");
  for (unsigned i = 0; i < 5; ++i) store.Enable(static_cast<Module>(i), false);
  Check(!store.CapsuleSelection(), "all disabled Capsule modules stay disabled");
}
void HistoryOrdering(const std::filesystem::path& root) {
  Store store(root);
  const auto first = store.RecordClipboardText("First copied text");
  const auto second = store.RecordClipboardText("Second copied text");
  Check(store.List(Module::Temporary)[0]["id"] == second["id"], "new copies appear first");
  const auto repeated = Store(root).RecordClipboardText("First copied text");
  Check(repeated["id"] == first["id"] && repeated["updated"] == first["updated"] &&
      store.List(Module::Temporary).size() == 2 && store.List(Module::Temporary)[0]["id"] == first["id"],
      "A/B/A across restart promotes the same identity without changing capture time");
  Reject([&] { store.PromoteTemporary(first); }, "stale promotion cannot overwrite a changed card");
  FixtureImage(store);
  const auto image = store.List(Module::Temporary)[0];
  const auto image_count = std::distance(std::filesystem::directory_iterator(root / L"assets"),
                                        std::filesystem::directory_iterator{});
  store.RecordClipboardText("Between images");
  FixtureImage(store);
  Check(store.List(Module::Temporary)[0]["id"] == image["id"] &&
      std::distance(std::filesystem::directory_iterator(root / L"assets"),
                    std::filesystem::directory_iterator{}) == image_count,
      "repeated image promotes the original without creating another attachment");
  auto legacy = store.Get(image.at("id")); legacy.erase("fingerprint");
  AtomicWrite(root / L"records" / (workbench::Wide(legacy.at("id")) + L".json"), legacy.dump());
  FixtureImage(store);
  Check(Store(root).List(Module::Temporary)[0]["id"] == image["id"],
      "legacy history without digests deduplicates and persists ordering");
  const auto note_a = store.Save({{"module", "notes"}, {"title", "A"}, {"body", "one"}});
  auto note_b = store.Save({{"module", "notes"}, {"title", "B"}, {"body", "two"}});
  auto edited = note_a; edited["body"] = "edited";
  edited = store.Save(edited, edited.at("revision"));
  Check(store.List(Module::Notes)[0]["id"] == note_a["id"], "saved library uses edit time");
  note_b["updated"] = edited["updated"];
  AtomicWrite(root / L"records" / (workbench::Wide(note_b.at("id")) + L".json"), note_b.dump());
  const auto tied = Store(root).List(Module::Notes);
  Check(tied[0]["id"].get<std::string>() < tied[1]["id"].get<std::string>(),
      "equal timestamps have stable identifier ordering");
}
void ImagePasteGuards() {
  PasteTarget target{reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(2), 123, 456};
  auto live = target;
  DWORD sequence = 10;
  bool interactive = true, released = true;
  unsigned sends = 0;
  UINT accepted = 4;
  std::vector<std::vector<INPUT>> batches;
  PastePlatform api{[&] { return live; }, [&] { return sequence; }, [&] { return interactive; },
      [&] { return released; }, [&](UINT count, INPUT* input, int size) {
        Check(size == sizeof(INPUT), "native input structure size");
        ++sends; batches.emplace_back(input, input + count); return accepted;
      }};
  Check(PasteClipboardImage(target, 10, api) == PasteResult::Sent && sends == 1 &&
      batches[0].size() == 4 && batches[0][0].ki.wVk == VK_CONTROL &&
      batches[0][1].ki.wVk == 'V' && batches[0][2].ki.dwFlags == KEYEVENTF_KEYUP &&
      batches[0][3].ki.dwFlags == KEYEVENTF_KEYUP,
      "rich image activation dispatches one balanced native paste batch");
  released = false;
  Check(PasteClipboardImage(target, 10, api) == PasteResult::Waiting && sends == 1,
      "held shortcut or mouse buttons delay paste without altering real key state");
  released = true; ++sequence;
  Check(PasteClipboardImage(target, 10, api) == PasteResult::ClipboardChanged && sends == 1,
      "new clipboard data never gets pasted by an old activation");
  sequence = 10; live.focus = reinterpret_cast<HWND>(3);
  Check(PasteClipboardImage(target, 10, api) == PasteResult::TargetChanged && sends == 1,
      "same-process native focus changes cancel paste");
  live = target; interactive = false;
  Check(PasteClipboardImage(target, 10, api) == PasteResult::Blocked && sends == 1,
      "locked desktop never receives input");
  interactive = true;
  api.keys_released = [&] { live.focus = reinterpret_cast<HWND>(3); return true; };
  Check(PasteClipboardImage(target, 10, api) == PasteResult::TargetChanged && sends == 1,
      "focus is rechecked immediately before sending");
  api.keys_released = [] { return true; }; live = target; accepted = 2;
  Check(PasteClipboardImage(target, 10, api) == PasteResult::Blocked && sends == 3 &&
      batches.back().size() == 2 && batches.back()[0].ki.dwFlags == KEYEVENTF_KEYUP &&
      batches.back()[1].ki.dwFlags == KEYEVENTF_KEYUP,
      "partial input dispatch releases synthetic keys and never retries the paste");
  accepted = 0;
  Check(PasteClipboardImage(target, 10, api) == PasteResult::Blocked && sends == 4,
      "integrity-blocked dispatch does not pretend insertion succeeded");
}
void HistoryCleanup(const std::filesystem::path& root) {
  Store store(root);
  const auto bytes = std::string("\0", 1) + std::string(64, 'x');
  auto a = store.ImportData(Module::Temporary, "first", ".bin", bytes);
  auto b = store.ImportData(Module::Temporary, "second", ".bin", bytes + "b");
  auto c = store.ImportData(Module::Temporary, "third", ".bin", bytes + "c");
  CleanupPolicy policy; policy.recent_items = 2;
  const auto first_cleanup = store.Cleanup({}, policy);
  Check(first_cleanup.records == 1 && first_cleanup.assets == 1 &&
      !std::filesystem::exists(store.Asset(a)) && std::filesystem::exists(store.Asset(b)),
      "history count trimming reclaims actual oldest attachment bytes");
  auto note = store.Save({{"module", "notes"}, {"title", "shared"},
                         {"captureDocument", {{"source", b.at("asset")}}}});
  store.Enable(Module::Notes, false);
  policy.recent_items = 0;
  store.Cleanup({c.at("id")}, policy);
  Check(store.List(Module::Temporary).size() == 1 && std::filesystem::exists(store.Asset(b)) &&
      std::filesystem::exists(store.Asset(c)), "active cards and disabled-module nested references survive cleanup");
  store.Cleanup({}, policy);
  Check(!std::filesystem::exists(store.Asset(c)) && std::filesystem::exists(store.Asset(b)),
      "releasing a card allows bounded cleanup without deleting a shared original");
  const auto orphan = root / L"assets" / (workbench::Wide(NewId()) + L".bin");
  AtomicWrite(orphan, bytes);
  store.Cleanup({}, policy);
  Check(std::filesystem::exists(orphan), "new uncommitted assets have a grace period");
  policy.orphan_grace = 0;
  store.Cleanup({}, policy, note["updated"].get<std::uint64_t>() + 1000000000ULL);
  Check(!std::filesystem::exists(orphan) && std::filesystem::exists(store.Asset(b)),
      "orphan collection frees stale files only after checking every record");
  Store capacity(root / L"capacity");
  auto large = capacity.ImportData(Module::Temporary, "large", ".bin", bytes);
  auto newest = capacity.RecordClipboardText("newest");
  policy.recent_items = 200; policy.recent_bytes = newest.dump(2).size() + 8;
  capacity.Cleanup({}, policy);
  Check(capacity.List(Module::Temporary).size() == 1 &&
      capacity.List(Module::Temporary)[0]["id"] == newest["id"] &&
      !std::filesystem::exists(capacity.Asset(large)), "byte budget applies independently of item count");
  Store captures(root / L"captures");
  const auto temporary = captures.ImportData(Module::Capture, "automatic", ".png", bytes, "", true);
  const auto saved = captures.ImportData(Module::Capture, "saved import", ".png", bytes);
  policy = {}; policy.capture_age = 100;
  const auto later = temporary["captured"].get<std::uint64_t>() + 1000;
  captures.Cleanup({temporary.at("id")}, policy, later);
  Check(captures.List(Module::Capture).size() == 2, "leased screenshot is retained past its expiry");
  captures.Cleanup({}, policy, later);
  Check(captures.List(Module::Capture).size() == 1 && captures.Get(saved.at("id")) == saved &&
      !std::filesystem::exists(captures.Asset(temporary)), "expired automatic capture is reclaimed; saved or legacy captures remain");
  const auto over_budget = captures.ImportData(Module::Capture, "automatic two", ".png", bytes, "", true);
  policy.capture_bytes = 0;
  captures.Cleanup({}, policy);
  Check(!std::filesystem::exists(captures.Asset(over_budget)) && std::filesystem::exists(captures.Asset(saved)),
      "temporary screenshot byte cap never consumes permanent imports");
  Store damaged(root / L"damaged");
  const auto keep = damaged.ImportData(Module::Temporary, "keep", ".bin", bytes);
  AtomicWrite(damaged.Root() / L"records" / (workbench::Wide(NewId()) + L".json"), "broken");
  policy.recent_items = 0;
  Reject([&] { damaged.Cleanup({}, policy); }, "unreadable references stop cleanup before deletion");
  Check(damaged.Get(keep.at("id")) == keep && std::filesystem::exists(damaged.Asset(keep)),
      "damaged metadata cannot orphan or erase valid records");
}
void MailboxPreferences(const std::filesystem::path& root) {
  Store store(root);
  Check(store.MailboxSelection() == Module::Chat,
        "first Mailbox invocation defaults to Chat");
  store.RememberCapsuleSelection(Module::Notes);
  store.SetCollectClipboard(false);
  for (const auto module : {Module::Terminal, Module::Inbox, Module::Chat}) {
    store.RememberMailboxSelection(module);
    Check(Store(root).MailboxSelection() == module &&
              store.CapsuleSelection() == Module::Notes && !store.CollectClipboard(),
          "each Mailbox selection survives restart independently of Capsule");
  }
  const auto before = ReadFile(root / L"preferences.json");
  store.RememberMailboxSelection(Module::Temporary);
  Check(ReadFile(root / L"preferences.json") == before,
        "Capsule cannot overwrite the remembered Mailbox module");
  for (const auto& invalid : {Json("missing"), Json("notes"), Json(6), Json(nullptr)}) {
    auto prefs = store.Preferences();
    prefs["mailbox_last_module"] = invalid;
    store.Preferences(prefs);
    Check(store.MailboxSelection() == Module::Chat,
          "invalid saved Mailbox selection falls back to Chat");
  }
  store.RememberMailboxSelection(Module::Inbox);
  store.Enable(Module::Inbox, false);
  Check(store.MailboxSelection() == Module::Chat,
        "disabled remembered workspace falls back to Chat");
  store.Enable(Module::Chat, false);
  Check(store.MailboxSelection() == Module::Terminal,
        "disabled Chat falls back to the first enabled Mailbox module");
  store.Enable(Module::Terminal, false);
  Check(!store.MailboxSelection(), "all disabled Mailbox modules stay disabled");
}
void AutomaticClipboard(const std::filesystem::path& root) {
  Store store(root);
  workbench::Runtime runtime;
  DWORD sequence = 1;
  unsigned reads = 0;
  bool interactive = true;
  ClipboardContent content;
  content.opened = true; content.text = "Copied while Capsule is closed.";
  ClipboardSource source{[&] { return sequence; }, [&] { return interactive; },
      [&](HWND owner) {
        Check(owner == nullptr, "background history does not require a Capsule window");
        ++reads; return content;
      }};
  peripherals::Windows windows(runtime, root, {}, std::move(source));
  windows.ClipboardChanged();
  Check(!windows.Handle(false) && store.List(Module::Temporary).size() == 1 &&
        store.List(Module::Temporary)[0]["body"] == content.text,
        "a clipboard change is saved without opening Capsule or pressing collect");
  windows.ClipboardChanged();
  Check(reads == 1, "duplicate clipboard notification does not reread the same sequence");
  ++sequence; windows.ClipboardChanged();
  Check(store.List(Module::Temporary).size() == 1, "repeated identical copies do not duplicate history");
  content.text = "Next copy."; ++sequence; windows.ClipboardChanged();
  Check(store.List(Module::Temporary).size() == 2, "successive copies automatically append history");
  store.SetCollectClipboard(false);
  const auto before_pause = reads;
  content.text = "Paused copy."; ++sequence; windows.ClipboardChanged();
  Check(reads == before_pause && store.List(Module::Temporary).size() == 2,
        "explicit pause prevents clipboard reads and retains existing history");
  store.SetCollectClipboard(true);
  content.text = "Resumed copy."; ++sequence; windows.ClipboardChanged();
  Check(store.List(Module::Temporary).size() == 3, "new copies are collected after resuming");
  const auto before_protection = reads;
  windows.Protect(); ++sequence; content.text = "Locked copy."; windows.ClipboardChanged();
  Check(reads == before_protection, "locked session never reads clipboard contents");
  windows.Resume(); windows.ClipboardChanged();
  Check(reads == before_protection, "unlock skips clipboard data copied while protected");
  interactive = false; ++sequence; windows.ClipboardChanged();
  Check(reads == before_protection, "noninteractive desktop cannot observe clipboard contents");
  interactive = true; windows.Resume();
  store.Enable(Module::Temporary, false); ++sequence; windows.ClipboardChanged();
  Check(reads == before_protection, "disabled history module never reads clipboard contents");
  store.Enable(Module::Temporary, true);
  content = {}; content.opened = true; ++sequence; windows.ClipboardChanged();
  Check(store.List(Module::Temporary).size() == 3,
        "excluded or unsupported clipboard data returned empty by the reader is not recorded");
  content.text = "Copy after contention."; content.opened = false;
  ++sequence; windows.ClipboardChanged();
  Check(store.List(Module::Temporary).size() == 3, "busy clipboard schedules retry without losing history");
  content.opened = true; Sleep(120); windows.Tick();
  Check(store.List(Module::Temporary).size() == 4, "clipboard contention is retried automatically without collect action");
  const auto file = root / L"copied-file.bin";
  const std::string bytes("\0synthetic-file", 15);
  AtomicWrite(file, bytes);
  content = {}; content.opened = true; content.files = {file};
  ++sequence; windows.ClipboardChanged();
  const auto records = store.List(Module::Temporary);
  Check(records.size() == 5 && std::any_of(records.begin(), records.end(), [&](const auto& item) {
      return item.contains("asset") && ReadFile(store.Asset(item)) == bytes;
    }), "file copies are captured in the background with their original bytes");
}
void Routes() {
  workbench::Settings settings;
  settings.connections = {{"first", "First", "openai-compatible",
                           "https://example.invalid/v1", "model-a", ""},
                          {"second", "Second", "openai-compatible",
                           "https://example.invalid/v2", "model-b", ""}};
  workbench::SelectConnection(&settings, "first");
  auto route = FreezeRoute(settings);
  workbench::SelectConnection(&settings, "second");
  Check(ResolveRoute(route, settings).active_connection == "first",
        "conversation does not follow global provider selection");
  settings.connections[0].model = "changed";
  Reject([&] { ResolveRoute(route, settings); },
         "changed model cannot silently reroute");
}
void TerminalTest() {
  TerminalScreen screen(40, 8);
  screen.Feed("hello\r\nworld\x1b[1;1HHi");
  Check(screen.Text().starts_with(L"Hillo"), "VT cursor addressing");
  screen.Feed("\x1b[2J\x1b[H\xe4\xbd");
  screen.Feed("\xa0");
  Check(screen.Text().starts_with(L"你"), "fragmented UTF8 decoded");
  screen.Feed("好\xf0\x9f\x98\x80");
  Check(screen.Text().starts_with(L"你好\xd83d\xde00"),
        "CJK cells do not inject spaces or split emoji surrogate pairs");
  screen.Feed("\x1b]52;c;not-a-clipboard-write\x07");
  Check(screen.Text().find(L"clipboard") == std::wstring::npos,
        "OSC commands are inert");
  Check(screen.Feed("\x1b[6n").find("R") != std::string::npos,
        "terminal cursor report");
  wchar_t system[MAX_PATH]{};
  GetSystemDirectoryW(system, MAX_PATH);
  Terminal terminal;
  terminal.Start(std::wstring(system) + L"\\cmd.exe", L"/d /q", system);
  terminal.Input("echo XSIME_CONPTY_READY\r");
  const auto deadline = GetTickCount64() + 8000;
  while (GetTickCount64() < deadline &&
         terminal.Output().find(L"XSIME_CONPTY_READY") == std::wstring::npos)
    Sleep(25);
  Check(terminal.Output().find(L"XSIME_CONPTY_READY") != std::wstring::npos,
        "real ConPTY receives input and renders output");
  terminal.Resize(80, 20);
  Check(terminal.Running(), "resize retains process");
  terminal.End();
  Check(!terminal.Running(), "explicit end stops process");
}
void Staging() {
  workbench::Runtime runtime;
  auto target = runtime.Register(GetCurrentProcessId() + 1, 1, 1);
  runtime.Focus(target);
  runtime.Toggle(target.process);
  runtime.Paste("existing");
  Check(runtime.StageExternal(" accepted"), "reviewed peripheral text stages");
  Check(runtime.Snapshot().at("source") == "existing accepted",
        "staging appends without deleting existing draft");
  Check(!runtime.Capturing(target),
        "staging grants no previous focus authority");
  runtime.Send(true);
  Check(runtime.Snapshot().at("source") == "existing accepted",
        "detached staging cannot deliver");
  Check(!runtime.StageExternal(std::string(workbench::Model::kLimit, 'x')),
        "oversized staging atomic rejection");
  runtime.Stop();
}
void Pump(peripherals::Windows& windows) {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (!windows.HandleMessage(&msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  windows.Tick();
}
void ClipboardCardActions(const std::filesystem::path& root) {
  Store store(root);
  FixtureImage(store);
  const auto image = store.List(Module::Temporary)[0];
  workbench::Runtime runtime;
  const auto peer = GetCurrentProcessId() + 200;
  const auto target = runtime.Register(peer, 801, 901), other = runtime.Register(peer, 802, 902);
  PasteTarget native{reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(2), peer, 99};
  DWORD sequence = 1;
  ULONGLONG now = 100;
  unsigned reads = 0, writes = 0, pastes = 0;
  bool writable = true;
  PasteResult outcome = PasteResult::Sent;
  std::string copied;
  ClipboardSource source{[&] { return sequence; }, [] { return true; }, [&](HWND) {
    ++reads; ClipboardContent result; result.opened = true; result.text = "Fixture clipboard"; return result;
  }};
  ClipboardActions actions;
  actions.text = [&](HWND, const std::string& text, bool) {
    if (!writable) return false;
    copied = text; ++writes; ++sequence; return true;
  };
  actions.asset = [&](HWND, Store& source_store, const Json& item) {
    if (!writable) return false;
    Check(ReadFile(source_store.Asset(item)) == ReadFile(store.Asset(image)), "copy uses selected image bytes");
    ++writes; ++sequence; return true;
  };
  actions.target = [&] { return native; };
  actions.now = [&] { return now; };
  actions.paste = [&](PasteTarget selected_target, DWORD selected_sequence) {
    if (sequence != selected_sequence) return PasteResult::ClipboardChanged;
    if (selected_target != native) return PasteResult::TargetChanged;
    ++pastes; return outcome;
  };
  peripherals::Windows windows(runtime, root, workbench::GenerateText, source, actions);
  const auto open = [&] {
    windows.Open(Module::Temporary);
    runtime.Focus(target);
    Check(runtime.BorrowPeripheral(peer), "fixture acquires exact TSF target without activating a user app");
  };
  const auto activate = [&] {
    const auto pane = windows.Handle(false);
    const auto scale = GetDpiForWindow(pane);
    SendMessageW(GetDlgItem(pane, 227), WM_LBUTTONDBLCLK, MK_LBUTTON,
                 MAKELPARAM(MulDiv(60, scale, 96), MulDiv(60, scale, 96)));
  };
  open();
  SendMessageW(windows.Handle(false), WM_COMMAND, 203, 0);
  auto toast = FindWindowW(L"XSIME.Clipboard.Feedback", L"已复制图片");
  Check(writes == 1 && pastes == 0 && !IsWindowVisible(windows.Handle(false)) &&
      toast && IsWindowVisible(toast) && (GetWindowLongPtrW(toast, GWL_EXSTYLE) & WS_EX_NOACTIVATE) &&
      (GetWindowLongPtrW(toast, GWL_EXSTYLE) & WS_EX_TRANSPARENT),
      "copy closes Capsule and displays a nonactivating click-through toast without pasting");
  windows.ClipboardChanged();
  Check(reads == 0 && store.List(Module::Temporary).size() == 1,
      "own writes are baselined instead of being collected again");
  SendMessageW(toast, WM_TIMER, 1, 0);
  Check(!IsWindowVisible(toast), "copy hint dismisses itself");
  open();
  Check(!IsWindowVisible(toast), "reopening hides the previous copy hint");
  activate();
  Check(writes == 2 && !IsWindowVisible(windows.Handle(false)) && pastes == 0,
      "image double-click copies and closes before dispatch");
  now += 49; windows.Tick(); Check(pastes == 0, "paste waits for closing to settle");
  now += 2; windows.Tick(); windows.Tick();
  Check(pastes == 1 && FindWindowW(L"XSIME.Clipboard.Feedback", L"已发送图片粘贴"),
      "one image activation dispatches once after closing");
  open(); activate(); runtime.Focus(other); runtime.Focus(target);
  now += 60; windows.Tick();
  Check(pastes == 1, "same-process field switch cancels queued paste even if focus returns");
  open(); activate(); ++sequence; now += 60; windows.Tick();
  Check(pastes == 1 && FindWindowW(L"XSIME.Clipboard.Feedback", L"剪贴板已变化 · 未粘贴"),
      "changed clipboard cancels pending paste with accurate feedback");
  open(); activate(); now += 1000; windows.Tick();
  Check(pastes == 1, "expired closing delay never dispatches late input");
  open(); activate(); windows.Protect(); now += 60; windows.Tick();
  Check(pastes == 1 && !IsWindowVisible(toast), "locking cancels queued paste and hides feedback");
  windows.Resume(); open(); activate();
  // Reopen detached: this fixture's synthetic peer never owns the real desktop
  // foreground, so an actively borrowed rail would correctly cancel on Tick.
  const auto saved_native = native; native = {};
  windows.Open(Module::Temporary); native = saved_native;
  now += 60; windows.Tick();
  Check(pastes == 1 && IsWindowVisible(windows.Handle(false)), "reopening cancels the previous activation");
  runtime.ReleasePeripheral();
  const auto before_detached = writes; activate();
  Check(writes == before_detached + 1 && pastes == 1 && !IsWindowVisible(windows.Handle(false)),
      "detached image activation copies and closes without inventing focus authority");
  open(); writable = false;
  SendMessageW(windows.Handle(false), WM_COMMAND, 203, 0);
  Check(IsWindowVisible(windows.Handle(false)) && !IsWindowVisible(toast),
      "failed clipboard write keeps the card visible without a success hint");
  writable = true;
  const auto note = store.Save({{"module", "notes"}, {"title", "Saved note"}, {"body", "Exact note contents"}});
  windows.Open(Module::Notes);
  SendMessageW(windows.Handle(false), WM_COMMAND, 203, 0); windows.ClipboardChanged();
  Check(copied == "Exact note contents" && !IsWindowVisible(windows.Handle(false)) && reads == 0 &&
      store.Get(note.at("id")) == note && store.List(Module::Temporary).size() == 1,
      "saved card copy preserves exact content, saved ordering and separate Recent history");
  runtime.Stop();
}
void CapsuleCancellation(const std::filesystem::path& root) {
  Store store(root);
  store.RecordClipboardText("First"); store.RecordClipboardText("Second");
  workbench::Runtime runtime;
  const ClipboardSource source{[] { return DWORD{5}; }, [] { return true; },
                               [](HWND) { return ClipboardContent{}; }};
  peripherals::Windows windows(runtime, root, {}, source);
  windows.Open();
  const auto pane = windows.Handle(false), search = GetDlgItem(pane, 100);
  Check(GetFocus() == search, "first detached invocation immediately focuses Capsule search");
  SendMessageW(search, WM_CHAR, 'S', 0);
  Check(GetWindowTextLengthW(search) == 1 && SendMessageW(GetDlgItem(pane, 102), LB_GETCOUNT, 0, 0) == 2,
        "typing immediately updates Capsule search");
  SetWindowTextW(search, L"");
  MSG key{}; key.hwnd = search; key.message = WM_KEYDOWN; key.wParam = VK_RIGHT;
  Check(windows.HandleMessage(&key) && SendMessageW(GetDlgItem(pane, 102), LB_GETCURSEL, 0, 0) == 1,
        "navigation is ready on first invocation without clicking a card");
  const auto outside = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"isolated outside fixture",
      WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  MSG click{}; click.hwnd = search; click.message = WM_LBUTTONDOWN;
  windows.HandleMessage(&click);
  MSG dismiss{};
  Check(!PeekMessageW(&dismiss, pane, OutsideClick::kDismiss, OutsideClick::kDismiss, PM_REMOVE),
        "clicking inside Capsule does not schedule dismissal");
  const auto owned = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"owned popup fixture",
      WS_POPUP, 0, 0, 10, 10, pane, nullptr, GetModuleHandleW(nullptr), nullptr);
  click.hwnd = owned; windows.HandleMessage(&click);
  Check(!PeekMessageW(&dismiss, pane, OutsideClick::kDismiss, OutsideClick::kDismiss, PM_REMOVE),
        "owned popup interaction belongs to Capsule and does not cancel it");
  DestroyWindow(owned);
  click.hwnd = outside;
  Check(!windows.HandleMessage(&click) &&
      PeekMessageW(&dismiss, pane, OutsideClick::kDismiss, OutsideClick::kDismiss, PM_REMOVE),
      "outside click schedules cancel while preserving the original mouse event");
  DispatchMessageW(&dismiss);
  Check(!IsWindowVisible(pane) && store.List(Module::Temporary).size() == 2,
        "outside click hides the rail without consuming or deleting content");
  windows.Open(); windows.HandleMessage(&click);
  Check(PeekMessageW(&dismiss, pane, OutsideClick::kDismiss, OutsideClick::kDismiss, PM_REMOVE) != FALSE,
        "second invocation has its own cancellation generation");
  SendMessageW(pane, WM_CLOSE, 0, 0); windows.Open(); DispatchMessageW(&dismiss);
  Check(IsWindowVisible(pane), "old queued outside click cannot cancel a new invocation");
  windows.SetCollectClipboard(false);
  Check(!Store(root).CollectClipboard(), "settings pause is persisted immediately");
  windows.SetCollectClipboard(true); windows.ClipboardChanged();
  Check(Store(root).CollectClipboard() && store.List(Module::Temporary).size() == 2,
        "settings resume baselines clipboard instead of importing paused content");
  SendMessageW(pane, WM_CLOSE, 0, 0);
  const auto peer = GetCurrentProcessId() + 300;
  const auto target = runtime.Register(peer, 991, 992); runtime.Focus(target);
  ClipboardActions actions;
  actions.target = [&] { return PasteTarget{outside, outside, peer, 1}; };
  peripherals::Windows bound(runtime, root, {}, source, actions);
  const auto foreground = GetForegroundWindow(); bound.Open();
  Check(runtime.Capturing(target) && GetForegroundWindow() == foreground &&
      (GetWindowLongPtrW(bound.Handle(false), GWL_EXSTYLE) & WS_EX_NOACTIVATE),
      "trusted target gives immediate borrowed input without activating Capsule");
  click.hwnd = outside; bound.HandleMessage(&click);
  Check(PeekMessageW(&dismiss, bound.Handle(false), OutsideClick::kDismiss, OutsideClick::kDismiss, PM_REMOVE),
        "clicking the original target also counts as cancellation");
  DispatchMessageW(&dismiss);
  Check(!runtime.Capturing(target) && !IsWindowVisible(bound.Handle(false)),
        "outside cancellation releases the exact TSF input lease");
  bound.Open(); runtime.ReleasePeripheral(); bound.Tick();
  Check(!IsWindowVisible(bound.Handle(false)), "losing the borrowed input lease dismisses the rail");
  DestroyWindow(outside); runtime.Stop();
}
void CaptureUI(HWND hwnd, const std::filesystem::path& path,
               IWICImagingFactory* imaging) {
  std::wcout << L"render " << path.filename().wstring() << L"\n";
  const auto dpi = GetDpiForWindow(hwnd);
  RECT client{};
  GetClientRect(hwnd, &client);
  struct Bounds {
    RECT client;
    bool valid = true;
  } bounds{client};
  std::vector<RECT> occupied;
  for (auto child = GetWindow(hwnd, GW_CHILD); child;
       child = GetWindow(child, GW_HWNDNEXT))
    if (IsWindowVisible(child)) {
      RECT r{};
      GetWindowRect(child, &r);
      MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
      wchar_t name[64]{};
      GetClassNameW(child, name, 64);
      if (std::wstring(name) == L"ComboBox")
        r.bottom = (std::min)(r.bottom, r.top + MulDiv(30, dpi, 96));
      for (const auto& previous : occupied) {
        RECT overlap{};
        if (!IsWindowVisible(GetDlgItem(hwnd, 228)) &&
            IntersectRect(&overlap, &r, &previous))
          std::wcerr << L"overlap control " << GetDlgCtrlID(child) << L" "
                     << r.left << L"," << r.top << L"," << r.right << L","
                     << r.bottom << L" with " << previous.left << L","
                     << previous.top << L"," << previous.right << L","
                     << previous.bottom << L"\n";
        Check(IsWindowVisible(GetDlgItem(hwnd, 228)) ||
                  !IntersectRect(&overlap, &r, &previous),
              "visible native controls never overlap");
      }
      occupied.push_back(r);
    }
  EnumChildWindows(
      hwnd,
      [](HWND child, LPARAM data) -> BOOL {
        auto& b = *reinterpret_cast<Bounds*>(data);
        if (!IsWindowVisible(child)) return TRUE;
        RECT r{};
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, GetParent(child), reinterpret_cast<POINT*>(&r),
                        2);
        wchar_t name[64]{};
        GetClassNameW(child, name, 64);
        // Message panels live in a clipped scroll document. Their children
        // must fit the panel; the panel itself may extend beyond the viewport.
        if (std::wstring(name) == L"RIMES.MessagePanel") return TRUE;
        RECT parent_client{};
        GetClientRect(GetParent(child), &parent_client);
        if (std::wstring(name) == L"ComboBox") r.bottom = r.top + 30;
        if (r.left < 0 || r.top < 0 || r.right > parent_client.right ||
            r.bottom > parent_client.bottom) {
          std::wcerr << L"outside control " << GetDlgCtrlID(child) << L" "
                     << name << L": " << r.left << L"," << r.top << L","
                     << r.right << L"," << r.bottom << L" client "
                     << b.client.right << L"," << b.client.bottom << L"\n";
          b.valid = false;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&bounds));
  Check(bounds.valid, "native control remains within resized client");
  Canvas canvas(client.right, client.bottom);
  SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(canvas.dc),
               PRF_CLIENT | PRF_ERASEBKGND);
  std::vector<HWND> children;
  for (auto child = GetWindow(hwnd, GW_CHILD); child;
       child = GetWindow(child, GW_HWNDNEXT))
    children.push_back(child);
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    auto child = *it;
    if (IsWindowVisible(child)) {
      RECT r{};
      GetWindowRect(child, &r);
      MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
      auto saved = SaveDC(canvas.dc);
      SetViewportOrgEx(canvas.dc, r.left, r.top, nullptr);
      wchar_t kind[32]{};
      GetClassNameW(child, kind, 32);
      if (_wcsicmp(kind, L"BUTTON") == 0 &&
          (GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW) {
        // WM_PRINT can reuse a composited button's previous surface. Render
        // through its real owner-draw handler so this export sees live state.
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_BUTTON;
        draw.CtlID = static_cast<UINT>(GetDlgCtrlID(child));
        draw.itemAction = ODA_DRAWENTIRE;
        draw.itemState = IsWindowEnabled(child) ? 0 : ODS_DISABLED;
        draw.hwndItem = child;
        draw.hDC = canvas.dc;
        GetClientRect(child, &draw.rcItem);
        SendMessageW(hwnd, WM_DRAWITEM, draw.CtlID,
                     reinterpret_cast<LPARAM>(&draw));
      } else {
        SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(canvas.dc),
                     PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
      }
      RestoreDC(canvas.dc, saved);
    }
  }
  bool varied = false;
  const auto first = GetPixel(canvas.dc, 0, 0);
  for (int y = 0; y < client.bottom && !varied; y += 11)
    for (int x = 0; x < client.right; x += 11)
      if (GetPixel(canvas.dc, x, y) != first) {
        varied = true;
        break;
      }
  Check(varied, "export must contain actual native controls");
  canvas.Save(path, imaging);
}
void SizeClient(HWND hwnd, int width, int height) {
  const auto dpi = GetDpiForWindow(hwnd);
  RECT r{0, 0, MulDiv(width, dpi, 96), MulDiv(height, dpi, 96)};
  AdjustWindowRectExForDpi(
      &r, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
      static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), dpi);
  SetWindowPos(hwnd, nullptr, 50, 50, r.right - r.left, r.bottom - r.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
}
COLORREF SurfacePixel(HWND window) {
  RECT r{};
  GetClientRect(window, &r);
  auto dc = GetDC(window);
  auto frame = CreateCompatibleDC(dc);
  auto bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
  auto old = SelectObject(frame, bitmap);
  SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(frame),
               PRF_CLIENT);
  const auto pixel = GetPixel(frame, 2, 2);
  SelectObject(frame, old);
  DeleteObject(bitmap);
  DeleteDC(frame);
  ReleaseDC(window, dc);
  return pixel;
}
void CapsuleReopening(const std::filesystem::path& root) {
  Store store(root);
  store.Save({{"module", "temporary"}, {"kind", "text"},
              {"title", "Recent fixture"}, {"body", "clipboard history"}});
  store.Save({{"module", "notes"}, {"kind", "bullet"},
              {"title", "Notes fixture"}, {"body", "saved note"}});
  workbench::Runtime runtime;
  const ClipboardSource clipboard{[] { return DWORD{1}; }, [] { return true; },
                                  [](HWND) { return ClipboardContent{}; }};
  auto displayed = [](HWND pane, const wchar_t* title) {
    wchar_t text[128]{};
    SendMessageW(GetDlgItem(pane, 102), LB_GETTEXT, 0,
                 reinterpret_cast<LPARAM>(text));
    return IsWindowVisible(pane) && std::wstring(text) == title;
  };
  {
    peripherals::Windows windows(runtime, root, workbench::GenerateText, clipboard);
    windows.Toggle(false);
    Check(displayed(windows.Handle(false), L"Recent fixture") &&
              !IsWindowVisible(GetDlgItem(windows.Handle(false), 211)),
          "first generic toggle displays Recent without the recording switch");
    SendMessageW(windows.Handle(false), WM_COMMAND,
                 MAKEWPARAM(302, BN_CLICKED), 0);
    Check(displayed(windows.Handle(false), L"Notes fixture"),
          "manual navigation selects Notes");
    windows.Toggle(false);
    windows.Toggle(false);
    Check(displayed(windows.Handle(false), L"Notes fixture"),
          "hide and show restores the manually selected module");
    windows.Open(true);
    Check(store.CapsuleSelection() == Module::Notes,
          "opening Mailbox preserves Capsule selection");
    windows.Protect();
    windows.Open(Module::Temporary);
    Check(!IsWindowVisible(windows.Handle(false)) &&
              store.CapsuleSelection() == Module::Notes,
          "blocked opens cannot replace the last successful selection");
  }
  {
    peripherals::Windows windows(runtime, root, workbench::GenerateText, clipboard);
    windows.Open();
    Check(displayed(windows.Handle(false), L"Notes fixture"),
          "a new host restores persisted selection through generic open");
    windows.Manage();
    Check(displayed(windows.Handle(false), L"Notes fixture"),
          "manager entry retains the remembered module");
    windows.Toggle(false);
    std::string error;
    Check(windows.SetPluginEnabled("builtin.capsule.notes", false, &error),
          "disable the previously selected module");
    windows.Toggle(false);
    Check(displayed(windows.Handle(false), L"Recent fixture") &&
              !store.Enabled(Module::Notes) &&
              store.CapsuleSelection() == Module::Temporary,
          "disabled selection opens and remembers Recent without re-enabling Notes");
    windows.Toggle(false);
    for (unsigned i = 0; i < 5; ++i) store.Enable(static_cast<Module>(i), false);
    windows.Open();
    windows.Manage();
    windows.Toggle(false);
    Check(!IsWindowVisible(windows.Handle(false)),
          "generic entries stay closed when all Capsule modules are disabled");
  }
  runtime.Stop();
}
void MailboxReopening(const std::filesystem::path& root) {
  Store store(root);
  const auto chat = store.Save({{"module", "chat"}, {"title", "Chat fixture"},
                                {"messages", Json::array()}});
  store.Save({{"module", "inbox"}, {"title", "Inbox fixture"},
              {"messages", Json::array()}});
  workbench::Runtime runtime;
  const ClipboardSource clipboard{[] { return DWORD{1}; }, [] { return true; },
                                  [](HWND) { return ClipboardContent{}; }};
  auto displayed = [](HWND pane, const wchar_t* title) {
    wchar_t text[128]{};
    GetWindowTextW(GetDlgItem(pane, 230), text, 128);
    return IsWindowVisible(pane) && std::wstring(text) == title;
  };
  {
    peripherals::Windows windows(runtime, root, workbench::GenerateText, clipboard);
    windows.Toggle(true);
    const auto mail = windows.Handle(true);
    Check(displayed(mail, L"Chat fixture"), "first Mailbox toggle displays Chat");
    SetWindowTextW(GetDlgItem(mail, 106), L"Draft survives module switching");
    SendMessageW(mail, WM_COMMAND, MAKEWPARAM(307, BN_CLICKED), 0);
    Check(displayed(mail, L"Inbox fixture") &&
              store.Get(chat.at("id")).value("draft", "") ==
                  "Draft survives module switching",
          "manual switch remembers Inbox and saves the Chat draft");
    windows.Toggle(true);
    windows.Toggle(true);
    Check(displayed(mail, L"Inbox fixture"), "Mailbox close/reopen restores Inbox");
    windows.Open(Module::Notes);
    windows.Protect();
    windows.Open(Module::Chat);
    Check(store.MailboxSelection() == Module::Inbox &&
              store.CapsuleSelection() == Module::Notes,
          "Capsule and blocked opens cannot replace Mailbox selection");
  }
  {
    peripherals::Windows windows(runtime, root, workbench::GenerateText, clipboard);
    windows.Open(true);
    Check(displayed(windows.Handle(true), L"Inbox fixture"),
          "new Mailbox host restores Inbox through generic open");
    std::string error;
    Check(windows.SetPluginEnabled("builtin.mailbox.inbox", false, &error),
          "disable last Mailbox selection");
    windows.Toggle(true);
    Check(displayed(windows.Handle(true), L"Chat fixture") &&
              store.MailboxSelection() == Module::Chat && !store.Enabled(Module::Inbox),
          "disabled Mailbox selection falls back without re-enabling the module");
    SendMessageW(windows.Handle(true), WM_COMMAND, MAKEWPARAM(305, BN_CLICKED), 0);
    Check(store.MailboxSelection() == Module::Terminal,
          "manual terminal navigation is remembered");
  }
  {
    peripherals::Windows windows(runtime, root, workbench::GenerateText, clipboard);
    windows.Open(true);
    Check(IsWindowVisible(GetDlgItem(windows.Handle(true), 215)) &&
              SendMessageW(GetDlgItem(windows.Handle(true), 102), LB_GETCOUNT, 0, 0) == 0,
          "restoring Terminal displays its launcher without starting a process");
    windows.Toggle(true);
    for (unsigned i = 5; i < 8; ++i) store.Enable(static_cast<Module>(i), false);
    windows.Open(true);
    windows.Toggle(true);
    windows.Manage(true);
    Check(!IsWindowVisible(windows.Handle(true)),
          "all-disabled Mailbox stays closed through every generic entry");
  }
  runtime.Stop();
}
void CardHover(const std::filesystem::path& root,
               const std::filesystem::path& output) {
  Store store(root);
  for (int i = 0; i < 3; ++i)
    store.Save({{"module", "notes"}, {"kind", "bullet"},
                {"title", "Hover fixture " + std::to_string(i)},
                {"body", "Stable card text\nSecond line"}});
  workbench::Runtime runtime;
  peripherals::Windows windows(runtime, root, workbench::GenerateText,
      ClipboardSource{[] { return DWORD{1}; }, [] { return false; },
                      [](HWND) { return ClipboardContent{}; }});
  windows.Open(Module::Notes);
  const auto pane = windows.Handle(false), cards = GetDlgItem(pane, 227);
  const auto foreground = GetForegroundWindow();
  const auto dpi = GetDpiForWindow(cards);
  auto point = [dpi](int x, int y) { return MAKELPARAM(MulDiv(x, dpi, 96), MulDiv(y, dpi, 96)); };
  RECT bounds{};
  GetClientRect(cards, &bounds);
  Canvas canvas(bounds.right, bounds.bottom);
  auto render = [&] {
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_STATIC; draw.CtlID = 227; draw.hwndItem = cards;
    draw.hDC = canvas.dc; draw.rcItem = bounds; draw.itemAction = ODA_DRAWENTIRE;
    SendMessageW(pane, WM_DRAWITEM, 227, reinterpret_cast<LPARAM>(&draw));
    return canvas.Pixels();
  };
  Microsoft::WRL::ComPtr<IWICImagingFactory> imaging;
  Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imaging))), "hover snapshot factory");
  for (auto theme : {ui::ThemeId::kDay, ui::ThemeId::kNight,
                     ui::ThemeId::kQuiet, ui::ThemeId::kRasta}) {
    windows.PreviewTheme(theme);
    SendMessageW(cards, WM_MOUSELEAVE, 0, 0);
    const auto normal = render();
    ValidateRect(cards, nullptr);
    SendMessageW(cards, WM_MOUSEMOVE, 0, point(20, 60));
    RECT damage{};
    Check(GetUpdateRect(cards, &damage, FALSE) && damage.left == 0 &&
              damage.right <= MulDiv(206, dpi, 96),
          "entering a card invalidates only that card");
    const auto hover = render();
    Check(hover != normal, "hover presents the Fluent copy affordance");
    canvas.Save(output / workbench::Wide(std::string(ui::ThemeIdString(theme)) +
                                         "-card-hover.png"), imaging.Get());
    ValidateRect(cards, nullptr);
    for (int x = 21; x < 180; ++x)
      SendMessageW(cards, WM_MOUSEMOVE, 0, point(x, 60));
    Check(!GetUpdateRect(cards, nullptr, FALSE) && render() == hover,
          "movement within one card keeps a stable frame without repaint requests");
    SendMessageW(GetDlgItem(pane, 200), WM_MOUSELEAVE, 0, 0);
    Check(render() == hover, "late leave from another control cannot clear card hover");
    SendMessageW(cards, WM_MOUSEMOVE, 0, point(208, 60));
    Check(render() == normal, "card gaps have no copy affordance");
    ValidateRect(cards, nullptr);
    SendMessageW(cards, WM_MOUSEMOVE, 0, point(230, 60));
    Check(GetUpdateRect(cards, &damage, FALSE) && damage.left >= MulDiv(214, dpi, 96) &&
              damage.right <= MulDiv(420, dpi, 96),
          "entering the second card leaves the first and unrelated cards untouched");
    SendMessageW(cards, WM_MOUSELEAVE, 0, 0);
    Check(render() == normal, "leaving restores the exact card frame");
    ValidateRect(cards, nullptr);
    SendMessageW(cards, WM_MOUSEMOVE, 0, point(700, 60));
    Check(!GetUpdateRect(cards, nullptr, FALSE), "empty space does not repaint cards");
    const auto stable = render();
    Check(SendMessageW(cards, WM_ERASEBKGND, reinterpret_cast<WPARAM>(canvas.dc), 0) &&
              canvas.Pixels() == stable,
          "background erase never exposes a blank intermediate card frame");
  }
  Check(GetForegroundWindow() == foreground, "card hover never takes foreground focus");
  runtime.Stop();
}
void UI(const std::filesystem::path& root,
        const std::filesystem::path& output) {
  Store store(root);
  FixtureImage(store);
  store.Save({{"module", "notes"},
              {"kind", "bullet"},
              {"title", "周末的小计划"},
              {"summary", "把重要的事情留在手边"},
              {"body",
               "## 今天\r\n- [ ] 整理阅读笔记\r\n- [ ] "
               "给项目补充示例\r\n\r\n## 灵感\r\n让输入与内容管理保持轻快。"}});
  store.Save({{"module", "inbox"},
              {"kind", "text"},
              {"title", "设计评审记录"},
              {"body", "对齐交互后，检查窄窗口、主题和焦点变化。"},
              {"messages", Json::array()},
              {"unread", true}});
  workbench::Runtime runtime;
  auto initial = runtime.Configuration();
  workbench::SelectConnection(&initial, "codex");
  std::string configure_error;
  Check(runtime.Configure(initial, L"", false, &configure_error),
        "fixture chooses bundled connector");
  std::atomic_bool hold = false;
  std::atomic_int requests = 0;
  peripherals::Windows windows(
      runtime, root,
      [&](const workbench::Settings& settings,
          const workbench::Generation& generation, const auto& chunk,
          const auto& cancelled, std::string*) {
        ++requests;
        Check(settings.active_connection == "codex",
              "chat generation uses frozen route");
        Check(!generation.source.empty(), "chat carries conversation");
        while (hold.load() && !cancelled()) Sleep(5);
        return !cancelled() && chunk("这是一条隔离测试回复。");
      }, ClipboardSource{[] { return DWORD{1}; }, [] { return false; },
                          [](HWND) { return ClipboardContent{}; }});
  POINT opening_pointer{};
  GetCursorPos(&opening_pointer);
  MONITORINFO opening_monitor{sizeof(opening_monitor)};
  GetMonitorInfoW(MonitorFromPoint(opening_pointer, MONITOR_DEFAULTTOPRIMARY),
                  &opening_monitor);
  windows.Open(Module::Notes);
  const auto first_capsule = windows.Handle(false);
  RECT first_frame{};
  GetWindowRect(first_capsule, &first_frame);
  Check(
      std::abs(first_frame.left + first_frame.right -
               opening_monitor.rcWork.left - opening_monitor.rcWork.right) <= 1,
      "first Capsule rail is horizontally centred on the current monitor");
  Check(
      first_frame.bottom == opening_monitor.rcWork.bottom -
                                MulDiv(24, GetDpiForWindow(first_capsule), 96),
      "first Capsule rail sits above the work-area bottom like macOS");
  SetWindowPos(first_capsule, nullptr, first_frame.left + 8,
               first_frame.top - 8, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  SendMessageW(first_capsule, WM_CLOSE, 0, 0);
  windows.Open(Module::Notes);
  RECT reopened{};
  GetWindowRect(first_capsule, &reopened);
  Check(reopened.left == first_frame.left + 8 &&
            reopened.top == first_frame.top - 8,
        "reopening Capsule retains the user's moved position");
  SendMessageW(first_capsule, WM_CLOSE, 0, 0);
  windows.Open(Module::Chat);
  const auto chat = windows.Handle(true);
  SendMessageW(chat, WM_COMMAND, MAKEWPARAM(200, BN_CLICKED), 0);
  Check(IsWindowEnabled(GetDlgItem(chat, 109)) != FALSE,
        "new conversation can choose a connector before its first turn");
  SetWindowTextW(GetDlgItem(chat, 103), L"与助手一起整理思路");
  SendMessageW(chat, WM_COMMAND, MAKEWPARAM(201, BN_CLICKED), 0);
  auto changed = runtime.Configuration();
  workbench::SelectConnection(&changed, "legacy-api");
  Check(runtime.Configure(changed, L"", false, &configure_error),
        "global route changed for isolation test");
  SetWindowTextW(GetDlgItem(chat, 106), L"请帮我整理一个小计划。");
  MSG enter{};
  enter.hwnd = GetDlgItem(chat, 106);
  enter.message = WM_KEYDOWN;
  enter.wParam = VK_RETURN;
  SendMessageW(enter.hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
  Check(!windows.HandleMessage(&enter),
        "Enter remains owned by the active IME composition");
  Check(requests == 0,
        "confirming a Chinese composition does not send a message");
  SendMessageW(enter.hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
  Check(windows.HandleMessage(&enter), "Enter sends a completed message");
  auto deadline = GetTickCount64() + 10000;
  while (GetTickCount64() < deadline) {
    Pump(windows);
    const auto conversations = store.List(Module::Chat);
    if (!conversations.empty() &&
        conversations[0].value("messages", Json::array()).size() == 2)
      break;
    Sleep(5);
  }
  auto conversations = store.List(Module::Chat);
  if (requests != 1 || conversations.empty() ||
      conversations[0].value("messages", Json::array()).size() != 2) {
    wchar_t status[2048]{};
    GetWindowTextW(GetDlgItem(chat, 108), status, 2048);
    std::wcerr << L"chat status: " << status << L"\n";
    std::cerr << "requests=" << requests.load()
              << " fixture=" << conversations.dump() << "\n";
  }
  Check(requests == 1 && conversations.size() == 1 &&
            conversations[0].at("messages").size() == 2,
        "streamed chat persists only final reply");
  Check(conversations[0].at("title") == "与助手一起整理思路",
        "rename persists");
  wchar_t heading[100]{};
  GetWindowTextW(GetDlgItem(chat, 230), heading, 100);
  Check(std::wstring(heading) == L"与助手一起整理思路",
        "rename updates the visible heading");
  CHARRANGE selected_text{1, 4};
  auto first_body = GetDlgItem(GetDlgItem(GetDlgItem(chat, 237), 1000), 1);
  Check(first_body != nullptr, "each message owns a selectable native body");
  SendMessageW(first_body, EM_EXSETSEL, 0,
               reinterpret_cast<LPARAM>(&selected_text));
  windows.Open(Module::Chat);
  CHARRANGE restored{};
  SendMessageW(first_body, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&restored));
  Check(restored.cpMin == 1 && restored.cpMax == 4,
        "transcript refresh preserves native text selection");
  const auto assistant = GetDlgItem(GetDlgItem(chat, 237), 1001);
  Check(GetDlgItem(assistant, 101) && GetDlgItem(assistant, 102),
        "inbound messages expose separate Buffer and Capsule actions");
  RECT message_body{};
  GetClientRect(GetDlgItem(assistant, 1), &message_body);
  Check(message_body.bottom <= MulDiv(60, GetDpiForWindow(chat), 96),
        "single-line message has no expanding blank body");
  SendMessageW(assistant, WM_COMMAND, MAKEWPARAM(101, BN_CLICKED), 0);
  Check(runtime.Snapshot()["source"] == "这是一条隔离测试回复。" &&
            !runtime.Snapshot()["capture"].get<bool>(),
        "per-message staging uses exactly that body with no host authority");
  selected_text = {0, 0};
  SendMessageW(first_body, EM_EXSETSEL, 0,
               reinterpret_cast<LPARAM>(&selected_text));
  SetWindowTextW(GetDlgItem(chat, 106), L"保留为草稿");
  SendMessageW(chat, WM_CLOSE, 0, 0);
  Check(store.List(Module::Chat)[0].at("draft") == "保留为草稿",
        "close preserves conversation draft");
  windows.Open(Module::Chat);
  hold = true;
  SetWindowTextW(GetDlgItem(chat, 106), L"取消这次生成");
  SendMessageW(chat, WM_COMMAND, MAKEWPARAM(207, BN_CLICKED), 0);
  store.Enable(Module::Chat, false);
  Pump(windows);
  hold = false;
  for (int i = 0; i < 30; ++i) {
    Pump(windows);
    Sleep(5);
  }
  store.Enable(Module::Chat, true);
  Check(store.List(Module::Chat)[0].at("messages").size() == 3,
        "disabled module cannot publish delayed reply");
  std::filesystem::create_directories(output);
  Microsoft::WRL::ComPtr<IWICImagingFactory> imaging;
  CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                   IID_PPV_ARGS(&imaging));
  {
    Store empty_history(root / L"clipboard-empty");
    ClipboardSource empty_source{[] { return DWORD{1}; }, [] { return false; },
                                 [](HWND) { return ClipboardContent{}; }};
    peripherals::Windows history(runtime, empty_history.Root(), {}, std::move(empty_source));
    for (const bool enabled : {true, false}) {
      empty_history.SetCollectClipboard(enabled);
      history.Open(Module::Temporary); Pump(history);
      const auto hwnd = history.Handle(false);
      Check(!IsWindowVisible(GetDlgItem(hwnd, 211)) && history.CollectClipboard() == enabled,
            "recording preference stays in settings, never on the rail");
      RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
      CaptureUI(hwnd, output / (enabled ? L"clipboard-auto-on.png" : L"clipboard-auto-paused.png"), imaging.Get());
      SendMessageW(hwnd, WM_CLOSE, 0, 0);
    }
  }
  for (const auto theme : {"night", "day"}) {
    auto config = runtime.Configuration();
    config.theme = theme;
    config.follow_system_theme = false;
    std::string error;
    Check(runtime.Configure(config, L"", false, &error), "fixture theme saved");
    for (auto module :
         {Module::Temporary, Module::Notes, Module::Passwords, Module::Capture,
          Module::Resources, Module::Chat, Module::Inbox, Module::Terminal})
      for (int width : {1000, 680}) {
        windows.Open(module);
        const auto hwnd = windows.Handle(static_cast<unsigned>(module) >= 5);
        if (static_cast<unsigned>(module) < 5)
          SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(223, BN_CLICKED), 0);
        if (module == Module::Terminal)
          Check(
              IsWindowEnabled(GetDlgItem(hwnd, 109)) != FALSE,
              "terminal launcher remains selectable after a frozen chat route");
        SizeClient(hwnd, width, width == 680 ? 500 : 700);
        Pump(windows);
        if (module == Module::Notes || module == Module::Inbox ||
            module == Module::Temporary) {
          SendMessageW(GetDlgItem(hwnd, 102), LB_SETCURSEL, 0, 0);
          SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(102, LBN_SELCHANGE),
                       reinterpret_cast<LPARAM>(GetDlgItem(hwnd, 102)));
        }
        Pump(windows);
        CaptureUI(hwnd,
                  output / (workbench::Wide(
                                std::string(theme) + "-" +
                                kModuleIds[static_cast<unsigned>(module)] +
                                "-" + std::to_string(width)) +
                            L".png"),
                  imaging.Get());
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
      }
  }
  for (const auto theme : {"day", "night"}) {
    auto config = runtime.Configuration();
    config.theme = theme;
    config.follow_system_theme = false;
    std::string error;
    Check(runtime.Configure(config, L"", false, &error),
          "interaction fixture theme");
    windows.Open(Module::Notes);
    auto capsule = windows.Handle(false);
    Check(GetFocus() == GetDlgItem(capsule, 100) &&
              (GetWindowLongPtrW(capsule, GWL_EXSTYLE) &
               (WS_EX_NOACTIVATE | WS_EX_TOPMOST)) ==
                  WS_EX_TOPMOST,
          "detached Capsule is floating and immediately ready for local search");
    const auto host_plugins = windows.Plugins();
    Check(host_plugins.size() == 8 &&
              host_plugins[2].id == "builtin.capsule.notes" &&
              host_plugins[2].kind == "capsule" &&
              host_plugins[6].kind == "mailbox",
          "peer hosts expose their modules under distinct plugin kinds");
    Check(windows.SetPluginEnabled("builtin.capsule.notes", false, &error) &&
              !store.Enabled(Module::Notes) && !IsWindowVisible(capsule),
          "settings plugin disable immediately retires the matching host "
          "surface");
    Check(windows.SetPluginEnabled("builtin.capsule.notes", true, &error) &&
              store.Enabled(Module::Notes),
          "settings and the host module menu share persisted enablement");
    windows.Open(Module::Notes);
    Pump(windows);
    RECT rail{};
    GetClientRect(capsule, &rail);
    Check(MulDiv(rail.bottom, 96, GetDpiForWindow(capsule)) == 208,
          "Capsule opens as the 208pt rail");
    CaptureUI(
        capsule,
        output / workbench::Wide(std::string(theme) + "-capsule-rail.png"),
        imaging.Get());
    const auto cards = GetDlgItem(capsule, 227);
    SendMessageW(cards, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(60, 60));
    Pump(windows);
    RECT detail{};
    GetClientRect(capsule, &detail);
    Check(EqualRect(&rail, &detail),
          "card detail stays in place without resizing the rail");
    Check(GetWindowLongPtrW(capsule, GWL_EXSTYLE) & WS_EX_TOPMOST,
          "inline card detail retains the floating rail role");
    CaptureUI(
        capsule,
        output / workbench::Wide(std::string(theme) + "-capsule-detail.png"),
        imaging.Get());
    MSG escape{};
    escape.hwnd = cards;
    escape.message = WM_KEYDOWN;
    escape.wParam = VK_ESCAPE;
    Check(windows.HandleMessage(&escape) && IsWindowVisible(capsule),
          "Escape leaves detail before closing Capsule");
    SendMessageW(capsule, WM_COMMAND, MAKEWPARAM(232, BN_CLICKED), 0);
    Pump(windows);
    RECT editor{};
    GetClientRect(capsule, &editor);
    Check(MulDiv(editor.right, 96, GetDpiForWindow(capsule)) == 460 &&
              MulDiv(editor.bottom, 96, GetDpiForWindow(capsule)) == 400,
          "Capsule compact editor uses macOS content dimensions");
    Check((GetWindowLongPtrW(capsule, GWL_EXSTYLE) &
           (WS_EX_TOPMOST | WS_EX_NOACTIVATE)) == 0,
          "Capsule editor is an ordinary activatable window");
    CaptureUI(
        capsule,
        output / workbench::Wide(std::string(theme) + "-capsule-editor.png"),
        imaging.Get());
    windows.HandleMessage(&escape);
    Pump(windows);
    Check((GetWindowLongPtrW(capsule, GWL_EXSTYLE) &
           (WS_EX_TOPMOST | WS_EX_NOACTIVATE)) ==
              WS_EX_TOPMOST,
          "returning from the editor restores the floating interactive rail");
    SendMessageW(GetDlgItem(capsule, 100), WM_LBUTTONDOWN, MK_LBUTTON, 0);
    Check(GetWindowLongPtrW(capsule, GWL_EXSTYLE) & WS_EX_TOPMOST,
          "standalone search keeps Capsule in the topmost band");
    SendMessageW(capsule, WM_CLOSE, 0, 0);
    windows.Open(Module::Chat);
    auto mail = windows.Handle(true);
    Check((GetWindowLongPtrW(mail, GWL_EXSTYLE) &
           (WS_EX_TOPMOST | WS_EX_NOACTIVATE)) == 0,
          "Mailbox is an ordinary activatable peer host");
    SizeClient(mail, 680, 500);
    Pump(windows);
    RECT before{}, after{};
    GetWindowRect(GetDlgItem(mail, 237), &before);
    SendMessageW(mail, WM_COMMAND, MAKEWPARAM(213, BN_CLICKED), 0);
    Pump(windows);
    GetWindowRect(GetDlgItem(mail, 237), &after);
    Check(EqualRect(&before, &after) && IsWindowVisible(GetDlgItem(mail, 237)),
          "sidebar drawer overlays a stable live transcript");
    CaptureUI(
        mail,
        output / workbench::Wide(std::string(theme) + "-mailbox-drawer.png"),
        imaging.Get());
    SendMessageW(mail, WM_COMMAND, MAKEWPARAM(228, BN_CLICKED), 0);
    Pump(windows);
    Check(!IsWindowVisible(GetDlgItem(mail, 228)),
          "clicking the scrim dismisses its drawer");
    SizeClient(mail, 1000, 700);
    Pump(windows);
    Check(IsWindowVisible(GetDlgItem(mail, 102)),
          "widening restores the sidebar");
    SendMessageW(mail, WM_COMMAND, MAKEWPARAM(213, BN_CLICKED), 0);
    Pump(windows);
    Check(!IsWindowVisible(GetDlgItem(mail, 102)),
          "wide sidebar can be manually hidden");
    SendMessageW(mail, WM_COMMAND, MAKEWPARAM(213, BN_CLICKED), 0);
    windows.Open(Module::Terminal);
    SizeClient(mail, 680, 500);
    Pump(windows);
    GetWindowRect(GetDlgItem(mail, 105), &before);
    SendMessageW(mail, WM_COMMAND, MAKEWPARAM(233, BN_CLICKED), 0);
    Pump(windows);
    GetWindowRect(GetDlgItem(mail, 105), &after);
    Check(EqualRect(&before, &after),
          "terminal inspector overlay does not resize the terminal canvas");
    CaptureUI(mail,
              output / workbench::Wide(std::string(theme) +
                                       "-terminal-inspector.png"),
              imaging.Get());
    SendMessageW(mail, WM_COMMAND, MAKEWPARAM(228, BN_CLICKED), 0);
    SendMessageW(mail, WM_CLOSE, 0, 0);
  }
  auto rich = store.List(Module::Chat)[0];
  auto light = runtime.Configuration();
  light.theme = "day";
  Check(runtime.Configure(light, L"", false, &configure_error),
        "rich message light theme");
  rich["messages"] = Json::array(
      {{{"id", "prompt"}, {"role", "user"}, {"body", "检查消息样式与交互"}},
       {{"id", "markdown"},
        {"role", "assistant"},
        {"format", "markdown"},
        {"body",
         "# 格式与排版\n**重点**、*强调* 与 `inline code`\n> 引用文字\n- [x] "
         "完成一项\n- [ ] 继续检查\n\n| 项目 | 状态 |\n| --- | --- |\n| "
         "Capsule | 完成 |\n| Mailbox | 验证中 |\n\n```cpp\nconst auto message "
         "= \"hello\";\n```\n[打开项目网站](https://example.com)"}}});
  store.Save(rich, rich.at("revision"));
  windows.Open(Module::Chat);
  SizeClient(chat, 1000, 820);
  Pump(windows);
  CaptureUI(chat, output / L"day-message-formatting.png", imaging.Get());
  auto image_record = store.List(Module::Temporary)[0];
  auto inbox_image = store.Import(Module::Inbox, store.Asset(image_record));
  windows.Open(Module::Inbox);
  SetWindowTextW(GetDlgItem(chat, 100),
                 workbench::Wide(inbox_image.value("title", "")).c_str());
  SendMessageW(GetDlgItem(chat, 102), LB_SETCURSEL, 0, 0);
  SendMessageW(chat, WM_COMMAND, MAKEWPARAM(102, LBN_SELCHANGE), 0);
  Pump(windows);
  CaptureUI(chat, output / L"day-message-image.png", imaging.Get());
  windows.Open(Module::Notes);
  windows.Open(Module::Chat);
  SetWindowTextW(GetDlgItem(chat, 106), L"皮肤切换保留的草稿");
  for (const auto id : {ui::ThemeId::kNight, ui::ThemeId::kDay,
                        ui::ThemeId::kQuiet, ui::ThemeId::kRasta}) {
    windows.PreviewTheme(id);
    Pump(windows);
    Check(SurfacePixel(windows.Handle(false)) ==
              ui::ToColorRef(appearance::Palette(id, false).background),
          "visible Capsule immediately follows the selected theme");
    Check(SurfacePixel(chat) ==
              ui::ToColorRef(appearance::Palette(id, true).sidebar),
          "visible Mailbox immediately follows all four theme palettes");
    wchar_t draft[64]{};
    GetWindowTextW(GetDlgItem(chat, 106), draft, 64);
    Check(std::wstring(draft) == L"皮肤切换保留的草稿",
          "theme preview preserves the Mailbox draft");
    CaptureUI(chat,
              output / (workbench::Wide(ui::ThemeIdString(id)) +
                        L"-mailbox-theme.png"),
              imaging.Get());
    CaptureUI(windows.Handle(false),
              output / (workbench::Wide(ui::ThemeIdString(id)) +
                        L"-capsule-theme.png"),
              imaging.Get());
  }
  windows.PreviewTheme(std::nullopt);
  Check(
      SurfacePixel(chat) ==
          ui::ToColorRef(appearance::Palette(ui::ThemeId::kDay, true).sidebar),
      "discarding theme preview restores the saved skin");
  // A hidden manager must be resized before becoming a rail again.
  windows.Manage(false);
  SendMessageW(windows.Handle(false), WM_CLOSE, 0, 0);
  windows.Toggle(false);
  RECT rail{};
  GetClientRect(windows.Handle(false), &rail);
  Check(MulDiv(rail.bottom, 96, GetDpiForWindow(windows.Handle(false))) ==
            appearance::kRailHeight,
        "summoning a closed manager shows the final rail dimensions");
  for (const bool mailbox : {false, true}) {
    const auto pane = windows.Handle(mailbox);
    if (pane && IsWindowVisible(pane)) SendMessageW(pane, WM_CLOSE, 0, 0);
    windows.Toggle(mailbox);
    Check(IsWindowVisible(windows.Handle(mailbox)),
          "first host toggle opens component");
    windows.Toggle(mailbox);
    Check(!IsWindowVisible(windows.Handle(mailbox)),
          "second host toggle closes component");
  }
  runtime.Stop();
}
}  // namespace

void CaptureFeatures(const std::filesystem::path& root,
                     const std::filesystem::path& output) {
  Store store(root);
  CaptureImage base;
  base.width = 640;
  base.height = 400;
  for (int y = 0; y < base.height; ++y)
    for (int x = 0; x < base.width; ++x)
      base.pixels.push_back(0xff000000U | ((40 + x / 4) << 16) |
                            ((80 + y / 4) << 8) | 160);
  const auto rect = CaptureSelection({500, 300}, {100, 80}, 640, 400);
  auto crop = CropCapture(base, rect);
  Check(crop.width == 400 && crop.height == 220 &&
            crop.pixels[0] == base.pixels[80 * 640 + 100],
        "capture reverse drag retains exact source pixels");
  Reject([&] { CropCapture(base, {0, 0, 0, 3}); }, "empty crop is rejected");
  Json operations =
      Json::array({{{"tool", "redact"}, {"points", {{20, 20}, {90, 80}}}}});
  auto covered = RenderCapture(base, operations);
  Check(covered.pixels[40 * 640 + 40] == 0xff141414U &&
            covered.pixels[0] == base.pixels[0],
        "redaction changes exported opaque pixels only in selection");
  operations.push_back({{"tool", "crop"}, {"points", {{10, 10}, {300, 200}}}});
  auto edited = RenderCapture(base, operations);
  Check(edited.width == 290 && edited.height == 190 &&
            edited.pixels[30 * 290 + 30] == 0xff141414U,
        "crop preserves earlier marks");
  auto item = store.ImportData(Module::Capture, "截图测试", ".png",
                               EncodeCapture(edited));
  auto roundtrip = LoadCapture(store.Asset(item));
  Check(roundtrip.pixels == edited.pixels, "PNG lossless encode/decode");
  Reject(
      [&] {
        RenderCapture(
            base,
            Json::array({{{"tool", "pen"}, {"points", {{-1, 0}, {10, 10}}}}}));
      },
      "invalid project coordinates rejected");
  int notifications = 0;
  CaptureWindows ui(
      store, [&] { ++notifications; }, [] { return "night"; },
      [&] { return std::make_pair(base, RECT{40, 40, 680, 440}); });
  ui.Start();
  auto select = ui.SelectionWindow();
  Check(select && IsWindowVisible(select),
        "area capture opens an actual selection surface");
  SendMessageW(select, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
  SendMessageW(select, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(600, 360));
  SendMessageW(select, WM_LBUTTONUP, 0, MAKELPARAM(600, 360));
  Check(!IsWindowVisible(select) && IsWindowVisible(ui.StackWindow()) &&
            notifications == 1,
        "selection stores automatically and presents stack");
  auto entries = store.List(Module::Capture);
  Check(entries.size() == 2, "capture persists one record");
  auto captured = entries[0];
  auto captureImage = LoadCapture(store.Asset(captured));
  Check(captureImage.width == 580 && captureImage.height == 340,
        "selection is saved at physical resolution");
  ui.Start();
  SendMessageW(ui.SelectionWindow(), WM_KEYDOWN, VK_ESCAPE, 0);
  Check(store.List(Module::Capture).size() == 2 &&
            IsWindowVisible(ui.StackWindow()),
        "Escape cancels without a record and restores existing stack");
  ui.Edit(captured);
  auto editor = ui.EditorWindow();
  Check(editor && IsWindowVisible(editor), "stack image opens native editor");
  RECT client{};
  GetClientRect(editor, &client);
  SendMessageW(editor, WM_COMMAND, 103, 0);
  SendMessageW(editor, WM_LBUTTONDOWN, MK_LBUTTON,
               MAKELPARAM(client.right / 3, client.bottom / 3));
  SendMessageW(editor, WM_MOUSEMOVE, MK_LBUTTON,
               MAKELPARAM(client.right / 2, client.bottom / 2));
  SendMessageW(editor, WM_LBUTTONUP, 0,
               MAKELPARAM(client.right / 2, client.bottom / 2));
  Check(IsWindowEnabled(GetDlgItem(editor, 108)),
        "annotation creates an unsaved edit");
  SendMessageW(editor, WM_COMMAND, 106, 0);
  Check(IsWindowEnabled(GetDlgItem(editor, 107)), "undo enables redo");
  SendMessageW(editor, WM_COMMAND, 107, 0);
  SendMessageW(editor, WM_COMMAND, 108, 0);
  auto saved = store.Get(captured.at("id"));
  Check(saved.at("revision").get<int>() == 2 &&
            saved.at("captureDocument").at("source") == captured.at("asset") &&
            saved.at("captureDocument").at("operations").size() == 1,
        "saved edits retain original and a reopenable project");
  Check(std::filesystem::exists(store.Asset(captured)),
        "original asset survives save");
  SendMessageW(editor, WM_CLOSE, 0, 0);
  ui.Edit(saved);
  Check(IsWindowEnabled(GetDlgItem(editor, 106)) &&
            !IsWindowEnabled(GetDlgItem(editor, 108)),
        "reopened edit retains undo and starts clean");
  if (!output.empty()) {
    std::filesystem::create_directories(output);
    Microsoft::WRL::ComPtr<IWICImagingFactory> imaging;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                     CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&imaging))),
          "capture fixture imaging");
    CaptureUI(editor, output / L"capture-editor.png", imaging.Get());
    CaptureUI(ui.StackWindow(), output / L"capture-stack.png", imaging.Get());
  }
  SendMessageW(ui.StackWindow(), WM_COMMAND, 203, 0);
  Check(store.List(Module::Capture).size() == 2,
        "closing stack card never deletes history");
  ui.Protect();
  Check(!IsWindowVisible(editor) && !IsWindowVisible(ui.StackWindow()),
        "lock hides all capture surfaces");
  store.Enable(Module::Capture, false);
  Reject([&] { ui.Start(); }, "disabled capture cannot be triggered");
}

int wmain(int argc, wchar_t** argv) {
  try {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto root = std::filesystem::temp_directory_path() /
                (L"xsime-peripheral-test-" + workbench::Wide(NewId()));
    std::filesystem::create_directories(root);
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    Storage(root / L"store");
    HistoryOrdering(root / L"history-ordering");
    HistoryCleanup(root / L"history-cleanup");
    ImagePasteGuards();
    CapsulePreferences(root / L"capsule-preferences");
    MailboxPreferences(root / L"mailbox-preferences");
    AutomaticClipboard(root / L"clipboard");
    CaptureFeatures(root / L"capture",
                    argc == 3 && std::wstring(argv[1]) == L"--ui"
                        ? std::filesystem::path(argv[2])
                        : std::filesystem::path{});
    Routes();
    TerminalTest();
    Staging();
    auto markdown = FormatMessage(
        "# Title\n**bold** and *italic* and "
        "`code`\n[site](https://example.com)\n> quote\n| A | B |\n|---|---|\n| "
        "one | two |\n",
        true);
    std::wstring display;
    bool bold = false, italic = false, code = false, link = false,
         quote = false;
    for (const auto& span : markdown) {
      display += span.text;
      bold |= span.bold;
      italic |= span.italic;
      code |= span.code;
      link |= span.link == L"https://example.com";
      quote |= span.quote;
    }
    Check(bold && italic && code && link && quote &&
              display.find(L'\t') != display.npos &&
              display.find(L"**") == display.npos &&
              display.find(L"---") == display.npos,
          "Markdown preserves semantic styles, safe links and selectable table "
          "columns");
    auto unsafe = FormatMessage("[bad](file:///C:/Windows/test.exe)", true);
    Check(
        std::all_of(unsafe.begin(), unsafe.end(),
                    [](const auto& s) { return s.link.empty(); }),
        "message formatting never turns local or executable URIs into actions");
    if (argc == 3 && std::wstring(argv[1]) == L"--ui") {
      CapsuleReopening(root / L"capsule-reopening");
      ClipboardCardActions(root / L"clipboard-actions");
      CapsuleCancellation(root / L"capsule-cancellation");
      MailboxReopening(root / L"mailbox-reopening");
      CardHover(root / L"card-hover", argv[2]);
      UI(root / L"ui", argv[2]);
    }
    std::cout
        << "PASS: Capsule persistence, conflicts, DPAPI, physical chords, "
           "Mailbox routing/assets, ConPTY, detached Buffer staging"
        << std::endl;
    std::filesystem::remove_all(root);
    CoUninitialize();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
