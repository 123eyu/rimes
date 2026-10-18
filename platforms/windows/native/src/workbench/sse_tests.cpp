#include <cstdlib>
#include <iostream>

#include "sse.hpp"
using namespace rimes::windows;
void Check(bool ok, const char* reason) {
  if (!ok) {
    std::cerr << reason << '\n';
    std::exit(1);
  }
}
int main() {
  const std::string text =
      ": ping\r\ndata: "
      "{\"choices\":[{\"delta\":{\"content\":\"你好💡\"}}]}\r\n\r\ndata: "
      "{\"choices\":[]}\n\ndata: [DONE]\n\n";
  for (std::size_t step = 1; step <= text.size(); ++step) {
    std::string output;
    workbench::SseDecoder decoder([&](const std::string& value) {
      output += value;
      return true;
    });
    for (std::size_t at = 0; at < text.size(); at += step)
      Check(decoder.Feed(text.substr(at, step)),
            "arbitrary transport boundaries");
    Check(decoder.Done() && output == "你好💡",
          "UTF-8 streaming and explicit completion");
  }
  workbench::SseDecoder incomplete([](const auto&) { return true; });
  Check(incomplete.Feed("data: {\"choices\":[]}\n\n") && !incomplete.Done(),
        "EOF without DONE is failure");
  workbench::SseDecoder bad([](const auto&) { return true; });
  Check(!bad.Feed("data: {broken}\n\n"), "malformed JSON rejected");
  workbench::SseDecoder denied([](const auto&) { return true; });
  Check(!denied.Feed("data: {\"error\":{}}\n\n"), "provider error rejected");
  workbench::SseDecoder cancelled([](const auto&) { return false; });
  Check(!cancelled.Feed(text), "cancelled callback stops stream");
  workbench::SseDecoder oversized([](const auto&) { return true; });
  Check(!oversized.Feed(std::string(256 * 1024 + 1, 'x')), "event bound");
  Check(!core::DecodeControl(core::EncodeControl(core::Json::array())),
        "control object required");
  auto nested = core::Json::object();
  for (int i = 0; i < 18; ++i) nested = core::Json{{"a", nested}};
  Check(!core::DecodeControl(core::EncodeControl(nested)), "bounded nesting");
  std::string body, summary, activity;
  workbench::SseDecoder thinking([&](const std::string& text) { body += text; return true; },
      [&](bool public_summary, const std::string& text) { (public_summary ? summary : activity) = text; return true; });
  Check(thinking.Feed("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"never show raw chain\"}}]}\n\n") &&
      body.empty() && summary.empty() && activity == "模型正在思考…", "raw reasoning emits activity only");
  Check(thinking.Feed("data: {\"choices\":[{\"delta\":{\"reasoning_summary\":\"公开摘要\",\"content\":\"正文\"}}]}\n\ndata: [DONE]\n\n") &&
      thinking.Done() && body == "正文" && summary == "公开摘要", "public summary and answer use separate channels");
  std::cout << "SSE/control tests passed\n";
}
