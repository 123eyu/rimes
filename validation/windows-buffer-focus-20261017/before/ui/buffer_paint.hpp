#pragma once

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "buffer_layout.hpp"
#include "icons.hpp"
#include "theme.hpp"

namespace rimes::windows::ui {

[[nodiscard]] inline D2D1_COLOR_F ColorF(std::uint32_t rgb,
                                         float a = 1.0f) noexcept {
  return D2D1::ColorF(Rf(rgb), Gf(rgb), Bf(rgb), a);
}

inline void DrawD2DIcon(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                        IconId id, const DipRect& box, std::uint32_t color,
                        float stroke = 1.25f) {
  if (!target || !brush || box.width() < 4.0f || box.height() < 4.0f) return;
  brush->SetColor(ColorF(color));
  const float cx = (box.left + box.right) * 0.5f;
  const float cy = (box.top + box.bottom) * 0.5f;
  const float s = (std::min)(box.width(), box.height()) * 0.5f - 2.0f;

  auto line = [&](float x0, float y0, float x1, float y1) {
    target->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush,
                     stroke);
  };
  auto round_rect = [&](float l, float t, float r, float b, float radius,
                        bool fill) {
    const auto rr =
        D2D1::RoundedRect(D2D1::RectF(l, t, r, b), radius, radius);
    if (fill)
      target->FillRoundedRectangle(rr, brush);
    else
      target->DrawRoundedRectangle(rr, brush, stroke);
  };

  // Match the Mac toolbar's compact semibold symbols with native vector ink.
  // These are independently drawn paths, not an SF Symbols font dependency.
  const float unit = (std::min)(box.width(), box.height()) / 22.0f;
  auto point = [&](float x, float y) {
    return D2D1::Point2F(cx + x * unit, cy + y * unit);
  };
  auto path = [&](auto draw, bool filled) {
    ID2D1Factory* factory = nullptr;
    target->GetFactory(&factory);
    ID2D1PathGeometry* geometry = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(factory->CreatePathGeometry(&geometry)) &&
        SUCCEEDED(geometry->Open(&sink))) {
      draw(sink);
      if (SUCCEEDED(sink->Close())) {
        if (filled) target->FillGeometry(geometry, brush);
        else target->DrawGeometry(geometry, brush, stroke);
      }
    }
    if (sink) sink->Release();
    if (geometry) geometry->Release();
    factory->Release();
  };

  switch (id) {
    case IconId::kSparkles: {
      // sparkles: one curved filled star and two smaller companions.
      auto star = [&](float x, float y, float r) {
        path([&](ID2D1GeometrySink* sink) {
          sink->BeginFigure(point(x, y - r), D2D1_FIGURE_BEGIN_FILLED);
          sink->AddBezier({point(x + r * .18f, y - r * .18f),
                           point(x + r * .18f, y - r * .18f), point(x + r, y)});
          sink->AddBezier({point(x + r * .18f, y + r * .18f),
                           point(x + r * .18f, y + r * .18f), point(x, y + r)});
          sink->AddBezier({point(x - r * .18f, y + r * .18f),
                           point(x - r * .18f, y + r * .18f), point(x - r, y)});
          sink->AddBezier({point(x - r * .18f, y - r * .18f),
                           point(x - r * .18f, y - r * .18f), point(x, y - r)});
          sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        }, true);
      };
      star(1.5f, 1.5f, 5.0f);
      star(-4.1f, -3.7f, 2.5f);
      star(3.5f, -5.0f, 1.4f);
      break;
    }
    case IconId::kGrid: {
      // Four rounded squares (Mac toolbar grid).
      const float cell = 4.6f * unit;
      const float gap = 2.4f * unit;
      const float x0 = cx - cell - gap * 0.5f;
      const float y0 = cy - cell - gap * 0.5f;
      round_rect(x0, y0, x0 + cell, y0 + cell, 1.6f, false);
      round_rect(x0 + cell + gap, y0, x0 + 2 * cell + gap, y0 + cell, 1.6f,
                 false);
      round_rect(x0, y0 + cell + gap, x0 + cell, y0 + 2 * cell + gap, 1.6f,
                 false);
      round_rect(x0 + cell + gap, y0 + cell + gap, x0 + 2 * cell + gap,
                 y0 + 2 * cell + gap, 1.6f, false);
      break;
    }
    case IconId::kPaste: {
      // arrow.down.doc: folded document with an incoming downward arrow.
      path([&](ID2D1GeometrySink* sink) {
        sink->BeginFigure(point(-3, -6), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(point(-4.5f, -6)); sink->AddLine(point(-4.5f, 6));
        sink->AddLine(point(4.5f, 6)); sink->AddLine(point(4.5f, -2.5f));
        sink->AddLine(point(1, -6));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(point(1, -5.5f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(point(1, -2.5f)); sink->AddLine(point(4, -2.5f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(point(-1.5f, -6.5f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(point(-1.5f, 1.5f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(point(-3.7f, -.8f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(point(-1.5f, 1.5f)); sink->AddLine(point(.7f, -.8f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
      }, false);
      break;
    }
    case IconId::kCopy: {
      // rectangle.portrait.and.arrow.forward: copy out and close.
      path([&](ID2D1GeometrySink* sink) {
        sink->BeginFigure(point(1.5f, -2.5f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(point(1.5f, -5.5f)); sink->AddLine(point(-5, -5.5f));
        sink->AddLine(point(-5, 5.5f)); sink->AddLine(point(1.5f, 5.5f));
        sink->AddLine(point(1.5f, 2.5f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(point(-1.5f, 0), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(point(6, 0));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(point(3.3f, -2.7f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(point(6, 0)); sink->AddLine(point(3.3f, 2.7f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
      }, false);
      break;
    }
    case IconId::kPlane: {
      // paperplane.fill: solid upper-right plane with a clear diagonal fold.
      path([&](ID2D1GeometrySink* sink) {
        sink->BeginFigure(point(-6.5f, -1), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(point(6.5f, -6.5f)); sink->AddLine(point(1, 6.5f));
        sink->AddLine(point(-1.2f, 1.3f)); sink->AddLine(point(3.8f, -3.8f));
        sink->AddLine(point(-2.5f, .1f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
      }, true);
      break;
    }
    case IconId::kClose:
      line(cx - s * 0.55f, cy - s * 0.55f, cx + s * 0.55f, cy + s * 0.55f);
      line(cx + s * 0.55f, cy - s * 0.55f, cx - s * 0.55f, cy + s * 0.55f);
      break;
    case IconId::kMore:
      for (int i = -1; i <= 1; ++i) {
        const float x = cx + static_cast<float>(i) * s * 0.45f;
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 1.6f, 1.6f),
                            brush);
      }
      break;
    case IconId::kChevron:
      line(cx - s * 0.45f, cy - s * 0.25f, cx, cy + s * 0.35f);
      line(cx, cy + s * 0.35f, cx + s * 0.45f, cy - s * 0.25f);
      break;
    case IconId::kSettings:
      target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.5f, s * 0.5f), brush, stroke);
      target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.18f, s * 0.18f), brush, stroke);
      for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * 0.785398f;
        line(cx + std::cos(angle) * s * 0.48f, cy + std::sin(angle) * s * 0.48f,
             cx + std::cos(angle) * s * 0.72f, cy + std::sin(angle) * s * 0.72f);
      }
      break;
    case IconId::kCheck:
      line(cx - s * 0.45f, cy + s * 0.05f, cx - s * 0.1f, cy + s * 0.45f);
      line(cx - s * 0.1f, cy + s * 0.45f, cx + s * 0.5f, cy - s * 0.4f);
      break;
    default:
      round_rect(box.left + 2, box.top + 2, box.right - 2, box.bottom - 2, 3.0f,
                 false);
      break;
  }
}

struct BufferPaintContext {
  ID2D1RenderTarget* target = nullptr;
  IDWriteFactory* write = nullptr;
  IDWriteTextFormat* body = nullptr;
  IDWriteTextFormat* label = nullptr;
  ID2D1SolidColorBrush* brush = nullptr;
  float dpi = 96.0f;
};

// Use the same DirectWrite metrics for horizontal scrolling and drawing.
// No wrapping: a long block remains one rail item and cannot hide its tail
// on an unpainted second line.
inline float MeasureBufferContent(IDWriteFactory* write,
                                  IDWriteTextFormat* body,
                                  const std::vector<BufferBlockView>& blocks,
                                  const std::wstring& tail,
                                  bool source_tail = true) {
  if (!write || !body) return 0;
  float width = 0;
  auto add = [&](const std::wstring& text, bool preedit) {
    if (text.empty()) return;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(write->CreateTextLayout(text.c_str(),
            static_cast<UINT32>(text.size()), body, 8000, 32, &layout)))
      return;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (preedit) layout->SetFontSize(13, {0, static_cast<UINT32>(text.size())});
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    width += metrics.widthIncludingTrailingWhitespace + 5.0f;
    layout->Release();
  };
  for (const auto& block : blocks) add(block.text, false);
  add(tail, source_tail);
  return width;
}
inline float MeasureBufferCaret(IDWriteFactory* write, IDWriteTextFormat* body,
                                const BufferPaintState& state) {
  const auto index = (std::min)(state.insertion, state.source_blocks.size());
  std::vector<BufferBlockView> prefix(state.source_blocks.begin(),
      state.source_blocks.begin() + static_cast<std::ptrdiff_t>(index));
  auto width = MeasureBufferContent(write, body, prefix, L"");
  if (!state.preedit.empty()) {
    const auto part = state.preedit.substr(0, state.preedit_caret);
    width += MeasureBufferContent(write, body, {}, part) - (part.empty() ? 0.0f : 5.0f);
  }
  return width;
}

inline void DrawBufferWorkbench(const BufferPaintContext& ctx,
                                const BufferPaintState& state,
                                const BufferLayout& layout) {
  auto* target = ctx.target;
  auto* write = ctx.write;
  auto* body = ctx.body;
  auto* label = ctx.label;
  auto* brush = ctx.brush;
  if (!target || !write || !body || !label || !brush) return;

  label->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

  // Never clear the caller's full render target; host clears its canvas.
  const ThemePalette& p = Palette(state.theme);
  const float hairline = 96.0f / (std::max)(ctx.dpi, 1.0f);

  brush->SetColor(ColorF(p.buffer));
  target->FillRoundedRectangle(
      D2D1::RoundedRect(
          D2D1::RectF(layout.chrome.left, layout.chrome.top, layout.chrome.right,
                      layout.chrome.bottom),
          11.0f, 11.0f),
      brush);
  brush->SetColor(ColorF(p.buffer_border));
  target->DrawRoundedRectangle(
      D2D1::RoundedRect(
          D2D1::RectF(layout.chrome.left, layout.chrome.top, layout.chrome.right,
                      layout.chrome.bottom),
          11.0f, 11.0f),
      brush, hairline);

  // Toolbar shares buffer background (not a bright secondary stripe).
  brush->SetColor(ColorF(p.buffer_divider));
  target->DrawLine(D2D1::Point2F(layout.divider.left, layout.divider.top),
                   D2D1::Point2F(layout.divider.right, layout.divider.top),
                   brush, hairline);

  auto icon_color = [&](BufferHitKind kind, bool enabled,
                        std::uint32_t normal) -> std::uint32_t {
    if (!enabled) return Blend(normal, p.buffer, 0.55f);
    if (state.pressed == static_cast<int>(kind))
      return Blend(normal, p.accent, 0.35f);
    if (state.hover == static_cast<int>(kind))
      return Blend(normal, p.text_primary, 0.25f);
    return normal;
  };

  auto draw_control_bg = [&](const DipRect& box, BufferHitKind kind,
                             bool enabled, std::uint32_t base) {
    const bool pressed = enabled && state.pressed == static_cast<int>(kind);
    const bool hovered = enabled && state.hover == static_cast<int>(kind);
    const float fill_alpha = !enabled ? 0.34f : (pressed ? 1.0f : 0.78f);
    std::uint32_t fill = Blend(base, p.surface_secondary, fill_alpha);
    if (hovered) fill = Blend(fill, p.text_primary, pressed ? 0.14f : 0.08f);
    brush->SetColor(ColorF(fill));
    const auto shape = D2D1::RoundedRect(
        D2D1::RectF(box.left + hairline * 0.5f, box.top + hairline * 0.5f,
                    box.right - hairline * 0.5f, box.bottom - hairline * 0.5f),
        6, 6);
    target->FillRoundedRectangle(shape, brush);
    brush->SetColor(ColorF(Blend(base,
        hovered ? p.border_strong : p.border, enabled ? 0.82f : 0.40f)));
    target->DrawRoundedRectangle(shape, brush, hairline);
  };

  draw_control_bg(layout.mode_button, BufferHitKind::kMode, true, p.buffer);
  DrawD2DIcon(target, brush, IconId::kGrid, layout.mode_button,
              icon_color(BufferHitKind::kMode, true, p.text_secondary));

  // The mode popup shares the persistent surface and disclosure separator.
  draw_control_bg(layout.mode_chip, BufferHitKind::kMode, true, p.buffer);
  brush->SetColor(ColorF(p.border));
  const float disclosure_x = layout.mode_chip.right - 20;
  target->DrawLine(D2D1::Point2F(disclosure_x, layout.mode_chip.top + 4),
                   D2D1::Point2F(disclosure_x, layout.mode_chip.bottom - 4),
                   brush, hairline);
  const wchar_t* mode_name = L"Default";
  if (state.mode == BufferMode::kGenerate) mode_name = L"生成";
  if (state.mode == BufferMode::kTranslate) mode_name = L"实时翻译";
  if (!state.plugin_name.empty()) mode_name = state.plugin_name.c_str();
  brush->SetColor(ColorF(p.text_primary));
  target->DrawTextW(mode_name, static_cast<UINT32>(wcslen(mode_name)), label,
                    D2D1::RectF(layout.mode_chip.left + 8, layout.mode_chip.top,
                                disclosure_x - 4,
                                layout.mode_chip.bottom),
                    brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
  DrawD2DIcon(target, brush, IconId::kChevron,
              {layout.mode_chip.right - 14, layout.mode_chip.top + 4,
               layout.mode_chip.right - 4, layout.mode_chip.bottom - 4},
              p.text_muted, 1.1f);

  draw_control_bg(layout.configure, BufferHitKind::kConfigure, true, p.buffer);
  DrawD2DIcon(target, brush, IconId::kSettings, layout.configure, p.text_secondary);
  if (layout.show_languages) {
    const bool enabled = state.translation_controls_enabled;
    auto language = [&](const DipRect& box, BufferHitKind hit, const std::wstring& title) {
      draw_control_bg(box, hit, enabled, p.buffer);
      brush->SetColor(ColorF(icon_color(hit, enabled, p.text_primary)));
      target->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), label,
          D2D1::RectF(box.left + 6, box.top, box.right - 16, box.bottom), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
      DrawD2DIcon(target, brush, IconId::kChevron,
          {box.right - 13, box.top + 4, box.right - 3, box.bottom - 4}, p.text_muted, 1.1f);
    };
    language(layout.source_language, BufferHitKind::kSourceLanguage, state.source_language);
    language(layout.target_language, BufferHitKind::kTargetLanguage, state.target_language);
    draw_control_bg(layout.swap_languages, BufferHitKind::kSwapLanguages, enabled, p.buffer);
    brush->SetColor(ColorF(icon_color(BufferHitKind::kSwapLanguages, enabled, p.text_secondary)));
    const auto b = layout.swap_languages;
    const float cy = (b.top + b.bottom) * .5f;
    target->DrawLine({b.left + 4, cy - 3}, {b.right - 4, cy - 3}, brush, 1.1f);
    target->DrawLine({b.right - 7, cy - 6}, {b.right - 4, cy - 3}, brush, 1.1f);
    target->DrawLine({b.left + 4, cy + 3}, {b.right - 4, cy + 3}, brush, 1.1f);
    target->DrawLine({b.left + 4, cy + 3}, {b.left + 7, cy + 6}, brush, 1.1f);
    draw_control_bg(layout.speech, BufferHitKind::kSpeech, true, p.buffer);
    brush->SetColor(ColorF(icon_color(BufferHitKind::kSpeech, true,
        state.translation_speech ? p.accent_text : p.text_secondary)));
    const auto s = layout.speech;
    const float sy = (s.top + s.bottom) * .5f;
    target->DrawRectangle({s.left + 3, sy - 2, s.left + 6, sy + 2}, brush, 1.1f);
    target->DrawLine({s.left + 6, sy - 2}, {s.left + 10, sy - 5}, brush, 1.1f);
    target->DrawLine({s.left + 10, sy - 5}, {s.left + 10, sy + 5}, brush, 1.1f);
    target->DrawLine({s.left + 10, sy + 5}, {s.left + 6, sy + 2}, brush, 1.1f);
    if (state.translation_speech) {
      target->DrawLine({s.left + 13, sy - 3}, {s.left + 15, sy}, brush, 1.1f);
      target->DrawLine({s.left + 15, sy}, {s.left + 13, sy + 3}, brush, 1.1f);
    } else target->DrawLine({s.left + 12, sy + 4}, {s.left + 17, sy - 4}, brush, 1.1f);
  }
  if (layout.show_edit) {
    draw_control_bg(layout.edit, BufferHitKind::kEdit, true, p.buffer);
    brush->SetColor(ColorF(p.text_secondary));
    const auto b = layout.edit;
    const float y = (b.top + b.bottom) * .5f;
    target->DrawLine({b.left + 6, y}, {b.right - 5, y}, brush, 1.4f);
    target->DrawLine({b.left + 6, y}, {b.left + 10, y - 4}, brush, 1.4f);
    target->DrawLine({b.left + 6, y}, {b.left + 10, y + 4}, brush, 1.4f);
  }
  if (layout.show_ai_choices) {
    const auto choice = [&](const DipRect& box, BufferHitKind hit, const std::wstring& title) {
      draw_control_bg(box, hit, state.ai_choices_enabled, p.buffer);
      brush->SetColor(ColorF(icon_color(hit, state.ai_choices_enabled, p.text_primary)));
      target->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), label,
          D2D1::RectF(box.left + 6, box.top, box.right - 16, box.bottom), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
      DrawD2DIcon(target, brush, IconId::kChevron,
          {box.right - 13, box.top + 4, box.right - 3, box.bottom - 4}, p.text_muted, 1.1f);
    };
    choice(layout.ai_model, BufferHitKind::kAIModel, state.ai_model);
    choice(layout.ai_effort, BufferHitKind::kAIEffort, state.ai_effort);
  }
  if (layout.show_auto_send) {
    draw_control_bg(layout.auto_send, BufferHitKind::kAutoSend, true, state.auto_send_seconds ? p.accent : p.buffer);
    brush->SetColor(ColorF(state.auto_send_seconds ? p.accent_text : p.text_secondary));
    const auto timer = state.auto_send_seconds ? std::to_wstring(state.auto_send_seconds) : L"N";
    label->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    target->DrawTextW(timer.c_str(), static_cast<UINT32>(timer.size()), label,
        D2D1::RectF(layout.auto_send.left, layout.auto_send.top, layout.auto_send.right, layout.auto_send.bottom), brush);
    label->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    draw_control_bg(layout.close_after, BufferHitKind::kCloseAfter, true, p.buffer);
    const auto box = layout.close_after;
    const float cy = (box.top + box.bottom) * .5f;
    brush->SetColor(ColorF(state.close_after_send ? p.accent : p.border_strong));
    target->FillRoundedRectangle(D2D1::RoundedRect({box.left, cy - 7.5f, box.right, cy + 7.5f}, 7.5f, 7.5f), brush);
    brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    const float cx = state.close_after_send ? box.right - 7.5f : box.left + 7.5f;
    target->FillEllipse(D2D1::Ellipse({cx, cy}, 5.5f, 5.5f), brush);
  }

  const bool status_in_result = layout.show_result && state.result_blocks.empty() &&
      state.preview.empty() && !state.result_hint.empty();
  if (!status_in_result && layout.status_label.width() >= 64 && (state.bound || !state.status.empty())) {
    const std::wstring status =
        !state.status.empty()
            ? state.status
            : (state.capturing ? L"已绑定" : L"已暂停");
    const bool danger =
        state.status.find(L"失败") != std::wstring::npos ||
        state.status.find(L"错误") != std::wstring::npos;
    brush->SetColor(ColorF(danger ? p.danger_text
                                  : (state.capturing ? p.accent_text
                                                     : p.warning_text)));
    target->DrawTextW(status.c_str(), static_cast<UINT32>(status.size()), label,
                      D2D1::RectF(layout.status_label.left,
                                  layout.status_label.top,
                                  layout.status_label.right,
                                  layout.status_label.bottom),
                      brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }

  draw_control_bg(layout.close_button, BufferHitKind::kClose, true, p.buffer);
  DrawD2DIcon(target, brush, IconId::kClose, layout.close_button,
              icon_color(BufferHitKind::kClose, true, p.text_secondary));

  if (layout.toolbar_only) return;
  if (layout.show_live_metrics || layout.show_reasoning) {
    brush->SetColor(ColorF(p.text_muted));
    const auto box = layout.show_reasoning ? layout.reasoning : layout.live_metrics;
    const auto text = layout.show_reasoning ? L"思考摘要 · " + state.reasoning_summary : state.live_metrics;
    target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), label,
        D2D1::RectF(box.left, box.top, box.right, box.bottom), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }

  auto paint_rail = [&](const DipRect& rail, const DipRect& text_box,
                        std::uint32_t fill,
                        const std::vector<BufferBlockView>& blocks,
                        const std::wstring& tail, bool result_lane,
                        float scroll) {
    brush->SetColor(ColorF(fill));
    target->FillRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(rail.left, rail.top, rail.right, rail.bottom), 6, 6),
        brush);
    target->PushAxisAlignedClip(
        D2D1::RectF(text_box.left, text_box.top, text_box.right, text_box.bottom),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float x = text_box.left - scroll;
    if (!result_lane && state.selected) {
      brush->SetColor(ColorF(p.buffer_chip_selected));
      target->FillRectangle(D2D1::RectF(text_box.left, text_box.top + 3,
          text_box.right, text_box.bottom - 3), brush);
    }
    auto paint_item = [&](const std::wstring& text, bool streaming,
                          bool preedit, float countdown = 0) {
      if (text.empty()) return;
      IDWriteTextLayout* layout_obj = nullptr;
      if (FAILED(write->CreateTextLayout(
              text.c_str(), static_cast<UINT32>(text.size()), body, 8000.0f,
              text_box.height(), &layout_obj)))
        return;
      layout_obj->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      layout_obj->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      if (preedit)
        layout_obj->SetFontSize(13, {0, static_cast<UINT32>(text.size())});
      DWRITE_TEXT_METRICS metrics{};
      layout_obj->GetMetrics(&metrics);
      const float w = metrics.widthIncludingTrailingWhitespace;
      brush->SetColor(ColorF(p.text_primary, 1 - .65f * (std::clamp)(countdown, 0.f, 1.f)));
      target->DrawTextLayout(D2D1::Point2F(x, text_box.top), layout_obj,
                             brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
      brush->SetColor(ColorF(streaming ? p.accent : p.border_strong));
      target->DrawLine(D2D1::Point2F(x, rail.bottom - 4.0f),
                       D2D1::Point2F(x + w, rail.bottom - 4.0f), brush,
                       hairline);
      layout_obj->Release();
      x += w + 5.0f;
    };
    const auto& hint = result_lane ? state.result_hint : state.empty_hint;
    if (blocks.empty() && tail.empty() && !hint.empty()) {
      brush->SetColor(ColorF(result_lane && (hint.find(L"失败") != std::wstring::npos ||
                                           hint.find(L"错误") != std::wstring::npos) ? p.danger_text : p.text_muted));
      target->DrawTextW(hint.c_str(),
                        static_cast<UINT32>(hint.size()), label,
                        D2D1::RectF(text_box.left, text_box.top,
                                    text_box.right, text_box.bottom),
                        brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    } else {
      for (std::size_t i = 0; i <= blocks.size(); ++i) {
        if (!result_lane && i == (std::min)(state.insertion, blocks.size()) && !tail.empty())
          paint_item(tail, true, true);
        if (i < blocks.size()) paint_item(blocks[i].text, blocks[i].streaming, false, blocks[i].countdown);
      }
      if (result_lane && !tail.empty()) paint_item(tail, true, false);
    }
    if (!result_lane && state.capturing && !state.selected) {
      const auto caret_x = text_box.left - scroll + MeasureBufferCaret(write, body, state);
      brush->SetColor(ColorF(p.accent));
      target->DrawLine({caret_x, rail.top + 7}, {caret_x, rail.bottom - 7}, brush, 1.5f);
    }
    target->PopAxisAlignedClip();
  };

  if (layout.show_source) paint_rail(layout.source_rail, layout.source_text,
             layout.show_result ? p.buffer_source_rail : p.candidate,
             state.source_blocks, state.preedit, false, state.scroll_source);
  if (layout.show_result) {
    // Preview is drawn as the streaming tail only — callers must not also
    // append it into result_blocks.
    paint_rail(layout.result_rail, layout.result_text, p.buffer_target_rail,
               state.result_blocks, state.preview, true, state.scroll_result);
  }

  const auto action_surface = layout.show_result ? p.buffer_target_rail : p.candidate;
  if (layout.show_paste) {
    draw_control_bg(layout.paste, BufferHitKind::kPaste, state.paste_enabled, action_surface);
    DrawD2DIcon(target, brush, IconId::kPaste, layout.paste,
                icon_color(BufferHitKind::kPaste, state.paste_enabled,
                           p.text_secondary));
  }
  if (layout.show_copy) {
    draw_control_bg(layout.copy, BufferHitKind::kCopy, state.copy_enabled, action_surface);
    DrawD2DIcon(target, brush, IconId::kCopy, layout.copy,
                icon_color(BufferHitKind::kCopy, state.copy_enabled,
                           p.text_secondary));
  }
  if (layout.show_send) {
    draw_control_bg(layout.send, BufferHitKind::kSend, state.send_enabled, action_surface);
    // The primary action generates before a final exists; it only sends a
    // reviewed final afterwards. This is shared by API and local CLI routes.
    const auto primary = state.mode != BufferMode::kInput && state.result_blocks.empty()
        ? IconId::kSparkles : IconId::kPlane;
    if (!state.busy) DrawD2DIcon(target, brush, primary, layout.send,
                icon_color(BufferHitKind::kSend, state.send_enabled,
                           p.accent_text));
  }
  if (layout.show_result && state.busy) {
    // The same primary control owns generation, waiting and delivery.
    const float cx = (layout.waiting.left + layout.waiting.right) * 0.5f;
    const float cy = (layout.waiting.top + layout.waiting.bottom) * 0.5f;
    for (int i = 0; i < 8; ++i) {
      const float angle = state.spinner_phase + static_cast<float>(i) * 0.785398f;
      brush->SetColor(ColorF(p.accent, 0.2f + static_cast<float>(i) * 0.1f));
      target->DrawLine({cx + std::cos(angle) * 4, cy + std::sin(angle) * 4},
                      {cx + std::cos(angle) * 7, cy + std::sin(angle) * 7}, brush, 1.6f);
    }
  }
  // macOS BufferInlineView uses a two-point strip at the content's bottom.
  // It is an overlay, so progress never changes the rail or text geometry.
  if (state.send_hold_progress >= 0.0f) {
    const auto& rail = layout.show_result ? layout.result_rail : layout.source_rail;
    const float amount = (std::clamp)(state.send_hold_progress, 0.0f, 1.0f);
    brush->SetColor(ColorF(p.accent));
    target->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(rail.left, rail.bottom - 2, rail.left + rail.width() * amount, rail.bottom),
        1, 1), brush);
  }
}

// Compatibility wrapper used by older call sites.
inline void DrawBufferWorkbench(ID2D1RenderTarget* target, IDWriteFactory* write,
                                IDWriteTextFormat* body,
                                IDWriteTextFormat* label,
                                ID2D1SolidColorBrush* brush,
                                const BufferPaintState& state,
                                const BufferLayout& layout) {
  DrawBufferWorkbench(BufferPaintContext{target, write, body, label, brush, 96},
                      state, layout);
}

}  // namespace rimes::windows::ui
