#include <Windows.h>
#include "../core/control.hpp"
#include <iostream>
#include <iterator>
#include <string>
int wmain(int argc, wchar_t** argv) {
  const std::wstring mode = argc > 1 ? argv[1] : L"success";
  if (mode == L"login-output" || mode == L"login-cancel") {
    std::cerr << "https://auth.openai.com/oauth/authorize?state=" << std::flush;
    Sleep(50);
    std::cerr << "fixture&code_challenge=fixture\n" << std::flush;
    Sleep(mode == L"login-cancel" ? 30000 : 100);
    std::cout << "login-complete\n" << std::flush;
    return 0;
  }
  if (mode.starts_with(L"app-")) {
    using J = rimes::windows::core::Json;
    const auto emit = [](const J& value) { std::cout << value.dump() << "\n" << std::flush; };
    std::string line;
    while (std::getline(std::cin, line)) {
      const auto j = J::parse(line); const auto method = j.value("method", "");
      if (method == "initialize") emit({{"id",1},{"result",J::object()}});
      else if (method == "mcpServerStatus/list") emit({{"id",2},{"result",{{"data",mode == L"app-mcp" ? J::array({1}) : J::array()}}}});
      else if (method == "thread/start") emit({{"id",3},{"result",{{"thread",{{"id","t"}}},{"model",mode == L"app-model" ? "other" : "fixture-model"}}}});
      else if (method == "turn/start") {
        if (j["params"]["input"][0]["text"] != "synthetic source" || j["params"]["effort"] != "high") return 9;
        emit({{"id",4},{"result",{{"turn",{{"id","u"}}}}}});
        const auto event = [&](const char* method, J params) {
          params["threadId"] = "t"; params["turnId"] = "u"; emit({{"method",method},{"params",params}});
        };
        if (mode == L"app-tool") { event("item/started",{{"item",{{"type","commandExecution"},{"id","bad"}}}}); continue; }
        event("item/reasoning/summaryTextDelta",{{"itemId","r"},{"delta","公开摘要"},{"summaryIndex",0}});
        event("item/reasoning/textDelta",{{"itemId","r"},{"delta","private chain never shown"}});
        event("item/started",{{"item",{{"type","agentMessage"},{"id","a"},{"phase","final_answer"}}}});
        event("item/agentMessage/delta",{{"itemId","a"},{"delta","你好。"}});
        Sleep(mode == L"app-delay" ? 30000 : 60);
        if (mode == L"app-malformed") { std::cout << "broken\n" << std::flush; continue; }
        event("item/agentMessage/delta",{{"itemId","a"},{"delta","第二块💡。"}});
        event("item/completed",{{"item",{{"type","agentMessage"},{"id","a"},{"phase","final_answer"},{"text","你好。第二块💡。"}}}});
        if (mode == L"app-unfinished") return 0;
        event("turn/completed",{{"turn",{{"id","u"},{"status","completed"}}}});
      }
    }
    return 0;
  }
  if (mode.starts_with(L"worker")) {
    if (mode == L"worker-blocked") { Sleep(30000); return 0; }
    std::string line; unsigned count = 0;
    while (std::getline(std::cin, line)) {
      if (mode == L"worker-delay") { Sleep(30000); return 0; }
      if (mode == L"worker-invalid") { std::cout << "first\nextra\n" << std::flush; continue; }
      std::cout << GetCurrentProcessId() << ":" << ++count << ":" << line << "\n" << std::flush;
    }
    return 0;
  }
  if (mode == L"blocked") { Sleep(30000); return 0; }
  const std::string input((std::istreambuf_iterator<char>(std::cin)), {});
  if (input != "synthetic source & ; \"中文💡\"") return 9;
  if (mode == L"delay") { Sleep(30000); return 0; }
  if (mode == L"stderr") { std::cerr << std::string(1100000, 'x'); return 1; }
  std::cout << "{\"type\":\"thread.started\",\"thread_id\":\"fixture\"}\n{\"type\":\"turn.started\"}\n";
  if (mode == L"tool") {
    std::cout << "{\"type\":\"item.started\",\"item\":{\"type\":\"command_execution\"}}\n"; return 0;
  }
  if (mode == L"malformed") { std::cout << "not-json\n"; return 0; }
  std::cout << "{\"type\":\"item.completed\",\"item\":{\"type\":\"reasoning\",\"text\":\"private reasoning\"}}\n";
  std::cout << "{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"你好💡\"}}\n";
  if (mode != L"unfinished") std::cout << "{\"type\":\"turn.completed\"}\n";
  return mode == L"nonzero" ? 3 : 0;
}
