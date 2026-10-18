#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "candidate_strip.hpp"
#include "theme.hpp"

namespace rimes::windows::ui {

struct BufferMetrics {
  int default_width_dip = 760;
  int min_width_dip = 520;
  int max_width_dip = 1100;
  int ordinary_height_dip = 73;
  int two_rail_height_dip = 105;
  int toolbar_only_height_dip = 35;
  int toolbar_height_dip = 30;
  int chrome_inset_dip = 2;
  int chrome_radius_dip = 11;
  int rail_radius_dip = 6;
  int source_rail_height_dip = 32;
  int rail_spacing_dip = 4;
  int icon_size_dip = 22;
  int icon_gap_dip = 4;
  int block_spacing_dip = 5;
  int rail_inset_dip = 5;
  int action_clearance_dip = 8;
  int body_font_dip = 12;
  int preedit_font_dip = 13;
  int rail_label_font_dip = 10;
};

enum class BufferMode { kInput = 0, kGenerate = 1, kTranslate = 2 };

struct BufferBlockView {
  std::wstring text;
  bool streaming = false;
  float countdown = 0;
};

struct BufferPaintState {
  ThemeId theme = ThemeId::kNight;
  BufferMode mode = BufferMode::kInput;
  std::wstring plugin_name;
  std::wstring live_metrics = L"— 字/分  ·  码长 —  ·  击键 —/秒";
  bool show_ai_choices = false, ai_choices_enabled = true;
  std::wstring ai_model, ai_effort;
  bool bound = false;
  bool capturing = false;
  bool busy = false;
  bool translate = false;
  bool visible = true;
  // Reserved for an explicit fold feature; unused until implemented.
  bool folded = false;
  bool paste_enabled = true;
  bool copy_enabled = true;
  bool send_enabled = true;
  bool selected = false;
  std::size_t insertion = 0;
  unsigned preedit_caret = 0, auto_send_seconds = 0;
  bool close_after_send = false;
  bool translation_speech = false, translation_controls_enabled = true;
  std::wstring source_language = L"简体中文", target_language = L"英语";
  float spinner_phase = 0;
  float send_hold_progress = -1; // Negative means no active physical send gesture.
  std::wstring status;
  std::wstring result_hint;
  std::wstring preedit;
  std::wstring preview, reasoning_summary;
  std::wstring empty_hint = L"等待输入";
  std::vector<BufferBlockView> source_blocks;
  std::vector<BufferBlockView> result_blocks;
  float scroll_source = 0;
  float scroll_result = 0;
  // Hover/pressed control for icon affordances (-1 = none).
  int hover = -1;
  int pressed = -1;
};

enum class BufferHitKind {
  kNone = 0,
  kMode,
  kClose,
  kPaste,
  kCopy,
  kSend,
  kBind,
  kConfigure, kEdit, kAutoSend, kCloseAfter,
  kSourceLanguage, kSwapLanguages, kTargetLanguage, kSpeech, kAIModel, kAIEffort, kReasoning,
};

struct BufferLayout {
  float width_dip = 680;
  float height_dip = 73;
  DipRect chrome{};
  DipRect toolbar{};
  DipRect divider{};
  DipRect mode_button{};
  DipRect mode_chip{};
  DipRect close_button{};
  DipRect bind_label{};
  DipRect status_label{};
  DipRect source_rail{};
  DipRect result_rail{};
  DipRect source_text{};
  DipRect result_text{};
  DipRect paste{};
  DipRect copy{};
  DipRect send{};
  DipRect waiting{};
  DipRect configure{}, edit{}, auto_send{}, close_after{}, association{};
  DipRect source_language{}, swap_languages{}, target_language{}, speech{};
  DipRect ai_model{}, ai_effort{}, live_metrics{}, reasoning{};
  bool show_reasoning = false;
  bool show_ai_choices = false, show_live_metrics = false;
  bool show_languages = false, show_auto_send = false;
  bool show_source = true, show_edit = false;
  bool show_result = false;
  bool show_paste = false;
  bool show_copy = false;
  bool show_send = false;
  bool toolbar_only = false;
};

[[nodiscard]] inline bool BufferNeedsResultRail(
    const BufferPaintState& state) noexcept {
  return (state.mode == BufferMode::kTranslate && !state.source_blocks.empty()) || state.busy ||
         !state.result_blocks.empty() ||
         !state.preview.empty();
}

[[nodiscard]] inline float BufferContentWidth(
    const std::vector<BufferBlockView>& blocks, const std::wstring& tail,
    float body_font_dip, float spacing) noexcept {
  float width = 0;
  auto add = [&](const std::wstring& text) {
    if (text.empty()) return;
    // Approximate glyph advance for layout/scroll clamps (paint measures exactly).
    width += static_cast<float>(text.size()) * body_font_dip * 0.92f + spacing;
  };
  for (const auto& block : blocks) add(block.text);
  add(tail);
  return width;
}

[[nodiscard]] inline float ClampScroll(float scroll, float content_width,
                                       float visible_width) noexcept {
  const float max_scroll = (std::max)(0.0f, content_width - visible_width);
  if (scroll < 0.0f) return 0.0f;
  if (scroll > max_scroll) return max_scroll;
  return scroll;
}

[[nodiscard]] inline BufferLayout LayoutBuffer(
    const BufferPaintState& state, float width_dip,
    const BufferMetrics& metrics = {}) {
  BufferLayout layout;
  width_dip = (std::clamp)(width_dip, static_cast<float>(metrics.min_width_dip),
                           static_cast<float>(metrics.max_width_dip));
  layout.width_dip = width_dip;
  const float inset = static_cast<float>(metrics.chrome_inset_dip);
  const float toolbar_h = static_cast<float>(metrics.toolbar_height_dip);
  const float preferred_rail_h = static_cast<float>(metrics.source_rail_height_dip);
  const float icon = static_cast<float>(metrics.icon_size_dip);
  const float gap = static_cast<float>(metrics.icon_gap_dip);
  const float rail_inset = static_cast<float>(metrics.rail_inset_dip);
  const float clearance = static_cast<float>(metrics.action_clearance_dip);

  const bool show_result = BufferNeedsResultRail(state);
  layout.show_result = show_result;
  layout.show_source = !(show_result && (state.mode == BufferMode::kGenerate ||
                                        state.source_blocks.empty()));
  layout.show_edit = state.mode == BufferMode::kGenerate && !state.result_blocks.empty();
  const bool two_rails = show_result && layout.show_source;
  layout.toolbar_only = state.folded;

  if (state.folded) {
    layout.show_source = layout.show_result = layout.show_edit = false;
    layout.height_dip = static_cast<float>(metrics.toolbar_only_height_dip);
  } else if (two_rails) {
    // Generation, waiting, translation, and any result preview need two rails.
    layout.height_dip = static_cast<float>(metrics.two_rail_height_dip);
  } else {
    // Open Buffer with empty or source-only content stays at ordinary height
    // so the source rail (and empty hint) remain visible.
    layout.height_dip = static_cast<float>(metrics.ordinary_height_dip);
  }

  layout.show_live_metrics = state.mode == BufferMode::kInput && !state.folded;
  layout.show_reasoning = state.busy && !state.folded && !state.reasoning_summary.empty();
  if (layout.show_live_metrics || layout.show_reasoning) layout.height_dip += 16;
  layout.chrome = {inset, inset, width_dip - inset, layout.height_dip - inset};
  layout.toolbar = {inset, inset, width_dip - inset, inset + toolbar_h};
  // Divider is one DIP at layout time; paint strokes 1 physical pixel.
  layout.divider = {inset, layout.toolbar.bottom, width_dip - inset,
                    layout.toolbar.bottom + 1.0f};

  layout.mode_button = {layout.toolbar.left + 6.0f,
                        layout.toolbar.top + (toolbar_h - icon) * 0.5f,
                        layout.toolbar.left + 6.0f + icon,
                        layout.toolbar.top + (toolbar_h - icon) * 0.5f + icon};
  layout.close_button = {layout.toolbar.right - 6.0f - icon,
                         layout.mode_button.top, layout.toolbar.right - 6.0f,
                         layout.mode_button.bottom};
  layout.mode_chip = {layout.mode_button.right + 6.0f, layout.toolbar.top + 6.0f,
                      layout.mode_button.right + 78.0f,
                      layout.toolbar.bottom - 6.0f};
  layout.configure = {layout.mode_chip.right + gap, layout.mode_button.top,
                       layout.mode_chip.right + gap + icon, layout.mode_button.bottom};
  layout.edit = {layout.configure.right + gap, layout.mode_button.top,
                  layout.configure.right + gap + icon, layout.mode_button.bottom};
  float controls_right = layout.show_edit ? layout.edit.right : layout.configure.right;
  layout.show_languages = state.mode == BufferMode::kTranslate;
  if (layout.show_languages) {
    const float language_width = width_dip < 620 ? 60.0f : 86.0f;
    layout.source_language = {controls_right + 6, layout.mode_chip.top,
                              controls_right + 6 + language_width, layout.mode_chip.bottom};
    layout.swap_languages = {layout.source_language.right + 6, layout.mode_button.top + 2,
                              layout.source_language.right + 24, layout.mode_button.bottom - 2};
    layout.target_language = {layout.swap_languages.right + 6, layout.mode_chip.top,
                              layout.swap_languages.right + 6 + language_width, layout.mode_chip.bottom};
    layout.speech = {layout.target_language.right + 6, layout.mode_button.top + 2,
                     layout.target_language.right + 26, layout.mode_button.bottom - 2};
    controls_right = layout.speech.right;
  }
  layout.show_ai_choices = state.mode == BufferMode::kGenerate && state.show_ai_choices;
  if (layout.show_ai_choices) {
    layout.ai_model = {controls_right + gap, layout.mode_chip.top,
                       controls_right + gap + (width_dip < 620 ? 104.f : 138.f), layout.mode_chip.bottom};
    layout.ai_effort = {layout.ai_model.right + gap, layout.mode_chip.top,
                        layout.ai_model.right + gap + 72, layout.mode_chip.bottom};
    controls_right = layout.ai_effort.right;
  }
  layout.association = {layout.close_button.left - gap - icon, layout.mode_button.top,
                         layout.close_button.left - gap, layout.mode_button.bottom};
  float trailing_left = layout.association.left;
  layout.show_auto_send = state.mode == BufferMode::kInput;
  if (layout.show_auto_send) {
    layout.close_after = {trailing_left - gap - 26, layout.mode_button.top,
                          trailing_left - gap, layout.mode_button.bottom};
    layout.auto_send = {layout.close_after.left - gap - icon, layout.mode_button.top,
                        layout.close_after.left - gap, layout.mode_button.bottom};
    trailing_left = layout.auto_send.left;
  }
  layout.bind_label = {controls_right + 8.0f, layout.toolbar.top + 8.0f,
                       (std::max)(controls_right + 8.0f, trailing_left - 8.0f),
                       layout.toolbar.bottom - 8.0f};
  layout.status_label = layout.bind_label;

  if (layout.toolbar_only) {
    // No rail hit targets while folded.
    return layout;
  }

  // Current macOS source: 30pt utility shelf, 38/70pt main bar; the 32/64pt
  // inline view is centered in it. Translation adds 5pt inside that view.
  const float vertical_inset = two_rails ? 8.0f : 3.0f;
  const float rail_top = layout.divider.bottom + vertical_inset;
  const float horizontal_inset = two_rails ? 10.0f : 5.0f;
  const float rail_left = inset + horizontal_inset;
  const float rail_right = width_dip - inset - horizontal_inset;
  const float rail_gap = static_cast<float>(metrics.rail_spacing_dip);
  // Both rows must clear the inner chrome, including its bottom stroke.
  const float main_bottom = layout.chrome.bottom - ((layout.show_live_metrics || layout.show_reasoning) ? 16 : 0);
  if (layout.show_live_metrics)
    layout.live_metrics = {rail_left + 9, main_bottom, rail_right - 5, layout.chrome.bottom};
  if (layout.show_reasoning) layout.reasoning = {rail_left + 9, main_bottom, rail_right - 5, layout.chrome.bottom};
  const float available = main_bottom - vertical_inset - rail_top;
  const float rail_h = (std::min)(preferred_rail_h,
      two_rails ? (available - rail_gap) * 0.5f : available);
  if (layout.show_source)
    layout.source_rail = {rail_left, rail_top, rail_right, rail_top + rail_h};
  if (show_result) {
    const float top = two_rails ? layout.source_rail.bottom + rail_gap : rail_top;
    layout.result_rail = {rail_left, top, rail_right, top + rail_h};
  }

  layout.show_paste = true;
  layout.show_copy = show_result || state.selected || !state.bound;
  layout.show_send = true;
  // Mac's paste/copy/send cluster overlays the primary (result when present)
  // rail. Hidden copy has no empty slot; neither row's outer width changes.
  const DipRect& primary = show_result ? layout.result_rail : layout.source_rail;
  const float action_y = primary.top + (rail_h - icon) * 0.5f;
  layout.send = {primary.right - icon - rail_inset, action_y,
                 primary.right - rail_inset, action_y + icon};
  float next_right = layout.send.left - gap;
  if (layout.show_copy) {
    layout.copy = {next_right - icon, action_y, next_right, action_y + icon};
    next_right = layout.copy.left - gap;
  }
  layout.paste = {next_right - icon, action_y, next_right, action_y + icon};
  if (state.busy) layout.waiting = layout.send;
  const float text_right = layout.paste.left - clearance;
  if (layout.show_source) layout.source_text = {layout.source_rail.left + rail_inset,
                        layout.source_rail.top,
                        show_result ? layout.source_rail.right - rail_inset
                                    : text_right,
                        layout.source_rail.bottom};
  if (show_result) {
    layout.result_text = {layout.result_rail.left + rail_inset,
                          layout.result_rail.top, text_right,
                          layout.result_rail.bottom};
  }

  return layout;
}

[[nodiscard]] inline bool RectInside(const DipRect& inner,
                                     const DipRect& outer) noexcept {
  if (inner.width() <= 0.0f || inner.height() <= 0.0f) return true;
  return inner.left >= outer.left - 0.01f && inner.right <= outer.right + 0.01f &&
         inner.top >= outer.top - 0.01f && inner.bottom <= outer.bottom + 0.01f;
}

[[nodiscard]] inline bool BufferLayoutFullyContained(
    const BufferLayout& layout) noexcept {
  const DipRect bounds{0, 0, layout.width_dip, layout.height_dip};
  if (!RectInside(layout.chrome, bounds)) return false;
  if (!RectInside(layout.toolbar, bounds)) return false;
  if (!RectInside(layout.mode_button, bounds)) return false;
  if (!RectInside(layout.close_button, bounds)) return false;
  for (const auto& control : {layout.configure, layout.auto_send, layout.close_after,
                             layout.source_language, layout.swap_languages, layout.target_language, layout.speech, layout.ai_model, layout.ai_effort})
    if (!RectInside(control, layout.toolbar)) return false;
  if (layout.close_after.right > layout.association.left) return false;
  if (layout.show_ai_choices && layout.ai_effort.right > layout.association.left) return false;
  if (layout.show_live_metrics && !RectInside(layout.live_metrics, layout.chrome)) return false;
  if (layout.show_reasoning && !RectInside(layout.reasoning, layout.chrome)) return false;
  if (layout.toolbar_only) {
    // Folded: rail/action hit targets must be empty / off-window.
    return layout.source_rail.height() <= 0.0f &&
           layout.result_rail.height() <= 0.0f &&
           layout.paste.height() <= 0.0f && layout.copy.height() <= 0.0f &&
           layout.send.height() <= 0.0f;
  }
  if (!RectInside(layout.source_rail, layout.chrome)) return false;
  if (!RectInside(layout.source_text, bounds)) return false;
  if (layout.show_paste && !RectInside(layout.paste, bounds)) return false;
  if (layout.show_result) {
    if (!RectInside(layout.result_rail, layout.chrome)) return false;
    if (!RectInside(layout.result_text, bounds)) return false;
    if (layout.show_copy && !RectInside(layout.copy, bounds)) return false;
    if (layout.show_send && !RectInside(layout.send, bounds)) return false;
  } else {
    if (layout.show_copy && !RectInside(layout.copy, bounds)) return false;
    if (layout.show_send && !RectInside(layout.send, bounds)) return false;
  }
  return true;
}

[[nodiscard]] inline BufferHitKind HitTestBuffer(const BufferLayout& layout,
                                                 float x, float y) noexcept {
  if (layout.show_reasoning && layout.reasoning.contains(x, y)) return BufferHitKind::kReasoning;
  if (layout.mode_button.contains(x, y) || layout.mode_chip.contains(x, y))
    return BufferHitKind::kMode;
  if (layout.show_ai_choices && layout.ai_model.contains(x, y)) return BufferHitKind::kAIModel;
  if (layout.show_ai_choices && layout.ai_effort.contains(x, y)) return BufferHitKind::kAIEffort;
  if (layout.configure.contains(x, y)) return BufferHitKind::kConfigure;
  if (layout.show_edit && layout.edit.contains(x, y)) return BufferHitKind::kEdit;
  if (layout.auto_send.contains(x, y)) return BufferHitKind::kAutoSend;
  if (layout.close_after.contains(x, y)) return BufferHitKind::kCloseAfter;
  if (layout.show_languages) {
    if (layout.source_language.contains(x, y)) return BufferHitKind::kSourceLanguage;
    if (layout.swap_languages.contains(x, y)) return BufferHitKind::kSwapLanguages;
    if (layout.target_language.contains(x, y)) return BufferHitKind::kTargetLanguage;
    if (layout.speech.contains(x, y)) return BufferHitKind::kSpeech;
  }
  if (layout.close_button.contains(x, y)) return BufferHitKind::kClose;
  if (layout.show_paste && layout.paste.contains(x, y))
    return BufferHitKind::kPaste;
  if (layout.show_copy && layout.copy.contains(x, y))
    return BufferHitKind::kCopy;
  if (layout.show_send && layout.send.contains(x, y))
    return BufferHitKind::kSend;
  // Only the source rail establishes capture. Result/status surfaces are
  // passive, and the action overlay keeps priority over the rail below it.
  if (!layout.toolbar_only && layout.show_source && layout.source_rail.contains(x, y))
    return BufferHitKind::kBind;
  return BufferHitKind::kNone;
}

[[nodiscard]] inline bool BufferHitIsCaptionExcluded(
    BufferHitKind hit) noexcept {
  return hit != BufferHitKind::kNone;
}

}  // namespace rimes::windows::ui
