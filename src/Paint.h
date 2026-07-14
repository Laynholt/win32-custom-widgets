#pragma once

#include <wcw/Theme.h>

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <memory>
#include <string_view>

namespace wcw::paint {

class Buffer {
public:
    Buffer(HDC target, const RECT& bounds);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    HDC dc() const { return memory_; }
    explicit operator bool() const { return bitmap_ != nullptr; }

private:
    HDC target_{};
    HDC memory_{};
    HBITMAP bitmap_{};
    HGDIOBJ previous_{};
    RECT bounds_{};
};

unsigned Dpi(HWND window);
float ToPixels(float dip, unsigned dpi);
Gdiplus::Color GdiPlusColor(Color color);
void Clear(HDC dc, const RECT& bounds, Color color);
std::unique_ptr<Gdiplus::GraphicsPath> RoundedPath(const Gdiplus::RectF& bounds, float radius);
void Fill(Gdiplus::Graphics& graphics, const Gdiplus::RectF& bounds, float radius, Color color);
void Border(Gdiplus::Graphics& graphics, const Gdiplus::RectF& bounds, float radius, Color color,
            float width);
void Text(HDC dc, std::wstring_view text, RECT bounds, HFONT font, Color color,
          UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE);
void Icon(HDC dc, HICON icon, const RECT& bounds);
void Bitmap(HDC dc, HBITMAP bitmap, const RECT& bounds);

HFONT Font(const FontSpec& spec, unsigned dpi);
void ClearFontCache();

} // namespace wcw::paint
