#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Paint.h"

#include <algorithm>
#include <memory>
#include <new>

namespace wcw {
namespace {

constexpr wchar_t LabelClass[] = L"WcwLabel";
constexpr wchar_t ImageClass[] = L"WcwImageView";
constexpr wchar_t SeparatorClass[] = L"WcwSeparator";
constexpr wchar_t PanelClass[] = L"WcwPanel";

enum class DisplayKind { Label, Image, Separator, Panel };

struct DisplayState {
    DisplayKind kind{};
    ImageSource source{};
    ImageMode mode{};
    bool vertical{};
};

SIZE ImageSize(ImageSource source) {
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

void PaintDisplay(HWND window, const DisplayState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    paint::Buffer buffer(target, bounds);
    if (buffer) {
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
            paint::Text(buffer.dc(), std::wstring_view(text, length), bounds,
                        paint::Font(internal::ResolveLabelFont(theme, local), dpi),
                        IsWindowEnabled(window) ? style.text : style.disabledText,
                        DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
        } else if (state.kind == DisplayKind::Image && state.source.handle) {
            const int diameter = DipToPx(style.cornerRadiusDip * 2, dpi);
            const auto clip = radius > 0
                                  ? CreateRoundRectRgn(bounds.left, bounds.top, bounds.right + 1,
                                                       bounds.bottom + 1, diameter, diameter)
                                  : CreateRectRgnIndirect(&bounds);
            SelectClipRgn(buffer.dc(), clip);
            const auto destination = ImageBounds(bounds, ImageSize(state.source), state.mode);
            if (state.source.kind == ImageSource::Kind::Icon)
                paint::Icon(buffer.dc(), static_cast<HICON>(state.source.handle), destination);
            else
                paint::Bitmap(buffer.dc(), static_cast<HBITMAP>(state.source.handle), destination);
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
    if (internal::HandleControlMessage(window, message, shared)) return shared;
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

LRESULT CALLBACK DisplayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return DisplayProcImpl(window, message, wParam, lParam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return message == WM_NCCREATE ? FALSE : DefWindowProcW(window, message, wParam, lParam);
}

HWND Create(const wchar_t* className, const ControlOptions& options, DisplayState state) {
    if (!options.parent || !IsWindow(options.parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    const auto window = CreateWindowExW(
        0, className, options.text.c_str(), WS_CHILD | (options.style & ~WS_TABSTOP),
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(), &state);
    if (window) SetStyleOverride(window, options.appearance);
    return window;
}

} // namespace

HWND CreateLabel(const ControlOptions& options) {
    return Create(LabelClass, options, {DisplayKind::Label});
}

HWND CreateImageView(const ControlOptions& options, ImageSource source, ImageMode mode) {
    return Create(ImageClass, options, {DisplayKind::Image, source, mode});
}

HWND CreateSeparator(const ControlOptions& options, bool vertical) {
    return Create(SeparatorClass, options,
                  {DisplayKind::Separator, {}, ImageMode::Contain, vertical});
}

HWND CreatePanel(const ControlOptions& options) {
    return Create(PanelClass, options, {DisplayKind::Panel});
}

namespace internal {
FontSpec ResolveLabelFont(const Theme& theme, const StyleOverride& local) {
    return local.font.value_or(theme.label);
}

bool RegisterDisplayClasses() {
    return RegisterControlClass(LabelClass, DisplayProc) &&
           RegisterControlClass(ImageClass, DisplayProc) &&
           RegisterControlClass(SeparatorClass, DisplayProc) &&
           RegisterControlClass(PanelClass, DisplayProc);
}
} // namespace internal
} // namespace wcw
