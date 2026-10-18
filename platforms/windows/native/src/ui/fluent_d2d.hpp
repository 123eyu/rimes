#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include "fluent_icons.hpp"

namespace rimes::windows::ui {
// Use the exact embedded Fluent font selected by GDI. A private memory font
// is not in DirectWrite's system collection; GDI interop keeps both renderers
// on the same upstream outlines without installing a font on the machine.
inline IDWriteFontFace* FluentFontFace(IDWriteFactory* factory) {
  struct Face {
    IDWriteFontFace* value = nullptr;
    explicit Face(IDWriteFactory* write) {
      if (!write || !EnsureFluentIcons()) return;
      IDWriteGdiInterop* interop = nullptr;
      if (FAILED(write->GetGdiInterop(&interop))) return;
      const auto dc = CreateCompatibleDC(nullptr);
      const auto font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
          DEFAULT_PITCH, L"FluentSystemIcons-Regular");
      if (dc && font) {
        const auto old = SelectObject(dc, font);
        interop->CreateFontFaceFromHdc(dc, &value);
        SelectObject(dc, old);
      }
      if (font) DeleteObject(font);
      if (dc) DeleteDC(dc);
      interop->Release();
    }
    ~Face() { if (value) value->Release(); }
  };
  static Face face(factory);
  return face.value;
}

inline bool DrawFluentIcon(ID2D1RenderTarget* target, IDWriteFactory* write,
                           ID2D1SolidColorBrush* brush, FluentIcon icon,
                           D2D1_RECT_F box, D2D1_COLOR_F color) {
  if (!target || !write || !brush) return false;
  auto* face = FluentFontFace(write);
  if (!face) return false;
  const UINT32 character = static_cast<UINT32>(icon);
  UINT16 glyph = 0;
  if (FAILED(face->GetGlyphIndices(&character, 1, &glyph)) || !glyph) return false;
  DWRITE_FONT_METRICS metrics{};
  face->GetMetrics(&metrics);
  DWRITE_GLYPH_METRICS advance{};
  if (FAILED(face->GetDesignGlyphMetrics(&glyph, 1, &advance))) return false;
  const float size = (std::min)(20.f, (std::min)(box.right - box.left, box.bottom - box.top));
  if (size <= 0 || !metrics.designUnitsPerEm) return false;
  const float scale = size / metrics.designUnitsPerEm;
  const float width = advance.advanceWidth * scale;
  const float height = (metrics.ascent + metrics.descent) * scale;
  const D2D1_POINT_2F baseline{(box.left + box.right - width) * .5f,
      (box.top + box.bottom - height) * .5f + metrics.ascent * scale};
  const DWRITE_GLYPH_RUN run{face, size, 1, &glyph, &width, nullptr, FALSE, 0};
  brush->SetColor(color);
  target->DrawGlyphRun(baseline, &run, brush, DWRITE_MEASURING_MODE_NATURAL);
  return true;
}
}  // namespace rimes::windows::ui
