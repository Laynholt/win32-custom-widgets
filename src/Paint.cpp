#include "Paint.h"

#include <wcw/Geometry.h>

#include <algorithm>
#include <map>
#include <string>
#include <tuple>

namespace wcw::paint {
namespace {

using FontKey = std::tuple<std::wstring, float, int, bool, unsigned>;
std::map<FontKey, HFONT> fonts;

} // namespace

Buffer::Buffer(HDC target, const RECT& bounds) : target_(target), bounds_(bounds) {
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (!target || width <= 0 || height <= 0) return;
    memory_ = CreateCompatibleDC(target);
    if (!memory_) return;
    bitmap_ = CreateCompatibleBitmap(target, width, height);
    if (!bitmap_) {
        DeleteDC(memory_);
        memory_ = nullptr;
        return;
    }
    previous_ = SelectObject(memory_, bitmap_);
    SetViewportOrgEx(memory_, -bounds.left, -bounds.top, nullptr);
}

Buffer::~Buffer() {
    if (!memory_) return;
    if (bitmap_) {
        BitBlt(target_, bounds_.left, bounds_.top, bounds_.right - bounds_.left,
               bounds_.bottom - bounds_.top, memory_, bounds_.left, bounds_.top, SRCCOPY);
        SelectObject(memory_, previous_);
        DeleteObject(bitmap_);
    }
    DeleteDC(memory_);
}

unsigned Dpi(HWND window) {
    return window ? GetDpiForWindow(window) : USER_DEFAULT_SCREEN_DPI;
}

float ToPixels(float dip, unsigned dpi) {
    return dip * static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI;
}

Gdiplus::RectF ToPixels(RectDip bounds, unsigned dpi) {
    return {ToPixels(bounds.x, dpi), ToPixels(bounds.y, dpi), ToPixels(bounds.width, dpi),
            ToPixels(bounds.height, dpi)};
}

Gdiplus::Color GdiPlusColor(Color color) {
    return {color.a, color.r, color.g, color.b};
}

void Clear(HDC dc, const RECT& bounds, Color color) {
    const auto brush = CreateSolidBrush(RGB(color.r, color.g, color.b));
    if (!brush) return;
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

std::unique_ptr<Gdiplus::GraphicsPath> RoundedPath(const Gdiplus::RectF& bounds, float radius) {
    auto path = std::make_unique<Gdiplus::GraphicsPath>();
    radius = std::clamp(radius, 0.0f, (std::min)(bounds.Width, bounds.Height) / 2.0f);
    if (radius == 0) {
        path->AddRectangle(bounds);
        return path;
    }
    const float diameter = radius * 2;
    path->AddArc(bounds.X, bounds.Y, diameter, diameter, 180, 90);
    path->AddArc(bounds.GetRight() - diameter, bounds.Y, diameter, diameter, 270, 90);
    path->AddArc(bounds.GetRight() - diameter, bounds.GetBottom() - diameter, diameter, diameter, 0,
                 90);
    path->AddArc(bounds.X, bounds.GetBottom() - diameter, diameter, diameter, 90, 90);
    path->CloseFigure();
    return path;
}

void Fill(Gdiplus::Graphics& graphics, const Gdiplus::RectF& bounds, float radius, Color color) {
    Gdiplus::SolidBrush brush(GdiPlusColor(color));
    const auto path = RoundedPath(bounds, radius);
    graphics.FillPath(&brush, path.get());
}

void Border(Gdiplus::Graphics& graphics, const Gdiplus::RectF& bounds, float radius, Color color,
            float width) {
    if (width <= 0) return;
    Gdiplus::Pen pen(GdiPlusColor(color), width);
    pen.SetAlignment(Gdiplus::PenAlignmentInset);
    auto outline = bounds;
    outline.Width = (std::max)(0.0f, outline.Width - 1.0f);
    outline.Height = (std::max)(0.0f, outline.Height - 1.0f);
    const auto path = RoundedPath(outline, radius);
    graphics.DrawPath(&pen, path.get());
}

void Focus(Gdiplus::Graphics& graphics, const Gdiplus::RectF& bounds, float radius, Color color,
           float width) {
    Border(graphics, bounds, radius, color, width);
}

void Text(HDC dc, std::wstring_view text, RECT bounds, HFONT font, Color color, UINT format) {
    const auto oldFont = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(color.r, color.g, color.b));
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &bounds, format);
    if (oldFont) SelectObject(dc, oldFont);
}

void Icon(HDC dc, HICON icon, const RECT& bounds) {
    if (!icon) return;
    DrawIconEx(dc, bounds.left, bounds.top, icon, bounds.right - bounds.left,
               bounds.bottom - bounds.top, 0, nullptr, DI_NORMAL);
}

void Bitmap(HDC dc, HBITMAP bitmap, const RECT& bounds) {
    if (!bitmap) return;
    BITMAP info{};
    if (!GetObjectW(bitmap, sizeof(info), &info)) return;
    const auto source = CreateCompatibleDC(dc);
    if (!source) return;
    const auto previous = SelectObject(source, bitmap);
    SetStretchBltMode(dc, HALFTONE);
    StretchBlt(dc, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
               source, 0, 0, info.bmWidth, info.bmHeight, SRCCOPY);
    SelectObject(source, previous);
    DeleteDC(source);
}

HFONT Font(const FontSpec& spec, unsigned dpi) {
    const FontKey key{spec.family, spec.sizeDip, spec.weight, spec.italic, dpi};
    if (const auto found = fonts.find(key); found != fonts.end()) return found->second;
    const auto font = CreateFontW(-DipToPx(spec.sizeDip, dpi), 0, 0, 0, spec.weight,
                                  spec.italic, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                  spec.family.c_str());
    if (font) fonts.emplace(key, font);
    return font;
}

void ClearFontCache() {
    for (const auto& [key, font] : fonts) DeleteObject(font);
    fonts.clear();
}

} // namespace wcw::paint
