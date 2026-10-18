#pragma once
#include "provider.hpp"
#include "../core/control.hpp"
#include <functional>
#include <map>

namespace rimes::windows::workbench {
// The app-server emits real deltas. Only public summaries are shown; raw
// reasoning and commentary never become deliverable answer blocks.
class CodexAppServer {
 public:
  using Publish = std::function<bool(const std::string&)>;
  using Progress = std::function<bool(bool, const std::string&)>;
  CodexAppServer(std::string cwd, std::string prompt, std::string model,
      std::string effort, Publish send, Publish snapshot, Progress progress)
      : cwd_(std::move(cwd)), prompt_(std::move(prompt)), model_(std::move(model)),
        effort_(std::move(effort)), send_(std::move(send)), snapshot_(std::move(snapshot)), progress_(std::move(progress)) {}
  bool Start() {
    return Send(1, "initialize", {{"clientInfo", {{"name", "rimes"}, {"version", "1"}}}});
  }
  bool Feed(std::string_view bytes) {
    if (failed_ || bytes.size() > 4 * 1024 * 1024 - total_) return Fail();
    total_ += bytes.size(); pending_.append(bytes);
    try {
      for (auto end = pending_.find('\n'); end != std::string::npos; end = pending_.find('\n')) {
        auto line = pending_.substr(0, end); pending_.erase(0, end + 1);
        if (!line.empty() && !Consume(core::Json::parse(line))) return Fail();
      }
      return pending_.size() <= Model::kLimit * 6 || Fail();
    } catch (...) { return Fail(); }
  }
  bool Done() const { return done_ && !failed_; }
 private:
  using Json = core::Json;
  bool Fail() { failed_ = true; return false; }
  bool Send(int id, const char* method, Json params) {
    return send_(Json{{"id", id}, {"method", method}, {"params", std::move(params)}}.dump() + "\n");
  }
  bool ProgressText(bool summary, const std::string& text) {
    return !progress_ || progress_(summary, text);
  }
  bool PublishText(const std::string& text) {
    if (text.size() > Model::kLimit || text.find('\0') != std::string::npos) return false;
    (void)Wide(text); return snapshot_(text);
  }
  bool Consume(const Json& j) {
    if (done_) return true;
    // Never service tool/approval requests, including requests sharing our IDs.
    if (j.contains("id")) {
      if (j.contains("method") || j.contains("error") || !j.contains("result")) return false;
      const auto id = j.at("id").get<int>();
      if (id != stage_ || stage_ > 4) return false;
      const auto& r = j.at("result"); if (!r.is_object()) return false;
      ++stage_;
      if (id == 1) {
        return send_("{\"method\":\"initialized\",\"params\":{}}\n") &&
            Send(2, "mcpServerStatus/list", {{"detail", "toolsAndAuthOnly"}, {"limit", 100}});
      }
      if (id == 2) {
        if (!r.at("data").is_array() || !r.at("data").empty() ||
            (r.contains("nextCursor") && !r.at("nextCursor").is_null())) return false;
        if (prompt_.empty()) { done_ = true; return true; } // Prompt-free compatibility probe.
        Json params{{"cwd", cwd_}, {"approvalPolicy", "never"}, {"ephemeral", true}, {"personality", "none"}};
        if (!model_.empty()) { params["model"] = model_; params["allowProviderModelFallback"] = false; }
        if (!effort_.empty()) params["config"] = {{"model_reasoning_effort", effort_}};
        return Send(3, "thread/start", std::move(params));
      }
      if (id == 3) {
        thread_ = r.at("thread").at("id").get<std::string>();
        if (thread_.empty() || (!model_.empty() && r.value("model", "") != model_)) return false;
        Json params{{"threadId", thread_}, {"input", Json::array({{{"type", "text"}, {"text", prompt_}}})}, {"summary", "concise"}};
        if (!model_.empty()) params["model"] = model_;
        if (!effort_.empty()) params["effort"] = effort_;
        return Send(4, "turn/start", std::move(params)) && ProgressText(false, "请求已提交，等待模型…");
      }
      const auto id_value = r.at("turn").at("id").get<std::string>();
      if (id_value.empty() || (!turn_.empty() && turn_ != id_value)) return false;
      turn_ = id_value; return true;
    }
    const auto method = j.value("method", "");
    const auto p = j.value("params", Json::object());
    if (p.contains("threadId") && !thread_.empty() && p.at("threadId") != thread_) return false;
    if (p.contains("turnId") && !turn_.empty() && p.at("turnId") != turn_) return false;
    if (method == "model/rerouted") return false;
    if (method == "error") return p.value("willRetry", false) && ProgressText(false, "连接波动，正在重试…");
    if (method == "turn/started") {
      if (stage_ < 4) return false;
      const auto value = p.at("turn").at("id").get<std::string>();
      if (value.empty() || (!turn_.empty() && value != turn_)) return false;
      turn_ = value; return true;
    }
    if (method.starts_with("item/") || method == "turn/completed") {
      if (stage_ < 4 || turn_.empty()) return false;
    }
    if (method == "item/started" || method == "item/completed") {
      const auto& item = p.at("item"); const auto kind = item.at("type").get<std::string>();
      if (kind != "agentMessage" && kind != "reasoning" && kind != "userMessage") return false;
      if (kind == "reasoning") return ProgressText(false, "模型正在思考…");
      if (kind != "agentMessage") return true;
      const auto id = item.at("id").get<std::string>();
      if (id.empty() || texts_.size() > 256) return false;
      if (item.contains("phase") && item["phase"].is_string()) phases_[id] = item["phase"].get<std::string>();
      if (method == "item/started") {
        texts_.try_emplace(id);
        if (phases_[id] != "commentary") final_received_ = false;
        return ProgressText(false, "正在生成回复…");
      }
      if (!texts_.contains(id)) return false;
      const auto text = item.at("text").get<std::string>();
      if (phases_[id] == "commentary") return true;
      final_ = text; final_received_ = true; return PublishText(text);
    }
    if (method == "item/agentMessage/delta") {
      const auto id = p.at("itemId").get<std::string>();
      if (!texts_.contains(id)) return false;
      auto& text = texts_[id]; const auto delta = p.at("delta").get<std::string>();
      if (texts_.size() > 256 || text.size() + delta.size() > Model::kLimit) return false;
      text += delta;
      return phases_[id] == "commentary" ? true : PublishText(text);
    }
    if (method == "item/reasoning/summaryTextDelta") {
      const auto delta = p.at("delta").get<std::string>();
      // Bound the visible summary without ever truncating a UTF-8 code point.
      summary_ += delta;
      if (summary_.size() > 16384) {
        auto cut = summary_.size() - 16384;
        while (cut < summary_.size() && (static_cast<unsigned char>(summary_[cut]) & 0xc0) == 0x80) ++cut;
        summary_.erase(0, cut);
      }
      return ProgressText(true, summary_);
    }
    if (method == "item/reasoning/summaryPartAdded") {
      if (!summary_.empty()) summary_ += "\n";
      return ProgressText(false, "模型正在思考…");
    }
    if (method == "turn/completed") {
      if (p.at("turn").at("id") != turn_ || p.at("turn").value("status", "") != "completed" ||
          !final_received_ || final_.empty()) return false;
      done_ = true;
    }
    // item/reasoning/textDelta intentionally ignored.
    return true;
  }
  std::string cwd_, prompt_, model_, effort_, pending_, thread_, turn_, final_, summary_;
  Publish send_, snapshot_; Progress progress_;
  std::map<std::string, std::string> texts_, phases_;
  std::size_t total_ = 0;
  int stage_ = 1;
  bool done_ = false, failed_ = false, final_received_ = false;
};
}
