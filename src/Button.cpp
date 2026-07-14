#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Accessibility.h"
#include "Paint.h"

#include <algorithm>
#include <memory>
#include <windowsx.h>

namespace wcw {
namespace {

constexpr wchar_t ButtonClass[] = L"WcwButton";

struct ButtonState {
    HICON icon{};
    HBITMAP bitmap{};
    float iconSizeDip{};
    UINT alignment{};
    bool isDefault{};
    bool isCancel{};
    bool hover{};
    bool mousePressed{};
    WPARAM keyboardPressedKey{};
};

bool Inside(HWND window, LPARAM position) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    return PtInRect(&bounds, {GET_X_LPARAM(position), GET_Y_LPARAM(position)});
}

void NotifyClicked(HWND window) {
    if (!IsWindowEnabled(window)) return;
    SendMessageW(GetParent(window), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(window), BN_CLICKED), reinterpret_cast<LPARAM>(window));
}

void CancelPress(HWND window, ButtonState& state) {
    const bool changed = state.mousePressed || state.keyboardPressedKey;
    state.mousePressed = false;
    state.keyboardPressedKey = 0;
    if (GetCapture() == window) ReleaseCapture();
    if (changed) InvalidateRect(window, nullptr, FALSE);
}

void PaintButton(HWND window, ButtonState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    if (paint::Buffer buffer(target, bounds); buffer) {
        const auto dpi = paint::Dpi(window);
        const auto theme = GetTheme();
        const auto style = ResolveStyle(theme, internal::WindowStyleOverride(window));
        const auto width = static_cast<float>(bounds.right - bounds.left);
        const auto height = static_cast<float>(bounds.bottom - bounds.top);
        const Gdiplus::RectF shape{0, 0, width, height};
        const auto radius = paint::ToPixels(style.cornerRadiusDip, dpi);
        const auto enabled = IsWindowEnabled(window) != FALSE;
        const auto background = !enabled ? style.disabledSurface
                              : (state.mousePressed || state.keyboardPressedKey) ? style.pressed
                              : state.hover   ? style.hover
                                              : style.background;
        paint::Clear(buffer.dc(), bounds, theme.palette.window);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        paint::Fill(graphics, shape, radius, background);
        paint::Border(graphics, shape, radius, style.border,
                      paint::ToPixels(style.borderWidthDip, dpi));

        RECT content = bounds;
        const int padding = DipToPx(style.paddingXDip, dpi);
        InflateRect(&content, -padding, 0);
        const int iconSize = DipToPx(state.iconSizeDip, dpi);
        const bool hasImage = state.icon || state.bitmap;
        const int gap = hasImage && GetWindowTextLengthW(window) ?
                            DipToPx(style.spacingDip, dpi) : 0;
        int imageLeft = content.left;
        if (state.alignment & DT_CENTER)
            imageLeft = (bounds.right - iconSize - gap -
                         (GetWindowTextLengthW(window) ? bounds.right / 3 : 0)) / 2;
        else if (state.alignment & DT_RIGHT)
            imageLeft = content.right - iconSize;
        const RECT imageBounds{imageLeft, (bounds.bottom - iconSize) / 2,
                               imageLeft + iconSize, (bounds.bottom + iconSize) / 2};
        if (state.icon) paint::Icon(buffer.dc(), state.icon, imageBounds);
        if (state.bitmap) paint::Bitmap(buffer.dc(), state.bitmap, imageBounds);

        wchar_t text[512]{};
        const int length = GetWindowTextW(window, text, 512);
        if (length) {
            RECT textBounds = content;
            if (hasImage) textBounds.left = imageBounds.right + gap;
            paint::Text(buffer.dc(), std::wstring_view(text, length), textBounds,
                        paint::Font(style.font, dpi), enabled ? style.text : style.disabledText,
                        state.alignment | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (GetFocus() == window)
            paint::Border(graphics, shape, radius, style.focus,
                         paint::ToPixels(style.focusWidthDip, dpi));
    }
    EndPaint(window, &ps);
}

LRESULT ButtonProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<ButtonState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto options = static_cast<const ButtonOptions*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<ButtonState>(ButtonState{
            options->icon, options->bitmap, options->iconSizeDip, options->alignment,
            options->isDefault, options->isCancel});
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_NCDESTROY:
        if (state) CancelPress(window, *state);
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_GETDLGCODE:
        return DLGC_BUTTON |
               (state && state->isDefault ? DLGC_DEFPUSHBUTTON : DLGC_UNDEFPUSHBUTTON);
    case WM_SETFOCUS:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
    case WM_CANCELMODE:
        if (state) CancelPress(window, *state);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ENABLE:
        if (state && !wParam) CancelPress(window, *state);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (state && !state->hover) {
            state->hover = true;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window};
            TrackMouseEvent(&tracking);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state) state->hover = false;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        if (state && IsWindowEnabled(window)) {
            SetFocus(window);
            SetCapture(window);
            state->mousePressed = true;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state && state->mousePressed) {
            const bool activate = GetCapture() == window && Inside(window, lParam);
            state->mousePressed = false;
            if (GetCapture() == window) ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
            if (activate) NotifyClicked(window);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state) CancelPress(window, *state);
        return 0;
    case BM_CLICK:
        NotifyClicked(window);
        return 0;
    case internal::ButtonGetPressedMessage:
        return state && (state->mousePressed || state->keyboardPressedKey);
    case WM_KEYDOWN:
        if (state && IsWindowEnabled(window) &&
            (wParam == VK_SPACE || wParam == VK_RETURN || (state->isCancel && wParam == VK_ESCAPE)) &&
            !state->keyboardPressedKey) {
            state->keyboardPressedKey = wParam;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_KEYUP:
        if (state && state->keyboardPressedKey == wParam) {
            state->keyboardPressedKey = 0;
            InvalidateRect(window, nullptr, FALSE);
            NotifyClicked(window);
        }
        return 0;
    case WM_PAINT:
        if (state) PaintButton(window, *state);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

HWND Create(const ButtonOptions& options) {
    if (!options.parent || !IsWindow(options.parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    const int id = options.isCancel ? IDCANCEL : options.id;
    const auto window = CreateWindowExW(
        0, ButtonClass, options.text.c_str(), WS_CHILD | WS_TABSTOP | options.style,
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), internal::Instance(),
        const_cast<ButtonOptions*>(&options));
    if (window && !options.appearance.background.has_value() && options.isDefault) {
        auto appearance = options.appearance;
        appearance.background = GetTheme().palette.accent;
        SetStyleOverride(window, appearance);
    } else if (window) {
        SetStyleOverride(window, options.appearance);
    }
    if (window && options.isDefault) SendMessageW(options.parent, DM_SETDEFID, id, 0);
    internal::RegisterAccessibility(window, internal::AccessibleKind::Button, options);
    return window;
}

} // namespace

HWND CreateButton(const ButtonOptions& options) { return Create(options); }

namespace internal {
bool RegisterButtonClasses() {
    return RegisterControlClass(ButtonClass, SafeWindowProc<ButtonProcImpl>);
}
} // namespace internal
} // namespace wcw
