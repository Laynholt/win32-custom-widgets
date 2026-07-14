#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Accessibility.h"
#include "Paint.h"

#include <algorithm>
#include <memory>

namespace wcw {
namespace {

constexpr wchar_t DisplayClass[] = L"WcwDisplay";

enum class DisplayKind { Label, Image, Separator, Panel };

struct DisplayState {
    DisplayKind kind{};
    ImageSource source{};
    ImageMode mode{};
    bool vertical{};
};

SIZE ImageSize(ImageSource source) {
    if (source.kind == ImageSource::Kind::Builtin) return {16, 16};
    if (!source.handle) return {};
    if (source.kind == ImageSource::Kind::Bitmap) {
        BITMAP bitmap{};
        GetObjectW(static_cast<HBITMAP>(source.handle), sizeof(bitmap), &bitmap);
        return {bitmap.bmWidth, bitmap.bmHeight};
    }
    ICONINFO icon{};
    if (!GetIconInfo(static_cast<HICON>(source.handle), &icon)) return {};
    BITMAP bitmap{};
    GetObjectW(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(bitmap), &bitmap);
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    return {bitmap.bmWidth, icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2};
}

RECT ImageBounds(RECT bounds, SIZE source, ImageMode mode) {
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (mode == ImageMode::Stretch || source.cx <= 0 || source.cy <= 0) return bounds;
    const float scaleX = static_cast<float>(width) / source.cx;
    const float scaleY = static_cast<float>(height) / source.cy;
    const float scale = mode == ImageMode::Contain ? (std::min)(scaleX, scaleY)
                                                   : (std::max)(scaleX, scaleY);
    const int imageWidth = static_cast<int>(source.cx * scale);
    const int imageHeight = static_cast<int>(source.cy * scale);
    const int left = bounds.left + (width - imageWidth) / 2;
    const int top = bounds.top + (height - imageHeight) / 2;
    return {left, top, left + imageWidth, top + imageHeight};
}

RECT LabelTextBounds(HDC dc, RECT bounds, HFONT font, const ResolvedStyle& style,
                     unsigned dpi, std::wstring_view text) {
    const int paddingX = (std::max)(0, DipToPx(style.paddingXDip, dpi));
    const int paddingY = (std::max)(0, DipToPx(style.paddingYDip, dpi));
    RECT content{bounds.left + paddingX, bounds.top + paddingY,
                 (std::max)(bounds.left + paddingX, bounds.right - paddingX),
                 (std::max)(bounds.top + paddingY, bounds.bottom - paddingY)};
    RECT measured{0, 0, content.right - content.left, 0};
    const auto old = font ? SelectObject(dc, font) : nullptr;
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &measured,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    if (old) SelectObject(dc, old);
    const int height = (std::min)(measured.bottom - measured.top,
                                  content.bottom - content.top);
    content.top += (content.bottom - content.top - height) / 2;
    content.bottom = content.top + height;
    return content;
}

void PaintDisplay(HWND window, const DisplayState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    if (paint::Buffer buffer(target, bounds); buffer) {
        const auto dpi = paint::Dpi(window);
        const auto theme = GetTheme();
        const auto local = internal::WindowStyleOverride(window);
        const auto style = ResolveStyle(theme, local);
        const auto width = static_cast<float>(bounds.right);
        const auto height = static_cast<float>(bounds.bottom);
        const Gdiplus::RectF shape{0, 0, width, height};
        const auto radius = paint::ToPixels(style.cornerRadiusDip, dpi);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        paint::Clear(buffer.dc(), bounds, theme.palette.window);

        const auto background = state.kind == DisplayKind::Panel
                                    ? local.background.value_or(theme.palette.panel)
                                    : style.background;
        paint::Fill(graphics, shape, radius, background);
        if (state.kind == DisplayKind::Panel)
            paint::Border(graphics, shape, radius, style.border,
                          paint::ToPixels(style.borderWidthDip, dpi));

        if (state.kind == DisplayKind::Label) {
            wchar_t text[1024]{};
            const int length = GetWindowTextW(window, text, 1024);
            const std::wstring_view label(text, length);
            const auto font = paint::Font(local.font.value_or(theme.label), dpi);
            const auto textBounds = LabelTextBounds(buffer.dc(), bounds, font, style, dpi, label);
            paint::Text(buffer.dc(), label, textBounds, font,
                        IsWindowEnabled(window) ? style.text : style.disabledText,
                        DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else if (state.kind == DisplayKind::Image &&
                   (state.source.kind == ImageSource::Kind::Builtin || state.source.handle)) {
            const int diameter = DipToPx(style.cornerRadiusDip * 2, dpi);
            const auto clip = radius > 0
                                  ? CreateRoundRectRgn(bounds.left, bounds.top, bounds.right + 1,
                                                       bounds.bottom + 1, diameter, diameter)
                                  : CreateRectRgnIndirect(&bounds);
            SelectClipRgn(buffer.dc(), clip);
            const auto destination = ImageBounds(bounds, ImageSize(state.source), state.mode);
            paint::Image(buffer.dc(), state.source, destination, style.text);
            SelectClipRgn(buffer.dc(), nullptr);
            DeleteObject(clip);
        } else if (state.kind == DisplayKind::Separator) {
            const auto pen = CreatePen(PS_SOLID,
                                       (std::max)(1, static_cast<int>(paint::ToPixels(
                                                         style.borderWidthDip, dpi))),
                                       RGB(style.border.r, style.border.g, style.border.b));
            const auto old = SelectObject(buffer.dc(), pen);
            if (state.vertical) {
                const int x = bounds.right / 2;
                MoveToEx(buffer.dc(), x, bounds.top, nullptr);
                LineTo(buffer.dc(), x, bounds.bottom);
            } else {
                const int y = bounds.bottom / 2;
                MoveToEx(buffer.dc(), bounds.left, y, nullptr);
                LineTo(buffer.dc(), bounds.right, y);
            }
            SelectObject(buffer.dc(), old);
            DeleteObject(pen);
        }
    }
    EndPaint(window, &ps);
}

LRESULT DisplayProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<DisplayState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto supplied = static_cast<const DisplayState*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<DisplayState>(*supplied);
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETFOCUS:
        SetFocus(GetParent(window));
        return 0;
    case WM_PAINT:
        if (state) PaintDisplay(window, *state);
        return 0;
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

HWND Create(const ControlOptions& options, DisplayState state) {
    if (!options.parent || !IsWindow(options.parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    const auto window = CreateWindowExW(
        0, DisplayClass, options.text.c_str(), WS_CHILD | (options.style & ~WS_TABSTOP),
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(), &state);
    if (window) SetStyleOverride(window, options.appearance);
    internal::AccessibleKind accessibleKind{};
    switch (state.kind) {
    case DisplayKind::Label: accessibleKind = internal::AccessibleKind::Label; break;
    case DisplayKind::Image: accessibleKind = internal::AccessibleKind::Image; break;
    case DisplayKind::Separator: accessibleKind = internal::AccessibleKind::Separator; break;
    case DisplayKind::Panel: accessibleKind = internal::AccessibleKind::Panel; break;
    }
    internal::RegisterAccessibility(window, accessibleKind, options);
    return window;
}

} // namespace

HWND CreateLabel(const ControlOptions& options) {
    return Create(options, {DisplayKind::Label});
}

HWND CreateImageView(const ControlOptions& options, ImageSource source, ImageMode mode) {
    return Create(options, {DisplayKind::Image, source, mode});
}

HWND CreateSeparator(const ControlOptions& options, bool vertical) {
    return Create(options,
                  {DisplayKind::Separator, {}, ImageMode::Contain, vertical});
}

HWND CreatePanel(const ControlOptions& options) {
    return Create(options, {DisplayKind::Panel});
}

namespace internal {
bool RegisterDisplayClasses() {
    return RegisterControlClass(DisplayClass, SafeWindowProc<DisplayProcImpl>);
}
} // namespace internal
} // namespace wcw
