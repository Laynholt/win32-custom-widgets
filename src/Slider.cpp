#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Accessibility.h"
#include "Paint.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <windowsx.h>

namespace wcw {
namespace {

constexpr wchar_t SliderClass[] = L"WcwSlider";

struct SliderState {
    double minimum{};
    double maximum{};
    double step{};
    double value{};
    StyleOverride trackAppearance;
    StyleOverride thumbAppearance;
    int wheelRemainder{};
    bool dragging{};
};

bool IsSliderWindow(HWND window) {
    return internal::IsLibraryWindow(window, SliderClass);
}

double Normalize(const SliderState& state, double value, bool snap) {
    value = std::clamp(value, state.minimum, state.maximum);
    if (!snap || value == state.minimum || value == state.maximum) return value;
    const auto steps = std::round((value - state.minimum) / state.step);
    return std::clamp(state.minimum + steps * state.step, state.minimum, state.maximum);
}

void NotifyChanged(HWND window, double value) {
    ValueChangedNotification notification{{window, static_cast<UINT_PTR>(GetDlgCtrlID(window)),
                                            WCN_VALUE_CHANGED}, value};
    SendMessageW(GetParent(window), WM_NOTIFY, notification.header.idFrom,
                 reinterpret_cast<LPARAM>(&notification));
}

bool SetValue(HWND window, SliderState& state, double value, bool notify, bool snap = true) {
    const auto normalized = Normalize(state, value, snap);
    if (normalized == state.value) return false;
    state.value = normalized;
    InvalidateRect(window, nullptr, FALSE);
    internal::NotifyAccessibility(window, EVENT_OBJECT_VALUECHANGE);
    if (notify) NotifyChanged(window, normalized);
    return true;
}

SliderGeometry Geometry(HWND window, const SliderState& state) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    const auto dpi = paint::Dpi(window);
    const auto theme = GetTheme();
    const auto base = internal::WindowStyleOverride(window);
    const auto track = ResolveStyle(theme, internal::OverlayStyle(base, state.trackAppearance));
    const auto thumb = ResolveStyle(theme, internal::OverlayStyle(base, state.thumbAppearance));
    return {static_cast<float>(bounds.right), static_cast<float>(bounds.bottom),
            paint::ToPixels(thumb.thumbSizeDip, dpi),
            paint::ToPixels(track.trackThicknessDip, dpi),
            static_cast<float>(state.minimum), static_cast<float>(state.maximum),
            static_cast<float>(state.value)};
}

void SetFromMouse(HWND window, SliderState& state, int x) {
    const auto geometry = Geometry(window, state);
    SetValue(window, state,
             geometry.ValueAt(static_cast<float>(x), static_cast<float>(state.minimum),
                              static_cast<float>(state.maximum)), true);
}

void PaintSlider(HWND window, const SliderState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    if (paint::Buffer buffer(target, bounds); buffer) {
        const auto dpi = paint::Dpi(window);
        const auto theme = GetTheme();
        const auto base = internal::WindowStyleOverride(window);
        const auto trackStyle = ResolveStyle(theme, internal::OverlayStyle(base, state.trackAppearance));
        const auto thumbStyle = ResolveStyle(theme, internal::OverlayStyle(base, state.thumbAppearance));
        const auto enabled = IsWindowEnabled(window) != FALSE;
        paint::Clear(buffer.dc(), bounds, theme.palette.window);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        const float height = static_cast<float>(bounds.bottom);
        const auto geometry = Geometry(window, state);
        const Gdiplus::RectF track{geometry.trackStartPx,
                                   (height - geometry.trackThicknessPx) / 2.0f,
                                   geometry.trackEndPx - geometry.trackStartPx,
                                   geometry.trackThicknessPx};
        const auto trackRadius = paint::ToPixels(trackStyle.cornerRadiusDip, dpi);
        paint::Fill(graphics, track, trackRadius,
                    enabled ? trackStyle.background : trackStyle.disabledSurface);
        if (geometry.thumbPx > geometry.trackStartPx) {
            const Gdiplus::RectF selected{geometry.trackStartPx, track.Y,
                                         geometry.thumbPx - geometry.trackStartPx, track.Height};
            paint::Fill(graphics, selected, trackRadius,
                        enabled ? trackStyle.accent : trackStyle.disabledText);
        }
        const float thumbRadius = geometry.thumbSizePx / 2.0f;
        const Gdiplus::RectF thumb{geometry.thumbPx - thumbRadius,
                                   height / 2.0f - thumbRadius,
                                   geometry.thumbSizePx, geometry.thumbSizePx};
        paint::Fill(graphics, thumb, paint::ToPixels(thumbStyle.cornerRadiusDip, dpi),
                    enabled ? thumbStyle.accent : thumbStyle.disabledText);
        if (GetFocus() == window) {
            paint::Border(graphics, thumb, paint::ToPixels(thumbStyle.cornerRadiusDip, dpi),
                         thumbStyle.focus, paint::ToPixels(thumbStyle.focusWidthDip, dpi));
        }
    }
    EndPaint(window, &ps);
}

LRESULT SliderProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<SliderState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto options = static_cast<const SliderOptions*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<SliderState>();
        created->minimum = options->minimum;
        created->maximum = options->maximum;
        created->step = options->step;
        created->trackAppearance = options->trackAppearance;
        created->thumbAppearance = options->thumbAppearance;
        created->value = Normalize(*created, options->value, true);
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_NCDESTROY:
        if (GetCapture() == window) ReleaseCapture();
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ENABLE:
        if (state && !wParam && state->dragging) {
            state->dragging = false;
            if (GetCapture() == window) ReleaseCapture();
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (state) state->dragging = false;
        if (message == WM_CANCELMODE && GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_LBUTTONDOWN:
        if (state && IsWindowEnabled(window)) {
            SetFocus(window);
            SetCapture(window);
            state->dragging = true;
            SetFromMouse(window, *state, GET_X_LPARAM(lParam));
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state && state->dragging && GetCapture() == window)
            SetFromMouse(window, *state, GET_X_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (state && state->dragging) {
            if (GetCapture() == window) SetFromMouse(window, *state, GET_X_LPARAM(lParam));
            state->dragging = false;
            if (GetCapture() == window) ReleaseCapture();
        }
        return 0;
    case WM_KEYDOWN:
        if (!state || !IsWindowEnabled(window)) return 0;
        switch (wParam) {
        case VK_LEFT:
        case VK_DOWN: SetValue(window, *state, state->value - state->step, true, false); break;
        case VK_RIGHT:
        case VK_UP: SetValue(window, *state, state->value + state->step, true, false); break;
        case VK_PRIOR: SetValue(window, *state, state->value + state->step * 10, true, false); break;
        case VK_NEXT: SetValue(window, *state, state->value - state->step * 10, true, false); break;
        case VK_HOME: SetValue(window, *state, state->minimum, true); break;
        case VK_END: SetValue(window, *state, state->maximum, true); break;
        default: break;
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state && IsWindowEnabled(window)) {
            state->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
            const int notches = state->wheelRemainder / WHEEL_DELTA;
            state->wheelRemainder %= WHEEL_DELTA;
            if (notches)
                SetValue(window, *state, state->value + notches * state->step, true, false);
        }
        return 0;
    case internal::SliderSetValueMessage:
        if (state && lParam) return SetValue(window, *state, *reinterpret_cast<double*>(lParam), false) || TRUE;
        return FALSE;
    case internal::SliderGetValueMessage:
        if (state && lParam) {
            *reinterpret_cast<std::optional<double>*>(lParam) = state->value;
            return TRUE;
        }
        return FALSE;
    case WM_PAINT:
        if (state) PaintSlider(window, *state);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

HWND CreateSlider(const SliderOptions& options) {
    if (!options.parent || !IsWindow(options.parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    if (!std::isfinite(options.minimum) || !std::isfinite(options.maximum) ||
        !std::isfinite(options.step) || !std::isfinite(options.value) || options.step <= 0 ||
        options.minimum >= options.maximum) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    const auto window = CreateWindowExW(
        0, SliderClass, L"", WS_CHILD | WS_TABSTOP | options.style,
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(),
        const_cast<SliderOptions*>(&options));
    if (window) SetStyleOverride(window, options.appearance);
    internal::RegisterAccessibility(window, internal::AccessibleKind::Slider, options);
    return window;
}

bool SetSliderValue(HWND slider, double value) {
    if (!IsSliderWindow(slider)) return false;
    if (!std::isfinite(value)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    return SendMessageW(slider, internal::SliderSetValueMessage, 0,
                        reinterpret_cast<LPARAM>(&value)) != FALSE;
}

std::optional<double> GetSliderValue(HWND slider) {
    std::optional<double> value;
    if (IsSliderWindow(slider))
        SendMessageW(slider, internal::SliderGetValueMessage, 0, reinterpret_cast<LPARAM>(&value));
    return value;
}

namespace internal {
bool RegisterSliderClass() {
    return RegisterControlClass(SliderClass, SafeWindowProc<SliderProcImpl>);
}
} // namespace internal
} // namespace wcw
