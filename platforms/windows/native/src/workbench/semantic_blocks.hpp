#pragma once
#include "provider.hpp"
#include <icu.h>
#include <set>
#include <cwctype>

namespace rimes::windows::workbench {
struct SemanticFragment { std::size_t index; std::string text; };
// SemanticBlockSegmenter.swift: exact text, grapheme boundaries, four Latin
// words / 48 characters, protected code/quotes/URLs, at most 200 balanced chips.
inline std::vector<SemanticFragment> SemanticBlocks(const std::string& text) {
  const auto wide = Wide(text);
  UErrorCode error = U_ZERO_ERROR;
  auto* iterator = ubrk_open(UBRK_CHARACTER, "", reinterpret_cast<const UChar*>(wide.data()), static_cast<int32_t>(wide.size()), &error);
  if (U_FAILURE(error) || !iterator) throw std::runtime_error("Grapheme segmentation unavailable");
  std::vector<std::wstring> chars;
  for (int32_t begin = ubrk_first(iterator), end = ubrk_next(iterator); end != UBRK_DONE; begin = end, end = ubrk_next(iterator))
    chars.push_back(wide.substr(begin, end-begin));
  ubrk_close(iterator);
  const std::wstring boundaries = L"。！？!?；;，,：:.", closings = L"”’\"'）)】]》〉」』";
  const std::wstring url_end = L"。！？；，：”’\"'）】》〉」』";
  const std::map<std::wstring, std::wstring> pairs{{L"“",L"”"},{L"‘",L"’"},{L"「",L"」"},{L"『",L"』"},
      {L"《",L"》"},{L"〈",L"〉"},{L"\"",L"\""},{L"'",L"'"}};
  const auto member = [](const std::wstring& set, const std::wstring& c) { return c.size() == 1 && set.find(c) != std::wstring::npos; };
  const auto space = [](const std::wstring& c) { return c.size() == 1 && iswspace(c[0]); };
  const auto digit = [](const std::wstring& c) { return c.size() == 1 && iswdigit(c[0]); };
  const auto latin = [](const std::wstring& c) { return c.size() == 1 && ((c[0] >= L'0' && c[0] <= L'9') ||
      (c[0] >= L'A' && c[0] <= L'Z') || (c[0] >= L'a' && c[0] <= L'z')); };
  std::vector<SemanticFragment> result;
  std::wstring current, token; std::vector<std::wstring> quotes;
  unsigned count = 0, words = 0, code = 0; bool in_word = false, pending = false, url = false;
  const auto flush = [&] {
    if (current.empty()) return;
    if (std::all_of(current.begin(), current.end(), [](wchar_t c) { return iswspace(c); })) {
      if (result.empty()) return;
      result.back().text += Utf8(current);
    } else result.push_back({result.size(), Utf8(current)});
    current.clear(); token.clear(); count = words = 0; in_word = pending = url = false;
  };
  for (std::size_t i = 0; i < chars.size(); ++i) {
    const auto& c = chars[i]; const auto previous = i ? chars[i-1] : L"";
    const auto next = i+1 < chars.size() ? chars[i+1] : L"";
    if (c == L"`") {
      unsigned run = 1; while (i+run < chars.size() && chars[i+run] == L"`") ++run;
      const bool protect = code || !quotes.empty() || url;
      if ((pending || count >= 48) && !protect) flush();
      current.append(run, L'`'); count += run; token.clear(); url = in_word = false;
      if (!code) code = run; else if (code == run) code = 0;
      i += run-1; continue;
    }
    const bool terminates_url = url && (space(c) || member(url_end,c));
    if (terminates_url) { url = false; token.clear(); }
    const bool protect = code || !quotes.empty() || url;
    if (pending && !space(c) && !member(closings,c) && !protect) flush();
    const bool continues = latin(c) || ((c == L"'" || c == L"’" || c == L"-") && latin(previous) && latin(next));
    if (count >= (protect ? 160U : 48U) && !member(boundaries,c) && !member(closings,c) && !terminates_url &&
        (!url || count >= 160) && (!(in_word && continues) || count >= 160)) flush();
    if (latin(c) && !in_word) ++words;
    in_word = continues; current += c; ++count;
    if (space(c)) { token.clear(); url = in_word = false; }
    else {
      token += c;
      std::wstring lower = token; std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
      if (lower.find(L"https://") != std::wstring::npos || lower.find(L"http://") != std::wstring::npos || lower.find(L"www.") != std::wstring::npos) url = true;
    }
    if (!code && !url) {
      if (!quotes.empty() && quotes.back() == c) quotes.pop_back();
      else if (pairs.contains(c) && !((c == L"'" || c == L"’") && !previous.empty() && iswalnum(previous[0]))) quotes.push_back(pairs.at(c));
    }
    bool boundary = member(boundaries,c);
    auto lexical = token.substr(0, token.size() >= c.size() ? token.size()-c.size() : 0);
    std::transform(lexical.begin(), lexical.end(), lexical.begin(), towlower);
    if ((c == L"," || c == L"." || c == L"，" || c == L"：" || c == L":") && digit(previous) && digit(next)) boundary = false;
    if (c == L".") {
      static const std::set<std::wstring> abbreviations{L"dr",L"mr",L"mrs",L"ms",L"prof",L"sr",L"jr",L"st",L"vs"};
      if ((!next.empty() && !space(next) && !member(closings,next)) || abbreviations.contains(lexical) ||
          std::all_of(lexical.begin(), lexical.end(), [](wchar_t ch) { return iswdigit(ch); })) boundary = false;
    }
    if (c == L":" && (lexical.ends_with(L"http") || lexical.ends_with(L"https")) && next == L"/") boundary = false;
    if (c == L"\n") flush();
    else if (!protect && boundary) pending = true;
    else if (pending && space(c)) flush();
    else if (space(c) && words >= 4 && !code && quotes.empty() && !url) flush();
  }
  flush();
  if (result.empty() && !current.empty()) result.push_back({0, Utf8(current)});
  if (result.size() <= 200) return result;
  std::vector<SemanticFragment> compact;
  for (std::size_t cursor = 0; cursor < result.size();) {
    const auto take = (result.size()-cursor + (200-compact.size())-1) / (200-compact.size());
    SemanticFragment group{result[cursor].index, {}};
    for (std::size_t n=0; n<take; ++n) group.text += result[cursor++].text;
    compact.push_back(std::move(group));
  }
  return compact;
}
}
