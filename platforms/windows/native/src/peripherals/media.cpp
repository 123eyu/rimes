#include "media.hpp"

#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace rimes::windows::peripherals {
using Microsoft::WRL::ComPtr;
bool InteractiveDesktop() {
  auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!desktop) return false;
  wchar_t name[128]{}; DWORD size = 0;
  const bool available = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &size) && _wcsicmp(name, L"Default") == 0;
  CloseDesktop(desktop); return available;
}
ClipboardContent ReadClipboard(HWND owner) {
  ClipboardContent result;
  if (!OpenClipboard(owner)) return result;
  result.opened = true;
  struct Close {
    ~Close() { CloseClipboard(); }
  } close;
  if (IsClipboardFormatAvailable(RegisterClipboardFormatW(
          L"ExcludeClipboardContentFromMonitorProcessing")))
    return result;
  const auto copy = [](UINT format, std::size_t limit) {
    std::string bytes;
    if (auto handle = GetClipboardData(format)) {
      const auto size = GlobalSize(handle);
      if (size && size <= limit)
        if (const auto* p = static_cast<const char*>(GlobalLock(handle))) {
          bytes.assign(p, size);
          GlobalUnlock(handle);
        }
    }
    return bytes;
  };
  for (auto name :
       {L"CanIncludeInClipboardHistory", L"CanUploadToCloudClipboard"}) {
    const auto policy = copy(RegisterClipboardFormatW(name), sizeof(DWORD));
    if (policy.size() == sizeof(DWORD) &&
        policy == std::string(sizeof(DWORD), '\0'))
      return result;
  }
  result.bytes = copy(RegisterClipboardFormatW(L"PNG"), 64 * 1024 * 1024);
  if (!result.bytes.empty()) {
    result.extension = ".png";
    return result;
  }
  auto dib = copy(CF_DIB, 64 * 1024 * 1024);
  if (dib.size() >= sizeof(BITMAPINFOHEADER)) {
    BITMAPINFOHEADER h{};
    memcpy(&h, dib.data(), sizeof(h));
    if (h.biSize >= sizeof(h) && h.biSize <= dib.size() && h.biWidth > 0 &&
        h.biWidth <= 16384 && h.biHeight && h.biHeight >= -16384 &&
        h.biHeight <= 16384 && h.biPlanes == 1 &&
        (h.biBitCount == 1 || h.biBitCount == 4 || h.biBitCount == 8 ||
         h.biBitCount == 16 || h.biBitCount == 24 || h.biBitCount == 32) &&
        (h.biCompression == BI_RGB || h.biCompression == BI_BITFIELDS)) {
      const std::size_t colors = h.biClrUsed         ? h.biClrUsed
                                 : h.biBitCount <= 8 ? (1U << h.biBitCount)
                                                     : 0;
      const auto offset =
          h.biSize + colors * sizeof(RGBQUAD) +
          (h.biCompression == BI_BITFIELDS && h.biSize == 40 ? 12 : 0);
      const auto stride =
          ((static_cast<std::uint64_t>(h.biWidth) * h.biBitCount + 31) / 32) *
          4;
      if (offset <= dib.size() &&
          stride * static_cast<std::uint64_t>(std::abs(h.biHeight)) <=
              dib.size() - offset) {
        BITMAPFILEHEADER file{};
        file.bfType = 0x4d42;
        file.bfSize = static_cast<DWORD>(sizeof(file) + dib.size());
        file.bfOffBits = static_cast<DWORD>(sizeof(file) + offset);
        result.bytes.assign(reinterpret_cast<const char*>(&file), sizeof(file));
        result.bytes += dib;
        result.extension = ".bmp";
        return result;
      }
    }
  }
  if (auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP))) {
    const auto count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
    if (count <= 16)
      for (UINT i = 0; i < count; ++i) {
        wchar_t path[32768]{};
        if (DragQueryFileW(drop, i, path, 32768))
          result.files.emplace_back(path);
      }
    return result;
  }
  const auto text = copy(CF_UNICODETEXT, 512 * 1024);
  if (text.size() % sizeof(wchar_t) == 0 && !text.empty()) {
    std::wstring value(text.size() / sizeof(wchar_t), L'\0');
    memcpy(value.data(), text.data(), text.size());
    const auto end = value.find(L'\0');
    if (end != std::wstring::npos) {
      value.resize(end);
      result.text = workbench::Utf8(value);
    }
  }
  return result;
}
HBITMAP Thumbnail(const std::filesystem::path& path, unsigned maximum) {
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICBitmapScaler> scaler;
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateDecoderFromFilename(
          path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
          &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)))
    return nullptr;
  UINT w = 0, h = 0;
  if (FAILED(frame->GetSize(&w, &h)) || !w || !h || w > 32768 || h > 32768 ||
      static_cast<std::uint64_t>(w) * h > 128 * 1024 * 1024)
    return nullptr;
  const double ratio =
      (std::min)(1.0, static_cast<double>(maximum) / (std::max)(w, h));
  w = (std::max)(1U, static_cast<unsigned>(w * ratio));
  h = (std::max)(1U, static_cast<unsigned>(h * ratio));
  if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
      FAILED(scaler->Initialize(frame.Get(), w, h,
                                WICBitmapInterpolationModeFant)) ||
      FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)))
    return nullptr;
  BITMAPINFO info{};
  info.bmiHeader = {sizeof(BITMAPINFOHEADER),
                    static_cast<LONG>(w),
                    -static_cast<LONG>(h),
                    1,
                    32,
                    BI_RGB};
  void* pixels = nullptr;
  auto bitmap =
      CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (bitmap && FAILED(converter->CopyPixels(nullptr, w * 4, w * h * 4,
                                             static_cast<BYTE*>(pixels)))) {
    DeleteObject(bitmap);
    return nullptr;
  }
  return bitmap;
}
}  // namespace rimes::windows::peripherals
