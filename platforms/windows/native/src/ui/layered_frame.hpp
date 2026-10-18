#pragma once
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rimes::windows::ui {
// One premultiplied frame, including native icons, is presented atomically.
// Binary HWND regions must not clip the antialiased outer edge.
class LayeredFrame {
 public:
  LayeredFrame() = default;
  LayeredFrame(const LayeredFrame&) = delete;
  LayeredFrame& operator=(const LayeredFrame&) = delete;
  ~LayeredFrame() { Reset(); }
  bool Resize(int width, int height) {
    if (pixels_ && width == width_ && height == height_) return true;
    Reset();
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return false;
    dc_ = CreateCompatibleDC(nullptr);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS,
        reinterpret_cast<void**>(&pixels_), nullptr, 0);
    if (!dc_ || !bitmap_) { Reset(); return false; }
    old_ = SelectObject(dc_, bitmap_); width_ = width; height_ = height;
    return true;
  }
  HDC dc() const { return dc_; }
  HBITMAP bitmap() const { return bitmap_; }
  std::uint32_t* pixels() const { return pixels_; }
  void DrawIcon(HICON icon, RECT box) {
    DrawIconEx(dc_, box.left, box.top, icon, box.right - box.left,
        box.bottom - box.top, 0, nullptr, DI_NORMAL);
    Opaque(box);  // The target badge is wholly inside the opaque toolbar.
  }
  void Opaque(RECT rect) {
    GdiFlush();
    for (int y = (std::max)(0L, rect.top); y < (std::min)(LONG(height_), rect.bottom); ++y)
      for (int x = (std::max)(0L, rect.left); x < (std::min)(LONG(width_), rect.right); ++x)
        pixels_[y * width_ + x] |= 0xff000000U;
  }
  // GDI doesn't write alpha. Apply fractional pixel coverage to its completed
  // rounded surface, preserving the RGB interior and premultiplying the edge.
  void RoundAlpha(float radius) {
    GdiFlush();
    radius = (std::min)(radius, (std::min)(width_, height_) * .5f);
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) {
      const float dx = (std::max)((std::max)(radius - (x + .5f), x + .5f - (width_ - radius)), 0.f);
      const float dy = (std::max)((std::max)(radius - (y + .5f), y + .5f - (height_ - radius)), 0.f);
      const auto a = static_cast<unsigned>(std::lround(std::clamp(radius + .5f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f) * 255));
      auto& pixel = pixels_[y * width_ + x];
      pixel = (a << 24U) | ((((pixel >> 16U) & 255U) * a / 255U) << 16U) |
          ((((pixel >> 8U) & 255U) * a / 255U) << 8U) | ((pixel & 255U) * a / 255U);
    }
  }
  bool Present(HWND window) const {
    POINT source{}; SIZE size{width_, height_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    return UpdateLayeredWindow(window, nullptr, nullptr, &size, dc_, &source, 0, &blend, ULW_ALPHA) != FALSE;
  }
 private:
  void Reset() {
    if (old_) SelectObject(dc_, old_);
    if (bitmap_) DeleteObject(bitmap_);
    if (dc_) DeleteDC(dc_);
    dc_ = nullptr; bitmap_ = nullptr; old_ = nullptr; pixels_ = nullptr; width_ = height_ = 0;
  }
  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ old_ = nullptr;
  std::uint32_t* pixels_ = nullptr;
  int width_ = 0, height_ = 0;
};
}  // namespace rimes::windows::ui
