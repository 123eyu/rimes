#include "../ui/buffer_layout.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Check(bool ok, const char* message) {
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

using rimes::windows::ui::BufferLayoutFullyContained;
using rimes::windows::ui::BufferMode;
using rimes::windows::ui::BufferPaintState;
using rimes::windows::ui::ClampScroll;
using rimes::windows::ui::HitTestBuffer;
using rimes::windows::ui::LayoutBuffer;
using rimes::windows::ui::RectInside;

BufferPaintState BaseState() {
  BufferPaintState state;
  state.visible = true;
  state.bound = true;
  state.capturing = true;
  return state;
}

void ExpectContained(const BufferPaintState& state, const char* label) {
  const auto layout = LayoutBuffer(state, 760);
  Check(BufferLayoutFullyContained(layout), label);
  Check(layout.height_dip <= 105.01f || state.folded, label);
}

void TestInputEmptyShowsRail() {
  auto state = BaseState();
  state.mode = BufferMode::kInput;
  const auto layout = LayoutBuffer(state, 760);
  Check(layout.height_dip == 89 && layout.show_live_metrics, "Default includes its permanent 16 DIP live metrics row");
  Check(layout.toolbar.height() == 30 && layout.source_rail.top == 36 && layout.source_rail.height() == 32,
        "current macOS shelf and centered standard rail geometry");
  Check(!layout.toolbar_only, "empty open buffer is not toolbar-only");
  Check(layout.source_rail.height() > 0, "empty open buffer shows source rail");
  Check(BufferLayoutFullyContained(layout), "empty input layout contained");
  Check(layout.show_paste && HitTestBuffer(layout, layout.paste.left + 1,
                                           layout.paste.top + 1) ==
                                 rimes::windows::ui::BufferHitKind::kPaste,
        "paste hit is inside window");
}

void TestGenerationReadyTwoRails() {
  auto state = BaseState();
  state.mode = BufferMode::kGenerate;
  state.source_blocks = {{L"Explain DIP", false}};
  state.result_blocks = {{L"DIP keeps layout stable", false}};
  const auto layout = LayoutBuffer(state, 760);
  Check(layout.height_dip == 73 && !layout.show_source && layout.show_edit,
        "generated answer replaces prompt on one rail with return-to-edit");
  Check(layout.show_result, "generation shows result rail");
  Check(layout.result_rail.bottom < layout.chrome.bottom,
        "result rail fits inside height");
  Check(BufferLayoutFullyContained(layout), "generation-ready contained");
  Check(layout.paste.top >= layout.result_rail.top,
        "paste joins copy and send on the primary result rail");
  Check(layout.copy.top >= layout.result_rail.top - 0.01f,
        "copy sits on result rail");
  Check(layout.send.top >= layout.result_rail.top - 0.01f,
        "send sits on result rail");
}

void TestGenerationWaiting() {
  auto state = BaseState();
  state.mode = BufferMode::kGenerate;
  state.busy = true;
  state.source_blocks = {{L"Summarize", false}};
  state.preview = L"Themes share…";
  const auto layout = LayoutBuffer(state, 760);
  Check(layout.height_dip == 73 && !layout.show_source, "waiting generation uses one result rail");
  Check(BufferLayoutFullyContained(layout), "waiting generation contained");
  Check(layout.waiting.bottom <= layout.height_dip + 0.01f,
        "waiting indicator inside bounds");
}

void TestTranslation() {
  auto state = BaseState();
  state.mode = BufferMode::kTranslate;
  state.translate = true;
  state.source_blocks = {{L"紧凑候选条", false}};
  state.result_blocks = {{L"compact candidate strip", false}};
  const auto layout = LayoutBuffer(state, 760);
  Check(layout.height_dip == 105, "translation uses 105 DIP");
  Check(layout.source_rail.top == 41 && layout.source_rail.height() == 25 &&
            layout.result_rail.top == 70 && layout.result_rail.height() == 25,
        "current macOS translation rails preserve 5pt internal vertical insets");
  Check(BufferLayoutFullyContained(layout), "translation contained");
  using Hit = rimes::windows::ui::BufferHitKind;
  for (const float width : {520.f, 680.f, 760.f, 1100.f}) {
    const auto compact = LayoutBuffer(state, width);
    Check(BufferLayoutFullyContained(compact) && compact.show_languages,
          "translation toolbar fits narrow and wide Buffer layouts");
    Check(compact.source_language.right < compact.swap_languages.left &&
          compact.swap_languages.right < compact.target_language.left &&
          compact.target_language.right < compact.speech.left &&
          compact.speech.right < compact.association.left && !compact.show_auto_send &&
          compact.auto_send.width() == 0 && compact.close_after.width() == 0 &&
          compact.close_after.right < compact.association.left,
          "language controls and send controls never overlap");
    Check(HitTestBuffer(compact, compact.swap_languages.left + 2, compact.swap_languages.top + 2) == Hit::kSwapLanguages,
          "swap control is clickable rather than window drag chrome");
    Check(HitTestBuffer(compact, compact.target_language.left + 2, compact.target_language.top + 2) == Hit::kTargetLanguage,
          "target language owns its complete hit area");
  }
  state.source_blocks.clear(); state.result_blocks.clear();
  const auto empty = LayoutBuffer(state, 680);
  Check(empty.show_source && !empty.show_result && empty.height_dip == 73,
        "empty translation exposes the editable source instead of an empty result-only rail");
}

void TestErrorStatus() {
  auto state = BaseState();
  state.source_blocks = {{L"Keep source", false}};
  state.status = L"API 失败，原文保留";
  const auto layout = LayoutBuffer(state, 760);
  Check(layout.height_dip == 89, "Default error preserves live metrics row");
  Check(BufferLayoutFullyContained(layout), "error layout contained");
}

void TestFoldedHasNoOffWindowHits() {
  auto state = BaseState();
  state.folded = true;
  const auto layout = LayoutBuffer(state, 760);
  Check(layout.toolbar_only, "folded is toolbar-only");
  Check(layout.height_dip == 35, "folded height 35");
  Check(BufferLayoutFullyContained(layout),
        "folded layout has no off-window rail hits");
}

void TestScrollClamp() {
  Check(ClampScroll(-10, 400, 200) == 0, "scroll floors at 0");
  Check(ClampScroll(500, 400, 200) == 200, "scroll caps at content-visible");
  Check(ClampScroll(50, 100, 200) == 0, "short content forces zero scroll");
}

void TestPrimaryActionCluster() {
  for (const float width : {520.0f, 760.0f, 1100.0f}) {
    auto state = BaseState();
    for (const auto mode : {BufferMode::kInput, BufferMode::kGenerate,
                            BufferMode::kTranslate}) {
      state.mode = mode;
      for (const bool busy : {false, true}) {
        state.busy = busy;
        const auto layout = LayoutBuffer(state, width);
        using Hit = rimes::windows::ui::BufferHitKind;
        if (layout.show_source) Check(HitTestBuffer(layout, layout.source_text.left + 1,
                            layout.source_text.top + 1) == Hit::kBind,
              "clicking source text explicitly binds capture in every mode");
        Check(HitTestBuffer(layout, layout.status_label.left + 1,
                            layout.status_label.top + 1) == Hit::kNone,
              "target status remains passive");
        Check(HitTestBuffer(layout, layout.paste.left + 1,
                            layout.paste.top + 1) == Hit::kPaste,
              "paste overlay cannot accidentally bind capture");
        Check(HitTestBuffer(layout, layout.send.left + 1,
                            layout.send.top + 1) == Hit::kSend,
              "send overlay cannot accidentally bind capture");
        if (layout.show_result)
          Check(HitTestBuffer(layout, layout.result_text.left + 1,
                              layout.result_text.top + 1) == Hit::kNone,
                "clicking generated results cannot silently retarget input");
        const auto& primary = layout.show_result ? layout.result_rail
                                                : layout.source_rail;
        Check(BufferLayoutFullyContained(layout), "every mode clears inner chrome");
        Check(RectInside(layout.paste, primary) && RectInside(layout.send, primary),
              "action rectangles remain within primary rail");
        if (layout.show_source) Check(layout.source_rail.left == primary.left &&
                  layout.source_rail.right == primary.right,
              "overlay never narrows either outer rail");
        Check(layout.show_copy ? layout.paste.right < layout.copy.left &&
                                  layout.copy.right < layout.send.left
                               : layout.send.left - layout.paste.right == 4,
              "paste copy send stay ordered without a hidden copy gap");
        const auto& text = layout.show_result ? layout.result_text : layout.source_text;
        Check(text.right < layout.paste.left, "text clears the primary action cluster");
        if (layout.show_result && busy)
          Check(text.right < layout.waiting.left && layout.waiting.left == layout.send.left,
                "waiting animation replaces the primary action");
      }
    }
  }
}

}  // namespace

int main() {
  TestInputEmptyShowsRail();
  TestGenerationReadyTwoRails();
  TestGenerationWaiting();
  TestTranslation();
  TestErrorStatus();
  TestFoldedHasNoOffWindowHits();
  TestScrollClamp();
  TestPrimaryActionCluster();
  for (const float width : {520.f, 760.f, 1100.f}) {
    auto state=BaseState(); state.mode=BufferMode::kInput;
    auto layout=LayoutBuffer(state,width);
    Check(layout.show_live_metrics && layout.auto_send.left > width-140 &&
        layout.close_after.width()==26 && layout.close_after.right < layout.association.left, "Default metrics and compact controls align right");
    state.mode=BufferMode::kGenerate; state.show_ai_choices=true; state.busy=true;
    state.reasoning_summary=L"正在整理公开信息"; state.result_blocks={{L"第一块",true},{L"第二块",true}};
    layout=LayoutBuffer(state,width);
    Check(BufferLayoutFullyContained(layout) && layout.show_reasoning &&
        layout.ai_model.right < layout.ai_effort.left && layout.ai_effort.right < layout.association.left,
        "ChatGPT controls and public summary fit narrow and wide windows");
  }
  ExpectContained(BaseState(), "default contained");
  if (g_failures) {
    std::cerr << g_failures << " buffer layout failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "Buffer layout containment tests passed\n";
  return EXIT_SUCCESS;
}
