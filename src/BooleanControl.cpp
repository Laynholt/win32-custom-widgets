#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Accessibility.h"
#include "Paint.h"

#include <algorithm>
#include <memory>
#include <new>
#include <windowsx.h>

namespace wcw {
namespace {

constexpr wchar_t CheckboxClass[] = L"WcwCheckbox";
constexpr wchar_t ToggleClass[] = L"WcwToggle";

struct BooleanCreate {
    const CheckableOptions* options;
    bool toggle;
};

struct BooleanState {
    bool checked{};
    bool toggle{};
    bool hover{};
    bool mousePressed{};
    bool keyboardPressed{};
};

bool IsBooleanWindow(HWND window) {
    wchar_t name[32]{};
    if (!IsWindow(window) || !GetClassNameW(window, name, 32) ||
        (wcscmp(name, CheckboxClass) != 0 && wcscmp(name, ToggleClass) != 0)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    return true;
}

bool Inside(HWND window, LPARAM position) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    return PtInRect(&bounds, {GET_X_LPARAM(position), GET_Y_LPARAM(position)}) != FALSE;
}

void NotifyChanged(HWND window, bool checked) {
    CheckChangedNotification notification{{window, static_cast<UINT_PTR>(GetDlgCtrlID(window)),
                                            WCN_CHECK_CHANGED}, checked};
    SendMessageW(GetParent(window), WM_NOTIFY, notification.header.idFrom,
                 reinterpret_cast<LPARAM>(&notification));
}

void SetValue(HWND window, BooleanState& state, bool checked, bool notify) {
    if (state.checked == checked) return;
    state.checked = checked;
    InvalidateRect(window, nullptr, FALSE);
    internal::NotifyAccessibility(window, EVENT_OBJECT_STATECHANGE);
    if (notify) NotifyChanged(window, checked);
}

void CancelPress(HWND window, BooleanState& state) {
    const bool changed = state.mousePressed || state.keyboardPressed;
    state.mousePressed = false;
    state.keyboardPressed = false;
    if (GetCapture() == window) ReleaseCapture();
    if (changed) InvalidateRect(window, nullptr, FALSE);
}

void PaintControl(HWND window, const BooleanState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    paint::Buffer buffer(target, bounds);
    if (buffer) {
        const auto dpi = paint::Dpi(window);
        const auto theme = GetTheme();
        const auto style = ResolveStyle(theme, internal::WindowStyleOverride(window));
        const auto enabled = IsWindowEnabled(window) != FALSE;
        paint::Clear(buffer.dc(), bounds, theme.palette.window);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        const float height = static_cast<float>(bounds.bottom - bounds.top);
        const float indicatorHeight = std::min(height, paint::ToPixels(style.indicatorSizeDip, dpi));
        const float indicatorWidth = state.toggle ? indicatorHeight * 1.8f : indicatorHeight;
        const Gdiplus::RectF indicator{2.0f, (height - indicatorHeight) / 2.0f,
                                      indicatorWidth, indicatorHeight};
        const auto active = !enabled ? style.disabledSurface
                          : state.mousePressed || state.keyboardPressed ? style.pressed
                          : state.checked ? style.accent
                          : state.hover ? style.hover : style.background;
        const float radius = paint::ToPixels(style.cornerRadiusDip, dpi);
        paint::Fill(graphics, indicator, radius, active);
        paint::Border(graphics, indicator, radius, style.border,
                      paint::ToPixels(style.borderWidthDip, dpi));

        if (state.toggle) {
            const float inset = indicatorHeight * .15f;
            const float diameter = indicatorHeight - inset * 2.0f;
            const float x = state.checked ? indicator.GetRight() - inset - diameter
                                          : indicator.X + inset;
            Gdiplus::SolidBrush thumb(paint::GdiPlusColor(enabled ? theme.palette.text
                                                                 : style.disabledText));
            graphics.FillEllipse(&thumb, x, indicator.Y + inset, diameter, diameter);
        } else if (state.checked) {
            Gdiplus::Pen check(paint::GdiPlusColor(theme.palette.window),
                               std::max(2.0f, paint::ToPixels(2, dpi)));
            check.SetStartCap(Gdiplus::LineCapRound);
            check.SetEndCap(Gdiplus::LineCapRound);
            const Gdiplus::PointF points[]{
                {indicator.X + indicator.Width * .22f, indicator.Y + indicator.Height * .52f},
                {indicator.X + indicator.Width * .43f, indicator.Y + indicator.Height * .72f},
                {indicator.X + indicator.Width * .78f, indicator.Y + indicator.Height * .30f}};
            graphics.DrawLines(&check, points, 3);
        }

        wchar_t text[512]{};
        const int length = GetWindowTextW(window, text, 512);
        if (length) {
            RECT textBounds{static_cast<LONG>(indicator.GetRight() +
                                              paint::ToPixels(style.spacingDip, dpi)),
                            bounds.top, bounds.right, bounds.bottom};
            paint::Text(buffer.dc(), std::wstring_view(text, length), textBounds,
                        paint::Font(style.font, dpi), enabled ? style.text : style.disabledText,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (GetFocus() == window) {
            const Gdiplus::RectF focus{0, 0, static_cast<float>(bounds.right), height};
            paint::Focus(graphics, focus, paint::ToPixels(style.cornerRadiusDip, dpi), style.focus,
                         paint::ToPixels(style.focusWidthDip, dpi));
        }
    }
    EndPaint(window, &ps);
}

LRESULT BooleanProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<BooleanState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto create = static_cast<const BooleanCreate*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<BooleanState>();
        created->checked = create->options->checked;
        created->toggle = create->toggle;
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
        return DLGC_BUTTON;
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
            const bool toggle = GetCapture() == window && Inside(window, lParam);
            state->mousePressed = false;
            if (GetCapture() == window) ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
            if (toggle) SetValue(window, *state, !state->checked, true);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state) CancelPress(window, *state);
        return 0;
    case WM_KEYDOWN:
        if (state && IsWindowEnabled(window) && wParam == VK_SPACE && !state->keyboardPressed) {
            state->keyboardPressed = true;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_KEYUP:
        if (state && wParam == VK_SPACE && state->keyboardPressed) {
            state->keyboardPressed = false;
            InvalidateRect(window, nullptr, FALSE);
            SetValue(window, *state, !state->checked, true);
        }
        return 0;
    case internal::BooleanSetMessage:
        if (state) SetValue(window, *state, wParam != 0, false);
        return state != nullptr;
    case internal::BooleanGetMessage:
        return state && state->checked;
    case WM_SETTEXT: {
        const auto result = DefWindowProcW(window, message, wParam, lParam);
        InvalidateRect(window, nullptr, FALSE);
        return result;
    }
    case WM_PAINT:
        if (state) PaintControl(window, *state);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK BooleanProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return BooleanProcImpl(window, message, wParam, lParam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return message == WM_NCCREATE ? FALSE : DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateBoolean(const wchar_t* className, const CheckableOptions& options, bool toggle) {
    if (!options.parent || !IsWindow(options.parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    const BooleanCreate create{&options, toggle};
    const auto window = CreateWindowExW(
        0, className, options.text.c_str(), WS_CHILD | WS_TABSTOP | options.style,
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(),
        const_cast<BooleanCreate*>(&create));
    if (window) SetStyleOverride(window, options.appearance);
    internal::RegisterAccessibility(window,
        toggle ? internal::AccessibleKind::Toggle : internal::AccessibleKind::Checkbox, options);
    return window;
}

} // namespace

HWND CreateCheckbox(const CheckableOptions& options) {
    return CreateBoolean(CheckboxClass, options, false);
}

HWND CreateToggle(const CheckableOptions& options) {
    return CreateBoolean(ToggleClass, options, true);
}

bool SetChecked(HWND control, bool checked) {
    return IsBooleanWindow(control) &&
           SendMessageW(control, internal::BooleanSetMessage, checked, 0) != FALSE;
}

bool GetChecked(HWND control) {
    return IsBooleanWindow(control) &&
           SendMessageW(control, internal::BooleanGetMessage, 0, 0) != FALSE;
}

namespace internal {
bool RegisterBooleanControlClasses() {
    return RegisterControlClass(CheckboxClass, BooleanProc) &&
           RegisterControlClass(ToggleClass, BooleanProc);
}
} // namespace internal
} // namespace wcw
